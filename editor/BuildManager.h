#pragma once
// ============ BuildManager：防破解堡垒打包类（死命令 1 + 死命令 2） ============
// 目录结构树全部由 QDir/QFile 在代码里动态生成（绝不手动建文件夹）：
//   MyGame_Release/
//    ├─ GameRuntime.exe / config.ini / engine_settings.json / 5 个根 DLL
//    ├─ system_libs/（4 个第二层依赖库）
//    ├─ platforms/（qwindows.dll）
//    ├─ shaders/（17 个碎片着色器）
//    ├─ configs/（4 个配置文件）
//    └─ data/（assets 全量 5~10KB 分块 AES-256 加密 + 干扰块 + lang_zh_cn.dat）
// DLL 层由 MinGW-w64 现场编译为真实 PE 格式；启动器本身静态编译，可脱离它们独立运行。
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <QProcess>
#include <QRegularExpression>
#include <QByteArray>
#include <random>
#include "../common/Aes256.h"

class BuildManager {
public:
    struct Result {
        bool ok = false;
        int chunkFiles = 0, decoyFiles = 0, shaderFiles = 0, dllFiles = 0, totalData = 0;
        qint64 dataBytes = 0;
        QString root;
    };

    static Result buildFortress(const QString &projectPath, const QString &product,
                                const QString &platform, const QString &keyStr,
                                const QString &builtExe, QStringList &log) {
        Result r;
        const QString root = projectPath + "/MyGame_Release";
        // ---- 1. 目录骨架（QDir 动态生成） ----
        for (const QString &d : {root, root + "/system_libs", root + "/platforms",
                                 root + "/shaders", root + "/configs", root + "/data"})
            QDir().mkpath(d);
        r.root = root;
        log << QString("[Fortress] 目录骨架已生成（QDir 动态创建）：%1").arg(root);

        // ---- 2. 主运行程序 ----
        const QString exeName = (platform == "Windows") ? "GameRuntime.exe" : "GameRuntime";
        if (QFileInfo::exists(builtExe)) {
            QFile::remove(root + "/" + exeName);
            if (QFile::copy(builtExe, root + "/" + exeName))
                log << QString("[Fortress] 主运行程序就位：%1（%2 KB，真实可执行）")
                        .arg(exeName).arg(QFileInfo(root + "/" + exeName).size() / 1024);
        } else {
            log << "[Fortress] 警告：平台编译产物缺失，GameRuntime 未就位";
        }

        // ---- 3. 根配置：config.ini + engine_settings.json ----
        writeFile(root + "/config.ini", R"INI(; 主配置 —— 由 BuildManager 自动生成
[Display]
Width = 1600
Height = 900
Fullscreen = false
VSync = true

[Renderer]
API = OpenGL 3.3
ShadowQuality = PCF-4tap
Anisotropy = 8

[Game]
ProductName = )INI" + product + "\n");
        writeFile(root + "/engine_settings.json", R"JSON({
  "engine": "PhysicsEngine3D",
  "version": "1.2.0",
  "physics": { "fixedRateHz": 60, "gravity": [0.0, -9.81, 0.0], "solverIterations": 10 },
  "render":  { "backend": "opengl33", "maxLights": 16, "shadowMapSize": 2048 },
  "audio":   { "mixRateHz": 48000, "channels": 2 }
})JSON");
        log << "[Fortress] config.ini + engine_settings.json 已写入（分辨率/渲染器/物理步长/重力）";

        // ---- 4. DLL 层（MinGW 现场编译真实 PE 格式） ----
        const QString tmpDir = projectPath + "/build/fortress_tmp";
        QDir().mkpath(tmpDir);
        int dllOk = 0;
        for (const QString &name : {"Qt6Core", "Qt6Gui", "opengl32sw", "steam_api", "vcruntime140"})
            if (makeDll(tmpDir, root, name)) ++dllOk;
        for (const QString &name : {"zlib1", "libcrypto-3-x64", "d3dcompiler_47", "core_engine"})
            if (makeDll(tmpDir, root + "/system_libs", name)) ++dllOk;
        if (makeDll(tmpDir, root + "/platforms", "qwindows")) ++dllOk;
        r.dllFiles = dllOk;
        log << QString("[Fortress] DLL 层完成：%1 个（根 5 + system_libs 4 + platforms 1，MinGW 编译 PE 格式）").arg(dllOk);

