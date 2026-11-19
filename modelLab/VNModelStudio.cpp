// VNModelStudio —— 第七个程序：模型骨骼动画工作室。
// 左：骨骼树 | 中：模型视口（蒙皮渲染 + 骨骼线）| 右：骨骼属性/关键帧/播放 | 底：时间轴
// 复用 editor/ModelSystem.h 的 VNModel / VNAnim / sample / skin（与 3DEditor 同一条数据管线）
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QMainWindow>
#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>
#include <QDebug>
#include <QStyle>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QToolBar>
#include <QDateTime>
#include <cmath>

#include "../editor/ModelSystem.h"

static const char *MS_VSH = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
out vec3 vPos; out vec3 vNormal;
void main(){
    vPos = vec3(uModel * vec4(aPos,1.0));
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uProj * uView * vec4(aPos,1.0);
})";

static const char *MS_FSH = R"(
#version 330 core
in vec3 vPos; in vec3 vNormal;
uniform vec3 uColor; uniform bool uLit;
out vec4 FragColor;
void main(){
    if(!uLit){ FragColor = vec4(uColor,1.0); return; }
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(vec3(-0.4,-1.0,-0.3))), 0.0);
    FragColor = vec4(uColor * (0.35 + 0.65*diff), 1.0);
})";

// ============ 模型视口：蒙皮网格 + 骨骼线 ============
class ModelGLView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    VNModel model;
    VNAnim anim;
    bool hasAnim = false;
    bool playing = true;
    float animTime = 0.0f;
    float pitch = -20.0f, yaw = 35.0f, dist = 6.0f;
    std::vector<float> skinned;        // 蒙皮后顶点（CPU 蒙皮）
    std::vector<unsigned char> joints; // 与 model.joints 共享
    std::vector<float> weights;
    int selBone = -1;                  // 选中骨骼
    float manualRotDeg = 0.0f;         // 手动旋转（打关键帧用）
    std::vector<float> boneLines;      // 骨骼线段（每线段 6 float，模型空间）

    ModelGLView(QWidget *parent = nullptr) : QOpenGLWidget(parent) {}

    void refreshMesh() { uploadMesh(); }   // 供外部窗口刷新蒙皮网格

    void loadModelFile(const QString &vnmodelPath, const QString &vnanimPath) {
        if (!vnmodelPath.isEmpty())
            ModelSystem::loadVNModel(vnmodelPath, model);
        if (!vnanimPath.isEmpty() && QFile(vnanimPath).exists())
            hasAnim = ModelSystem::loadVNAnim(vnanimPath, anim);
        rebuildSkeletonFromBind();
        selBone = -1;
    }

    // 骨骼线：每根骨骼一根线（父原点 -> 骨骼平移），按当前姿态计算
    void updateBoneLines(const std::map<QString, QQuaternion> &pose) {
        boneLines.clear();
        for (const auto &b : model.bones) {
            QVector3D p0(0, 0, 0), p1 = b.translation;
            if (b.parent >= 0 && size_t(b.parent) < model.bones.size())
                p0 = model.bones[size_t(b.parent)].translation;
            // 手动/采样姿态下旋转端点
            auto it = pose.find(b.name);
            if (it != pose.end()) p1 = it->second.rotatedVector(p1);
            if (b.parent >= 0 && size_t(b.parent) < model.bones.size()) {
                auto pit = pose.find(model.bones[size_t(b.parent)].name);
                if (pit != pose.end()) p0 = pit->second.rotatedVector(p0);
            }
            boneLines.push_back(p0.x()); boneLines.push_back(p0.y()); boneLines.push_back(p0.z());
            boneLines.push_back(p1.x()); boneLines.push_back(p1.y()); boneLines.push_back(p1.z());
        }
    }

    void rebuildSkeletonFromBind() {
        // 初始姿态：采样 t=0（有动画）或绑定姿态
        std::map<QString, QQuaternion> pose;
        if (hasAnim && !anim.tracks.empty()) pose = ModelSystem::sample(anim, 0.0f);
        skinned = ModelSystem::skin(model, pose);
        joints = model.joints;
        weights = model.weights;
        updateBoneLines(pose);
    }

