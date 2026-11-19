#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

struct EngineSettings {
    int msaa = 4;              // 0/2/4/8
    int shadowCascades = 2;
    int aniso = 4;             // 1/4/8/16
    bool vsync = true;
    int uiScale = 100;         // %
    int stepHz = 60;           // 60/120/240
    float sleepThreshold = 0.008f;
    float gravity[3] = {0, -9.81f, 0};
    QString logLevel = "Info";
    int vramRefreshMs = 500;
    bool wireframeDebug = false;
};

// 独立设置窗口：渲染管线 / 物理引擎 / 底层调试
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const EngineSettings &cur, QWidget *parent = nullptr);
    EngineSettings applied;

    // 沙箱演示钩子用（正常使用不触发）
    void demoSetGravityY(float v) { gravSpin[1]->setValue(double(v)); }
    void demoApply() { onApply(); }
    // 第十阶段：设置窗口携带项目路径（engine_config.ini 覆写落点）
    void setProjectPath(const QString &p) { projectPath = p; }

signals:
    void settingsApplied(const EngineSettings &s);

private slots:
    void onApply();

private:
    QString projectPath;                       // 项目根（engine_config.ini）
    QComboBox *apiBox = nullptr;               // 渲染器 API 下拉
    QComboBox *msaaBox, *anisoBox, *hzBox, *logBox;
    QSlider *cascadesSlider, *uiScaleSlider;
    QCheckBox *vsyncBox, *wireBox;
    QLineEdit *sleepEdit, *vramEdit;
    QDoubleSpinBox *gravSpin[3];
};
