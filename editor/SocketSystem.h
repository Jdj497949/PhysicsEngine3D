#pragma once
// ============ 插座（Socket）图运行时：解析 .socket JSON，遍历执行 ============
// 节点/连线/参数/Target 全部由 VNSocketEditor 保存的 JSON 驱动，不在 C++ 里硬编码模块。
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QVector3D>
#include <QFileDialog>
#include <QDir>
#include <vector>
#include <map>
#include "World.h"
#include "AudioService.h"
#include "GLViewport.h"

class SocketGraph {
public:
    struct SNode {
        int id = 0;
        QString typeId, name, category, target;
        QJsonObject params;
        // 端口：name -> type
        std::vector<std::pair<QString, QString>> inputs, outputs;
    };
    struct Link { int fromNode = 0, fromPort = 0, toNode = 0, toPort = 0; };

    std::vector<SNode> nodes;
    std::vector<Link> links;
    QString path;
    // 索引
    std::map<int, int> byId;

    bool load(const QString &p) {
        path = p;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        auto root = doc.object();
        nodes.clear(); links.clear(); byId.clear();
        for (auto v : root.value("nodes").toArray()) {
            auto j = v.toObject();
            SNode n;
            n.id = j.value("id").toInt();
            n.typeId = j.value("typeId").toString();
            n.name = j.value("name").toString();
            n.category = j.value("category").toString();
            n.target = j.value("target").toString();
            n.params = j.value("params").toObject();
            for (auto po : j.value("inputs").toArray()) {
                auto pj = po.toObject();
                n.inputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
            }
            for (auto po : j.value("outputs").toArray()) {
                auto pj = po.toObject();
                n.outputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
            }
            byId[n.id] = int(nodes.size());
            nodes.push_back(n);
        }
        for (auto v : root.value("connections").toArray()) {
            auto j = v.toObject();
            links.push_back({j.value("fromNode").toInt(), j.value("fromPort").toInt(),
                             j.value("toNode").toInt(), j.value("toPort").toInt()});
        }
        return !nodes.empty();
    }
};

// 插座运行时：挂到带 Socket 组件的物体上，随主循环 tick 执行
class SocketSystem {
public:
    struct Instance {
        int selfIdx = -1;              // 挂载 Socket 组件的物体索引
        SocketGraph graph;
        bool started = false;
        std::map<int, bool> keyHeld;   // 输入节点：上一帧按键状态（上升沿触发）
    };
    std::vector<Instance> instances;

    // 注入回调：ToggleDayNight 节点需要操作 World（MainWindow 注入，避免反向依赖）
    std::function<void()> dayNightToggle;
    // 注入回调：OnInteract 需要玩家位置（Player 物体或角色控制器视点，MainWindow 注入）
    std::function<QVector3D()> playerPosFn;
    // 注入回调：ForceExitVehicle 在无 Player 物体时强制角色控制器下车（MainWindow 注入）
    std::function<void(const QVector3D &)> playerExitFn;
    // 注入回调：OpenFileDialog 在运行态弹真实系统文件选择器（QFileDialog，模态嵌套事件循环）
    std::function<QString(const QString &)> openFileDialogFn;
    // 手电筒状态：真实存放在 World（GLViewport 渲染与日志共同读取），MainWindow 注入地址
    bool *flashlightFlag_ = nullptr;

    bool attach(int selfIdx, const QString &socketPath) {
        Instance inst;
        if (!inst.graph.load(socketPath)) {
            qInfo() << QString("[Socket] 错误：无法解析插座文件 %1").arg(socketPath);
            return false;
        }
        inst.selfIdx = selfIdx;
        instances.push_back(std::move(inst));
        auto &ins = instances.back();
        qInfo().noquote() << QString("[Socket] 插座已挂载：物体=%1 文件=%2 节点=%3 连线=%4")
            .arg(QString("object#%1").arg(selfIdx), QFileInfo(socketPath).fileName())
            .arg(ins.graph.nodes.size()).arg(ins.graph.links.size());
        return true;
    }

    void startAll(std::vector<SceneObject> &objects) {
        for (auto &ins : instances) {
            ins.started = true;
            // 从 Start（基础启动）节点触发执行链
            for (auto &n : ins.graph.nodes)
                if (n.name.startsWith("Start")) fireChain(ins, n, objects);
        }
    }

