// VNMateriaLab —— 材质实验室。由 3DEditor 文件树双击 .mat 通过 QProcess 拉起，argv[1] 为材质路径。
// 左：资源/输出管理 | 中：3D 实时渲染预览（测试球，共享 MatShader 着色器）| 右：属性面板（颜色/金属度/粗糙度/贴图）
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QSurfaceFormat>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QMainWindow>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QColorDialog>
#include <QFileDialog>
#include <QTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <cmath>
#include "MaterialSystem.h"

// ============ 中间 3D 预览：测试球（带 UV，实时响应参数） ============
class PreviewGL : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    PreviewGL(Material *mat, QWidget *p = nullptr) : QOpenGLWidget(p), mat(mat) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void refreshMaterial() { update(); }
    // 程序生成演示贴图（无外部文件，直接上传 GPU）
    void setDemoChecker() {
        makeCurrent();
        QImage img(128, 128, QImage::Format_RGBA8888);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                bool w = ((x / 16) + (y / 16)) % 2 == 0;
                img.setPixelColor(x, y, w ? QColor(235, 235, 235) : QColor(70, 70, 80));
            }
        demoTexKey = QString("demo_checker_%1").arg(QDateTime::currentMSecsSinceEpoch());
        demoTex = texCache.put(this, demoTexKey, img);
        doneCurrent();
        update();
    }
protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        prog.addShaderFromSourceCode(QOpenGLShader::Vertex, MatShader::VSH);
        prog.addShaderFromSourceCode(QOpenGLShader::Fragment, MatShader::FSH);
        prog.link();
        buildSphere();
        timeClock.start();
    }
    void paintGL() override {
        glClearColor(0.09f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if (sphereCount == 0) return;
        QMatrix4x4 model, view, proj;
        view.lookAt(QVector3D(0, 0.4f, 2.6f), QVector3D(0, 0, 0), QVector3D(0, 1, 0));
        proj.perspective(45.0f, width() / float(height()), 0.1f, 100.0f);
        model.rotate(spin, QVector3D(0, 1, 0));
        prog.bind();
        prog.setUniformValue("uModel", model);
        prog.setUniformValue("uView", view);
        prog.setUniformValue("uProj", proj);
        // 动态自然：时间与风力（材质里的 windStrength/windFreq 实时驱动顶点摆动）
        prog.setUniformValue("u_Time", timeClock.elapsed() / 1000.0f);
        prog.setUniformValue("u_WindStrength", mat->windStrength);
        prog.setUniformValue("u_WindFreq", mat->windFreq);
        prog.setUniformValue("uAmbient", QVector3D(1.0f, 1.0f, 1.0f));
        prog.setUniformValue("uAmbientI", 1.0f);
        QColor c = mat->baseColor;
        prog.setUniformValue("uColor", QVector3D(c.redF(), c.greenF(), c.blueF()));
        prog.setUniformValue("uLit", true);
        prog.setUniformValue("uMetal", mat->metalness);
        prog.setUniformValue("uRough", mat->roughness);
        prog.setUniformValue("uEye", QVector3D(0, 0.4f, 2.6f));
        GLuint tex = mat->texture.isEmpty() ? demoTex : texCache.get(this, mat->texture);
        prog.setUniformValue("uHasTex", tex ? true : false);
        prog.setUniformValue("uTex", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex ? tex : 0);
        glBindVertexArray(sphereVao);
        glDrawElements(GL_TRIANGLES, sphereCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        prog.release();
        spin += 0.25f;
    }
private:
    void buildSphere() {
        std::vector<float> verts; std::vector<GLuint> idx;
        const int stacks = 24, slices = 32;
        for (int i = 0; i <= stacks; ++i) {
            float phi = float(M_PI) * i / stacks;
            for (int j = 0; j <= slices; ++j) {
                float th = 2.0f * float(M_PI) * j / slices;
                float nx = sinf(phi) * cosf(th), ny = cosf(phi), nz = sinf(phi) * sinf(th);
                verts.push_back(nx); verts.push_back(ny); verts.push_back(nz);
                verts.push_back(nx); verts.push_back(ny); verts.push_back(nz);
                verts.push_back(float(j) / slices); verts.push_back(1.0f - float(i) / stacks);
            }
        }
        for (int i = 0; i < stacks; ++i)
            for (int j = 0; j < slices; ++j) {
                GLuint a = GLuint(i * (slices + 1) + j), b = a + slices + 1;
                idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
                idx.push_back(b); idx.push_back(b + 1); idx.push_back(a + 1);
            }
        GLuint vbo, ebo;
        glGenVertexArrays(1, &sphereVao);
        glBindVertexArray(sphereVao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);
        glBindVertexArray(0);
        sphereCount = int(idx.size());
    }
    Material *mat;
    QOpenGLShaderProgram prog;
    GLuint sphereVao = 0; int sphereCount = 0;
    float spin = 0.0f;
    TextureCache texCache;
    GLuint demoTex = 0; QString demoTexKey;
    QTimer spinTimer;
    QElapsedTimer timeClock;
};

// ============ 主窗口 ============
class MatLabWindow : public QMainWindow {
    Q_OBJECT
public:
    MatLabWindow(const QString &matPath, QWidget *parent = nullptr) : QMainWindow(parent) {
        matFile = matPath;
        Material::load(matPath, mat);
        projectRoot = QDir(QFileInfo(matPath).dir().absolutePath() + "/..").absolutePath();  // assets/ 的上一级

        setWindowTitle(QString("VNMateriaLab 材质实验室 - %1").arg(QFileInfo(matPath).fileName()));
        resize(1280, 800);

        // 左：资源/输出管理（assets 目录）
        assets = new QListWidget;
        refreshAssets();
        auto *leftPanel = new QWidget;
        auto *leftLay = new QVBoxLayout(leftPanel);
        leftLay->addWidget(new QLabel("资源管理 (assets)"));
        leftLay->addWidget(assets);
        auto *outLabel = new QLabel("输出\n\n参数改动 → 预览实时刷新\n【保存并退出】写回 .mat 文件");
        outLabel->setStyleSheet("color:#8b95a8;");
        leftLay->addWidget(outLabel);

        // 中：3D 预览
        preview = new PreviewGL(&mat);

        // 右：属性面板
        auto *right = new QWidget;
        auto *rl = new QVBoxLayout(right);
        rl->addWidget(new QLabel("属性面板"));
        auto *form = new QFormLayout;
        colorBtn = new QPushButton;
        colorBtn->setStyleSheet(QString("background:%1; min-height:32px;").arg(mat.baseColor.name()));
        form->addRow("基础颜色", colorBtn);
        metalSlider = new QSlider(Qt::Horizontal); metalSlider->setRange(0, 100);
        metalSlider->setValue(int(mat.metalness * 100));
        metalVal = new QLabel; updMetalLabel();
        auto *mRow = new QHBoxLayout; mRow->addWidget(metalSlider); mRow->addWidget(metalVal);
        form->addRow("金属度", mRow);
        roughSlider = new QSlider(Qt::Horizontal); roughSlider->setRange(0, 100);
        roughSlider->setValue(int(mat.roughness * 100));
        roughVal = new QLabel; updRoughLabel();
        auto *rRow = new QHBoxLayout; rRow->addWidget(roughSlider); rRow->addWidget(roughVal);
        form->addRow("粗糙度", rRow);
        texEdit = new QLineEdit(mat.texture);
        texEdit->setPlaceholderText("贴图路径（拖拽图片或点浏览）");
        texEdit->setAcceptDrops(true);
        auto *browse = new QPushButton("浏览…");
        auto *texRow = new QHBoxLayout; texRow->addWidget(texEdit); texRow->addWidget(browse);
        form->addRow("贴图槽位", texRow);
        // 动态自然：风力滑条（强度=摆幅 cm，频率=rad/s，实时刷新预览）
        windSSlider = new QSlider(Qt::Horizontal); windSSlider->setRange(0, 60);
        windSSlider->setValue(int(mat.windStrength * 100));
        windSVal = new QLabel; updWindSLabel();
        auto *wsRow = new QHBoxLayout; wsRow->addWidget(windSSlider); wsRow->addWidget(windSVal);
        form->addRow("风力强度", wsRow);
        windFSlider = new QSlider(Qt::Horizontal); windFSlider->setRange(0, 60);
        windFSlider->setValue(int(mat.windFreq));
        windFVal = new QLabel; updWindFLabel();
        auto *wfRow = new QHBoxLayout; wfRow->addWidget(windFSlider); wfRow->addWidget(windFVal);
        form->addRow("风力频率", wfRow);
        rl->addLayout(form);
        auto *clearTex = new QPushButton("清除贴图");
        auto *save = new QPushButton("保存并退出");
        save->setStyleSheet("background:#2f6fab;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        rl->addWidget(clearTex);
        rl->addStretch();
        rl->addWidget(save);

        auto *central = new QWidget;
        auto *hl = new QHBoxLayout(central);
        hl->addWidget(leftPanel, 1);
        hl->addWidget(preview, 3);
        hl->addWidget(right, 2);
        setCentralWidget(central);

        connect(colorBtn, &QPushButton::clicked, this, [this]{
            QColor c = QColorDialog::getColor(mat.baseColor, this, "选择基础颜色");
            if (!c.isValid()) return;
            mat.baseColor = c;
            colorBtn->setStyleSheet(QString("background:%1; min-height:32px;").arg(c.name()));
            preview->refreshMaterial();
            qInfo().noquote() << QString("[材质] 基础颜色改为 %1，预览实时刷新").arg(c.name());
        });
        connect(metalSlider, &QSlider::valueChanged, this, [this](int v){
            mat.metalness = v / 100.0f; updMetalLabel(); preview->refreshMaterial(); });
        connect(roughSlider, &QSlider::valueChanged, this, [this](int v){
            mat.roughness = v / 100.0f; updRoughLabel(); preview->refreshMaterial(); });
        connect(windSSlider, &QSlider::valueChanged, this, [this](int v){
            mat.windStrength = v / 100.0f; updWindSLabel();
            preview->refreshMaterial();
            qInfo().noquote() << QString("[材质] 风力强度改为 %1（摆幅 %2 m），预览实时摆动")
                .arg(v).arg(mat.windStrength, 0, 'f', 2); });
        connect(windFSlider, &QSlider::valueChanged, this, [this](int v){
            mat.windFreq = float(v); updWindFLabel(); preview->refreshMaterial(); });
        connect(browse, &QPushButton::clicked, this, [this]{
            QString p = QFileDialog::getOpenFileName(this, "选择贴图", projectRoot, "图片 (*.png *.jpg *.jpeg)");
            if (!p.isEmpty()) { mat.texture = p; texEdit->setText(p); preview->refreshMaterial(); }
        });
        connect(clearTex, &QPushButton::clicked, this, [this]{
            mat.texture.clear(); texEdit->clear(); preview->refreshMaterial(); });
        connect(texEdit, &QLineEdit::textChanged, this, [this]{ preview->refreshMaterial(); });
        connect(save, &QPushButton::clicked, this, [this]{
            if (Material::save(matFile, mat)) {
                qInfo().noquote() << QString("[材质] 已保存并退出：%1 颜色=%2 金属度=%3 粗糙度=%4 贴图=%5")
                    .arg(QFile(matFile).fileName(), mat.baseColor.name())
                    .arg(mat.metalness).arg(mat.roughness)
                    .arg(mat.texture.isEmpty() ? "无" : mat.texture);
                qApp->quit();
            }
        });

        // 预览自转刷新
        spinTimer.start(33);
        connect(&spinTimer, &QTimer::timeout, this, [this]{ preview->update(); });

        // 演示钩子（沙箱截图用）
        QString autoMode = qEnvironmentVariable("MAT_AUTO_MODE");
        QString shot = qEnvironmentVariable("MAT_SHOT");
        if (!autoMode.isEmpty() && !shot.isEmpty()) {
            QTimer::singleShot(1800, this, [this, shot, autoMode]{
                if (autoMode == "wind") {
                    // 风吹演示：设风力滑条（触发 valueChanged 实时刷新）+ 截图
                    windSSlider->setValue(45);          // 摆幅 0.45 m
                    windFSlider->setValue(30);          // 30 rad/s
                    qInfo().noquote() << "[材质] 风力演示：强度 45 / 频率 30，测试球顶点开始摆动";
                    QTimer::singleShot(500, this, [this, shot]{ grabFullscreen(shot); });   // ① 风力生效
                } else {
                    grabFullscreen(shot);                              // ① 默认材质全貌
                }
                if (autoMode == "red") {
                    mat.baseColor = QColor(210, 30, 30);
                    colorBtn->setStyleSheet("background:rgb(210,30,30); min-height:32px;");
                    preview->setDemoChecker();                          // 程序生成棋盘贴图（真实上传 GPU）
                    mat.roughness = 0.55f;
                    roughSlider->setValue(55);
                    qInfo().noquote() << "[材质] 参数改为红色 + 棋盘贴图，预览实时刷新";
                }
            });
            if (autoMode == "wind") {
                QTimer::singleShot(3000, this, [this, shot]{
                    grabFullscreen(QString(shot).replace(".png", "_b.png"));   // ② 第二帧（摆动位置已变化）
                });
                QTimer::singleShot(4200, qApp, &QCoreApplication::quit);
                return;
            }
            QTimer::singleShot(4200, this, [this, shot]{
                preview->refreshMaterial();
            });
            QTimer::singleShot(5200, this, [this, shot]{
                grabFullscreen(QString(shot).replace(".png", "_red.png"));   // ② 贴图+红色实时变化
            });
            QTimer::singleShot(6000, qApp, &QCoreApplication::quit);
        }
    }
private:
    static void grabFullscreen(const QString &shot) {
        if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
    }
    void refreshAssets() {
        QDir dir(projectRoot + "/assets");
        QDir().mkpath(dir.absolutePath());
        assets->clear();
        for (const QFileInfo &f : dir.entryInfoList(QDir::Files, QDir::Name))
            assets->addItem(f.fileName());
    }
    void updMetalLabel() { metalVal->setText(QString::number(mat.metalness, 'f', 2)); }
    void updRoughLabel() { roughVal->setText(QString::number(mat.roughness, 'f', 2)); }
    void updWindSLabel() { windSVal->setText(QString("%1 cm").arg(int(mat.windStrength * 100))); }
    void updWindFLabel() { windFVal->setText(QString("%1 rad/s").arg(int(mat.windFreq))); }

    QString matFile, projectRoot;
    Material mat;
    QListWidget *assets = nullptr;
    PreviewGL *preview = nullptr;
    QPushButton *colorBtn = nullptr;
    QSlider *metalSlider = nullptr, *roughSlider = nullptr;
    QSlider *windSSlider = nullptr, *windFSlider = nullptr;
    QLabel *metalVal = nullptr, *roughVal = nullptr;
    QLabel *windSVal = nullptr, *windFVal = nullptr;
    QLineEdit *texEdit = nullptr;
    QTimer spinTimer;
};

#include "VNMateriaLab.moc"

static int runApp(QApplication &app, int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    app.setApplicationName("VNMateriaLab");

    if (argc < 2) {
        qInfo() << "用法：VNMateriaLab <材质路径.mat>";
        return 1;
    }
    QString matPath = argv[1];
    if (!QFile(matPath).exists()) {
        qInfo() << QString("错误：材质文件不存在：%1").arg(matPath);
        return 1;
    }
    MatLabWindow w(matPath);
    w.show();
    return app.exec();
}

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNMateriaLab", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
