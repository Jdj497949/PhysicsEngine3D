// CrashReporter —— 崩溃报告员（第十个程序）。
// 由崩溃进程 fork+exec 派生：全新进程、全新 Qt 状态，负责体面地弹双层弹窗、
// 展示日志路径，并在用户确认后 QProcess::startDetached 重新拉起启动器。
// 崩溃父进程在 waitpid 处安静等待，报告员退出后随之自杀。
// 参数：argv[1]=程序名 argv[2]=崩溃原因 argv[3]=日志路径 argv[4]=启动器路径 argv[5]=launcherMode(1/0)
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "用法：CrashReporter <程序名> <原因> <日志路径> <启动器路径> <launcherMode>\n"); return 2; }
    QApplication app(argc, argv);
    const QString progName   = argv[1];
    const QString cause      = argv[2];
    const QString logPath    = argv[3];
    const QString launcherEx = argv[4];
    const bool launcherMode  = QString(argv[5]) == "1";
    const bool autoDemo      = qEnvironmentVariableIsSet("CRASH_AUTO_DEMO");
    int t1 = qEnvironmentVariableIntValue("CRASH_AUTO_T1"); if (t1 <= 0) t1 = 3500;
    int t2 = qEnvironmentVariableIntValue("CRASH_AUTO_T2"); if (t2 <= 0) t2 = 4500;

    fprintf(stderr, "[CrashReporter] 报告员进程已就绪（全新 Qt 状态），开始体面弹窗\n");
    fflush(stderr);

    // ---------- 启动器模式：只弹一层（日志已写），绝不重启，彻底退出（防死循环兜底） ----------
    if (launcherMode) {
        fprintf(stderr, "[CrashReporter] 启动器崩溃兜底：不再拉起任何程序，写日志后彻底退出\n");
        fflush(stderr);
        QMessageBox box(QMessageBox::Critical, "⚠️ 启动器发生致命错误",
            QString("⚠️ 启动器发生致命错误，即将强制退出！\n\n崩溃类型：%1\n\n完整崩溃日志已导出至：\n%2\n\n（防死循环兜底：启动器崩溃后不再重启任何程序）")
                .arg(cause, logPath));
        box.exec();
        fprintf(stderr, "[CrashReporter] 启动器进程彻底退出\n");
        fflush(stderr);
        return 1;
    }

    // ---------- 第一层：红色警告 QMessageBox ----------
    QMessageBox *box = new QMessageBox(QMessageBox::Critical, "⚠️ 引擎发生致命错误",
        QString("⚠️ 引擎发生致命错误，即将强制退出！\n\n崩溃类型：%1\n\n点击「确定」查看详情与日志路径。").arg(cause));
    box->setStyleSheet("QLabel{color:#c62828; font-size:13px; font-weight:bold;}");
    box->addButton("确定", QMessageBox::AcceptRole);
    if (autoDemo) {
        QTimer::singleShot(t1, box, [box]{
            for (QPushButton *b : box->findChildren<QPushButton *>())
                if (b->text().contains("确定")) {
                    fprintf(stderr, "[CrashReporter] 演示：自动点击第一层「确定」\n"); fflush(stderr);
                    b->animateClick(); break;
                }
        });
    }
    box->exec();
    fprintf(stderr, "[CrashReporter] 第一层红色警告弹窗已确认，弹出第二层详情\n");
    fflush(stderr);

    // ---------- 第二层：详情 QDialog（错误信息 / 模块 / 日志路径 / 自爆按钮） ----------
    QDialog d;
    d.setWindowTitle("崩溃详情 · Crash Report");
    d.resize(600, 300);
    QVBoxLayout *lay = new QVBoxLayout(&d);
    QLabel *info = new QLabel(QString(
        "<div style='font-size:13px; line-height:1.8'>"
        "<b style='color:#c62828'>具体错误信息：</b>%1<br>"
        "<b>导致毛病的模块：</b>%2<br>"
        "<b style='color:#1565c0'>完整崩溃日志已导出至：\n%3</b><br>"
        "<span style='color:#555'>(日志含内存占用 VmRSS/VmPeak 与堆栈调用 Stack Trace)</span><br>"
        "</div>").arg(cause, progName, logPath));
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(info);
    lay->addStretch();
    QPushButton *btn = new QPushButton("退出并重启启动器");
    btn->setStyleSheet("QPushButton{padding:8px 18px; font-weight:bold;}");
    lay->addWidget(btn, 0, Qt::AlignRight);
    QObject::connect(btn, &QPushButton::clicked, [&] {
        fprintf(stderr, "[CrashReporter] 自爆指令执行：QProcess::startDetached 重新拉起启动器 %s\n", launcherEx.toUtf8().constData());
        fflush(stderr);
        QProcess::startDetached(launcherEx, QStringList());
        fprintf(stderr, "[CrashReporter] 启动器已重新拉起，崩溃进程即将安全退出 (exit 0)\n");
        fflush(stderr);
        d.accept();   // 标准退出模态弹窗（quit 对嵌套模态循环不可靠）
    });
    if (autoDemo) {
        QTimer::singleShot(t2, &d, [&]{
            fprintf(stderr, "[CrashReporter] 演示：自动点击「退出并重启启动器」\n");
            fflush(stderr);
            btn->animateClick();
        });
    }
    d.exec();
    fprintf(stderr, "[CrashReporter] 报告完毕，报告员退出\n");
    fflush(stderr);
    return 0;
}
