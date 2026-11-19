// 3DLauncher —— 项目创建与列表管理。独立程序，不含任何 3D / 物理代码。
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QTimer>
#include <QStyle>
#include <QLineEdit>
#include <QDialog>
#include "../common/CrashHandler.h"
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QScreen>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QTabWidget>
#include <QComboBox>
#include <QSettings>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QVersionNumber>
#include <QDateTime>

// 引擎版本号：写入 Project.vnproj，并在导入时与项目版本比对
static const QString kEngineVersion = QStringLiteral("v1.2.0");

// 版本兼容：项目版本 <= 引擎版本 即视为向下兼容
static bool versionCompatible(const QString &proj, const QString &cur) {
    QString p = proj.startsWith('v') ? proj.mid(1) : proj;
    QString c = cur.startsWith('v') ? cur.mid(1) : cur;
    QVersionNumber pv = QVersionNumber::fromString(p);
    QVersionNumber cv = QVersionNumber::fromString(c);
    if (pv.isNull() || cv.isNull()) return false;
    return pv <= cv;
}

static QString registryPath() {
    return QDir::home().filePath(".physics3d_launcher.json");
}

// ============ 新建项目 · 高级设置（Advanced Settings） ============
struct AdvancedSettings {
    QString renderer = "OpenGL 3.3";   // 渲染管线
    int physicsRate = 60;              // 物理步进率 Hz
    double gx = 0, gy = -9.81, gz = 0; // 初始物理重力
    QString font;                      // 默认字体 .ttf
};

// 构建高级设置面板（新建项目对话框复用），out 参数带回控件引用
static QWidget *buildAdvancedPanel(const AdvancedSettings &init,
                                   QComboBox **rendOut, QComboBox **rateOut,
                                   QDoubleSpinBox **gxOut, QDoubleSpinBox **gyOut, QDoubleSpinBox **gzOut,
                                   QLineEdit **fontOut) {
    auto *panel = new QWidget;
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(0, 4, 0, 0);
    v->setSpacing(8);

    auto *group = new QGroupBox("高级设置 (Advanced Settings)");
    auto *gv = new QVBoxLayout(group);
    gv->setSpacing(8);

    // 渲染管线
    gv->addWidget(new QLabel("渲染管线："));
    auto *rend = new QComboBox;
    rend->addItem("OpenGL 3.3");
    rend->addItem("OpenGL 4.5");
    rend->addItem("Vulkan");
    rend->addItem("Software 软件渲染");
    rend->setCurrentText(init.renderer);
    gv->addWidget(rend);

    // 物理步进率
    gv->addWidget(new QLabel("物理步进率："));
    auto *rate = new QComboBox;
    rate->addItem("60Hz");
    rate->addItem("120Hz");
    rate->addItem("240Hz");
    rate->setCurrentIndex(init.physicsRate == 60 ? 0 : init.physicsRate == 120 ? 1 : 2);
    gv->addWidget(rate);

    // 初始重力 X/Y/Z
    gv->addWidget(new QLabel("初始物理重力 (X, Y, Z)："));
    auto *grow = new QHBoxLayout;
    auto mkSpin = [](double val) {
        auto *sp = new QDoubleSpinBox;
        sp->setRange(-100.0, 100.0);
        sp->setDecimals(2);
        sp->setSingleStep(0.5);
        sp->setValue(val);
        return sp;
    };
    auto *gx = mkSpin(init.gx), *gy = mkSpin(init.gy), *gz = mkSpin(init.gz);
    grow->addWidget(gx); grow->addWidget(gy); grow->addWidget(gz);
    gv->addLayout(grow);

    // 默认字体
    gv->addWidget(new QLabel("默认字体（.ttf，项目全局字体）："));
    auto *frow = new QHBoxLayout;
    auto *fontEdit = new QLineEdit;
    fontEdit->setPlaceholderText("（未选择 = 引擎默认字体）");
    fontEdit->setText(init.font);
    fontEdit->setReadOnly(true);
    frow->addWidget(fontEdit, 1);
    auto *btnFont = new QPushButton("浏览…");
    QObject::connect(btnFont, &QPushButton::clicked, panel, [panel, fontEdit]{
        QString f = QFileDialog::getOpenFileName(panel, "选择默认字体", QString(),
                                                 "字体文件 (*.ttf *.otf)");
        if (!f.isEmpty()) fontEdit->setText(f);
    });
    frow->addWidget(btnFont);
    gv->addLayout(frow);

    v->addWidget(group);
    if (rendOut) *rendOut = rend;
    if (rateOut) *rateOut = rate;
    if (gxOut) *gxOut = gx;
    if (gyOut) *gyOut = gy;
    if (gzOut) *gzOut = gz;
    if (fontOut) *fontOut = fontEdit;
    return panel;
}
static QString editorPath() {
    QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath("3DEditor");
}
static QString editor2DPath() {
    QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath("VN2DEditor");
}

