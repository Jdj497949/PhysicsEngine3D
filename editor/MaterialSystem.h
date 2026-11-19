#pragma once
// ============ 材质系统（3DEditor 与 VNMateriaLab 共享） ============
// .mat 文件 = JSON：{"name", "baseColor":[r,g,b], "metalness", "roughness", "texture"}
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <map>

struct Material {
    QString name = "新材质球";
    QColor baseColor = QColor(200, 200, 200);      // 基础颜色
    float metalness = 0.0f;                        // 金属度 0..1
    float roughness = 0.5f;                        // 粗糙度 0..1
    QString texture;                               // 贴图路径（空=无贴图）
    // 动态自然（顶点风吹特效）：强度=摆幅（米），频率=rad/s
    float windStrength = 0.0f;
    float windFreq = 2.0f;

    static Material fromJson(const QJsonObject &j) {
        Material m;
        m.name = j.value("name").toString("材质球");
        auto c = j.value("baseColor").toArray();
        if (c.size() >= 3) m.baseColor = QColor::fromRgbF(c.at(0).toDouble(), c.at(1).toDouble(), c.at(2).toDouble());
        m.metalness = float(j.value("metalness").toDouble(0.0));
        m.roughness = float(j.value("roughness").toDouble(0.5));
        m.texture = j.value("texture").toString();
        m.windStrength = float(j.value("windStrength").toDouble(0.0));
        m.windFreq = float(j.value("windFreq").toDouble(2.0));
        return m;
    }
    QJsonObject toJson() const {
        return QJsonObject{
            {"name", name},
            {"baseColor", QJsonArray{baseColor.redF(), baseColor.greenF(), baseColor.blueF()}},
            {"metalness", metalness},
            {"roughness", roughness},
            {"texture", texture},
            {"windStrength", windStrength},
            {"windFreq", windFreq},
        };
    }
    static bool save(const QString &path, const Material &m) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(m.toJson()).toJson());
        return true;
    }
    static bool load(const QString &path, Material &out) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
        out = fromJson(doc.object());
        out.name = QFileInfo(path).fileName();
        return true;
    }
};

// 纹理缓存（每个 QOpenGLWidget 上下文各自一套，用法：上传 QImage 后返回 GL 纹理名）
class TextureCache {
public:
    // 返回纹理名；0 = 无
    GLuint get(QOpenGLFunctions_3_3_Core *gl, const QString &path) {
        if (path.isEmpty()) return 0;
        auto it = cache.find(path);
        if (it != cache.end()) return it->second;
        QImage img(path);
        if (img.isNull()) return 0;
        GLuint tex = 0;
        gl->glGenTextures(1, &tex);
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, img.mirrored(false, true).bits());
        gl->glGenerateMipmap(GL_TEXTURE_2D);
        cache[path] = tex;
        return tex;
    }
    // 直接上传一张内存图（无外部文件），例如程序生成的演示纹理
    GLuint put(QOpenGLFunctions_3_3_Core *gl, const QString &key, const QImage &img) {
        if (img.isNull()) return 0;
        GLuint tex = get(gl, key);
        if (tex) return tex;
        gl->glGenTextures(1, &tex);
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, img.mirrored(false, true).bits());
        gl->glGenerateMipmap(GL_TEXTURE_2D);
        cache[key] = tex;
        return tex;
    }
    void clear() { cache.clear(); }
private:
    std::map<QString, GLuint> cache;
};

// 通用 PBR 风格着色器（Blinn-Phong 近似：金属度调镜面反射颜色，粗糙度调高光衰减）
namespace MatShader {
inline const char *VSH = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel, uView, uProj;
// 动态自然：顶点级风吹特效（u_Time 驱动 sin 波，X/Z 双轴摆动）
uniform float u_Time;
uniform float u_WindStrength;
uniform float u_WindFreq;
out vec3 vPos; out vec3 vNormal; out vec2 vUV;
void main(){
    vec4 wp = uModel * vec4(aPos, 1.0);
    if (u_WindStrength > 0.0) {
        float ph = wp.x * 1.2 + wp.z * 1.2;                       // 沿平面传播的相位（波浪感）
        wp.x += sin(u_Time * u_WindFreq + ph) * u_WindStrength;
        wp.z += cos(u_Time * u_WindFreq * 0.83 + ph) * u_WindStrength * 0.6;
        wp.y += sin(u_Time * u_WindFreq * 0.6 + wp.z * 2.0) * u_WindStrength * 0.35;
    }
    vPos = wp.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * vec4(vPos,1.0);
})";

inline const char *FSH = R"(
#version 330 core
in vec3 vPos; in vec3 vNormal; in vec2 vUV;
uniform vec3 uColor; uniform bool uLit;
uniform float uMetal; uniform float uRough; uniform bool uHasTex; uniform vec3 uEye;
uniform sampler2D uTex;
// 天气/天空系统：环境光颜色与强度（VNWeatherStudio .sky 驱动）
uniform vec3 uAmbient;
uniform float uAmbientI;
out vec4 FragColor;
void main(){
    if(!uLit){ FragColor = vec4(uColor,1.0); return; }
    vec3 base = uColor;
    if(uHasTex) base *= texture(uTex, vUV).rgb;
    vec3 n = normalize(vNormal);
    vec3 amb2 = uAmbient * uAmbientI;
    vec3 ambient = vec3(0.34,0.34,0.36) * (1.0 - uMetal*0.55) * amb2;
    vec3 L = normalize(vec3(-0.45,-1.0,-0.35));
    float diff = max(dot(n,-L),0.0);
    vec3 back = vec3(0.12) * max(dot(n, vec3(0.5,0.2,0.8)),0.0) * amb2;
    // Blinn-Phong 高光：粗糙度 -> 指数；金属度 -> 高光带基色
    vec3 V = normalize(uEye - vPos);
    vec3 H = normalize(V - L);
    float shininess = mix(8.0, 96.0, clamp(1.0-uRough,0.0,1.0));
    float spec = pow(max(dot(n,H),0.0), shininess) * (1.0-uRough);
    vec3 specCol = mix(vec3(1.0), base, uMetal) * spec;
    FragColor = vec4(base*(ambient + diff*0.95*max(amb2, vec3(0.12))) + back + specCol, 1.0);
})";
}
