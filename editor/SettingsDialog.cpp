#include "SettingsDialog.h"
#include "RendererFactory.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

SettingsDialog::SettingsDialog(const EngineSettings &cur, QWidget *parent) : QDialog(parent) {
    setWindowTitle("引擎设置 — Engine Configuration (v2.0)");
    setModal(false);
    resize(560, 620);
    applied = cur;

    auto *lay = new QVBoxLayout(this);

    auto *warn = new QLabel("⚠️ 警告：以下参数仅供高级引擎架构师调整，错误配置可能导致内存溢出、栈溢出或 3D 视口渲染崩溃！");
    warn->setStyleSheet("color:#ff2222; font-weight:bold; font-size:13px; padding:8px;"
                        "border:2px solid #ff2222; background:#3a1010;");
    warn->setWordWrap(true);
    lay->addWidget(warn);

    auto *tabs = new QTabWidget;
    lay->addWidget(tabs);

    // ---------- 渲染管线 ----------
    auto *rendWidget = new QWidget;
    auto *rendForm = new QFormLayout(rendWidget);

    // 渲染器 API 下拉（第十阶段：engine_config.ini [Renderer] API）
    QString curApi = projectPath.isEmpty() ? QString("OpenGL") : RendererFactory::readApi(projectPath);
    apiBox = new QComboBox;
    apiBox->addItems({"OpenGL", "Vulkan（预留）", "Direct3D（预留）"});
    if (curApi == "Vulkan") apiBox->setCurrentIndex(1);
    else if (curApi == "Direct3D") apiBox->setCurrentIndex(2);
    rendForm->addRow("渲染器 API (Renderer)：", apiBox);

    msaaBox = new QComboBox;
    msaaBox->addItems({"关闭", "2x", "4x", "8x"});
    msaaBox->setCurrentText(cur.msaa == 0 ? "关闭" : QString::number(cur.msaa) + "x");
    rendForm->addRow("多重采样抗锯齿 (MSAA)：", msaaBox);

    cascadesSlider = new QSlider(Qt::Horizontal);
    cascadesSlider->setRange(1, 4);
    cascadesSlider->setValue(cur.shadowCascades);
    auto *cascRow = new QHBoxLayout;
    cascRow->addWidget(cascadesSlider);
    auto *cascVal = new QLabel(QString::number(cur.shadowCascades));
    cascRow->addWidget(cascVal);
    connect(cascadesSlider, &QSlider::valueChanged, cascVal, [cascVal](int v){ cascVal->setText(QString::number(v)); });
    rendForm->addRow("阴影级联层数 (Shadow Cascades)：", cascRow);

    anisoBox = new QComboBox;
    anisoBox->addItems({"1x", "4x", "8x", "16x"});
    anisoBox->setCurrentText(QString::number(cur.aniso) + "x");
    rendForm->addRow("各向异性过滤 (Anisotropic)：", anisoBox);

    vsyncBox = new QCheckBox("启用垂直同步 (V-Sync)");
    vsyncBox->setChecked(cur.vsync);
    rendForm->addRow("同步模式：", vsyncBox);

    uiScaleSlider = new QSlider(Qt::Horizontal);
    uiScaleSlider->setRange(80, 150);
    uiScaleSlider->setValue(cur.uiScale);
    auto *uiRow = new QHBoxLayout;
    uiRow->addWidget(uiScaleSlider);
    auto *uiVal = new QLabel(QString("%1%").arg(cur.uiScale));
    uiRow->addWidget(uiVal);
    connect(uiScaleSlider, &QSlider::valueChanged, uiVal, [uiVal](int v){ uiVal->setText(QString("%1%").arg(v)); });
    rendForm->addRow("UI 大小：", uiRow);
    tabs->addTab(rendWidget, "渲染管线 (Rendering)");

    // ---------- 物理引擎 ----------
    auto *physWidget = new QWidget;
    auto *physForm = new QFormLayout(physWidget);
    hzBox = new QComboBox;
    hzBox->addItems({"60Hz", "120Hz", "240Hz"});
    hzBox->setCurrentText(QString::number(cur.stepHz) + "Hz");
    physForm->addRow("Bullet 物理步进频率：", hzBox);

    sleepEdit = new QLineEdit(QString::number(cur.sleepThreshold));
    physForm->addRow("刚体休眠阈值：", sleepEdit);

    auto *gravRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        gravSpin[i] = new QDoubleSpinBox;
        gravSpin[i]->setRange(-100.0, 100.0);
        gravSpin[i]->setDecimals(2);
        gravSpin[i]->setSingleStep(0.5);
        gravSpin[i]->setValue(cur.gravity[i]);
        gravRow->addWidget(gravSpin[i]);
    }
    physForm->addRow("全局重力向量 Gravity (X, Y, Z)：", gravRow);

    auto *hint = new QLabel("提示：Y 轴改为正数，物体会向天上掉；改为 0，物体悬浮。");
    hint->setStyleSheet("color:#889; font-size:12px;");
    physForm->addRow(hint);
    tabs->addTab(physWidget, "物理引擎 (Physics)");

    // ---------- 底层调试 ----------
    auto *dbgWidget = new QWidget;
    auto *dbgForm = new QFormLayout(dbgWidget);
    logBox = new QComboBox;
    logBox->addItems({"Trace", "Debug", "Info", "Error"});
    logBox->setCurrentText(cur.logLevel);
    dbgForm->addRow("调试日志级别：", logBox);

    vramEdit = new QLineEdit(QString::number(cur.vramRefreshMs));
    dbgForm->addRow("控制台显存占用刷新率 (ms)：", vramEdit);

    wireBox = new QCheckBox("开启物理线框调试模式 (Draw Debug Lines)");
    wireBox->setChecked(cur.wireframeDebug);
    dbgForm->addRow("碰撞体可视化：", wireBox);
    tabs->addTab(dbgWidget, "底层调试 (Debugger)");

    auto *btns = new QHBoxLayout;
    auto *btnApply = new QPushButton("应用 (Apply)");
    auto *btnCancel = new QPushButton("取消 (Cancel)");
    btnApply->setStyleSheet("font-weight:bold;");
    btns->addWidget(btnApply);
    btns->addWidget(btnCancel);
    lay->addLayout(btns);
    connect(btnApply, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::close);
}

