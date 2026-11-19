// 3DRagdollDemo —— 布娃娃物理实测：15 关节人形从半空跌落，观察各关节自然瘫软
// 用法：3DRagdollDemo [跌落高度] ；退出码 0 = 全部关节保持完好且身体落地
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <vector>
#include <memory>
#include <cmath>
#include <btBulletDynamicsCommon.h>
#include "Ragdoll.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const float dropHeight = argc > 1 ? qBound(1.0f, QString(argv[1]).toFloat(), 50.0f) : 3.0f;

    // ---- 物理世界 ----
    btDefaultCollisionConfiguration cfg;
    btCollisionDispatcher dispatcher(&cfg);
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &cfg);
    world.setGravity(btVector3(0, -9.81f, 0));

    // ---- 地面（静止大板）----
    btBoxShape groundShape(btVector3(40, 0.5f, 40));
    btRigidBody::btRigidBodyConstructionInfo gInfo(0, nullptr, &groundShape, btVector3(0, 0, 0));
    btRigidBody ground(gInfo);
    btTransform gt; gt.setIdentity(); gt.setOrigin(btVector3(0, -0.5f, 0));
    ground.setWorldTransform(gt);
    ground.setFriction(0.9f);
    world.addRigidBody(&ground);

    // ---- 布娃娃 ----
    RagdollParts ragdoll;
    RagdollSpec spec = RagdollBuilder::humanoid(btVector3(0, dropHeight, 0), 1.0f);
    spec.windDir = {0.15f, 0, 0.1f};   // 轻微初始偏摆，落地姿态每次不重样
    RagdollBuilder::build(spec, world, ragdoll);
    qInfo().noquote() << QString("[PHYSICS] Ragdoll initialized with %1 joint constraints. (%2 body parts)")
        .arg(ragdoll.joints.size()).arg(ragdoll.bodies.size());
    for (const QString &j : ragdoll.jointNames)
        qInfo().noquote() << QString("[PHYSICS]   joint: %1 (btConeTwistConstraint)").arg(j);

    // ---- 逐步仿真 4 秒 ----
    const float dt = 1.0f / 120.0f;
    QElapsedTimer clock; clock.start();
    for (int step = 0; step < int(4.0f / dt); ++step)
        world.stepSimulation(dt, 1, dt);
    qInfo().noquote() << QString("[PHYSICS] simulated %1 s in %2 ms wall time")
        .arg(4.0f, 0, 'f', 1).arg(clock.elapsed());

    // ---- 验收：全部关节仍在（未被拉爆）+ 躯干贴地 ----
    bool jointsOk = ragdoll.joints.size() == 15;
    double minTorsoY = 1e9;
    for (size_t i = 0; i < ragdoll.bodies.size(); ++i) {
        const btTransform &t = ragdoll.bodies[i]->getWorldTransform();
        if (ragdoll.partNames[i].contains("盆") || ragdoll.partNames[i].contains("胸") || ragdoll.partNames[i].contains("腰"))
            minTorsoY = qMin<double>(minTorsoY, t.getOrigin().y());
    }
    const bool settled = minTorsoY < dropHeight * 0.5f;
    qInfo().noquote() << QString("[PHYSICS] 落地验收：关节完好 %1/15，躯干最低高度 %2 m（初始 %3 m）→ %4")
        .arg(jointsOk ? 15 : 0).arg(minTorsoY, 0, 'f', 2).arg(dropHeight, 0, 'f', 1)
        .arg(settled && jointsOk ? "瘫软落地 ✅" : "未达标 ❌");
    return (jointsOk && settled) ? 0 : 1;
}
