#include "GLViewport.h"
#include "ParticleSystem.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolButton>
#include <QHBoxLayout>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QMatrix4x4>
#include <QMimeData>
#include <QDropEvent>
#include <QMenu>
#include <cmath>
#include <vector>
#include <algorithm>

// 材质着色器（共享自 MaterialSystem.h：贴图 + 金属度 + 粗糙度 + Blinn-Phong 高光）
static const char *VSH = MatShader::VSH;
static const char *FSH = MatShader::FSH;

GLViewport::GLViewport(World *w, bool editorViewport, QWidget *parent)
    : QOpenGLWidget(parent), world(w), gizmoEnabled(editorViewport) {
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    if (editorViewport) {
        toolBar = buildToolbar();
        toolBar->raise();
    }
}

QWidget *GLViewport::buildToolbar() {
    auto *bar = new QWidget(this);
    bar->setStyleSheet("background: rgba(20,22,28,200); border-radius:6px;");
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(2);
    const char *names[TOOL_MODE_COUNT] = {"选择(Q)", "移动(W)", "旋转(E)", "缩放(R)", "2D吸附"};
    for (int i = 0; i < TOOL_MODE_COUNT; ++i) {
        auto *b = new QToolButton(bar);
        b->setText(names[i]);
        b->setCheckable(true);
        b->setStyleSheet(
            "QToolButton{color:#ddd;background:transparent;border:none;padding:4px 8px;border-radius:4px;}"
            "QToolButton:checked{background:#3b6db4;color:white;}");
        b->setToolTip(QString("工具：%1").arg(names[i]));
        connect(b, &QToolButton::clicked, this, [this, i]{ setToolMode(ToolMode(i)); });
        toolBtns[i] = b;
        lay->addWidget(b);
    }
    toolBtns[0]->setChecked(true);
    bar->adjustSize();
    bar->move(8, 8);
    return bar;
}

void GLViewport::setToolMode(ToolMode m) {
    world->tool = m;
    for (int i = 0; i < TOOL_MODE_COUNT; ++i)
        if (toolBtns[i]) toolBtns[i]->setChecked(i == int(m));
    static const char *names[TOOL_MODE_COUNT] = {"选择", "移动", "旋转", "缩放", "2D平面吸附"};
    qInfo() << QString("[工具] 模式切换 -> %1").arg(names[int(m)]);
    emit toolChanged(int(m));
    update();
}

GLuint GLViewport::compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

void GLViewport::buildCube(Mesh &m) {
    float s = 0.5f;
    // 每面：pos(3) normal(3) uv(2)，共 8 float
    float v[6][4][8] = {
        // +Z 正面
        {{-s,-s, s, 0,0,1, 0,0}, { s,-s, s, 0,0,1, 1,0}, { s, s, s, 0,0,1, 1,1}, {-s, s, s, 0,0,1, 0,1}},
        // -Z 背面
        {{ s,-s,-s, 0,0,-1, 0,0}, {-s,-s,-s, 0,0,-1, 1,0}, {-s, s,-s, 0,0,-1, 1,1}, { s, s,-s, 0,0,-1, 0,1}},
        // -X 左面
        {{-s,-s,-s, -1,0,0, 0,0}, {-s,-s, s, -1,0,0, 1,0}, {-s, s, s, -1,0,0, 1,1}, {-s, s,-s, -1,0,0, 0,1}},
        // +X 右面
        {{ s,-s, s, 1,0,0, 0,0}, { s,-s,-s, 1,0,0, 1,0}, { s, s,-s, 1,0,0, 1,1}, { s, s, s, 1,0,0, 0,1}},
        // +Y 顶面
        {{-s, s, s, 0,1,0, 0,0}, { s, s, s, 0,1,0, 1,0}, { s, s,-s, 0,1,0, 1,1}, {-s, s,-s, 0,1,0, 0,1}},
        // -Y 底面
        {{-s,-s,-s, 0,-1,0, 0,0}, { s,-s,-s, 0,-1,0, 1,0}, { s,-s, s, 0,-1,0, 1,1}, {-s,-s, s, 0,-1,0, 0,1}},
    };
    GLuint idx[36];
    int p = 0;
    for (int f = 0; f < 6; ++f) {
        int b = f * 4;
        idx[p++]=b; idx[p++]=b+1; idx[p++]=b+2; idx[p++]=b; idx[p++]=b+2; idx[p++]=b+3;
    }
    glGenVertexArrays(1, &m.vao);
    GLuint ebo; glGenBuffers(1, &ebo);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = 36; m.indexed = true;
}

void GLViewport::buildSphere(Mesh &m) {
    const int RINGS = 16, SECTORS = 24;
    std::vector<float> v;
    for (int r = 0; r <= RINGS; ++r) {
        float phi = M_PI * r / RINGS;
        for (int s = 0; s <= SECTORS; ++s) {
            float th = 2 * M_PI * s / SECTORS;
            float x = sin(phi) * cos(th), y = cos(phi), z = sin(phi) * sin(th);
            v.insert(v.end(), {x * 0.5f, y * 0.5f, z * 0.5f, x, y, z,
                               float(s) / SECTORS, 1.0f - float(r) / RINGS});
        }
    }
    std::vector<GLuint> idx;
    for (int r = 0; r < RINGS; ++r) {
        for (int s = 0; s < SECTORS; ++s) {
            int a = r * (SECTORS + 1) + s, b = a + SECTORS + 1;
            idx.insert(idx.end(), {GLuint(a), GLuint(b), GLuint(a + 1), GLuint(b), GLuint(b + 1), GLuint(a + 1)});
        }
    }
    glGenVertexArrays(1, &m.vao);
    GLuint ebo; glGenBuffers(1, &ebo);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = int(idx.size()); m.indexed = true;
}

void GLViewport::buildPlane(Mesh &m) {
    float v[] = {
        -0.5f,0,-0.5f, 0,1,0, 0,1,  0.5f,0,-0.5f, 0,1,0, 1,1,  0.5f,0,0.5f, 0,1,0, 1,0,
        -0.5f,0,-0.5f, 0,1,0, 0,1,  0.5f,0,0.5f, 0,1,0, 1,0,  -0.5f,0,0.5f, 0,1,0, 0,0,
    };
    glGenVertexArrays(1, &m.vao);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = 6; m.indexed = false;
}

void GLViewport::buildGrass(Mesh &m) {
    // 40x40 细分平面：顶点越多，着色器风吹摆动越顺滑（单位 1x1 平面，物体级 scale 撑开）
    const int N = 40;
    std::vector<float> v;
    v.reserve(size_t((N + 1) * (N + 1) * 8));
    for (int r = 0; r <= N; ++r) {
        for (int c = 0; c <= N; ++c) {
            float x = -0.5f + float(c) / N;
            float z = -0.5f + float(r) / N;
            v.insert(v.end(), {x, 0.0f, z, 0.0f, 1.0f, 0.0f, float(c) / N, float(r) / N});
        }
    }
    std::vector<GLuint> idx;
    idx.reserve(size_t(N * N * 6));
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            int a = r * (N + 1) + c, b = a + N + 1;
            idx.insert(idx.end(), {GLuint(a), GLuint(b), GLuint(a + 1), GLuint(b), GLuint(b + 1), GLuint(a + 1)});
        }
    }
    glGenVertexArrays(1, &m.vao);
    GLuint ebo; glGenBuffers(1, &ebo);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = int(idx.size()); m.indexed = true;
}

