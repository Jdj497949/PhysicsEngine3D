#pragma once
#include <QString>
#include <QVector3D>
#include <QQuaternion>
#include <QMatrix4x4>
#include <QJsonObject>
#include <vector>
#include <map>
#include <memory>
#include <cstdlib>
#include <btBulletDynamicsCommon.h>
#include "MaterialSystem.h"

// ============ Bullet 调试线框渲染器（绿色碰撞线框） ============
class PhysicsDebugDrawer : public btIDebugDraw {
public:
    std::vector<float> lines;   // 成对的 xyz
    int mode = DBG_DrawWireframe;
    void drawLine(const btVector3 &a, const btVector3 &b, const btVector3 &) override {
        lines.push_back(a.x()); lines.push_back(a.y()); lines.push_back(a.z());
        lines.push_back(b.x()); lines.push_back(b.y()); lines.push_back(b.z());
    }
    void drawContactPoint(const btVector3 &, const btVector3 &, btScalar, int, const btVector3 &) override {}
    void reportErrorWarning(const char *) override {}
    void draw3dText(const btVector3 &, const char *) override {}
    void setDebugMode(int m) override { mode = m; }
    int getDebugMode() const override { return mode; }
};

// ============ 场景物体 ============
enum class MeshType { Cube = 0, Sphere = 1, Plane = 2, Cylinder = 3, Capsule = 4, Camera = 5, Light = 6, Text3D = 7, ModelMesh = 8, Grass = 9 };
static constexpr int MESH_TYPE_COUNT = 10;

// 组件实例：类型来自 JSON 组件注册表，参数原样存 JSON
struct Component {
    QString type;             // "RigidBody" / "Collider" / "Renderer" / "Script" / ...
    QJsonObject params;
};

struct SceneObject {
    QString name;
    MeshType mesh = MeshType::Cube;
    // 局部变换（相对父节点；无父节点即世界坐标）
    QVector3D position{0, 0, 0};
    QQuaternion rotation{};
    QVector3D scale{1, 1, 1};
    QVector3D color{0.85f, 0.85f, 0.85f};
    bool hasBody = false;
    bool isStatic = false;
    int parent = -1;                          // 父节点索引，-1 = 根
    // 动态自然：物体级风力参数（.mat 材质风力优先；无材质时生效）
    float windStrength = 0.0f;
    float windFreq = 2.0f;
    std::vector<Component> components;        // ECS 组件列表
    // 材质（拖拽 .mat 应用后生效；为空时用内置 color）
    bool hasMaterial = false;
    Material material;
    // 3D 文本（Text3D）：文字内容与字体族
    QString text = "文本 Text";
    QString textFont;                          // 空 = 默认字体
    // 外部模型（ModelMesh）：.vnmodel / .vnmodel_col 路径 + 动画运行状态
    QString modelPath;                         // .vnmodel / .vnmodel_col
    QString animPath;                          // .vnanim（空 = 不播放）
    bool animPlaying = false;                  // 运行时由 PlayAnimation / Lua 触发
    float animTime = 0.0f;                     // 动画时钟（秒）
    QString animName;                          // 当前播放的动画剪辑名

    QMatrix4x4 localMatrix() const {
        QMatrix4x4 m;
        m.translate(position);
        m.rotate(rotation);
        m.scale(scale);
        return m;
    }
};

// 递归计算世界矩阵（父子层级：QMatrix4x4 矩阵乘法联动）
inline QMatrix4x4 worldMatrixOf(int idx, const std::vector<SceneObject> &objects) {
    QMatrix4x4 m = objects[size_t(idx)].localMatrix();
    int p = objects[size_t(idx)].parent;
    if (p >= 0 && p < int(objects.size()))
        m = worldMatrixOf(p, objects) * m;
    return m;
}

static constexpr float DEFAULT_FIXED_STEP = 1.0f / 60.0f;

class PhysicsWorld {
public:
    float fixedStep = DEFAULT_FIXED_STEP;
    float sleepThreshold = 0.008f;
    bool drawDebug = false;
    std::vector<float> debugLines;

    PhysicsWorld() {
        collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();
        dispatcher = std::make_unique<btCollisionDispatcher>(collisionConfig.get());
        broadphase = std::make_unique<btDbvtBroadphase>();
        solver = std::make_unique<btSequentialImpulseConstraintSolver>();
        world = std::make_unique<btDiscreteDynamicsWorld>(dispatcher.get(), broadphase.get(),
                                                          solver.get(), collisionConfig.get());
        world->setGravity(btVector3(0, -9.81f, 0));

        drawer = std::make_unique<PhysicsDebugDrawer>();
        drawer->lines = {};
        world->setDebugDrawer(drawer.get());
    }

