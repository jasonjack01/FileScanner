// FileScannerDLL.cpp
#include "FileScannerDLL.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

using namespace std;

// 核心扫描引擎类（采用单例模式设计）
class DirectoryScanner {
public:
  // 获取全局唯一的 DirectoryScanner 实例（Meyers' Singleton）
  static DirectoryScanner &Instance() {
    static DirectoryScanner
        instance; // 局部静态变量，在第一次调用时初始化，线程安全
    return instance;
  }

  // 启动异步扫描流程
  void Start(wstring root, ProgressCallback pCb, CompleteCallback cCb,
             void *ctx) {
    Stop();                  // 若先前有扫描正在进行，先强制停止并回收旧线程
    m_rootPath = root;       // 保存要扫描的根目录
    m_progressCb = pCb;      // 保存进度回调函数指针
    m_completeCb = cCb;      // 保存完成回调函数指针
    m_userContext = ctx;     // 保存上下文指针（通常传 UI 窗口指针）
    m_stopRequested = false; // 重置停止请求标志
    m_activeWorkers = 0;     // 重置活跃工作线程计数

    // 加锁清空上一轮残留的文件结果集
    {
      lock_guard<mutex> lock(m_resultsMutex);
      m_results.clear();
    }

    // 加锁初始化任务队列，将根目录压入队列作为第一个待扫描任务
    {
      lock_guard<mutex> lock(m_queueMutex);
      queue<wstring> empty;
      swap(m_dirQueue, empty);
      m_dirQueue.push(m_rootPath);
    }

    m_isScanning = true; // 标记当前状态为正在扫描

    // 获取当前 CPU 硬件支持的并发线程数，若获取失败则默认设为 4
    unsigned int threadCount = thread::hardware_concurrency();
    threadCount = (threadCount == 0) ? 4 : threadCount;

    // 循环创建多个工作线程，组成线程池并行消费任务
    for (unsigned int i = 0; i < threadCount; ++i) {
      m_workers.emplace_back(&DirectoryScanner::WorkerThread, this);
    }
  }

  // 停止扫描任务
  void Stop() {
    if (!m_isScanning)
      return;               // 如果本来就没有在扫描，直接返回
    m_stopRequested = true; // 设置停止标志为真，通知所有线程退出
    m_cv.notify_all();      // 唤醒所有因等待队列而阻塞的线程

    // 循环等待所有工作线程安全退出并回收资源
    for (auto &t : m_workers) {
      if (t.joinable()) {
        t.join();
      }
    }
    m_workers.clear();    // 清空线程容器
    m_isScanning = false; // 标记扫描已结束
  }

  // 获取当前已扫描到的文件总数（线程安全）
  int GetCount() {
    lock_guard<mutex> lock(m_resultsMutex);
    return static_cast<int>(m_results.size());
  }

  // 根据索引获取指定的文件信息（线程安全）
  bool GetItem(int index, FileItem *outItem) {
    lock_guard<mutex> lock(m_resultsMutex);
    if (index < 0 || index >= m_results.size())
      return false; // 越界检查
    *outItem = m_results[index];
    return true;
  }

  // 清空内存中的扫描结果
  void ClearResults() {
    lock_guard<mutex> lock(m_resultsMutex);
    vector<FileItem>().swap(m_results); // 通过 swap 释放 vector 占用的内存
  }

private:
  // 私有化构造函数，防止外部直接 new
  DirectoryScanner()
      : m_isScanning(false), m_stopRequested(false), m_activeWorkers(0) {}
  // 析构函数调用 Stop，确保对象销毁时清理所有后台线程
  ~DirectoryScanner() { Stop(); }

  // 工作线程的主体函数（消费者逻辑）
  void WorkerThread() {
    m_activeWorkers++; // 活跃线程数加 1
    while (!m_stopRequested) {
      wstring dir;
      {
        // 加锁获取任务队列中的目录
        unique_lock<mutex> lock(m_queueMutex);
        // 使用条件变量等待：当队列有任务 或 收到停止请求时被唤醒
        m_cv.wait(lock,
                  [this] { return !m_dirQueue.empty() || m_stopRequested; });

        if (m_stopRequested)
          break; // 如果收到停止请求，跳出循环

        // 如果队列空了，且只剩下最后一个活跃线程，说明所有任务处理完毕，退出循环
        if (m_dirQueue.empty()) {
          if (m_activeWorkers <= 1) {
            break;
          }
          continue;
        }

        // 从队列头部取出一个待扫描的目录路径
        dir = m_dirQueue.front();
        m_dirQueue.pop();
      }

      // 执行具体的目录扫描操作
      ScanDirectory(dir);

      // 检查任务是否全部处理完，若队列空且仅剩最后一个线程在收尾，则广播唤醒
      {
        lock_guard<mutex> lock(m_queueMutex);
        if (m_dirQueue.empty() && m_activeWorkers == 1) {
          m_cv.notify_all();
        }
      }
    }
    m_activeWorkers--; // 当前线程即将退出，活跃线程数减 1

    // 如果所有线程都已退出且不是人为中止，则触发完成回调
    if (m_activeWorkers == 0 && !m_stopRequested) {
      m_isScanning = false;
      if (m_completeCb) {
        lock_guard<mutex> lock(m_resultsMutex);
        uint64_t totalSize = 0;
        for (const auto &item : m_results)
          totalSize += item.fileSize; // 计算总大小
        m_completeCb(static_cast<int>(m_results.size()), totalSize,
                     m_userContext); // 掉用回调
      }
    }
  }