void GLViewport::buildCylinder(Mesh &m) {
    const int N = 20;
    std::vector<float> v;
    auto push = [&](const QVector3D &p, const QVector3D &n, float u, float vv) {
        v.insert(v.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), u, vv});
    };
    for (int i = 0; i < N; ++i) {
        float a0 = 2 * M_PI * i / N, a1 = 2 * M_PI * (i + 1) / N;
        float u0 = float(i) / N, u1 = float(i + 1) / N;
        QVector3D n0(cos(a0), 0, sin(a0)), n1(cos(a1), 0, sin(a1));
        QVector3D p0l(n0.x(), -0.5f, n0.z()), p0u(n0.x(), 0.5f, n0.z());
        QVector3D p1l(n1.x(), -0.5f, n1.z()), p1u(n1.x(), 0.5f, n1.z());
        push(p0l, n0, u0, 0); push(p1l, n1, u1, 0); push(p1u, n1, u1, 1);
        push(p0l, n0, u0, 0); push(p1u, n1, u1, 1); push(p0u, n0, u0, 1);
        QVector3D cu(0, 0.5f, 0), cd(0, -0.5f, 0);
        push(cu, {0,1,0}, cos(a0)*0.5f+0.5f, sin(a0)*0.5f+0.5f);
        push(p1u, {0,1,0}, cos(a1)*0.5f+0.5f, sin(a1)*0.5f+0.5f);
        push(p0u, {0,1,0}, cos(a0)*0.5f+0.5f, sin(a0)*0.5f+0.5f);
        push(cd, {0,-1,0}, cos(a0)*0.5f+0.5f, sin(a0)*0.5f+0.5f);
        push(p0l, {0,-1,0}, cos(a0)*0.5f+0.5f, sin(a0)*0.5f+0.5f);
        push(p1l, {0,-1,0}, cos(a1)*0.5f+0.5f, sin(a1)*0.5f+0.5f);
    }
    glGenVertexArrays(1, &m.vao);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = int(v.size() / 8); m.indexed = false;
}

void GLViewport::buildCapsule(Mesh &m) {
    const int N = 16, RINGS = 8;
    std::vector<float> v;
    auto push = [&](const QVector3D &p, const QVector3D &n, float u, float vv) {
        v.insert(v.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), u, vv});
    };
    const float H = 0.3f;
    for (int i = 0; i < N; ++i) {
        float a0 = 2 * M_PI * i / N, a1 = 2 * M_PI * (i + 1) / N;
        float u0 = float(i) / N, u1 = float(i + 1) / N;
        for (int r = 0; r < RINGS; ++r) {
            float p0 = M_PI * r / (2 * RINGS), p1 = M_PI * (r + 1) / (2 * RINGS);
            float v0 = 0.5f + 0.5f * float(r) / RINGS, v1 = 0.5f + 0.5f * float(r + 1) / RINGS;
            auto pt = [&](float ph, float th, float yc) {
                return QVector3D(sin(ph) * cos(th), cos(ph) + yc, sin(ph) * sin(th));
            };
            push(pt(p0, a0, H), pt(p0, a0, H).normalized(), u0, v0);
            push(pt(p1, a0, H), pt(p1, a0, H).normalized(), u0, v1);
            push(pt(p1, a1, H), pt(p1, a1, H).normalized(), u1, v1);
            push(pt(p0, a0, H), pt(p0, a0, H).normalized(), u0, v0);
            push(pt(p1, a1, H), pt(p1, a1, H).normalized(), u1, v1);
            push(pt(p0, a1, H), pt(p0, a1, H).normalized(), u1, v0);
            auto pb = [&](float ph, float th, float yc) {
                return QVector3D(sin(ph) * cos(th), -cos(ph) + yc, sin(ph) * sin(th));
            };
            push(pb(p0, a0, -H), pb(p0, a0, -H).normalized(), u0, 0.5f - 0.5f * float(r) / RINGS);
            push(pb(p1, a1, -H), pb(p1, a1, -H).normalized(), u1, 0.5f - 0.5f * float(r + 1) / RINGS);
            push(pb(p1, a0, -H), pb(p1, a0, -H).normalized(), u0, 0.5f - 0.5f * float(r + 1) / RINGS);
            push(pb(p0, a0, -H), pb(p0, a0, -H).normalized(), u0, 0.5f - 0.5f * float(r) / RINGS);
            push(pb(p1, a1, -H), pb(p1, a1, -H).normalized(), u1, 0.5f - 0.5f * float(r + 1) / RINGS);
            push(pb(p0, a1, -H), pb(p0, a1, -H).normalized(), u1, 0.5f - 0.5f * float(r) / RINGS);
        }
        QVector3D n0(cos(a0), 0, sin(a0)), n1(cos(a1), 0, sin(a1));
        QVector3D p0l(n0.x(), -H, n0.z()), p0u(n0.x(), H, n0.z());
        QVector3D p1l(n1.x(), -H, n1.z()), p1u(n1.x(), H, n1.z());
        push(p0l, n0, u0, 0); push(p1l, n1, u1, 0); push(p1u, n1, u1, 1);
        push(p0l, n0, u0, 0); push(p1u, n1, u1, 1); push(p0u, n0, u0, 1);
    }
    glGenVertexArrays(1, &m.vao);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindVertexArray(0);
    m.count = int(v.size() / 8); m.indexed = false;
}

void GLViewport::buildAxis(GLuint &vao, const QVector3D &dir) {
    float vv[12] = {0,0,0, 0,0,0, dir.x()*100.0f, dir.y()*100.0f, dir.z()*100.0f, 0,0,0};
    glGenVertexArrays(1, &vao);
    GLuint vbo; glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vv), vv, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);
}

void GLViewport::buildStars() {
    std::vector<float> pts;
    const int N = 1600;
    for (int i = 0; i < N; ++i) {
        float u = (std::rand() % 2000) / 2000.0f * 2 - 1;      // cos(theta)
        float a = (std::rand() % 2000) / 2000.0f * 2 * M_PI;
        float r = std::sqrt(std::max(0.f, 1 - u * u));
        float rad = 150.0f;
        pts.push_back(r * cos(a) * rad);
        pts.push_back(u * rad);
        pts.push_back(r * sin(a) * rad);
    }
    starCount = N;
    glGenVertexArrays(1, &starVao); glGenBuffers(1, &starVbo);
    glBindVertexArray(starVao);
    glBindBuffer(GL_ARRAY_BUFFER, starVbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(float), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glBindVertexArray(0);
}

void GLViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    GLuint vs = compile(GL_VERTEX_SHADER, VSH);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FSH);
    prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024]; glGetProgramInfoLog(prog, 1024, nullptr, buf);
        qWarning() << "[GL] 着色器链接失败:" << buf;
    }
    locModel = glGetUniformLocation(prog, "uModel");
    locView = glGetUniformLocation(prog, "uView");
    locProj = glGetUniformLocation(prog, "uProj");
    locColor = glGetUniformLocation(prog, "uColor");
    locLit = glGetUniformLocation(prog, "uLit");
    locMetal = glGetUniformLocation(prog, "uMetal");
    locRough = glGetUniformLocation(prog, "uRough");
    locHasTex = glGetUniformLocation(prog, "uHasTex");
    locEye = glGetUniformLocation(prog, "uEye");
    locTex = glGetUniformLocation(prog, "uTex");
    locTime = glGetUniformLocation(prog, "u_Time");
    locWindS = glGetUniformLocation(prog, "u_WindStrength");
    locWindF = glGetUniformLocation(prog, "u_WindFreq");
    locAmb = glGetUniformLocation(prog, "uAmbient");
    locAmbI = glGetUniformLocation(prog, "uAmbientI");
    windClock.start();

    buildCube(meshes[0]);
    buildSphere(meshes[1]);
    buildPlane(meshes[2]);
    buildCylinder(meshes[3]);
    buildCapsule(meshes[4]);
    meshes[5] = meshes[0];   // Camera 占位体（方块）
    meshes[6] = meshes[1];   // Light 占位体（球）
    meshes[7] = meshes[0];   // Text3D：不进 GL 管线，QPainter 公告牌绘制（占位防越界）
    buildGrass(meshes[9]);   // 草地：40x40 细分平面（顶点风吹位移）

    buildAxis(axisVao[0], QVector3D(1, 0, 0));
    buildAxis(axisVao[1], QVector3D(0, 1, 0));
    buildAxis(axisVao[2], QVector3D(0, 0, 1));
    buildStars();

    std::vector<float> lines;
    const float R = 25.0f, STEP = 1.0f;
    for (float x = -R; x <= R + 0.001f; x += STEP) {
        lines.push_back(x); lines.push_back(0); lines.push_back(-R);
        lines.push_back(x); lines.push_back(0); lines.push_back(R);
        lines.push_back(-R); lines.push_back(0); lines.push_back(x);
        lines.push_back(R);  lines.push_back(0); lines.push_back(x);
    }
    gridCount = int(lines.size() / 3);
    glGenVertexArrays(1, &gridVao); glGenBuffers(1, &gridVbo);
    glBindVertexArray(gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &dbgVao); glGenBuffers(1, &dbgVbo);
    glBindVertexArray(dbgVao);
    glBindBuffer(GL_ARRAY_BUFFER, dbgVbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glBindVertexArray(0);
}

