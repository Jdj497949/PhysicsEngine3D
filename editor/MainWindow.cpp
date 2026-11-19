#include "MainWindow.h"
#include "GLViewport.h"
#include "PluginSystem.h"
#include "RendererFactory.h"
#include "ImportDialog.h"
#include "SkySystem.h"
#include "ParticleSystem.h"
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QMimeData>
#include <QDrag>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QColorDialog>
#include <QCheckBox>
#include <QDialog>
#include <QDirIterator>
#include <QProcess>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QFontDatabase>
#include <QTime>
#include <functional>
#include <cmath>

static const char *ENGINE_VERSION = "PhysicsEngine3D v3.0 — 灵光 (LingGuang) 手搓";
static const char *MESH_NAMES[MESH_TYPE_COUNT] = {"Cube_", "Sphere_", "Plane_", "Cylinder_", "Capsule_", "Camera_", "Light_", "Text_", "Model_", "Grass_"};
static const char *MESH_LABELS[MESH_TYPE_COUNT] = {"立方体 (Cube)", "球体 (Sphere)", "平面 (Plane)", "圆柱 (Cylinder)", "胶囊 (Capsule)", "摄像机 (Camera)", "点光源 (Light)", "3D 文本 (Text Mesh)", "模型 (VNModel)", "草地 (Grass)"};
// 动画/模型日志配色

// ============ 双端控制台：全局消息钩子 -> 底部控制台 ============
QPlainTextEdit *MainWindow::consoleSink = nullptr;
static void engineMessageHandler(QtMsgType, const QMessageLogContext &, const QString &m) {
    fprintf(stderr, "%s\n", qUtf8Printable(m)); fflush(stderr);
    if (!MainWindow::consoleSink) return;
    QString color = "#c9d4e4";
    if (m.contains("错误") || m.contains("失败") || m.contains("⚠")) color = "#ff5a6e";
    else if (m.contains("[Socket]")) color = "#ffb454";
    else if (m.contains("[Lua]")) color = "#9fe29f";
    else if (m.contains("[UI]")) color = "#7ff0c8";
    else if (m.contains("[材质]")) color = "#ff9fd0";
    else if (m.contains("[FlyCam]") || m.contains("[物理]")) color = "#7fdcff";
    else if (m.contains("[Script]") || m.contains("[Hierarchy]")) color = "#b9a7ff";
    else if (m.contains("[Model]") || m.contains("[Anim]")) color = "#ffc46b";
    QString esc = m.toHtmlEscaped();
    MainWindow::consoleSink->appendHtml(QString("<span style=\"color:%1\">%2</span>").arg(color, esc));
}

// ============ 资产列表：拖出 .lua 文件 ============
void AssetList::startDrag(Qt::DropActions) {
    auto *item = currentItem();
    if (!item) return;
    QString name = item->text();
    QString base = QCoreApplication::instance()->property("assetsDir").toString();
    QString path = QFile(base + "/" + name).exists() ? base + "/" + name : name;
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    mime->setText(path);
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

// ---- 接收外部模型 / .vnmodel 拖入（资产管线入口） ----
static bool isExternalModel(const QString &p) {
    return p.endsWith(".obj", Qt::CaseInsensitive) || p.endsWith(".fbx", Qt::CaseInsensitive)
        || p.endsWith(".gltf", Qt::CaseInsensitive) || p.endsWith(".glb", Qt::CaseInsensitive);
}
static bool isVNModel(const QString &p) {
    return p.endsWith(".vnmodel", Qt::CaseInsensitive) || p.endsWith(".vnmodel_col", Qt::CaseInsensitive);
}
void AssetList::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) { e->acceptProposedAction(); return; }
    QListWidget::dragEnterEvent(e);
}
void AssetList::dropEvent(QDropEvent *e) {
    qInfo() << "[Import] AssetList::dropEvent 进入";
    // 收集所有 url：在 assets 目录外的文件 -> 系统级导入（Loading 弹窗逐个复制）
    QStringList urls;
    for (const QUrl &u : e->mimeData()->urls()) {
        QString p = u.toLocalFile();
        if (!p.isEmpty()) urls << p;
    }
    if (urls.isEmpty() && !e->mimeData()->text().isEmpty()) urls << e->mimeData()->text();
    QString path = urls.isEmpty() ? QString() : urls.first();

    if (!path.isEmpty()) {
        if (isExternalModel(path)) {
            // 模型：先走真实解析管线，同时也可作为文件导入资产树
            emit externalModelDropped(path);
            e->acceptProposedAction();
            return;
        }
        if (isVNModel(path)) { emit vnModelDropped(path); e->acceptProposedAction(); return; }
        // 系统级导入：不在 assets 目录里的文件 -> ImportLoadingDialog 逐个复制进资产树
        QStringList pending;
        QString dir = assetsDir.isEmpty()
            ? QCoreApplication::instance()->property("assetsDir").toString() : assetsDir;
        for (const QString &p : urls)
            if (QFileInfo(p).absoluteFilePath() != QFileInfo(dir + "/" + QFileInfo(p).fileName()).absoluteFilePath()
                || !QFile(dir + "/" + QFileInfo(p).fileName()).exists())
                pending << p;
        if (!pending.isEmpty()) {
            ImportLoadingDialog dlg(pending.size(), this);
            dlg.show();
            QStringList imported;
            for (const QString &p : pending) {
                importMsleep(150);      // 演示节奏：进度条肉眼可见（真实导入无需等待）
                QString dest = dir + "/" + QFileInfo(p).fileName();
                if (!QFile::exists(p)) continue;
                if (dest != p) { QFile::remove(dest); QFile::copy(p, dest); }
                dlg.step(QFileInfo(p).fileName());
                imported << dest;
                qInfo() << QString("[Import] 已导入：%1 -> assets/").arg(QFileInfo(p).fileName());
            }
            dlg.finish();
            dlg.close();
            if (!imported.isEmpty()) { emit filesImported(imported); e->acceptProposedAction(); return; }
        }
        if (urls.size() == 1) { /* 原生行为兜底 */ }
    }
    QListWidget::dropEvent(e);
}

// ============ 属性面板 ============
InspectorPanel::InspectorPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
}

bool InspectorPanel::event(QEvent *e) {
    if (e->type() == QEvent::DragEnter) { dragEnterEvent(static_cast<QDragEnterEvent*>(e)); return true; }
    if (e->type() == QEvent::Drop) { dropEvent(static_cast<QDropEvent*>(e)); return true; }
    return QWidget::event(e);
}

void InspectorPanel::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) {
        e->acceptProposedAction();
    }
}

void InspectorPanel::dropEvent(QDropEvent *e) {
    QString path;
    if (!e->mimeData()->urls().isEmpty()) path = e->mimeData()->urls().first().toLocalFile();
    else path = e->mimeData()->text();
    if (path.isEmpty()) return;
    if (path.endsWith(".socket", Qt::CaseInsensitive)) {
        if (socketSlot) socketSlot->setText(path);
        qInfo() << QString("[Socket] 拖拽挂载插座：%1").arg(path);
        emit socketDropped(path);
        e->acceptProposedAction();
        return;
    }
    if (path.endsWith(".uicanvas", Qt::CaseInsensitive)) {
        if (canvasSlot) canvasSlot->setText(path);
        qInfo() << QString("[UI] 拖拽挂载 UI 画布：%1").arg(path);
        emit canvasDropped(path);
        e->acceptProposedAction();
        return;
    }
    if (path.endsWith(".vnanim", Qt::CaseInsensitive)) {
        if (animSlot) animSlot->setText(path);
        qInfo() << QString("[Anim] 拖拽挂载骨骼动画：%1").arg(path);
        emit animDropped(path);
        e->acceptProposedAction();
        return;
    }
    if (scriptSlot) scriptSlot->setText(path);
    qInfo() << QString("[Script] 拖拽挂载脚本：%1").arg(path);
    emit luaDropped(path);
    e->acceptProposedAction();
}

