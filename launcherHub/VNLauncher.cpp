// VNLauncher — Hub 模式启动器（第十七阶段：商业级 Hub 重构）
// 侧边导航五项：项目 / 引擎版本(Installs) / 学习中心 / 设置 / 关于，页面切换淡入过渡
// 引擎版本页：零参数一键下载（无 Token/URL 输入），版本卡片 → 进度条 → 自动解压 → 打开目录/卸载
// 学习中心：QTabWidget 四大板块（插座蓝图指南 / Lua 脚本 API 手册 / 3D 物理组件详解 / 打包与加密指南）
// 项目管理：封面卡片（按模板配色）+ 大新建按钮 + 独立新建项目页
// 设置：语言 / 主题即时切换（暗黑/明亮）/ 代理 / 下载路径 / 缓存清理；关于页：版本/编译时间/开源库/协议/作者
#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QTextBrowser>
#include <QComboBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QFrame>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <cstdio>

// ================= i18n =================
static int g_lang = 0; // 0=zh 1=en
static inline QString L(const char *zh, const char *en) { return g_lang == 0 ? QString::fromUtf8(zh) : QString::fromUtf8(en); }

// ================= 配置 =================
static const char *kOrg = "PhysicsEngine3D";
static const char *kApp = "VNLauncher";
static QString repoApiFor(const QString &tag) { return QString("https://api.github.com/repos/Jdj497949/PhysicsEngine3D/releases/tags/%1").arg(tag); }
static QString engineCoreRoot() { return QCoreApplication::applicationDirPath() + "/Engine_Core"; }
static QString defaultTag() { return "v1.0.0"; }
static QString projectsRoot() { return QDir::homePath() + "/Physics3DProjects"; }
static const char *kBuildDate = __DATE__;
static const char *kBuildTime = __TIME__;

// 平台对应资产关键词（GitHub Release 里的真实资产）
static QStringList assetKeywordsForPlatform() {
#if defined(Q_OS_WIN)
    return {"Setup", "win"};
#elif defined(Q_OS_MAC)
    return {"macOS", "mac", "osx"};
#else
    return {"Linux"};
#endif
}

// ================= 下载管理器（防断网/防卡死/代理/超时重试） =================
class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr) : QObject(parent) {
        watchdog_ = new QTimer(this);
        watchdog_->setInterval(30000); // 30 秒无进度 = 超时
        watchdog_->setSingleShot(true);
        connect(watchdog_, &QTimer::timeout, this, [this]() {
            lastError_ = L("网络超时，请开启加速器后点击重试", "Network timeout. Enable your accelerator and retry.");
            if (reply_) reply_->abort();
            if (metaReply_) metaReply_->abort();
        });
    }

    void applyProxy(const QString &host, int port) {
        if (host.trimmed().isEmpty() || port <= 0) {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
            proxyDesc_ = "None";
        } else {
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, host.trimmed(), port));
            proxyDesc_ = host.trimmed() + ":" + QString::number(port);
        }
    }
    QString proxyDesc() const { return proxyDesc_; }
    double lastSpeed() const { return lastSpeed_; }
    QString lastError() const { return lastError_; }
    void reset() { lastError_.clear(); aborted_ = false; lastSpeed_ = 0; }

    // 第一步：请求 Release 元数据（公开仓库无需 Token）
    void fetchMeta(const QString &tag, const QString &token = QString()) {
        fprintf(stderr, "[LAUNCHER] Connecting to GitHub API... Proxy applied: %s\n", proxyDesc_.toUtf8().constData());
        fflush(stderr);
        QNetworkRequest req{QUrl(repoApiFor(tag))};
        req.setHeader(QNetworkRequest::UserAgentHeader, "VNLauncher/1.0");
        if (!token.trimmed().isEmpty())
            req.setRawHeader("Authorization", "Bearer " + token.trimmed().toUtf8());
        auto *r = nam_.get(req);
        metaReply_ = r;
        watchdog_->start();
        connect(r, &QNetworkReply::finished, this, [this, r, tag]() {
            metaReply_ = nullptr;
            watchdog_->stop();
            if (r->error() != QNetworkReply::NoError) {
                if (lastError_.isEmpty()) lastError_ = L("连接 GitHub 失败：", "GitHub connection failed: ") + r->errorString();
                r->deleteLater();
                emit metaDone(false, "", "", tag);
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
            r->deleteLater();
            QJsonArray assets = doc.object()["assets"].toArray();
            QStringList kws = assetKeywordsForPlatform();
            for (const QJsonValue &v : assets) {
                QJsonObject a = v.toObject();
                QString name = a["name"].toString();
                bool hit = false;
                for (const QString &kw : kws) if (name.contains(kw, Qt::CaseInsensitive)) { hit = true; break; }
                if (hit) { emit metaDone(true, a["browser_download_url"].toString(), name, tag); return; }
            }
            lastError_ = L("该版本没有本平台的安装包", "No asset for this platform in that release");
            emit metaDone(false, "", "", tag);
        });
    }

    // 第二步：下载资产（readyRead 落盘 + downloadProgress 算速）
    void download(const QUrl &url, const QString &token, const QString &savePath) {
        savePath_ = savePath;
        file_.setFileName(savePath);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit downloadFailed(L("无法写入本地文件：", "Cannot write file: ") + savePath);
            return;
        }
        QNetworkRequest req{url};
        req.setHeader(QNetworkRequest::UserAgentHeader, "VNLauncher/1.0");
        if (!token.trimmed().isEmpty())
            req.setRawHeader("Authorization", "Bearer " + token.trimmed().toUtf8());
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        reply_ = nam_.get(req);
        speed_.start();
        lastBytes_ = 0;
        lastSpeed_ = 0;
        watchdog_->start();
        connect(reply_, &QNetworkReply::readyRead, this, [this]() {
            file_.write(reply_->readAll());
        });
        connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            watchdog_->start(); // 有数据就续命
            double dt = speed_.elapsed() / 1000.0;
            if (dt > 0.4) {
                lastSpeed_ = (got - lastBytes_) / 1048576.0 / dt;
                lastBytes_ = got;
                speed_.restart();
            }
            double eta = (total > 0 && lastSpeed_ > 0.01) ? (total - got) / 1048576.0 / lastSpeed_ : 0;
            emit progress(int(total > 0 ? got * 100 / total : 0), lastSpeed_, eta, got, total);
        });
        connect(reply_, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError e) {
            if (e == QNetworkReply::OperationCanceledError) return;
            if (lastError_.isEmpty()) lastError_ = L("网络超时，请开启加速器后点击重试", "Network timeout. Enable your accelerator and retry.");
            aborted_ = true;
        });
        connect(reply_, &QNetworkReply::finished, this, [this]() {
            reply_ = nullptr;
            watchdog_->stop();
            file_.close();
            if (aborted_ || !lastError_.isEmpty()) { emit downloadFailed(lastError_); return; }
            if (file_.size() <= 0) { emit downloadFailed(L("下载内容为空", "Empty download")); return; }
            emit downloadDone(savePath_, file_.size());
        });
    }

signals:
    void metaDone(bool ok, const QString &assetUrl, const QString &assetName, const QString &tag);
    void progress(int percent, double mbps, double etaSec, qint64 got, qint64 total);
    void downloadDone(const QString &path, qint64 bytes);
    void downloadFailed(const QString &reason);

private:
    QNetworkAccessManager nam_;
    QNetworkReply *reply_ = nullptr;
    QNetworkReply *metaReply_ = nullptr;
    QTimer *watchdog_ = nullptr;
    QElapsedTimer speed_;
    QFile file_;
    QString savePath_, lastError_, proxyDesc_ = "None";
    qint64 lastBytes_ = 0;
    double lastSpeed_ = 0;
    bool aborted_ = false;
};