    void setGravity(const QVector3D &g) { world->setGravity(btVector3(g.x(), g.y(), g.z())); }
    QVector3D getGravity() const {
        auto g = world->getGravity();
        return {g.x(), g.y(), g.z()};
    }
    void setSleepThresholdAll(float v) {
        sleepThreshold = v;
        for (auto &b : bodies) b->setSleepingThresholds(v, v);
    }

    // ---- 动态创建刚体 ----
    int addBoxBody(const QVector3D &pos, const QQuaternion &rot, const QVector3D &scale, float mass = 1.0f) {
        shapes.push_back(std::make_unique<btBoxShape>(btVector3(scale.x() * 0.5f, scale.y() * 0.5f, scale.z() * 0.5f)));
        return addBodyCommon(pos, rot, mass);
    }
    int addSphereBody(const QVector3D &pos, const QVector3D &scale, float mass = 1.0f) {
        shapes.push_back(std::make_unique<btSphereShape>(0.5f * qMax(scale.x(), qMax(scale.y(), scale.z()))));
        return addBodyCommon(pos, QQuaternion(), mass);
    }
    int addPlaneBody(const QVector3D &pos, const QVector3D &scale) {
        shapes.push_back(std::make_unique<btBoxShape>(btVector3(scale.x() * 0.5f, qMax(0.025f, scale.y() * 0.5f), scale.z() * 0.5f)));
        return addBodyCommon(pos, QQuaternion(), 0.0f);
    }
    int addCylinderBody(const QVector3D &pos, const QQuaternion &rot, const QVector3D &scale, float mass = 1.0f) {
        shapes.push_back(std::make_unique<btCylinderShape>(btVector3(scale.x() * 0.5f, scale.y() * 0.5f, scale.z() * 0.5f)));
        return addBodyCommon(pos, rot, mass);
    }
    int addCapsuleBody(const QVector3D &pos, const QQuaternion &rot, const QVector3D &scale, float mass = 1.0f) {
        float r = qMin(scale.x(), scale.z()) * 0.5f;
        shapes.push_back(std::make_unique<btCapsuleShape>(r, qMax(0.05f, scale.y() - 2 * r)));
        return addBodyCommon(pos, rot, mass);
    }

    void clearBodies() {
        for (auto &b : bodies) world->removeRigidBody(b.get());
        bodies.clear();
        shapes.clear();
        bodyIndexMap.clear();
    }

    void stepFixed(float dt) { world->stepSimulation(dt, 0, dt); }

    void updateDebugLines() {
        drawer->lines.clear();
        if (drawDebug) world->debugDrawWorld();
        debugLines = drawer->lines;
    }

    // 只同步根级动态刚体（有父节点的物体由矩阵联动负责）
    void syncToObjects(std::vector<SceneObject> &objects) {
        for (size_t i = 0; i < objects.size(); ++i) {
            if (!objects[i].hasBody || objects[i].isStatic) continue;
            if (objects[i].parent >= 0) continue;
            int bi = bodyIndexOf(&objects[i]);
            if (bi < 0) continue;
            const btTransform &t = bodies[bi]->getWorldTransform();
            objects[i].position = QVector3D(t.getOrigin().x(), t.getOrigin().y(), t.getOrigin().z());
            const btQuaternion &q = t.getRotation();
            objects[i].rotation = QQuaternion(q.w(), q.x(), q.y(), q.z());
        }
    }

    void syncBodyFromObject(SceneObject &o, int bi, const QMatrix4x4 &worldMat) {
        if (bi < 0 || size_t(bi) >= bodies.size()) return;
        // 用世界矩阵提取位置（父节点联动后的真实位置）
        QVector3D pos = worldMat.column(3).toVector3D();
        btTransform t;
        t.setIdentity();
        t.setOrigin(btVector3(pos.x(), pos.y(), pos.z()));
        bodies[bi]->setWorldTransform(t);
        bodies[bi]->setLinearVelocity(btVector3(0, 0, 0));
        bodies[bi]->setAngularVelocity(btVector3(0, 0, 0));
    }