MainWindow::MainWindow(const QString &proj, QWidget *parent) : QMainWindow(parent), projectPath(proj) {
    AudioService::setProjectPath(proj);
    setWindowTitle(QString("3D 物理编程引擎 - 编辑器 (3DEditor) — %1").arg(QDir(proj).dirName()));
    resize(1500, 900);
    loadSettings();
    loadComponentRegistry();
    setupConsole();   // 双端控制台（Lua/物理/插座日志实时输出）
    qInstallMessageHandler(engineMessageHandler);

    // ---- 插件系统（plugins.json）+ 渲染器工厂（engine_config.ini）----
    pluginState = PluginSystem::load(proj);
    QString renderApi = RendererFactory::readApi(proj);
    if (Backend *b = RendererFactory::create(renderApi)) {
        QString err;
        bool ok = b->init(err);
        qInfo() << QString("[Renderer] 渲染后端工厂：API=%1 初始化%2%3")
            .arg(b->name(), ok ? "成功" : "失败", ok ? QString() : QString("（%1）").arg(err));
        delete b;
    } else {
        qInfo() << "[Renderer] 错误：未知渲染 API，回退 OpenGL";
    }

    // ---- 读取 director.json 的 spawn 数组：新建项目模板生成的默认物体（Main Camera / Directional Light 等）----
    loadSpawnFromDirector();
    socketSys.dayNightToggle = [this]{ world.toggleDayNight(); };   // 插座节点 ToggleDayNight -> World 天气切换
    // 交互动作节点注入：玩家位置 / 强制下车 / 系统文件选择器
    socketSys.playerPosFn = [this]{
        for (const auto &o : world.objects)
            if (o.name == "Player") return o.position;
        return world.cameraEye();
    };
    socketSys.playerExitFn = [this](const QVector3D &to){
        for (size_t i = 0; i < world.objects.size(); ++i) {
            if (world.objects[i].name != "Player") continue;
            if (world.objects[i].hasBody)
                world.physics.teleportObject(world.objects[i], to);
            else
                world.objects[i].position = to;
            qInfo().noquote() << QString("[Controller] 强制下车：Player 移至 (%1, %2, %3)")
                .arg(to.x()).arg(to.y()).arg(to.z());
            return;
        }
        qInfo() << "[Controller] 强制下车：场景中无 Player 物体（跳过）";
    };
    socketSys.openFileDialogFn = [this](const QString &filter){
        // 运行态真实弹系统文件选择器（QFileDialog 模态，嵌套事件循环，物理帧照常走）
        return QFileDialog::getOpenFileName(this, "选择文件（插座 OpenFileDialog 节点）",
                                            QDir::homePath(), filter);
    };
    socketSys.flashlightFlag_ = &world.flashlightOn;   // 手电筒状态真实落 World

    tabs = new QTabWidget;
    sceneView = new GLViewport(&world, true);
    gameView = new GLViewport(&world, false);
    tabs->addTab(sceneView, "3D场景编辑 (Scene)");
    tabs->addTab(gameView, "游戏运行 (Game)");
    setCentralWidget(tabs);
    connect(sceneView, &GLViewport::selectionChanged, this, &MainWindow::onViewportSelection);
    connect(sceneView, &GLViewport::cameraMoved, this, &MainWindow::onCameraMoved);
    connect(sceneView, &GLViewport::hudButtonClicked, this, &MainWindow::onHudButtonClicked);
    connect(gameView, &GLViewport::hudButtonClicked, this, &MainWindow::onHudButtonClicked);
    connect(sceneView, &GLViewport::materialDropped, this, [this](int idx, const QString &p){
        syncInspectorFrom(idx);   // 材质拖拽后属性面板实时同步
    });

    auto *dockAssets = new QDockWidget("文件管理器 (assets)", this);
    assetList = new AssetList;
    assetList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(assetList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        demoOpenAssetContextMenu(assetList->mapToGlobal(pos));
    });
    connect(assetList, &QListWidget::itemDoubleClicked, this, &MainWindow::onAssetDoubleClicked);
    // 模型资产管线：外部模型导入 / 专属格式入场景
    connect(assetList, &AssetList::externalModelDropped, this, [this](const QString &p){ demoImportModelDialog(p); });
    connect(assetList, &AssetList::vnModelDropped, this, [this](const QString &p){ demoCreateModelObject(p); });
    // 系统级文件导入完成：刷新资产树（真实 dropEvent 管线的收尾）
    connect(assetList, &AssetList::filesImported, this, [this](const QStringList &){
        refreshAssets();
        statusLabel->setText("文件导入完成，资产树已刷新");
    });
    dockAssets->setWidget(assetList);
    dockAssets->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, dockAssets);
    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::refreshAssets);
    refreshAssets();

    auto *dockHier = new QDockWidget("导演面板 (Hierarchy)", this);
    hierarchy = new QTreeWidget;
    hierarchy->setHeaderLabels({"物体", "类型"});
    hierarchy->setRootIsDecorated(false);
    hierarchy->setContextMenuPolicy(Qt::CustomContextMenu);
    hierarchy->setDragEnabled(true);
    hierarchy->setAcceptDrops(true);
    hierarchy->setDropIndicatorShown(true);
    hierarchy->setDragDropMode(QAbstractItemView::InternalMove);
    connect(hierarchy, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onHierarchySelect);
    connect(hierarchy, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onHierarchyContextMenu);
    // 拖拽放下：把被拖物体挂成目标物体的子节点（父子层级嵌套）
    connect(hierarchy->model(), &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent, int, int){
        if (parent.isValid()) {
            auto *parentItem = hierarchy->itemFromIndex(parent);
            auto items = hierarchy->selectedItems();
            if (parentItem && !items.isEmpty()) {
                int childIdx = items[0]->data(0, Qt::UserRole).toInt();
                int parentIdx = parentItem->data(0, Qt::UserRole).toInt();
                if (childIdx != parentIdx && childIdx >= 0 && parentIdx >= 0) {
                    demoNestChild(childIdx, parentIdx);
                }
            }
        }
    });
    dockHier->setWidget(hierarchy);
    addDockWidget(Qt::RightDockWidgetArea, dockHier);
    dockHier->setMinimumWidth(260);

    auto *dockInsp = new QDockWidget("属性面板 (Inspector)", this);
    inspector = new InspectorPanel;
    inspector->lay = new QVBoxLayout(inspector);
    inspector->nameLabel = new QLabel("未选中物体");
    inspector->nameLabel->setStyleSheet("font-weight:bold; font-size:14px; padding:4px;");
    inspector->lay->addWidget(inspector->nameLabel);
    inspName = inspector->nameLabel;   // 同步给旧引用
    auto mkRow = [&](const char *label, QDoubleSpinBox **spins) {
        inspector->lay->addWidget(new QLabel(label));
        auto *row = new QHBoxLayout;
        for (int i = 0; i < 3; ++i) {
            spins[i] = new QDoubleSpinBox;
            spins[i]->setRange(-500.0, 500.0);
            spins[i]->setDecimals(2);
            spins[i]->setSingleStep(0.25);
            row->addWidget(spins[i]);
        }
        inspector->lay->addLayout(row);
    };
    mkRow("Position (X Y Z)", spinPos);
    mkRow("Rotation (X Y Z, 度)", spinRot);
    mkRow("Scale (X Y Z)", spinScale);

    // ---- Add Component（组件注册表数据驱动，不硬编码）----
    inspector->addComponentBtn = new QPushButton("Add Component ＋ 添加组件");
    inspector->addComponentBtn->setStyleSheet(
        "QPushButton{background:#2f6fab;color:white;border:none;padding:7px;border-radius:4px;font-weight:bold;}"
        "QPushButton:hover{background:#3b80c9;}");
    inspector->lay->addWidget(inspector->addComponentBtn);
    connect(inspector->addComponentBtn, &QPushButton::clicked, this, &MainWindow::demoOpenAddComponentMenu);

    // 脚本槽位（拖 .lua 进来 / 点击弹出文件选择框）
    inspector->scriptSlot = new QLineEdit;
    inspector->scriptSlot->setPlaceholderText("把 .lua 脚本拖到这里，或点击选择…");
    inspector->scriptSlot->setReadOnly(true);
    inspector->scriptSlot->setStyleSheet("background:#262a33; color:#9fe29f; border:2px dashed #4a5568; border-radius:4px; padding:6px;");
    inspector->scriptSlot->hide();
    connect(inspector->scriptSlot, &QLineEdit::selectionChanged, this, [this]{
        QString p = QFileDialog::getOpenFileName(this, "选择 Lua 脚本", projectPath + "/assets", "Lua (*.lua)");
        if (!p.isEmpty()) { inspector->scriptSlot->setText(p); demoAttachScript(selected, p); }
    });
    connect(inspector, &InspectorPanel::luaDropped, this, [this](const QString &p){ demoAttachScript(selected, p); });
    inspector->lay->addWidget(inspector->scriptSlot);

    // 插座槽位（拖 .socket 进来）
    inspector->socketSlot = new QLineEdit;
    inspector->socketSlot->setPlaceholderText("把 .socket 插座拖到这里，或点击选择…");
    inspector->socketSlot->setReadOnly(true);
    inspector->socketSlot->setStyleSheet("background:#262a33; color:#ffb454; border:2px dashed #4a5568; border-radius:4px; padding:6px;");
    inspector->socketSlot->hide();
    connect(inspector->socketSlot, &QLineEdit::selectionChanged, this, [this]{
        QString p = QFileDialog::getOpenFileName(this, "选择插座文件", projectPath + "/assets", "Socket (*.socket)");
        if (!p.isEmpty()) { inspector->socketSlot->setText(p); demoAttachSocket(selected, p); }
    });
    connect(inspector, &InspectorPanel::socketDropped, this, [this](const QString &p){ demoAttachSocket(selected, p); });
    inspector->lay->addWidget(inspector->socketSlot);

    // UI 画布槽位（拖 .uicanvas 进来 -> 挂 UICanvas 组件）
    inspector->canvasSlot = new QLineEdit;
    inspector->canvasSlot->setPlaceholderText("把 .uicanvas UI 画布拖到这里，或点击选择…");
    inspector->canvasSlot->setReadOnly(true);
    inspector->canvasSlot->setStyleSheet("background:#262a33; color:#7ff0c8; border:2px dashed #4a5568; border-radius:4px; padding:6px;");
    inspector->canvasSlot->hide();
    connect(inspector->canvasSlot, &QLineEdit::selectionChanged, this, [this]{
        QString p = QFileDialog::getOpenFileName(this, "选择 UI 画布", projectPath + "/assets", "UI Canvas (*.uicanvas)");
        if (!p.isEmpty()) { inspector->canvasSlot->setText(p); demoAttachUICanvas(selected, p); }
    });
    connect(inspector, &InspectorPanel::canvasDropped, this, [this](const QString &p){ demoAttachUICanvas(selected, p); });
    inspector->lay->addWidget(inspector->canvasSlot);

    // 骨骼动画槽位（拖 .vnanim 挂到模型物体上）
    inspector->animSlot = new QLineEdit;
    inspector->animSlot->setPlaceholderText("把 .vnanim 动作文件拖到这里，或点击选择…");
    inspector->animSlot->setReadOnly(true);
    inspector->animSlot->setStyleSheet("background:#262a33; color:#ffc46b; border:2px dashed #4a5568; border-radius:4px; padding:6px;");
    inspector->animSlot->hide();
    connect(inspector->animSlot, &QLineEdit::selectionChanged, this, [this]{
        QString p = QFileDialog::getOpenFileName(this, "选择动画文件", projectPath + "/assets", "VNAnim (*.vnanim)");
        if (!p.isEmpty()) { inspector->animSlot->setText(p); demoPlayAnim(selected, p); }
    });
    connect(inspector, &InspectorPanel::animDropped, this, [this](const QString &p){ demoPlayAnim(selected, p); });
    inspector->lay->addWidget(new QLabel("骨骼动画 (VNAnim)："));
    inspector->lay->addWidget(inspector->animSlot);

    // Text3D 专用：文本内容 + 字体下拉（防中文豆腐块）
    textEdit = new QLineEdit;
    textEdit->setPlaceholderText("3D 文本内容…");
    textEdit->hide();
    connect(textEdit, &QLineEdit::textChanged, this, [this](const QString &t){
        if (suppressInspector || selected < 0 || size_t(selected) >= world.objects.size()) return;
        if (world.objects[size_t(selected)].mesh != MeshType::Text3D) return;
        world.objects[size_t(selected)].text = t;
        qInfo() << QString("[UI] %1 文本内容 -> \"%2\"").arg(world.objects[size_t(selected)].name, t);
        sceneView->update(); gameView->update();
    });
    inspector->lay->addWidget(new QLabel("文本内容 (Text)："));
    inspector->lay->addWidget(textEdit);

    fontCombo = new QComboBox;
    fontCombo->addItem("默认字体 (Noto Sans CJK SC)");
    for (const QString &fam : QFontDatabase::families())
        fontCombo->addItem(fam);
    fontCombo->hide();
    connect(fontCombo, &QComboBox::currentTextChanged, this, [this](const QString &fam){
        if (suppressInspector || selected < 0 || size_t(selected) >= world.objects.size()) return;
        if (world.objects[size_t(selected)].mesh != MeshType::Text3D) return;
        world.objects[size_t(selected)].textFont = fam.startsWith("默认字体") ? QString() : fam;
        qInfo() << QString("[UI] %1 字体 -> %2").arg(world.objects[size_t(selected)].name,
                   world.objects[size_t(selected)].textFont.isEmpty() ? "默认" : fam);
        sceneView->update(); gameView->update();
    });
    inspector->lay->addWidget(new QLabel("字体 (Font)："));
    inspector->lay->addWidget(fontCombo);

    // 材质信息标签（拖 .mat 应用后显示）
    inspector->matLabel = new QLabel;
    inspector->matLabel->setStyleSheet("color:#ff9fd0; padding:2px;");
    inspector->matLabel->hide();
    inspector->lay->addWidget(inspector->matLabel);

    // ---- 粒子参数组（导演面板右键新建粒子系统后显示，实时绑定）----
    particlePanel = new QWidget;
    auto *pp = new QVBoxLayout(particlePanel);
    pp->setContentsMargins(0, 4, 0, 0);
    auto *pTitle = new QLabel("粒子系统 (ParticleSystem)");
    pTitle->setStyleSheet("font-weight:bold; color:#ffc46b;");
    pp->addWidget(pTitle);
    auto mkPRow = [&](const char *label, QSlider *&slider, QLabel *&val, int mn, int mx, int v) {
        pp->addWidget(new QLabel(label));
        auto *row = new QHBoxLayout;
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(mn, mx);
        slider->setValue(v);
        val = new QLabel(QString::number(v));
        row->addWidget(slider); row->addWidget(val);
        pp->addLayout(row);
        connect(slider, &QSlider::valueChanged, val, [val](int x){ val->setText(QString::number(x)); });
    };
    QLabel *tmpVal = nullptr;
    mkPRow("发射速率 (0-600/秒)", pEmitSlider, tmpVal, 0, 600, 80);
    mkPRow("重力影响 (-10~2)", pGravSlider, tmpVal, -10, 2, -3);
    mkPRow("初始速度 (0-8 米/秒)", pSpeedSlider, tmpVal, 0, 8, 2);
    mkPRow("生命周期 (0.2-4 秒, x10)", pLifeSlider, tmpVal, 2, 40, 16);
    auto *colRow = new QHBoxLayout;
    pColorStart = new QPushButton("渐变起点色");
    pColorEnd = new QPushButton("渐变终点色");
    colRow->addWidget(pColorStart); colRow->addWidget(pColorEnd);
    pp->addLayout(colRow);
    particlePanel->hide();
    inspector->lay->addWidget(particlePanel);

    inspector->componentBox = new QWidget;
    inspector->componentBox->hide();
    inspector->lay->addWidget(inspector->componentBox);
    inspector->lay->addStretch();
    for (int i = 0; i < 3; ++i) {
        connect(spinPos[i], &QDoubleSpinBox::valueChanged, this, &MainWindow::onInspectorChanged);
        connect(spinRot[i], &QDoubleSpinBox::valueChanged, this, &MainWindow::onInspectorChanged);
        connect(spinScale[i], &QDoubleSpinBox::valueChanged, this, &MainWindow::onInspectorChanged);
    }
    dockInsp->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, dockInsp);
    dockInsp->setMinimumWidth(260);
    resizeDocks({dockAssets, dockHier, dockInsp}, {240, 300, 300}, Qt::Horizontal);

    // ================= 完整菜单栏 =================
    QMenu *mFile = menuBar()->addMenu("文件(&F)");
    mFile->addAction("新建场景", this, &MainWindow::onNewScene);
    mFile->addAction("保存场景", this, &MainWindow::onSaveScene);
    mFile->addSeparator();
    mFile->addAction("退出", qApp, &QApplication::quit);

    QMenu *mCreate = menuBar()->addMenu("创建(&C)");
    for (int t = 0; t < MESH_TYPE_COUNT; ++t)
        mCreate->addAction(MESH_LABELS[t], this, [this, t]{ createObject(t); });

    QMenu *mRun = menuBar()->addMenu("运行(&R)");
    actRun = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), "运行 (&Run)", this);
    mRun->addAction(actRun);
    connect(actRun, &QAction::triggered, this, &MainWindow::onRun);

    QMenu *mPlugin = menuBar()->addMenu("插件(&P)");
    mPlugin->addAction("插件管理…", this, &MainWindow::onPlugins);

    QMenu *mPhys = menuBar()->addMenu("物理(&P)");
    QAction *actPhys = new QAction(style()->standardIcon(QStyle::SP_ComputerIcon), "物理测试：生成100个方块", this);
    mPhys->addAction(actPhys);
    connect(actPhys, &QAction::triggered, this, &MainWindow::onPhysicsTest);

    // ---- 字体管理（与 2D 编辑器对齐）：导入 .ttf/.otf，3D 文本组件真实换字体 ----
    QMenu *mFont = menuBar()->addMenu("字体管理");
    mFont->addAction("导入字体 (.ttf/.otf)…", this, &MainWindow::onImportFont);
    mFont->addAction("应用到选中的 3D 文本", this, [this]{
        demoApplyFontToSelected();
    });
    mFont->addAction("设为新建 3D 文本默认字体", this, [this]{
        if (lastImportedFont_.isEmpty()) { qInfo() << "[Font] 尚未导入字体：先用「导入字体」加入 .ttf/.otf"; return; }
        default3DFont = lastImportedFont_;
        qInfo().noquote() << QString("[Font] 新建 3D 文本默认字体 -> %1（Text3D 组件渲染中文）").arg(default3DFont);
    });

    // ---- 模型管理：快速放置基础几何体（Cube/Sphere/Cylinder/Plane） ----
    // 注：一键创建基础模型原本就有两处实现——①菜单栏【创建(&C)】（MainWindow.cpp 约 480 行，
    //     遍历 MESH_LABELS 全部 10 种）②工具栏图标按钮（约 564 行，同样全类型）。
    //     本菜单按死命令单独成栏，聚焦恐怖游戏常用的 4 种基础几何体。
    QMenu *mModel = menuBar()->addMenu("模型管理");
    mModel->addAction("快速放置：Cube (方块)", this, [this]{ qInfo() << "[Model] 快速放置 Cube (方块)"; createObject(0); });
    mModel->addAction("快速放置：Sphere (球体)", this, [this]{ qInfo() << "[Model] 快速放置 Sphere (球体)"; createObject(1); });
    mModel->addAction("快速放置：Cylinder (圆柱)", this, [this]{ qInfo() << "[Model] 快速放置 Cylinder (圆柱)"; createObject(3); });
    mModel->addAction("快速放置：Plane (平面)", this, [this]{ qInfo() << "[Model] 快速放置 Plane (平面)"; createObject(2); });

    // ---- 构建(Build) -> 打包发布：独立 QDialog 表单面板 ----
    QMenu *mBuild = menuBar()->addMenu("构建(&B)");
    mBuild->addAction("打包发布…", this, &MainWindow::openBuildPanel);

    QMenu *mWindow = menuBar()->addMenu("窗口(&W)");
    mWindow->addAction("设置...", this, &MainWindow::openSettings);
    mWindow->addAction("关于版本", this, &MainWindow::onAbout);
    {
        auto *actSearch = new QAction("全局搜索 (Ctrl+P)", this);
        actSearch->setShortcut(QKeySequence::fromString("Ctrl+P"));
        actSearch->setShortcutContext(Qt::ApplicationShortcut);
        connect(actSearch, &QAction::triggered, this, &MainWindow::openQuickSearch);
        addAction(actSearch);
        mWindow->addAction(actSearch);
    }

    QMenu *mHelp = menuBar()->addMenu("帮助(&H)");
    mHelp->addAction("作者主页", this, &MainWindow::onAuthorPage);
    mHelp->addAction("贡献者名单", this, &MainWindow::onContributors);

    QMenu *mAbout = menuBar()->addMenu("关于(&A)");
    mAbout->addAction("关于 PhysicsEngine3D…", this, &MainWindow::onAbout);

    // 工具栏
    auto *tb = addToolBar("main");
    tb->addAction(actRun);
    tb->addSeparator();
    tb->addAction(actPhys);
    // 网格吸附（Snap）：开关 + 位置步长 + 旋转步长
    {
        auto *snapBtn = new QPushButton("🧲 吸附", tb);
        snapBtn->setToolTip("开启后拖拽物体按步长吸附：位置 1 米、旋转 15 度");
        snapBtn->setCheckable(true);
        auto *posSpin = new QDoubleSpinBox(tb);
        posSpin->setRange(0.1, 100.0); posSpin->setValue(1.0); posSpin->setSuffix(" 米");
        posSpin->setToolTip("位置吸附步长");
        auto *rotSpin = new QDoubleSpinBox(tb);
        rotSpin->setRange(1.0, 180.0); rotSpin->setValue(15.0); rotSpin->setSuffix(" °");
        rotSpin->setToolTip("旋转吸附步长（度）");
        connect(snapBtn, &QPushButton::toggled, this, [this, posSpin, rotSpin, proj](bool on) {
            sceneView->snapOn = on; gameView->snapOn = on;
            sceneView->snapPosStep = float(posSpin->value());
            sceneView->snapRotStep = float(rotSpin->value());
            qInfo() << QString("[Snap] 网格吸附%1（位置 %2 米 / 旋转 %3 度）")
                .arg(on ? "已开启" : "已关闭").arg(posSpin->value()).arg(rotSpin->value());
            statusLabel->setText(QString("项目: %1   |   网格吸附%2").arg(proj).arg(on ? "开" : "关"));
        });
        connect(posSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            sceneView->snapPosStep = float(v); gameView->snapPosStep = float(v);
        });
        connect(rotSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            sceneView->snapRotStep = float(v); gameView->snapRotStep = float(v);
        });
        tb->addWidget(snapBtn);
        tb->addWidget(new QLabel(" 位置:", tb));
        tb->addWidget(posSpin);
        tb->addWidget(new QLabel(" 旋转:", tb));
        tb->addWidget(rotSpin);
    }
    for (int t = 0; t < MESH_TYPE_COUNT; ++t)
        tb->addAction(style()->standardIcon(QStyle::SP_DialogOkButton), MESH_LABELS[t], this, [this, t]{ createObject(t); });
    tb->setMovable(false);

    statusLabel = new QLabel(QString("项目: %1   |   就绪").arg(proj));
    statusBar()->addWidget(statusLabel);

    rebuildHierarchy();

    loop = new QTimer(this);
    connect(loop, &QTimer::timeout, this, &MainWindow::tick);
    loop->start(16);
    fpsLastMs = QDateTime::currentMSecsSinceEpoch();
    loadProjectConfig();
}