  // 扫描单个目录下的文件与子文件夹
  void ScanDirectory(const wstring &dirPath) {
    wstring searchPath = dirPath + L"\\*"; // 构造 Win32 查找通配符路径
    WIN32_FIND_DATAW findData;
    // 使用 Windows API 快速查找第一个文件或文件夹
    HANDLE hFind = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic,
                                    &findData, FindExSearchNameMatch, NULL, 0);

    if (hFind == INVALID_HANDLE_VALUE)
      return; // 句柄无效则直接返回

    vector<wstring> subDirs;     // 存储当前目录下发现的子文件夹
    vector<FileItem> localBatch; // 临时存储当前目录下发现的文件

    do {
      if (m_stopRequested)
        break; // 循环中随时响应停止请求

      const wchar_t *name = findData.cFileName;
      // 过滤掉当前目录 (.) 和父目录 (..)
      if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
        continue;

      wstring fullPath = dirPath + L"\\" + name; // 拼接完整路径

      // 判断是文件夹还是文件
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        subDirs.push_back(fullPath); // 若是文件夹，放入子目录列表
      } else {
        FileItem item;
        wcscpy_s(item.filePath, fullPath.c_str()); // 拷贝文件路径
        // 合成 64 位文件大小（高 32 位左移 32 位后与低 32 位按位或）
        item.fileSize = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) |
                        findData.nFileSizeLow;

        localBatch.push_back(item); // 放入批处理列表

        // 若注册了进度回调，立刻将文件信息推送给上层
        if (m_progressCb) {
          m_progressCb(item.filePath, item.fileSize, m_userContext);
        }
      }
    } while (FindNextFileW(hFind, &findData)); // 循环查找下一个文件

    FindClose(hFind); // 关闭查找句柄，释放系统资源

    // 将当前目录扫描到的文件批量追加到全局结果容器中（加锁保护）
    if (!localBatch.empty()) {
      lock_guard<mutex> lock(m_resultsMutex);
      m_results.insert(m_results.end(), localBatch.begin(), localBatch.end());
    }

    // 将发现的子文件夹批量压入全局任务队列，并唤醒后台线程继续处理
    if (!subDirs.empty()) {
      lock_guard<mutex> lock(m_queueMutex);
      for (const auto &sub : subDirs) {
        m_dirQueue.push(sub);
      }
      m_cv.notify_all(); // 唤醒等待的线程来消费新任务
    }
  }

  wstring m_rootPath;                      // 根目录路径
  ProgressCallback m_progressCb = nullptr; // 进度回调函数指针
  CompleteCallback m_completeCb = nullptr; // 完成回调函数指针
  void *m_userContext = nullptr;           // 用户上下文指针

  atomic<bool> m_isScanning;    // 原子布尔值：标志是否正在扫描
  atomic<bool> m_stopRequested; // 原子布尔值：停止请求标志
  atomic<int> m_activeWorkers;  // 原子整数：当前活跃的工作线程数

  vector<thread> m_workers;  // 工作线程池容器
  queue<wstring> m_dirQueue; // 待扫描的目录任务队列
  mutex m_queueMutex;        // 保护任务队列的互斥锁
  condition_variable m_cv;   // 条件变量：用于线程同步与唤醒

  vector<FileItem> m_results; // 存储所有扫描结果的容器
  mutex m_resultsMutex;       // 保护结果容器的互斥锁
};

// 导出给 C++ / C 调用的标准 C 接口边界
extern "C" {
// 异步开始扫描接口
DllExport bool StartScanAsync(const wchar_t *rootPath, ProgressCallback pCb,
                              CompleteCallback cCb, void *userContext) {
  if (!rootPath)
    return false;
  DirectoryScanner::Instance().Start(rootPath, pCb, cCb, userContext);
  return true;
}

// 停止扫描接口
DllExport void StopScan() { DirectoryScanner::Instance().Stop(); }

// 获取结果总数接口
DllExport int GetResultsCount() {
  return DirectoryScanner::Instance().GetCount();
}

// 获取指定索引处结果接口
DllExport bool GetResultAt(int index, FileItem *outItem) {
  return DirectoryScanner::Instance().GetItem(index, outItem);
}

// 释放结果内存接口
DllExport void FreeResults() { DirectoryScanner::Instance().ClearResults(); }
}
