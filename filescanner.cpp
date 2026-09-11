#include "filescanner.h"
#include "ui_filescanner.h" // 编译时由 uic 自动生成的 UI 头文件

#include "FileScannerDLL.h"
#include <QFileDialog>
#include <QHeaderView>
#include <QMetaType>
MainWindow::MainWindow(QMainWindow *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this); // 初始化 UI 控件

  // 初始化控件状态
  ui->m_btnStop->setEnabled(false);
  ui->m_tableWidget->setHorizontalHeaderLabels({"文件路径", "大小 (Bytes)"});
  //  设置表格列宽比例：第 0 列占 80%，第 1 列占 20%
  QHeaderView *header = ui->m_tableWidget->horizontalHeader();
  // 1. 设置两列都为 Interactive（允许用户手动拖拽调整列宽）
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->setSectionResizeMode(1, QHeaderView::Interactive);
  // 初始设定第 1 列的宽度（假设表格总宽约 600 像素，20% 约为 120
  // 像素，也可以在后面 resize 时动态算）
  ui->m_tableWidget->setColumnWidth(1, 120);
  // 绑定“浏览”按钮点击事件
  connect(ui->btnBrowse, &QPushButton::clicked, [=]() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!dir.isEmpty())
      ui->m_pathEdit->setText(dir);
  });

  // 绑定开始和停止按钮
  connect(ui->m_btnStart, &QPushButton::clicked, this,
          &MainWindow::onStartScan);
  connect(ui->m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopScan);

  // 注册 uint64_t 类型用于跨线程信号槽
  qRegisterMetaType<uint64_t>("uint64_t");

  // 跨线程信号槽连接
  connect(this, &MainWindow::sigProgress, this, &MainWindow::onUpdateProgress,
          Qt::QueuedConnection);
  connect(this, &MainWindow::sigComplete, this, &MainWindow::onScanComplete,
          Qt::QueuedConnection);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::onStartScan() {
  QString path = ui->m_pathEdit->text();
  if (path.isEmpty())
    return;

  ui->m_tableWidget->setRowCount(0);
  FreeResults();
  ui->m_btnStart->setEnabled(false);
  ui->m_btnStop->setEnabled(true);
  ui->m_statusLabel->setText("正在扫描...");

  StartScanAsync(
      path.toStdWString().c_str(),
      [](const wchar_t *filePath, uint64_t fileSize, void *ctx) {
        MainWindow *self = static_cast<MainWindow *>(ctx);
        emit self->sigProgress(QString::fromWCharArray(filePath), fileSize);
      },
      [](int totalFiles, uint64_t totalSize, void *ctx) {
        MainWindow *self = static_cast<MainWindow *>(ctx);
        emit self->sigComplete(totalFiles, totalSize);
      },
      this);
}

void MainWindow::onStopScan() {
  StopScan();
  ui->m_btnStart->setEnabled(true);
  ui->m_btnStop->setEnabled(false);
  ui->m_statusLabel->setText("已手动终止。");
}

void MainWindow::onUpdateProgress(QString path, uint64_t size) {
  // 限制标签显示的最大宽度，超出部分显示省略号
  QFontMetrics metrics(ui->m_statusLabel->font());
  QString elidedPath =
      metrics.elidedText(path, Qt::ElideMiddle, ui->m_statusLabel->width());
  ui->m_statusLabel->setText(QString("正在扫描: %1").arg(path));
  int row = ui->m_tableWidget->rowCount();
  if (row < 1000) {
    ui->m_tableWidget->insertRow(row);
    ui->m_tableWidget->setItem(row, 0, new QTableWidgetItem(path));
    ui->m_tableWidget->setItem(row, 1,
                               new QTableWidgetItem(QString::number(size)));
  }
}

void MainWindow::onScanComplete(int totalFiles, uint64_t totalSize) {
  ui->m_btnStart->setEnabled(true);
  ui->m_btnStop->setEnabled(false);
  ui->m_statusLabel->setText(
      QString("扫描完成！共找到 %1 个文件，总大小: %2 字节")
          .arg(totalFiles)
          .arg(totalSize));
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);

  // 获取表格视口的实际可用宽度
  int totalWidth = ui->m_tableWidget->viewport()->width();
  if (totalWidth > 0) {
    ui->m_tableWidget->setColumnWidth(0, totalWidth * 0.8);
    ui->m_tableWidget->setColumnWidth(1, totalWidth * 0.2);
  }
}