        // ---- 5. shaders：17 个碎片着色器 ----
        writeShaders(root + "/shaders", r.shaderFiles);
        log << QString("[Fortress] 着色器碎片已拆分：%1 个 .vert/.frag").arg(r.shaderFiles);

        // ---- 6. configs：4 个配置文件 ----
        writeFile(root + "/configs/audio_settings.xml", R"XML(<?xml version="1.0" encoding="UTF-8"?>
<audio>
  <mixer master="0.9" bgm="0.8" sfx="1.0" ambient="0.6"/>
  <reverb room="hall" wet="0.22"/>
  <channels doppler="true" hrtf="false"/>
</audio>
)XML");
        writeFile(root + "/configs/keybindings.cfg", R"CFG(bind w        move_forward
bind s        move_back
bind a        move_left
bind d        move_right
bind space    jump
bind ctrl     crouch
bind shift    sprint
bind e        interact
bind f        flashlight
bind esc      pause
)CFG");
        writeFile(root + "/configs/render_pipeline.xml", R"XML(<?xml version="1.0" encoding="UTF-8"?>
<pipeline>
  <pass name="opaque"  target="gbuffer" shader="vertex_matrix+fragment_albedo"/>
  <pass name="shadow"  target="shadowmap" shader="shadow_map" light="directional"/>
  <pass name="lighting" target="hdr" shader="light_calc" mode="forward"/>
  <pass name="fog"     target="hdr" shader="fog_effect" blend="alpha"/>
  <pass name="post"    target="backbuffer" chain="tonemap_reinhard,gamma_correct"/>
</pipeline>
)XML");
        writeFile(root + "/configs/debug_flags.json", R"JSON({
  "drawWireframe": false, "showColliders": false, "showFps": true,
  "logPhysics": false, "breakOnNan": true, "logToConsole": true
})JSON");
        log << "[Fortress] 配置迷宫就位：audio_settings.xml / keybindings.cfg / render_pipeline.xml / debug_flags.json";

        // ---- 7. data：assets 5~10KB 分块 AES-256 + 干扰块 + lang_zh_cn.dat ----
        buildDataPit(root + "/data", projectPath + "/assets", keyStr, r);
        log << QString("[Fortress] 数据坑填满：assets 分块 %1 块 + 干扰块 %2 + 中文语言包 1 = %3 个加密文件（共 %4 KB）")
                .arg(r.chunkFiles).arg(r.decoyFiles).arg(r.totalData).arg(r.dataBytes / 1024);
        r.ok = (r.totalData >= 30) && r.chunkFiles > 0;
        if (!r.ok) log << "[Fortress] 错误：data/ 文件数不足 30，打包逻辑需要重查";
        return r;
    }

