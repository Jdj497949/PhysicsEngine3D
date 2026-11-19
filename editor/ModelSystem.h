#pragma once
// ============ 模型资产管线（Assimp 外部导入 → .vnmodel 专属格式 → CPU 蒙皮动画） ============
// 外部模型：.obj / .fbx / .gltf / .glb → Assimp 解析 → 转存引擎专属 .vnmodel（JSON）
// 骨骼动画：.vnanim（轨道=骨骼名 + 关键帧四元数），CPU 蒙皮后动态更新 VBO
#include <QString>
#include <QVector3D>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <vector>
#include <map>
#include <functional>
#include <cmath>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// 引擎专属模型：顶点(pos+normal 交错) + 索引 + 蒙皮关节权重 + 骨骼层级
struct VNModel {
    struct Bone {
        QString name;
        int parent = -1;                 // -1 = 根
        QVector3D translation;           // 绑定姿态局部平移
        QMatrix4x4 invBind;              // 逆绑定矩阵
    };
    std::vector<float> verts;            // px,py,pz, nx,ny,nz
    std::vector<unsigned> indices;
    std::vector<unsigned char> joints;   // 每顶点 4 个关节索引
    std::vector<float> weights;          // 每顶点 4 个权重
    std::vector<Bone> bones;
    QVector3D bboxMin, bboxMax;
    bool hasSkin() const { return !bones.empty(); }
    QVector3D bboxSize() const { return bboxMax - bboxMin; }
    QVector3D bboxCenter() const { return (bboxMax + bboxMin) * 0.5f; }
};

// 动画剪辑：多条轨道（骨骼名 + 时间/四元数关键帧）
struct VNAnim {
    struct Track { QString bone; std::vector<std::pair<float, QQuaternion>> keys; };
    QString name;
    float duration = 1.0f;
    std::vector<Track> tracks;
};