// ================= 解压与安装 =================
static QString findEditorBinary(const QString &root) {
    QDirIterator it(root, {"3DEditor", "3DEditor.exe"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) return it.next();
    return QString();
}

static bool extractEngine(const QString &archivePath, const QString &tag, QString &outDir) {
    outDir = engineCoreRoot() + "/" + tag;
    QDir().mkpath(outDir);
    fprintf(stderr, "[EXTRACT] Unzipping... -> %s\n", outDir.toUtf8().constData());
    fflush(stderr);
    bool ok = false;
    if (archivePath.endsWith(".tar.gz") || archivePath.endsWith(".tgz")) {
        ok = QProcess::execute("tar", {"-xzf", archivePath, "-C", outDir}) == 0;
    } else if (archivePath.endsWith(".zip")) {
        ok = QProcess::execute("unzip", {"-oq", archivePath, "-d", outDir}) == 0;
    } else if (archivePath.endsWith(".exe")) {
        ok = QFile::copy(archivePath, outDir + "/" + QFileInfo(archivePath).fileName());
    }
    if (!ok) return false;
    // Linux/Mac：赋予可执行权限
    QString editor = findEditorBinary(outDir);
    if (!editor.isEmpty()) {
        QFile::setPermissions(editor,
            QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther |
            QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
        fprintf(stderr, "[EXTRACT] chmod +x applied: %s\n", editor.toUtf8().constData());
    }
    fflush(stderr);
    return true;
}

// ================= 项目创建（标准目录结构，带模板） =================
static bool createProjectStd(const QString &name, const QString &parentDir, QString &projPath,
                             const QString &type = "3D", const QString &tpl = "blank") {
    projPath = parentDir + "/" + name;
    if (QFileInfo::exists(projPath)) return false;
    QDir().mkpath(projPath + "/assets");
    QFile f(projPath + "/Project.vnproj");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QString(
        "projectName=%1\nengine=PhysicsEngine3D\nengineVersion=v1.0.0\n"
        "renderer=OpenGL 3.3\nphysicsRate=60\ngravity=0,-9.81,0\ntype=%2\ntemplate=%3\ncreated=%4\n")
        .arg(name, type, tpl, QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
    f.close();
    return true;
}

// 读取 .vnproj 的模板信息（用于封面配色）
static QHash<QString,QString> readProjMeta(const QString &projDir) {
    QHash<QString,QString> m;
    QFile f(projDir + "/Project.vnproj");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            int eq = line.indexOf('=');
            if (eq > 0) m[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
        }
    }
    return m;
}

// ================= 主题样式（暗黑 / 明亮，即时切换） =================
static QString themeQss(bool dark) {
    if (dark) return QString(R"(
        QMainWindow, QStackedWidget, QWidget#pageRoot { background: #1e1f24; }
        QWidget#sidebar { background: #17181c; }
        QLabel#appTitle { color: #f0f0f2; font-size: 15px; font-weight: bold; }
        QLabel#appSub { color: #6d6e78; font-size: 11px; }
        QLabel#pageH1 { color: #f0f0f2; font-size: 20px; font-weight: bold; }
        QLabel#pageSub { color: #8a8b96; font-size: 12px; }
        QPushButton#navBtn { color: #9a9aa5; background: transparent; border: none; border-radius: 8px; padding: 11px 14px; text-align: left; font-size: 14px; }
        QPushButton#navBtn:hover { background: #26272e; color: #d8d8de; }
        QPushButton#navBtn:checked { background: #2b3b52; color: #4da3ff; font-weight: bold; }
        QPushButton#primary { background: #2f7cd6; color: #ffffff; border: none; border-radius: 8px; padding: 9px 20px; font-weight: bold; font-size: 13px; }
        QPushButton#primary:hover { background: #3d8ce6; }
        QPushButton#primary:pressed { background: #2568b8; }
        QPushButton#primary:disabled { background: #2c2d35; color: #6d6e78; }
        QPushButton#danger { background: transparent; color: #ff6b6b; border: 1px solid #5a2d2d; border-radius: 8px; padding: 8px 16px; font-size: 13px; }
        QPushButton#danger:hover { background: #3a2226; }
        QPushButton#ghost { background: #26272e; color: #d8d8de; border: 1px solid #3a3b45; border-radius: 8px; padding: 8px 16px; font-size: 13px; }
        QPushButton#ghost:hover { background: #2f3038; border-color: #4da3ff; }
        QFrame#projCard, QFrame#instCard, QFrame#engineCard { background: #26272e; border: 1px solid #33343c; border-radius: 12px; }
        QFrame#projCardNew { background: #223047; border: 2px solid #4da3ff; border-radius: 12px; }
        QFrame#cover { border-radius: 8px; }
        QLabel#cardTitle { color: #f0f0f2; font-size: 14px; font-weight: bold; }
        QLabel#cardPath { color: #8a8b96; font-size: 11px; }
        QLabel#coverIcon { font-size: 30px; background: transparent; }
        QPushButton#tplBtn { background: #26272e; border: 2px solid #33343c; border-radius: 12px; padding: 14px; text-align: left; color: #d8d8de; font-size: 13px; }
        QPushButton#tplBtn:hover { border-color: #4da3ff; }
        QPushButton#tplBtn:checked { border: 2px solid #4da3ff; background: #223047; color: #ffffff; }
        QLineEdit, QComboBox { background: #26272e; color: #e8e8ea; border: 1px solid #3a3b45; border-radius: 8px; padding: 8px 10px; font-size: 13px; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #4da3ff; }
        QComboBox QAbstractItemView { background: #26272e; color: #e8e8ea; selection-background-color: #2b3b52; }
        QProgressBar { background: #26272e; border: 1px solid #3a3b45; border-radius: 8px; height: 18px; text-align: center; color: #e8e8ea; font-size: 11px; }
        QProgressBar::chunk { background: #2f7cd6; border-radius: 7px; }
        QGroupBox { background: #222329; border: 1px solid #33343c; border-radius: 12px; margin-top: 14px; padding: 12px; color: #d8d8de; font-size: 13px; font-weight: bold; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; top: 0px; color: #8a8b96; }
        QLabel { color: #c8c9d2; font-size: 13px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #3a3b45; border-radius: 5px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #4da3ff; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QTextBrowser { background: #222329; border: 1px solid #33343c; border-radius: 12px; color: #c8c9d2; padding: 10px; }
        QTabWidget::pane { border: 1px solid #33343c; border-radius: 10px; background: #222329; }
        QTabBar::tab { background: transparent; color: #8a8b96; padding: 9px 18px; border-radius: 8px; font-size: 13px; }
        QTabBar::tab:selected { background: #2b3b52; color: #4da3ff; font-weight: bold; }
        QTabBar::tab:hover { color: #d8d8de; }
    )");
    return QString(R"(
        QMainWindow, QStackedWidget, QWidget#pageRoot { background: #f3f4f7; }
        QWidget#sidebar { background: #e6e7ec; }
        QLabel#appTitle { color: #1a1b20; font-size: 15px; font-weight: bold; }
        QLabel#appSub { color: #8a8b96; font-size: 11px; }
        QLabel#pageH1 { color: #1a1b20; font-size: 20px; font-weight: bold; }
        QLabel#pageSub { color: #6d6e78; font-size: 12px; }
        QPushButton#navBtn { color: #55565f; background: transparent; border: none; border-radius: 8px; padding: 11px 14px; text-align: left; font-size: 14px; }
        QPushButton#navBtn:hover { background: #dcdde3; color: #1a1b20; }
        QPushButton#navBtn:checked { background: #d4e4f7; color: #1d63c4; font-weight: bold; }
        QPushButton#primary { background: #2f7cd6; color: #ffffff; border: none; border-radius: 8px; padding: 9px 20px; font-weight: bold; font-size: 13px; }
        QPushButton#primary:hover { background: #3d8ce6; }
        QPushButton#primary:pressed { background: #2568b8; }
        QPushButton#primary:disabled { background: #d5d6dc; color: #9a9aa5; }
        QPushButton#danger { background: transparent; color: #c43737; border: 1px solid #e3b8b8; border-radius: 8px; padding: 8px 16px; font-size: 13px; }
        QPushButton#danger:hover { background: #f7e2e2; }
        QPushButton#ghost { background: #ffffff; color: #3a3b45; border: 1px solid #c9cad2; border-radius: 8px; padding: 8px 16px; font-size: 13px; }
        QPushButton#ghost:hover { border-color: #2f7cd6; }
        QFrame#projCard, QFrame#instCard, QFrame#engineCard { background: #ffffff; border: 1px solid #dcdde3; border-radius: 12px; }
        QFrame#projCardNew { background: #eaf3fd; border: 2px solid #2f7cd6; border-radius: 12px; }
        QFrame#cover { border-radius: 8px; }
        QLabel#cardTitle { color: #1a1b20; font-size: 14px; font-weight: bold; }
        QLabel#cardPath { color: #8a8b96; font-size: 11px; }
        QLabel#coverIcon { font-size: 30px; background: transparent; }
        QPushButton#tplBtn { background: #ffffff; border: 2px solid #dcdde3; border-radius: 12px; padding: 14px; text-align: left; color: #3a3b45; font-size: 13px; }
        QPushButton#tplBtn:hover { border-color: #2f7cd6; }
        QPushButton#tplBtn:checked { border: 2px solid #2f7cd6; background: #eaf3fd; color: #1a1b20; }
        QLineEdit, QComboBox { background: #ffffff; color: #1a1b20; border: 1px solid #c9cad2; border-radius: 8px; padding: 8px 10px; font-size: 13px; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #2f7cd6; }
        QComboBox QAbstractItemView { background: #ffffff; color: #1a1b20; selection-background-color: #d4e4f7; }
        QProgressBar { background: #e6e7ec; border: 1px solid #c9cad2; border-radius: 8px; height: 18px; text-align: center; color: #3a3b45; font-size: 11px; }
        QProgressBar::chunk { background: #2f7cd6; border-radius: 7px; }
        QGroupBox { background: #ffffff; border: 1px solid #dcdde3; border-radius: 12px; margin-top: 14px; padding: 12px; color: #3a3b45; font-size: 13px; font-weight: bold; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; top: 0px; color: #6d6e78; }
        QLabel { color: #3a3b45; font-size: 13px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #c9cad2; border-radius: 5px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #2f7cd6; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QTextBrowser { background: #ffffff; border: 1px solid #dcdde3; border-radius: 12px; color: #3a3b45; padding: 10px; }
        QTabWidget::pane { border: 1px solid #dcdde3; border-radius: 10px; background: #ffffff; }
        QTabBar::tab { background: transparent; color: #6d6e78; padding: 9px 18px; border-radius: 8px; font-size: 13px; }
        QTabBar::tab:selected { background: #d4e4f7; color: #1d63c4; font-weight: bold; }
        QTabBar::tab:hover { color: #1a1b20; }
    )");
}

// ================= 学习中心长文内容 =================
static QString learnBlueprintHtml() {
    return QString(R"(
    <h1>插座蓝图指南（Socket Blueprint）</h1>
    <p>插座蓝图是 NovaEngine3D 的可视化编程系统：不写一行代码，把节点像电源插座一样用线连起来，就能拼出完整游戏逻辑。节点库目前覆盖 <b>3000+ 节点</b>，分为事件、流程控制、数学运算、物理、音频、UI、剧情控制等大类。</p>
    <h2>1. 基本概念</h2>
    <ul>
    <li><b>节点（Node）</b>：一个功能方块，例如「播放音效」「生成物体」「获取按键」。</li>
    <li><b>引脚（Pin）</b>：节点左右两侧的小圆点。左侧是输入，右侧是输出。</li>
    <li><b>白色线 = 执行流（Flow）</b>：决定"先做什么后做什么"。</li>
    <li><b>彩色线 = 数据流（Data）</b>：把一个节点的数据（数字/字符串/向量）传给另一个节点。</li>
    </ul>
    <h2>2. 怎么连线</h2>
    <p>双击 <code>.socket</code> 文件打开插座编辑器 → 从左侧节点库拖一个节点进画布 → 按住右侧引脚拖到目标节点左侧引脚松手即连线。按 <b>Delete</b> 点线可断开；拖到空白处松手可搜索并快速创建下一个节点。</p>
    <h2>3. 制作分支（If / Switch）</h2>
    <p>从「事件分发」类拖出 <b>Branch（分支）</b> 节点：它有一个 <b>Condition</b> 数据入口和 <b>True / False</b> 两个执行出口。把「按键按下」事件连到 Branch 的执行入口，把「血量 &gt; 0」的比较节点连到 Condition，再分别把 True/False 连到「播放音效」和「游戏结束」。</p>
    <pre>【按键按下】──▶【Branch】──True──▶【播放音效】
                    │
                    False──▶【游戏结束】</pre>
    <h2>4. 制作循环（ForLoop / WhileLoop）</h2>
    <p><b>ForLoop</b> 节点有 Start（起始）、Stop（结束）、LoopBody（每次执行）、Done（全部结束）四个口。下面例子：循环生成 10 个方块，每个间隔 0.5 秒：</p>
    <pre>【游戏开始】──▶【ForLoop 0~9】──LoopBody──▶【延迟 0.5s】──▶【生成方块】
                              │
                              Done──▶【播放完成音效】</pre>
    <p><b>WhileLoop</b> 则在 Condition 为真时反复执行 LoopBody，注意一定要在循环体内改变条件（比如「剩余敌人 -1」），否则会死循环。</p>
    <h2>5. 常用节点速查</h2>
    <ul>
    <li><b>事件类</b>：游戏开始、按键按下、碰撞开始/结束、定时器、自定义事件（Event Dispatcher）。</li>
    <li><b>流程类</b>：Branch、ForLoop、WhileLoop、Sequence（顺序执行）、Delay（延迟）、Gate（门）。</li>
    <li><b>物理类</b>：施加力、设置速度、射线检测、生成爆炸冲击。</li>
    <li><b>音频类</b>：播放音效、播放背景音乐、停止、设置音量。</li>
    <li><b>剧情控制</b>：显示对话框、角色立绘、选项分支、淡入淡出、场景跳转。</li>
    </ul>
    <h2>6. 实战：开门机关</h2>
    <pre>【碰撞开始(玩家×机关)】──▶【Branch(钥匙数量&gt;0)】
                              True──▶【播放开门动画】──▶【延迟1s】──▶【销毁门】
                              False──▶【显示提示"没有钥匙"】</pre>
    <p>保存 <code>.socket</code> 文件后按 F5 运行即可看到效果。蓝图改动即时生效，不需要重新编译。</p>
    )");
}

static QString learnLuaHtml() {
    return QString(R"(
    <h1>Lua 脚本 API 手册</h1>
    <p>NovaEngine3D 使用 <b>Lua 5.4</b> 作为脚本语言。在编辑器资产面板右键 → 新建代码脚本(.lua)，拖到物体的 Script 槽位即可挂载。一个脚本文件可以挂到多个物体上，每个物体拥有独立的实例数据。</p>
    <h2>1. 生命周期函数（引擎自动调用）</h2>
    <pre>-- 物体创建后调用一次：初始化数据、查找引用
function OnStart(self)
    self.speed = 5.0
    self.hp = 100
    print("玩家已出生")
end

-- 每帧调用（dt = 距上一帧的秒数）：移动、计时、状态机
function OnUpdate(self, dt)
    self:Move(0, 0, self.speed * dt)
end

-- 碰撞开始（other 为对方物体）
function OnCollision(self, other)
    if other:HasTag("enemy") then
        self.hp = self.hp - 10
    end
end

-- 物体销毁前调用：保存数据、清理
function OnDestroy(self)
    print("再见")
end</pre>
    <h2>2. 变换（Transform）</h2>
    <table border="1" cellpadding="4">
    <tr><th>API</th><th>说明</th></tr>
    <tr><td>self:Move(x, y, z)</td><td>沿世界坐标平移（推荐配合 dt）</td></tr>
    <tr><td>self:SetPosition(x, y, z)</td><td>直接设置世界坐标</td></tr>
    <tr><td>self:GetPosition()</td><td>返回 x, y, z 三个值</td></tr>
    <tr><td>self:Rotate(pitch, yaw, roll)</td><td>按欧拉角旋转（度）</td></tr>
    <tr><td>self:SetScale(s)</td><td>统一缩放</td></tr>
    </table>
    <h2>3. 场景（Scene）</h2>
    <pre>local enemy = Scene:Spawn("EnemyPrefab", 0, 2, 0)  -- 在坐标生成预制体
local player = Scene:Find("Player")                 -- 按名字查找物体
Scene:Destroy(enemy)                                -- 销毁物体
Scene:JumpTo("Level2")                              -- 切换场景</pre>
    <h2>4. 物理（Physics）</h2>
    <pre>self:SetVelocity(0, 8, 0)          -- 设置速度（跳跃）
self:AddForce(0, 500, 0)           -- 施加持续力
local hit = Physics:Raycast(0, -1, 0, 100)  -- 射线检测
if hit then print("打中了: " .. hit.name) end</pre>
    <h2>5. 音频（Audio）</h2>
    <pre>Audio:PlayBGM("music/town.ogg", 0.6)  -- 循环背景音乐，音量 0.6
Audio:PlaySound("sfx/jump.wav")       -- 一次性音效
Audio:StopBGM()</pre>
    <h2>6. UI 与剧情</h2>
    <pre>UI:ShowDialogue("你好，勇者！")          -- 底部对话框
UI:ShowChoices({"继续", "逃跑"})          -- 弹出选项，返回选择的序号
UI:FadeToBlack(800)                      -- 800ms 淡出黑屏</pre>
    <h2>7. 输入（Input）</h2>
    <pre>function OnUpdate(self, dt)
    if Input:KeyDown("W") then self:Move(0, 0, 5 * dt) end
    if Input:KeyPress("Space") then self:SetVelocity(0, 8, 0) end
end</pre>
    <h2>8. 常见坑</h2>
    <ul>
    <li>OnUpdate 里做移动一定要乘 <b>dt</b>，否则不同帧率速度不一致。</li>
    <li>协程内不要直接调用阻塞函数；等待用 <code>WaitForClick()</code> 等专用 API。</li>
    <li>print 输出在编辑器底部「控制台」面板，按 F5 运行后可见。</li>
    </ul>
    )");
}

static QString learnPhysicsHtml() {
    return QString(R"(
    <h1>3D 物理组件详解</h1>
    <p>NovaEngine3D 的物理层基于 <b>Bullet Physics</b>，固定 60Hz 步进。所有物理行为由三个组件驱动：<b>刚体（Rigidbody）</b>、<b>碰撞体（Collider）</b>、<b>布娃娃（Ragdoll）</b>。</p>
    <h2>1. 刚体（Rigidbody）</h2>
    <p>选中物体 → 检视面板 → 添加组件 → Rigidbody。属性说明：</p>
    <ul>
    <li><b>Mass（质量）</b>：千克。0 表示静态物体（如地板、墙壁，永不移动）。</li>
    <li><b>Is Kinematic（运动学）</b>：勾选后物体不受力影响，只能用脚本 Move 控制（适合移动平台、电梯）。</li>
    <li><b>Gravity Scale</b>：重力倍数。设 0 可悬浮，设 -1 可反向上升。</li>
    <li><b>Bounciness（弹性）</b>：0 = 摔在地上不动，1 = 完全弹回。</li>
    </ul>
    <h2>2. 碰撞体（Collider）</h2>
    <ul>
    <li><b>Box Collider</b>：默认，性能最好，适合箱子、墙。</li>
    <li><b>Sphere Collider</b>：球类、滚动物体。</li>
    <li><b>Capsule Collider</b>：人形角色推荐。</li>
    <li><b>Mesh Collider</b>：精确贴合模型，性能最贵，只用于必要时（如赛道）。</li>
    </ul>
    <p><b>注意</b>：Mesh Collider 一定要勾 Convex（凸包），否则无法与刚体配合。</p>
    <h2>3. 布娃娃（Ragdoll）怎么挂载</h2>
    <p>布娃娃用于角色死亡后的瘫软效果。引擎内置 <b>人形布娃娃模板</b>：16 个身体部件 + 15 个锥形关节（颈/腰/双肩/肘/腕/髋/膝/踝/胸椎），从高处跌落会自然瘫软。</p>
    <ol>
    <li>选中角色物体 → 添加组件 → <b>Ragdoll</b>；</li>
    <li>把模型的骨骼依次拖进对应槽位（头、胸、上臂×2、前臂×2、手×2、大腿×2、小腿×2、脚×2、腰）；</li>
    <li>运行时调用 <code>self:EnableRagdoll()</code> 立即切换到布娃娃状态；</li>
    <li>想恢复控制调用 <code>self:DisableRagdoll()</code>（角色会保持当前姿势站起）。</li>
    </ol>
    <pre>-- 受击后倒地 3 秒再爬起
function OnCollision(self, other)
    self:EnableRagdoll()
    QTimer.after(3.0, function() self:DisableRagdoll() end)
end</pre>
    <h2>4. 调试技巧</h2>
    <ul>
    <li>编辑器 View 菜单 → <b>Show Colliders</b>：线框显示所有碰撞体，检查有没有穿模。</li>
    <li>物体抖动不停？把质量调小或把 Solver Iterations 调高（物理设置里）。</li>
    <li>穿墙？把高速物体的 Collision Detection 改为 <b>Continuous</b>。</li>
    </ul>
    )");
}

static QString learnPackageHtml() {
    return QString(R"(
    <h1>打包与加密指南</h1>
    <h2>1. 一键打包三大平台</h2>
    <p>编辑器菜单 → <b>File → Build</b>（文件 → 打包），弹出打包对话框：</p>
    <ol>
    <li>选择目标平台：<b>Windows (.exe) / macOS (.app) / Linux</b>；</li>
    <li>选择输出目录（默认 <code>项目目录/Build/</code>）；</li>
    <li>勾选「压缩资源」可显著减小体积；</li>
    <li>点 <b>Build</b>，进度条走完即出安装包。</li>
    </ol>
    <p>命令行党也可以用仓库自带的脚本：<code>build_win.sh</code>、<code>build_mac.sh</code>，同一套源码分别产出 Windows 与 Mac 版本。</p>
    <h2>2. 资源加密（AES）</h2>
    <p>打包时勾选「加密资源」，所有美术/音频/脚本会打进 <code>.vnpak</code> 包并用 <b>AES-256</b> 加密，防止资源被直接解包。</p>
    <h3>设置密钥</h3>
    <ol>
    <li>打开项目根目录的 <code>Project.vnproj</code>；</li>
    <li>添加一行 <code>encryptKey=你的密钥</code>（建议 32 位随机字符串）；</li>
    <li>重新打包即生效。运行时引擎会用同一密钥自动解密，玩家无感知。</li>
    </ol>
    <pre># Project.vnproj 示例
projectName=MyGame
encryptKey=8f3a91c2d7b4e6501a9f8c3d2b7e6f50
encryptAssets=true</pre>
    <div style="color:#e0a040"><b>⚠️ 务必保管好密钥</b>：密钥丢失后加密包无法恢复；也不要把密钥提交到公开仓库！</div>
    <h2>3. 打包检查清单</h2>
    <ul>
    <li>所有场景都加入了 Build Scenes 列表；</li>
    <li>启动场景（第一个加载的场景）排在第一位；</li>
    <li>脚本无 print 调试残留（可在打包设置勾选「剥离调试输出」）；</li>
    <li>图标：Build 设置里替换默认图标（Windows 用 .ico，Mac 用 .icns）。</li>
    </ul>
    <h2>4. 常见问题</h2>
    <ul>
    <li><b>打包后黑屏</b>：多半是启动场景没设，去 Build 设置选第一个场景。</li>
    <li><b>资源找不到</b>：确认资源在 assets/ 目录内，外部引用不会被自动打包。</li>
    <li><b>Mac 提示"已损坏"</b>：那是 Gatekeeper 拦截未签名应用，右键 → 打开即可，正式发布需申请 Apple 开发者签名。</li>
    </ul>
    )");
}

// ================= 主窗口 =================
class HubWindow : public QMainWindow {
    Q_OBJECT
public:
    HubWindow() {
        setWindowTitle(L("NovaEngine3D 启动器", "NovaEngine3D Launcher"));
        resize(1060, 680);
        QSettings st(kOrg, kApp);
        dark_ = st.value("theme", 0).toInt() == 0;
        qApp->setStyleSheet(themeQss(dark_)); // 全局样式，主题切换即时生效

        auto *central = new QWidget(this);
        central->setObjectName("pageRoot");
        auto *root = new QHBoxLayout(central);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        setCentralWidget(central);

        // ---------- 侧边栏 ----------
        auto *sidebar = new QWidget(this);
        sidebar->setObjectName("sidebar");
        sidebar->setFixedWidth(212);
        auto *sv = new QVBoxLayout(sidebar);
        sv->setContentsMargins(14, 20, 14, 16);
        sv->setSpacing(4);
        auto *logo = new QLabel("⚡ NovaEngine3D", this);
        logo->setObjectName("appTitle");
        auto *sub = new QLabel(L("Launcher Hub", "Launcher Hub"), this);
        sub->setObjectName("appSub");
        sv->addWidget(logo);
        sv->addWidget(sub);
        sv->addSpacing(18);

        const struct { const char *zh, *en, *icon; int page; } navs[] = {
            {"项目", "Projects", "📁", 0},
            {"引擎版本", "Installs", "⚙️", 2},
            {"学习中心", "Learn", "📖", 3},
            {"设置", "Settings", "🛠️", 4},
            {"关于", "About", "ℹ️", 5},
        };
        for (const auto &n : navs) {
            auto *b = new QPushButton(QString("%1  %2").arg(n.icon).arg(L(n.zh, n.en)), this);
            b->setObjectName("navBtn");
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QPushButton::clicked, this, [this, n]() { selectNav(n.page); });
            sv->addWidget(b);
            navBtns_.append(b);
        }
        sv->addStretch();
        auto *ver = new QLabel("v1.0.0", this);
        ver->setObjectName("appSub");
        sv->addWidget(ver);

        // ---------- 页面栈 ----------
        stack_ = new QStackedWidget(this);
        stack_->addWidget(buildProjectsPage());   // 0
        stack_->addWidget(buildNewProjectPage()); // 1
        stack_->addWidget(buildInstallsPage());   // 2
        stack_->addWidget(buildLearnPage());      // 3
        stack_->addWidget(buildSettingsPage());   // 4
        stack_->addWidget(buildAboutPage());      // 5
        root->addWidget(sidebar);
        root->addWidget(stack_, 1);

        selectNav(0);
        refreshEngineCard();
        refreshInstalls();
        populateProjects();
    }

    // 无头自检：完整跑 请求→下载→解压→建项目→拉起
    void runSelfTest() {
        QSettings st(kOrg, kApp);
        QString token = qEnvironmentVariable("LAUNCH_TOKEN");
        QString proxyHost = st.value("proxyHost", "").toString();
        int proxyPort = st.value("proxyPort", 0).toInt();
        dl_.applyProxy(proxyHost, proxyPort);
        QString tag = qEnvironmentVariable("LAUNCH_TAG", defaultTag());

        connect(&dl_, &DownloadManager::metaDone, this, [this, token, tag](bool ok, const QString &url, const QString &name, const QString &) {
            if (!ok) { fprintf(stderr, "[LAUNCHER] Meta failed: %s\n", dl_.lastError().toUtf8().constData()); fflush(stderr); qApp->exit(2); return; }
            fprintf(stderr, "[LAUNCHER] Release %s resolved: %s\n", tag.toUtf8().constData(), name.toUtf8().constData());
            fflush(stderr);
            dl_.reset();
            dl_.download(QUrl(url), token, QString("/tmp/%1").arg(name));
        });
        connect(&dl_, &DownloadManager::progress, this, [](int pct, double mbps, double eta, qint64 got, qint64 total) {
            fprintf(stderr, "[DOWNLOAD] %d%% %.2f MB/s ETA %.0fs (%lld/%lld bytes)\n", pct, mbps, eta, (long long)got, (long long)total);
            fflush(stderr);
        });
        connect(&dl_, &DownloadManager::downloadFailed, this, [this](const QString &reason) {
            fprintf(stderr, "[LAUNCHER] Download failed: %s\n", reason.toUtf8().constData());
            fflush(stderr);
            qApp->exit(3);
        });
        connect(&dl_, &DownloadManager::downloadDone, this, [this, tag](const QString &path, qint64 bytes) {
            fprintf(stderr, "[DOWNLOAD] %s downloaded. Speed: %.2f MB/s. Saved to %s (%lld bytes)\n",
                    QFileInfo(path).fileName().toUtf8().constData(), dl_.lastSpeed(), path.toUtf8().constData(), (long long)bytes);
            fflush(stderr);
            QString outDir;
            if (!extractEngine(path, tag, outDir)) { fprintf(stderr, "[LAUNCHER] Extract failed!\n"); fflush(stderr); qApp->exit(4); return; }
            fprintf(stderr, "[EXTRACT] Engine core ready at %s/\n", outDir.toUtf8().constData());
            fflush(stderr);
            QString projPath;
            createProjectStd("MyFirstGame", projectsRoot(), projPath);
            if (projPath.isEmpty()) projPath = projectsRoot() + "/MyFirstGame";
            QString editor = findEditorBinary(outDir);
            fprintf(stderr, "[LAUNCH] Starting 3DEditor with args: --project=MyFirstGame\n");
            fprintf(stderr, "[LAUNCHER] Editor downloaded and extracted successfully!\n");
            fflush(stderr);
            if (!editor.isEmpty()) {
                bool launched = QProcess::startDetached(editor, QStringList() << "--project=" + projPath);
                fprintf(stderr, "[LAUNCH] startDetached(%s) => %s\n", editor.toUtf8().constData(), launched ? "true" : "false");
            } else {
                fprintf(stderr, "[LAUNCH] editor binary not found under %s\n", outDir.toUtf8().constData());
            }
            fflush(stderr);
            QTimer::singleShot(800, qApp, []() {
                fprintf(stderr, "[LAUNCHER] SELFTEST EXIT=0\n");
                fflush(stderr);
                qApp->exit(0);
            });
        });
        dl_.fetchMeta(tag, token);
    }

private slots:
    // 引擎版本页：一键下载（零参数）
    void onOneClickDownload() {
        QSettings st(kOrg, kApp);
        dl_.applyProxy(st.value("proxyHost", "").toString(), st.value("proxyPort", 0).toInt());
        btnDl_->setEnabled(false);
        btnDl_->setText(L("⬇ 下载中…", "⬇ Downloading…"));
        bar_->show();
        status_->setText(L("正在连接 GitHub API…", "Connecting to GitHub API..."));
        disconnect(metaConn_);
        metaConn_ = connect(&dl_, &DownloadManager::metaDone, this, [this](bool ok, const QString &url, const QString &name, const QString &tag) {
            if (!ok) { status_->setText(L("连接失败：", "Connect failed: ") + dl_.lastError()); resetDlBtn(); return; }
            status_->setText(L("找到安装包：", "Asset found: ") + name);
            disconnect(progConn_);
            progConn_ = connect(&dl_, &DownloadManager::progress, this, [this](int pct, double mbps, double eta, qint64, qint64) {
                bar_->setValue(pct);
                status_->setText(QString("%1%  %2 MB/s  %3 %4s")
                    .arg(pct).arg(mbps, 0, 'f', 2).arg(L("剩余", "ETA")).arg(int(eta)));
            });
            disconnect(doneConn_);
            doneConn_ = connect(&dl_, &DownloadManager::downloadDone, this, [this, tag](const QString &path, qint64) {
                status_->setText(L("下载完成，正在自动解压安装…", "Download complete, installing..."));
                QString outDir;
                if (!extractEngine(path, tag, outDir)) { status_->setText(L("解压失败", "Extract failed")); resetDlBtn(); return; }
                status_->setText(L("✅ 安装完成！", "✅ Installed!"));
                bar_->hide();
                refreshInstalls();
                refreshEngineCard();
            });
            disconnect(failConn_);
            failConn_ = connect(&dl_, &DownloadManager::downloadFailed, this, [this](const QString &reason) {
                status_->setText(reason);
                resetDlBtn();
            });
            dl_.reset();
            dl_.download(QUrl(url), QString(), QString("/tmp/%1").arg(name));
        });
        dl_.fetchMeta(defaultTag());
    }

    void onOpenInstallDir() {
        QString tag = defaultTag();
        QDir().mkpath(engineCoreRoot() + "/" + tag);
        QDesktopServices::openUrl(QUrl::fromLocalFile(engineCoreRoot() + "/" + tag));
    }

    void onUninstall() {
        QString tag = defaultTag();
        QString dir = engineCoreRoot() + "/" + tag;
        if (QMessageBox::question(this, L("卸载引擎", "Uninstall Engine"),
                L("确定卸载当前引擎版本？项目文件不受影响。", "Uninstall this engine version? Projects are not affected.")) != QMessageBox::Yes) return;
        QDir(dir).removeRecursively();
        fprintf(stderr, "[LAUNCHER] Engine uninstalled: %s\n", dir.toUtf8().constData()); fflush(stderr);
        refreshInstalls();
        refreshEngineCard();
    }

    void onLaunchEditor() {
        QString editor = currentEditorPath();
        if (editor.isEmpty()) return;
        fprintf(stderr, "[LAUNCH] Starting 3DEditor (no project)\n"); fflush(stderr);
        QProcess::startDetached(editor, {});
    }

    void onOpenProject(const QString &projPath) {
        QString editor = currentEditorPath();
        if (editor.isEmpty()) {
            QMessageBox::warning(this, L("提示", "Info"), L("编辑器尚未安装，请先到「引擎版本」页一键下载。", "Editor not installed. Download it on the Installs page first."));
            return;
        }
        fprintf(stderr, "[LAUNCH] Starting 3DEditor with args: --project=%s\n", QFileInfo(projPath).fileName().toUtf8().constData());
        fflush(stderr);
        QProcess::startDetached(editor, QStringList() << "--project=" + projPath);
    }

    void onCreateNewProject() {
        QString name = npName_->text().trimmed();
        if (name.isEmpty()) { QMessageBox::warning(this, L("提示", "Info"), L("请先输入项目名字", "Enter a project name first")); return; }
        QString dir = npPath_->text().trimmed();
        if (dir.isEmpty()) dir = projectsRoot();
        QDir().mkpath(dir);
        QString projPath;
        QString type = selTpl_ == 2 ? "2D" : "3D";
        const char *tplArr[] = {"blank", "physics", "platformer", "socket"};
        QString tpl = tplArr[selTpl_];
        if (!createProjectStd(name, dir, projPath, type, tpl)) {
            QMessageBox::warning(this, L("提示", "Info"), L("目录已存在同名项目", "A project with this name already exists"));
            return;
        }
        fprintf(stderr, "[LAUNCH] Project created: %s (type=%s template=%s)\n", projPath.toUtf8().constData(), type.toUtf8().constData(), tpl.toUtf8().constData());
        fflush(stderr);
        populateProjects(name);
        selectNav(0);
        QTimer::singleShot(80, this, [this]() {
            if (highlightedCard_) { projScroll_->ensureWidgetVisible(highlightedCard_, 30, 30); highlightedCard_ = nullptr; }
        });
    }

    void onBrowseProjectDir() {
        QString dir = QFileDialog::getExistingDirectory(this, L("选择保存位置", "Choose location"), npPath_->text().trimmed().isEmpty() ? projectsRoot() : npPath_->text());
        if (!dir.isEmpty()) npPath_->setText(dir);
    }

    void onClearCache() {
        for (const QFileInfo &fi : QDir("/tmp").entryInfoList({"PhysicsEngine3D-*", "*.tar.gz", "*.zip"}))
            if (fi.isFile()) QFile::remove(fi.absoluteFilePath());
        QMessageBox::information(this, L("完成", "Done"), L("下载缓存已清除", "Download cache cleared"));
    }

private:
    void resetDlBtn() {
        btnDl_->setEnabled(true);
        btnDl_->setText(L("⬇  一键下载", "⬇  One-Click Download"));
        bar_->hide();
    }

    void selectNav(int page) {
        stack_->setCurrentIndex(page);
        if (page == 2) refreshInstalls();   // 每次进入引擎版本页都刷新安装状态
        const int map[] = {0, -1, 1, 2, 3, 4};
        for (int i = 0; i < navBtns_.size(); ++i)
            navBtns_[i]->setChecked(map[page] == i);
        // 丝滑淡入过渡
        auto *w = stack_->currentWidget();
        auto *eff = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(eff);
        auto *anim = new QPropertyAnimation(eff, "opacity", w);
        anim->setDuration(200);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    QString currentEditorPath() {
        QString ed = findEditorBinary(engineCoreRoot() + "/" + defaultTag());
        if (ed.isEmpty()) ed = findEditorBinary(engineCoreRoot());
        return ed;
    }

    void refreshEngineCard() {
        QString ed = currentEditorPath();
        bool has = !ed.isEmpty();
        btnOpen_->setEnabled(has);
        if (has) {
            cardText_->setText(QString("⚡  NovaEngine3D  %1\n%2: %3").arg(defaultTag()).arg(L("编辑器路径", "Editor path")).arg(ed));
            card_->setStyleSheet("QFrame#engineCard{background:#123b1e;border:1px solid #2ea043;border-radius:12px} QLabel{color:#7ee787;font-weight:bold;font-size:13px}");
        } else {
            cardText_->setText(L("⚡  尚未安装引擎 —— 到「引擎版本」页一键下载", "⚡  Engine not installed — one-click download on the Installs page"));
            card_->setStyleSheet("QFrame#engineCard{background:#1b1f2a;border:1px dashed #444c66;border-radius:12px} QLabel{color:#8b93b8;font-size:13px}");
        }
    }

    void refreshInstalls() {
        bool has = !currentEditorPath().isEmpty();
        btnDl_->setVisible(!has);
        btnOpenDir_->setVisible(has);
        btnUninstall_->setVisible(has);
        instStatus_->setText(has
            ? L("✅ 已安装 —— 随时可启动编辑器", "✅ Installed — ready to launch")
            : L("未安装 —— 点击下方按钮一键下载并自动安装", "Not installed — one-click download & install below"));
        bar_->hide();
        status_->clear();
    }

    // 项目封面：按模板配色 + 图标
    QWidget *buildCover(const QString &tpl, QWidget *parent) {
        auto *cover = new QFrame(parent);
        cover->setObjectName("cover");
        cover->setFixedSize(96, 64);
        QString icon = "🌄", c1 = "#2b3b52", c2 = "#1a2536";
        if (tpl == "physics") { icon = "🎳"; c1 = "#4a2b52"; c2 = "#2a1a36"; }
        else if (tpl == "platformer") { icon = "🎮"; c1 = "#2b5246"; c2 = "#1a3630"; }
        else if (tpl == "socket") { icon = "🔌"; c1 = "#52472b"; c2 = "#36301a"; }
        cover->setStyleSheet(QString("QFrame#cover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 %1,stop:1 %2);border-radius:8px}").arg(c1, c2));
        auto *cv = new QVBoxLayout(cover);
        cv->setContentsMargins(0, 0, 0, 0);
        auto *ic = new QLabel(icon, cover);
        ic->setObjectName("coverIcon");
        ic->setAlignment(Qt::AlignCenter);
        cv->addWidget(ic);
        return cover;
    }

    void populateProjects(const QString &highlight = QString()) {
        while (projListLay_->count() > 0) {
            auto *it = projListLay_->takeAt(0);
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        highlightedCard_ = nullptr;
        QDir dir(projectsRoot());
        bool any = false;
        for (const QFileInfo &d : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
            if (!QFileInfo::exists(d.absoluteFilePath() + "/Project.vnproj")) continue;
            any = true;
            bool isNew = !highlight.isEmpty() && d.fileName() == highlight;
            auto meta = readProjMeta(d.absoluteFilePath());
            auto *card = new QFrame(this);
            card->setObjectName(isNew ? "projCardNew" : "projCard");
            card->setFixedHeight(92);
            auto *cl = new QHBoxLayout(card);
            cl->setContentsMargins(14, 12, 12, 12);
            cl->setSpacing(14);
            cl->addWidget(buildCover(meta.value("template", "blank"), card));
            auto *left = new QVBoxLayout();
            auto *title = new QLabel(d.fileName(), card);
            title->setObjectName("cardTitle");
            auto *path = new QLabel(d.absoluteFilePath(), card);
            path->setObjectName("cardPath");
            left->addWidget(title);
            left->addWidget(path);
            cl->addLayout(left, 1);
            auto *open = new QPushButton(L("打开", "Open"), card);
            open->setObjectName("ghost");
            open->setCursor(Qt::PointingHandCursor);
            QString p = d.absoluteFilePath();
            connect(open, &QPushButton::clicked, this, [this, p]() { onOpenProject(p); });
            cl->addWidget(open);
            projListLay_->addWidget(card);
            if (isNew) highlightedCard_ = card;
        }
        if (!any) {
            auto *empty = new QLabel(L("还没有项目 —— 点右上角「＋ 新建项目」开始创作吧！", "No projects yet — click ＋ New Project to start!"), this);
            empty->setObjectName("pageSub");
            empty->setAlignment(Qt::AlignCenter);
            projListLay_->addWidget(empty);
        }
        projListLay_->addStretch();
    }

    // ---------- 页面构建 ----------
    QWidget *buildProjectsPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);

        auto *head = new QHBoxLayout();
        auto *t = new QLabel(L("项目", "Projects"), w);
        t->setObjectName("pageH1");
        auto *s = new QLabel(L("你的所有项目都在这里", "All your projects live here"), w);
        s->setObjectName("pageSub");
        auto *tv = new QVBoxLayout(); tv->addWidget(t); tv->addWidget(s);
        head->addLayout(tv, 1);
        auto *btnNewProject_ = new QPushButton(L("＋  新建项目", "＋  New Project"), w);
        btnNewProject_->setObjectName("primary");
        btnNewProject_->setCursor(Qt::PointingHandCursor);
        btnNewProject_->setFixedHeight(40);
        connect(btnNewProject_, &QPushButton::clicked, this, [this]() { selectNav(1); });
        head->addWidget(btnNewProject_);
        lay->addLayout(head);

        card_ = new QFrame(w);
        card_->setObjectName("engineCard");
        auto *cv = new QVBoxLayout(card_);
        cv->setContentsMargins(16, 12, 16, 12);
        cardText_ = new QLabel(card_);
        cardText_->setWordWrap(true);
        cv->addWidget(cardText_);
        lay->addWidget(card_);

        btnOpen_ = new QPushButton(L("▶  打开编辑器（不加载项目）", "▶  Open Editor (no project)"), w);
        btnOpen_->setObjectName("ghost");
        connect(btnOpen_, &QPushButton::clicked, this, &HubWindow::onLaunchEditor);
        lay->addWidget(btnOpen_);

        auto *title2 = new QLabel(L("最近项目", "Recent projects"), w);
        title2->setObjectName("pageSub");
        lay->addWidget(title2);

        projScroll_ = new QScrollArea(w);
        projScroll_->setWidgetResizable(true);
        projScroll_->setFrameShape(QFrame::NoFrame);
        auto *listHost = new QWidget(projScroll_);
        projListLay_ = new QVBoxLayout(listHost);
        projListLay_->setContentsMargins(0, 0, 0, 0);
        projListLay_->setSpacing(10);
        projScroll_->setWidget(listHost);
        lay->addWidget(projScroll_, 1);
        return w;
    }

    QWidget *buildNewProjectPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);
        auto *t = new QLabel(L("新建项目", "New Project"), w);
        t->setObjectName("pageH1");
        auto *s = new QLabel(L("选一个模板，起个名字，马上开做", "Pick a template, name it, and go"), w);
        s->setObjectName("pageSub");
        lay->addWidget(t);
        lay->addWidget(s);

        const struct { const char *icon, *zh, *en, *descZh, *descEn; } tpl[] = {
            {"🌄", "3D 空白项目", "3D Blank", "从零开始搭建你的世界", "Build your world from scratch"},
            {"🎳", "3D 物理沙盒", "3D Physics Sandbox", "自带刚体与碰撞演示", "Rigidbodies & collisions included"},
            {"🎮", "2D 平台跳跃", "2D Platformer", "横版跳跃起步模板", "Side-scroller starter"},
            {"🔌", "插座蓝图演示", "Socket Blueprint Demo", "事件与逻辑连线示例", "Event & logic wiring sample"},
        };
        auto *grid = new QGridLayout();
        grid->setSpacing(12);
        for (int i = 0; i < 4; ++i) {
            auto *b = new QPushButton(QString("%1  %2\n%3").arg(tpl[i].icon).arg(L(tpl[i].zh, tpl[i].en)).arg(L(tpl[i].descZh, tpl[i].descEn)), w);
            b->setObjectName("tplBtn");
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(86);
            connect(b, &QPushButton::clicked, this, [this, i]() {
                selTpl_ = i;
                for (int j = 0; j < tplBtns_.size(); ++j) tplBtns_[j]->setChecked(j == i);
            });
            grid->addWidget(b, i / 2, i % 2);
            tplBtns_.append(b);
        }
        tplBtns_[0]->setChecked(true);
        lay->addLayout(grid);

        auto *form = new QGroupBox(L("项目信息", "Project info"));
        auto *gl = new QGridLayout(form);
        gl->addWidget(new QLabel(L("项目名", "Name")), 0, 0);
        npName_ = new QLineEdit(form);
        npName_->setPlaceholderText(L("项目名字，如：MyFirstGame", "Project name, e.g. MyFirstGame"));
        gl->addWidget(npName_, 0, 1);
        gl->addWidget(new QLabel(L("保存位置", "Location")), 1, 0);
        auto *pv = new QHBoxLayout();
        npPath_ = new QLineEdit(projectsRoot(), form);
        pv->addWidget(npPath_, 1);
        auto *browse = new QPushButton(L("浏览…", "Browse…"), form);
        browse->setObjectName("ghost");
        connect(browse, &QPushButton::clicked, this, &HubWindow::onBrowseProjectDir);
        pv->addWidget(browse);
        gl->addLayout(pv, 1, 1);
        lay->addWidget(form);

        auto *create = new QPushButton(L("🚀  创建项目", "🚀  Create Project"), w);
        create->setObjectName("primary");
        create->setCursor(Qt::PointingHandCursor);
        create->setFixedHeight(44);
        connect(create, &QPushButton::clicked, this, &HubWindow::onCreateNewProject);
        lay->addWidget(create);
        lay->addStretch();
        return w;
    }

    // 引擎版本页：版本卡片 + 一键下载（零参数）
    QWidget *buildInstallsPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);
        auto *t = new QLabel(L("引擎版本", "Installs"), w);
        t->setObjectName("pageH1");
        auto *s = new QLabel(L("管理已安装的引擎版本", "Manage installed engine versions"), w);
        s->setObjectName("pageSub");
        lay->addWidget(t);
        lay->addWidget(s);

        auto *card = new QFrame(w);
        card->setObjectName("instCard");
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(18, 16, 18, 16);
        cv->setSpacing(10);
        auto *name = new QLabel("⚡  NovaEngine3D  v1.0.0-LTS", card);
        name->setObjectName("cardTitle");
        instStatus_ = new QLabel(card);
        auto *desc = new QLabel(L("包含：3D 编辑器、插座蓝图编辑器、Lua 运行时、物理沙盒、打包工具", "Includes: 3D editor, socket blueprint editor, Lua runtime, physics sandbox, build tools"), card);
        desc->setObjectName("cardPath");
        desc->setWordWrap(true);
        cv->addWidget(name);
        cv->addWidget(instStatus_);
        cv->addWidget(desc);

        auto *btnRow = new QHBoxLayout();
        btnDl_ = new QPushButton(L("⬇  一键下载", "⬇  One-Click Download"), card);
        btnDl_->setObjectName("primary");
        btnDl_->setCursor(Qt::PointingHandCursor);
        btnDl_->setFixedHeight(40);
        connect(btnDl_, &QPushButton::clicked, this, &HubWindow::onOneClickDownload);
        btnRow->addWidget(btnDl_);
        btnOpenDir_ = new QPushButton(L("📂 打开目录", "📂 Open Folder"), card);
        btnOpenDir_->setObjectName("ghost");
        btnOpenDir_->setCursor(Qt::PointingHandCursor);
        connect(btnOpenDir_, &QPushButton::clicked, this, &HubWindow::onOpenInstallDir);
        btnRow->addWidget(btnOpenDir_);
        btnUninstall_ = new QPushButton(L("🗑 卸载", "🗑 Uninstall"), card);
        btnUninstall_->setObjectName("danger");
        btnUninstall_->setCursor(Qt::PointingHandCursor);
        connect(btnUninstall_, &QPushButton::clicked, this, &HubWindow::onUninstall);
        btnRow->addWidget(btnUninstall_);
        btnRow->addStretch();
        cv->addLayout(btnRow);

        bar_ = new QProgressBar(card);
        bar_->setValue(0);
        bar_->hide();
        cv->addWidget(bar_);
        status_ = new QLabel(card);
        status_->setWordWrap(true);
        cv->addWidget(status_);
        lay->addWidget(card);
        lay->addStretch();
        return w;
    }

    // 学习中心：四大板块标签页
    QWidget *buildLearnPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);
        auto *t = new QLabel(L("学习中心", "Learn"), w);
        t->setObjectName("pageH1");
        auto *s = new QLabel(L("从蓝图到打包，一篇讲透", "From blueprints to packaging"), w);
        s->setObjectName("pageSub");
        lay->addWidget(t);
        lay->addWidget(s);

        auto *tabs = new QTabWidget(w);
        const struct { const char *zh, *en; QString (*fn)(); } docs[] = {
            {"插座蓝图指南", "Blueprint Guide", learnBlueprintHtml},
            {"Lua 脚本 API 手册", "Lua API Manual", learnLuaHtml},
            {"3D 物理组件详解", "Physics Guide", learnPhysicsHtml},
            {"打包与加密指南", "Packaging & Encryption", learnPackageHtml},
        };
        for (const auto &d : docs) {
            auto *tb = new QTextBrowser(tabs);
            tb->setOpenExternalLinks(true);
            tb->setHtml(d.fn());
            tabs->addTab(tb, L(d.zh, d.en));
        }
        lay->addWidget(tabs, 1);
        return w;
    }

    QWidget *buildSettingsPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);
        auto *t = new QLabel(L("设置", "Settings"), w);
        t->setObjectName("pageH1");
        auto *s = new QLabel(L("语言、主题、网络与存储", "Language, theme, network and storage"), w);
        s->setObjectName("pageSub");
        lay->addWidget(t);
        lay->addWidget(s);
        QSettings st(kOrg, kApp);

        auto *langBox = new QGroupBox(L("语言 / Language", "Language / 语言"));
        auto *lv = new QVBoxLayout(langBox);
        QComboBox *lang = new QComboBox(langBox);
        lang->addItem("简体中文");
        lang->addItem("English");
        lang->setCurrentIndex(st.value("lang", 0).toInt());
        connect(lang, &QComboBox::currentIndexChanged, this, [this](int i) {
            QSettings st(kOrg, kApp);
            st.setValue("lang", i);
            QMessageBox::information(this, L("提示", "Info"), L("语言已保存，重启启动器生效。", "Language saved. Restart the launcher to apply."));
        });
        lv->addWidget(lang);
        lay->addWidget(langBox);

        auto *themeBox = new QGroupBox(L("主题", "Theme"));
        auto *thv = new QVBoxLayout(themeBox);
        QComboBox *theme = new QComboBox(themeBox);
        theme->addItem(L("🌙 暗黑主题", "🌙 Dark theme"));
        theme->addItem(L("☀️ 明亮主题", "☀️ Light theme"));
        theme->setCurrentIndex(st.value("theme", 0).toInt());
        connect(theme, &QComboBox::currentIndexChanged, this, [this](int i) {
            QSettings st(kOrg, kApp);
            st.setValue("theme", i);
            dark_ = (i == 0);
            qApp->setStyleSheet(themeQss(dark_)); // 即时切换，无需重启
        });
        thv->addWidget(theme);
        lay->addWidget(themeBox);

        auto *net = new QGroupBox(L("网络", "Network"));
        auto *nv = new QGridLayout(net);
        auto *hint = new QLabel(L("下载慢？在下面填上你的 HTTP 代理（如 Clash 的 127.0.0.1:7890），下载会走代理加速。留空则直连。", "Slow downloads? Fill in your HTTP proxy below (e.g. 127.0.0.1:7890). Leave empty for direct connection."), net);
        hint->setWordWrap(true);
        hint->setObjectName("pageSub");
        nv->addWidget(hint, 0, 0, 1, 2);
        nv->addWidget(new QLabel(L("HTTP 代理 IP", "Proxy host")), 1, 0);
        proxyEdit_ = new QLineEdit(st.value("proxyHost", "").toString(), net);
        proxyEdit_->setPlaceholderText("127.0.0.1");
        nv->addWidget(proxyEdit_, 1, 1);
        nv->addWidget(new QLabel(L("端口", "Port")), 2, 0);
        portEdit_ = new QLineEdit(st.value("proxyPort", "").toString(), net);
        portEdit_->setPlaceholderText("7890");
        nv->addWidget(portEdit_, 2, 1);
        auto *saveNet = new QPushButton(L("保存网络设置", "Save network settings"), net);
        saveNet->setObjectName("ghost");
        connect(saveNet, &QPushButton::clicked, this, [this]() {
            QSettings st(kOrg, kApp);
            st.setValue("proxyHost", proxyEdit_->text().trimmed());
            st.setValue("proxyPort", portEdit_->text().toInt());
            QMessageBox::information(this, L("完成", "Done"), L("已保存，下载时将使用该代理。", "Saved. Downloads will use this proxy."));
        });
        nv->addWidget(saveNet, 3, 0, 1, 2);
        lay->addWidget(net);

        auto *misc = new QGroupBox(L("存储", "Storage"));
        auto *mv = new QVBoxLayout(misc);
        mv->addWidget(new QLabel(L("默认下载路径", "Default download path"), misc));
        auto *pathEdit = new QLineEdit(st.value("downloadPath", "/tmp").toString(), misc);
        mv->addWidget(pathEdit);
        auto *clearBtn = new QPushButton(L("🧹 清除下载缓存", "🧹 Clear download cache"), misc);
        clearBtn->setObjectName("ghost");
        connect(clearBtn, &QPushButton::clicked, this, &HubWindow::onClearCache);
        mv->addWidget(clearBtn);
        lay->addWidget(misc);
        lay->addStretch();
        return w;
    }

    QWidget *buildAboutPage() {
        auto *w = new QWidget(this);
        w->setObjectName("pageRoot");
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(28, 24, 28, 20);
        lay->setSpacing(14);
        auto *t = new QLabel(L("关于", "About"), w);
        t->setObjectName("pageH1");
        lay->addWidget(t);

        auto *box = new QGroupBox(L("NovaEngine3D", "NovaEngine3D"));
        auto *bv = new QVBoxLayout(box);
        auto *info = new QLabel(QString(
            "<b>%1</b> v1.0.0<br>"
            "%2: %3 %4<br><br>"
            "<b>%5</b><br>"
            "• Qt 6 — %6<br>"
            "• Bullet Physics — %7<br>"
            "• OpenSSL — %8<br><br>"
            "<b>%9</b> MIT License<br><br>"
            "%10 <a href=\"https://github.com/Jdj497949/PhysicsEngine3D\">github.com/Jdj497949/PhysicsEngine3D</a>")
            .arg(L("引擎版本", "Engine version"),
                 L("编译时间", "Build time"), kBuildDate, kBuildTime,
                 L("使用的开源库", "Open-source libraries"),
                 L("跨平台界面框架（LGPL）", "cross-platform UI framework (LGPL)"),
                 L("物理引擎（zlib）", "physics engine (zlib)"),
                 L("加密（OpenSSL/Apache）", "encryption (OpenSSL/Apache)"),
                 L("开源协议", "License"),
                 L("作者主页", "Author")))
            ;
        info->setWordWrap(true);
        info->setOpenExternalLinks(true);
        info->setTextInteractionFlags(Qt::TextBrowserInteraction);
        info->setAlignment(Qt::AlignTop);
        bv->addWidget(info);
        bv->addStretch();
        lay->addWidget(box, 1);
        return w;
    }

    QStackedWidget *stack_ = nullptr;
    QList<QPushButton*> navBtns_, tplBtns_;
    QFrame *card_ = nullptr;
    QLabel *cardText_ = nullptr, *status_ = nullptr, *instStatus_ = nullptr;
    QLineEdit *npName_ = nullptr, *npPath_ = nullptr, *proxyEdit_ = nullptr, *portEdit_ = nullptr;
    QPushButton *btnDl_ = nullptr, *btnOpen_ = nullptr, *btnOpenDir_ = nullptr, *btnUninstall_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QScrollArea *projScroll_ = nullptr;
    QVBoxLayout *projListLay_ = nullptr;
    QFrame *highlightedCard_ = nullptr;
    int selTpl_ = 0;
    bool dark_ = true;
    DownloadManager dl_{this};
    QMetaObject::Connection metaConn_, progConn_, doneConn_, failConn_;
};

#include "VNLauncher.moc"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QSettings st(kOrg, kApp);
    g_lang = st.value("lang", 0).toInt();

    if (QCoreApplication::arguments().contains("--selftest")) {
        HubWindow hub;
        hub.runSelfTest();
        return app.exec();
    }
    HubWindow w;
    w.show();
    return app.exec();
}
