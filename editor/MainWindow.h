#pragma once
#include <QMainWindow>
#include "ParticleSystem.h"
#include <QTabWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QAction>
#include <QJsonObject>
#include <QWidget>
#include "World.h"
#include "PluginSystem.h"
#include "SettingsDialog.h"
#include "GLViewport.h"
#include "LuaSystem.h"
#include "SocketSystem.h"
#include "BuildDialog.h"

class GLViewport;
class QComboBox;
class QVBoxLayout;
class QPlainTextEdit;

// 资产列表（接受 .lua 拖拽 + 外部模型 .obj/.fbx/.gltf/.glb 导入 + .vnmodel 入场景）
class AssetList : public QListWidget {
    Q_OBJECT
public:
    explicit AssetList(QWidget *p = nullptr) : QListWidget(p) { setDragEnabled(true); setAcceptDrops(true); }
    QString assetsDir;                          // 项目 assets 目录（dropEvent 导入目标）
    void demoDropEvent(QDropEvent *e) { dropEvent(e); }   // 演示钩子直调导入管线（绕过 Qt 拖放事件通道）
protected:
    void startDrag(Qt::DropActions) override;   // 提供 .lua 文件路径拖拽数据
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
signals:
    void luaDropped(const QString &path);
    void externalModelDropped(const QString &path);    // 外部模型（.obj/.fbx/.gltf/.glb）
    void vnModelDropped(const QString &path);          // .vnmodel / .vnmodel_col 拖入场景
    void filesImported(const QStringList &paths);      // 系统级文件拖入并导入完成
};

// 属性面板（组件列表 + 脚本/插座/材质 拖拽槽位）
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget *parent = nullptr);
    QVBoxLayout *lay = nullptr;
    QLabel *nameLabel = nullptr;
    QWidget *componentBox = nullptr;          // 组件参数区
    class QPushButton *addComponentBtn = nullptr;
    class QLineEdit *scriptSlot = nullptr;    // Lua 脚本槽位
    class QLineEdit *socketSlot = nullptr;    // 插座槽位
    class QLineEdit *canvasSlot = nullptr;    // UI 画布槽位（.uicanvas）
    class QLineEdit *animSlot = nullptr;      // 动画槽位（.vnanim，模型骨骼动画）
    class QLabel *matLabel = nullptr;         // 材质信息
    bool event(QEvent *e) override;           // 接收拖拽
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;   // 按 .lua/.socket/.uicanvas/.vnanim 路由
signals:
    void luaDropped(const QString &path);
    void socketDropped(const QString &path);
    void canvasDropped(const QString &path);
    void animDropped(const QString &path);
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &projectPath, QWidget *parent = nullptr);
    static QPlainTextEdit *consoleSink;        // qInstallMessageHandler 落点（全局钩子可访问）