    void setBodyIndex(SceneObject *obj, int idx) { bodyIndexMap[obj] = idx; }
    int bodyIndexOf(SceneObject *obj) const {
        auto it = bodyIndexMap.find(obj);
        return it == bodyIndexMap.end() ? -1 : it->second;
    }
    // 演示钩子：把物体连同刚体一起摆到指定位置
    void teleportObject(SceneObject &o, const QVector3D &pos) {
        o.position = pos;
        int bi = bodyIndexOf(&o);
        if (bi < 0) return;
        btTransform t;
        t.setIdentity();
        t.setOrigin(btVector3(pos.x(), pos.y(), pos.z()));
        bodies[size_t(bi)]->setWorldTransform(t);
        bodies[size_t(bi)]->setLinearVelocity(btVector3(0, 0, 0));
        bodies[size_t(bi)]->setAngularVelocity(btVector3(0, 0, 0));
    }
    bool hasBodies() const { return !bodies.empty(); }

    // ---- 插座运行时：按刚体索引施加持续力/冲量 ----
    void applyForce(int bi, const QVector3D &f) {
        if (bi < 0 || size_t(bi) >= bodies.size()) return;
        bodies[size_t(bi)]->activate();
        bodies[size_t(bi)]->applyCentralForce(btVector3(f.x(), f.y(), f.z()));
    }
    void applyImpulse(int bi, const QVector3D &imp) {
        if (bi < 0 || size_t(bi) >= bodies.size()) return;
        bodies[size_t(bi)]->activate();
        bodies[size_t(bi)]->applyCentralImpulse(btVector3(imp.x(), imp.y(), imp.z()));
    }
    // 直接给刚体设速度（持续移动模式用）
    void setBodyVelocity(int bi, const QVector3D &v) {
        if (bi < 0 || size_t(bi) >= bodies.size()) return;
        bodies[size_t(bi)]->activate();
        bodies[size_t(bi)]->setLinearVelocity(btVector3(v.x(), v.y(), v.z()));
    }
    // 读取刚体当前线速度（CharacterController 区分水平/垂直分量用）
    QVector3D getBodyVelocity(int bi) const {
        if (bi < 0 || size_t(bi) >= bodies.size()) return {0, 0, 0};
        const btVector3 &v = bodies[size_t(bi)]->getLinearVelocity();
        return {v.x(), v.y(), v.z()};
    }
    // 下蹲：把刚体碰撞形状替换为更矮的胶囊（总高 totalHeight，半径不变）
    void setCrouchShape(int bi, float totalHeight, float radius) {
        if (bi < 0 || size_t(bi) >= bodies.size()) return;
        shapes.push_back(std::make_unique<btCapsuleShape>(radius, qMax(0.05f, totalHeight - 2 * radius)));
        bodies[size_t(bi)]->setCollisionShape(shapes.back().get());
    }

private:
    int addBodyCommon(const QVector3D &pos, const QQuaternion &rot, float mass) {
        btTransform t; t.setIdentity();
        t.setOrigin(btVector3(pos.x(), pos.y(), pos.z()));
        t.setRotation(btQuaternion(rot.x(), rot.y(), rot.z(), rot.scalar()));
        bodies.push_back(std::make_unique<btRigidBody>(
            btRigidBody::btRigidBodyConstructionInfo(mass, nullptr, shapes.back().get(), btVector3(0, 0, 0))));
        btRigidBody *b = bodies.back().get();
        b->setWorldTransform(t);
        b->setFriction(0.6f);
        b->setRestitution(0.05f);
        b->setSleepingThresholds(sleepThreshold, sleepThreshold);
        if (mass > 0) b->setLinearVelocity(btVector3((std::rand() % 400 - 200) / 1000.0f, 0, (std::rand() % 400 - 200) / 1000.0f));
        world->addRigidBody(b);
        return int(bodies.size()) - 1;
    }

    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfig;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> world;
    std::unique_ptr<PhysicsDebugDrawer> drawer;
    std::vector<std::unique_ptr<btCollisionShape>> shapes;
    std::vector<std::unique_ptr<btRigidBody>> bodies;
    std::map<SceneObject *, int> bodyIndexMap;
};

// ============ 编辑器工具模式（Unity 工作流） ============
enum class ToolMode { Select = 0, Translate = 1, Rotate = 2, Scale = 3, Mode2D = 4 };
static constexpr int TOOL_MODE_COUNT = 5;

struct World {
    std::vector<SceneObject> objects;
    PhysicsWorld physics;
    bool showGizmo = true;
    ToolMode tool = ToolMode::Select;

    // 轨道相机（左键拖拽/滚轮）
    float camYaw = 35.0f, camPitch = -28.0f, camDist = 14.0f;
    QVector3D camTarget{0, 2.0f, 0};