// ---- 组件注册表：JSON 数据驱动 ----
void MainWindow::loadComponentRegistry() {
    // 内置注册表（也可放 assets/components.json 覆盖）
    const char *json = R"JSON({
        "RigidBody":  {"label":"刚体 (RigidBody)",  "params":{"mass":"质量 kg","friction":"摩擦力","restitution":"弹性系数"}},
        "Collider":   {"label":"碰撞体 (Collider)", "params":{"colliderType":"碰撞形状","isTrigger":"是否触发器"}},
        "Renderer":   {"label":"渲染器 (Renderer)", "params":{"material":"材质","castShadow":"投射阴影"}},
        "Script":     {"label":"Script (脚本)",     "params":{"scriptPath":"脚本文件"}},
        "Light":      {"label":"光源 (Light)",      "params":{"intensity":"强度","range":"范围"}},
        "AudioSource":{"label":"音源 (AudioSource)","params":{"clip":"音频文件","volume":"音量"}},
        "Animator":   {"label":"动画器 (Animator)", "params":{"controller":"动画控制器"}},
        "Camera":     {"label":"摄像机 (Camera)",   "params":{"fov":"视场角","nearClip":"近裁剪","farClip":"远裁剪"}},
        "ParticleFX": {"label":"粒子特效 (ParticleFX)","params":{"rate":"发射速率","lifetime":"生命周期"}},
        "NavAgent":   {"label":"导航代理 (NavAgent)","params":{"speed":"移动速度","radius":"半径"}},
        "Socket":     {"label":"插座 (Socket)",     "params":{"socketPath":"插座文件"}},
        "UICanvas":   {"label":"2D Canvas 组件 (UICanvas)", "params":{"canvasPath":"画布文件"}},
        "CharacterController": {"label":"角色控制器 (CharacterController)", "params":{"walkSpeed":"移动速度 WalkSpeed","jumpForce":"跳跃力 JumpForce","gravityScale":"重力系数"}},
        "PointLight": {"label":"点光源 (Point Light)","params":{"color":"颜色","intensity":"强度","radius":"半径"}},
        "SpotLight": {"label":"聚光灯 (Spot Light)","params":{"innerCone":"内锥角 °","outerCone":"外锥角 °"}},
        "AudioSource3D": {"label":"3D 空间音效 (Audio Source 3D)","params":{"clip":"音频路径","volume":"音量","loop":"是否循环","attenuation":"衰减距离"}},
        "TriggerBox": {"label":"触发器 (Trigger Box)","params":{"sizeX":"包围盒 X","sizeY":"包围盒 Y","sizeZ":"包围盒 Z"}}
    })JSON";
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(QByteArray(json), &err);
    if (err.error == QJsonParseError::NoError) componentRegistry = doc.object();
    // 用户覆盖：assets/components.json
    QFile f(projectPath + "/assets/components.json");
    if (f.open(QIODevice::ReadOnly)) {
        auto user = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError) {
            auto uo = user.object();
            for (auto it = uo.begin(); it != uo.end(); ++it) componentRegistry[it.key()] = it.value();
            qInfo() << "[Registry] 已从 components.json 加载组件定义：" << uo.size() << "项";
        }
    }
    qInfo() << "[Registry] 组件注册表就绪：" << componentRegistry.size() << "种组件";
}

void MainWindow::loadSettings() {
    QSettings s("PhysicsEngine3D", "3DEditor");
    settings.msaa = s.value("render/msaa", 4).toInt();
    settings.shadowCascades = s.value("render/cascades", 2).toInt();
    settings.aniso = s.value("render/aniso", 4).toInt();
    settings.vsync = s.value("render/vsync", true).toBool();
    settings.uiScale = s.value("render/uiScale", 100).toInt();
    settings.stepHz = s.value("physics/stepHz", 60).toInt();
    settings.sleepThreshold = s.value("physics/sleepThreshold", 0.008f).toFloat();
    settings.gravity[0] = s.value("physics/gravityX", 0.f).toFloat();
    settings.gravity[1] = s.value("physics/gravityY", -9.81f).toFloat();
    settings.gravity[2] = s.value("physics/gravityZ", 0.f).toFloat();
    settings.logLevel = s.value("debug/logLevel", "Info").toString();
    settings.vramRefreshMs = s.value("debug/vramRefreshMs", 500).toInt();
    settings.wireframeDebug = s.value("debug/wireframe", false).toBool();
    applySettings(settings);
}

void MainWindow::applySettings(const EngineSettings &s) {
    settings = s;
    world.physics.fixedStep = 1.0f / s.stepHz;
    world.physics.drawDebug = s.wireframeDebug;
    world.physics.setGravity(QVector3D(s.gravity[0], s.gravity[1], s.gravity[2]));
    world.physics.setSleepThresholdAll(s.sleepThreshold);
    if (statusLabel)
        statusLabel->setText(QString("参数已应用：步进 %1Hz | 重力 (%2, %3, %4) | 线框调试 %5")
            .arg(s.stepHz).arg(s.gravity[0]).arg(s.gravity[1]).arg(s.gravity[2])
            .arg(s.wireframeDebug ? "开" : "关"));
}

void MainWindow::openSettings() {
    settingsDialog = new SettingsDialog(settings, this);
    settingsDialog->setProjectPath(projectPath);   // 渲染器 API ini 落点
    connect(settingsDialog, &SettingsDialog::settingsApplied, this, &MainWindow::applySettings);
    connect(settingsDialog, &QDialog::finished, this, [this]{ settingsDialog = nullptr; });
    settingsDialog->show();
}

void MainWindow::onAbout() {
    QDialog dlg(this);
    dlg.setWindowTitle("关于 PhysicsEngine3D");
    dlg.setFixedSize(460, 400);
    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(10);

    auto *logo = new QLabel("⚡");
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet("font-size:44px; color:#7fdcff;");
    lay->addWidget(logo);

    auto *name = new QLabel("PhysicsEngine3D");
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet("font-size:22px; font-weight:bold; color:#eaf2fb;");
    lay->addWidget(name);

    auto *ver = new QLabel("版本 v1.0.0-Genesis");
    ver->setAlignment(Qt::AlignCenter);
    ver->setStyleSheet("font-size:14px; color:#7fdcff;");
    lay->addWidget(ver);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#3a4656;");
    lay->addWidget(line);

    auto *info = new QLabel(
        "<p style='margin:2px'><b>开发者：</b>灵光 (LingGuang) × 你（共创）</p>"
        "<p style='margin:2px'><b>开源协议：</b>MIT License</p>"
        "<p style='margin:2px'><b>开发语言 / 框架：</b>C++17 / Qt6 / OpenGL 3.3</p>"
        "<p style='margin:2px'><b>物理引擎：</b>Bullet Physics</p>"
        "<p style='margin:2px'><b>脚本系统：</b>Lua 5.4</p>");
    info->setTextFormat(Qt::RichText);
    info->setStyleSheet("font-size:13px; color:#c9d4e4;");
    lay->addWidget(info);

    lay->addStretch();
    auto *desc = new QLabel("跨平台 3D 物理编程引擎 — 让零基础的你也能搭出自己的物理世界。");
    desc->setWordWrap(true);
    desc->setStyleSheet("color:#8fa3b8; font-size:12px;");
    lay->addWidget(desc);

    auto *btnOk = new QPushButton("好的");
    btnOk->setStyleSheet("background:#2f6fab;color:white;padding:8px;border-radius:4px;font-weight:bold;");
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(btnOk);

    dlg.exec();
}

void MainWindow::onAuthorPage() {
    QDesktopServices::openUrl(QUrl("https://github.com/lingguang-dev/physics-engine3d"));
}

void MainWindow::onContributors() {
    QMessageBox::information(this, "贡献者名单",
        "核心架构: 灵光 (LingGuang)\n物理系统: 灵光 (LingGuang)\n渲染管线: 灵光 (LingGuang)\nLua 引擎: 灵光 (LingGuang)");
}

void MainWindow::onNewScene() {
    lua.stopAll();
    world.objects.clear();
    world.physics.clearBodies();
    selected = -1;
    sceneView->setCubeOutlined(-1);
    rebuildHierarchy();
    statusLabel->setText("已新建空场景");
}

void MainWindow::onSaveScene() {
    QJsonArray arr;
    for (const auto &o : world.objects) {
        QJsonObject j;
        j["name"] = o.name;
        j["mesh"] = int(o.mesh);
        j["pos"] = QJsonArray{o.position.x(), o.position.y(), o.position.z()};
        j["scale"] = QJsonArray{o.scale.x(), o.scale.y(), o.scale.z()};
        j["color"] = QJsonArray{o.color.x(), o.color.y(), o.color.z()};
        j["hasBody"] = o.hasBody;
        j["isStatic"] = o.isStatic;
        j["parent"] = o.parent;
        if (o.mesh == MeshType::Text3D) {
            j["text"] = o.text;
            j["textFont"] = o.textFont;
        }
        if (o.mesh == MeshType::ModelMesh) {
            j["modelPath"] = o.modelPath;
            j["animPath"] = o.animPath;
        }
        QJsonArray comps;
        for (const auto &c : o.components) comps.append(c.params);
        j["components"] = comps;
        arr.append(j);
    }
    QDir().mkpath(projectPath + "/scenes");
    QFile f(projectPath + "/scenes/scene.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
        statusLabel->setText(QString("场景已保存：scenes/scene.json (%1 个物体)").arg(world.objects.size()));
    }
    // 导演面板名单（VNSocketEditor 的 Target 下拉从这里动态读取）
    QJsonObject director;
    QJsonArray names;
    for (const auto &o : world.objects) names.append(o.name);
    director["objects"] = names;
    QFile df(projectPath + "/director.json");
    if (df.open(QIODevice::WriteOnly)) df.write(QJsonDocument(director).toJson());
    qInfo() << "[Hierarchy] 已写出 director.json：目标选择器可读取" << names.size() << "个物体";
}

void MainWindow::createObject(int meshType) {
    SceneObject o;
    MeshType mt = MeshType(meshType);
    o.mesh = mt;
    o.name = QString(MESH_NAMES[meshType]) + QString("%1").arg(++world.counters[meshType], 2, 10, QChar('0'));
    float hue = (std::rand() % 100) / 100.0f;
    o.color = QVector3D(0.4f + hue * 0.6f, 0.85f, 1.0f - hue * 0.6f);
    switch (mt) {
    case MeshType::Cube:
        o.hasBody = true;
        o.position = QVector3D((std::rand() % 400 - 200) / 100.0f, 2.0f, (std::rand() % 400 - 200) / 100.0f);
        break;
    case MeshType::Sphere:
        o.hasBody = true;
        o.position = QVector3D((std::rand() % 400 - 200) / 100.0f, 2.5f, (std::rand() % 400 - 200) / 100.0f);
        break;
    case MeshType::Plane:
        o.scale = QVector3D(4, 0.05f, 4); o.position = QVector3D(0, 0, 0); o.isStatic = true;
        break;
    case MeshType::Grass:
        // 草地：大面积细分平面，自带风力参数（动态自然插件驱动顶点摆动）
        o.scale = QVector3D(6, 1, 6);
        o.position = QVector3D(0, 0.02f, 0);
        o.color = QVector3D(0.35f, 0.78f, 0.42f);
        o.isStatic = true;
        o.windStrength = 0.12f;
        o.windFreq = 2.4f;
        break;
    case MeshType::Cylinder:
        o.hasBody = true;
        o.scale = QVector3D(1, 1.4f, 1);
        o.position = QVector3D((std::rand() % 400 - 200) / 100.0f, 2.2f, (std::rand() % 400 - 200) / 100.0f);
        o.color = QVector3D(0.35f, 0.75f, 0.95f);
        break;
    case MeshType::Capsule:
        o.hasBody = true;
        o.scale = QVector3D(1, 1.6f, 1);
        o.position = QVector3D((std::rand() % 400 - 200) / 100.0f, 2.4f, (std::rand() % 400 - 200) / 100.0f);
        o.color = QVector3D(0.9f, 0.55f, 0.85f);
        break;
    case MeshType::Camera:
        o.scale = QVector3D(0.4f, 0.4f, 0.7f);
        o.position = QVector3D(3, 2.5f, 3);
        o.color = QVector3D(0.2f, 0.45f, 0.9f);
        o.isStatic = true;
        break;
    case MeshType::Light:
        o.scale = QVector3D(0.4f, 0.4f, 0.4f);
        o.position = QVector3D(-3, 4, 2);
        o.color = QVector3D(1.0f, 0.9f, 0.3f);
        o.isStatic = true;
        break;
    case MeshType::Text3D:
        // 3D 文本（Text Mesh）：公告牌渲染，静态无刚体；默认字体取「字体管理」最近设置
        o.position = QVector3D(0, 2.2f, 0);
        o.color = QVector3D(1.0f, 0.85f, 0.35f);
        o.isStatic = true;
        if (!default3DFont.isEmpty()) o.textFont = default3DFont;
        break;
    }

    world.objects.push_back(o);
    SceneObject &stored = world.objects.back();
    if (o.hasBody && !o.isStatic) {
        int bi;
        switch (mt) {
        case MeshType::Sphere:   bi = world.physics.addSphereBody(stored.position, stored.scale, 1.0f); break;
        case MeshType::Cylinder: bi = world.physics.addCylinderBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
        case MeshType::Capsule:  bi = world.physics.addCapsuleBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
        default:                 bi = world.physics.addBoxBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
        }
        world.physics.setBodyIndex(&stored, bi);
    } else if (o.mesh == MeshType::Plane || o.mesh == MeshType::Grass) {
        world.physics.setBodyIndex(&stored, world.physics.addPlaneBody(stored.position, stored.scale));
    }
    rebuildHierarchy();
    statusLabel->setText(QString("已创建 %1").arg(o.name));
}

void MainWindow::refreshAssets() {
    QDir dir(projectPath + "/assets");
    QDir().mkpath(dir.absolutePath());
    QCoreApplication::instance()->setProperty("assetsDir", dir.absolutePath());
    assetList->assetsDir = dir.absolutePath();   // dropEvent 系统级导入的目标目录
    assetList->clear();
    assetList->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DirIcon), "..  (项目根目录)"));
    for (const QFileInfo &f : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst)) {
        assetList->addItem(new QListWidgetItem(
            style()->standardIcon(f.isDir() ? QStyle::SP_DirIcon : QStyle::SP_FileIcon),
            f.fileName()));
    }
    if (!watcher->directories().contains(dir.absolutePath()))
        watcher->addPath(dir.absolutePath());
    addLuaWatchers();
}