public slots:
    void onRun();
    void onPhysicsTest();
    void onHierarchySelect();
    void onInspectorChanged();
    void tick();
    void refreshAssets();
    void rebuildHierarchy();
    void createObject(int meshType);            // 0..6 共7种
    void onSettings() { openSettings(); }
    void onAbout();
    void onAuthorPage();
    void onContributors();
    void onNewScene();
    void onSaveScene();
    void onViewportSelection(int idx);
    void onHierarchyContextMenu(const QPoint &pos);
    void openSettings();
    // 演示钩子：走真实交互管线
    void demoSelectObject(int idx) { sceneView->clickOnObject(idx); }
    void demoGrabGizmo(int idx, int axis) { sceneView->clickGizmoHandle(idx, axis, world.tool); }
    void setGravityY(float y) { settings.gravity[1] = y; applySettings(settings); }
    void setWireframe(bool on) { settings.wireframeDebug = on; applySettings(settings); }
    // 新演示钩子
    void demoSetTool(int mode) { sceneView->setToolMode(ToolMode(mode)); }
    void demoAttachScript(int idx, const QString &path);          // 挂 Lua 脚本
    void demoOpenAddComponentMenu();                              // 弹出添加组件下拉
    void demoNestChild(int childIdx, int parentIdx);              // 父子层级
    void demoNewLua(const QString &name);                         // 资产右键新建脚本
    void demoNewMat(const QString &name);                         // 资产右键新建材质球
    void demoNewSocket(const QString &name);                      // 资产右键新建插座脚本
    void demoOpenAsset(const QString &fileName);                  // 双击资产（QProcess 拉起编辑器）
    void demoDropMat(int objIdx, const QString &matPath);         // 拖 .mat 到物体
    void demoAttachSocket(int idx, const QString &path);          // 挂插座文件
    void demoSocketKey(char key, int ms);                         // 模拟游戏按键（插座输入）
    void demoOpenAssetContextMenu(const QPoint &globalPos);       // 资产右键菜单（真实菜单）
    void demoNamePlayer(int idx) {
        if (idx < 0 || size_t(idx) >= world.objects.size()) return;
        world.objects[size_t(idx)].name = "Player";
        rebuildHierarchy();
        qInfo() << "[Demo] 物体已命名为 Player（OnInteract 交互检测目标）";
    }
    void demoFlyKey(char key, int ms) { sceneView->demoKeyFly(int(key), ms); }
    void demoFlyKeyInt(int key, int ms) { sceneView->demoKeyFly(key, ms); }   // 支持方向键/修饰键
    void demoRightDrag(int dx, int dy) { sceneView->demoRightDrag(dx, dy); }
    void demoClickEmpty(int x, int y) { Q_UNUSED(x); Q_UNUSED(y); sceneView->clearHighlight(); }
    void demoSetObjectPos(int idx, double x, double y, double z);
    void demoAttachUICanvas(int idx, const QString &path);        // 挂 UI 画布（HUD 跨维度）
    void demoHudClick(int idx) { gameView->demoClickHUD(idx); }  // 演示点击 HUD 按钮（运行页签真实点击）
    void demoGizmoDrag(int idx, int axis, int mode, int dx, int dy) { sceneView->demoGizmoDrag(idx, axis, ToolMode(mode), dx, dy); }
    void demoNewScene3D(const QString &name);                     // 新建 .scene3d
    void demoNewScene2D(const QString &name);                     // 新建 .scene2d
    void demoNewUICanvas(const QString &name);                    // 新建 .uicanvas
    // 第七阶段：模型管线演示钩子
    void demoImportModelDialog(const QString &src);               // 外部模型导入 → 弹转换类型选择框
    void demoCreateModelObject(const QString &vnmodelPath);       // .vnmodel / .vnmodel_col 创建场景物体
    void demoPlayAnim(int idx, const QString &animPath);          // 挂动画并播放

    // 第九/十阶段：插件、粒子、天空、相机挂载、拖拽导入、风力演示钩子
    void onPlugins();                                             // 插件管理菜单（QDialog 勾选 + JSON 落盘）
    void demoOpenPlugins() { onPlugins(); }
    void demoAddParticles();                                      // 新建粒子系统（烟花预设）
    void demoAddFirework() { demoAddParticles(); }
    void demoSetWind(int objIdx, float strength, float freq);     // 演示钩：物体级风力参数
    void demoAttachCamToChild(int camIdx, int parentIdx);         // 摄像机挂为子节点（平滑跟随）
    void demoApplySky(const QString &path);                       // 应用 .sky（真实管线同拖入）
    void demoToggleDayNight() { world.toggleDayNight(); }
    void demoDropImport(const QStringList &urls);                 // 走真实 dropEvent 管线导入文件

    // 第十一阶段：网格吸附 / 多选对齐 / 全局搜索演示钩子
    void demoSnapOn(float posStep, float rotStep) {
        sceneView->snapOn = true; gameView->snapOn = true;
        sceneView->snapPosStep = posStep; sceneView->snapRotStep = rotStep;
        qInfo() << QString("[Snap] 演示钩子：吸附开启（位置 %1 米 / 旋转 %2 度）").arg(posStep).arg(rotStep);
    }
    void demoMultiSelect(int idx) { sceneView->selectMulti(idx, true); }
    void demoAlignTop() { sceneView->alignTop(); }
    void demoDistributeX() { sceneView->distributeX(); }
    void demoOpenQuickSearch(const QString &prefill);

    // ============ 字体管理（3D 编辑器）============
    void onImportFont();                          // 导入 .ttf/.otf：QFontDatabase 入库 + 3D Text 可用
    void demoImportFont(const QString &path);     // 演示钩子：真实走同一条导入管线
    void demoApplyFontToSelected();               // 把最近导入字体应用到选中的 3D 文本
    void demoOpenFontMenu();                      // 弹出菜单栏【字体管理】（真实 QMenu popup）
    QStringList importedFonts() const { return importedFontFamilies_; }   // 本会话导入的字体族
    QString lastFont() const { return lastImportedFont_; }                    // 最近导入的字体族名

    // ============ 第十二阶段：Project.vnproj + CharacterController ============
    void loadProjectConfig();        // 启动时读取 Project.vnproj 高级设置（渲染管线/物理步进率/重力/字体）
    void charCtrlStart();            // onRun 时启动角色控制器（读取组件参数并接管视角）
    void charCtrlTick(float dt);     // 每帧驱动：WASD/Shift 奔跑/空格跳跃/Ctrl 下蹲/鼠标视角

    // ============ 第十三阶段：控制台悬浮 / 模型管理 / 打包发布 ============
    void demoDetachConsole();                     // 控制台拖出成独立窗口（真实 topLevelChanged 管线）
    void demoOpenModelMenu();                     // 弹出菜单栏【模型管理】（真实 QMenu popup）
    void demoOpenBuildPanel() { openBuildPanel(); }          // 打包发布面板（QDialog）
    void demoBuildCheckEncrypt() { if (buildDialog) buildDialog->demoCheckEncrypt(); }   // 勾选加密
    void demoBuildFill(const QString &p, const QString &n, const QString &b) { if (buildDialog) buildDialog->demoFill(p, n, b); }
    void demoBuildSetAvatar(const QString &p) { if (buildDialog) buildDialog->demoSetAvatar(p); }   // 填应用头像
    void demoStartBuild() { if (buildDialog) buildDialog->demoStartBuild(); }   // 真实打包（build_config.json + QProcess + AES）
    void demoAttachPointLight(int idx, const QString &color, double radius, double intensity) {
        if (idx < 0 || size_t(idx) >= world.objects.size()) return;
        SceneObject &o = world.objects[size_t(idx)];
        o.components.push_back({"PointLight", QJsonObject{{"color", color}, {"radius", radius},
                                                          {"intensity", intensity}}});
        syncInspectorFrom(idx);
        qInfo().noquote() << QString("[Component] %1 已添加组件：PointLight（颜色 %2 / 半径 %3 米 / 强度 %4）")
            .arg(o.name, color).arg(radius).arg(intensity);
    }