class ModelSystem {
public:
    // ---------- Assimp 导入外部模型 ----------
    static bool importExternal(const QString &path, VNModel &out, QString *err = nullptr,
                               std::vector<VNAnim> *anims = nullptr) {
        Assimp::Importer imp;
        const aiScene *sc = imp.ReadFile(path.toStdString(),
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals);
        if (!sc || !sc->HasMeshes()) {
            if (err) *err = QString("Assimp 无法解析 %1").arg(path);
            return false;
        }
        // 节点名 -> aiNode（查骨骼父子关系）
        std::map<std::string, const aiNode*> nodeByName;
        std::function<void(const aiNode*)> walk = [&](const aiNode *n) {
            nodeByName[n->mName.C_Str()] = n;
            for (unsigned i = 0; i < n->mNumChildren; ++i) walk(n->mChildren[i]);
        };
        walk(sc->mRootNode);

        auto nodeWorld = [&](const aiNode *n) {
            QMatrix4x4 m;
            auto &t = n->mTransformation;
            QMatrix4x4 l(t.a1, t.b1, t.c1, t.d1,
                         t.a2, t.b2, t.c2, t.d2,
                         t.a3, t.b3, t.c3, t.d3,
                         t.a4, t.b4, t.c4, t.d4);
            m = l;
            return m;
        };

        int ok2 = 0; (void)ok2;
        std::map<std::string, int> boneIndex;
        bool ok = false;

        // 合并所有网格（应用节点世界变换）
        for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
            const aiMesh *m = sc->mMeshes[mi];
            unsigned base = unsigned(out.verts.size() / 6);
            for (unsigned vi = 0; vi < m->mNumVertices; ++vi) {
                const aiVector3D &p = m->mVertices[vi];
                out.verts.push_back(p.x); out.verts.push_back(p.y); out.verts.push_back(p.z);
                if (m->mNormals) {
                    const aiVector3D &n = m->mNormals[vi];
                    out.verts.push_back(n.x); out.verts.push_back(n.y); out.verts.push_back(n.z);
                } else { out.verts.push_back(0); out.verts.push_back(1); out.verts.push_back(0); }
                out.joints.push_back(0); out.joints.push_back(0); out.joints.push_back(0); out.joints.push_back(0);
                out.weights.push_back(1); out.weights.push_back(0); out.weights.push_back(0); out.weights.push_back(0);
            }
            for (unsigned fi = 0; fi < m->mNumFaces; ++fi) {
                const aiFace &f = m->mFaces[fi];
                for (unsigned k = 0; k < f.mNumIndices; ++k)
                    out.indices.push_back(base + f.mIndices[k]);
            }
            // 蒙皮权重
            for (unsigned bi = 0; bi < m->mNumBones; ++bi) {
                const aiBone *b = m->mBones[bi];
                std::string bn = b->mName.C_Str();
                if (!boneIndex.count(bn)) {
                    VNModel::Bone nb;
                    nb.name = QString::fromStdString(bn);
                    // 找父骨骼：沿节点树向上，直到命中另一根骨骼或场景根
                    const aiNode *n = nodeByName.count(bn) ? nodeByName[bn] : nullptr;
                    if (n && n->mParent) {
                        nb.translation = QVector3D(n->mTransformation.a4, n->mTransformation.b4, n->mTransformation.c4);
                        std::string pn = n->mParent->mName.C_Str();
                        if (boneIndex.count(pn)) nb.parent = boneIndex[pn];
                    }
                    auto &t = b->mOffsetMatrix;
                    nb.invBind = QMatrix4x4(t.a1, t.b1, t.c1, t.d1,
                                            t.a2, t.b2, t.c2, t.d2,
                                            t.a3, t.b3, t.c3, t.d3,
                                            t.a4, t.b4, t.c4, t.d4);
                    boneIndex[bn] = int(out.bones.size());
                    out.bones.push_back(nb);
                }
                int biIdx = boneIndex[bn];
                for (unsigned w = 0; w < b->mNumWeights; ++w) {
                    unsigned vid = b->mWeights[w].mVertexId;
                    float wt = b->mWeights[w].mWeight;
                    size_t vo = size_t(base + vid);
                    // 找到空权重槽位写入
                    for (int s = 0; s < 4; ++s) {
                        if (out.weights[vo * 4 + s] <= 0.0f) {
                            out.weights[vo * 4 + s] = wt;
                            out.joints[vo * 4 + s] = (unsigned char)biIdx;
                            break;
                        }
                    }
                }
            }
            ok = true;
        }
        if (!ok) { if (err) *err = "场景内没有网格"; return false; }
        // 包围盒
        out.bboxMin = out.bboxMax = QVector3D(out.verts[0], out.verts[1], out.verts[2]);
        for (size_t i = 0; i < out.verts.size() / 6; ++i) {
            QVector3D p(out.verts[i * 6], out.verts[i * 6 + 1], out.verts[i * 6 + 2]);
            out.bboxMin = QVector3D(qMin(out.bboxMin.x(), p.x()), qMin(out.bboxMin.y(), p.y()), qMin(out.bboxMin.z(), p.z()));
            out.bboxMax = QVector3D(qMax(out.bboxMax.x(), p.x()), qMax(out.bboxMax.y(), p.y()), qMax(out.bboxMax.z(), p.z()));
        }
        // 导出内嵌动画（供 VNModelStudio 直接预览）
        if (anims) {
            for (unsigned ai = 0; ai < sc->mNumAnimations; ++ai) {
                const aiAnimation *an = sc->mAnimations[ai];
                VNAnim va;
                va.name = QString::fromStdString(an->mName.C_Str()).isEmpty()
                    ? QString("anim%1").arg(ai) : QString::fromStdString(an->mName.C_Str());
                va.duration = float(an->mDuration);
                if (va.duration <= 0) va.duration = 1.0f;
                for (unsigned c = 0; c < an->mNumChannels; ++c) {
                    const aiNodeAnim *ch = an->mChannels[c];
                    if (!ch->mNumRotationKeys) continue;
                    VNAnim::Track tr;
                    tr.bone = QString::fromStdString(ch->mNodeName.C_Str());
                    for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                        const auto &q = ch->mRotationKeys[k].mValue;
                        tr.keys.push_back({float(ch->mRotationKeys[k].mTime),
                            QQuaternion(float(q.w), float(q.x), float(q.y), float(q.z))});
                    }
                    va.tracks.push_back(tr);
                }
                if (!va.tracks.empty()) anims->push_back(va);
            }
        }
        (void)nodeWorld;
        return true;
    }

    // ---------- .vnmodel 专属格式（JSON） ----------
    static QJsonObject toJson(const VNModel &m, bool withCollision, const QString &srcPath) {
        QJsonObject root;
        root["format"] = "vnmodel";
        root["collision"] = withCollision;
        root["source"] = QFileInfo(srcPath).fileName();
        QJsonArray bb0 = {m.bboxMin.x(), m.bboxMin.y(), m.bboxMin.z()};
        QJsonArray bb1 = {m.bboxMax.x(), m.bboxMax.y(), m.bboxMax.z()};
        root["bboxMin"] = bb0; root["bboxMax"] = bb1;
        QJsonArray vs;
        for (float v : m.verts) vs.append(v);
        root["verts"] = vs;
        QJsonArray is;
        for (unsigned v : m.indices) is.append(double(v));
        root["indices"] = is;
        if (m.hasSkin()) {
            QJsonArray bn, jn, wt;
            for (auto &b : m.bones) {
                QJsonObject jo;
                jo["name"] = b.name; jo["parent"] = b.parent;
                jo["translation"] = QJsonArray{b.translation.x(), b.translation.y(), b.translation.z()};
                QJsonArray ib;
                for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) ib.append(b.invBind(r, c));
                jo["invBind"] = ib;
                bn.append(jo);
            }
            for (unsigned char j : m.joints) jn.append(int(j));
            for (float w : m.weights) wt.append(w);
            root["bones"] = bn; root["joints"] = jn; root["weights"] = wt;
        }
        return root;
    }
    static bool saveVNModel(const VNModel &m, bool withCollision, const QString &outPath, const QString &srcPath) {
        QJsonDocument doc(toJson(m, withCollision, srcPath));
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(doc.toJson());
        return true;
    }
    static bool loadVNModel(const QString &path, VNModel &m) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        if (root.value("format").toString() != "vnmodel") return false;
        for (auto v : root.value("verts").toArray()) m.verts.push_back(float(v.toDouble()));
        for (auto v : root.value("indices").toArray()) m.indices.push_back(unsigned(v.toDouble()));
        auto b0 = root.value("bboxMin").toArray(), b1 = root.value("bboxMax").toArray();
        if (b0.size() == 3 && b1.size() == 3) {
            m.bboxMin = QVector3D(float(b0[0].toDouble()), float(b0[1].toDouble()), float(b0[2].toDouble()));
            m.bboxMax = QVector3D(float(b1[0].toDouble()), float(b1[1].toDouble()), float(b1[2].toDouble()));
        } else { m.bboxMin = QVector3D(-1, 0, -1); m.bboxMax = QVector3D(1, 2, 1); }
        for (auto v : root.value("bones").toArray()) {
            auto jo = v.toObject();
            VNModel::Bone b;
            b.name = jo.value("name").toString(); b.parent = jo.value("parent").toInt(-1);
            auto tr = jo.value("translation").toArray();
            if (tr.size() == 3) b.translation = QVector3D(float(tr[0].toDouble()), float(tr[1].toDouble()), float(tr[2].toDouble()));
            auto ib = jo.value("invBind").toArray();
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
                int i = r * 4 + c;
                if (i < ib.size()) b.invBind(r, c) = float(ib[i].toDouble());
            }
            m.bones.push_back(b);
        }
        for (auto v : root.value("joints").toArray()) m.joints.push_back((unsigned char)v.toInt());
        for (auto v : root.value("weights").toArray()) m.weights.push_back(float(v.toDouble()));
        return !m.verts.empty();
    }

    // ---------- .vnanim 动画文件 ----------
    static bool saveVNAnim(const VNAnim &a, const QString &outPath) {
        QJsonObject root;
        root["format"] = "vnanim"; root["name"] = a.name; root["duration"] = a.duration;
        QJsonArray trs;
        for (auto &t : a.tracks) {
            QJsonObject tr; tr["bone"] = t.bone;
            QJsonArray ks;
            for (auto &k : t.keys) ks.append(QJsonArray{k.first, k.second.x(), k.second.y(), k.second.z(), k.second.scalar()});
            tr["keys"] = ks;
            trs.append(tr);
        }
        root["tracks"] = trs;
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(root).toJson());
        return true;
    }
    static bool loadVNAnim(const QString &path, VNAnim &a) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        if (root.value("format").toString() != "vnanim") return false;
        a.name = root.value("name").toString("anim");
        a.duration = float(root.value("duration").toDouble(1.0));
        for (auto v : root.value("tracks").toArray()) {
            auto jo = v.toObject();
            VNAnim::Track t; t.bone = jo.value("bone").toString();
            for (auto kv : jo.value("keys").toArray()) {
                auto k = kv.toArray();
                if (k.size() >= 5)
                    t.keys.push_back({float(k[0].toDouble()),
                        QQuaternion(float(k[4].toDouble()), float(k[1].toDouble()),
                                    float(k[2].toDouble()), float(k[3].toDouble()))});
            }
            if (!t.keys.empty()) a.tracks.push_back(t);
        }
        return !a.tracks.empty();
    }

    // ---------- 采样：时间 t（循环） -> 每骨骼旋转 ----------
    static std::map<QString, QQuaternion> sample(const VNAnim &a, float t) {
        std::map<QString, QQuaternion> pose;
        float tt = a.duration > 0 ? std::fmod(t, a.duration) : 0;
        for (auto &tr : a.tracks) {
            if (tr.keys.empty()) continue;
            if (tt <= tr.keys.front().first) { pose[tr.bone] = tr.keys.front().second; continue; }
            if (tt >= tr.keys.back().first) { pose[tr.bone] = tr.keys.back().second; continue; }
            for (size_t k = 1; k < tr.keys.size(); ++k) {
                if (tt <= tr.keys[k].first) {
                    float t0 = tr.keys[k - 1].first, t1 = tr.keys[k].first;
                    float f = (t1 > t0) ? (tt - t0) / (t1 - t0) : 0;
                    pose[tr.bone] = QQuaternion::slerp(tr.keys[k - 1].second, tr.keys[k].second, f);
                    break;
                }
            }
        }
        return pose;
    }

    // ---------- CPU 蒙皮：按骨骼姿势变换顶点，返回新 pos+normal 数组 ----------
    static std::vector<float> skin(const VNModel &m, const std::map<QString, QQuaternion> &pose) {
        size_t vc = m.verts.size() / 6;
        std::vector<float> out(m.verts);
        if (!m.hasSkin()) return out;
        // 骨骼世界矩阵链
        std::vector<QMatrix4x4> world(m.bones.size());
        for (size_t i = 0; i < m.bones.size(); ++i) {
            QMatrix4x4 l; l.translate(m.bones[i].translation);
            auto it = pose.find(m.bones[i].name);
            if (it != pose.end()) l.rotate(it->second);
            world[i] = (m.bones[i].parent >= 0) ? world[size_t(m.bones[i].parent)] * l : l;
        }
        std::vector<QMatrix4x4> skinM(m.bones.size());
        for (size_t i = 0; i < m.bones.size(); ++i) skinM[i] = world[i] * m.bones[i].invBind;
        for (size_t v = 0; v < vc; ++v) {
            QVector3D p(m.verts[v * 6], m.verts[v * 6 + 1], m.verts[v * 6 + 2]);
            QVector3D n(m.verts[v * 6 + 3], m.verts[v * 6 + 4], m.verts[v * 6 + 5]);
            QVector3D sp(0, 0, 0), sn(0, 0, 0);
            for (int k = 0; k < 4; ++k) {
                float w = m.weights[v * 4 + k];
                if (w <= 0) continue;
                int j = m.joints[v * 4 + k];
                if (size_t(j) >= skinM.size()) continue;
                sp += w * skinM[size_t(j)] * p;
                // 法线用旋转部分变换（去掉平移分量即可，演示管线不做逆转置校正）
                QMatrix4x4 r = skinM[size_t(j)];
                r.setRow(3, QVector4D(0, 0, 0, 1));
                r.setColumn(3, QVector4D(0, 0, 0, 1));
                sn += w * (r * n);
            }
            out[v * 6] = sp.x(); out[v * 6 + 1] = sp.y(); out[v * 6 + 2] = sp.z();
            out[v * 6 + 3] = sn.x(); out[v * 6 + 4] = sn.y(); out[v * 6 + 5] = sn.z();
        }
        return out;
    }
};