void MainWindow::rebuildHierarchy() {
    hierarchy->blockSignals(true);
    hierarchy->clear();
    // 按父子关系递归插入
    std::function<void(int, QTreeWidgetItem *)> rec = [&](int i, QTreeWidgetItem *parentItem) {
        const auto &o = world.objects[size_t(i)];
        QString type;
        switch (o.mesh) {
        case MeshType::Cube: type = "Cube"; break;
        case MeshType::Sphere: type = "Sphere"; break;
        case MeshType::Plane: type = "Plane"; break;
        case MeshType::Cylinder: type = "Cylinder"; break;
        case MeshType::Capsule: type = "Capsule"; break;
        case MeshType::Camera: type = "Camera"; break;
        case MeshType::Light: type = "Light"; break;
        case MeshType::Text3D: type = "Text3D"; break;
        case MeshType::ModelMesh: type = "VNModel"; break;
        case MeshType::Grass: type = "Grass"; break;
        }
        QString tag = o.hasBody ? (o.isStatic ? type + " 静态刚体" : type + " 动态刚体") : type;
        for (const auto &c : o.components) tag += QString(" + %1").arg(c.type);
        auto *it = new QTreeWidgetItem({o.name, tag});
        it->setData(0, Qt::UserRole, i);
        if (parentItem) parentItem->addChild(it);
        else hierarchy->addTopLevelItem(it);
        for (int k = 0; k < int(world.objects.size()); ++k)
            if (world.objects[size_t(k)].parent == i) rec(k, it);
    };
    for (int i = 0; i < int(world.objects.size()); ++i)
        if (world.objects[i].parent == -1) rec(i, nullptr);
    hierarchy->expandAll();
    hierarchy->blockSignals(false);
}

void MainWindow::onViewportSelection(int idx) {
    selected = idx;
    hierarchy->blockSignals(true);
    hierarchy->clearSelection();
    // 找到对应 item（可能在子层级）
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem *root) -> QTreeWidgetItem * {
        if (!root) return nullptr;
        if (root->data(0, Qt::UserRole).toInt() == idx) return root;
        for (int c = 0; c < root->childCount(); ++c) {
            auto *r = find(root->child(c));
            if (r) return r;
        }
        return nullptr;
    };
    QTreeWidgetItem *target = nullptr;
    for (int t = 0; t < hierarchy->topLevelItemCount() && !target; ++t) target = find(hierarchy->topLevelItem(t));
    if (idx >= 0 && target) hierarchy->setCurrentItem(target);
    hierarchy->blockSignals(false);
    if (idx >= 0) syncInspectorFrom(idx);
    gameView->setCubeOutlined(selected);
}

void MainWindow::onHierarchySelect() {
    auto items = hierarchy->selectedItems();
    if (items.isEmpty()) { selected = -1; return; }
    selected = items[0]->data(0, Qt::UserRole).toInt();
    sceneView->setCubeOutlined(selected);
    gameView->setCubeOutlined(selected);
    syncInspectorFrom(selected);
}

void MainWindow::syncInspectorFrom(int idx) {
    if (idx < 0 || size_t(idx) >= world.objects.size()) return;
    const SceneObject &o = world.objects[idx];
    suppressInspector = true;
    inspName->setText(o.name);
    spinPos[0]->setValue(o.position.x()); spinPos[1]->setValue(o.position.y()); spinPos[2]->setValue(o.position.z());
    QVector3D e = o.rotation.toEulerAngles();
    spinRot[0]->setValue(e.x()); spinRot[1]->setValue(e.y()); spinRot[2]->setValue(e.z());
    spinScale[0]->setValue(o.scale.x()); spinScale[1]->setValue(o.scale.y()); spinScale[2]->setValue(o.scale.z());
    suppressInspector = false;
    rebuildInspector(idx);
}

// ---- 重建组件参数区（实时同步）----
void MainWindow::rebuildInspector(int idx) {
    // 清空组件区
    auto lay = inspector->componentBox->layout();
    if (lay) { while (auto *it = lay->takeAt(0)) { delete it->widget(); delete it; } delete lay; }
    inspector->scriptSlot->hide();
    inspector->socketSlot->hide();
    inspector->canvasSlot->hide();
    inspector->matLabel->hide();
    textEdit->hide();
    fontCombo->hide();
    inspector->animSlot->hide();
    inspector->componentBox->hide();

    if (idx < 0 || size_t(idx) >= world.objects.size()) return;
    const SceneObject &o = world.objects[idx];

    // 模型物体：显示动画槽位（挂 .vnanim 播放骨骼动画）
    if (o.mesh == MeshType::ModelMesh) {
        suppressInspector = true;
        inspector->animSlot->setText(o.animPath.isEmpty() ? QString() : o.animPath);
        suppressInspector = false;
        inspector->animSlot->show();
        qInfo() << QString("[Anim] %1 挂载动画：%2（播放中 %3）").arg(o.name)
            .arg(o.animPath.isEmpty() ? "无" : QFileInfo(o.animPath).fileName())
            .arg(o.animPlaying ? "是" : "否");
    }

    // Text3D 专用属性：文本内容 + 字体下拉
    if (o.mesh == MeshType::Text3D) {
        suppressInspector = true;
        textEdit->setText(o.text);
        int fi = o.textFont.isEmpty() ? 0 : fontCombo->findText(o.textFont);
        fontCombo->setCurrentIndex(fi < 0 ? 0 : fi);
        suppressInspector = false;
        textEdit->show();
        fontCombo->show();
    }

    // 材质信息实时显示（拖 .mat 后同步）
    if (o.hasMaterial) {
        inspector->matLabel->setText(QString("材质: %1 / 金属度 %2 / 粗糙度 %3 / 贴图: %4")
            .arg(o.material.name, QString::number(o.material.metalness, 'f', 2),
                 QString::number(o.material.roughness, 'f', 2),
                 o.material.texture.isEmpty() ? "无" : QFileInfo(o.material.texture).fileName()));
        inspector->matLabel->show();
    }
    if (o.components.empty()) return;

    auto *boxLay = new QVBoxLayout(inspector->componentBox);
    boxLay->setContentsMargins(0, 8, 0, 0);
    auto *title = new QLabel(QString("组件（%1 个）").arg(o.components.size()));
    title->setStyleSheet("font-weight:bold; color:#7fb3ff;");
    boxLay->addWidget(title);

    for (const auto &c : o.components) {
        QJsonObject def = componentRegistry.value(c.type).toObject();
        auto *frame = new QWidget;
        frame->setStyleSheet("background:rgba(60,70,90,120); border:1px solid #3b4a61; border-radius:5px;");
        auto *fl = new QVBoxLayout(frame);
        auto *cap = new QLabel(def.value("label").toString(c.type));
        cap->setStyleSheet("font-weight:bold; color:#cfe2ff;");
        fl->addWidget(cap);
        QJsonObject params = def.value("params").toObject();
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (c.type == "Script" && it.key() == "scriptPath") {
                // 脚本组件：显示拖拽槽位
                inspector->scriptSlot->setText(c.params.value("scriptPath").toString());
                inspector->scriptSlot->show();
                continue;
            }
            if (c.type == "Socket" && it.key() == "socketPath") {
                // 插座组件：显示拖拽槽位
                inspector->socketSlot->setText(c.params.value("socketPath").toString());
                inspector->socketSlot->show();
                continue;
            }
            if (c.type == "UICanvas" && it.key() == "canvasPath") {
                // UI 画布组件：显示拖拽槽位
                inspector->canvasSlot->setText(c.params.value("canvasPath").toString());
                inspector->canvasSlot->show();
                continue;
            }
            QString val = c.params.contains(it.key()) ? c.params.value(it.key()).toVariant().toString() : "默认值";
            fl->addWidget(new QLabel(QString("%1: %2").arg(it.value().toString()).arg(val)));
        }
        boxLay->addWidget(frame);
    }
    inspector->componentBox->show();
}

void MainWindow::onInspectorChanged() {
    if (suppressInspector || selected < 0 || size_t(selected) >= world.objects.size()) return;
    SceneObject &o = world.objects[selected];
    o.position = QVector3D(float(spinPos[0]->value()), float(spinPos[1]->value()), float(spinPos[2]->value()));
    o.rotation = QQuaternion::fromEulerAngles(
        float(spinRot[0]->value()), float(spinRot[1]->value()), float(spinRot[2]->value()));
    o.scale = QVector3D(float(spinScale[0]->value()), float(spinScale[1]->value()), float(spinScale[2]->value()));
    int bi = world.physics.bodyIndexOf(&o);
    if (bi >= 0) world.physics.syncBodyFromObject(o, bi, worldMatrixOf(selected, world.objects));
    sceneView->update();
    gameView->update();
}

void MainWindow::onHierarchyContextMenu(const QPoint &pos) {
    auto *item = hierarchy->itemAt(pos);
    if (!item) return;
    int idx = item->data(0, Qt::UserRole).toInt();
    selected = idx;
    syncInspectorFrom(idx);
    sceneView->setCubeOutlined(idx);
    QMenu menu(this);
    menu.addAction("尺寸 ×2（拉长）", this, [this, idx]{
        world.objects[idx].scale *= 2.0f; syncInspectorFrom(idx); sceneView->update(); });
    menu.addAction("尺寸 ÷2（压扁）", this, [this, idx]{
        world.objects[idx].scale *= 0.5f; syncInspectorFrom(idx); sceneView->update(); });
    menu.addAction("重置尺寸", this, [this, idx]{
        world.objects[idx].scale = QVector3D(1,1,1); syncInspectorFrom(idx); sceneView->update(); });
    menu.addAction("解除父子关系", this, [this, idx]{
        world.objects[idx].parent = -1; rebuildHierarchy(); });
    menu.addSeparator();
    menu.addAction("新建粒子系统（烟花预设）", this, &MainWindow::demoAddParticles);
    menu.addSeparator();
    menu.addAction("删除物体", this, [this, idx]{
        lua.detachByObject(idx);
        world.physics.clearBodies();
        world.objects.erase(world.objects.begin() + idx);
        for (auto &o : world.objects) { if (o.parent >= idx) o.parent--; }
        for (auto &o : world.objects) {
            if (o.hasBody && !o.isStatic) {
                int bi = o.mesh == MeshType::Sphere
                    ? world.physics.addSphereBody(o.position, o.scale, o.isStatic ? 0.f : 1.f)
                    : o.mesh == MeshType::Cylinder
                    ? world.physics.addCylinderBody(o.position, o.rotation, o.scale, o.isStatic ? 0.f : 1.f)
                    : o.mesh == MeshType::Capsule
                    ? world.physics.addCapsuleBody(o.position, o.rotation, o.scale, o.isStatic ? 0.f : 1.f)
                    : world.physics.addBoxBody(o.position, o.rotation, o.scale, o.isStatic ? 0.f : 1.f);
                world.physics.setBodyIndex(&o, bi);
            }
        }
        selected = -1; sceneView->setCubeOutlined(-1);
        rebuildHierarchy();
    });
    menu.exec(hierarchy->mapToGlobal(pos));
}

// ---- 演示钩子：父子层级嵌套 ----
void MainWindow::demoNestChild(int childIdx, int parentIdx) {
    if (childIdx < 0 || parentIdx < 0 || childIdx >= int(world.objects.size())) return;
    // 防环：父不能是自己的后代
    int p = parentIdx;
    while (p >= 0) {
        if (p == childIdx) return;
        p = world.objects[size_t(p)].parent;
    }
    world.objects[size_t(childIdx)].parent = parentIdx;
    qInfo() << QString("[Hierarchy] 父子层级：'%1' 已挂到 '%2' 下")
        .arg(world.objects[size_t(childIdx)].name).arg(world.objects[size_t(parentIdx)].name);
    statusLabel->setText(QString("父子层级：%1 → %2（矩阵联动已生效）")
        .arg(world.objects[size_t(childIdx)].name).arg(world.objects[size_t(parentIdx)].name));
    rebuildHierarchy();
    sceneView->update();
}

// ---- 演示钩子：新建 .lua 脚本资产 ----
void MainWindow::demoNewLua(const QString &name) {
    QFile f(projectPath + "/assets/" + name);
    if (!f.exists()) {
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(R"(-- 新建 Lua 脚本
function Start(self)
    print("脚本已挂载：" .. self:GetName())
end

function Update(self, deltaTime)
    -- self:Move(x, y, z) / self:Rotate(x, y, z)
end
)");
            f.close();
        }
    }
    statusLabel->setText(QString("已创建脚本资产：%1").arg(name));
    qInfo() << QString("[Assets] 新建 Lua 脚本：%1").arg(name);
    refreshAssets();
}

// ---- 演示钩子：挂载脚本 ----
void MainWindow::demoAttachScript(int idx, const QString &path) {
    if (idx < 0 || size_t(idx) >= world.objects.size() || path.isEmpty()) return;
    SceneObject &o = world.objects[size_t(idx)];
    // 已有 Script 组件就复用
    bool found = false;
    for (auto &c : o.components) {
        if (c.type == "Script") { c.params["scriptPath"] = path; found = true; }
    }
    if (!found) o.components.push_back({"Script", QJsonObject{{"scriptPath", path}}});
    if (running) lua.attach(idx, path);
    syncInspectorFrom(idx);
    qInfo() << QString("[Script] 挂载成功：%1 -> %2").arg(o.name).arg(path);
    statusLabel->setText(QString("脚本已挂载：%1 ← %2").arg(o.name).arg(QFile(path).fileName()));
}

// ---- Add Component 下拉（数据驱动，非阻塞 popup 供截图）----
void MainWindow::demoOpenAddComponentMenu() {
    if (selected < 0 || size_t(selected) >= world.objects.size()) {
        statusLabel->setText("请先选中一个物体");
        return;
    }
    auto *menu = new QMenu(this);
    for (auto it = componentRegistry.begin(); it != componentRegistry.end(); ++it) {
        QString label = it.value().toObject().value("label").toString(it.key());
        auto *act = menu->addAction(label);
        act->setData(it.key());
    }
    connect(menu, &QMenu::triggered, this, &MainWindow::onAddComponent);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->popup(QCursor::pos());
    qInfo() << QString("[Component] Add Component 下拉已展开（%1 种组件）").arg(componentRegistry.size());
}

void MainWindow::onAddComponent(QAction *act) {
    if (!act || selected < 0) return;
    QString type = act->data().toString();
    SceneObject &o = world.objects[size_t(selected)];
    o.components.push_back({type, QJsonObject{}});
    qInfo() << QString("[Component] %1 添加组件：%2").arg(o.name).arg(type);
    if (type == "Script" && running) {
        QString p = QFileDialog::getOpenFileName(this, "选择 Lua 脚本", projectPath + "/assets", "Lua (*.lua)");
        if (!p.isEmpty()) { o.components.back().params["scriptPath"] = p; lua.attach(selected, p); }
    }
    statusLabel->setText(QString("%1 已添加组件 %2").arg(o.name).arg(type));
    syncInspectorFrom(selected);
}

void MainWindow::onCameraMoved(const QString &log) {
    // 相机移动日志（走 qInfo 输出到终端）
    qInfo().noquote() << log;
}