static QJsonArray loadRegistry() {
    QFile f(registryPath());
    if (f.open(QIODevice::ReadOnly)) {
        return QJsonDocument::fromJson(f.readAll()).array();
    }
    return {};
}
static void saveRegistry(const QJsonArray &arr) {
    QFile f(registryPath());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(arr).toJson());
}

class LauncherWindow : public QMainWindow {
public:
    QListWidget *list;
    QLabel *status;
    QTabWidget *tabs = nullptr;
    QLineEdit *pathEdit = nullptr;
    QComboBox *precisionCombo = nullptr;
    QComboBox *langCombo = nullptr;

    LauncherWindow() {
        setWindowTitle(QString("3D 物理编程引擎 - 项目启动器 (%1)").arg(kEngineVersion));
        resize(760, 560);

        auto *central = new QWidget;
        auto *h = new QHBoxLayout(central);

        // 左侧：操作按钮
        auto *left = new QVBoxLayout;
        auto *title = new QLabel("我的项目");
        title->setStyleSheet("font-size:18px; font-weight:bold; padding:6px 2px;");
        left->addWidget(title);

        auto *btnNew = new QPushButton("新建 3D 项目");
        btnNew->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
        btnNew->setMinimumHeight(38);
        left->addWidget(btnNew);

        auto *btnImport = new QPushButton("导入现有项目");
        btnImport->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
        btnImport->setMinimumHeight(38);
        left->addWidget(btnImport);

        auto *btnOpen = new QPushButton("打开（进入编辑器）");
        btnOpen->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
        btnOpen->setMinimumHeight(38);
        left->addWidget(btnOpen);

        auto *btnRemove = new QPushButton("从列表移除");
        btnRemove->setMinimumHeight(30);
        left->addWidget(btnRemove);

        left->addStretch();

        // 底部：目录直达按钮
        auto *btnOpenEditorDir = new QPushButton("打开编辑器所在目录");
        btnOpenEditorDir->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        btnOpenEditorDir->setToolTip(QCoreApplication::applicationDirPath());
        connect(btnOpenEditorDir, &QPushButton::clicked, this, []{
            QDesktopServices::openUrl(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()));
        });
        left->addWidget(btnOpenEditorDir);

        auto *btnOpenProjDir = new QPushButton("打开项目所在目录");
        btnOpenProjDir->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        connect(btnOpenProjDir, &QPushButton::clicked, this, [this]{
            auto *it = list->currentItem();
            if (!it) { status->setText("先在右侧选中一个项目"); return; }
            QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
        });
        left->addWidget(btnOpenProjDir);

        status = new QLabel("就绪");
        status->setStyleSheet("color:#4a6; padding:4px;");
        left->addWidget(status);
        h->addLayout(left, 0);

        // 右侧：项目列表
        list = new QListWidget;
        list->setAlternatingRowColors(true);
        list->setStyleSheet("QListWidget::item{height:34px; font-size:14px;}");
        h->addWidget(list, 1);

        // 顶部选项卡：项目 / 设置
        tabs = new QTabWidget;
        tabs->addTab(central, "项目");
        tabs->addTab(buildSettingsPage(), "设置");
        setCentralWidget(tabs);

        connect(btnNew, &QPushButton::clicked, this, &LauncherWindow::createProject);
        connect(btnImport, &QPushButton::clicked, this, &LauncherWindow::importProject);
        connect(btnOpen, &QPushButton::clicked, this, &LauncherWindow::openSelected);
        connect(btnRemove, &QPushButton::clicked, this, &LauncherWindow::removeSelected);
        connect(list, &QListWidget::itemDoubleClicked, this, &LauncherWindow::openSelected);

