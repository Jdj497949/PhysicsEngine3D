#pragma once
// ============ 布娃娃（Ragdoll）系统：Bullet 多刚体 + 15 关节约束 ============
// 纯 Bullet 依赖（QtCore 日志可选），无头版/编辑器/演示程序共用：
//   RagdollSpec spec; spec.origin = btVector3(0, 3, 0);
//   RagdollBuilder::build(spec, *btDiscreteDynamicsWorld*, parts, joints);
#include <QString>
#include <vector>
#include <memory>
#include <btBulletDynamicsCommon.h>

struct RagdollSpec {
    btVector3 origin{0, 3, 0};       // 盆骨出生点（世界坐标）
    float scale = 1.0f;              // 整体缩放
    float mass = 1.0f;               // 单件质量
    btVector3 windDir{0, 0, 0};      // 起始姿态扰动（防假死对称）
};

struct RagdollParts {
    std::vector<std::unique_ptr<btCollisionShape>> shapes;
    std::vector<std::unique_ptr<btRigidBody>> bodies;
    std::vector<btTypedConstraint *> joints;        // world 持有后由 build 方清空所有权
    std::vector<QString> partNames;
    std::vector<QString> jointNames;
};

class RagdollBuilder {
public:
    // 15 关节人形：颈、腰椎、双肩、双肘、双腕、双髋、双膝、双踝
    static RagdollSpec humanoid(const btVector3 &origin, float scale = 1.0f) {
        RagdollSpec s;
        s.origin = origin;
        s.scale = scale;
        return s;
    }