    void stopAll() { instances.clear(); }

    // 每帧：轮询输入事件节点 + 交互事件节点 + 执行持续动作 + 车门/交互动画
    void tick(std::vector<SceneObject> &objects, PhysicsWorld &physics,
              const std::function<bool(char)> &keyDown) {
        for (auto &ins : instances) {
            if (!ins.started) continue;
            for (auto &n : ins.graph.nodes) {
                // ---- OnInteract（交互事件）：玩家在半径内 + 交互键上升沿 → 触发执行链 ----
                if (n.name.startsWith("OnInteract")) {
                    char key = 'E';
                    if (!n.params.value("key").toString().isEmpty())
                        key = n.params.value("key").toString().toUpper().at(0).toLatin1();
                    float radius = float(n.params.value("radius").toDouble(3.0));
                    bool down = keyDown(char(tolower(key)));
                    bool was = ins.keyHeld[n.id];
                    ins.keyHeld[n.id] = down;
                    if (!down || was) continue;
                    int selfIdx = ins.selfIdx;
                    QVector3D selfPos(0, 0, 0);
                    if (selfIdx >= 0 && size_t(selfIdx) < objects.size())
                        selfPos = objects[size_t(selfIdx)].position;
                    QVector3D player = playerPosFn ? playerPosFn() : QVector3D(0, 1, 0);
                    float dist = (player - selfPos).length();
                    if (dist > radius) continue;   // 太远，不触发（没靠近按 E 没反应）
                    qInfo().noquote() << QString("[Socket] 交互事件 OnInteract：玩家距 %1 %2 米（半径 %3 米）按 E 触发 -> 执行连线逻辑")
                        .arg(selfIdx >= 0 ? objects[size_t(selfIdx)].name : "Self")
                        .arg(QString::number(dist, 'f', 2)).arg(radius);
                    fireChain(ins, n, objects, &physics);
                    continue;
                }
                // ---- OnTriggerEnter（进入触发器）：玩家进入 TriggerBox 包围盒上升沿 → 触发执行链 ----
                if (n.name.startsWith("OnTriggerEnter")) {
                    int ti = resolveTarget(ins, n, objects);
                    if (ti < 0) continue;
                    const SceneObject &box = objects[size_t(ti)];
                    float sx = float(n.params.value("sizeX").toDouble(3.0)),
                          sy = float(n.params.value("sizeY").toDouble(3.0)),
                          sz = float(n.params.value("sizeZ").toDouble(3.0));
                    // 优先读场景中 TriggerBox 组件的包围盒参数
                    for (const auto &c : box.components)
                        if (c.type == "TriggerBox") {
                            sx = float(c.params.value("sizeX").toDouble(sx));
                            sy = float(c.params.value("sizeY").toDouble(sy));
                            sz = float(c.params.value("sizeZ").toDouble(sz));
                        }
                    QVector3D player = playerPosFn ? playerPosFn() : QVector3D(0, 1, 0);
                    QVector3D d = player - box.position;
                    bool inside = qAbs(d.x()) <= sx * 0.5f && qAbs(d.y()) <= sy * 0.5f && qAbs(d.z()) <= sz * 0.5f;
                    bool was = ins.keyHeld[n.id];
                    ins.keyHeld[n.id] = inside;
                    if (inside && !was) {
                        qInfo().noquote() << QString("[Socket] 触发器事件 OnTriggerEnter：玩家进入 %1 包围盒（%2 × %3 × %4 米）-> 执行连线逻辑")
                            .arg(box.name).arg(sx).arg(sy).arg(sz);
                        fireChain(ins, n, objects, &physics);
                    }
                    continue;
                }
                if (!n.name.startsWith("OnKeyDown") && !n.name.startsWith("OnKeyHold")) continue;
                char key = keyCharOf(n);
                if (!key) continue;
                bool down = keyDown(char(tolower(key)));
                bool was = ins.keyHeld[n.id];
                if (n.name.startsWith("OnKeyDown") && down && !was) {
                    qInfo().noquote() << QString("[Socket] 输入事件：%1 触发 -> 执行连线逻辑").arg(n.name);
                    fireChain(ins, n, objects, &physics);
                }
                if (n.name.startsWith("OnKeyHold") && down)
                    fireChain(ins, n, objects, &physics);
                ins.keyHeld[n.id] = down;
            }
            // 持续移动类动作（每帧维持速度）
            tickContinuous(ins, objects, physics);
        }
        // 车门旋转动画：每帧推进（Ease-Out 缓动，duration 秒内转到目标角度）
        for (auto &da : doorAnims_) {
            if (da.elapsed >= da.duration || da.target < 0 || size_t(da.target) >= objects.size()) continue;
            da.elapsed += 0.016f;
            float t = qMin(da.elapsed / da.duration, 1.0f);
            float ease = 1.0f - (1.0f - t) * (1.0f - t);   // Ease-Out
            float yaw = da.baseYaw + ease * da.targetAngle;
            QVector3D ax = da.axis == 0 ? QVector3D(1, 0, 0) : (da.axis == 2 ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
            objects[size_t(da.target)].rotation = QQuaternion::fromAxisAndAngle(ax, yaw);
            if (da.elapsed >= da.duration && !da.doneLogged) {
                da.doneLogged = true;
                qInfo().noquote() << QString("[Socket] 旋转动画完成：%1 绕 %2 轴旋转 %3°（Ease-Out）")
                    .arg(objects[size_t(da.target)].name)
                    .arg(da.axis == 0 ? "X" : (da.axis == 2 ? "Z" : "Y")).arg(da.targetAngle);
            }
        }
    }

private:
    static char keyCharOf(const SocketGraph::SNode &n) {
        // 名称形如 "OnKeyDown 键W" / "OnKeyHold 键S"
        int p = n.name.indexOf("键");
        if (p < 0 || p + 1 >= n.name.size()) {
            // 可能是 "OnKeyDown W"
            QStringList parts = n.name.split(' ');
            for (const QString &s : parts) {
                if (s.length() == 1 && s[0].isLetter()) return s.toUpper().at(0).toLatin1();
            }
            return 0;
        }
        return n.name.at(p + 1).toUpper().toLatin1();
    }

    // 解析 Target：Self=挂载者 / Player / Scene.X
    int resolveTarget(const Instance &ins, const SocketGraph::SNode &n, std::vector<SceneObject> &objects) {
        QString t = n.target;
        if (t.isEmpty() || t == "Self") return ins.selfIdx;
        QString name = (t.startsWith("Scene.")) ? t.mid(6) : t;
        for (size_t i = 0; i < objects.size(); ++i)
            if (objects[i].name == name) return int(i);
        return -1;
    }

    void fireChain(Instance &ins, const SocketGraph::SNode &from, std::vector<SceneObject> &objects,
                   PhysicsWorld *physics = nullptr, int depth = 0) {
        if (depth > 32) return;  // 防环
        executeAction(ins, from, objects, physics);
        for (const auto &lk : ins.graph.links) {
            if (lk.fromNode != from.id) continue;
            // 只沿 Flow 出口推进
            QString outType = from.outputs.size() > size_t(lk.fromPort)
                ? from.outputs[size_t(lk.fromPort)].second : "Flow";
            if (outType != "Flow") continue;
            auto it = ins.graph.byId.find(lk.toNode);
            if (it == ins.graph.byId.end()) continue;
            fireChain(ins, ins.graph.nodes[size_t(it->second)], objects, physics, depth + 1);
        }
    }

    void executeAction(Instance &ins, const SocketGraph::SNode &n, std::vector<SceneObject> &objects, PhysicsWorld *physics) {
        const QString &nm = n.name;
        auto log = [&](const QString &s) { qInfo().noquote() << QString("[Socket] %1").arg(s); };

        if (nm.startsWith("Start")) { log(QString("Start（基础启动）已触发：%1").arg(nm)); return; }
        if (nm.contains("AddForce")) {
            if (!physics) return;
            int ti = resolveTarget(ins, n, objects);
            if (ti < 0) { log(QString("错误：AddForce 目标无效（%1）").arg(n.target)); return; }
            QVector3D f;
            if (nm.contains("向上")) f = QVector3D(0, float(n.params.value("force").toDouble(300.0)), 0);
            else if (nm.contains("向下")) f = QVector3D(0, -float(n.params.value("force").toDouble(300.0)), 0);
            else if (nm.contains("向前")) f = QVector3D(0, 0, -float(n.params.value("force").toDouble(300.0)));
            else if (nm.contains("向左")) f = QVector3D(-float(n.params.value("force").toDouble(300.0)), 0, 0);
            else if (nm.contains("向右")) f = QVector3D(float(n.params.value("force").toDouble(300.0)), 0, 0);
            else f = QVector3D(float(n.params.value("fx").toDouble(0.0)), float(n.params.value("fy").toDouble(0.0)),
                               float(n.params.value("fz").toDouble(0.0)));
            int bi = physics->bodyIndexOf(&objects[size_t(ti)]);
            if (bi < 0) { log(QString("错误：目标 %1 没有刚体，无法受力").arg(objects[size_t(ti)].name)); return; }
            physics->applyImpulse(bi, f * 0.016f);
            log(QString("AddForce -> 目标 %1 力度=(%2, %3, %4)")
                .arg(objects[size_t(ti)].name).arg(f.x()).arg(f.y()).arg(f.z()));
            return;
        }
        if (nm.contains("MoveForward") || nm.contains("MoveBackward") || nm.contains("MoveLeft") || nm.contains("MoveRight")
            || nm.contains("MoveUp") || nm.contains("MoveDown")) {
            continuous_.push_back({ins.graph.path, n.id, resolveTarget(ins, n, objects), nm});
            log(QString("持续移动已激活：%1 -> 目标 %2").arg(nm, n.target.isEmpty() ? "Self" : n.target));
            return;
        }
        if (nm.contains("PrintConsole")) { log(QString("节点输出：%1").arg(n.params.value("text").toString("(空)"))); return; }
        if (nm.contains("PlayAnimation")) {
            int ti = resolveTarget(ins, n, objects);
            if (ti < 0) { log(QString("错误：PlayAnimation 目标无效（%1）").arg(n.target)); return; }
            SceneObject &o = objects[size_t(ti)];
            QString animName = n.params.value("anim").toString(n.name);
            // 找动画：目标物体已挂 animPath 时按名字替换；否则找 assets/ 下同名 .vnanim
            QString animPath = o.animPath;
            QString cand = QString("assets/") + animName + ".vnanim";
            if (QFile(cand).exists()) animPath = cand;
            if (animPath.isEmpty() || !QFile(animPath).exists()) {
                log(QString("错误：%1 未找到动画 %2.vnanim").arg(o.name, animName));
                return;
            }
            GLViewport::playAnim(o, animPath);
            log(QString("PlayAnimation -> 目标 %1 播放 %2").arg(o.name, QFileInfo(animPath).fileName()));
            return;
        }
        if (nm.contains("PlayBGM") || nm.contains("PlayMusic")) {
            QString file = n.params.value("file").toString("test.wav");
            AudioService::instance().playBGM(file);
            log(QString("播放 BGM：%1（QMediaPlayer）").arg(file));
            return;
        }
        if (nm.contains("PlaySFX") || nm.contains("播放音效")) {
            QString file = n.params.value("file").toString("test.wav");
            AudioService::instance().playSFX(file);
            log(QString("播放音效：%1（QSoundEffect）").arg(file));
            return;
        }
        if (nm.contains("StopSound") || nm.contains("StopBGM")) { AudioService::instance().stopBGM(); log("BGM 已停止"); return; }
        if (nm.contains("PlaySound") || nm.contains("PlayAudio")) { log(QString("播放音效：%1").arg(nm)); return; }
        if (nm.contains("ToggleDayNight") || nm.contains("切换昼夜")) {
            // 第十阶段：昼夜循环切换（回调由 MainWindow 注入，操作 World 天气状态）
            if (dayNightToggle) dayNightToggle();
            else log(QString("切换昼夜：节点执行（World 回调未注入）"));
            return;
        }
        if (nm.contains("ShowText")) { log(QString("显示文本：%1").arg(n.params.value("text").toString(nm))); return; }
        // ---- 系统调用：打开文件选择器（真实 QFileDialog 模态弹窗）----
        if (nm.contains("OpenFileDialog") || nm.contains("文件选择器")) {
            QString filter = n.params.value("filter").toString("所有文件 (*)");
            QString picked;
            if (openFileDialogFn) picked = openFileDialogFn(filter);
            else picked = QFileDialog::getOpenFileName(nullptr, "选择文件（插座 OpenFileDialog 节点）",
                                                       QDir::homePath(), filter);
            if (picked.isEmpty()) log("OpenFileDialog：用户取消选择（FilePath = 空）");
            else log(QString("OpenFileDialog：真实系统文件选择器返回 -> FilePath = %1").arg(picked));
            lastStringOut_ = picked;
            return;
        }
        // ---- 交互动作：打开车门（绕 Y 轴旋转动画）----
        if (nm.contains("OpenDoor") || nm.contains("打开车门")) {
            int ti = resolveTarget(ins, n, objects);
            if (ti < 0) { log(QString("错误：打开车门 目标无效（%1）").arg(n.target)); return; }
            SceneObject &o = objects[size_t(ti)];
            float angle = float(n.params.value("angle").toDouble(70.0));
            float dur = float(n.params.value("duration").toDouble(1.0));
            float baseYaw = 0.0f;
            QMatrix4x4 m; m.rotate(o.rotation);
            baseYaw = float(std::atan2(m.column(0).z(), m.column(0).x()) * 180.0 / M_PI);
            DoorAnim da; da.target = ti; da.baseYaw = baseYaw; da.targetAngle = angle; da.duration = dur;
            da.graphPath = ins.graph.path; da.nodeId = n.id;
            // 防重复触发：同一节点同一目标只挂一次
            bool dup = false;
            for (auto &e : doorAnims_) if (e.target == ti && e.nodeId == n.id) dup = true;
            if (!dup) doorAnims_.push_back(da);
            qInfo().noquote() << QString("[Socket] 打开车门 -> 目标 %1 开始旋转 %2°（时长 %3 秒，Ease-Out）")
                .arg(o.name).arg(angle).arg(dur);
            return;
        }
        // ---- 交互动作：开关手电筒（真实切换 World 灯光状态）----
        if (nm.contains("ToggleFlashlight") || nm.contains("手电筒")) {
            flashlightToggle();
            log(QString("ToggleFlashlight：手电筒已%1（World 灯光状态真实切换）")
                .arg(flashlightState() ? "打开" : "关闭"));
            return;
        }
        // ---- 灯光节点：切换灯光 (Toggle Light)——按 F 亮/灭灯，走 World 灯光状态 ----
        if (nm.contains("ToggleLight") || nm.contains("切换灯光") || nm.contains("开关灯")) {
            flashlightToggle();
            log(QString("Toggle Light：灯光已%1（World 灯光状态真实切换，场景亮暗即时生效）")
                .arg(flashlightState() ? "打开 💡" : "关闭"));
            return;
        }
        // ---- 旋转物体 (Rotate Object)：绕指定轴旋转动画（axis=X/Y/Z，角度+时长）----
        if (nm.contains("RotateObject") || nm.contains("旋转物体")) {
            int ti = resolveTarget(ins, n, objects);
            if (ti < 0) { log(QString("错误：Rotate Object 目标无效（%1）").arg(n.target)); return; }
            SceneObject &o = objects[size_t(ti)];
            float angle = float(n.params.value("angle").toDouble(90.0));
            float dur = float(n.params.value("duration").toDouble(1.0));
            QString axisStr = n.params.value("axis").toString("Y").toUpper();
            int axis = axisStr.startsWith("X") ? 0 : (axisStr.startsWith("Z") ? 2 : 1);
            float base = 0.0f;
            QMatrix4x4 m; m.rotate(o.rotation);
            if (axis == 0) base = float(std::atan2(m.column(2).y(), m.column(2).z()) * 180.0 / M_PI);
            else if (axis == 1) base = float(std::atan2(m.column(0).z(), m.column(0).x()) * 180.0 / M_PI);
            else base = float(std::atan2(m.column(1).x(), m.column(1).y()) * 180.0 / M_PI);
            DoorAnim da; da.target = ti; da.axis = axis; da.baseYaw = base;
            da.targetAngle = angle; da.duration = dur;
            da.graphPath = ins.graph.path; da.nodeId = n.id;
            bool dup = false;
            for (auto &e : doorAnims_) if (e.target == ti && e.nodeId == n.id) dup = true;
            if (!dup) doorAnims_.push_back(da);
            log(QString("Rotate Object -> 目标 %1 绕 %2 轴旋转 %3°（时长 %4 秒，Ease-Out）")
                .arg(o.name).arg(axisStr).arg(angle).arg(dur));
            return;
        }
        // ---- 交互动作：强制下车（把玩家从车内移到车旁）----
        if (nm.contains("ForceExitVehicle") || nm.contains("强制下车")) {
            QVector3D carPos(0, 0, 0);
            int ti = resolveTarget(ins, n, objects);
            if (ti >= 0) carPos = objects[size_t(ti)].position;
            QJsonArray off = n.params.value("offset").toArray();
            QVector3D delta(float(off.at(0).toDouble(2.5)), float(off.at(1).toDouble(1.0)),
                            float(off.at(2).toDouble(0.0)));
            // 玩家：优先找名为 Player 的物体，否则用注入的玩家位置（角色控制器视点）
            int playerIdx = -1;
            for (size_t i = 0; i < objects.size(); ++i)
                if (objects[i].name == "Player") { playerIdx = int(i); break; }
            if (playerIdx >= 0) {
                if (physics) physics->teleportObject(objects[size_t(playerIdx)], carPos + delta);
                log(QString("ForceExitVehicle：Player 物体已移出车厢 -> (%1, %2, %3)")
                    .arg(QString::number((carPos + delta).x(), 'f', 1),
                         QString::number((carPos + delta).y(), 'f', 1),
                         QString::number((carPos + delta).z(), 'f', 1)));
            } else if (playerExitFn) {
                playerExitFn(carPos + delta);
                log(QString("ForceExitVehicle：角色控制器已强制下车 -> (%1, %2, %3)")
                    .arg(QString::number((carPos + delta).x(), 'f', 1),
                         QString::number((carPos + delta).y(), 'f', 1),
                         QString::number((carPos + delta).z(), 'f', 1)));
            } else {
                log("ForceExitVehicle：场景中没有 Player，玩家未在车内（跳过）");
            }
            return;
        }
        if (nm.startsWith("Delay") || nm.startsWith("Branch") || nm.startsWith("Sequence")
            || nm.contains("DoOnce") || nm.contains("Loop")) {
            log(QString("流程节点执行：%1").arg(nm));
            return;
        }
        log(QString("节点执行：%1（模拟）").arg(nm));
    }

    struct Continuous { QString graphPath; int nodeId; int target; QString name; };
    std::vector<Continuous> continuous_;

    // 车门旋转动画状态（OpenDoor / Rotate Object 节点入队，tick 每帧推进）
    struct DoorAnim {
        int target = -1;          // 车门物体索引
        QString graphPath; int nodeId = 0;
        int axis = 1;             // 旋转轴：0=X 1=Y 2=Z（OpenDoor 默认绕 Y）
        float baseYaw = 0.0f;     // 起始朝向角
        float targetAngle = 70.0f;
        float duration = 1.0f;
        float elapsed = 0.0f;
        bool doneLogged = false;
    };
    std::vector<DoorAnim> doorAnims_;

    // 手电筒状态：真实存放在 World（注入地址在公共区 flashlightFlag_）
    void flashlightToggle() {
        if (flashlightFlag_) *flashlightFlag_ = !*flashlightFlag_;
    }
    bool flashlightState() const { return flashlightFlag_ ? *flashlightFlag_ : false; }

    QString lastStringOut_;   // OpenFileDialog 节点的 FilePath 输出（供下游节点取值）


    void tickContinuous(Instance &ins, std::vector<SceneObject> &objects, PhysicsWorld &physics) {
        for (auto &c : continuous_) {
            if (c.graphPath != ins.graph.path || c.target < 0) continue;
            float speed = 2.0f;
            // 从节点参数读速度
            for (const auto &n : ins.graph.nodes)
                if (n.id == c.nodeId) speed = float(n.params.value("speed").toDouble(2.0));
            QVector3D v(0, 0, 0);
            if (c.name.contains("MoveForward")) v = QVector3D(0, 0, -speed);
            else if (c.name.contains("MoveBackward")) v = QVector3D(0, 0, speed);
            else if (c.name.contains("MoveLeft")) v = QVector3D(-speed, 0, 0);
            else if (c.name.contains("MoveRight")) v = QVector3D(speed, 0, 0);
            else if (c.name.contains("MoveUp")) v = QVector3D(0, speed, 0);
            else if (c.name.contains("MoveDown")) v = QVector3D(0, -speed, 0);
            int bi = physics.bodyIndexOf(&objects[size_t(c.target)]);
            if (bi >= 0) physics.setBodyVelocity(bi, v);
        }
    }
};