void GLViewport::setCubeOutlined(int idx) {
    highlighted = idx;
    update();
}

void GLViewport::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

// ---- 飞行相机：平滑惯性移动 ----
void GLViewport::updateFly(float dt) {
    if (!world->flyMode) return;
    float yr = qDegreesToRadians(world->flyYaw), pr = qDegreesToRadians(world->flyPitch);
    QVector3D fwd(cos(pr) * sin(yr), sin(pr), cos(pr) * cos(yr));
    QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0, 1, 0)).normalized();
    if (right.lengthSquared() < 0.5f) right = QVector3D(1, 0, 0);

    QVector3D want(0, 0, 0);
    const float SPEED = 7.0f;
    if (keys['w']) want += fwd;
    if (keys['s']) want -= fwd;
    if (keys['d']) want += right;
    if (keys['a']) want -= right;
    if (keys['e'] || keys[' ']) want += QVector3D(0, 1, 0);
    if (keys[Qt::Key_Control & 0xff] || keys['q']) want -= QVector3D(0, 1, 0);
    if (want.lengthSquared() > 0) want = want.normalized() * SPEED;

    // 指数平滑：速度逐渐趋近目标，绝不一顿一顿
    float k = 1.0f - std::exp(-8.0f * dt);
    world->flyVel += (want - world->flyVel) * k;
    if (world->flyVel.length() > 0.001f) {
        world->flyPos += world->flyVel * dt;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastMoveLogMs > 500) {
            lastMoveLogMs = now;
            emit cameraMoved(QString("[FlyCam] 相机移动中 pos=(%1, %2, %3) yaw=%4 pitch=%5")
                .arg(world->flyPos.x(), 0, 'f', 2).arg(world->flyPos.y(), 0, 'f', 2).arg(world->flyPos.z(), 0, 'f', 2)
                .arg(world->flyYaw, 0, 'f', 1).arg(world->flyPitch, 0, 'f', 1));
        }
    }
}

bool GLViewport::event(QEvent *ev) {
    return QOpenGLWidget::event(ev);
}

void GLViewport::keyPressEvent(QKeyEvent *e) {
    int k = e->key();
    if (k == Qt::Key_Control || k == Qt::Key_Shift) {
        keys[k & 0xff] = true;
        return;
    }
    char c = (char)QChar::toLower(k & 0xff);
    if (k == Qt::Key_Space) { keys[' '] = true; return; }
    if (c == 'w' || c == 'a' || c == 's' || c == 'd' || c == 'q' || c == 'e' || c == 'r') {
        if (!keys[c]) { heldToolKey = c; keyTimer.start(); }
        keys[c] = true;
        world->flyMode = true;
        update();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void GLViewport::keyReleaseEvent(QKeyEvent *e) {
    int k = e->key();
    if (k == Qt::Key_Control || k == Qt::Key_Shift) { keys[k & 0xff] = false; return; }
    char c = (char)QChar::toLower(k & 0xff);
    if (k == Qt::Key_Space) { keys[' '] = false; return; }
    if (c == 'w' || c == 'a' || c == 's' || c == 'd' || c == 'q' || c == 'e' || c == 'r') {
        keys[c] = false;
        // 快速点按（<250ms）且是 Q/W/E/R -> 切换工具；按住 -> 飞行移动
        if (heldToolKey == c && keyTimer.isValid() && keyTimer.elapsed() < 250) {
            if (c == 'q') setToolMode(ToolMode::Select);
            else if (c == 'w') setToolMode(ToolMode::Translate);
            else if (c == 'e') setToolMode(ToolMode::Rotate);
            else if (c == 'r') setToolMode(ToolMode::Scale);
        }
        heldToolKey = -1;
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

// 演示钩子：模拟按住 WASD 飞行若干毫秒（非阻塞，由帧循环驱动 updateFly）
void GLViewport::demoKeyFly(int key, int ms) {
    keyPressEvent(new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, QString(QChar(key))));
    world->flyMode = true;
    QTimer::singleShot(ms, this, [this, key]{
        keyReleaseEvent(new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier, QString(QChar(key))));
        qInfo() << "[FlyCam] 飞行按键已释放，相机平滑减速停止";
    });
}

// 演示钩子：模拟右键拖拽自由视角
void GLViewport::demoRightDrag(int dx, int dy) {
    mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, QPoint(width()/2, height()/2),
                                    Qt::RightButton, Qt::RightButton, Qt::NoModifier));
    mouseMoveEvent(new QMouseEvent(QEvent::MouseMove, QPoint(width()/2 + dx, height()/2 + dy),
                                   Qt::RightButton, Qt::RightButton, Qt::NoModifier));
    mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, QPoint(width()/2 + dx, height()/2 + dy),
                                      Qt::RightButton, Qt::NoButton, Qt::NoModifier));
}

QPointF GLViewport::project(const QVector3D &wp) const {
    QMatrix4x4 vp = m_proj * m_view;
    QVector4D c = vp * QVector4D(wp, 1.0f);
    if (c.w() <= 0.0001f) return QPointF(-9999, -9999);
    float nx = c.x() / c.w(), ny = c.y() / c.w();
    return QPointF((nx * 0.5f + 0.5f) * width(), (1.0f - (ny * 0.5f + 0.5f)) * height());
}

QVector3D GLViewport::rayFromScreen(const QPoint &p) {
    float nx = p.x() / float(width()) * 2 - 1;
    float ny = 1 - p.y() / float(height()) * 2;
    QMatrix4x4 inv = (m_proj * m_view).inverted();
    QVector4D near4 = inv * QVector4D(nx, ny, -1.0f, 1);
    QVector4D far4 = inv * QVector4D(nx, ny, 1.0f, 1);
    QVector3D nearP(near4.x() / near4.w(), near4.y() / near4.w(), near4.z() / near4.w());
    QVector3D farP(far4.x() / far4.w(), far4.y() / far4.w(), far4.z() / far4.w());
    return (farP - nearP).normalized();
}

