#pragma once
// ============ 自定义导入 Loading 弹窗（第九阶段：系统级文件拖入资产树） ============
// 进度条 + 「正在解析并导入文件... %1/%2：%3」；step() 内 processEvents 保证弹窗实时刷新。
#include <QDialog>
#include <QVBoxLayout>
#include <QProgressBar>
#include <QLabel>
#include <QApplication>
#include <QThread>
#include <QElapsedTimer>

class ImportLoadingDialog : public QDialog {
public:
    explicit ImportLoadingDialog(int total, QWidget *parent = nullptr) : QDialog(parent), totalSteps(total) {
        setWindowTitle("正在导入资产");
        setModal(true);
        setFixedSize(460, 130);
        auto *lay = new QVBoxLayout(this);
        label = new QLabel("正在解析并导入文件... 0/" + QString::number(total));
        label->setStyleSheet("font-size:13px; padding:4px;");
        lay->addWidget(label);
        bar = new QProgressBar;
        bar->setRange(0, total);
        bar->setTextVisible(false);
        bar->setStyleSheet("QProgressBar{background:#262a33; border:1px solid #3b4a61; border-radius:5px; height:20px;}"
                           "QProgressBar::chunk{background:#3b80c9; border-radius:4px;}");
        lay->addWidget(bar);
    }

    // 每导入一个文件推进一步（processEvents 让进度条真实动起来）
    void step(const QString &fileName) {
        ++done;
        label->setText(QString("正在解析并导入文件... %1/%2：%3").arg(done).arg(totalSteps).arg(fileName));
        bar->setValue(done);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    void finish() { QApplication::processEvents(QEventLoop::AllEvents, 50); }

private:
    QLabel *label = nullptr;
    QProgressBar *bar = nullptr;
    int totalSteps = 0, done = 0;
};

// 演示钩子用：同步小睡但保持事件循环（Loading 弹窗实时重绘，截图时刻可见）
inline void importMsleep(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
}