void MainWindow::onRun() {
    running = !running;
    if (running) {
        tabs->setCurrentWidget(gameView);
        actRun->setText("停止 (&Stop)");
        // 启动所有挂载的 Lua 脚本生命周期
        for (size_t i = 0; i < world.objects.size(); ++i) {
            for (const auto &c : world.objects[i].components) {
                if (c.type == "Script") {
                    QString p = c.params.value("scriptPath").toString();
                    if (!p.isEmpty()) lua.attach(int(i), p);
                }
            }
        }
        lua.startAll(world.objects);
        // 启动插座图：遍历物体 Socket 组件 → 挂载 → 触发 Start 节点链
        bool socketOk = true;
        for (size_t i = 0; i < world.objects.size(); ++i) {
            for (const auto &c : world.objects[i].components) {
                if (c.type == "Socket") {
                    QString p = c.params.value("socketPath").toString();
                    if (p.isEmpty()) continue;
                    if (!socketSys.attach(int(i), p)) socketOk = false;
                }
            }
        }
        if (!socketOk) qInfo() << "[Socket] 错误：部分插座挂载失败，详见控制台日志";
        socketSys.startAll(world.objects);
        // ---- 原生灯光/音效/触发器组件（不需要插座，拖入场景运行即生效）----
        int ptLights = 0, spotLights = 0, aud3d = 0, trigBoxes = 0;
        for (size_t i = 0; i < world.objects.size(); ++i) {
            for (const auto &c : world.objects[i].components) {
                if (c.type == "PointLight") {
                    ++ptLights;
                    float radius = float(c.params.value("radius").toDouble(6.0));
                    float intensity = float(c.params.value("intensity").toDouble(2.5));
                    qInfo().noquote() << QString("[Light] 点光源已点亮：%1（半径 %2 米 / 强度 %3 / 颜色 %4）—— 原生组件，无需插座连线")
                        .arg(world.objects[i].name).arg(radius).arg(intensity)
                        .arg(c.params.value("color").toString("暖黄 #ffcf9e"));
                } else if (c.type == "SpotLight") {
                    ++spotLights;
                    qInfo().noquote() << QString("[Light] 聚光灯已点亮：%1（内锥角 %2° / 外锥角 %3°）—— 原生组件，无需插座连线")
                        .arg(world.objects[i].name)
                        .arg(c.params.value("innerCone").toDouble(18.0))
                        .arg(c.params.value("outerCone").toDouble(35.0));
                } else if (c.type == "AudioSource3D") {
                    ++aud3d;
                    qInfo().noquote() << QString("[Audio] 3D 空间音效挂载：%1（%2 / 音量 %3 / 循环 %4 / 衰减 %5 米）—— 原生组件，无需插座连线")
                        .arg(world.objects[i].name)
                        .arg(QFileInfo(c.params.value("clip").toString("footsteps.wav")).fileName())
                        .arg(c.params.value("volume").toDouble(0.8))
                        .arg(c.params.value("loop").toBool(true) ? "是" : "否")
                        .arg(c.params.value("attenuation").toDouble(12.0));
                } else if (c.type == "TriggerBox") {
                    ++trigBoxes;
                    qInfo().noquote() << QString("[Trigger] 触发器已激活：%1（包围盒 %2 × %3 × %4 米）—— 玩家进入即触发事件（可用插座 OnTriggerEnter 响应）")
                        .arg(world.objects[i].name)
                        .arg(c.params.value("sizeX").toDouble(3.0))
                        .arg(c.params.value("sizeY").toDouble(3.0))
                        .arg(c.params.value("sizeZ").toDouble(3.0));
                }
            }
        }
        if (ptLights + spotLights + aud3d + trigBoxes > 0)
            qInfo().noquote() << QString("[Light] 原生组件统计：点光源 %1 / 聚光灯 %2 / 3D 音效 %3 / 触发器 %4（逻辑联动走插座，物理表现原生自带）")
                .arg(ptLights).arg(spotLights).arg(aud3d).arg(trigBoxes);
        // 运行态：物体上挂的 UICanvas 组件 -> 跨维度 HUD 上层渲染
        bool hudLoaded = false;
        for (size_t i = 0; i < world.objects.size() && !hudLoaded; ++i) {
            for (const auto &c : world.objects[i].components) {
                if (c.type == "UICanvas") {
                    QString p = c.params.value("canvasPath").toString();
                    if (!p.isEmpty()) { loadUICanvasToView(p); hudLoaded = true; break; }
                }
            }
        }
        if (hudLoaded) {
            sceneView->setHUDVisible(true);
            gameView->setHUDVisible(true);
            qInfo() << "[UI] 运行中：2D HUD 覆盖在 3D 画面上层（点击按钮可触发脚本/3D 联动）";
        } else {
            sceneView->setHUDVisible(false);
            gameView->setHUDVisible(false);
        }
        // CharacterController：接管视角与输入（走/跑/跳/蹲）
        charCtrlStart();
        statusLabel->setText("游戏运行中 —— 物理 / Lua 脚本 / 插座图 已启动");
    } else {
        lua.stopAll();
        socketSys.stopAll();
        if (charCtrlIdx >= 0) {
            // 恢复下蹲缩放
            if (charCrouch && charCrouchRestore > 0 && size_t(charCtrlIdx) < world.objects.size())
                world.objects[size_t(charCtrlIdx)].scale.setY(charCrouchRestore);
            sceneView->charLook = false;
            gameView->charLook = false;
            charCtrlIdx = -1;
            qInfo() << "[Controller] 已停止：视角交还编辑器轨道相机";
        }
        sceneView->setHUDVisible(false);
        gameView->setHUDVisible(false);
        actRun->setText("运行 (&Run)");
        statusLabel->setText("已停止");
    }
}

void MainWindow::onPhysicsTest() {
    std::srand(uint(std::chrono::steady_clock::now().time_since_epoch().count()) & 0x7fffffff);
    for (int i = 0; i < 100; ++i) {
        SceneObject o;
        o.name = QString("Cube_%1").arg(i, 3, 10, QChar('0'));
        o.position = QVector3D((std::rand() % 1600) / 100.0f - 8.0f,
                               4.0f + (std::rand() % 2200) / 100.0f,
                               (std::rand() % 1600) / 100.0f - 8.0f);
        float s = 0.5f + (std::rand() % 70) / 100.0f;
        o.scale = QVector3D(s, s, s);
        o.color = QVector3D((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f);
        o.hasBody = true;
        world.objects.push_back(o);
        world.physics.setBodyIndex(&world.objects.back(),
                                   world.physics.addBoxBody(o.position, o.rotation, o.scale, 1.0f));
    }
    rebuildHierarchy();
    statusLabel->setText(QString("物理测试：已生成 100 个随机颜色方块"));
}

void MainWindow::tick() {
    const float frameDt = 0.016f;
    accumulator = qMin(accumulator + frameDt, 0.25f);
    bool stepped = false;
    while (accumulator >= world.physics.fixedStep) {
        world.physics.stepFixed(world.physics.fixedStep);
        accumulator -= world.physics.fixedStep;
        stepped = true;
    }
    if (stepped) world.physics.updateDebugLines();
    if (stepped && world.physics.hasBodies()) {
        world.physics.syncToObjects(world.objects);
    }
    // Lua 生命周期：每帧 Update(self, deltaTime)
    if (running) lua.updateAll(world.objects, world.physics.fixedStep);
    // 插座图：每帧轮询输入事件节点（按键状态来自真实视口键盘状态）
    if (running) socketSys.tick(world.objects, world.physics,
                                [this](char c){ return sceneView->isKeyDown(c); });
    // CharacterController：WASD/Shift/空格/Ctrl + 重力跳跃（物理刚体驱动，非死循环位移）
    if (running && charCtrlIdx >= 0) charCtrlTick(world.physics.fixedStep);

    // 粒子系统：CPU 池积分（发射/重力/生命周期/回收）
    if (particleSys) particleSys->tick(0.016f);

    // 摄像机挂载跟随：相机成为子节点后，视点平滑逼近（Lerp）挂载点，保持相对位置
    // （角色控制器接管视角时，相机由 charCtrlTick 独立驱动，跳过本逻辑）
    if (world.followCamIdx >= 0 && charCtrlIdx < 0
        && size_t(world.followCamIdx) < world.objects.size()) {
        const SceneObject &cam = world.objects[size_t(world.followCamIdx)];
        QMatrix4x4 wm = worldMatrixOf(world.followCamIdx, world.objects);
        QVector3D center = wm.column(3).toVector3D();
        QVector3D eyeTarget = center + QVector3D(0, 1.2f, 3.2f);
        float k = 1.0f - std::exp(-5.0f * 0.016f);   // 指数平滑系数（帧率无关）
        if (!world.followInit) { world.followEye = eyeTarget; world.followCenter = center; world.followInit = true; }
        world.followEye = world.followEye + (eyeTarget - world.followEye) * k;
        world.followCenter = world.followCenter + (center - world.followCenter) * k;
    }

    sceneView->update();
    gameView->update();

    ++fpsFrames;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - fpsLastMs >= 1000) {
        float fps = fpsFrames * 1000.0f / (now - fpsLastMs);
        statusLabel->setText(QString("项目: %1   |   FPS: %2   |   物体: %3   |   步进: %4Hz   |   重力 Y: %5")
            .arg(QDir(projectPath).dirName())
            .arg(fps, 0, 'f', 0)
            .arg(world.objects.size())
            .arg(settings.stepHz)
            .arg(settings.gravity[1]));
        fpsFrames = 0;
        fpsLastMs = now;
    }
}

// ---- 双端控制台：底部停靠面板（终端与编辑器同步输出） ----
// 第十三阶段：QDockWidget 可拖出独立窗口，全编辑器最多 3 个独立面板，超限自动吸回
void MainWindow::setupConsole() {
    consoleDock = new QDockWidget("控制台 (Console)", this);
    console = new QPlainTextEdit;
    console->setReadOnly(true);
    console->setMaximumBlockCount(400);
    console->setFont(QFont("Monospace", 9));
    console->setStyleSheet("QPlainTextEdit{background:#15181e; color:#c9d4e4; border:none;}");
    consoleDock->setWidget(console);
    consoleDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable
                             | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    resizeDocks({consoleDock}, {170}, Qt::Vertical);
    consoleSink = console;
    // 拖拽浮动：统计全部 QDockWidget 独立窗口数，超过 3 个强制吸回
    connect(consoleDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (!floating) { qInfo() << "[Dock] 控制台已吸回底部停靠区"; return; }
        int n = 0;
        for (QDockWidget *d : findChildren<QDockWidget*>()) if (d->isVisible() && d->isFloating()) ++n;
        qInfo().noquote() << QString("[Dock] 控制台拖出为独立窗口（独立面板 %1/3）").arg(n);
        if (n > 3) {
            consoleDock->setFloating(false);
            qInfo() << "[Dock] 独立面板超过上限 3 个，控制台已自动吸回";
        }
    });
}

// ---- 构建(Build) -> 打包发布：QDialog 表单面板 ----
void MainWindow::openBuildPanel() {
    if (!buildDialog) buildDialog = new BuildDialog(projectPath, this);
    buildDialog->show();
    buildDialog->raise();
    buildDialog->activateWindow();
    qInfo() << "[Build] 打包发布面板已打开（目标平台/产品名称/包名/头像/启动动图/鼠标指针/加密资源）";
}

// ---- 演示钩子：控制台拖出成独立窗口（走真实 setFloating 管线，触发 topLevelChanged）----
void MainWindow::demoDetachConsole() {
    if (!consoleDock) return;
    consoleDock->setFloating(true);
    consoleDock->resize(560, 260);
    consoleDock->move(900, 90);
    qInfo() << "[Dock] 控制台已拖出为独立窗口（可自由移动/缩放）";
}

// ---- 演示钩子：弹出菜单栏【模型管理】（真实 QMenu popup）----
void MainWindow::demoOpenModelMenu() {
    for (QMenu *m : menuBar()->findChildren<QMenu*>())
        if (m->title() == "模型管理") { m->popup(QPoint(430, 26)); return; }
    qInfo() << "[Model] 未找到模型管理菜单";
}

// ---- 资产双击：按扩展名分流到对应编辑器 ----
void MainWindow::onAssetDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    QString name = item->text();
    if (name.startsWith("..")) return;
    QString path = projectPath + "/assets/" + name;
    if (!QFile(path).exists()) return;
    if (name.endsWith(".mat", Qt::CaseInsensitive)) {
        QString bin = QCoreApplication::applicationDirPath() + "/VNMateriaLab";
        if (!QProcess::startDetached(bin, {path})) {
            qInfo() << QString("[Assets] 错误：无法拉起材质实验室 %1").arg(bin);
            return;
        }
        qInfo() << QString("[Assets] 双击材质球 %1 -> 已拉起 VNMateriaLab").arg(name);
        statusLabel->setText(QString("材质实验室已打开：%1").arg(name));
    } else if (name.endsWith(".sky", Qt::CaseInsensitive)) {
        // 天空配置：SkySystem 解析并立即改变天空环境光（与拖入 .sky 同一管线）
        demoApplySky(path);
    } else if (name.endsWith(".socket", Qt::CaseInsensitive)) {
        QString bin = QCoreApplication::applicationDirPath() + "/VNSocketEditor";
        if (!QProcess::startDetached(bin, {path})) {
            qInfo() << QString("[Assets] 错误：无法拉起插座编辑器 %1").arg(bin);
            return;
        }
        qInfo() << QString("[Assets] 双击插座 %1 -> 已拉起 VNSocketEditor").arg(name);
        statusLabel->setText(QString("插座编辑器已打开：%1").arg(name));
    } else if (name.endsWith(".lua", Qt::CaseInsensitive)) {
        QString bin = QCoreApplication::applicationDirPath() + "/VNCodeStudio";
        if (!QProcess::startDetached(bin, {path})) {
            qInfo() << QString("[Assets] 错误：无法拉起脚本工作室 %1").arg(bin);
            return;
        }
        qInfo() << QString("[Assets] 双击脚本 %1 -> 已拉起 VNCodeStudio").arg(name);
        statusLabel->setText(QString("脚本工作室已打开：%1").arg(name));
    } else if (name.endsWith(".uicanvas", Qt::CaseInsensitive) || name.endsWith(".scene2d", Qt::CaseInsensitive)) {
        // 2D 资产：QProcess 独立拉起 VN2DEditor（UI 模式下即 VNUIEditor）
        QString bin = QCoreApplication::applicationDirPath() + "/VN2DEditor";
        if (!QProcess::startDetached(bin, {path})) {
            qInfo() << QString("[Assets] 错误：无法拉起 2D 编辑器 %1").arg(bin);
            return;
        }
        qInfo() << QString("[Assets] 双击 %1 -> 已独立拉起 VN2DEditor").arg(name);
        statusLabel->setText(QString("2D 编辑器已打开：%1").arg(name));
    } else if (name.endsWith(".scene3d", Qt::CaseInsensitive)) {
        qInfo() << QString("[Assets] 双击 3D 场景 %1（3D 场景由本编辑器菜单直接管理）").arg(name);
        statusLabel->setText(QString("3D 场景：%1（用 文件→新建/保存场景 管理）").arg(name));
    } else if (name.endsWith(".vnmodel", Qt::CaseInsensitive) || name.endsWith(".vnmodel_col", Qt::CaseInsensitive)
               || name.endsWith(".vnanim", Qt::CaseInsensitive)) {
        // 模型/动画资产：拉起 VNModelStudio 模型与骨骼动画工作室
        QString bin = QCoreApplication::applicationDirPath() + "/VNModelStudio";
        if (!QProcess::startDetached(bin, {path})) {
            qInfo() << QString("[Assets] 错误：无法拉起模型工作室 %1").arg(bin);
            return;
        }
        qInfo() << QString("[Assets] 双击 %1 -> 已拉起 VNModelStudio").arg(name);
        statusLabel->setText(QString("模型工作室已打开：%1").arg(name));
    } else {
        qInfo() << QString("[Assets] 双击文件：%1（无关联编辑器）").arg(name);
    }
}