protected:
    GLuint vao = 0, vbo = 0, ebo = 0, lineVao = 0, lineVbo = 0;
    GLuint prog = 0, lineProg = 0;
    bool glReady = false;
    GLsizei indexCount = 0;

    GLuint makeProg(const char *, const char *) = delete;   // 占位：实际用 lambda compile/link
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);

        auto compile = [this](GLenum type, const char *src) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) { char lg[512]; glGetShaderInfoLog(s, 512, nullptr, lg); qWarning() << "[Model] 着色器编译失败:" << lg; }
            return s;
        };
        auto link = [&](const char *v, const char *f) {
            GLuint p = glCreateProgram();
            GLuint vs = compile(GL_VERTEX_SHADER, v), fs = compile(GL_FRAGMENT_SHADER, f);
            glAttachShader(p, vs); glAttachShader(p, fs);
            glLinkProgram(p);
            GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) { char lg[512]; glGetProgramInfoLog(p, 512, nullptr, lg); qWarning() << "[Model] 链接失败:" << lg; }
            return p;
        };
        prog = link(MS_VSH, MS_FSH);
        lineProg = link(MS_VSH, MS_FSH);
        glReady = true;
        uploadMesh();
    }

    void uploadMesh() {
        if (!glReady) return;
        if (skinned.empty()) rebuildSkeletonFromBind();
        if (model.verts.empty()) return;
        // 蒙皮后顶点交错的法线用原法线（演示管线：位置蒙皮，法线保持绑定姿态）
        std::vector<float> data;
        size_t nv = skinned.size() / 3;
        data.resize(nv * 6);
        for (size_t i = 0; i < nv; ++i) {
            data[i*6+0] = skinned[i*3+0]; data[i*6+1] = skinned[i*3+1]; data[i*6+2] = skinned[i*3+2];
            data[i*6+3] = model.verts[i*6+3]; data[i*6+4] = model.verts[i*6+4]; data[i*6+5] = model.verts[i*6+5];
        }
        if (!vao) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glGenBuffers(1, &ebo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(model.indices.size() * sizeof(unsigned)), model.indices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
        } else {
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(data.size() * sizeof(float)), data.data());
        }
        indexCount = GLsizei(model.indices.size());

        // 骨骼线
        if (!lineVao) {
            glGenVertexArrays(1, &lineVao);
            glGenBuffers(1, &lineVbo);
        }
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(std::max(size_t(1), boneLines.size()) * sizeof(float)),
                     boneLines.empty() ? nullptr : boneLines.data(), GL_DYNAMIC_DRAW);
        if (!boneLines.empty()) {
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
        }
        glBindVertexArray(0);
        update();
    }

    void paintGL() override {
        glClearColor(0.075f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!glReady || model.verts.empty()) return;

        QMatrix4x4 proj, view, modelM;
        float ar = float(width()) / qMax(1, height());
        proj.perspective(45.0f, ar, 0.1f, 100.0f);
        QVector3D eye(dist * cos(qDegreesToRadians(pitch)) * sin(qDegreesToRadians(yaw)),
                      dist * sin(qDegreesToRadians(pitch)),
                      dist * cos(qDegreesToRadians(pitch)) * cos(qDegreesToRadians(yaw)));
        view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));
        // 模型居中：包围盒中心平移到原点
        QVector3D c = model.bboxCenter();
        modelM.translate(-c);

        // 网格
        glUseProgram(prog);
        setUniform(prog, "uProj", proj);
        setUniform(prog, "uView", view);
        setUniform(prog, "uModel", modelM);
        GLint litLoc = glGetUniformLocation(prog, "uLit");
        glUniform1i(litLoc, 1);
        GLint colLoc = glGetUniformLocation(prog, "uColor");
        glUniform3f(colLoc, 0.45f, 0.72f, 0.95f);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);

        // 骨骼线（无光照橙色）
        if (!boneLines.empty()) {
            glUseProgram(lineProg);
            setUniform(lineProg, "uProj", proj);
            setUniform(lineProg, "uView", view);
            setUniform(lineProg, "uModel", modelM);
            glUniform1i(glGetUniformLocation(lineProg, "uLit"), 0);
            glUniform3f(glGetUniformLocation(lineProg, "uColor"), 1.0f, 0.62f, 0.15f);
            glBindVertexArray(lineVao);
            glDrawArrays(GL_LINES, 0, GLsizei(boneLines.size() / 3));
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void setUniform(GLuint p, const char *name, const QMatrix4x4 &m) {
        glUniformMatrix4fv(glGetUniformLocation(p, name), 1, GL_FALSE, m.constData());
    }

    void mousePressEvent(QMouseEvent *e) override { lastPos = e->pos(); }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton) {
            yaw += (e->pos().x() - lastPos.x());
            pitch += (e->pos().y() - lastPos.y());
            pitch = qBound(-89.0f, pitch, 89.0f);
            lastPos = e->pos();
            update();
        }
    }
    void wheelEvent(QWheelEvent *e) override {
        dist = qBound(2.0f, dist - e->angleDelta().y() / 240.0f, 30.0f);
        update();
    }