int GLViewport::pickObject(const QPoint &p) {
    QVector3D dir = rayFromScreen(p);
    QVector3D origin = world->cameraEye();

    int hit = -1; float bestT = 1e9f;
    for (size_t i = 0; i < world->objects.size(); ++i) {
        const SceneObject &o = world->objects[i];
        QMatrix4x4 wm = worldMatrixOf(int(i), world->objects);
        QVector3D pos = wm.column(3).toVector3D();
        // 简化：用物体 scale 的近似 AABB（旋转后按最长轴保守估计）
        float ext = qMax(o.scale.x(), qMax(o.scale.y(), o.scale.z())) * 0.55f;
        QVector3D half = (o.mesh == MeshType::Plane || o.mesh == MeshType::Grass)
            ? QVector3D(o.scale.x() * 0.5f, 0.1f, o.scale.z() * 0.5f)
            : QVector3D(ext, ext, ext);
        QVector3D mn = pos - half, mx = pos + half;
        float tmin = -1e9f, tmax = 1e9f;
        bool ok = true;
        for (int a = 0; a < 3; ++a) {
            float d = dir[a], ro = origin[a];
            if (qAbs(d) < 1e-6f) { if (ro < mn[a] || ro > mx[a]) { ok = false; break; } continue; }
            float t1 = (mn[a] - ro) / d, t2 = (mx[a] - ro) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = qMax(tmin, t1); tmax = qMin(tmax, t2);
        }
        if (ok && tmax >= tmin && tmin > 0 && tmin < bestT) { bestT = tmin; hit = int(i); }
    }
    qInfo() << QString("[Raycast] 屏幕坐标(%1,%2) -> 射线 origin=(%3) dir=(%4) -> %5")
        .arg(p.x()).arg(p.y())
        .arg(QString("(%1, %2, %3)").arg(origin.x(), 0, 'f', 2).arg(origin.y(), 0, 'f', 2).arg(origin.z(), 0, 'f', 2))
        .arg(QString("(%1, %2, %3)").arg(dir.x(), 0, 'f', 2).arg(dir.y(), 0, 'f', 2).arg(dir.z(), 0, 'f', 2))
        .arg(hit >= 0 ? QString("命中 %1").arg(world->objects[hit].name) : QString("未命中"));
    return hit;
}

QVector3D GLViewport::objectAxis(int objIdx, int axisIdx) const {
    static const QVector3D axes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    const SceneObject &o = world->objects[objIdx];
    return o.rotation.rotatedVector(axes[axisIdx]);
}

int GLViewport::pickGizmoHandle(const QPoint &p, int &axis) {
    if (highlighted < 0 || size_t(highlighted) >= world->objects.size()) return -1;
    QVector3D center = world->objects[highlighted].position;
    const float HANDLE_LEN = 1.4f, TOL = 16.0f;
    QPointF sc = project(center);
    float best = TOL; axis = -1;
    for (int a = 0; a < 3; ++a) {
        QVector3D end = center + objectAxis(highlighted, a) * HANDLE_LEN;
        QPointF se = project(end);
        QPointF d = se - sc;
        float len2 = d.x() * d.x() + d.y() * d.y();
        if (len2 < 1) continue;
        QPointF mp = QPointF(p) - sc;
        float t = QPointF::dotProduct(mp, d) / len2;
        if (t < 0 || t > 1.15f) continue;
        QPointF foot = sc + d * t;
        float dist = std::hypot(foot.x() - p.x(), foot.y() - p.y());
        if (dist < best) { best = dist; axis = a; }
    }
    return axis >= 0 ? 0 : -1;
}

void GLViewport::clickOnObject(int idx) {
    if (idx < 0 || size_t(idx) >= world->objects.size()) return;
    QPointF p = project(world->objects[idx].position);
    if (p.x() < -1000) return;
    QPoint local(int(p.x()), int(p.y()));
    mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, local,
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, local,
                                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
}