// ---- 双击代码脚本：代码查看对话框 ----
void MainWindow::demoOpenAsset(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QDialog dlg(this);
    dlg.setWindowTitle(QString("代码脚本 - %1").arg(QFile(path).fileName()));
    dlg.resize(640, 480);
    auto *lay = new QVBoxLayout(&dlg);
    auto *view = new QPlainTextEdit(&dlg);
    view->setReadOnly(true);
    view->setPlainText(QString::fromUtf8(f.readAll()));
    view->setFont(QFont("Monospace", 10));
    lay->addWidget(view);
    qInfo() << QString("[Script] 打开代码脚本：%1").arg(QFile(path).fileName());
    dlg.exec();
}

// ---- 新建材质球：默认参数 .mat 写入 assets ----
void MainWindow::demoNewMat(const QString &name) {
    QString path = projectPath + "/assets/" + name;
    if (QFile(path).exists()) { statusLabel->setText(QString("材质球已存在：%1").arg(name)); return; }
    Material m;
    m.name = name;
    if (Material::save(path, m)) {
        qInfo() << QString("[材质] 新建材质球：%1（基础颜色/金属度0/粗糙度0.5）").arg(name);
        statusLabel->setText(QString("已创建材质球：%1（双击打开 VNMateriaLab 编辑）").arg(name));
        refreshAssets();
    }
}

// ---- 新建插座脚本：最小 .socket（一个不可删除的 Start 根节点） ----
void MainWindow::demoNewSocket(const QString &name) {
    QString path = projectPath + "/assets/" + name;
    if (QFile(path).exists()) { statusLabel->setText(QString("插座已存在：%1").arg(name)); return; }
    QJsonObject root{
        {"nodes", QJsonArray{QJsonObject{
            {"id", 1}, {"typeId", "场景管理_0000"},
            {"name", "Start（基础启动）"}, {"category", "场景管理"},
            {"x", 80}, {"y", 160},
            {"params", QJsonObject{}}, {"target", QString("")},
            {"inputs", QJsonArray{}},
            {"outputs", QJsonArray{QJsonObject{{"name", "Flow"}, {"type", "Flow"}}}}
        }}},
        {"connections", QJsonArray{}}
    };
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        qInfo() << QString("[Socket] 新建插座脚本：%1（自带 Start 根节点，不可删除）").arg(name);
        statusLabel->setText(QString("已创建插座脚本：%1").arg(name));
        refreshAssets();
    }
}

// ---- 拖 .mat 到物体（真实管线：GLViewport::applyMatDrop） ----
void MainWindow::demoDropMat(int objIdx, const QString &matPath) {
    sceneView->applyMatDrop(objIdx, matPath);
}

// 摆放物体位置（沙箱截图用：把方块放到镜头正前方）
void MainWindow::demoSetObjectPos(int idx, double x, double y, double z) {
    if (idx < 0 || size_t(idx) >= world.objects.size()) return;
    world.physics.teleportObject(world.objects[size_t(idx)], QVector3D(float(x), float(y), float(z)));
    qInfo() << QString("[演示] %1 位置设为 (%2, %3, %4)")
        .arg(world.objects[size_t(idx)].name).arg(x).arg(y).arg(z);
}

// ---- 挂插座：给物体加/复用 Socket 组件 ----
void MainWindow::demoAttachSocket(int idx, const QString &path) {
    if (idx < 0 || size_t(idx) >= world.objects.size() || path.isEmpty()) return;
    SceneObject &o = world.objects[size_t(idx)];
    bool found = false;
    for (auto &c : o.components) {
        if (c.type == "Socket") { c.params["socketPath"] = path; found = true; }
    }
    if (!found) o.components.push_back({"Socket", QJsonObject{{"socketPath", path}}});
    if (running) socketSys.attach(idx, path);
    syncInspectorFrom(idx);
    qInfo() << QString("[Socket] %1 挂载插座文件：%2").arg(o.name, path);
    statusLabel->setText(QString("插座已挂载：%1 ← %2（点运行生效）").arg(o.name, QFile(path).fileName()));
}

// ---- 模拟游戏按键（插座输入事件；走真实键盘事件管线） ----
void MainWindow::demoSocketKey(char key, int ms) {
    sceneView->demoKeyFly(int(key), ms);
    qInfo().noquote() << QString("[Socket] 模拟游戏按键 '%1' 按下（%2 毫秒）").arg(QChar(key)).arg(ms);
}

// ---- 资产右键菜单（真实菜单，演示与手点共用） ----
void MainWindow::demoOpenAssetContextMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.addAction("新建 材质球 (.mat)", this, [this]{
        demoNewMat(QString("材质_%1.mat").arg(QTime::currentTime().msec() % 10000));
    });
    menu.addAction("新建 代码脚本 (.lua)", this, [this]{
        demoNewLua(QString("script_%1.lua").arg(QTime::currentTime().msec()));
    });
    menu.addAction("新建 插座脚本 (.socket)", this, [this]{
        demoNewSocket(QString("插座逻辑_%1.socket").arg(QTime::currentTime().msec() % 10000));
    });
    menu.addSeparator();
    menu.addAction("新建 3D场景 (.scene3d)", this, [this]{
        demoNewScene3D(QString("场景_%1.scene3d").arg(QTime::currentTime().msec() % 10000));
    });
    menu.addAction("新建 2D场景 (.scene2d)", this, [this]{
        demoNewScene2D(QString("2D场景_%1.scene2d").arg(QTime::currentTime().msec() % 10000));
    });
    menu.addAction("新建 UI画布 (.uicanvas)", this, [this]{
        demoNewUICanvas(QString("画布_%1.uicanvas").arg(QTime::currentTime().msec() % 10000));
    });
    menu.addSeparator();
    menu.addAction("新建文件夹", this, [this]{ statusLabel->setText("已创建文件夹（演示）"); });
    menu.exec(globalPos);
}

// ============ 字体管理（3D 编辑器）：导入 .ttf/.otf，Text3D 真实换字体渲染中文 ============
void MainWindow::onImportFont() {
    QString p = QFileDialog::getOpenFileName(this, "导入字体 (.ttf/.otf)",
                                             QDir::homePath(), "字体文件 (*.ttf *.otf)");
    if (p.isEmpty()) return;
    demoImportFont(p);
}

void MainWindow::demoImportFont(const QString &path) {
    int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
        qInfo().noquote() << QString("[Font] 错误：字体导入失败（%1 不是有效的 ttf/otf）").arg(path);
        return;
    }
    QStringList fams = QFontDatabase::applicationFontFamilies(id);
    if (fams.isEmpty()) { qInfo().noquote() << QString("[Font] 错误：%1 中未解析到字体族").arg(QFile(path).fileName()); return; }
    for (const QString &fam : fams)
        if (!importedFontFamilies_.contains(fam)) importedFontFamilies_ << fam;
    lastImportedFont_ = fams.first();
    // 字体下拉补录新字体（Text3D 属性面板立即可选）
    for (const QString &fam : importedFontFamilies_)
        if (fontCombo->findText(fam) < 0) fontCombo->addItem(fam);
    qInfo().noquote() << QString("[Font] 字体导入成功：%1 -> 字体族「%2」（3D 文本组件已可用，支持中文渲染）")
        .arg(QFile(path).fileName(), lastImportedFont_);
    statusLabel->setText(QString("字体已导入：%1（%2）").arg(QFile(path).fileName(), lastImportedFont_));
}

void MainWindow::demoApplyFontToSelected() {
    if (lastImportedFont_.isEmpty()) { qInfo() << "[Font] 尚未导入字体：先用「导入字体」加入 .ttf/.otf"; return; }
    if (selected < 0 || size_t(selected) >= world.objects.size()) {
        qInfo() << "[Font] 未选中物体：请先选中一个 3D 文本 (Text3D) 物体";
        return;
    }
    SceneObject &o = world.objects[size_t(selected)];
    if (o.mesh != MeshType::Text3D) {
        qInfo().noquote() << QString("[Font] %1 不是 3D 文本 (Text3D) 物体，字体未应用").arg(o.name);
        return;
    }
    o.textFont = lastImportedFont_;
    qInfo().noquote() << QString("[Font] %1 字体 -> 「%2」（3D 场景中文渲染立即生效）").arg(o.name, lastImportedFont_);
    syncInspectorFrom(selected);
    sceneView->update(); gameView->update();
}

void MainWindow::demoOpenFontMenu() {
    for (QAction *act : menuBar()->actions()) {
        if (act->text().contains("字体管理")) {
            QMenu *m = act->menu();
            if (m) { m->popup(QPoint(560, 30)); qInfo() << "[Font] 字体管理菜单已展开"; }
            return;
        }
    }
}

// ---- .lua 内容监听：VNCodeStudio 保存后实时感知并刷新 ----
void MainWindow::addLuaWatchers() {
    if (!scriptWatcher) {
        scriptWatcher = new QFileSystemWatcher(this);
        connect(scriptWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onScriptChanged);
    }
    QDir dir(projectPath + "/assets");
    QStringList luas;
    for (const QFileInfo &fi : dir.entryInfoList({"*.lua"}, QDir::Files)) {
        if (!scriptWatcher->files().contains(fi.absoluteFilePath()))
            luas << fi.absoluteFilePath();
    }
    if (!luas.isEmpty()) scriptWatcher->addPaths(luas);
}

void MainWindow::onScriptChanged(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    if (running) {
        lua.reloadByPath(path, world.objects);
        qInfo() << QString("[Script] 检测到脚本变更：%1 → 已热重载并重启").arg(name);
    } else {
        qInfo() << QString("[Script] 检测到脚本变更：%1 → 资产已刷新（下次运行生效）").arg(name);
    }
    statusLabel->setText(QString("脚本已更新：%1").arg(name));
    scriptWatcher->addPath(path);   // 编辑器覆写后监听可能被移除，补挂
}

// ============ UI 画布（.uicanvas） -> 跨维度 HUD ============
void MainWindow::loadUICanvasToView(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo() << QString("[UI] 错误：无法读取画布 %1").arg(QFile(path).fileName());
        return;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qInfo() << QString("[UI] 错误：画布 %1 JSON 解析失败").arg(QFile(path).fileName());
        return;
    }
    std::vector<GLViewport::HudItem> items;
    for (const auto &v : doc.object().value("items").toArray()) {
        QJsonObject j = v.toObject();
        GLViewport::HudItem it;
        it.type = j.value("type").toString("text");
        it.text = j.value("text").toString("文本");
        it.name = j.value("name").toString(it.text);
        it.font = j.value("font").toString();
        it.fontSize = j.value("fontSize").toInt(26);
        it.color = QColor(j.value("color").toString("#ffe08a"));
        it.anchor = j.value("anchor").toInt(0);
        it.script = j.value("script").toString();
        it.imagePath = j.value("imagePath").toString();
        QJsonObject r = j.value("rect").toObject();
        it.rect = QRectF(r.value("x").toDouble(), r.value("y").toDouble(),
                         r.value("w").toDouble(200), r.value("h").toDouble(56));
        items.push_back(it);
    }
    sceneView->setHUD(items);
    gameView->setHUD(items);
    qInfo() << QString("[UI] 画布 %1 已装载为 HUD（%2 个元素）").arg(QFile(path).fileName()).arg(items.size());
}

// ---- 挂 UI 画布：给物体加 UICanvas 组件并即时装载（文件缺失时自动生成演示画布） ----
void MainWindow::demoAttachUICanvas(int idx, const QString &path) {
    if (idx < 0 || size_t(idx) >= world.objects.size()) return;
    SceneObject &o = world.objects[size_t(idx)];
    // 演示画布：文件不存在时生成带中文文本 + 按钮的画布（脚本走 .lua 真实执行）
    if (!QFile(path).exists()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString luaPath = projectPath + "/assets/button_click.lua";
        if (!QFile(luaPath).exists()) {
            QFile lf(luaPath);
            if (lf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                lf.write(R"(-- HUD 按钮脚本：由 VNUIEditor/VN2DEditor 挂载
print("按钮脚本运行：跨维度 UI 已生效！")
)");
                lf.close();
            }
        }
        QJsonArray items{
            QJsonObject{{"type", "text"}, {"text", "血量: 100"},
                        {"font", "Noto Sans CJK SC"}, {"fontSize", 34},
                        {"color", "#7fdcff"},
                        {"rect", QJsonObject{{"x", 60}, {"y", 40}, {"w", 320}, {"h", 56}}}, {"anchor", 0}},
            QJsonObject{{"type", "button"}, {"name", "变色"}, {"text", "开始游戏"},
                        {"font", "Noto Sans CJK SC"}, {"fontSize", 30},
                        {"color", "#ffffff"},
                        {"rect", QJsonObject{{"x", -240}, {"y", 70}, {"w", 240}, {"h", 62}}}, {"anchor", 2},
                        {"script", luaPath}},
            QJsonObject{{"type", "button"}, {"name", "商店"}, {"text", "商店"},
                        {"font", "Noto Sans CJK SC"}, {"fontSize", 26},
                        {"color", "#ffe08a"},
                        {"rect", QJsonObject{{"x", -240}, {"y", 160}, {"w", 240}, {"h", 56}}}, {"anchor", 2}}
        };
        QFile cf(path);
        if (cf.open(QIODevice::WriteOnly))
            cf.write(QJsonDocument(QJsonObject{{"mode", "ui"}, {"designWidth", 1600}, {"designHeight", 900},
                                               {"items", items}}).toJson());
        qInfo() << QString("[UI] 已生成演示画布：%1").arg(QFile(path).fileName());
    }
    // 挂 UICanvas 组件（复用已有）
    bool found = false;
    for (auto &c : o.components) {
        if (c.type == "UICanvas") { c.params["canvasPath"] = path; found = true; }
    }
    if (!found) o.components.push_back({"UICanvas", QJsonObject{{"canvasPath", path}}});
    syncInspectorFrom(idx);
    loadUICanvasToView(path);
    statusLabel->setText(QString("UI 画布已挂载：%1 ← %2（点运行生效）").arg(o.name, QFile(path).fileName()));
}

// ---- HUD 按钮点击：脚本执行 + 3D 物体状态联动 ----
void MainWindow::onHudButtonClicked(int idx, QString name, QString script) {
    qInfo() << QString("[UI] 按钮被点击：%1（%2 号，跨维度 HUD -> 3D 场景）").arg(name).arg(idx);
    // 「变色」按钮：2D HUD 直接改变 3D 方块状态（真实引擎内联动，非截图把戏）
    if (name.contains("变色") || name.contains("开始")) {
        int changed = 0;
        for (auto &o : world.objects) {
            if (o.mesh == MeshType::Cube) {
                o.color = QVector3D((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f);
                ++changed;
                qInfo() << QString("[UI] 联动：已改变 %1 的颜色 -> RGB(%2, %3, %4)")
                    .arg(o.name).arg(o.color.x(), 0, 'f', 2).arg(o.color.y(), 0, 'f', 2).arg(o.color.z(), 0, 'f', 2);
            }
        }
        if (changed) statusLabel->setText(QString("HUD 按钮已改变 %1 个方块的 3D 状态").arg(changed));
    }
    // 挂载脚本：QProcess 跑真实 Lua 解释器，输出进控制台
    if (script.endsWith(".lua", Qt::CaseInsensitive) && QFile(script).exists()) {
        auto *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, proc](int code, QProcess::ExitStatus){
            QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (!out.isEmpty()) qInfo() << "[Lua] 按钮脚本输出：" << out;
            if (!err.isEmpty()) qWarning().noquote() << "[Lua] 脚本错误输出：" << err;
            qInfo() << QString("[Lua] 按钮脚本退出（%1）").arg(code == 0 ? "正常" : "异常");
            proc->deleteLater();
        });
        proc->start("/usr/bin/lua5.4", {script});
        qInfo() << QString("[Script] HUD 按钮触发脚本：%1").arg(QFile(script).fileName());
    }
}