signals:
    void frameTick();

private:
    QPoint lastPos;
};

// ============ 主窗口 ============
class ModelStudioWindow;   // 动捕模拟器回调宿主（后置定义）
class MocapSimDialog;

// 动捕骨骼名（与 stickman.vnmodel 骨骼一一对应）
static const char *MOCAP_BONES[] = {"hip", "spine", "leftArm", "rightArm", "leftLeg", "rightLeg"};
static const char *MOCAP_LABELS[] = {"骨盆 (hip)", "脊柱 (spine)", "左臂 (leftArm)",
                                     "右臂 (rightArm)", "左腿 (leftLeg)", "右腿 (rightLeg)"};

class ModelStudioWindow : public QMainWindow {
    Q_OBJECT
public:
    ModelGLView *view = nullptr;
    QTreeWidget *boneTree = nullptr;
    QListWidget *keyList = nullptr;
    QSlider *timeSlider = nullptr;
    QLabel *boneLabel = nullptr;
    QSlider *rotSlider = nullptr;
    QString modelPath, animPath;

    ModelStudioWindow() {
        resize(1500, 900);
        view = new ModelGLView;

        // 顶：工具栏（含【设备捕捉 Motion Capture】）
        auto *tb = addToolBar("tools");
        tb->setMovable(false);
        auto *actMocap = tb->addAction(style()->standardIcon(QStyle::SP_ComputerIcon), "设备捕捉 (Motion Capture)");
        connect(actMocap, &QAction::triggered, this, &ModelStudioWindow::openMocap);

        // 左：骨骼树
        auto *leftBox = new QWidget;
        auto *lv = new QVBoxLayout(leftBox);
        lv->setContentsMargins(4, 8, 4, 4);
        lv->addWidget(new QLabel("骨骼层级 (Hierarchy)"));
        boneTree = new QTreeWidget;
        boneTree->setHeaderLabel("骨骼 / Bones");
        boneTree->setStyleSheet("QTreeWidget{background:#1b1f26; color:#dfe6ef; border:none; font-size:13px;}");
        lv->addWidget(boneTree, 1);
        leftBox->setFixedWidth(240);

        // 右：骨骼属性
        auto *rightBox = new QWidget;
        rightBox->setFixedWidth(280);
        auto *rv = new QVBoxLayout(rightBox);
        rv->setContentsMargins(8, 8, 8, 4);
        auto *rtitle = new QLabel("属性 (Properties)");
        rtitle->setStyleSheet("font-weight:bold; font-size:14px;");
        rv->addWidget(rtitle);
        boneLabel = new QLabel("未选中骨骼");
        boneLabel->setStyleSheet("color:#7fdcff; font-size:14px;");
        rv->addWidget(boneLabel);
        rv->addWidget(new QLabel("旋转角度（X 轴，度）："));
        rotSlider = new QSlider(Qt::Horizontal);
        rotSlider->setRange(-90, 90);
        rv->addWidget(rotSlider);
        auto *rotVal = new QLabel("0°");
        connect(rotSlider, &QSlider::valueChanged, this, [this, rotVal](int v){
            rotVal->setText(QString("%1°").arg(v));
            view->manualRotDeg = float(v);
            refreshPose();
        });
        rv->addWidget(rotVal);
        auto *btnKey = new QPushButton("打关键帧 (K)");
        connect(btnKey, &QPushButton::clicked, this, &ModelStudioWindow::addKeyframe);
        rv->addWidget(btnKey);
        auto *btnPlay = new QPushButton("播放 / 暂停");
        connect(btnPlay, &QPushButton::clicked, this, [this, btnPlay]{
            view->playing = !view->playing;
            qInfo() << QString("[Anim] %1").arg(view->playing ? "播放动画" : "暂停动画");
        });
        rv->addWidget(btnPlay);
        auto *btnSave = new QPushButton("保存 .vnanim");
        connect(btnSave, &QPushButton::clicked, this, &ModelStudioWindow::saveAnim);
        rv->addWidget(btnSave);
        rv->addWidget(new QLabel("关键帧列表："));
        keyList = new QListWidget;
        rv->addWidget(keyList, 1);

        // 底：时间轴
        auto *bottom = new QWidget;
        auto *bl = new QVBoxLayout(bottom);
        bl->setContentsMargins(8, 2, 8, 4);
        auto *tl = new QHBoxLayout;
        tl->addWidget(new QLabel("时间轴"));
        timeSlider = new QSlider(Qt::Horizontal);
        timeSlider->setRange(0, 1200);   // 1.2 秒 x 1000
        tl->addWidget(timeSlider, 1);
        bl->addLayout(tl);
        connect(timeSlider, &QSlider::valueChanged, this, [this](int v){
            view->animTime = float(v) / 1000.0f;
            if (!view->playing) refreshPose();
        });

        auto *central = new QWidget;
        auto *h = new QHBoxLayout(central);
        h->addWidget(leftBox);
        h->addWidget(view, 1);
        h->addWidget(rightBox);
        auto *outer = new QWidget;
        auto *ov = new QVBoxLayout(outer);
        ov->setContentsMargins(0, 0, 0, 0);
        ov->addWidget(central, 1);
        ov->addWidget(bottom);
        setCentralWidget(outer);

        connect(boneTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur, QTreeWidgetItem*){
            if (!cur) return;
            view->selBone = cur->data(0, Qt::UserRole).toInt();
            const VNModel::Bone &b = view->model.bones[size_t(view->selBone)];
            boneLabel->setText(QString("骨骼：%1（父：%2）")
                .arg(b.name, b.parent >= 0 ? view->model.bones[size_t(b.parent)].name : "根"));
            rotSlider->setValue(0);
            view->manualRotDeg = 0;
            qInfo() << QString("[Model] 选中骨骼：%1").arg(b.name);
        });

