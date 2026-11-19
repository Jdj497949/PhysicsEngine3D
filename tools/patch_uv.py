#!/usr/bin/env python3
# 一次性改写 GLViewport.cpp 的网格构建函数：全部加 UV（32字节顶点）
import re, io, os

P = "/root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D/editor/GLViewport.cpp"
src = open(P, encoding="utf-8").read()

start = src.index("void GLViewport::buildCube")
end = src.index("void GLViewport::buildAxis")
new_builders = r'''void GLViewport::buildCube(Mesh &m) {
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

'''
src = src[:start] + new_builders + src[end:]
open(P, "w", encoding="utf-8").write(src)
print("网格构建函数已全部加 UV")