void SettingsDialog::onApply() {
    applied.msaa = msaaBox->currentIndex() == 0 ? 0 : msaaBox->currentText().replace("x", "").toInt();
    applied.shadowCascades = cascadesSlider->value();
    applied.aniso = anisoBox->currentText().replace("x", "").toInt();
    applied.vsync = vsyncBox->isChecked();
    applied.uiScale = uiScaleSlider->value();
    applied.stepHz = hzBox->currentText().replace("Hz", "").toInt();
    applied.sleepThreshold = sleepEdit->text().toFloat();
    for (int i = 0; i < 3; ++i) applied.gravity[i] = float(gravSpin[i]->value());
    applied.logLevel = logBox->currentText();
    applied.vramRefreshMs = vramEdit->text().toInt();
    applied.wireframeDebug = wireBox->isChecked();

    // QSettings 持久化
    QSettings s("PhysicsEngine3D", "3DEditor");
    s.setValue("render/msaa", applied.msaa);
    s.setValue("render/cascades", applied.shadowCascades);
    s.setValue("render/aniso", applied.aniso);
    s.setValue("render/vsync", applied.vsync);
    s.setValue("render/uiScale", applied.uiScale);
    s.setValue("physics/stepHz", applied.stepHz);
    s.setValue("physics/sleepThreshold", applied.sleepThreshold);
    s.setValue("physics/gravityX", applied.gravity[0]);
    s.setValue("physics/gravityY", applied.gravity[1]);
    s.setValue("physics/gravityZ", applied.gravity[2]);
    s.setValue("debug/logLevel", applied.logLevel);
    s.setValue("debug/vramRefreshMs", applied.vramRefreshMs);
    s.setValue("debug/wireframe", applied.wireframeDebug);

    // 第十阶段：渲染器 API 下拉 -> 覆写项目根 engine_config.ini
    if (!projectPath.isEmpty() && apiBox) {
        QString api = apiBox->currentIndex() == 1 ? "Vulkan" : apiBox->currentIndex() == 2 ? "Direct3D" : "OpenGL";
        RendererFactory::writeApi(projectPath, api);
        qInfo() << "[Settings] 渲染器 API 已保存：" << api
                << (api == "OpenGL" ? "（OpenGL 后端全功能可用）" : "（预留后端：下次启动回退 OpenGL 并提示）");
    }

    qInfo() << "[Settings] 参数已持久化：stepHz=" << applied.stepHz
            << " gravity=(" << applied.gravity[0] << "," << applied.gravity[1] << "," << applied.gravity[2]
            << ") wireframe=" << applied.wireframeDebug;

    emit settingsApplied(applied);
    accept();
}