        // 动画帧循环
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]{
            if (!view->playing || !view->hasAnim) return;
            view->animTime += 1.0f / 60.0f;
            if (view->animTime > view->anim.duration) view->animTime -= view->anim.duration;
            if (!timeSlider->isSliderDown()) timeSlider->blockSignals(true);
            timeSlider->setValue(int(view->animTime * 1000));
            timeSlider->blockSignals(false);
            refreshPose();
        });
        timer->start(16);
    }

    void refreshPose() {
        // 采样当前时间 + 手动旋转叠加到选中骨骼
        std::map<QString, QQuaternion> pose;
        if (view->hasAnim && !view->anim.tracks.empty())
            pose = ModelSystem::sample(view->anim, view->animTime);
        if (view->selBone >= 0 && size_t(view->selBone) < view->model.bones.size()) {
            pose[view->model.bones[size_t(view->selBone)].name] =
                QQuaternion::fromAxisAndAngle(1, 0, 0, view->manualRotDeg);
        }
        view->skinned = ModelSystem::skin(view->model, pose);
        view->updateBoneLines(pose);
        view->refreshMesh();
    }

    void rebuildBoneTree() {
        boneTree->clear();
        std::vector<QTreeWidgetItem*> nodes(view->model.bones.size(), nullptr);
        for (size_t i = 0; i < view->model.bones.size(); ++i) {
            auto *it = new QTreeWidgetItem(QStringList(view->model.bones[i].name));
            it->setData(0, Qt::UserRole, int(i));
            nodes[i] = it;
        }
        for (size_t i = 0; i < view->model.bones.size(); ++i) {
            int p = view->model.bones[i].parent;
            if (p >= 0 && nodes[size_t(p)]) nodes[size_t(p)]->addChild(nodes[i]);
            else boneTree->addTopLevelItem(nodes[i]);
        }
        boneTree->expandAll();
        qInfo() << QString("[Model] 骨骼树已构建：%1 根骨骼").arg(view->model.bones.size());
    }

    void addKeyframe() {
        if (view->selBone < 0) { qInfo() << "[Anim] 提示：先在左侧骨骼树选中一根骨骼"; return; }
        QString bn = view->model.bones[size_t(view->selBone)].name;
        VNAnim::Track *tr = nullptr;
        for (auto &t : view->anim.tracks) if (t.bone == bn) { tr = &t; break; }
        if (!tr) { view->anim.tracks.push_back({bn, {}}); tr = &view->anim.tracks.back(); }
        tr->keys.push_back({view->animTime, QQuaternion::fromAxisAndAngle(1, 0, 0, view->manualRotDeg)});
        std::sort(tr->keys.begin(), tr->keys.end(),
                  [](auto &a, auto &b){ return a.first < b.first; });
        if (view->anim.duration < view->animTime + 0.1f) view->anim.duration = view->animTime + 0.1f;
        refreshKeyList();
        refreshPose();
        qInfo() << QString("[Anim] 关键帧已记录：骨骼=%1 t=%2s 旋转=%3°")
            .arg(bn).arg(view->animTime, 0, 'f', 2).arg(view->manualRotDeg);
    }

    void refreshKeyList() {
        keyList->clear();
        for (const auto &t : view->anim.tracks)
            for (const auto &k : t.keys)
                keyList->addItem(QString("%1   t=%2s   rx=%3°")
                    .arg(t.bone).arg(k.first, 0, 'f', 2)
                    .arg(k.second.toEulerAngles().x(), 0, 'f', 0));
    }

    void saveAnim() {
        if (animPath.isEmpty()) {
            animPath = QFileDialog::getSaveFileName(this, "保存动画", QDir::home().filePath("Physics3DProjects/assets/run.vnanim"),
                                                    "VNAnim (*.vnanim)");
            if (animPath.isEmpty()) return;
        }
        if (ModelSystem::saveVNAnim(view->anim, animPath))
            qInfo() << QString("[Anim] 动画已保存：%1（%2 轨道）").arg(QFile(animPath).fileName()).arg(view->anim.tracks.size());
    }

    void openMocap();   // 虚拟动捕模拟器（定义在 MocapSimDialog 之后）
    MocapSimDialog *mocapDlg = nullptr;
    void mocapDrive(double t);
    void mocapToggleRecord();
    void mocapSaveAndPlay();

