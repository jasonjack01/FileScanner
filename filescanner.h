#pragma once

#include <QMainWindow>
#include <cstdint>

// 前置声明 UI 命名空间中的类（由 filescanner.ui 自动生成）
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QMainWindow *parent = nullptr);
  ~MainWindow();

private slots:
  void onStartScan();
  void onStopScan();
  void onUpdateProgress(QString path, uint64_t size);
  void onScanComplete(int totalFiles, uint64_t totalSize);

signals:
  void sigProgress(QString path, uint64_t size);
  void sigComplete(int totalFiles, uint64_t size);

protected:
  void showEvent(QShowEvent *event) override;

private:
  Ui::MainWindow *ui; // UI 指针，用于访问拖拽的控件
};
