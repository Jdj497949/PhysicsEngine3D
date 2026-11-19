#pragma once
// ============ 粒子系统（GL_POINTS 实时渲染，CPU 模拟 + GPU 点精灵）============
// 独立顶点着色器：位置+颜色逐粒子属性；加法混合；池上限 2000，远超 QGraphicsItem 方案
#include <QVector3D>
#include <QColor>
#include <QString>
#include <QElapsedTimer>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>

struct ParticleParams {
    QString name = "粒子系统";
    float emitRate = 80.0f;        // 发射速率（个/秒）
    float gravity = -3.0f;         // 重力影响（米/秒^2）
    float initSpeed = 2.0f;        // 初始速度（米/秒）
    float lifetime = 1.6f;         // 生命周期（秒）
    QColor colorStart{255, 210, 90};   // 颜色渐变：起点
    QColor colorEnd{255, 60, 30};      // 颜色渐变：终点
    bool burstMode = false;        // 烟花模式：周期性瞬间爆发
    float burstEvery = 1.3f;       // 爆发间隔（秒）
    int burstCount = 450;          // 每次爆发粒子数
};

class ParticleSystem {
public:
    ParticleParams params;
    QVector3D origin{0, 1.5f, 0};

    static constexpr int MAX_P = 2000;
    std::vector<QVector3D> pos, vel;
    std::vector<float> life, maxLife;
    std::vector<QVector3D> col;          // 出生时随机颜色（烟花的"随机闪烁"）
    float acc = 0.0f, clockT = 0.0f;

    ParticleSystem() { pos.reserve(MAX_P); vel.reserve(MAX_P); life.reserve(MAX_P); maxLife.reserve(MAX_P); col.reserve(MAX_P); }
    bool empty() const { return pos.empty(); }
    size_t count() const { return pos.size(); }

    void presetFirework() {
        params = ParticleParams{};
        params.name = "烟花特效 (Firework)";
        params.burstMode = true;
        params.burstEvery = 1.3f;
        params.burstCount = 450;
        params.gravity = -3.5f;
        params.initSpeed = 3.2f;
        params.lifetime = 1.7f;
        origin = QVector3D(0, 1.2f, 0);
    }

    void spawnOne() {
        if (pos.size() >= size_t(MAX_P)) return;
        float a = float(std::rand()) / RAND_MAX * 2 * float(M_PI);
        float up = 0.25f + 0.65f * float(std::rand()) / RAND_MAX;
        float r = (0.35f + 0.65f * float(std::rand()) / RAND_MAX) * params.initSpeed;
        vel.push_back({r * cosf(a), r * up + params.initSpeed * 0.45f, r * sinf(a)});
        pos.push_back(origin);
        maxLife.push_back(params.lifetime * (0.7f + 0.6f * float(std::rand()) / RAND_MAX));
        life.push_back(maxLife.back());
        // 随机亮色（HSV 均匀取色相，烟花感）
        QColor c = QColor::fromHsv(int(std::rand() % 360), 220, 255);
        col.push_back({float(c.redF()), float(c.greenF()), float(c.blueF())});
    }
    void spawnBurst() {
        for (int i = 0; i < params.burstCount; ++i) spawnOne();
    }

    void tick(float dt) {
        clockT += dt;
        if (params.burstMode) {
            if (clockT >= params.burstEvery) { clockT = 0; spawnBurst(); }
        } else {
            acc += dt * params.emitRate;
            while (acc >= 1.0f) { acc -= 1.0f; spawnOne(); }
        }
        // 积分 + 回收
        for (size_t i = 0; i < pos.size(); ) {
            life[i] -= dt;
            if (life[i] <= 0) {
                pos[i] = pos.back(); vel[i] = vel.back();
                life[i] = life.back(); maxLife[i] = maxLife.back(); col[i] = col.back();
                pos.pop_back(); vel.pop_back(); life.pop_back(); maxLife.pop_back(); col.pop_back();
                continue;
            }
            vel[i].setY(vel[i].y() + params.gravity * dt);
            vel[i] *= (1.0f - 0.8f * dt);      // 微弱空气阻力
            pos[i] += vel[i] * dt;
            ++i;
        }
    }

    // ---- GL 资源（惰性创建：vao/vbo/prog）----
    GLuint vao = 0, vbo = 0, prog = 0;
    GLint locPV = -1, locSize = -1;
    bool glReady = false;

    void ensureGL(QOpenGLFunctions_3_3_Core *gl) {
        if (glReady) return;
        static const char *VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;
uniform mat4 uPV; uniform float uSize;
out vec4 vCol;
void main(){ vCol = aCol; gl_Position = uPV * vec4(aPos, 1.0); gl_PointSize = uSize; })";
        static const char *FS = R"(
#version 330 core
in vec4 vCol; out vec4 FragColor;
void main(){
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float a = smoothstep(1.0, 0.15, length(d));
    FragColor = vec4(vCol.rgb, vCol.a * a); })";
        GLuint vs = gl->glCreateShader(GL_VERTEX_SHADER);
        gl->glShaderSource(vs, 1, &VS, nullptr); gl->glCompileShader(vs);
        GLuint fs = gl->glCreateShader(GL_FRAGMENT_SHADER);
        gl->glShaderSource(fs, 1, &FS, nullptr); gl->glCompileShader(fs);
        prog = gl->glCreateProgram();
        gl->glAttachShader(prog, vs); gl->glAttachShader(prog, fs); gl->glLinkProgram(prog);
        gl->glDeleteShader(vs); gl->glDeleteShader(fs);
        locPV = gl->glGetUniformLocation(prog, "uPV");
        locSize = gl->glGetUniformLocation(prog, "uSize");
        gl->glGenVertexArrays(1, &vao);
        gl->glGenBuffers(1, &vbo);
        gl->glBindVertexArray(vao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // 交错缓冲：pos(3) + color(4) = 7 float/粒子；动态更新
        gl->glBufferData(GL_ARRAY_BUFFER, MAX_P * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        gl->glEnableVertexAttribArray(0); gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (void*)0);
        gl->glEnableVertexAttribArray(1); gl->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, (void*)12);
        gl->glBindVertexArray(0);
        glReady = true;
    }

    void draw(QOpenGLFunctions_3_3_Core *gl, const QMatrix4x4 &pv, float sizePx) {
        if (pos.empty()) return;
        ensureGL(gl);
        // 上传顶点数据（出生色 × 生命周期渐隐 -> 颜色向终点渐变）
        static std::vector<float> buf;
        buf.clear();
        for (size_t i = 0; i < pos.size(); ++i) {
            float t = 1.0f - life[i] / maxLife[i];
            QVector3D c(col[i].x() + (params.colorEnd.redF() - col[i].x()) * t,
                        col[i].y() + (params.colorEnd.greenF() - col[i].y()) * t,
                        col[i].z() + (params.colorEnd.blueF() - col[i].z()) * t);
            float alpha = 1.0f - t;
            buf.insert(buf.end(), {pos[i].x(), pos[i].y(), pos[i].z(), c.x(), c.y(), c.z(), alpha});
        }
        gl->glUseProgram(prog);
        gl->glUniformMatrix4fv(locPV, 1, GL_FALSE, pv.constData());
        gl->glUniform1f(locSize, sizePx);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);       // 加法混合（烟花辉光感）
        gl->glDepthMask(GL_FALSE);
        gl->glBindVertexArray(vao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, buf.size() * sizeof(float), buf.data());
        gl->glDrawArrays(GL_POINTS, 0, int(pos.size()));
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
        gl->glBindVertexArray(0);
        gl->glUseProgram(0);
    }
};