private:
    // 角色控制器运行态
    int charCtrlIdx = -1;
    bool charCrouch = false;
    bool charSpaceWas = false;
    bool charRunLogged = false;
    float ccWalk = 5.0f, ccJump = 6.0f, ccGrav = 1.0f;
    float charCrouchRestore = 0.0f;

private:
    void loadUICanvasToView(const QString &path);     // 解析 .uicanvas -> HUD
    void loadSpawnFromDirector();                     // 读 director.json spawn 数组创建模板默认物体
    void addLuaWatchers();                          // 给所有 .lua 挂内容监听
    void onScriptChanged(const QString &path);      // VNCodeStudio 保存后自动刷新
    void openQuickSearch();                         // Ctrl+P 全局搜索：资产/物体/Lua 内容
private slots:
    void onAddComponent(QAction *act);
    void onCameraMoved(const QString &log);
    void onAssetDoubleClicked(QListWidgetItem *item);
    void onHudButtonClicked(int idx, QString name, QString script);   // HUD 按钮点击（脚本/3D联动）

private:
    QString projectPath;
    World world;
    LuaSystem lua;
    QTabWidget *tabs;
    GLViewport *sceneView;
    GLViewport *gameView;
    AssetList *assetList;
    QTreeWidget *hierarchy;
    QDoubleSpinBox *spinPos[3], *spinRot[3], *spinScale[3];
    InspectorPanel *inspector = nullptr;
    QLabel *inspName;
    QFileSystemWatcher *watcher;
    QFileSystemWatcher *scriptWatcher = nullptr;    // .lua 内容监听
    QTimer *loop;
    QAction *actRun;
    QLabel *statusLabel = nullptr;
    EngineSettings settings;
    SettingsDialog *settingsDialog = nullptr;
    int selected = -1;
    bool running = false;
    bool suppressInspector = false;
    float accumulator = 0.0f;
    int fpsFrames = 0;
    qint64 fpsLastMs = 0;
    QJsonObject componentRegistry;             // 组件注册表（JSON 数据驱动，非硬编码）
    SocketSystem socketSys;                    // 插座图运行时
    QPlainTextEdit *console = nullptr;         // 底部控制台（Lua/物理/插座 全量日志）
    QDockWidget *consoleDock = nullptr;        // 控制台停靠面板（可拖出独立窗口，上限 3）
    class QComboBox *fontCombo = nullptr;      // Text3D 字体下拉
    QString default3DFont;                     // 「字体管理」设置的 3D 文本默认字体族
    QStringList importedFontFamilies_;         // 本会话导入的字体族
    QString lastImportedFont_;                 // 最近导入的字体族名
    class QLineEdit *textEdit = nullptr;       // Text3D 文本内容
    PluginState pluginState;                   // 插件状态（plugins.json 持久化）
    std::unique_ptr<class ParticleSystem> particleSys;   // 粒子系统（烟花）
    // 粒子参数面板控件（Inspector 粒子参数组）
    class QSlider *pEmitSlider = nullptr;
    class QSlider *pGravSlider = nullptr;
    class QSlider *pSpeedSlider = nullptr;
    class QSlider *pLifeSlider = nullptr;
    class QPushButton *pColorStart = nullptr;
    class QPushButton *pColorEnd = nullptr;
    QWidget *particlePanel = nullptr;          // 参数组容器（无粒子时隐藏）
    BuildDialog *buildDialog = nullptr;        // 打包发布面板（构建菜单）
    void setupConsole();
    void openBuildPanel();                         // 构建(Build) -> 打包发布（QDialog 表单）
    void loadComponentRegistry();
    void rebuildInspector(int idx);
    void syncInspectorFrom(int idx);
    void applySettings(const EngineSettings &s);
    void loadSettings();
};