        reload();
        loadLauncherSettings();
    }

    QWidget *buildSettingsPage() {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(32, 28, 32, 16);
        v->setSpacing(14);

        auto *title = new QLabel("全局设置");
        title->setStyleSheet("font-size:18px; font-weight:bold;");
        v->addWidget(title);

        // 项目管理设置
        v->addWidget(new QLabel("项目管理设置"));
        auto *row = new QHBoxLayout;
        pathEdit = new QLineEdit;
        pathEdit->setPlaceholderText("默认项目存放路径");
        row->addWidget(pathEdit, 1);
        auto *btnBrowse = new QPushButton("浏览…");
        connect(btnBrowse, &QPushButton::clicked, this, [this]{
            QString d = QFileDialog::getExistingDirectory(this, "选择默认项目路径", pathEdit->text());
            if (!d.isEmpty()) pathEdit->setText(d);
        });
        row->addWidget(btnBrowse);
        v->addLayout(row);

        // 核心设置
        v->addWidget(new QLabel("核心设置"));
        precisionCombo = new QComboBox;
        precisionCombo->addItems({"低（流畅优先 · 物理 30Hz）",
                                  "标准（推荐 · 物理 60Hz）",
                                  "高（精度优先 · 物理 120Hz）"});
        precisionCombo->setCurrentIndex(1);
        v->addWidget(precisionCombo);

        // 语言设置
        v->addWidget(new QLabel("语言设置"));
        langCombo = new QComboBox;
        langCombo->addItem("简体中文");
        v->addWidget(langCombo);
        auto *langNote = new QLabel("后续版本将支持更多语言");
        langNote->setStyleSheet("color:#779; font-size:11px;");
        v->addWidget(langNote);

        v->addStretch();
        auto *btnSave = new QPushButton("保存设置");
        btnSave->setStyleSheet("background:#2f6fab;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        connect(btnSave, &QPushButton::clicked, this, [this]{
            QSettings s("PhysicsEngine3D", "3DLauncher");
            s.setValue("projects/defaultPath", pathEdit->text());
            s.setValue("core/physicsPrecision", precisionCombo->currentIndex());
            s.setValue("general/language", langCombo->currentText());
            status->setText("设置已保存（QSettings 持久化）");
        });
        v->addWidget(btnSave);
        return page;
    }

    void loadLauncherSettings() {
        QSettings s("PhysicsEngine3D", "3DLauncher");
        pathEdit->setText(s.value("projects/defaultPath",
            QDir::home().filePath("Physics3DProjects")).toString());
        precisionCombo->setCurrentIndex(s.value("core/physicsPrecision", 1).toInt());
    }

    void switchToSettings() { tabs->setCurrentWidget(tabs->findChild<QWidget*>()); tabs->setCurrentIndex(1); }

    void reload() {
        list->clear();
        for (const auto &v : loadRegistry()) {
            QJsonObject o = v.toObject();
            QString typeTag = o["type"].toString() == "2d" ? "【2D】" : "【3D】";
            auto *it = new QListWidgetItem(style()->standardIcon(QStyle::SP_DirIcon),
                                           typeTag + o["name"].toString() + "   —   " + o["path"].toString());
            it->setData(Qt::UserRole, o["path"].toString());
            it->setData(Qt::UserRole + 1, o["type"].toString("3d"));
            list->addItem(it);
        }
    }

    void createProject() {
        QString name;
        QString type = "3d";
        QString tmpl;   // 模板：blank / action3d / 2ddemo / grass3d / ui2d
        AdvancedSettings adv;
        // 自动演示模式：直接取环境变量里的名字与类型
        QString autoName = qEnvironmentVariable("3D_AUTO_NAME");
        if (!autoName.isEmpty()) {
            name = autoName;
            type = qEnvironmentVariable("3D_AUTO_TYPE");
            if (type.isEmpty()) type = "3d";
            tmpl = qEnvironmentVariable("3D_AUTO_TEMPLATE");
            QString rend = qEnvironmentVariable("3D_AUTO_RENDERER");
            if (!rend.isEmpty()) adv.renderer = rend;
            QString rate = qEnvironmentVariable("3D_AUTO_RATE");
            if (!rate.isEmpty()) adv.physicsRate = rate.toInt();
            QString grav = qEnvironmentVariable("3D_AUTO_GRAV");
            if (!grav.isEmpty()) {
                QStringList parts = grav.split(',');
                if (parts.size() == 3) {
                    adv.gx = parts[0].toDouble(); adv.gy = parts[1].toDouble(); adv.gz = parts[2].toDouble();
                }
            }
            adv.font = qEnvironmentVariable("3D_AUTO_FONT");
        } else {
            if (!getNewProjectSettings(name, type, tmpl, adv)) return;
        }
        if (name.isEmpty()) return;

        QString base = QDir::home().filePath("Physics3DProjects");
        QDir().mkpath(base);
        QString path = base + "/" + name;
        int n = 1;
        while (QDir(path).exists()) path = base + "/" + name + QString::number(++n);

        QDir().mkpath(path + "/assets");
        QDir().mkpath(path + "/scenes");

        // ============ Project.vnproj：项目的“灵魂”入口文件 ============
        QJsonObject vnproj{
            {"projectName", name},
            {"engine", "PhysicsEngine3D"},
            {"engineVersion", kEngineVersion},
            {"renderer", adv.renderer},
            {"physicsRate", adv.physicsRate},
            {"gravity", QJsonArray{adv.gx, adv.gy, adv.gz}},
            {"font", adv.font},
            {"assetDirs", QJsonArray{"assets", "scenes"}},
            {"type", type},
            {"template", tmpl},
            {"created", QDateTime::currentDateTime().toString(Qt::ISODate)}
        };
        QFile vf(path + "/Project.vnproj");
        if (vf.open(QIODevice::WriteOnly))
            vf.write(QJsonDocument(vnproj).toJson(QJsonDocument::Indented));

        QFile pf(path + "/project.json");
        if (pf.open(QIODevice::WriteOnly)) {
            pf.write(QJsonDocument(QJsonObject{{"name", name}, {"type", type},
                                               {"engine", "PhysicsEngine3D"}, {"version", "1.0"}}).toJson());
        }

        // 项目模板：3D 项目默认主摄像机+平行光；3D 动作模板加大地板+胶囊玩家（CharacterController）；
        // 2D 平台跳跃模板生成 level2d.scene2d；草地模板铺摇摆草地
        if (type != "2d") {
            QJsonArray spawn;
            spawn.append(QJsonObject{{"type", 5}, {"name", "Main Camera"},
                                     {"pos", QJsonArray{3, 2.5, 3}}});
            spawn.append(QJsonObject{{"type", 6}, {"name", "Directional Light"},
                                     {"pos", QJsonArray{-3, 4, 2}}});
            if (tmpl == "action3d") {
                // 超大带碰撞地板
                spawn.append(QJsonObject{
                    {"type", 2}, {"name", "大地板 Floor"},
                    {"pos", QJsonArray{0, 0, 0}}, {"scale", QJsonArray{30, 0.05, 30}},
                    {"color", QJsonArray{0.55, 0.45, 0.30}}, {"static", true}});
                // 胶囊玩家：RigidBody 动态刚体 + CharacterController 组件（属性面板可调）
                spawn.append(QJsonObject{
                    {"type", 4}, {"name", "Player"},
                    {"pos", QJsonArray{0, 2.0, 5}}, {"scale", QJsonArray{1, 1.6, 1}},
                    {"color", QJsonArray{0.90, 0.55, 0.85}}, {"static", false},
                    {"components", QJsonArray{QJsonObject{
                        {"type", "CharacterController"},
                        {"params", QJsonObject{{"walkSpeed", 5.0}, {"jumpForce", 6.0}, {"gravityScale", 1.0}}}
                    }}}});
            } else if (tmpl == "grass3d") {
                for (int gx = 0; gx < 2; ++gx)
                    for (int gz = 0; gz < 2; ++gz)
                        spawn.append(QJsonObject{
                            {"type", 9}, {"name", QString("Grass_%1%2").arg(gx).arg(gz)},
                            {"pos", QJsonArray{gx * 12.0 - 6, 0, gz * 12.0 - 6}},
                            {"scale", QJsonArray{6, 1, 6}},
                            {"color", QJsonArray{0.35, 0.75, 0.3}},
                            {"static", true}, {"wind", 0.25}});
            }
            QFile df(path + "/director.json");
            if (df.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QJsonObject root;
                // 保留已有 objects 数组（新项目为空）
                root.insert("objects", QJsonArray{});
                root.insert("spawn", spawn);
                df.write(QJsonDocument(root).toJson());
            }
        } else if (tmpl == "ui2d") {
            // 2D UI 演示模板：生成一张示例画布
            QFile cf(path + "/assets/主界面.uicanvas");
            if (cf.open(QIODevice::WriteOnly)) {
                cf.write(QJsonDocument(QJsonObject{
                    {"kind", "uicanvas"}, {"name", "主界面.uicanvas"},
                    {"designWidth", 1600}, {"designHeight", 900},
                    {"items", QJsonArray{QJsonObject{
                        {"type", "text"}, {"text", "欢迎来到 PhysicsEngine3D"}, {"fontSize", 36},
                        {"color", "#ffe08a"},
                        {"rect", QJsonObject{{"x", 120}, {"y", 100}, {"w", 560}, {"h", 52}}}, {"anchor", 0}
                    }}}
                }).toJson());
            }
        } else if (tmpl == "2ddemo") {
            // 2D 平台跳跃演示模板：平台（静态碰撞体）+ 玩家（RigidBody2D）+ 金币
            auto box = [](const QString &nm, double x, double y, double w, double h,
                          const QString &col, bool stat) {
                return QJsonObject{{"type", "box2d"}, {"name", nm},
                                   {"rect", QJsonObject{{"x", x}, {"y", y}, {"w", w}, {"h", h}}},
                                   {"color", col}, {"hasBody", true}, {"isStatic", stat}};
            };
            QJsonArray boxes{
                box("玩家_Player", 180, 560, 70, 90, "#4aa3e0", false),
                box("平台_1", 520, 720, 280, 26, "#7a8a5a", true),
                box("平台_2", 960, 520, 280, 26, "#7a8a5a", true),
                box("金币_1", 620, 660, 44, 44, "#ffd24a", true),
                box("金币_2", 1060, 460, 44, 44, "#ffd24a", true),
                box("金币_3", 380, 816, 44, 44, "#ffd24a", true)
            };
            QFile lf(path + "/assets/level2d.scene2d");
            if (lf.open(QIODevice::WriteOnly)) {
                lf.write(QJsonDocument(QJsonObject{
                    {"kind", "scene2d"}, {"name", "level2d.scene2d"}, {"gravity", 980},
                    {"boxes", boxes}}).toJson());
            }
        }

        QJsonArray arr = loadRegistry();
        arr.append(QJsonObject{{"name", name}, {"path", path}, {"type", type}});
        saveRegistry(arr);
        status->setText(QString("已创建%1项目：%2（Project.vnproj 已写入）").arg(type == "2d" ? "2D" : "3D", name));
        reload();
    }

    // 新建项目对话框（模态）：名称 + 类型 + 模板 + 高级设置面板
    bool getNewProjectSettings(QString &outName, QString &outType, QString &outTmpl, AdvancedSettings &adv) {
        QDialog dlg(this);
        dlg.setWindowTitle("新建项目");
        dlg.resize(480, 640);
        auto *lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel("项目名称："));
        auto *edit = new QLineEdit;
        edit->setText(QString("MyProject"));
        lay->addWidget(edit);
        lay->addWidget(new QLabel("项目类型："));
        auto *rb3d = new QRadioButton("3D 项目（3D 编辑器 3DEditor）");
        auto *rb2d = new QRadioButton("2D 项目（2D 编辑器 VN2DEditor / VNUIEditor）");
        rb3d->setChecked(true);
        lay->addWidget(rb3d);
        lay->addWidget(rb2d);
        lay->addWidget(new QLabel("项目模板："));
        auto *tmplCombo = new QComboBox;
        tmplCombo->addItem("空白项目", "blank");
        tmplCombo->addItem("3D 动作演示模板（胶囊角色控制器：走/跑/跳/蹲）", "action3d");
        tmplCombo->addItem("2D 演示模板（平台跳跃：RigidBody2D + 金币计分）", "2ddemo");
        tmplCombo->addItem("3D 草地演示项目（默认摄像机+平行光+摇摆草地）", "grass3d");
        tmplCombo->addItem("2D UI 演示项目（示例画布 主界面.uicanvas）", "ui2d");
        lay->addWidget(tmplCombo);
        QComboBox *rend, *rate;
        QDoubleSpinBox *gx, *gy, *gz;
        QLineEdit *fontEdit;
        lay->addWidget(buildAdvancedPanel(adv, &rend, &rate, &gx, &gy, &gz, &fontEdit));
        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(btns);
        if (dlg.exec() == QDialog::Accepted) {
            outName = edit->text();
            outType = rb2d->isChecked() ? "2d" : "3d";
            outTmpl = tmplCombo->currentData().toString();
            adv.renderer = rend->currentText();
            adv.physicsRate = QStringList{"60", "120", "240"}.at(rate->currentIndex()).toInt();
            adv.gx = gx->value(); adv.gy = gy->value(); adv.gz = gz->value();
            adv.font = fontEdit->text();
            return true;
        }
        outName.clear();
        return false;
    }

    // ============ 导入现有项目：QFileDialog 只认 .vnproj + 引擎版本比对 ============
    void importProject() {
        QString base = QDir::home().filePath("Physics3DProjects");
        QString file = QFileDialog::getOpenFileName(this, "导入现有项目（选择 .vnproj 项目文件）",
                                                    base, "项目文件 (*.vnproj)");
        if (file.isEmpty()) return;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "错误", "无法读取项目文件"); return; }
        QJsonParseError perr;
        QJsonObject o = QJsonDocument::fromJson(f.readAll(), &perr).object();
        if (perr.error != QJsonParseError::NoError || o.isEmpty()
            || o.value("engine").toString() != "PhysicsEngine3D"
            || !o.contains("engineVersion")) {
            QMessageBox::warning(this, "错误", "这不是合法的 PhysicsEngine3D 项目文件\n"
                                               "（Project.vnproj 缺少引擎元数据）");
            return;
        }
        QString pv = o.value("engineVersion").toString();
        QString projPath = QFileInfo(file).absolutePath();
        if (versionCompatible(pv, kEngineVersion)) {
            // 版本匹配（或向下兼容）：加入列表，双击打开
            addToRegistry(projPath, o);
            status->setText(QString("版本兼容（项目 %1 / 引擎 %2），已加入列表，双击即可打开编辑器")
                            .arg(pv, kEngineVersion));
        } else {
            // 版本不匹配：红色警告框 + 【取消导入】【强制/暴力打开】
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle("版本不兼容");
            box.setStyleSheet("QLabel{color:#c0392b; font-size:14px;}");
            box.setText(QString("⚠️ 警告：当前引擎版本 (%1) 与项目版本 (%2) 不兼容！")
                            .arg(kEngineVersion, pv));
            box.setInformativeText("强行打开将忽略版本差异，可能触发资产解析失败甚至崩溃"
                                   "（崩溃时会自动生成崩溃日志）。");
            QPushButton *btnCancel = box.addButton("取消导入", QMessageBox::RejectRole);
            QPushButton *btnForce = box.addButton("强制/暴力打开", QMessageBox::DestructiveRole);
            box.setDefaultButton(btnCancel);
            box.exec();
            if (box.clickedButton() == btnForce) {
                addToRegistry(projPath, o);
                status->setText(QString("已强制/暴力打开（忽略版本差异 %1 → %2）").arg(pv, kEngineVersion));
                forceOpenEditor(projPath, o.value("type").toString("3d"));
            }
        }
        reload();
    }

    void addToRegistry(const QString &projPath, const QJsonObject &meta) {
        QJsonArray arr = loadRegistry();
        for (const auto &v : arr)
            if (v.toObject().value("path").toString() == projPath) return;   // 防重复
        arr.append(QJsonObject{{"name", meta.value("projectName").toString(QDir(projPath).dirName())},
                               {"path", projPath},
                               {"type", meta.value("type").toString("3d")}});
        saveRegistry(arr);
    }

    // 暴力打开：忽略版本差异强行拉起编辑器（编辑器通过 VN_FORCE_OPEN 标注日志）
    void forceOpenEditor(const QString &projPath, const QString &type) {
        QString bin = (type == "2d") ? editor2DPath() : editorPath();
        qputenv("VN_FORCE_OPEN", "1");
        QProcess::startDetached(bin, {projPath});
        qunsetenv("VN_FORCE_OPEN");
    }

    // 演示钩子：真实打开新建项目对话框（非模态，含模板选择 + 高级设置面板，供全屏截图）
    QDialog *showNewProjectDialogDemo() {
        auto *dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("新建项目");
        dlg->resize(480, 660);
        dlg->move(150, 30);
        auto *lay = new QVBoxLayout(dlg);
        lay->addWidget(new QLabel("项目名称："));
        auto *edit = new QLineEdit;
        edit->setText(QString("MyProject"));
        lay->addWidget(edit);
        lay->addWidget(new QLabel("项目类型："));
        auto *rb3d = new QRadioButton("3D 项目（3D 编辑器 3DEditor）");
        auto *rb2d = new QRadioButton("2D 项目（2D 编辑器 VN2DEditor / VNUIEditor）");
        rb3d->setChecked(true);
        lay->addWidget(rb3d);
        lay->addWidget(rb2d);
        lay->addWidget(new QLabel("项目模板："));
        auto *tmplCombo = new QComboBox;
        tmplCombo->addItem("空白项目", "blank");
        tmplCombo->addItem("3D 动作演示模板（胶囊角色控制器：走/跑/跳/蹲）", "action3d");
        tmplCombo->addItem("2D 演示模板（平台跳跃：RigidBody2D + 金币计分）", "2ddemo");
        tmplCombo->addItem("3D 草地演示项目（默认摄像机+平行光+摇摆草地）", "grass3d");
        tmplCombo->addItem("2D UI 演示项目（示例画布 主界面.uicanvas）", "ui2d");
        tmplCombo->setCurrentIndex(1);   // 演示：选中 3D 动作演示模板
        lay->addWidget(tmplCombo);
        QComboBox *rend, *rate;
        QDoubleSpinBox *gx, *gy, *gz;
        QLineEdit *fontEdit;
        lay->addWidget(buildAdvancedPanel(AdvancedSettings{}, &rend, &rate, &gx, &gy, &gz, &fontEdit));
        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
        lay->addWidget(btns);
        dlg->show();
        // 演示：把模板下拉展开，三个选项一屏可见
        QTimer::singleShot(600, dlg, [tmplCombo]{ tmplCombo->showPopup(); });
        QTimer::singleShot(2600, dlg, [tmplCombo]{ tmplCombo->hidePopup(); });
        return dlg;
    }

    void openSelected() {
        auto *it = list->currentItem();
        if (!it) return;
        QString path = it->data(Qt::UserRole).toString();
        QString type = it->data(Qt::UserRole + 1).toString();
        if (type.isEmpty()) {   // 兼容旧记录：回读 project.json
            QFile pf(path + "/project.json");
            if (pf.open(QIODevice::ReadOnly))
                type = QJsonDocument::fromJson(pf.readAll()).object().value("type").toString("3d");
        }
        if (!QDir(path).exists()) { QMessageBox::warning(this, "错误", "项目目录不存在"); return; }
        // 关键：按项目类型 QProcess 独立拉起对应编辑器
        QString bin = (type == "2d") ? editor2DPath() : editorPath();
        QString binName = QFileInfo(bin).fileName();
        bool ok = QProcess::startDetached(bin, {path});
        if (ok) {
            status->setText(QString("%1 已启动：%2").arg(binName, path));
            QTimer::singleShot(1500, this, [this, binName]{
                status->setText(QString("%1 正在独立运行（关闭它不影响启动器）").arg(binName));
            });
        } else {
            QMessageBox::warning(this, "错误", QString("启动 %1 失败").arg(binName));
        }
    }

    void removeSelected() {
        int row = list->currentRow();
        if (row < 0) return;
        QJsonArray arr = loadRegistry();
        arr.removeAt(row);
        saveRegistry(arr);
        reload();
    }
};