    static void build(const RagdollSpec &spec, btDiscreteDynamicsWorld &world,
                      RagdollParts &out) {
        const float S = spec.scale;
        auto box = [&](const btVector3 &half) {
            out.shapes.push_back(std::make_unique<btBoxShape>(
                btVector3(half.x() * S, half.y() * S, half.z() * S)));
            return out.shapes.back().get();
        };
        auto body = [&](const btVector3 &pos, btCollisionShape *shape, float mass,
                        const QString &name) {
            btTransform t; t.setIdentity();
            t.setOrigin(btVector3(spec.origin.x() + pos.x() * S,
                                  spec.origin.y() + pos.y() * S,
                                  spec.origin.z() + pos.z() * S));
            out.bodies.push_back(std::make_unique<btRigidBody>(
                btRigidBody::btRigidBodyConstructionInfo(mass * S, nullptr, shape, btVector3(0, 0, 0))));
            btRigidBody *b = out.bodies.back().get();
            b->setWorldTransform(t);
            b->setFriction(0.7f);
            b->setRestitution(0.02f);
            b->setDamping(0.05f, 0.15f);
            b->setLinearVelocity(btVector3(spec.windDir.x(), 0, spec.windDir.z()));
            world.addRigidBody(b);
            out.partNames.push_back(name);
            return b;
        };
        // 锥形关节（球窝 + 限角）：骨骼铰点天生受限于真实人体活动范围
        auto coneJoint = [&](btRigidBody *a, btRigidBody *b, const btVector3 &pivotA,
                             const btVector3 &pivotB, const btVector3 &axisA,
                             const btVector3 &axisB, float angleLimit, const QString &name) {
            btVector3 pa(pivotA.x() * S, pivotA.y() * S, pivotA.z() * S);
            btVector3 pb(pivotB.x() * S, pivotB.y() * S, pivotB.z() * S);
            btVector3 axA(axisA.x(), axisA.y(), axisA.z());
            btVector3 axB(axisB.x(), axisB.y(), axisB.z());
            auto *c = new btConeTwistConstraint(*a, *b, btTransform(btQuaternion(axA.normalized(), 0), pa),
                                                btTransform(btQuaternion(axB.normalized(), 0), pb));
            c->setLimit(angleLimit, angleLimit, angleLimit, angleLimit, angleLimit);  // 软限位：允许各向小幅弯曲
            c->setDamping(0.05f);
            world.addConstraint(c);
            out.joints.push_back(c);
            out.jointNames.push_back(name);
        };

        // ---- 身体件（局部偏移，单位米，未乘 S；相对 origin=盆骨中心）----
        auto *pelvis  = body({0, 0, 0},        box({0.20f, 0.13f, 0.12f}), spec.mass, "盆骨");
        auto *lumbar  = body({0, 0.28f, 0},    box({0.17f, 0.15f, 0.11f}), spec.mass, "腰椎");
        auto *chest   = body({0, 0.60f, 0},    box({0.22f, 0.17f, 0.12f}), spec.mass, "胸椎");
        auto *head    = body({0, 0.92f, 0},    box({0.11f, 0.11f, 0.11f}), spec.mass * 0.8f, "头部");
        auto *armLU   = body({-0.32f, 0.58f, 0}, box({0.07f, 0.14f, 0.07f}), spec.mass * 0.7f, "左上臂");
        auto *armRU   = body({0.32f, 0.58f, 0},  box({0.07f, 0.14f, 0.07f}), spec.mass * 0.7f, "右上臂");
        auto *armLL   = body({-0.32f, 0.28f, 0}, box({0.06f, 0.14f, 0.06f}), spec.mass * 0.6f, "左前臂");
        auto *armRL   = body({0.32f, 0.28f, 0},  box({0.06f, 0.14f, 0.06f}), spec.mass * 0.6f, "右前臂");
        auto *handL   = body({-0.32f, 0.02f, 0}, box({0.05f, 0.06f, 0.05f}), spec.mass * 0.3f, "左手");
        auto *handR   = body({0.32f, 0.02f, 0},  box({0.05f, 0.06f, 0.05f}), spec.mass * 0.3f, "右手");
        auto *thighL  = body({-0.12f, -0.34f, 0}, box({0.09f, 0.19f, 0.09f}), spec.mass * 0.9f, "左大腿");
        auto *thighR  = body({0.12f, -0.34f, 0},  box({0.09f, 0.19f, 0.09f}), spec.mass * 0.9f, "右大腿");
        auto *shinL   = body({-0.12f, -0.76f, 0}, box({0.07f, 0.20f, 0.07f}), spec.mass * 0.7f, "左小腿");
        auto *shinR   = body({0.12f, -0.76f, 0},  box({0.07f, 0.20f, 0.07f}), spec.mass * 0.7f, "右小腿");
        auto *footL   = body({-0.12f, -1.04f, 0.05f}, box({0.07f, 0.05f, 0.12f}), spec.mass * 0.4f, "左脚");
        auto *footR   = body({0.12f, -1.04f, 0.05f},  box({0.07f, 0.05f, 0.12f}), spec.mass * 0.4f, "右脚");

        const btVector3 UP(1, 0, 0), FWD(0, 0, 1);
        // ---- 15 关节（锥形球窝，限位即自然瘫软的来源）----
        coneJoint(chest, head,   {0, 0.15f, 0},  {0, -0.13f, 0}, FWD, FWD, 0.4f, "颈椎");
        coneJoint(pelvis, lumbar, {0, 0.13f, 0}, {0, -0.16f, 0}, FWD, FWD, 0.5f, "腰椎");
        coneJoint(armLU, chest,  {0, 0.16f, 0},  {-0.24f, 0.10f, 0}, UP, UP, 0.9f, "左肩");
        coneJoint(armRU, chest,  {0, 0.16f, 0},  {0.24f, 0.10f, 0},  UP, UP, 0.9f, "右肩");
        coneJoint(armLL, armLU,  {0, 0.15f, 0},  {0, -0.15f, 0}, FWD, FWD, 0.8f, "左肘");
        coneJoint(armRL, armRU,  {0, 0.15f, 0},  {0, -0.15f, 0}, FWD, FWD, 0.8f, "右肘");
        coneJoint(handL, armLL,  {0, 0.07f, 0},  {0, -0.07f, 0}, FWD, FWD, 0.5f, "左腕");
        coneJoint(handR, armRL,  {0, 0.07f, 0},  {0, -0.07f, 0}, FWD, FWD, 0.5f, "右腕");
        coneJoint(thighL, pelvis, {0, 0.20f, 0}, {-0.12f, -0.12f, 0}, FWD, FWD, 0.9f, "左髋");
        coneJoint(thighR, pelvis, {0, 0.20f, 0}, {0.12f, -0.12f, 0},  FWD, FWD, 0.9f, "右髋");
        coneJoint(shinL, thighL, {0, 0.20f, 0},  {0, -0.20f, 0}, FWD, FWD, 0.7f, "左膝");
        coneJoint(shinR, thighR, {0, 0.20f, 0},  {0, -0.20f, 0}, FWD, FWD, 0.7f, "右膝");
        coneJoint(footL, shinL,  {0, 0.22f, 0},  {0, -0.06f, 0}, FWD, FWD, 0.4f, "左踝");
        coneJoint(footR, shinR,  {0, 0.22f, 0},  {0, -0.06f, 0}, FWD, FWD, 0.4f, "右踝");
        coneJoint(lumbar, chest, {0, 0.14f, 0},  {0, -0.16f, 0}, FWD, FWD, 0.4f, "胸椎关节");
    }
};
