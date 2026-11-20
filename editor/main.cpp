// 3DEditor —— 主编辑器。由 3DLauncher 通过 QProcess 独立拉起，argv[1] 为项目路径。
#include <QApplication>
#include <QSurfaceFormat>
#include <QScreen>
#include <QTimer>
#include <QDir>
#include <QMessageBox>
#include "MainWindow.h"
#include "../common/CrashHandler.h"

static void grabFullscreen(const QString &shot) {
    if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
}

// 原主体（由 main 包装：崩溃捕获恢复点在 main 里）
static int runApp(QApplication &app, int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    app.setApplicationName("3DEditor");

    QString projectPath = (argc > 1) ? argv[1] : QDir::currentPath();
    if (!QDir(projectPath).exists()) {
        QMessageBox::critical(nullptr, "错误", QString("项目目录不存在：%1").arg(projectPath));
        return 1;
    }

    MainWindow w(projectPath);
    w.show();

    // 自动化演示钩子（沙箱截图用，正常使用不触发）
    QString shot = qEnvironmentVariable("3D_SHOT");
    if (!shot.isEmpty()) {
        QString mode = qEnvironmentVariable("3D_AUTO_MODE");
        qInfo() << "[Auto] mode =" << mode << "shot =" << shot;
        if (mode == "scene") {
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); w.createObject(1); w.createObject(2); });
            QTimer::singleShot(2200, &w, [&w, shot]{ w.grab().save(shot); });
            QTimer::singleShot(2600, &w, [&w]{ w.demoSelectObject(0); });
            QTimer::singleShot(3600, &w, [&w, shot]{ w.grab().save(QString(shot).replace(".png", "_gizmo.png")); });
        } else if (mode == "gizmo") {
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); w.createObject(1); w.createObject(2); });
            QTimer::singleShot(2600, &w, [&w]{ w.demoSelectObject(0); });
            QTimer::singleShot(3400, &w, [&w]{ w.demoGrabGizmo(0, 0); });
            QTimer::singleShot(4200, &w, [&w, shot]{ w.grab().save(shot); });
        } else if (mode == "settings") {
            // 设置对话框是独立顶层窗口，w.grab() 拍不到，必须抓全屏
            QTimer::singleShot(800, &w, [&w]{ w.openSettings(); });
            QTimer::singleShot(2000, &w, [&w]{ if (auto *d = w.findChild<SettingsDialog*>()) d->demoSetGravityY(9.81f); });
            QTimer::singleShot(2800, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(3600, &w, [&w]{ if (auto *d = w.findChild<SettingsDialog*>()) d->demoApply(); });
        } else if (mode == "fly") {
            QTimer::singleShot(600, &w, [&w]{ w.onPhysicsTest(); w.setGravityY(9.81f); });
            QTimer::singleShot(3600, &w, [&w, shot]{ w.grab().save(shot); });
        } else if (mode == "wire") {
            QTimer::singleShot(600, &w, [&w]{ w.onPhysicsTest(); w.setWireframe(true); });
            QTimer::singleShot(3000, &w, [&w, shot]{ w.grab().save(shot); });
        } else if (mode == "protop") {
            // 创世场景：平面+方块+圆柱+胶囊+摄像机+点光源+星空顶
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2); w.createObject(0); w.createObject(3); w.createObject(4);
                w.createObject(5); w.createObject(6);
            });
            QTimer::singleShot(3600, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4000, &w, [&w]{ w.demoRightDrag(90, -15); });  // 轻转视角看罗盘联动
            QTimer::singleShot(5600, &w, [shot]{ grabFullscreen(QString(shot).replace(".png", "_turn.png")); });
        } else if (mode == "components") {
            // 选中方块 -> 弹出 Add Component 下拉（组件注册表数据驱动）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); });
            QTimer::singleShot(2000, &w, [&w]{ w.demoSelectObject(0); });
            QTimer::singleShot(3000, &w, [&w]{ w.demoOpenAddComponentMenu(); });
            QTimer::singleShot(4200, &w, [shot]{ grabFullscreen(shot); });
        } else if (mode == "hierarchy") {
            // 父子层级：球体、圆柱挂到方块下（树状嵌套）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); w.createObject(1); w.createObject(3); });
            QTimer::singleShot(2600, &w, [&w]{ w.demoNestChild(1, 0); });
            QTimer::singleShot(3400, &w, [&w]{ w.demoNestChild(2, 0); });
            QTimer::singleShot(4400, &w, [shot]{ grabFullscreen(shot); });
        } else if (mode == "scriptslot") {
            // 挂载 Lua 脚本 -> 属性面板脚本槽位
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); });
            QTimer::singleShot(2000, &w, [&w]{ w.demoSelectObject(0); });
            QTimer::singleShot(2800, &w, [&w, projectPath]{
                w.demoAttachScript(0, projectPath + "/assets/spin.lua");
            });
            QTimer::singleShot(4200, &w, [shot]{ grabFullscreen(shot); });
        } else if (mode == "runscene") {
            // 直接运行当前项目场景（模板演示用）：先拍编辑器画面，再运行拍游戏画面
            QTimer::singleShot(1800, &w, [&w, shot]{ w.grab().save(QString(shot).replace(".png", "_edit.png")); });
            QTimer::singleShot(2600, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(7000, &w, [&w, shot]{ w.grab().save(shot); });
            QTimer::singleShot(12000, &w, [&w, shot]{ w.grab().save(QString(shot).replace(".png", "_later.png")); });
            QTimer::singleShot(13500, &app, []{ QApplication::quit(); });
        } else if (mode == "luarun") {
            // 点运行 -> Lua 生命周期生效，方块被脚本驱动旋转
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); w.createObject(2); });
            QTimer::singleShot(2000, &w, [&w, projectPath]{
                w.demoAttachScript(0, projectPath + "/assets/spin.lua");
            });
            QTimer::singleShot(3600, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(7600, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(11600, &w, [shot]{ grabFullscreen(QString(shot).replace(".png", "_later.png")); });
        } else if (mode == "flycam") {
            // 模拟按住 W 飞行 2 秒，终端打印相机移动日志
            QTimer::singleShot(800, &w, [&w]{ w.demoFlyKey('W', 2000); });
            QTimer::singleShot(4200, &w, [&w]{ w.demoFlyKey('D', 1200); });
            QTimer::singleShot(7000, &w, [shot]{ grabFullscreen(shot); });
        } else if (mode == "matdrop") {
            // 拖 .mat 材质到方块：红宝石材质真实应用（Blinn-Phong 高光）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(2); w.createObject(0); w.demoSetObjectPos(1, 0.4, 2.0, 0.4); });
            QTimer::singleShot(2200, &w, [&w, projectPath]{
                w.demoDropMat(1, projectPath + "/assets/红宝石.mat");
            });
            QTimer::singleShot(3400, &w, [&w]{ w.demoClickEmpty(0, 0); });   // 取消选中，露出红宝石本色
            QTimer::singleShot(4200, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5200, &app, []{ QApplication::quit(); });
        } else if (mode == "socketrun") {
            // 插座图运行：方块挂跳跃逻辑 -> 运行 -> 按 W 物理受力
            QTimer::singleShot(800, &w, [&w]{ w.createObject(2); w.createObject(0); w.demoSetObjectPos(1, 0.4, 2.0, 0.4); });
            QTimer::singleShot(2400, &w, [&w, projectPath]{
                w.demoAttachSocket(1, projectPath + "/assets/跳跃逻辑.socket");
            });
            QTimer::singleShot(3400, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(5600, &w, [&w]{ w.demoSocketKey('W', 700); });
            QTimer::singleShot(7600, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(8600, &w, [&w]{ w.demoSocketKey('W', 700); });
            QTimer::singleShot(11600, &w, [shot]{ grabFullscreen(QString(shot).replace(".png", "_later.png")); });
            QTimer::singleShot(12600, &app, []{ QApplication::quit(); });
        } else if (mode == "matmenu") {
            // 资产右键菜单（popup 独立窗口，只能全屏拍）
            QTimer::singleShot(1200, &w, [&w]{
                w.demoOpenAssetContextMenu(w.mapToGlobal(QPoint(280, 300)));
            });
            QTimer::singleShot(3200, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "inspector") {
            // 属性面板：挂 Script + Socket + 材质，验证槽位与材质信息显示
            QTimer::singleShot(800, &w, [&w, projectPath]{
                w.createObject(2);   // 地面（防方块坠出画面）
                w.createObject(0);
                w.demoSetObjectPos(1, 0.4, 2.0, 0.4);
                w.demoSelectObject(1);
            });
            QTimer::singleShot(2000, &w, [&w, projectPath]{ w.demoAttachScript(1, projectPath + "/assets/spin.lua"); });
            QTimer::singleShot(2800, &w, [&w, projectPath]{ w.demoAttachSocket(1, projectPath + "/assets/跳跃逻辑.socket"); });
            QTimer::singleShot(3600, &w, [&w, projectPath]{ w.demoDropMat(1, projectPath + "/assets/红宝石.mat"); });
            QTimer::singleShot(5200, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(6200, &app, []{ QApplication::quit(); });
        } else if (mode == "hud") {
            // 终极神图：3D 方块掉落 + 2D HUD（中文文本 + 按钮）覆盖上层，再真实点击 HUD 按钮
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);   // 地面
                w.createObject(0);   // 方块（掉落）
                w.demoSetObjectPos(1, 0.4, 2.0, 0.4);
            });
            QTimer::singleShot(2000, &w, [&w, projectPath]{
                w.demoAttachUICanvas(1, projectPath + "/assets/演示画布.uicanvas");
            });
            QTimer::singleShot(2600, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(4200, &w, [&w]{ w.demoHudClick(1); });   // 点「开始游戏」（变色联动 + Lua）
            QTimer::singleShot(5600, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(6600, &app, []{ QApplication::quit(); });
        } else if (mode == "textmesh") {
            // 3D 文本 + 字体下拉（属性面板）
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);          // 地面
                w.createObject(7);          // 3D 文本
                w.demoSetObjectPos(1, 0.0, 2.2, 0.0);
                w.demoSelectObject(1);
            });
            QTimer::singleShot(2400, &w, [&w, projectPath]{
                w.demoAttachScript(1, projectPath + "/assets/button_click.lua");
            });
            QTimer::singleShot(4200, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5200, &app, []{ QApplication::quit(); });
        } else if (mode == "gizmo2") {
            // 精准 Gizmo：绿轴(Y) 精准升降 —— 射线-平面拖拽，验证只动 Y
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);   // 地面
                w.createObject(0);   // 方块
                w.demoSetObjectPos(1, 0.4, 2.0, 0.4);
            });
            QTimer::singleShot(2400, &w, [&w]{ w.demoSelectObject(1); });
            QTimer::singleShot(3400, &w, [&w]{ w.demoGizmoDrag(1, 1, 1, 10, -70); });   // 抓绿轴往上拖
            QTimer::singleShot(4600, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5600, &app, []{ QApplication::quit(); });
        } else if (mode == "assetmenu2") {
            // 资产右键菜单（popup 独立窗口，只能全屏拍）
            QTimer::singleShot(1200, &w, [&w]{
                w.demoOpenAssetContextMenu(w.mapToGlobal(QPoint(280, 300)));
            });
            QTimer::singleShot(3200, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "modelimport") {
            // 第七阶段：外部模型导入 → Assimp 解析 → 弹「碰撞体」选择框（全屏拍弹窗）
            QTimer::singleShot(1200, &w, [&w, projectPath]{
                w.demoImportModelDialog(projectPath + "/assets/runner.gltf");
            });
            QTimer::singleShot(3400, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4400, &app, []{ QApplication::quit(); });
        } else if (mode == "modelrun") {
            // 第七阶段终极神图：带碰撞体模型 + 蒙皮跑步动画 + Lua 驱动位移
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);   // 地面
            });
            QTimer::singleShot(2000, &w, [&w, projectPath]{
                w.demoCreateModelObject(projectPath + "/assets/hut.vnmodel_col");
            });
            QTimer::singleShot(3000, &w, [&w, projectPath]{
                w.demoCreateModelObject(projectPath + "/assets/runner.vnmodel");
                w.demoSetObjectPos(2, 0.0, 1.0, -1.2);
            });
            QTimer::singleShot(4000, &w, [&w, projectPath]{
                w.demoPlayAnim(2, projectPath + "/assets/runner.vnanim");
                w.demoAttachScript(2, projectPath + "/assets/runner_move.lua");
            });
            QTimer::singleShot(4800, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(8000, &w, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(11000, &w, [shot]{ grabFullscreen(QString(shot).replace(".png", "_later.png")); });
            QTimer::singleShot(12000, &app, []{ QApplication::quit(); });
        } else if (mode == "about") {
            // 第八阶段：关于窗口（精美 QDialog 全屏截图）
            QTimer::singleShot(1200, &w, [&w]{ w.onAbout(); });
            QTimer::singleShot(3000, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "audiorun") {
            // 第八阶段：Lua Game.PlayBGM 真实播放验证
            QTimer::singleShot(800, &w, [&w]{ w.createObject(2); });
            QTimer::singleShot(2000, &w, [&w, projectPath]{
                w.demoAttachScript(0, projectPath + "/assets/bgm_test.lua");
                w.onRun();
            });
            QTimer::singleShot(6000, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(7200, &app, []{ QApplication::quit(); });
        } else if (mode == "plugins") {
            // 第九阶段：插件管理对话框（HD Graphics / Dynamic Nature 勾选）
            QTimer::singleShot(1200, &w, [&w]{ w.onPlugins(); });
            QTimer::singleShot(3200, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "wind2") {
            // 第九阶段：草地顶点风吹——两帧对比截图（u_Time + sin 位移）
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(9);   // 草地
                w.createObject(0);   // 方块（参照物，不动）
                w.demoSetWind(0, 0.25f, 2.8f);
            });
            QTimer::singleShot(6000, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(6800, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_b.png")); });
            QTimer::singleShot(7800, &app, []{ QApplication::quit(); });
        } else if (mode == "dragimport") {
            // 第九阶段：系统级文件拖入 -> Loading 弹窗 -> 导入完成资产树刷新
            QTimer::singleShot(1500, &w, [&w, projectPath]{
                w.demoDropImport({"/tmp/import_test_a.png", "/tmp/import_test_b.png", "/tmp/import_test_c.png"});
            });
            QTimer::singleShot(1750, &app, [shot]{ grabFullscreen(shot); });   // Loading 弹窗进行中（导入 1.5s~1.95s）
            QTimer::singleShot(4000, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_done.png")); });   // 完成后资产树
            QTimer::singleShot(5000, &app, []{ QApplication::quit(); });
        } else if (mode == "firework") {
            // 第十阶段：烟花粒子系统（GL_POINTS 密集爆发，两帧对比）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(9); w.demoAddFirework(); });
            QTimer::singleShot(5600, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(6100, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_b.png")); });
            QTimer::singleShot(7000, &app, []{ QApplication::quit(); });
        } else if (mode == "camfollow") {
            // 第十阶段：摄像机挂为子节点 -> 父物体移动 -> 视角平滑跟随（前后两帧）
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(0);   // 方块（被跟随者）
                w.createObject(5);   // 摄像机
                w.demoAttachCamToChild(1, 0);
            });
            QTimer::singleShot(3000, &app, [shot]{ grabFullscreen(shot); });   // 移动前
            QTimer::singleShot(3600, &w, [&w]{ w.demoSetObjectPos(0, 2.5, 1.0, 2.0); });   // 父物体移动
            QTimer::singleShot(5500, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_after.png")); });   // 移动后
            QTimer::singleShot(6500, &app, []{ QApplication::quit(); });
        } else if (mode == "crashtest") {
            // 故障注入演练：把各种坏数据依次喂给引擎，验证关键时刻不崩
            qInfo() << "[CrashTest] ========== 故障注入演练开始 ==========";
            QTimer::singleShot(800, &w, [&w]{ w.demoApplySky("/tmp/evil_sky_broken.sky"); });   // 坏1：畸形 JSON
            QTimer::singleShot(1600, &w, [&w]{ w.createObject(9); w.createObject(0); w.demoApplySky("/tmp/evil_sky_extreme.sky"); });   // 坏2：数值爆表 + 不存在的天空盒
            QTimer::singleShot(2400, &w, [&w]{ w.demoDropImport({"/tmp/evil_zero.png", "/tmp/evil_fake.png"}); });   // 坏3+4：0字节文件 + 文本冒充图片
            QTimer::singleShot(3200, &w, [&w, projectPath]{
                w.createObject(0); w.createObject(2);
                w.demoAttachScript(0, projectPath + "/assets/demo_broken.lua");   // 坏5：语法错误 Lua
            });
            QTimer::singleShot(4200, &w, [&w]{ w.onRun(); });                                   // 点运行：坏脚本进入生命周期
            QTimer::singleShot(6500, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(7500, &app, []{ qInfo() << "[CrashTest] ========== 演练结束，程序未崩溃 =========="; QApplication::quit(); });
        } else if (mode == "boom2") {
            // 崩溃捕获体系演示：真实段错误 → 日志导出 → 双层弹窗 → 自爆重启
            qInfo() << "[Boom2] 崩溃捕获演示：5 秒后制造真实段错误（解引用空指针）";
            QTimer::singleShot(5000, &app, []{
                qInfo() << "[Boom2] 引爆：向地址 0 写入！";
                volatile int *bomb = nullptr;
                *bomb = 42;   // 必崩：真实段错误信号 SIGSEGV
            });
        } else if (mode == "skyrun") {
            // 第十阶段：.sky 天空配置 + 昼夜切换（夜晚 -> 白天两帧）
            QTimer::singleShot(800, &w, [&w, projectPath]{
                w.createObject(9); w.createObject(0);
                w.demoApplySky(projectPath + "/assets/夜空.sky");
            });
            QTimer::singleShot(3000, &app, [shot]{ grabFullscreen(shot); });   // 黑夜
            QTimer::singleShot(3600, &w, [&w]{ w.demoToggleDayNight(); });     // 插座节点逻辑：切回白天
            QTimer::singleShot(4200, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_day.png")); });   // 白天
            QTimer::singleShot(5200, &app, []{ QApplication::quit(); });
        } else if (mode == "snap") {
            // 第十一阶段：网格吸附 + 多选对齐/分布（属性面板整数坐标入画）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); w.createObject(1); w.createObject(3); });
            QTimer::singleShot(1800, &w, [&w]{ w.demoSnapOn(1.0f, 15.0f); });   // 1 米 / 15 度
            QTimer::singleShot(2400, &w, [&w]{ w.demoSelectObject(0); });
            // 沿 X 轴拖拽：吸附到整数米
            QTimer::singleShot(3000, &w, [&w]{ w.demoGizmoDrag(0, 0, int(ToolMode::Translate), 300, 0); });
            QTimer::singleShot(3400, &w, [&w]{ w.demoGizmoDrag(1, 0, int(ToolMode::Translate), 600, 0); });
            QTimer::singleShot(3800, &w, [&w, shot]{ grabFullscreen(shot); });   // 属性面板整数坐标
            // Shift 多选 3 个 → 顶端对齐 → X 轴等距分布
            QTimer::singleShot(4600, &w, [&w]{ w.demoMultiSelect(0); w.demoMultiSelect(1); w.demoMultiSelect(2); });
            QTimer::singleShot(5200, &w, [&w]{ w.demoAlignTop(); });
            QTimer::singleShot(5700, &w, [&w]{ w.demoDistributeX(); });
            QTimer::singleShot(6600, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_align.png")); });
            QTimer::singleShot(7600, &app, []{ QApplication::quit(); });
        } else if (mode == "search") {
            // 第十一阶段：Ctrl+P 全局搜索（资产 / 物体 / Lua 内容）
            QTimer::singleShot(800, &w, [&w]{ w.createObject(0); });
            QTimer::singleShot(1800, &w, [&w]{ w.demoOpenQuickSearch("lua"); });
            QTimer::singleShot(3600, &app, [shot]{ grabFullscreen(shot); });    // 搜索结果入画
            QTimer::singleShot(4600, &app, []{ QApplication::quit(); });
        } else if (mode == "action3d") {
            // 第十二阶段：3D 动作模板（CharacterController：走/跑/跳/蹲，真实物理刚体）
            // 模板物体（大地板+Player+相机+灯）由 director.json spawn 装载
            QTimer::singleShot(1500, &w, [&w]{ w.onRun(); });                    // 控制台打印 [Controller] Walk Speed: 5.0
            QTimer::singleShot(2100, &w, [&w]{ w.demoFlyKeyInt(Qt::Key_W, 1800); });   // 向前走（相机跟随）
            QTimer::singleShot(2500, &app, [shot]{ grabFullscreen(shot); });     // 走动 + 控制台日志
            QTimer::singleShot(4100, &w, [&w]{ w.demoFlyKeyInt(Qt::Key_Space, 220); }); // 跳跃
            QTimer::singleShot(4300, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_jump.png")); });   // 半空瞬间
            QTimer::singleShot(5300, &w, [&w]{ w.demoFlyKeyInt(Qt::Key_Control, 2400); }); // 蹲下（碰撞体减半+相机降低）
            QTimer::singleShot(5900, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_crouch.png")); });
            QTimer::singleShot(6600, &w, [&w]{ w.demoRightDrag(180, 40); });          // 鼠标滑动转视角
            QTimer::singleShot(7200, &app, [shot]{ grabFullscreen(QString(shot).replace(".png", "_look.png")); });
            QTimer::singleShot(8200, &app, []{ QApplication::quit(); });
        } else if (mode == "fontmenu") {
            // 字体管理：导入真实 .otf 字体 -> 应用到 3D 文本 -> 展开字体管理菜单
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);   // 地面
                w.createObject(7);   // 3D 文本（中文）
                w.demoSetObjectPos(1, 0.0, 2.4, 0.0);
                w.demoSelectObject(1);
            });
            QTimer::singleShot(2000, &w, [&w, projectPath]{ w.demoImportFont(projectPath + "/assets/演示字体.otf"); });
            QTimer::singleShot(2800, &w, [&w]{ w.demoApplyFontToSelected(); });
            QTimer::singleShot(3600, &w, [&w]{ w.demoOpenFontMenu(); });
            QTimer::singleShot(4400, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5400, &app, []{ QApplication::quit(); });
        } else if (mode == "interact") {
            // 交互动作：车(父)+车门(子) 挂 OnInteract 蓝图 -> 运行 -> 玩家靠近按 E -> 车门旋转动画
            QTimer::singleShot(800, &w, [&w]{
                w.createObject(2);            // 地面
                w.createObject(0);            // 车身（蓝）
                w.demoSetObjectPos(1, 0.0, 0.6, -2.0);
                w.createObject(0);            // 车门（黄，稍后挂为子节点）
                w.demoSetObjectPos(2, 0.9, 0.6, -2.0);
                w.createObject(0);            // 玩家（绿）
                w.demoSetObjectPos(3, 1.8, 0.6, -0.8);
                w.demoNestChild(2, 1);        // 车门挂为车身子节点
            });
            QTimer::singleShot(2400, &w, [&w, projectPath]{
                w.demoNamePlayer(3);
                w.demoAttachSocket(2, projectPath + "/assets/车门交互.socket");
            });
            QTimer::singleShot(3600, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(5600, &w, [&w]{ w.demoSocketKey('E', 300); });   // 靠近车辆按 E
            QTimer::singleShot(6100, &w, [&w]{ w.demoSocketKey('E', 300); });   // 上升沿需要松开，再按一次
            QTimer::singleShot(6900, &app, [shot]{ grabFullscreen(shot); });    // 车门旋转中 + 控制台日志
            QTimer::singleShot(8600, &app, []{ QApplication::quit(); });
        } else if (mode == "filedlg") {
            // 系统调用：Start -> OpenFileDialog -> ShowImage，运行时弹真实系统文件选择器
            QTimer::singleShot(800, &w, [&w, projectPath]{
                w.createObject(2);
                w.createObject(0);
                w.demoAttachSocket(1, projectPath + "/assets/打开图片.socket");
            });
            QTimer::singleShot(2800, &w, [&w]{ w.onRun(); });
            QTimer::singleShot(4600, &app, [shot]{ grabFullscreen(shot); });    // 文件选择器弹出中
            QTimer::singleShot(6600, &app, []{ QApplication::quit(); });
        } else if (mode == "dock3d") {
            // 第十三阶段：控制台拖出成独立窗口（真实 setFloating 管线，最多 3 个独立面板）
            QTimer::singleShot(1200, &w, [&w]{ w.demoDetachConsole(); });
            QTimer::singleShot(2400, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(3200, &app, []{ QApplication::quit(); });
        } else if (mode == "modelmenu") {
            // 第十三阶段：菜单栏【模型管理】下拉（快速放置 4 种基础几何体）
            QTimer::singleShot(900, &w, [&w]{
                w.createObject(2);   // 地面（衬托新物体）
            });
            QTimer::singleShot(2000, &w, [&w]{ w.demoOpenModelMenu(); });
            QTimer::singleShot(3200, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "compsmenu") {
            // 第十三阶段：添加组件下拉（点光源/聚光灯/3D 空间音效/触发器 全部入列）
            QTimer::singleShot(900, &w, [&w]{
                w.createObject(2);
                w.createObject(0);
                w.demoSetObjectPos(1, 0.4, 2.0, 0.4);
                w.demoSelectObject(1);
            });
            QTimer::singleShot(2400, &w, [&w]{ w.demoOpenAddComponentMenu(); });
            QTimer::singleShot(3600, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4600, &app, []{ QApplication::quit(); });
        } else if (mode == "pointlight") {
            // 第十三阶段：方块 + 点光源组件 -> 夜晚场景亮起暖黄光晕（原生组件，无需插座）
            QTimer::singleShot(900, &w, [&w, projectPath]{
                w.createObject(2);   // 地面
                w.createObject(0);   // 方块（灯泡座）
                w.demoSetObjectPos(1, 0.0, 1.2, 0.0);
                w.demoApplySky(projectPath + "/assets/夜空.sky");
                w.demoAttachPointLight(1, "#ffcf9e", 3.2, 2.8);   // 真实挂 PointLight 组件
                w.demoSelectObject(1);
            });
            QTimer::singleShot(2600, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(3600, &app, []{ QApplication::quit(); });
        } else if (mode == "buildpanel") {
            // 第十三阶段：构建 -> 打包发布面板（平台/名称/包名/头像/动图/指针/加密）
            QTimer::singleShot(1200, &w, [&w]{ w.demoOpenBuildPanel(); });
            QTimer::singleShot(2400, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(3400, &app, []{ QApplication::quit(); });
        } else if (mode == "buildencrypt") {
            // 第十三阶段：勾选【加密资源】-> 下方弹出加密密钥输入框
            QTimer::singleShot(1200, &w, [&w]{ w.demoOpenBuildPanel(); });
            QTimer::singleShot(2200, &w, [&w]{ w.demoBuildCheckEncrypt(); });
            QTimer::singleShot(3200, &app, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        } else if (mode == "buildrun") {
            // 第十三阶段+稳定性测试：真实打包 -> build_config.json + 平台工具链 + AES-256 加密日志
            // 环境变量可覆盖：3D_BUILD_PLATFORM（Windows/Linux）、3D_BUILD_ICON（应用头像路径）
            const QString plat = qEnvironmentVariable("3D_BUILD_PLATFORM", "Windows");
            const QString icon = qEnvironmentVariable("3D_BUILD_ICON");
            QTimer::singleShot(1200, &w, [&w, plat, icon]{
                w.demoOpenBuildPanel();
                w.demoBuildFill(plat, "My Horror Game", "com.lingguang.horror");
                if (!icon.isEmpty()) w.demoBuildSetAvatar(icon);
            });
            QTimer::singleShot(2300, &w, [&w]{ w.demoBuildCheckEncrypt(); });
            QTimer::singleShot(3100, &w, [&w]{ w.demoStartBuild(); });
            QTimer::singleShot(14000, &app, [shot]{ grabFullscreen(shot); });   // 控制台编译+加密+堡垒日志
            QTimer::singleShot(16000, &app, []{ QApplication::quit(); });
        } else {
            QTimer::singleShot(1500, &w, [&w, shot]{ w.grab().save(shot); });
        }
    }
    return app.exec();
}

// 崩溃捕获主入口：安装拦截器 → 恢复点 → 崩溃则弹双层弹窗并自爆重启
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QString logDir = (argc > 1 && QDir(argv[1]).exists())
        ? QString(argv[1]) + "/logs"      // 项目路径/logs/crash_xxx.txt（死命令 2）
        : QCoreApplication::applicationDirPath() + "/../logs";
    CrashHandler::install("3DEditor", logDir,
                          QCoreApplication::applicationDirPath() + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {   // true = 刚从崩溃中体面回归
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