private:
    static void writeFile(const QString &path, const QString &content) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(content.toUtf8());
            f.close();
        }
    }

    // MinGW 现场编译真实 PE 格式 DLL（导出一个随机种子函数，文件内容互不相同）
    static bool makeDll(const QString &tmpDir, const QString &outDir, const QString &name) {
        const QString cPath = QString("%1/%2_src.c").arg(tmpDir, name);
        const quint32 seed = quint32(QDateTime::currentMSecsSinceEpoch()) + quint32(qHash(name));
        QString ident = name;                        // C 标识符不允许 '-'：libcrypto-3-x64 → libcrypto_3_x64
        ident.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
        QFile c(cPath);
        if (!c.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        c.write(QString("__declspec(dllexport) int VN_%1_Seed(void) { return 0x%2u; }\n"
                        "__declspec(dllexport) const char *VN_%1_Tag(void)  { return \"PhysicsEngine3D\"; }\n"
                        "int VN_%1_Entry(void) { return VN_%1_Seed(); }\n")
                    .arg(ident, QString::number(seed % 0x7FFFFFFF, 16).toUpper()).toUtf8());
        c.close();
        const QString dllPath = QString("%1/%2.dll").arg(outDir, name);
        QFile::remove(dllPath);
        return QProcess::execute("sh", {"-c", QString("x86_64-w64-mingw32-gcc -shared '%1' -o '%2' 2>/dev/null")
                                            .arg(cPath, dllPath)}) == 0 && QFileInfo::exists(dllPath);
    }

    // 17 个碎片着色器（真实 GLSL 片段，极度拆分）
    static void writeShaders(const QString &dir, int &count) {
        struct { const char *name; const char *code; } shaders[] = {
        {"vertex_matrix.vert", "uniform mat4 u_Model, u_VP;\nin vec3 aPos;\nvoid main(){ vec4 w = u_Model*vec4(aPos,1.0); gl_Position = u_VP*w; }\n"},
        {"vertex_pos.frag",    "vec3 toWorld(vec3 p, mat4 m){ return (m*vec4(p,1.0)).xyz; }\n"},
        {"light_calc.frag",    "float lambert(vec3 n, vec3 l, vec3 ld){ return max(dot(normalize(n), normalize(l-ld)), 0.0); }\n"},
        {"shadow_map.vert",    "uniform mat4 u_LightVP, u_Model;\nin vec3 aPos;\nvoid main(){ gl_Position = u_LightVP*u_Model*vec4(aPos,1.0); }\n"},
        {"fog_effect.frag",    "float fogExp(float dist, float density){ return 1.0 - exp(-dist*density); }\n"},
        {"vertex_normal.vert", "vec3 transformNormal(mat4 m, vec3 n){ return normalize(mat3(transpose(inverse(m)))*n); }\n"},
        {"vertex_uv.vert",     "out vec2 vUV;\nin vec2 aUV;\nvoid main(){ vUV = aUV; }\n"},
        {"vertex_skinned.vert","vec3 skin(vec3 p, vec4 w, mat4 b0, mat4 b1){ return (b0*p).xyz*w.x + (b1*p).xyz*w.y; }\n"},
        {"fragment_albedo.frag","uniform sampler2D u_Albedo;\nin vec2 vUV;\nvec3 albedo(){ return texture(u_Albedo, vUV).rgb; }\n"},
        {"light_phong.frag",   "vec3 phong(vec3 n,vec3 v,vec3 l,vec3 ld,float sh){ vec3 h=normalize(l+v); return ld*(sh*pow(max(dot(n,h),0.0),32.0)); }\n"},
        {"light_blinn.frag",   "vec3 blinn(vec3 n,vec3 l,vec3 v,float r){ vec3 h=normalize(l+v); return vec3(pow(max(dot(n,h),0.0), r*128.0)); }\n"},
        {"light_atten.frag",   "float attenuate(float d, float range){ float k = clamp(1.0 - d/range, 0.0, 1.0); return k*k; }\n"},
        {"shadow_pcf.frag",    "float pcf(sampler2D sm, vec3 uv, vec2 texel){ float s=0.0; for(int i=-1;i<=1;++i) for(int j=-1;j<=1;++j) s += step(uv.z, texture(sm, uv.xy+vec2(i,j)*texel).r); return s/9.0; }\n"},
        {"fog_exp2.frag",      "float fogExp2(float dist, float density){ float f = dist*density; return 1.0 - exp(-f*f); }\n"},
        {"tonemap_reinhard.frag","vec3 reinhard(vec3 c){ return c/(vec3(1.0)+c); }\n"},
        {"gamma_correct.frag", "vec3 gamma(vec3 c, float g){ return pow(clamp(c,0.0,1.0), vec3(1.0/g)); }\n"},
        {"noise_grain.frag",   "float grain(vec2 uv, float t){ return fract(sin(dot(uv+t, vec2(12.9898,78.233)))*43758.5453); }\n"}};
        count = 0;
        for (const auto &s : shaders) { writeFile(dir + "/" + s.name, s.code); ++count; }
    }

    // data/ 数据坑：assets 全量分块 AES-256 加密 + 干扰块 + 单独加密的中文语言包
    static void buildDataPit(const QString &dataDir, const QString &assetsDir,
                             const QString &keyStr, Result &r) {
        QByteArray key(32, 0);
        QByteArray ks = keyStr.isEmpty() ? QByteArray("vn-horror-2026") : keyStr.toUtf8();
        for (int i = 0; i < qMin(32, ks.size()); ++i) key[i] = ks.at(i);

        std::mt19937 rng(uint32_t(QDateTime::currentMSecsSinceEpoch()) & 0xFFFFFFFFu);
        const QStringList exts = {"pak", "dat", "vnpkg", "bin", "idx", "res", "tex", "sfx", "geo", "ani", "tbl", "fnt"};
        QSet<QString> used;
        auto randName = [&]() -> QString {
            for (;;) {
                QString hex;
                static const char *hexc = "0123456789abcdef";
                for (int i = 0; i < 6; ++i) hex += hexc[rng() % 16];
                QString n = hex + "." + exts[rng() % exts.size()];
                if (!used.contains(n)) { used.insert(n); return n; }
            }
        };
        auto writeChunk = [&](const QByteArray &plain) -> QString {
            QByteArray iv(8, 0);
            for (int i = 0; i < 8; ++i) iv[i] = char(rng() & 0xFF);
            QByteArray enc = Aes256::crypt(key, iv, plain);
            const QString name = randName();
            QFile f(dataDir + "/" + name);
            if (f.open(QIODevice::WriteOnly)) {   // 头：魔数 + IV8 + 原始长度4（解包校验用）
                quint32 plen = quint32(plain.size());
                f.write("VNCK01", 6);
                f.write(iv.constData(), 8);
                f.write(reinterpret_cast<const char *>(&plen), 4);
                f.write(enc.constData(), enc.size());
                f.close();
                r.dataBytes += qint64(18 + enc.size());
            }
            return name;
        };

        // 7a. 遍历 assets：每个资源切成 5~10KB 小块，逐块 AES-256 加密，随机名写入 data/
        QDir dir(assetsDir);
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
            QFile in(fi.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly)) continue;
            QByteArray all = in.readAll();
            qsizetype pos = 0;
            while (pos < all.size()) {
                int chunk = 5000 + int(rng() % 5001);          // 5KB ~ 10KB
                QByteArray piece = all.mid(int(pos), chunk);
                writeChunk(piece);
                ++r.chunkFiles;
                pos += piece.size();
            }
        }

        // 7b. 干扰块（加密随机噪声，至少补足 30 个文件）
        while (r.chunkFiles + r.decoyFiles < 30) {
            int sz = 4096 + int(rng() % 32768);
            QByteArray noise(sz, 0);
            for (int i = 0; i < sz; ++i) noise[i] = char(rng() & 0xFF);
            writeChunk(noise);
            ++r.decoyFiles;
        }

        // 7c. 中文语言包：单独提取 + 单独加密
        const QString langJson = R"JSON({
  "menu.start": "开始游戏", "menu.settings": "设置", "menu.quit": "退出",
  "ui.save": "保存", "ui.loading": "加载中…", "ui.paused": "已暂停",
  "tip.flashlight": "按 F 打开手电筒", "tip.run": "按住 Shift 奔跑"
})JSON";
        const QString langName = writeChunk(langJson.toUtf8());
        QFile::remove(dataDir + "/lang_zh_cn.dat");
        QFile::rename(dataDir + "/" + langName, dataDir + "/lang_zh_cn.dat");  // 语言包固定名
        r.totalData = r.chunkFiles + r.decoyFiles + 1;
    }
};