protected:
    void resizeEvent(QResizeEvent *e) override { QMainWindow::resizeEvent(e); view->refreshMesh(); }
};

// ============ 虚拟动捕模拟器（沙箱无摄像头：滑条模拟设备输入 → 火柴人 → 关键帧） ============
class MocapFigure : public QWidget {
public:
    float angles[6] = {0, 0, 0, 0, 0, 0};   // hip/spine/leftArm/rightArm/leftLeg/rightLeg（度）
    std::vector<float> marks;               // 已录制关键帧时间点（时间轴打点）
    float recordT = 0; float maxT = 3.0;
    explicit MocapFigure(QWidget *p = nullptr) : QWidget(p) { setMinimumSize(360, 420); }

    void paintEvent(QPaintEvent *) override {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        g.fillRect(rect(), QColor("#12161d"));
        // 地面
        g.setPen(QPen(QColor("#2a3442"), 2));
        g.drawLine(20, height() - 24, width() - 20, height() - 24);

        float cx = width() / 2.0f, cy = 70.0f;              // 头心
        float R = 26;
        float hipY = cy + 210;                               // 骨盆
        float hipSwing = angles[0] * 0.15f;                  // 骨盆整体摆动（度→横移）
        float lean = angles[1] * 0.5f;                       // 脊柱倾斜
        g.setPen(QPen(QColor("#7fdcff"), 5, Qt::SolidLine, Qt::RoundCap));

        auto rot = [](QPointF p, QPointF o, float deg) {
            float r = qDegreesToRadians(deg), c = cos(r), s = sin(r);
            QPointF d = p - o;
            return QPointF(o.x() + d.x() * c - d.y() * s, o.y() + d.x() * s + d.y() * c);
        };
        QPointF headC(cx + hipSwing, cy);
        // 头
        g.setBrush(QBrush(QColor("#7fdcff")));
        g.drawEllipse(headC, R, R);
        // 躯干：颈(hipY-160) → 骨盆，随 lean 倾斜
        QPointF neck(cx + hipSwing, hipY - 160);
        QPointF pelvis(cx, hipY);
        QPointF leanNeck = rot(neck, pelvis, lean);
        g.drawLine(leanNeck, pelvis);
        // 手臂：肩 = 颈下方 12px，随 spine 一起摆
        QPointF shoulderBase = leanNeck + QPointF(0, 12);
        for (int side = 0; side < 2; ++side) {
            float ang = angles[2 + side];
            QPointF sh(shoulderBase.x() + (side == 0 ? -18 : 18), shoulderBase.y());
            QPointF hand = rot(sh + QPointF(0, 96), sh, ang);
            g.drawLine(sh, hand);
        }
        // 腿：从骨盆出发
        for (int side = 0; side < 2; ++side) {
            float ang = angles[4 + side];
            QPointF hp(pelvis.x() + (side == 0 ? -13 : 13), pelvis.y());
            QPointF foot = rot(hp + QPointF(0, 150), hp, ang);
            g.drawLine(hp, foot);
        }
        // 时间轴 + 关键帧打点
        float ty = height() - 12;
        g.setPen(QPen(QColor("#3a4656"), 3));
        g.drawLine(20, ty, width() - 20, ty);
        g.setPen(QPen(QColor("#ff8c1a"), 1));
        g.setBrush(QBrush(QColor("#ff8c1a")));
        for (float m : marks) {
            float x = 20 + (width() - 40) * (m / maxT);
            g.drawEllipse(QPointF(x, ty), 5, 5);
        }
        // 录制指针
        float px = 20 + (width() - 40) * (recordT / maxT);
        g.setPen(QPen(QColor("#ff3b3b"), 2));
        g.drawLine(QPointF(px, ty - 10), QPointF(px, ty + 10));
        // 标签
        g.setPen(QColor("#8fa3b8"));
        g.setFont(QFont("Noto Sans CJK SC", 10));
        g.drawText(QPointF(20, 22), QString("虚拟动捕 · 手臂 %1°/%2°  腿 %3°/%4°")
            .arg(angles[2], 0, 'f', 0).arg(angles[3], 0, 'f', 0)
            .arg(angles[4], 0, 'f', 0).arg(angles[5], 0, 'f', 0));
    }
};