    // 飞行相机（右键自由视角 + WASD/QE 平滑移动）
    bool flyMode = false;
    QVector3D flyPos{0, 3.0f, 12.0f};
    float flyYaw = 180.0f, flyPitch = -10.0f;
    QVector3D flyVel{0, 0, 0};        // 平滑速度（惯性）
    QVector3D flyVelTarget{0, 0, 0};

    // 角色控制器运行态：鼠标滑动控制视角（Pitch/Yaw，角色面朝前进方向）
    float charLookYaw = 180.0f, charLookPitch = -14.0f;

    // 星空顶
    bool showSkybox = true;

    // ---- 天气 / 天空系统（VNWeatherStudio .sky 与 ToggleDayNight 插座节点驱动）----
    QString skyWeather = "晴天";
    QVector3D skyAmbient{1.0f, 1.0f, 1.0f};       // 环境光颜色（着色器 uAmbient）
    float skyIntensity = 1.0f;                    // 环境光强度（uAmbientI）
    QVector3D skyClear{0.09f, 0.10f, 0.13f};      // 视口清屏色
    bool nightMode = false;                       // 昼夜切换状态（插座节点循环翻转）

    // ---- 手电筒（交互动作节点 ToggleFlashlight 驱动；开着时角色视点前方有聚光）----
    bool flashlightOn = false;

    void applyWeather(const QString &w) {
        skyWeather = w;
        if (w.contains("晴"))  { skyAmbient = {1.00f,1.00f,1.00f}; skyIntensity=1.0f; skyClear={0.09f,0.10f,0.13f}; nightMode=false; }
        if (w.contains("雨"))  { skyAmbient = {0.45f,0.50f,0.62f}; skyIntensity=0.7f; skyClear={0.16f,0.18f,0.24f}; nightMode=false; }
        if (w.contains("雾"))  { skyAmbient = {0.90f,0.92f,0.95f}; skyIntensity=0.55f; skyClear={0.55f,0.58f,0.62f}; nightMode=false; }
        if (w.contains("夜"))  { skyAmbient = {0.30f,0.36f,0.62f}; skyIntensity=0.45f; skyClear={0.02f,0.03f,0.09f}; nightMode=true; }
    }
    // 插座节点：白天 <-> 黑夜 循环切换
    void toggleDayNight() {
        applyWeather(nightMode ? "晴天" : "夜晚");
        qInfo().noquote() << QString("[Sky] 插座节点 ToggleDayNight：切换到%1（环境光 x%2）")
            .arg(nightMode ? "黑夜" : "白天").arg(skyIntensity, 0, 'f', 2);
    }

    // ---- 摄像机挂载（Camera 物体成为其他物体的子节点后，平滑跟随）----
    int followCamIdx = -1;                        // -1 = 不跟随
    QVector3D followEye{0, 3, 12};
    QVector3D followCenter{0, 0, 0};
    bool followInit = false;

    int counters[MESH_TYPE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // ---- 飞行相机视图矩阵 ----
    QMatrix4x4 viewMatrix() const {
        QMatrix4x4 v;
        // 摄像机挂载跟随：Camera 物体为子节点时，主视角 = 挂载相机的平滑跟随视图
        if (followCamIdx >= 0 && !flyMode) {
            v.lookAt(followEye, followCenter, QVector3D(0, 1, 0));
            return v;
        }
        if (flyMode) {
            float yr = qDegreesToRadians(flyYaw), pr = qDegreesToRadians(flyPitch);
            QVector3D dir(cos(pr) * sin(yr), sin(pr), cos(pr) * cos(yr));
            v.lookAt(flyPos, flyPos + dir, QVector3D(0, 1, 0));
        } else {
            float y = qDegreesToRadians(camPitch), yr = qDegreesToRadians(camYaw);
            QVector3D eye(
                camTarget.x() + camDist * cos(yr) * cos(y),
                camTarget.y() + camDist * sin(-y),
                camTarget.z() + camDist * sin(yr) * cos(y));
            v.lookAt(eye, camTarget, QVector3D(0, 1, 0));
        }
        return v;
    }
    QVector3D cameraEye() const {
        if (followCamIdx >= 0 && !flyMode) return followEye;
        if (flyMode) return flyPos;
        float y = qDegreesToRadians(camPitch), yr = qDegreesToRadians(camYaw);
        return QVector3D(
            camTarget.x() + camDist * cos(yr) * cos(y),
            camTarget.y() + camDist * sin(-y),
            camTarget.z() + camDist * sin(yr) * cos(y));
    }
};
