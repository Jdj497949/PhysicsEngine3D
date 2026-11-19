#pragma once
// ============ 插件系统（第九阶段：C++ 插件架构 + JSON 配置机制） ============
// 项目根 plugins.json 持久化；自带两个可勾选插件：
//   HD Graphics（高清画质：MSAA/阴影，低配/高配）
//   Dynamic Nature（动态自然：草地顶点风吹特效）
// 插件通过 JSON 配置启用/禁用，C++ 侧按状态驱动渲染特性，不硬编码开关。
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>

struct PluginState {
    bool hdGraphics = true;
    QString hdQuality = "高配";      // 低配 / 高配
    bool dynamicNature = true;

    QJsonObject toJson() const {
        return QJsonObject{
            {"hdGraphics", hdGraphics},
            {"hdQuality", hdQuality},
            {"dynamicNature", dynamicNature},
        };
    }
    void fromJson(const QJsonObject &j) {
        hdGraphics = j.value("hdGraphics").toBool(true);
        hdQuality = j.value("hdQuality").toString("高配");
        dynamicNature = j.value("dynamicNature").toBool(true);
    }
};

class PluginSystem {
public:
    static PluginState load(const QString &projectPath) {
        PluginState st;
        QFile f(projectPath + "/plugins.json");
        if (f.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            auto doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError)
                st.fromJson(doc.object());
            qInfo() << QString("[Plugin] 插件配置已加载：HD Graphics=%1(%2) Dynamic Nature=%3")
                .arg(st.hdGraphics ? "开" : "关", st.hdQuality, st.dynamicNature ? "开" : "关");
        } else {
            qInfo() << "[Plugin] 未找到 plugins.json，使用默认插件状态（HD Graphics + Dynamic Nature）";
            save(projectPath, st);
        }
        return st;
    }

    static bool save(const QString &projectPath, const PluginState &st) {
        QFile f(projectPath + "/plugins.json");
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(st.toJson()).toJson());
        qInfo() << "[Plugin] 插件配置已保存到 plugins.json";
        return true;
    }
};