class MocapSimDialog : public QDialog {
    Q_OBJECT
public:
    ModelStudioWindow *host = nullptr;
    MocapFigure *fig = nullptr;
    QSlider *sliders[6] = {};
    QSlider *timeline = nullptr;
    QLabel *stat = nullptr;
    QPushButton *btnRec = nullptr;
    QTimer recTimer;
    bool recording = false;

    explicit MocapSimDialog(ModelStudioWindow *w) : QDialog(w), host(w) {
        setWindowTitle("虚拟动捕模拟器 (Motion Capture Simulator)");
        resize(860, 560);
        auto *h = new QHBoxLayout(this);

        // 左：火柴人画布
        fig = new MocapFigure;
        fig->setStyleSheet("border:1px solid #2a3442; border-radius:8px;");
        h->addWidget(fig, 1);

        // 右：设备输入滑条 + 录制
        auto *right = new QWidget;
        right->setFixedWidth(320);
        auto *v = new QVBoxLayout(right);
        v->addWidget(new QLabel("设备输入（模拟动捕滑条）："));
        for (int i = 0; i < 6; ++i) {
            v->addWidget(new QLabel(QString::fromUtf8(MOCAP_LABELS[i])));
            sliders[i] = new QSlider(Qt::Horizontal);
            sliders[i]->setRange(-90, 90);
            connect(sliders[i], &QSlider::valueChanged, this, [this, i](int val){
                fig->angles[i] = float(val);
                fig->update();
            });
            v->addWidget(sliders[i]);
        }
        v->addWidget(new QLabel("录制时间轴（关键帧打点）："));
        timeline = new QSlider(Qt::Horizontal);
        timeline->setRange(0, 3000);
        timeline->setEnabled(false);
        v->addWidget(timeline);
        stat = new QLabel("已录制 0 关键帧 · 未在录制");
        v->addWidget(stat);

        btnRec = new QPushButton("录制动捕");
        btnRec->setStyleSheet("background:#b0413e;color:white;padding:9px;border-radius:4px;font-weight:bold;");
        connect(btnRec, &QPushButton::clicked, this, &MocapSimDialog::toggleRecord);
        v->addWidget(btnRec);
        auto *btnSave = new QPushButton("保存 .vnanim 并挂到模型");
        connect(btnSave, &QPushButton::clicked, this, &MocapSimDialog::saveAndPlay);
        v->addWidget(btnSave);
        v->addStretch();
        h->addWidget(right);

        // 录制节拍：每 300ms 捕获一帧（全部 6 骨骼）
        connect(&recTimer, &QTimer::timeout, this, [this]{ capture(); });
    }

