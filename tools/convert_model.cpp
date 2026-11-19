// 模型转换工具：Assimp 导入 → VNModel 序列化
// 用法：convert_model <src.obj|.fbx|.gltf> <out.vnmodel|.vnmodel_col> [带碰撞=1]
#include "ModelSystem.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QDir>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) { qWarning() << "用法: convert_model <src> <out.vnmodel[_col]>"; return 1; }
    VNModel m;
    std::vector<VNAnim> anims;
    QString err;
    if (!ModelSystem::importExternal(argv[1], m, &err, &anims)) {
        qWarning() << "导入失败:" << err;
        return 2;
    }
    QString out = argv[2];
    bool col = out.endsWith(".vnmodel_col");
    QDir().mkpath(QFileInfo(out).absolutePath());
    if (!ModelSystem::saveVNModel(m, col, out, argv[1])) { qWarning() << "写出失败"; return 3; }
    qInfo() << QString("转换完成：%1（%2 顶点 / %3 面片 / %4 骨骼 / %5 动画）%6")
        .arg(out, QString::number(m.verts.size() / 6), QString::number(m.indices.size() / 3),
             QString::number(m.bones.size()), QString::number(anims.size()),
             col ? "带碰撞体" : "纯视觉");
    // 顺带导出第一个动画为 <basename>.vnanim（同名骨骼轨道）
    for (size_t ai = 0; ai < anims.size(); ++ai) {
        if (anims[ai].tracks.empty()) continue;
        QString animOut = QFileInfo(out).absolutePath() + "/" + QFileInfo(out).completeBaseName() + ".vnanim";
        if (ModelSystem::saveVNAnim(anims[ai], animOut))
            qInfo() << QString("动画已导出：%1（%2 轨道，时长 %3s）")
                .arg(QFileInfo(animOut).fileName()).arg(anims[ai].tracks.size()).arg(anims[ai].duration);
        break;
    }
    return 0;
}