void GLViewport::clickGizmoHandle(int idx, int axis, ToolMode withTool) {
    setToolMode(withTool);
    if (highlighted != idx) clickOnObject(idx);
    if (highlighted != idx) return;
    QVector3D c = world->objects[idx].position;
    QPointF p = project(c + objectAxis(idx, axis) * 1.4f);
    QPoint local(int(p.x()), int(p.y()));
    mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, local,
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    QPoint target = local + QPoint(int(60 * (axis == 0 ? 1 : 0)), int(-60 * (axis == 1 ? 1 : 0)));
    mouseMoveEvent(new QMouseEvent(QEvent::MouseMove, target,
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, target,
                                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
}

void GLViewport::mousePressEvent(QMouseEvent *e) {
    lastPos = e->pos();
    setFocus();
    if (e->button() == Qt::RightButton) {
        // Shift 多选后右键：对齐/分布菜单（否则维持自由视角）
        if (!multiSel.isEmpty()) {
            QMenu menu(this);
            QAction *actTop = menu.addAction("顶端对齐");
            QAction *actDistX = menu.addAction("X 轴等距分布");
            QAction *chosen = menu.exec(e->globalPosition().toPoint());
            if (chosen == actTop) { alignTop(); return; }
            if (chosen == actDistX) { distributeX(); return; }
            return;
        }
        rightLooking = true;
        if (charLook) {
            // 角色控制器运行态：鼠标滑动控制视角（Pitch/Yaw），不进入飞行相机
            qInfo() << "[Look] 鼠标滑动视角已启用（拖拽旋转 Pitch/Yaw，角色面朝前进方向）";
        } else {
            world->flyMode = true;   // 右键自由视角
            qInfo() << "[FlyCam] 右键自由视角已启用（拖拽旋转，WASD/QE 平移）";
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // 跨维度 HUD：运行态下 2D 按钮优先于 3D 拾取（不穿透）
    int hb = hudHitTest(e->pos());
    if (hb >= 0) {
        qInfo() << QString("[UI] 命中 HUD 按钮：%1（第 %2 号）").arg(hudItems[size_t(hb)].name).arg(hb);
        emit hudButtonClicked(hb, hudItems[size_t(hb)].name, hudItems[size_t(hb)].script);
        return;
    }

    int axis = -1;
    if (gizmoEnabled && world->tool != ToolMode::Select && highlighted >= 0
        && pickGizmoHandle(e->pos(), axis) >= 0) {
        dragAxis = axis;
        dragStartMouse = e->pos();
        SceneObject &o = world->objects[highlighted];
        dragStartVec = (world->tool == ToolMode::Translate || world->tool == ToolMode::Mode2D) ? o.position
                     : world->tool == ToolMode::Rotate
                     ? QVector3D(o.rotation.toEulerAngles())
                     : o.scale;
        if (world->tool == ToolMode::Translate || world->tool == ToolMode::Mode2D) {
            // 射线-平面拖拽：过物体中心、法线指向相机的平面（轴向量保证在平面上不退化）
            QVector3D axisVec = objectAxis(highlighted, axis);
            dragPlaneNormal = (world->cameraEye() - o.position).normalized();
            if (qAbs(QVector3D::dotProduct(axisVec, dragPlaneNormal)) > 0.97f) {
                dragPlaneNormal = qAbs(axisVec.y()) > 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            }
            dragStartHit = rayPlaneHit(e->pos(), o.position, dragPlaneNormal);
            qInfo() << QString("[Gizmo] 射线-平面拖拽：抓住 %1 的 %2 轴控制柄，起点交点 (%3)")
                .arg(o.name).arg(QChar("XYZ"[axis]))
                .arg(QString("%1, %2, %3").arg(dragStartHit.x(), 0, 'f', 2)
                     .arg(dragStartHit.y(), 0, 'f', 2).arg(dragStartHit.z(), 0, 'f', 2));
        } else {
            // 旋转/缩放沿用屏幕空间投影法
            QPointF c = project(o.position);
            QPointF ept = project(o.position + objectAxis(highlighted, axis) * 1.4f);
            dragAxisDir = ept - c;
            float l = std::hypot(dragAxisDir.x(), dragAxisDir.y());
            if (l > 0.001f) dragAxisDir /= l;
            qInfo() << QString("[Gizmo] 抓住 %1 的控制柄（%2 轴，模式 %3）")
                .arg(o.name).arg(QChar("XYZ"[axis])).arg(int(world->tool));
        }
        return;
    }
    int hit = pickObject(e->pos());
    if (hit >= 0) {
        highlighted = hit;
        selectMulti(hit, bool(e->modifiers() & Qt::ShiftModifier));
        if (e->modifiers() & Qt::ShiftModifier)
            qInfo() << QString("[Multi] Shift 多选：%1（共 %2 个）").arg(world->objects[hit].name).arg(multiSel.size());
        emit selectionChanged(hit);
        update();
    }
}

// ---- 顶端对齐：所有选中物体升到最高点的 Y ----
void GLViewport::alignTop() {
    if (multiSel.size() < 2) return;
    float topY = -FLT_MAX;
    for (int idx : multiSel) {
        const SceneObject &o = world->objects[idx];
        topY = std::max(topY, o.position.y());
    }
    for (int idx : multiSel)
        world->objects[idx].position.setY(topY);
    qInfo() << QString("[Multi] 顶端对齐：%1 个物体，顶部 Y = %2").arg(multiSel.size()).arg(topY, 0, 'f', 2);
    emit selectionChanged(highlighted);
    update();
}

// ---- X 轴等距分布：按 X 排序，两端固定，中间等距 ----
void GLViewport::distributeX() {
    if (multiSel.size() < 3) return;
    QVector<int> sorted = multiSel;
    std::sort(sorted.begin(), sorted.end(), [this](int a, int b) {
        return world->objects[a].position.x() < world->objects[b].position.x();
    });
    float minX = world->objects[sorted.first()].position.x();
    float maxX = world->objects[sorted.last()].position.x();
    float step = (maxX - minX) / float(sorted.size() - 1);
    for (int i = 1; i < sorted.size() - 1; ++i)
        world->objects[sorted[i]].position.setX(minX + step * i);
    qInfo() << QString("[Multi] X 轴等距分布：%1 个物体，间距 %2 米")
        .arg(sorted.size()).arg(step, 0, 'f', 2);
    emit selectionChanged(highlighted);
    update();
}

void GLViewport::mouseMoveEvent(QMouseEvent *e) {
    if (rightLooking) {
        QPoint d = e->pos() - lastPos;
        if (charLook) {
            // 角色视角：Yaw/Pitch 独立存储（角色面朝 Yaw 方向）
            world->charLookYaw += d.x() * 0.25f;
            world->charLookPitch = qBound(-60.0f, world->charLookPitch + d.y() * 0.25f, 60.0f);
        } else {
            world->flyYaw += d.x() * 0.25f;
            world->flyPitch = qBound(-89.0f, world->flyPitch + d.y() * 0.25f, 89.0f);
        }
        lastPos = e->pos();
        update();
        return;
    }
    if (dragAxis >= 0 && highlighted >= 0) {
        SceneObject &o = world->objects[highlighted];
        if (world->tool == ToolMode::Translate || world->tool == ToolMode::Mode2D) {
            // 精准轴向锁死：射线与拖拽平面求交，交点差投影到轴向量
            QVector3D hit = rayPlaneHit(e->pos(), dragStartVec, dragPlaneNormal);
            QVector3D axisVec = objectAxis(highlighted, dragAxis);
            float along = QVector3D::dotProduct(hit - dragStartHit, axisVec);
            o.position = snapPosition(dragStartVec + axisVec * along);
            if (world->tool == ToolMode::Mode2D) {
                QVector3D move = axisVec * along;      // 2D 吸附：只在 XZ 平面移动
                move.setY(0.0f);                       // Y 锁定
                o.position = snapPosition(dragStartVec + move);
            }
            if (snapOn)
                qInfo() << QString("[Snap] 网格吸附：位置吸附到 (%1)，步长 %2 米")
                    .arg(QString("%1, %2, %3").arg(o.position.x(), 0, 'f', 2)
                         .arg(o.position.y(), 0, 'f', 2).arg(o.position.z(), 0, 'f', 2))
                    .arg(snapPosStep, 0, 'f', 2);
            qInfo() << QString("[Gizmo] 射线-平面拖拽：%1 沿 %2 轴移动 %3 -> (%4)")
                .arg(o.name).arg(QChar("XYZ"[dragAxis])).arg(along, 0, 'f', 3)
                .arg(QString("%1, %2, %3").arg(o.position.x(), 0, 'f', 2)
                     .arg(o.position.y(), 0, 'f', 2).arg(o.position.z(), 0, 'f', 2));
        } else {
            QPointF d = QPointF(e->pos()) - dragStartMouse;
            float along = float(QPointF::dotProduct(d, dragAxisDir));
            switch (world->tool) {
            case ToolMode::Rotate: {
                QVector3D eul = dragStartVec;
                eul[dragAxis] = snapRotation(eul[dragAxis] + along * 0.2f);
                if (snapOn)
                    qInfo() << QString("[Snap] 旋转吸附：角度吸附到 %1 度，步长 %2 度")
                        .arg(eul[dragAxis], 0, 'f', 2).arg(snapRotStep, 0, 'f', 2);
                o.rotation = QQuaternion::fromEulerAngles(eul);
                break;
            }
            case ToolMode::Scale: {
                o.scale = dragStartVec;
                float ns = qBound(0.05f, dragStartVec[dragAxis] + along * 0.01f, 200.0f);
                o.scale[dragAxis] = ns;
                break;
            }
            default: break;
            }
        }
        emit selectionChanged(highlighted);
        update();
        return;
    }
    if (e->buttons() & Qt::LeftButton) {
        // 左键拖拽：轨道相机旋转
        QPoint d = e->pos() - lastPos;
        world->camYaw += d.x() * 0.4f;
        world->camPitch = qBound(-85.0f, world->camPitch + d.y() * 0.4f, 85.0f);
        lastPos = e->pos();
        update();
    }
}

void GLViewport::mouseReleaseEvent(QMouseEvent *e) {
    if (rightLooking && e->button() == Qt::RightButton) {
        rightLooking = false;
        return;
    }
    if (dragAxis >= 0) {
        SceneObject &o = world->objects[highlighted];
        if (world->tool == ToolMode::Scale)
            qInfo() << QString("[Gizmo] 结束拖拽，%1 的 Scale = (%2)")
                .arg(o.name).arg(QString("%1, %2, %3").arg(o.scale.x(), 0, 'f', 2)
                     .arg(o.scale.y(), 0, 'f', 2).arg(o.scale.z(), 0, 'f', 2));
        else
            qInfo() << QString("[Gizmo] 结束拖拽，%1 已更新（模式 %2）").arg(o.name).arg(int(world->tool));
        dragAxis = -1;
    }
}

void GLViewport::wheelEvent(QWheelEvent *e) {
    if (world->flyMode) return;   // 飞行模式下滚轮不拉轨道距离
    world->camDist = qBound(3.0f, world->camDist - e->angleDelta().y() / 120.0f, 60.0f);
    update();
}

void GLViewport::paintGL() {
    // 帧时间（平滑飞行用）
    static QElapsedTimer frameTimer;
    if (!frameTimer.isValid()) frameTimer.start();
    float dt = float(frameTimer.elapsed()) / 1000.0f;
    frameTimer.restart();
    dt = qBound(0.001f, dt, 0.1f);
    updateFly(dt);

    // 清屏色由天空系统驱动（晴天/雨/雾/夜各一套底色；星空顶开启时叠加星点）
    if (world->showSkybox)
        glClearColor(world->skyClear.x(), world->skyClear.y(), world->skyClear.z(), 1);
    else
        glClearColor(0.16f, 0.18f, 0.22f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_proj.setToIdentity();
    m_proj.perspective(55.0f, width() / float(height()), 0.1f, 400.0f);
    m_view = viewMatrix();
    QMatrix4x4 proj = m_proj, view = m_view;
    QMatrix4x4 id; id.setToIdentity();

    glUseProgram(prog);
    glUniformMatrix4fv(locView, 1, GL_FALSE, view.constData());
    glUniformMatrix4fv(locProj, 1, GL_FALSE, proj.constData());
    // 全局 uniform：帧时间（风吹相位）+ 天空环境光（VNWeatherStudio .sky 驱动）
    if (locTime >= 0) glUniform1f(locTime, float(windClock.elapsed()) / 1000.0f);
    if (locAmb >= 0) glUniform3f(locAmb, world->skyAmbient.x(), world->skyAmbient.y(), world->skyAmbient.z());
    if (locAmbI >= 0) glUniform1f(locAmbI, world->skyIntensity);

    // ---- 星空顶（先画，关深度写入）----
    if (world->showSkybox) {
        glUniform1i(locLit, 0);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, id.constData());
        glUniform3f(locColor, 1.0f, 1.0f, 0.9f);
        glDepthMask(GL_FALSE);
        glBindVertexArray(starVao);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glPointSize(1.8f);
        glDrawArrays(GL_POINTS, 0, starCount);
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDepthMask(GL_TRUE);
    }

    // ---- 无限网格地面 ----
    glUniform1i(locLit, 0);
    glUniformMatrix4fv(locModel, 1, GL_FALSE, id.constData());
    glUniform3f(locColor, 0.45f, 0.5f, 0.58f);
    glBindVertexArray(gridVao);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, gridCount);

    // ---- 红绿蓝世界坐标轴 ----
    const float axisCol[3][3] = {{1.0f,0.15f,0.15f},{0.2f,1.0f,0.2f},{0.25f,0.45f,1.0f}};
    glLineWidth(3.0f);
    for (int a = 0; a < 3; ++a) {
        glUniform3f(locColor, axisCol[a][0], axisCol[a][1], axisCol[a][2]);
        glBindVertexArray(axisVao[a]);
        glDrawArrays(GL_LINES, 0, 2);
    }
    glLineWidth(1.0f);

    // ---- 物理调试线框（绿色）----
    if (world->physics.drawDebug && !world->physics.debugLines.empty()) {
        glBindVertexArray(dbgVao);
        glBindBuffer(GL_ARRAY_BUFFER, dbgVbo);
        glBufferData(GL_ARRAY_BUFFER, world->physics.debugLines.size() * sizeof(float),
                     world->physics.debugLines.data(), GL_DYNAMIC_DRAW);
        glUniform3f(locColor, 0.1f, 1.0f, 0.2f);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, int(world->physics.debugLines.size() / 3));
    }

    // ---- 场景物体（父子层级：世界矩阵 = 父矩阵 × 局部矩阵；材质系统：贴图+金属度+粗糙度）----
    glUniform1i(locLit, 1);
    glUniform3f(locEye, world->cameraEye().x(), world->cameraEye().y(), world->cameraEye().z());
    glUniform1i(locTex, 0);
    for (size_t i = 0; i < world->objects.size(); ++i) {
        const SceneObject &o = world->objects[i];
        if (o.mesh == MeshType::Text3D) continue;   // 3D 文本走 QPainter 公告牌，无 GL mesh
        if (o.mesh == MeshType::ModelMesh) {        // 外部模型：专属 .vnmodel 管线 + CPU 蒙皮
            ModelGL *mg = ensureModel(o.modelPath);
            if (!mg) continue;
            if (mg->mesh.vao == 0) skinUpload(mg, mg->data.verts);   // 首帧建 VAO（绑定姿态，无骨骼模型也要建）
            if (o.animPlaying && mg->hasAnim) {
                world->objects[i].animTime += dt;
                auto pose = ModelSystem::sample(mg->anim, world->objects[i].animTime);
                auto sk = ModelSystem::skin(mg->data, pose);
                skinUpload(mg, sk);   // 蒙皮后顶点回传 VBO
            }
            QMatrix4x4 mm = worldMatrixOf(int(i), world->objects);
            glUniformMatrix4fv(locModel, 1, GL_FALSE, mm.constData());
            if (int(i) == highlighted) glUniform3f(locColor, 1.0f, 0.85f, 0.2f);
            else glUniform3f(locColor, o.color.x(), o.color.y(), o.color.z());
            glBindVertexArray(mg->mesh.vao);
            glDrawElements(GL_TRIANGLES, mg->mesh.count, GL_UNSIGNED_INT, (void*)nullptr);
            continue;
        }
        const Mesh &m = meshes[int(o.mesh)];
        QMatrix4x4 mm = worldMatrixOf(int(i), world->objects);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, mm.constData());
        // 动态自然：每物体风力（.mat 材质风力优先；插件关闭时归 0）
        float windS = windEnabled ? (o.hasMaterial ? o.material.windStrength : o.windStrength) : 0.0f;
        float windF = o.hasMaterial ? o.material.windFreq : o.windFreq;
        if (locWindS >= 0) glUniform1f(locWindS, windS);
        if (locWindF >= 0) glUniform1f(locWindF, windF);
        // 材质优先：.mat 拖拽应用后的真实渲染
        GLuint tex = 0;
        if (o.hasMaterial) {
            QVector3D c(float(o.material.baseColor.redF()), float(o.material.baseColor.greenF()), float(o.material.baseColor.blueF()));
            if (int(i) == highlighted) glUniform3f(locColor, 1.0f, 0.85f, 0.2f);
            else glUniform3f(locColor, c.x(), c.y(), c.z());
            glUniform1f(locMetal, o.material.metalness);
            glUniform1f(locRough, o.material.roughness);
            tex = texCache.get(this, o.material.texture);
        } else {
            if (int(i) == highlighted) glUniform3f(locColor, 1.0f, 0.85f, 0.2f);
            else glUniform3f(locColor, o.color.x(), o.color.y(), o.color.z());
            glUniform1f(locMetal, 0.0f);
            glUniform1f(locRough, 1.0f);
        }
        glUniform1i(locHasTex, tex ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(m.vao);
        if (m.indexed) glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
        else glDrawArrays(GL_TRIANGLES, 0, m.count);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    // ---- 粒子系统（GL_POINTS 点精灵，加法混合烟花）----
    if (particles) {
        particles->draw(this, m_proj * m_view, 4.0f);
        glUseProgram(prog);   // 粒子管线用独立着色器，画完切回主程序供 Gizmo 使用
    }

    // ---- Gizmo（选中物体，按当前工具模式着色）----
    if (gizmoEnabled && highlighted >= 0 && size_t(highlighted) < world->objects.size()) {
        const SceneObject &o = world->objects[highlighted];
        const float LEN = 1.4f;
        glLineWidth(5.0f);
        QMatrix4x4 wm = worldMatrixOf(highlighted, world->objects);
        QVector3D center = wm.column(3).toVector3D();
        static const QVector3D kAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
        for (int a = 0; a < 3; ++a) {
            QVector3D ax = o.rotation.rotatedVector(kAxes[a]);
            QMatrix4x4 mm;
            mm.translate(center);
            mm.scale(ax * LEN);
            glUniformMatrix4fv(locModel, 1, GL_FALSE, mm.constData());
            glUniform3f(locColor, axisCol[a][0], axisCol[a][1], axisCol[a][2]);
            glBindVertexArray(axisVao[a]);
            glDrawArrays(GL_LINES, 0, 2);
            QMatrix4x4 tip;
            tip.translate(center + ax * LEN);
            tip.scale(QVector3D(0.07f, 0.07f, 0.07f));
            glUniformMatrix4fv(locModel, 1, GL_FALSE, tip.constData());
            glBindVertexArray(meshes[0].vao);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
        glLineWidth(1.0f);
    }

    // ---- 右上角坐标罗盘（跟随相机旋转，永远指示真实世界方向）----
    {
        glDisable(GL_DEPTH_TEST);
        QMatrix4x4 ortho;
        ortho.ortho(0.0f, float(width()), 0.0f, float(height()), -100.0f, 100.0f);
        glUniformMatrix4fv(locProj, 1, GL_FALSE, ortho.constData());
        glUniformMatrix4fv(locView, 1, GL_FALSE, id.constData());
        QMatrix4x4 camRot = view.inverted();           // 相机旋转（世界->相机 的逆）
        QMatrix4x4 compass;
        compass.translate(float(width()) - 70.0f, float(height()) - 70.0f, 0.0f);
        compass.scale(0.42f, 0.42f, 0.42f);
        compass = compass * camRot;
        glUniformMatrix4fv(locModel, 1, GL_FALSE, compass.constData());
        glUniform1i(locLit, 0);
        glLineWidth(3.0f);
        for (int a = 0; a < 3; ++a) {
            glUniform3f(locColor, axisCol[a][0], axisCol[a][1], axisCol[a][2]);
            glBindVertexArray(axisVao[a]);
            glDrawArrays(GL_LINES, 0, 2);
        }
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        // 恢复主投影
        glUniformMatrix4fv(locProj, 1, GL_FALSE, proj.constData());
        glUniformMatrix4fv(locView, 1, GL_FALSE, view.constData());
    }
    glUniform1i(locLit, 1);
    glBindVertexArray(0);

    // ============ QPainter 2D 叠加层：3D 文本公告牌 + 跨维度 HUD ============
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // ---- 点光源 / 聚光灯光晕（原生组件，挂上即亮；夜晚环境光弱时光晕更醒目）----
        for (size_t i = 0; i < world->objects.size(); ++i) {
            const SceneObject &o = world->objects[i];
            for (const auto &c : o.components) {
                bool isPt = c.type == "PointLight", isSpot = c.type == "SpotLight";
                if (!isPt && !isSpot) continue;
                QMatrix4x4 wm = worldMatrixOf(int(i), world->objects);
                QPointF sc = project(wm.column(3).toVector3D());
                if (sc.x() < -1000) break;
                // 颜色：组件 color 参数（#rrggbb），解析失败用暖黄
                QColor lc = QColor(c.params.value("color").toString("#ffcf9e"));
                if (!lc.isValid()) lc = QColor(255, 207, 158);
                float radius = isPt ? float(c.params.value("radius").toDouble(6.0)) : 4.0f;
                float intensity = float(c.params.value("intensity").toDouble(2.5));
                // 屏幕光晕半径：随相机距离衰减（近大远小）
                QVector3D eye = world->cameraEye();
                float dist = (wm.column(3).toVector3D() - eye).length();
                float px = qBound(40.0f, radius * 900.0f / qMax(2.0f, dist), 900.0f);
                // 恐怖氛围：灯泡轻微闪烁（sin 高频抖动 ±8%）
                float flicker = 1.0f + 0.08f * float(std::sin(double(i) * 7.3 + QDateTime::currentMSecsSinceEpoch() * 0.006));
                float a = qBound(30.0f, intensity * 90.0f * flicker, 200.0f);
                // 环境光越暗，光晕越突出（夜晚模式 alpha 提升）
                if (world->nightMode) a = qMin(255.0f, a * 1.45f);
                // 三层径向渐变：核心亮斑 -> 光晕 -> 消散
                for (auto [k, alpha] : {std::pair<float,float>{1.0f, a}, {0.55f, a * 0.45f}, {0.25f, a * 0.18f}}) {
                    float r = px * k;
                    QRadialGradient g(sc, r);
                    QColor c0 = lc; c0.setAlpha(int(alpha));
                    QColor c1 = lc; c1.setAlpha(0);
                    g.setColorAt(0, c0);
                    g.setColorAt(1, c1);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g);
                    p.drawEllipse(sc, r, r);
                }
                // 光源本体亮斑（灯泡）
                QColor core = lc; core.setAlpha(235);
                p.setPen(QColor(255, 255, 255, 200));
                p.setBrush(core);
                p.drawEllipse(sc, qMax(3.0, px * 0.06), qMax(3.0, px * 0.06));
                if (isSpot) {
                    // 聚光灯：内外锥角标注线（沿视线方向两条张角线）
                    float inner = float(c.params.value("innerCone").toDouble(18.0));
                    float outer = float(c.params.value("outerCone").toDouble(35.0));
                    QPen pen(lc); pen.setWidthF(1.4);
                    pen.setStyle(Qt::DashLine);
                    p.setPen(pen);
                    double baseAng = -90.0;
                    for (double ang : {baseAng - outer, baseAng - inner, baseAng + inner, baseAng + outer}) {
                        double rad = qDegreesToRadians(ang);
                        p.drawLine(sc, sc + QPointF(std::cos(rad) * px, std::sin(rad) * px) * 0.9);
                    }
                    p.setPen(Qt::NoPen);
                }
            }
        }

        // ---- 3D 文本（Text Mesh）公告牌：世界坐标投影到屏幕，带半透明背板 ----
        for (size_t i = 0; i < world->objects.size(); ++i) {
            const SceneObject &o = world->objects[i];
            if (o.mesh != MeshType::Text3D) continue;
            QMatrix4x4 wm = worldMatrixOf(int(i), world->objects);
            QPointF sc = project(wm.column(3).toVector3D());
            if (sc.x() < -1000) continue;
            QFont f(o.textFont.isEmpty() ? "Noto Sans CJK SC" : o.textFont, 20);
            f.setBold(true);
            QFontMetrics fm(f);
            QRectF br = fm.boundingRect(o.text);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(10, 12, 22, 175));
            p.drawRoundedRect(QRectF(sc.x() - br.width()/2.0 - 10, sc.y() - br.height() - 16, br.width() + 20, br.height() + 14), 6, 6);
            QColor tc(int(o.color.x() * 255), int(o.color.y() * 255), int(o.color.z() * 255));
            p.setPen(tc);
            p.setFont(f);
            p.drawText(QPointF(sc.x() - br.width()/2.0, sc.y() - 9), o.text);
        }

        // ---- 跨维度 HUD：3D 画面上层的 2D UI ----
        drawHUDItems(p);
        p.end();
    }
}

// ============ 材质球拖拽应用（资产面板 .mat -> 视口物体） ============
void GLViewport::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) {
        QString path = e->mimeData()->urls().isEmpty()
            ? e->mimeData()->text() : e->mimeData()->urls().first().toLocalFile();
        if (path.endsWith(".mat")) e->acceptProposedAction();
    }
}