    void toggleRecord() {
        if (!recording) {
            // 清空旧轨道，从头录
            if (host) { host->view->anim.tracks.clear(); host->view->hasAnim = false; }
            fig->marks.clear();
            fig->recordT = 0;
            recording = true;
            btnRec->setText("停止录制");
            stat->setText("录制中…（拖动滑条即设备输入）");
            recTimer.start(300);
        } else {
            recording = false;
            recTimer.stop();
            btnRec->setText("录制动捕");
            stat->setText(QString("录制完成：共 %1 关键帧").arg(fig->marks.size() * 6));
        }
        fig->update();
    }

    void capture() {
        if (!host) return;
        float t = fig->recordT;
        for (int i = 0; i < 6; ++i) {
            if (!host->view->model.bones.empty() && !boneInModel(MOCAP_BONES[i])) continue;
            VNAnim::Track *tr = nullptr;
            for (auto &tt : host->view->anim.tracks)
                if (tt.bone == MOCAP_BONES[i]) { tr = &tt; break; }
            if (!tr) { host->view->anim.tracks.push_back({MOCAP_BONES[i], {}}); tr = &host->view->anim.tracks.back(); }
            tr->keys.push_back({t, QQuaternion::fromAxisAndAngle(1, 0, 0, fig->angles[i])});
        }
        host->view->anim.duration = t + 0.3f;
        fig->marks.push_back(t);
        fig->recordT += 0.3f;
        timeline->blockSignals(true);
        timeline->setValue(int(fig->recordT * 1000));
        timeline->blockSignals(false);
        stat->setText(QString("录制中… t=%1s 已录 %2 关键帧")
            .arg(fig->recordT, 0, 'f', 1).arg(fig->marks.size() * 6));
        fig->update();
        if (fig->recordT >= fig->maxT) toggleRecord();   // 到 3 秒自动停
    }

    bool boneInModel(const char *bn) {
        for (const auto &b : host->view->model.bones) if (b.name == bn) return true;
        return false;
    }

    void saveAndPlay() {
        if (!host || host->view->anim.tracks.empty()) { stat->setText("还没有录制任何关键帧"); return; }
        QString out = qEnvironmentVariable("MOCAP_OUT");
        if (out.isEmpty())
            out = QDir(QFileInfo(qEnvironmentVariable("MODEL_ANIM")).absolutePath()).exists()
                ? QFileInfo(qEnvironmentVariable("MODEL_ANIM")).absolutePath() + "/mocap.vnanim"
                : QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets/mocap.vnanim");
        if (ModelSystem::saveVNAnim(host->view->anim, out)) {
            qInfo() << QString("[Mocap] 动作已保存：%1（%2 轨道 / %3 关键帧，时长 %4s）")
                .arg(out).arg(host->view->anim.tracks.size()).arg(fig->marks.size() * 6)
                .arg(host->view->anim.duration, 0, 'f', 2);
            // 挂到模型并播放
            host->animPath = out;
            host->view->hasAnim = true;
            host->view->playing = true;
            stat->setText(QString("已保存并播放：%1").arg(QFileInfo(out).fileName()));
            fig->update();
        } else {
            stat->setText("保存失败（目录不可写）");
        }
    }
};

void ModelStudioWindow::openMocap() {
    auto *dlg = new MocapSimDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this]{ mocapDlg = nullptr; });
    mocapDlg = dlg;
    dlg->show();
    qInfo() << "[Mocap] 虚拟动捕模拟器已打开（滑条模拟设备输入，300ms 采一帧关键帧）";
}

