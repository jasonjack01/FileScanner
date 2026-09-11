// FileScannerDLL.h
#pragma once // 防止头文件被重复包含

#ifdef DllExport
#else
#define DllExport                                                              \
  __declspec(dllexport) // 若未定义 DllExport 宏，则默认定义为导出动态库符号
#endif

#include <cstdint> // 引入标准固定宽度整型头文件（如 uint64_t）

#ifdef __cplusplus
extern "C" { // 以 C 语言的符号修饰规范编译内部函数，防止 C++ 的名称修饰（Name
             // Mangling）导致跨语言调用失败
#endif

// 定义单个文件信息的结构体，用于跨边界安全传输数据
struct FileItem {
  wchar_t filePath[260]; // 存储文件绝对路径的宽字符数组（支持中文，260对应传统
                         // MAX_PATH）
  uint64_t fileSize; // 存储文件大小的 64 位无符号整数（支持超大文件）
};

// 定义进度回调函数的指针类型：当后台扫描到一个文件时调用，用于实时通知上层
typedef void (*ProgressCallback)(const wchar_t *filePath, uint64_t fileSize,
                                 void *userContext);

// 定义完成回调函数的指针类型：当整个扫描任务结束时调用，用于传递汇总数据
typedef void (*CompleteCallback)(int totalFiles, uint64_t totalSize,
                                 void *userContext);

// 声明外部可调用的 DLL 导出函数：异步启动扫描任务
DllExport bool StartScanAsync(const wchar_t *rootPath, ProgressCallback pCb,
                              CompleteCallback cCb, void *userContext);

// 声明外部可调用的 DLL 导出函数：停止当前的扫描任务
DllExport void StopScan();

// 声明外部可调用的 DLL 导出函数：获取当前已扫描到的文件总数
DllExport int GetResultsCount();

// 声明外部可调用的 DLL 导出函数：根据索引安全获取指定的文件信息
DllExport bool GetResultAt(int index, FileItem *outItem);

// 声明外部可调用的 DLL 导出函数：清空内存中保存的所有扫描结果
DllExport void FreeResults();

#ifdef __cplusplus
} // 结束 extern "C"
#endif