static int runApp(QApplication &app, int argc, char **argv) {
    app.setApplicationName("3DLauncher");
    LauncherWindow w;
    w.show();

    // 自动化演示：环境变量 3D_AUTO_NAME 存在时自动创建一个项目
    if (!qEnvironmentVariable("3D_AUTO_NAME").isEmpty()) {
        QTimer::singleShot(400, &w, &LauncherWindow::createProject);
        QString shot = qEnvironmentVariable("3D_SHOT");
        if (!shot.isEmpty()) {
            QTimer::singleShot(1600, &w, [&, shot]{
                if (auto *scr = QGuiApplication::primaryScreen())
                    scr->grabWindow(0).save(shot);
            });
        }
    } else if (!qEnvironmentVariable("3D_AUTO_DIALOG").isEmpty()) {
        // 演示钩子：真实打开【项目类型】选择对话框，全屏截图
        QTimer::singleShot(600, &w, [&w]{ w.showNewProjectDialogDemo(); });
        QString shot = qEnvironmentVariable("3D_SHOT");
        if (!shot.isEmpty()) {
            QTimer::singleShot(2200, &w, [&, shot]{
                if (auto *scr = QGuiApplication::primaryScreen())
                    scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(3200, &w, [&w]{ w.close(); });
        }
    } else if (!qEnvironmentVariable("3D_AUTO_DIRSHOT").isEmpty()) {
        // 演示钩子：启动器主界面（含底部两个目录直达按钮）全屏截图
        QString shot = qEnvironmentVariable("3D_SHOT");
        QTimer::singleShot(1500, &w, [&, shot]{
            if (auto *scr = QGuiApplication::primaryScreen())
                scr->grabWindow(0).save(shot);
        });
        QTimer::singleShot(2500, &w, [&w]{ w.close(); });
    } else if (!qEnvironmentVariable("3D_AUTO_SETTINGS").isEmpty()) {
        // 演示钩子：切到【设置】选项卡并截图
        QString shot = qEnvironmentVariable("3D_SHOT");
        QTimer::singleShot(500, &w, [&w]{ w.switchToSettings(); });
        QTimer::singleShot(1800, &w, [&, shot]{
            if (auto *scr = QGuiApplication::primaryScreen())
                scr->grabWindow(0).save(shot);
        });
        QTimer::singleShot(2800, &w, [&w]{ w.close(); });
    } else if (!qEnvironmentVariable("3D_AUTO_IMPORT").isEmpty()) {
        // 演示钩子：打开【导入现有项目】文件选择框（只认 .vnproj），全屏截图
        QTimer::singleShot(500, &w, [&w]{
            auto *fd = new QFileDialog(&w);
            fd->setAttribute(Qt::WA_DeleteOnClose);
            fd->setWindowTitle("导入现有项目（选择 .vnproj 项目文件）");
            fd->setNameFilter("项目文件 (*.vnproj)");
            fd->setDirectory(QDir::home().filePath("Physics3DProjects"));
            fd->setViewMode(QFileDialog::List);
            fd->show();
            fd->move(120, 140);
        });
        QString shot = qEnvironmentVariable("3D_SHOT");
        QTimer::singleShot(1800, &w, [&, shot]{
            if (auto *scr = QGuiApplication::primaryScreen())
                scr->grabWindow(0).save(shot);
        });
        QTimer::singleShot(2800, &w, [&w]{ w.close(); });
    } else if (!qEnvironmentVariable("3D_AUTO_WARN").isEmpty()) {
        // 演示钩子：版本不兼容红色警告框（【取消导入】/【强制/暴力打开】），全屏截图
        QTimer::singleShot(500, &w, [&w]{
            auto *box = new QMessageBox(&w);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setIcon(QMessageBox::Warning);
            box->setWindowTitle("版本不兼容");
            box->setStyleSheet("QLabel{color:#c0392b; font-size:14px;}");
            box->setText("⚠️ 警告：当前引擎版本 (v1.2.0) 与项目版本 (v1.0.0) 不兼容！");
            box->setInformativeText("强行打开将忽略版本差异，可能触发资产解析失败甚至崩溃"
                                    "（崩溃时会自动生成崩溃日志）。");
            box->addButton("取消导入", QMessageBox::RejectRole);
            box->addButton("强制/暴力打开", QMessageBox::DestructiveRole);
            box->show();
        });
        QString shot = qEnvironmentVariable("3D_SHOT");
        QTimer::singleShot(1600, &w, [&, shot]{
            if (auto *scr = QGuiApplication::primaryScreen())
                scr->grabWindow(0).save(shot);
        });
        QTimer::singleShot(2800, &w, [&w]{ w.close(); });
    } else if (!qEnvironmentVariable("3D_AUTO_FORCEOPEN").isEmpty()) {
        // 演示钩子：模拟“强制/暴力打开”——带 VN_FORCE_OPEN 标记拉起编辑器（编辑器自行截图）
        QTimer::singleShot(600, &w, []{
            QString proj = qEnvironmentVariable("3D_FORCE_PROJECT");
            if (proj.isEmpty()) proj = QDir::home().filePath("Physics3DProjects/老版本演示项目");
            qputenv("VN_FORCE_OPEN", "1");
            QProcess::startDetached(editorPath(), {proj});
            qunsetenv("VN_FORCE_OPEN");
        });
        QTimer::singleShot(2400, &w, [&w]{ w.close(); });
    }
    return app.exec();
}

// 崩溃捕获：启动器采用兜底模式——崩溃只写日志，不再拉起编辑器，彻底退出（防死循环）
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("3DLauncher", appDir + "/../logs", appDir + "/3DLauncher", true);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