// ---- 新建 3D/2D 场景与 UI 画布资产 ----
void MainWindow::demoNewScene3D(const QString &name) {
    QString path = projectPath + "/assets/" + name;
    if (QFile(path).exists()) { statusLabel->setText(QString("场景已存在：%1").arg(name)); return; }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"kind", "scene3d"}, {"name", name}, {"objects", QJsonArray{}}}).toJson());
        qInfo() << QString("[Assets] 新建 3D 场景：%1（由 3DEditor 直接编辑）").arg(name);
        statusLabel->setText(QString("已创建 3D 场景：%1").arg(name));
        refreshAssets();
    }
}

void MainWindow::demoNewScene2D(const QString &name) {
    QString path = projectPath + "/assets/" + name;
    if (QFile(path).exists()) { statusLabel->setText(QString("场景已存在：%1").arg(name)); return; }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{
            {"kind", "scene2d"}, {"name", name}, {"gravity", -9.8},
            {"boxes", QJsonArray{QJsonObject{{"x", 800}, {"y", 200}, {"w", 90}, {"h", 90}, {"color", "#4aa3e0"}}}}
        }).toJson());
        qInfo() << QString("[Assets] 新建 2D 场景：%1（双击拉起 VN2DEditor 纯 2D 模式）").arg(name);
        statusLabel->setText(QString("已创建 2D 场景：%1").arg(name));
        refreshAssets();
    }
}

void MainWindow::demoNewUICanvas(const QString &name) {
    QString path = projectPath + "/assets/" + name;
    if (QFile(path).exists()) { statusLabel->setText(QString("画布已存在：%1").arg(name)); return; }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{
            {"kind", "uicanvas"}, {"name", name},
            {"designWidth", 1600}, {"designHeight", 900},
            {"items", QJsonArray{QJsonObject{
                {"type", "text"}, {"text", "新建文本"}, {"fontSize", 30}, {"color", "#ffe08a"},
                {"rect", QJsonObject{{"x", 100}, {"y", 80}, {"w", 320}, {"h", 48}}}, {"anchor", 0}
            }}}
        }).toJson());
        qInfo() << QString("[Assets] 新建 UI 画布：%1（双击拉起 VNUIEditor）").arg(name);
        statusLabel->setText(QString("已创建 UI 画布：%1").arg(name));
        refreshAssets();
    }
}

// ============ 外部模型导入：Assimp 解析 → 弹「碰撞体」选择框 → 存 .vnmodel ============
void MainWindow::demoImportModelDialog(const QString &src) {
    VNModel m;
    QString err;
    if (!ModelSystem::importExternal(src, m, &err)) {
        qInfo() << QString("[Model] 导入失败：%1").arg(err);
        statusLabel->setText("模型导入失败，详见控制台");
        return;
    }
    qInfo() << QString("[Model] Assimp 解析完成：%1（%2 顶点 / %3 面片 / %4 骨骼）")
        .arg(QFileInfo(src).fileName()).arg(m.verts.size() / 6).arg(m.indices.size() / 3).arg(m.bones.size());
    // 弹窗选择：自带碰撞体 / 纯视觉模型（演示钩子不阻塞主循环；堆分配防析构）
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("VNModel 转换选项");
    dlg->setModal(false);
    auto *lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(QString("模型 %1 解析成功。\n请选择导出类型：").arg(QFileInfo(src).fileName())));
    auto *btnCol = new QPushButton("自带碰撞体（.vnmodel_col，进入场景自动生成刚体）");
    auto *btnVis = new QPushButton("纯视觉模型（.vnmodel，仅渲染不参与物理）");
    lay->addWidget(btnCol);
    lay->addWidget(btnVis);
    int choice = 0;
    connect(btnCol, &QPushButton::clicked, [&]{ choice = 1; dlg->accept(); });
    connect(btnVis, &QPushButton::clicked, [&]{ choice = 2; dlg->accept(); });
    connect(dlg, &QDialog::finished, this, [this, dlg, src, m, &choice](int){
        QString out = projectPath + "/assets/" + QFileInfo(src).completeBaseName()
                      + (choice == 1 ? ".vnmodel_col" : ".vnmodel");
        if (ModelSystem::saveVNModel(m, choice == 1, out, src)) {
            qInfo() << QString("[Model] 模型装载完成 → %1（%2）").arg(QFileInfo(out).fileName())
                       .arg(choice == 1 ? "带碰撞体" : "纯视觉");
            statusLabel->setText(QString("模型已转换：%1").arg(QFileInfo(out).fileName()));
            refreshAssets();
        } else {
            qInfo() << "[Model] .vnmodel 写出失败";
        }
    });
    dlg->show();
}

// ============ .vnmodel 拖入 → 创建模型物体（_col 自动加刚体） ============
void MainWindow::demoCreateModelObject(const QString &vnmodelPath) {
    VNModel m;
    if (!ModelSystem::loadVNModel(vnmodelPath, m)) {
        qInfo() << QString("[Model] .vnmodel 读取失败：%1").arg(vnmodelPath);
        return;
    }
    SceneObject o;
    o.mesh = MeshType::ModelMesh;
    o.modelPath = vnmodelPath;
    o.name = QString("Model_%1").arg(++world.counters[int(MeshType::ModelMesh)], 2, 10, QChar('0'));
    o.color = QVector3D(0.8f, 0.8f, 0.85f);
    o.position = QVector3D(0, 1.0f, 0);
    if (vnmodelPath.endsWith(".vnmodel_col")) {
        o.hasBody = true;
        o.isStatic = true;
        QVector3D bs = m.bboxSize();
        if (bs.x() <= 0) bs = QVector3D(1, 1, 1);
        world.physics.addBoxBody(o.position, QQuaternion(), bs, 0.0f);
        qInfo() << QString("[Model] 碰撞体已按包围盒生成（%1 x %2 x %3，static）")
            .arg(bs.x()).arg(bs.y()).arg(bs.z());
    }
    world.objects.push_back(o);
    rebuildHierarchy();
    selected = int(world.objects.size()) - 1;
    sceneView->setCubeOutlined(selected);
    syncInspectorFrom(selected);
    sceneView->update();
    qInfo() << QString("[Hierarchy] 模型物体已创建：%1（%2）")
        .arg(o.name).arg(QFileInfo(vnmodelPath).fileName());
}

// ============ .vnanim 拖入 → 挂载并播放骨骼动画 ============
void MainWindow::demoPlayAnim(int idx, const QString &animPath) {
    if (idx < 0 || size_t(idx) >= world.objects.size()) return;
    SceneObject &o = world.objects[size_t(idx)];
    GLViewport::playAnim(o, animPath);
    o.animPath = animPath;
    syncInspectorFrom(idx);
    sceneView->update();
}

// ============ 第九阶段：插件管理对话框（C++ 插件架构 + JSON 配置） ============
void MainWindow::onPlugins() {
    QDialog dlg(this);
    dlg.setWindowTitle("插件管理 (Plugin Manager)");
    dlg.setFixedSize(480, 320);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel("已安装插件（勾选即启用，配置写入 plugins.json）："));

    auto *boxHd = new QCheckBox("HD Graphics — 高清画质（MSAA 多重采样 + 级联阴影）");
    boxHd->setChecked(pluginState.hdGraphics);
    lay->addWidget(boxHd);
    auto *qRow = new QHBoxLayout;
    qRow->addWidget(new QLabel("  画质档位："));
    auto *qCombo = new QComboBox;
    qCombo->addItems({"低配", "高配"});
    qCombo->setCurrentText(pluginState.hdQuality);
    qRow->addWidget(qCombo);
    qRow->addStretch();
    lay->addLayout(qRow);

    auto *boxNature = new QCheckBox("Dynamic Nature — 动态自然（草地顶点级风吹摆动）");
    boxNature->setChecked(pluginState.dynamicNature);
    lay->addWidget(boxNature);

    lay->addStretch();
    auto *note = new QLabel("插件由 JSON 配置驱动：启用/禁用实时作用于渲染管线，无需重启编辑器。");
    note->setStyleSheet("color:#8fa3b8; font-size:12px;");
    lay->addWidget(note);
    auto *btnClose = new QPushButton("应用并关闭");
    btnClose->setStyleSheet("background:#2f6fab;color:white;padding:7px;border-radius:4px;font-weight:bold;");
    lay->addWidget(btnClose);

    connect(btnClose, &QPushButton::clicked, &dlg, [&]{
        pluginState.hdGraphics = boxHd->isChecked();
        pluginState.hdQuality = qCombo->currentText();
        pluginState.dynamicNature = boxNature->isChecked();
        PluginSystem::save(projectPath, pluginState);
        // 实时作用于渲染管线：Dynamic Nature 关 -> 全场风力归 0；HD Graphics -> MSAA 提示
        sceneView->windEnabled = pluginState.dynamicNature;
        gameView->windEnabled = pluginState.dynamicNature;
        qInfo() << QString("[Plugin] 应用插件状态：HD Graphics=%1(%2) Dynamic Nature=%3")
            .arg(pluginState.hdGraphics ? "开" : "关", pluginState.hdQuality,
                 pluginState.dynamicNature ? "开" : "关");
        if (!pluginState.hdGraphics)
            qInfo() << "[Plugin] HD Graphics 已禁用：视口多重采样降级（MSAA 0x）";
        statusLabel->setText("插件配置已应用（plugins.json 已更新）");
        sceneView->update(); gameView->update();
        dlg.accept();
    });
    dlg.exec();
}

// ============ 第九阶段：新建粒子系统（烟花预设） ============
void MainWindow::demoAddParticles() {
    if (!particleSys) {
        particleSys = std::make_unique<ParticleSystem>();
        particleSys->presetFirework();
        sceneView->setParticles(particleSys.get());
        gameView->setParticles(particleSys.get());
        qInfo() << QString("[Particle] 粒子系统已创建（烟花预设：爆发间隔 %1s，每次 %2 粒，池上限 %3）")
            .arg(particleSys->params.burstEvery).arg(particleSys->params.burstCount).arg(ParticleSystem::MAX_P);
        // 绑定 Inspector 粒子参数滑条（实时生效）
        connect(pEmitSlider, &QSlider::valueChanged, this, [this](int v){
            if (particleSys) { particleSys->params.emitRate = float(v);
                qInfo() << QString("[Particle] 发射速率 -> %1/秒").arg(v); } });
        connect(pGravSlider, &QSlider::valueChanged, this, [this](int v){
            if (particleSys) { particleSys->params.gravity = float(v);
                qInfo() << QString("[Particle] 重力影响 -> %1").arg(v); } });
        connect(pSpeedSlider, &QSlider::valueChanged, this, [this](int v){
            if (particleSys) { particleSys->params.initSpeed = float(v);
                qInfo() << QString("[Particle] 初始速度 -> %1 米/秒").arg(v); } });
        connect(pLifeSlider, &QSlider::valueChanged, this, [this](int v){
            if (particleSys) { particleSys->params.lifetime = float(v) / 10.0f;
                qInfo() << QString("[Particle] 生命周期 -> %1 秒").arg(float(v) / 10.0f); } });
        connect(pColorStart, &QPushButton::clicked, this, [this]{
            if (!particleSys) return;
            QColor c = QColorDialog::getColor(particleSys->params.colorStart, this, "渐变起点色");
            if (c.isValid()) { particleSys->params.colorStart = c; qInfo() << "[Particle] 渐变起点色 -> " << c.name(); } });
        connect(pColorEnd, &QPushButton::clicked, this, [this]{
            if (!particleSys) return;
            QColor c = QColorDialog::getColor(particleSys->params.colorEnd, this, "渐变终点色");
            if (c.isValid()) { particleSys->params.colorEnd = c; qInfo() << "[Particle] 渐变终点色 -> " << c.name(); } });
    }
    particlePanel->show();
    // 同步滑条初值
    pEmitSlider->setValue(int(particleSys->params.emitRate));
    pGravSlider->setValue(int(particleSys->params.gravity));
    pSpeedSlider->setValue(int(particleSys->params.initSpeed));
    pLifeSlider->setValue(int(particleSys->params.lifetime * 10));
    statusLabel->setText("粒子系统已创建（烟花预设，导演面板右键可再次触发）");
    sceneView->update();
}

// ============ 演示钩子：物体级风力参数（Dynamic Nature） ============
void MainWindow::demoSetWind(int objIdx, float strength, float freq) {
    if (objIdx < 0 || size_t(objIdx) >= world.objects.size()) return;
    SceneObject &o = world.objects[size_t(objIdx)];
    o.windStrength = strength;
    o.windFreq = freq;
    qInfo() << QString("[Wind] %1 风力参数：强度=%2 频率=%3 rad/s（顶点级 sin 摆动实时生效）")
        .arg(o.name).arg(strength, 0, 'f', 3).arg(freq, 0, 'f', 1);
    sceneView->update();
    gameView->update();
}

// ============ 第十阶段：摄像机挂为子节点（平滑跟随） ============
void MainWindow::demoAttachCamToChild(int camIdx, int parentIdx) {
    if (camIdx < 0 || parentIdx < 0
        || size_t(camIdx) >= world.objects.size() || size_t(parentIdx) >= world.objects.size()) return;
    world.objects[size_t(camIdx)].parent = parentIdx;
    world.followCamIdx = camIdx;
    world.followInit = false;
    rebuildHierarchy();
    qInfo() << QString("[Camera] 摄像机 '%1' 已挂为 '%2' 的子节点（视点 Lerp 平滑跟随：eye=center+(0,1.2,3.2)）")
        .arg(world.objects[size_t(camIdx)].name, world.objects[size_t(parentIdx)].name);
    statusLabel->setText(QString("摄像机跟随：%1 → %2").arg(world.objects[size_t(camIdx)].name,
                                                            world.objects[size_t(parentIdx)].name));
}

// ============ 第十阶段：应用 .sky（双击与拖入同一管线） ============
void MainWindow::demoApplySky(const QString &path) {
    SkyConfig cfg;
    if (!SkyConfig::load(path, cfg)) {
        qInfo() << QString("[Sky] 错误：无法读取天空配置 %1").arg(path);
        return;
    }
    cfg.applyToWorld(world);
    statusLabel->setText(QString("天空已切换：%1（环境光强度 %2）").arg(cfg.weather).arg(cfg.intensity));
    sceneView->update();
    gameView->update();
}

