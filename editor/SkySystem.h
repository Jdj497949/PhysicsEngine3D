#pragma once
// ============ 天空与天气系统（第十阶段：VNWeatherStudio 的 .sky 文件格式） ============
// .sky = JSON：天气状态（晴/雨/雾/夜）+ 环境光颜色/强度 + 天空盒贴图槽
// applyToWorld() 把配置应用到 World：清屏色、环境光 uniform、星空开关。
#include <QString>
#include <QColor>
#include <QVector3D>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include "World.h"

struct SkyConfig {
    QString weather = "晴天";
    QVector3D ambient{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    QString skybox;               // 天空盒图片路径（空 = 无）
    QString name = "新天空配置";

    static bool save(const QString &path, const SkyConfig &c) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        QJsonObject j{
            {"kind", "sky"},
            {"name", c.name},
            {"weather", c.weather},
            {"ambient", QJsonArray{c.ambient.x(), c.ambient.y(), c.ambient.z()}},
            {"intensity", c.intensity},
            {"skybox", c.skybox},
        };
        f.write(QJsonDocument(j).toJson());
        return true;
    }

    static bool load(const QString &path, SkyConfig &out) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        auto j = doc.object();
        out.name = j.value("name").toString("天空配置");
        out.weather = j.value("weather").toString("晴天");
        auto a = j.value("ambient").toArray();
        if (a.size() >= 3) out.ambient = QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
        out.intensity = float(j.value("intensity").toDouble(1.0));
        out.skybox = j.value("skybox").toString();
        sanitize(out);
        return true;
    }

    // 数值体检：环境光/强度超范围或非有限值 -> 回退默认值（防脏数据污染渲染管线）
    static void sanitize(SkyConfig &c) {
        auto safeCh = [](float v) { return (std::isfinite(v) && v >= 0.0f && v <= 5.0f) ? v : 1.0f; };
        c.ambient = QVector3D(safeCh(c.ambient.x()), safeCh(c.ambient.y()), safeCh(c.ambient.z()));
        c.intensity = (std::isfinite(c.intensity) && c.intensity >= 0.0f && c.intensity <= 10.0f) ? c.intensity : 1.0f;
        if (!QFile::exists(c.skybox)) c.skybox.clear();   // 天空盒路径不存在 -> 回退程序渐变
        static const QStringList known = {"晴天", "雨天", "大雾", "夜晚"};
        if (!known.contains(c.weather)) c.weather = "晴天";
    }

    // 应用到 3D 场景：天气预设 + 用户微调的环境光（VNWeatherStudio 保存后拖入即生效）
    void applyToWorld(World &w) const {
        w.applyWeather(weather);
        // 用户在天气编辑器里调过的环境光/强度覆盖预设值
        w.skyAmbient = ambient;
        w.skyIntensity = intensity;
        qInfo().noquote() << QString("[Sky] .sky 已应用：天气=%1 环境光=(%2,%3,%4) 强度=%5")
            .arg(weather).arg(ambient.x(), 0, 'f', 2).arg(ambient.y(), 0, 'f', 2)
            .arg(ambient.z(), 0, 'f', 2).arg(intensity, 0, 'f', 2);
    }
};
