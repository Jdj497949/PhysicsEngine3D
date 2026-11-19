// ============================================================
// CrashHandler.h —— 商业级崩溃捕获 / 日志导出 / 双层弹窗 / 自爆重启
// 覆盖：段错误(SIGSEGV/SIGBUS/SIGFPE/SIGILL)、std::terminate(未捕获异常)、
//       qFatal(Qt 致命错误 → abort → SIGABRT)
// 用法：QApplication 创建后、app.exec() 前：
//   CrashHandler::install("3DEditor", logDir, launcherExe, false);
//   if (CrashHandler::saveRecoveryPoint()) { CrashHandler::showCrashDialogs(); return 0; }
//   return runApp(...);   // 原主体
// ============================================================
#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#ifndef Q_OS_WIN
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <execinfo.h>     // backtrace / backtrace_symbols_fd（glibc 自带）
#endif

namespace CrashHandler {

#ifdef Q_OS_WIN
// ---------------- Windows 轻量版：不装信号捕获（Windows 原生异常由系统弹窗兜底） ----------------
inline void install(const char *progName, const QString &logDir,
                    const QString &launcherExe, bool launcherMode) {
    QDir().mkpath(logDir);
    fprintf(stderr, "[CrashHandler] Windows 版已就绪：程序=%s（轻量模式，日志目录=%s）\n",
            progName, QDir::toNativeSeparators(logDir).toUtf8().constData());
    fflush(stderr);
}
#define CRASH_SAVE_POINT() 0
inline void showCrashDialogs() {}
#else

// ---------------- 全局现场（信号处理器可用的极简结构） ----------------
static jmp_buf       g_jmp;                 // 恢复点
static volatile sig_atomic_t g_armed = 0;   // 1=已进入"可能崩溃"区
static char g_progName[128]  = "unknown";
static char g_logDir[512]    = "";
static char g_logPath[512]   = "";
static char g_launcherExe[512] = "";
static char g_reporterExe[512] = "";
static char g_modulePath[512] = "";
static char g_cause[256]     = "";          // 崩溃原因（信号处理器填写）
static bool g_launcherMode   = false;       // 启动器模式：崩溃只写日志不重启
static bool g_autoDemo       = false;       // 演示模式：自动点击按钮
static int  g_t1 = 2500, g_t2 = 3200;       // 演示自动点击延时(ms)

// ---------------- 内部：信号安全的日志写入 ----------------
static void safeAppend(int fd, const char *s, ssize_t n) { if (s && n > 0) ::write(fd, s, (size_t)n); }
static void safeAppendStr(int fd, const char *s) { safeAppend(fd, s, (ssize_t)::strlen(s)); }

static void writeCrashLog(int sig, siginfo_t *info) {
    // 兜底创建目录（正常路径已在 install 里用 QDir 建好）
    ::mkdir(g_logDir, 0755);
    int fd = ::open(g_logPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return;

    char buf[1024];
    time_t t = ::time(nullptr);
    struct tm lt;
    ::localtime_r(&t, &lt);
    int n = ::snprintf(buf, sizeof(buf),
        "==============================================\n"
        "  引擎崩溃日志 Crash Report\n"
        "==============================================\n"
        "时间    : %04d-%02d-%02d %02d:%02d:%02d\n"
        "程序    : %s\n"
        "模块    : %s\n"
        "信号    : %d (si_code=%d, 故障地址=%p)\n"
        "原因    : %s\n\n"
        "[内存占用 Memory Usage]\n",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
        g_progName, g_modulePath, sig, info ? info->si_code : 0,
        info ? info->si_addr : nullptr, g_cause);
    safeAppend(fd, buf, n);

    // 内存占用：原样抄 /proc/self/status 的 Vm* 行（读文件是信号安全的）
    int mfd = ::open("/proc/self/status", O_RDONLY);
    if (mfd >= 0) {
        char mbuf[8192];
        ssize_t r = ::read(mfd, mbuf, sizeof(mbuf) - 1);
        ::close(mfd);
        if (r > 0) {
            mbuf[r] = '\0';
            safeAppendStr(fd, "VmPeak / VmSize / VmRSS 等如下（字节）:\n");
            char *line = mbuf;
            while (line && *line) {
                char *nl = ::strchr(line, '\n');
                if (nl) *nl = '\0';
                if (::strncmp(line, "Vm", 2) == 0) { safeAppendStr(fd, line); safeAppendStr(fd, "\n"); }
                if (!nl) break;
                line = nl + 1;
            }
        }
    }

    // 堆栈调用信息 Stack Trace（真实采集，非伪造）
    safeAppendStr(fd, "\n[堆栈调用 Stack Trace]（地址需 addr2line -e 模块路径 解析）\n");
    void *bt[64];
    int nb = ::backtrace(bt, 64);
    ::backtrace_symbols_fd(bt, nb, fd);

    safeAppendStr(fd, "\n[备注] 本日志由 CrashHandler 在崩溃信号处理器内真实导出。\n");
    ::close(fd);
}

static void describeSignal(int sig, siginfo_t *info) {
    switch (sig) {
    case SIGSEGV:
        ::snprintf(g_cause, sizeof(g_cause), "段错误 SIGSEGV —— 无效内存访问 (Access Violation)，故障地址 %p（如解引用空指针）", info ? info->si_addr : nullptr);
        break;
    case SIGBUS:  ::snprintf(g_cause, sizeof(g_cause), "总线错误 SIGBUS —— 内存对齐/映射失效，地址 %p", info ? info->si_addr : nullptr); break;
    case SIGFPE:  ::snprintf(g_cause, sizeof(g_cause), "算术错误 SIGFPE —— 除零/非法算术运算，地址 %p", info ? info->si_addr : nullptr); break;
    case SIGILL:  ::snprintf(g_cause, sizeof(g_cause), "非法指令 SIGILL，地址 %p", info ? info->si_addr : nullptr); break;
    case SIGABRT: ::snprintf(g_cause, sizeof(g_cause), "异常中止 SIGABRT —— qFatal / std::terminate / assert 触发"); break;
    default:      ::snprintf(g_cause, sizeof(g_cause), "致命信号 %d，地址 %p", sig, info ? info->si_addr : nullptr); break;
    }
}

static void signalHandler(int sig, siginfo_t *info, void *) {
    // 防重入：崩溃处理中再崩，直接死，不留半截日志
    if (g_armed == 0) { ::signal(sig, SIG_DFL); ::raise(sig); return; }
    g_armed = 0;
    describeSignal(sig, info);
    fprintf(stderr, "\n[CrashHandler] 信号 %d 被拦截：%s\n", sig, g_cause);
    fprintf(stderr, "[CrashHandler] 崩溃日志写入: %s\n", g_logPath);
    fflush(stderr);
    writeCrashLog(sig, info);
    fprintf(stderr, "[CrashHandler] 日志落盘完毕，派生崩溃报告员（全新进程，体面弹窗）\n"); fflush(stderr);
    // ---------- 派生报告员进程（fork/exec/waitpid 全部异步信号安全） ----------
    pid_t pid = ::fork();
    if (pid == 0) {
        // 子进程：脱胎换骨为崩溃报告员（全新进程映像，Qt 状态干净）
        ::execl(g_reporterExe, "CrashReporter", g_progName, g_cause, g_logPath,
                g_launcherExe, g_launcherMode ? "1" : "0", (char *)nullptr);
        ::_exit(127);
    }
    if (pid > 0) {
        int st = 0;
        ::waitpid(pid, &st, 0);   // 父进程安静等待报告员弹窗完毕
    }
    fprintf(stderr, "[CrashHandler] 报告员已退场，崩溃进程安全自杀 (exit 0，链子体面收场)\n");
    fflush(stderr);
    ::_exit(0);
}

// ---------------- std::set_terminate（死命令 2-1） ----------------
static void terminateHandler() {
    fprintf(stderr, "[CrashHandler] std::terminate 被触发（未捕获 C++ 异常）\n");
    fflush(stderr);
    describeSignal(SIGABRT, nullptr);
    if (g_armed) {
        writeCrashLog(SIGABRT, nullptr);
        pid_t pid = ::fork();
        if (pid == 0) {
            ::execl(g_reporterExe, "CrashReporter", g_progName, g_cause, g_logPath,
                    g_launcherExe, g_launcherMode ? "1" : "0", (char *)nullptr);
            ::_exit(127);
        }
        if (pid > 0) { int st = 0; ::waitpid(pid, &st, 0); }
    }
    ::abort();
}

// ---------------- qInstallMessageHandler（死命令 2-1：镜像到 stderr） ----------------
static QtMessageHandler g_prevMsg = nullptr;
static void qtMsgBridge(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    if (type == QtFatalMsg) {
        fprintf(stderr, "[CrashHandler] Qt 致命错误: %s\n", qUtf8Printable(msg));
        fflush(stderr);
    }
    if (g_prevMsg) { g_prevMsg(type, ctx, msg); return; }
    fprintf(stderr, "%s\n", qUtf8Printable(msg));
    fflush(stderr);
}

// ---------------- 安装（死命令 2-3：QDir 创建 logs 目录） ----------------
inline void install(const char *progName, const QString &logDir,
                    const QString &launcherExe, bool launcherMode) {
    strncpy(g_progName, progName, sizeof(g_progName) - 1);
    strncpy(g_logDir, QDir::toNativeSeparators(logDir).toUtf8(), sizeof(g_logDir) - 1);
    QDir().mkpath(logDir);   // logs 文件夹不存在 → 立刻用 QDir 创建
    strncpy(g_launcherExe, launcherExe.toUtf8(), sizeof(g_launcherExe) - 1);
    g_launcherMode = launcherMode;
    g_autoDemo = qEnvironmentVariableIsSet("CRASH_AUTO_DEMO");
    int t1 = qEnvironmentVariableIntValue("CRASH_AUTO_T1");
    if (t1 > 0) g_t1 = t1;
    int t2 = qEnvironmentVariableIntValue("CRASH_AUTO_T2");
    if (t2 > 0) g_t2 = t2;

    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString p = logDir + QString("/crash_%1_%2.txt").arg(progName).arg(stamp);
    strncpy(g_logPath, QDir::toNativeSeparators(p).toUtf8(), sizeof(g_logPath) - 1);
    QFileInfo self(qApp->applicationFilePath());
    strncpy(g_modulePath, QDir::toNativeSeparators(self.absoluteFilePath()).toUtf8(), sizeof(g_modulePath) - 1);
    strncpy(g_reporterExe, QDir::toNativeSeparators(self.dir().absoluteFilePath("CrashReporter")).toUtf8(), sizeof(g_reporterExe) - 1);

    struct sigaction sa;
    ::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signalHandler;
    sa.sa_flags = SA_SIGINFO;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGSEGV, &sa, nullptr);
    ::sigaction(SIGABRT, &sa, nullptr);
    ::sigaction(SIGFPE,  &sa, nullptr);
    ::sigaction(SIGBUS,  &sa, nullptr);
    ::sigaction(SIGILL,  &sa, nullptr);

    g_prevMsg = qInstallMessageHandler(qtMsgBridge);   // Qt 消息桥（含 fatal 拦截）
    std::set_terminate(terminateHandler);              // C++ 未捕获异常拦截

    fprintf(stderr, "[CrashHandler] 已安装：程序=%s 模式=%s 崩溃日志目录=%s\n",
            progName, launcherMode ? "启动器(只写日志不重启)" : "编辑器(双层弹窗+自爆重启)",
            QDir::toNativeSeparators(logDir).toUtf8().constData());
    fflush(stderr);
}

// ---------------- 恢复点（必须以宏展开在 main 的栈帧里，setjmp 函数返回后跳回属未定义行为） ----------------
static inline void armRecovery() { g_armed = 1; }
#define CRASH_SAVE_POINT() (CrashHandler::armRecovery(), ::sigsetjmp(CrashHandler::g_jmp, 1))

// ---------------- 双层弹窗 + 自爆（死命令 1、3） ----------------
inline void showCrashDialogs() {
    fprintf(stderr, "[CrashHandler] 已回到 main 恢复点，进入弹窗流程\n"); fflush(stderr);
    // 保险：再兜底装一次目录
    QDir().mkpath(QString::fromUtf8(g_logDir));

    // ---------- 启动器模式：只写日志、不重启任何程序，彻底退出（防死循环兜底） ----------
    if (g_launcherMode) {
        fprintf(stderr, "[CrashHandler] 启动器崩溃兜底：日志已保存，不再拉起编辑器，程序彻底退出\n");
        fflush(stderr);
        QMessageBox box(QMessageBox::Critical, "⚠️ 启动器发生致命错误",
            QString("⚠️ 启动器发生致命错误，即将强制退出！\n\n崩溃类型：%1\n\n完整崩溃日志已导出至：\n%2\n\n(防死循环兜底：启动器崩溃后不再重启任何程序)")
                .arg(QString::fromUtf8(g_cause), QString::fromUtf8(g_logPath)));
        box.exec();
        exit(1);
    }

    // ---------- 第一层：红色警告 QMessageBox ----------
    QMessageBox *box = new QMessageBox(QMessageBox::Critical, "⚠️ 引擎发生致命错误",
        QString("⚠️ 引擎发生致命错误，即将强制退出！\n\n崩溃类型：%1\n\n点击「确定」查看详情与日志路径。")
            .arg(QString::fromUtf8(g_cause)));
    box->setStyleSheet("QLabel{color:#c62828; font-size:13px; font-weight:bold;}");
    box->setAttribute(Qt::WA_Moved);   // 崩溃后跳过 QDialog::adjustPosition（其取父窗几何的路径在恢复态不可靠）
    box->move(500, 330);
    box->addButton("确定", QMessageBox::AcceptRole);
    if (g_autoDemo) {
        QTimer::singleShot(g_t1, box, [box]{
            const auto btns = box->findChildren<QPushButton *>();
            for (QPushButton *b : btns)
                if (b->text().contains("确定")) { fprintf(stderr, "[CrashHandler] 演示：自动点击第一层「确定」\n"); fflush(stderr); b->animateClick(); break; }
        });
    }
    box->exec();
    fprintf(stderr, "[CrashHandler] 第一层红色警告弹窗已确认，弹出第二层详情\n");
    fflush(stderr);
    // ---------- 第二层：详情 QDialog（错误信息 / 模块 / 日志路径 / 自爆按钮） ----------
    QDialog d;
    d.setWindowTitle("崩溃详情 · Crash Report");
    d.resize(560, 300);
    d.setAttribute(Qt::WA_Moved);      // 同上：跳过 adjustPosition
    d.move(520, 300);
    QVBoxLayout *lay = new QVBoxLayout(&d);
    QLabel *info = new QLabel(
        QString("<div style='font-size:13px; line-height:1.7'>"
                "<b style='color:#c62828'>具体错误信息：</b>%1<br>"
                "<b>导致毛病的模块：</b>%2<br>"
                "<b style='color:#1565c0'>完整崩溃日志已导出至：\n%3</b><br>"
                "<span style='color:#555'>(日志含内存占用 VmRSS/VmPeak 与堆栈调用 Stack Trace)</span><br>"
                "</div>").arg(QString::fromUtf8(g_cause),
                              QString::fromUtf8(g_modulePath),
                              QString::fromUtf8(g_logPath)));
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(info);
    lay->addStretch();
    QPushButton *btn = new QPushButton("退出并重启启动器");
    btn->setStyleSheet("QPushButton{padding:8px 18px; font-weight:bold;}");
    lay->addWidget(btn, 0, Qt::AlignRight);
    QObject::connect(btn, &QPushButton::clicked, [&] {
        fprintf(stderr, "[CrashHandler] 自爆指令执行：QProcess::startDetached 拉起启动器 %s\n", g_launcherExe);
        fflush(stderr);
        QProcess::startDetached(QString::fromUtf8(g_launcherExe), QStringList());
        fprintf(stderr, "[CrashHandler] 自爆完成：启动器已重新拉起，当前崩溃进程安全退出 (exit 0)\n");
        fflush(stderr);
        qApp->quit();
        exit(0);
    });
    if (g_autoDemo) {
        QTimer::singleShot(g_t2, &d, [&]{
            fprintf(stderr, "[CrashHandler] 演示：自动点击「退出并重启启动器」\n");
            fflush(stderr);
            btn->animateClick();
        });
    }
    d.exec();
    exit(0);
}
#endif // Q_OS_WIN

} // namespace CrashHandler