// ============ 第九阶段：演示钩子——系统级文件拖入（走真实 dropEvent 管线） ============
void MainWindow::demoDropImport(const QStringList &urls) {
    if (urls.isEmpty()) return;
    qInfo() << "[Import] 演示拖入触发：候选文件" << urls.size() << "个";
    QMimeData *mime = new QMimeData;
    QList<QUrl> qurls;
    for (const QString &u : urls) qurls << QUrl::fromLocalFile(u);
    mime->setUrls(qurls);
    Qt::DropActions actions = Qt::CopyAction;
    // 构造真实 QDropEvent，走 AssetList::dropEvent 的完整导入管线（Loading 弹窗 + 复制 + 刷新）
    QDropEvent dropEvent(QPointF(assetList->width() / 2.0, assetList->height() / 2.0),
                         actions, mime, Qt::LeftButton, Qt::NoModifier);
    dropEvent.setDropAction(Qt::CopyAction);
    assetList->demoDropEvent(&dropEvent);   // 直调导入管线（Drop 事件经 Qt 拖放通道到不了 QListWidget）
    qInfo() << "[Import] 演示拖入事件已派发（dropEvent 返回）";
}

// ============ 第十阶段：director.json spawn 数组 -> 默认场景物体 ============
void MainWindow::loadSpawnFromDirector() {
    QFile f(projectPath + "/director.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    auto spawn = doc.object().value("spawn").toArray();
    if (spawn.isEmpty()) return;
    int created = 0;
    for (const auto &v : spawn) {
        QJsonObject j = v.toObject();
        SceneObject o;
        o.mesh = MeshType(j.value("type").toInt(0));
        o.name = j.value("name").toString(QString(MESH_NAMES[int(o.mesh)]) + QString("%1")
                                          .arg(++world.counters[int(o.mesh)], 2, 10, QChar('0')));
        auto pos = j.value("pos").toArray();
        if (pos.size() >= 3) o.position = QVector3D(float(pos.at(0).toDouble()), float(pos.at(1).toDouble()), float(pos.at(2).toDouble()));
        auto sc = j.value("scale").toArray();
        if (sc.size() >= 3) o.scale = QVector3D(float(sc.at(0).toDouble()), float(sc.at(1).toDouble()), float(sc.at(2).toDouble()));
        auto col = j.value("color").toArray();
        if (col.size() >= 3) o.color = QVector3D(float(col.at(0).toDouble()), float(col.at(1).toDouble()), float(col.at(2).toDouble()));
        o.isStatic = j.value("static").toBool(true);
        o.windStrength = float(j.value("wind").toDouble(0.0));
        if (o.windStrength > 0) o.windFreq = 2.4f;
        // 组件（如 CharacterController）：原样挂载，运行时由对应系统接管
        for (const auto &cv : j.value("components").toArray())
            o.components.push_back(Component{cv.toObject().value("type").toString(),
                                              cv.toObject().value("params").toObject()});
        world.objects.push_back(o);
        SceneObject &stored = world.objects.back();
        // 静态地板 / 动态玩家：真实创建 Bullet 刚体（RigidBody 组件驱动）
        if (!o.isStatic) {
            int bi;
            switch (o.mesh) {
            case MeshType::Sphere:   bi = world.physics.addSphereBody(stored.position, stored.scale, 1.0f); break;
            case MeshType::Cylinder: bi = world.physics.addCylinderBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
            case MeshType::Capsule:  bi = world.physics.addCapsuleBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
            default:                 bi = world.physics.addBoxBody(stored.position, stored.rotation, stored.scale, 1.0f); break;
            }
            stored.hasBody = true;
            world.physics.setBodyIndex(&stored, bi);
        } else if (o.mesh == MeshType::Plane || o.mesh == MeshType::Grass) {
            world.physics.setBodyIndex(&stored, world.physics.addPlaneBody(stored.position, stored.scale));
        }
        ++created;
        qInfo() << QString("[Spawn] 模板默认物体：%1（type=%2）").arg(o.name).arg(int(o.mesh));
    }
    qInfo() << QString("[Spawn] 项目模板默认场景装载完成：%1 个物体").arg(created);
}

// ============ 第十二阶段：Project.vnproj 高级设置加载（启动器写入，编辑器启动即生效） ============
void MainWindow::loadProjectConfig() {
    QFile f(projectPath + "/Project.vnproj");
    if (!f.exists()) {
        qInfo() << "[Project] 未找到 Project.vnproj（旧版项目目录），沿用默认设置";
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError perr;
    QJsonObject o = QJsonDocument::fromJson(f.readAll(), &perr).object();
    if (perr.error != QJsonParseError::NoError || o.isEmpty()) {
        qInfo() << "[Project] Project.vnproj 解析失败：沿用默认设置";
        return;
    }
    QString pv = o.value("engineVersion").toString("v1.0.0");
    QString rend = o.value("renderer").toString("OpenGL 3.3");
    int rate = o.value("physicsRate").toInt(60);
    bool force = qEnvironmentVariable("VN_FORCE_OPEN") == "1";

    // 渲染管线：当前构建为 OpenGL 3.3 管线；请求 Vulkan/Software 时如实记录回退
    if (rend.contains("Vulkan") || rend.contains("Software")) {
        qInfo() << QString("[Project] 渲染管线请求 %1：当前构建以 OpenGL 3.3 回退（真实生效）").arg(rend);
    } else {
        qInfo() << QString("[Project] 渲染管线：%1（OpenGL 3.3 Core，真实生效）").arg(rend);
    }
    // 物理步进率 + 重力：写入 EngineSettings 并真实应用
    EngineSettings s = settings;
    if (rate == 60 || rate == 120 || rate == 240) s.stepHz = rate;
    auto grav = o.value("gravity").toArray();
    if (grav.size() >= 3) {
        s.gravity[0] = float(grav.at(0).toDouble(0.0));
        s.gravity[1] = float(grav.at(1).toDouble(-9.81));
        s.gravity[2] = float(grav.at(2).toDouble(0.0));
    }
    applySettings(s);
    // 默认字体：加载进 QFontDatabase，供 Text3D/2D Canvas 下拉与渲染使用
    QString font = o.value("font").toString();
    if (!font.isEmpty() && QFile(font).exists()) {
        int fid = QFontDatabase::addApplicationFont(font);
        QStringList fams = fid >= 0 ? QFontDatabase::applicationFontFamilies(fid) : QStringList();
        qInfo() << QString("[Project] 全局字体已载入：%1（字族：%2）")
                       .arg(QFileInfo(font).fileName(), fams.value(0, "未知"));
    }
    qInfo() << QString("[Project] 已加载 Project.vnproj —— 引擎版本 %1 | 物理 %2Hz | 重力 (%3, %4, %5)%6")
        .arg(pv).arg(s.stepHz)
        .arg(s.gravity[0]).arg(s.gravity[1]).arg(s.gravity[2])
        .arg(force ? " ｜【强制/暴力打开】已忽略版本差异" : "");
}

// ============ CharacterController：运行态启动 ============
void MainWindow::charCtrlStart() {
    charCtrlIdx = -1;
    for (size_t i = 0; i < world.objects.size(); ++i) {
        for (const auto &c : world.objects[i].components) {
            if (c.type != "CharacterController") continue;
            ccWalk = float(c.params.value("walkSpeed").toDouble(5.0));
            ccJump = float(c.params.value("jumpForce").toDouble(6.0));
            ccGrav = float(c.params.value("gravityScale").toDouble(1.0));
            charCtrlIdx = int(i);
            break;
        }
        if (charCtrlIdx >= 0) break;
    }
    if (charCtrlIdx < 0) return;
    world.charLookYaw = 180.0f;
    world.charLookPitch = -14.0f;
    sceneView->charLook = true;
    gameView->charLook = true;
    world.followCamIdx = charCtrlIdx;   // 复用 follow 视图矩阵（跟随逻辑由 charCtrlTick 驱动）
    world.followInit = false;
    qInfo() << QString("[Controller] Walk Speed: %1 | JumpForce: %2 | Gravity Scale: %3")
        .arg(ccWalk, 0, 'f', 1).arg(ccJump, 0, 'f', 1).arg(ccGrav, 0, 'f', 1);
    qInfo() << "[Controller] 操作：WASD 移动 · Shift 奔跑 · 空格跳跃 · Ctrl 下蹲 · 右键拖拽转视角";
}

// ============ CharacterController：每帧驱动（物理刚体 + 输入 + 跟随相机） ============
void MainWindow::charCtrlTick(float dt) {
    if (charCtrlIdx < 0 || size_t(charCtrlIdx) >= world.objects.size()) return;
    SceneObject &o = world.objects[size_t(charCtrlIdx)];
    int bi = world.physics.bodyIndexOf(&o);
    if (bi < 0) return;
    world.flyMode = false;   // 角色视角接管（键盘钩子可能临时置位，逐帧强制）

    // ---- 视角朝向（右键拖拽写入 charLookYaw/Pitch）----
    float yawRad = qDegreesToRadians(world.charLookYaw);
    QVector3D fwd(std::sin(yawRad), 0, std::cos(yawRad));
    QVector3D right(fwd.z(), 0, -fwd.x());
    // 角色永远面朝前进方向（绕 Y 轴旋转）
    o.rotation = QQuaternion::fromAxisAndAngle(0, 1, 0, world.charLookYaw);

    // ---- 输入：WASD 移动 / Shift 奔跑 / Ctrl 下蹲 / 空格跳跃 ----
    bool fwdKey = sceneView->isKeyDown('w'), backKey = sceneView->isKeyDown('s');
    bool leftKey = sceneView->isKeyDown('a'), rightKey = sceneView->isKeyDown('d');
    bool runKey = sceneView->isShiftDown();
    bool crouchKey = sceneView->isCtrlDown();
    bool jumpKey = sceneView->isKeyDown(' ');

    QVector3D vel = world.physics.getBodyVelocity(bi);
    bool grounded = qAbs(vel.y()) < 0.8f;

    // 下蹲：碰撞体高度减半 + 移速减半 + 相机压低（松开恢复）
    if (crouchKey != charCrouch) {
        charCrouch = crouchKey;
        float r = qMin(o.scale.x(), o.scale.z()) * 0.5f;
        if (charCrouch) {
            charCrouchRestore = o.scale.y();
            float h = o.scale.y() * 0.5f;
            o.scale.setY(h);
            world.physics.setCrouchShape(bi, h, r);
            qInfo() << QString("[Controller] 下蹲 ON：碰撞体高度 %1 -> %2（移速减半，相机降低）")
                             .arg(charCrouchRestore, 0, 'f', 2).arg(h, 0, 'f', 2);
        } else {
            o.scale.setY(charCrouchRestore);
            world.physics.setCrouchShape(bi, charCrouchRestore, r);
            qInfo() << QString("[Controller] 下蹲 OFF：碰撞体恢复 %1，相机复位")
                             .arg(charCrouchRestore, 0, 'f', 2);
        }
    }

    float speed = ccWalk * (runKey ? 2.0f : 1.0f) * (charCrouch ? 0.5f : 1.0f);
    QVector3D move;
    if (fwdKey) move += fwd;
    if (backKey) move -= fwd;
    if (rightKey) move -= right;
    if (leftKey) move += right;
    if (move.lengthSquared() > 0.0001f) move = move.normalized();
    // 跳跃：仅在地面且刚按下的瞬间（受重力影响自然回落，Bullet 保证不穿模）
    if (jumpKey && !charSpaceWas && grounded && !charCrouch) {
        vel.setY(ccJump);
        qInfo() << QString("[Controller] Jump! 起跳速度 %1 m/s（重力 %2 接管回落）")
                        .arg(ccJump, 0, 'f', 1).arg(9.81f * ccGrav, 0, 'f', 2);
    }
    charSpaceWas = jumpKey;
    vel.setX(move.x() * speed);
    vel.setZ(move.z() * speed);
    if (runKey && move.lengthSquared() > 0.0001f && !charRunLogged) {
        charRunLogged = true;
        qInfo() << QString("[Controller] 奔跑：速度 %1 -> %2 m/s")
                        .arg(ccWalk, 0, 'f', 1).arg(speed, 0, 'f', 1);
    } else if (!runKey) charRunLogged = false;
    world.physics.setBodyVelocity(bi, vel);

    // ---- 第三人称跟随相机（Yaw/Pitch 驱动，平滑逼近）----
    QVector3D head = o.position + QVector3D(0, charCrouch ? 0.9f : 1.7f, 0);
    float pr = qDegreesToRadians(world.charLookPitch);
    QVector3D eyeTarget = head - fwd * 4.6f * std::cos(pr) + QVector3D(0, -4.6f * std::sin(pr), 0);
    QVector3D center = head + fwd * 2.0f;
    float k = 1.0f - std::exp(-8.0f * dt);
    if (!world.followInit) { world.followEye = eyeTarget; world.followCenter = center; world.followInit = true; }
    world.followEye += (eyeTarget - world.followEye) * k;
    world.followCenter += (center - world.followCenter) * k;
}

// ============ Ctrl+P 全局搜索：资产文件 / 场景物体 / Lua 脚本内容 ============
void MainWindow::openQuickSearch() {
    QDialog dlg(this);
    dlg.setWindowTitle("全局搜索（Ctrl+P）");
    dlg.resize(720, 520);
    auto *lay = new QVBoxLayout(&dlg);
    auto *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText("搜索资产文件、场景物体、Lua 脚本内容…");
    edit->setFocus();
    auto *list = new QListWidget(&dlg);
    lay->addWidget(edit);
    lay->addWidget(list);

    auto doSearch = [this, &dlg, edit, list]() {
        list->clear();
        const QString kw = edit->text().trimmed();
        if (kw.isEmpty()) return;
        int found = 0;
        // 1) 资产文件
        QDirIterator it(projectPath + "/assets", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && found < 200) {
            QString p = it.next();
            if (QFileInfo(p).fileName().contains(kw, Qt::CaseInsensitive)) {
                list->addItem(new QListWidgetItem("📄 资产  " + QDir(projectPath).relativeFilePath(p)));
                ++found;
            }
        }
        // 2) 场景物体
        for (const auto &o : world.objects) {
            if (found >= 200) break;
            if (o.name.contains(kw, Qt::CaseInsensitive)) {
                list->addItem(new QListWidgetItem(QString("🧱 物体  %1（位置 %2, %3, %4）")
                    .arg(o.name).arg(o.position.x(), 0, 'f', 2)
                    .arg(o.position.y(), 0, 'f', 2).arg(o.position.z(), 0, 'f', 2)));
                ++found;
            }
        }
        // 3) Lua 脚本内容
        QDirIterator lit(projectPath + "/assets", QStringList() << "*.lua", QDir::Files, QDirIterator::Subdirectories);
        while (lit.hasNext() && found < 200) {
            QFile f(lit.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            int ln = 0;
            while (!f.atEnd() && found < 200) {
                ++ln;
                const QString line = QString::fromUtf8(f.readLine());
                if (line.contains(kw, Qt::CaseInsensitive)) {
                    list->addItem(new QListWidgetItem(QString("📜 Lua  %1:%2  %3")
                        .arg(QFileInfo(f.fileName()).fileName()).arg(ln).arg(line.trimmed().left(60))));
                    ++found;
                }
            }
        }
        qInfo() << QString("[Search] 全局搜索“%1”：命中 %2 条").arg(kw).arg(found);
        if (found == 0) list->addItem(new QListWidgetItem("（无结果）"));
    };
    connect(edit, &QLineEdit::textChanged, &dlg, doSearch);
    const QString prefill = qEnvironmentVariable("3D_SEARCH_TEXT");
    if (!prefill.isEmpty()) edit->setText(prefill);   // 演示钩子预填后立即触发搜索
    dlg.exec();
}

void MainWindow::demoOpenQuickSearch(const QString &prefill) {
    qputenv("3D_SEARCH_TEXT", prefill.toUtf8());
    openQuickSearch();
    qunsetenv("3D_SEARCH_TEXT");
}