void GLViewport::dropEvent(QDropEvent *e) {
    QString path;
    if (!e->mimeData()->urls().isEmpty()) path = e->mimeData()->urls().first().toLocalFile();
    else path = e->mimeData()->text();
    if (!path.endsWith(".mat")) return;
    // 落点射线拾取：拖到哪个物体上就应用给谁
    int hit = pickObject(e->position().toPoint());
    if (hit < 0) {
        qInfo() << QString("[材质] 拖拽 %1 未命中任何物体").arg(QFile(path).fileName());
        return;
    }
    applyMatDrop(hit, path);
    e->acceptProposedAction();
}

void GLViewport::applyMatDrop(int objIdx, const QString &matPath) {
    if (objIdx < 0 || size_t(objIdx) >= world->objects.size()) return;
    Material m;
    if (!Material::load(matPath, m)) {
        qInfo() << QString("[材质] 错误：无法读取材质文件 %1").arg(matPath);
        return;
    }
    SceneObject &o = world->objects[size_t(objIdx)];
    o.hasMaterial = true;
    o.material = m;
    qInfo() << QString("[材质] %1 已应用材质 '%2'：颜色(%3,%4,%5) 金属度=%6 粗糙度=%7 贴图=%8")
        .arg(o.name, QFileInfo(matPath).fileName())
        .arg(m.baseColor.redF(), 0, 'f', 2).arg(m.baseColor.greenF(), 0, 'f', 2).arg(m.baseColor.blueF(), 0, 'f', 2)
        .arg(m.metalness, 0, 'f', 2).arg(m.roughness, 0, 'f', 2)
        .arg(m.texture.isEmpty() ? "无" : QFileInfo(m.texture).fileName());
    emit materialDropped(objIdx, matPath);
    update();
}