// 模拟设备输入：把当前姿态写到 6 根滑条（滑条联动火柴人与录制采样）
void ModelStudioWindow::mocapDrive(double t) {
    if (!mocapDlg) return;
    int vals[6] = { int(8 * sin(t)), int(15 * sin(t * 0.8)), int(50 * sin(t * 2.0)),
                    int(50 * sin(t * 2.0 + M_PI)), int(35 * sin(t * 2.0 + M_PI)), int(35 * sin(t * 2.0)) };
    for (int i = 0; i < 6; ++i) mocapDlg->sliders[i]->setValue(vals[i]);
}
void ModelStudioWindow::mocapToggleRecord() { if (mocapDlg) mocapDlg->toggleRecord(); }
void ModelStudioWindow::mocapSaveAndPlay() { if (mocapDlg) mocapDlg->saveAndPlay(); }

static int runApp(QApplication &app, int argc, char **argv) {
    

    ModelStudioWindow win;
    win.show();

    QString arg1 = (argc > 1) ? argv[1] : "";
    QString modelFile = qEnvironmentVariable("MODEL_FILE");
    QString animFile = qEnvironmentVariable("MODEL_ANIM");
    QString autoMode = qEnvironmentVariable("MODEL_AUTO_MODE");
    QString shot = qEnvironmentVariable("MODEL_SHOT");

    // 载入模型：argv[1] 或 MODEL_FILE
    QString mp = !arg1.isEmpty() ? arg1 : modelFile;
    if (!mp.isEmpty()) {
        win.view->loadModelFile(mp, animFile);
        win.modelPath = mp;
        if (!animFile.isEmpty()) win.animPath = animFile;
        win.rebuildBoneTree();
        qInfo() << QString("[Model] VNModelStudio 打开：%1（骨骼 %2，动画 %3）")
            .arg(QFile(mp).fileName()).arg(win.view->model.bones.size())
            .arg(win.view->hasAnim ? win.view->anim.name : "无");
    }

    if (!autoMode.isEmpty() && !shot.isEmpty()) {
        // 演示钩子：动画播几帧后全屏截图退出（mocap 分支时序长，自行安排截图与退出）
        if (autoMode != "mocap") {
            QTimer::singleShot(2600, &app, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(3600, &app, []{ QApplication::quit(); });
        }
        if (autoMode == "keyframe") {
            // 关键帧演示：选中 spine 骨骼，打两个关键帧
            QTimer::singleShot(1200, &app, [&win]{
                for (int i = 0; i < win.view->model.bones.size(); ++i)
                    if (win.view->model.bones[size_t(i)].name == "spine") win.view->selBone = i;
                win.view->manualRotDeg = 30;
                win.addKeyframe();
                win.view->animTime = 0.6f;
                win.addKeyframe();
                win.rebuildBoneTree();
                // 树中选中 spine
                QTreeWidgetItemIterator it(win.boneTree);
                while (*it) {
                    if ((*it)->data(0, Qt::UserRole).toInt() == win.view->selBone) {
                        win.boneTree->setCurrentItem(*it);
                        break;
                    }
                    ++it;
                }
                win.refreshPose();
            });
        } else if (autoMode == "mocap") {
            // 动捕演示：开模拟器 → 滑条动起来（模拟设备输入）→ 录制 → 保存并挂到模型播放
            QTimer::singleShot(800, &app, [&win]{ win.openMocap(); });
            QTimer *drive = new QTimer(&win);
            QObject::connect(drive, &QTimer::timeout, &win, [&win]{
                if (!win.mocapDlg) return;
                double t = double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
                win.mocapDrive(t);   // 双臂/双腿正弦摆动 = 模拟设备输入
            });
            drive->start(60);
            QTimer::singleShot(1500, &app, [&win]{ win.mocapToggleRecord(); });
            QTimer::singleShot(10000, &app, [&win]{
                if (win.mocapDlg && win.mocapDlg->recording) win.mocapToggleRecord(); // 还在录才停
                win.mocapSaveAndPlay();    // 保存 .vnanim 并挂到模型播放
            });
            QTimer::singleShot(11500, &app, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(12500, &app, []{ QApplication::quit(); });
        }
    }
    return app.exec();
}
#include "VNModelStudio.moc"

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNModelStudio", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
