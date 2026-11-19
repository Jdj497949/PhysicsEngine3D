#pragma once
// ============ 渲染器工厂（第十阶段：engine_config.ini 驱动的 C++ 工厂模式） ============
// 项目根 engine_config.ini：[Renderer] API = OpenGL / Vulkan / Direct3D
// 缺失时自动生成默认配置；C++ 侧 RendererFactory::create() 按字符串产出后端实例。
#include <QString>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QDebug>

// ---- 后端抽象基类：每种渲染 API 一个实例，init() 返回是否可用 ----
class Backend {
public:
    virtual ~Backend() = default;
    virtual QString name() const = 0;
    virtual bool init(QString &err) = 0;
};

// OpenGL 后端：当前引擎主渲染管线（GL 3.3 Core，全功能可用）
class OpenGLBackend : public Backend {
public:
    QString name() const override { return "OpenGL"; }
    bool init(QString &err) override {
        Q_UNUSED(err);
        return true;   // GL 3.3 Core 由 QOpenGLWidget 上下文保证
    }
};

// Vulkan 后端（预留）：接口就位，初始化返回 false（下一代管线占位）
class VulkanBackend : public Backend {
public:
    QString name() const override { return "Vulkan"; }
    bool init(QString &err) override { err = "Vulkan 后端为预留接口，尚未接入实例"; return false; }
};

// Direct3D 后端（预留）：仅 Windows 世代占位
class Direct3DBackend : public Backend {
public:
    QString name() const override { return "Direct3D"; }
    bool init(QString &err) override { err = "Direct3D 后端为预留接口，尚未接入实例"; return false; }
};

class RendererFactory {
public:
    // 项目根 engine_config.ini 读写（QSettings IniFormat）
    static QString readApi(const QString &projectPath) {
        QString ini = projectPath + "/engine_config.ini";
        if (!QFile::exists(ini)) {
            // 缺省生成默认配置
            QFile f(ini);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write("[Renderer]\nAPI = OpenGL\n");
            qInfo() << "[Renderer] 未找到 engine_config.ini，已生成默认配置（API = OpenGL）";
            return "OpenGL";
        }
        QSettings s(ini, QSettings::IniFormat);
        QString api = s.value("Renderer/API", "OpenGL").toString();
        if (api.compare("Vulkan", Qt::CaseInsensitive) == 0) return "Vulkan";
        if (api.compare("Direct3D", Qt::CaseInsensitive) == 0) return "Direct3D";
        return "OpenGL";
    }

    static bool writeApi(const QString &projectPath, const QString &api) {
        QString ini = projectPath + "/engine_config.ini";
        QSettings s(ini, QSettings::IniFormat);
        s.setValue("Renderer/API", api);
        s.sync();
        qInfo() << "[Renderer] engine_config.ini 已更新：Renderer/API =" << api;
        return QFile::exists(ini);
    }

    // 工厂：按 API 名产出后端实例（多态）
    static Backend *create(const QString &api) {
        if (api.compare("OpenGL", Qt::CaseInsensitive) == 0) return new OpenGLBackend();
        if (api.compare("Vulkan", Qt::CaseInsensitive) == 0) return new VulkanBackend();
        if (api.compare("Direct3D", Qt::CaseInsensitive) == 0) return new Direct3DBackend();
        return nullptr;
    }
};