// ============ 射线-平面求交（Gizmo 精准拖拽的数学核心） ============
QVector3D GLViewport::rayPlaneHit(const QPoint &p, const QVector3D &planePt, const QVector3D &normal) {
    QVector3D dir = rayFromScreen(p);
    QVector3D eye = world->cameraEye();
    float denom = QVector3D::dotProduct(dir, normal);
    if (qAbs(denom) < 1e-5f) return planePt;    // 射线与平面平行：交点退化为平面点
    float t = QVector3D::dotProduct(planePt - eye, normal) / denom;
    return eye + dir * t;
}

// ============ 跨维度 HUD ============
void GLViewport::setHUD(const std::vector<HudItem> &items) {
    hudItems = items;
    qInfo() << QString("[UI] HUD 装载完成：%1 个元素").arg(items.size());
    update();
}

// 设计空间(1600x900)矩形 -> 当前视口屏幕矩形（含锚点解析）
QRectF GLViewport::hudScreenRect(const HudItem &it, double sx, double sy) const {
    double x = it.rect.x() * sx, y = it.rect.y() * sy;
    double w = it.rect.width() * sx, h = it.rect.height() * sy;
    if (it.anchor == 1) x = (width() - w) / 2.0 + it.rect.x() * sx;          // 居中（x 为偏移）
    else if (it.anchor == 2) x = width() - w - it.rect.x() * sx;             // 右下
    if (it.anchor == 2) y = height() - h - it.rect.y() * sy;
    return QRectF(x, y, w, h);
}

int GLViewport::hudHitTest(const QPoint &p) {
    if (!hudOn || hudItems.empty()) return -1;
    double sx = width() / 1600.0, sy = height() / 900.0;
    for (int i = int(hudItems.size()) - 1; i >= 0; --i) {
        if (hudItems[size_t(i)].type != "button") continue;   // 只有按钮可点
        if (hudScreenRect(hudItems[size_t(i)], sx, sy).contains(QPointF(p))) return i;
    }
    return -1;
}

void GLViewport::drawHUDItems(QPainter &p) {
    if (!hudOn || hudItems.empty()) return;
    double sx = width() / 1600.0, sy = height() / 900.0;
    for (const HudItem &it : hudItems) {
        QRectF r = hudScreenRect(it, sx, sy);
        if (it.type == "button") {
            // 半透明面板按钮：圆角底 + 描边 + 居中文字
            p.setPen(QPen(QColor(120, 190, 255, 230), 2));
            p.setBrush(QColor(30, 70, 130, 200));
            p.drawRoundedRect(r, 8, 8);
            QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(10, int(it.fontSize * sy)));
            f.setBold(true);
            p.setFont(f);
            p.setPen(it.color);
            p.drawText(r, Qt::AlignCenter, it.text);
        } else if (it.type == "text") {
            QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(10, int(it.fontSize * sy)));
            f.setBold(true);
            p.setFont(f);
            p.setPen(it.color);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, it.text);
        } else if (it.type == "image") {
            QImage img(it.imagePath);
            if (!img.isNull()) p.drawImage(r, img);
            else { p.setPen(QColor(255, 120, 120)); p.drawText(r, Qt::AlignCenter, "图片缺失"); }
        }
    }
}

// 演示钩子：走真实点击判定路径（与用户点击完全同一条管线）
void GLViewport::demoClickHUD(int idx) {
    if (idx < 0 || size_t(idx) >= hudItems.size()) return;
    double sx = width() / 1600.0, sy = height() / 900.0;
    QRectF r = hudScreenRect(hudItems[size_t(idx)], sx, sy);
    QPoint c(int(r.center().x()), int(r.center().y()));
    mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
}

// 演示钩子：按住 Gizmo 轴向拖拽（真实鼠标事件管线 + 射线-平面数学）
void GLViewport::demoGizmoDrag(int idx, int axis, ToolMode withTool, int dx, int dy) {
    setToolMode(withTool);
    if (highlighted != idx) clickOnObject(idx);
    if (highlighted != idx) return;
    QVector3D c = world->objects[size_t(idx)].position;
    QPointF p = project(c + objectAxis(idx, axis) * 1.4f);
    QPoint local(int(p.x()), int(p.y()));
    mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, local,
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    QPoint target = local + QPoint(dx, dy);
    mouseMoveEvent(new QMouseEvent(QEvent::MouseMove, target,
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, target,
                                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
}

// ============ 外部模型（.vnmodel）专属渲染管线 ============
QString GLViewport::animLog;

std::map<QString, GLViewport::ModelGL *> &GLViewport::modelCache() {
    // 关键：VAO/VBO 属于各自 OpenGL 上下文（Scene / Game 两个视口各一份），
    // 缓存必须按上下文隔离，否则第二个视口拿到别人上下文的 VAO 直接段错误
    static std::map<const void *, std::map<QString, ModelGL *>> cacheByCtx;
    return cacheByCtx[QOpenGLContext::currentContext()];
}

// 数据装载（不建 GL 对象：GL 资源在 paintGL 首帧上下文内惰性创建）
GLViewport::ModelGL *GLViewport::ensureModel(const QString &vnmodelPath) {
    auto &cache = modelCache();
    auto it = cache.find(vnmodelPath);
    if (it != cache.end()) return it->second;
    auto *mg = new ModelGL;
    if (!ModelSystem::loadVNModel(vnmodelPath, mg->data)) {
        qInfo() << QString("[Model] 错误：无法读取模型 %1").arg(vnmodelPath);
        cache[vnmodelPath] = nullptr;
        return nullptr;
    }
    cache[vnmodelPath] = mg;
    qInfo() << QString("[Model] 模型装载完成：%1（顶点 %2 三角 %3 骨骼 %4）")
        .arg(QFileInfo(vnmodelPath).fileName())
        .arg(mg->data.verts.size() / 6).arg(mg->data.indices.size() / 3).arg(mg->data.bones.size());
    return mg;
}

// 把顶点数组（pos+normal 6 float）写成交错 8 float（补 uv=0）上传 VBO；首帧建 VAO/EBO
void GLViewport::skinUpload(ModelGL *mg, const std::vector<float> &posn) {
    size_t vc = posn.size() / 6;
    std::vector<float> interleaved(vc * 8);
    for (size_t v = 0; v < vc; ++v) {
        interleaved[v * 8] = posn[v * 6];
        interleaved[v * 8 + 1] = posn[v * 6 + 1];
        interleaved[v * 8 + 2] = posn[v * 6 + 2];
        interleaved[v * 8 + 3] = posn[v * 6 + 3];
        interleaved[v * 8 + 4] = posn[v * 6 + 4];
        interleaved[v * 8 + 5] = posn[v * 6 + 5];
        // uv=0
    }
    if (mg->mesh.vao == 0) {
        glGenVertexArrays(1, &mg->mesh.vao);
        GLuint ebo; glGenBuffers(1, &ebo);
        glGenBuffers(1, &mg->vbo);
        glBindVertexArray(mg->mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mg->vbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mg->data.indices.size() * sizeof(unsigned),
                     mg->data.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
        glBindVertexArray(0);
        mg->mesh.count = int(mg->data.indices.size());
        mg->mesh.indexed = true;
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, mg->vbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_DYNAMIC_DRAW);
    }
}

// 播放动画：装载 .vnanim 并让物体进入播放状态（Lua / 插座 / 属性面板 共用）
void GLViewport::playAnim(SceneObject &o, const QString &vnanimPath) {
    ModelGL *mg = ensureModel(o.modelPath);
    if (!mg) return;
    VNAnim a;
    if (!ModelSystem::loadVNAnim(vnanimPath, a)) {
        qInfo() << QString("[Anim] 错误：无法读取动画 %1").arg(vnanimPath);
        return;
    }
    mg->anim = a;
    mg->hasAnim = true;
    o.animPath = vnanimPath;
    o.animName = a.name;
    o.animPlaying = true;
    o.animTime = 0.0f;
    animLog = QString("[Anim] 播放动画：%1 -> 模型 %2（时长 %3s，%4 条轨道）")
        .arg(a.name, QFileInfo(vnanimPath).fileName()).arg(a.duration).arg(a.tracks.size());
    qInfo().noquote() << animLog;
}
