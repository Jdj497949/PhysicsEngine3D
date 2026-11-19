#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <QToolButton>
#include <QWidget>
#include <QElapsedTimer>
#include <QPainter>
#include "World.h"
#include "ModelSystem.h"

class QComboBox;

// 3D 视口：飞行相机 + 模式工具栏 + 坐标罗盘 + 星空顶 + 全套 Gizmo（移动/旋转/缩放）
class GLViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLViewport(World *w, bool editorViewport, QWidget *parent = nullptr);

    void setCubeOutlined(int idx);
    void setGizmoEnabled(bool on) { gizmoEnabled = on; }
    void setToolMode(ToolMode m);              // 工具栏联动
    ToolMode toolMode() const { return world->tool; }
    // 自动化演示：真实管线模拟点击（点击物体 / 抓Gizmo控制柄），带射线日志
    void clickOnObject(int idx);
    void clearHighlight() { highlighted = -1; emit selectionChanged(-1); update(); }
    void clickGizmoHandle(int idx, int axis, ToolMode withTool);
    // 演示钩子：模拟按住 WASD 飞行
    void demoKeyFly(int key, int ms);
    // 演示钩子：模拟右键拖拽自由视角
    void demoRightDrag(int dx, int dy);
    // 拖 .mat 材质到物体上（真实拖拽与演示共用一条管线）
    void applyMatDrop(int objIdx, const QString &matPath);
    // 插座运行时查询按键状态
    bool isKeyDown(char c) const { return keys[int(c)]; }
    bool isShiftDown() const { return keys[Qt::Key_Shift & 0xff]; }
    bool isCtrlDown() const { return keys[Qt::Key_Control & 0xff]; }
    bool charLook = false;   // 角色控制器运行态：右键拖拽改视角（Pitch/Yaw）而非飞行相机

    // ============ 跨维度 HUD：2D UI 覆盖在 3D 画面上层 ============
    struct HudItem {
        QString type;                 // "text" / "button" / "image"
        QString text = "文本";
        QString font;                 // 字体族（空 = 默认）
        int fontSize = 26;
        QColor color = QColor("#ffe08a");
        QRectF rect;                  // 设计空间 1600x900
        int anchor = 0;               // 0 左上 1 居中 2 右下
        QString script;               // .socket / .lua 挂载路径
        QString imagePath;
        QString name = "按钮";
    };
    void setHUD(const std::vector<HudItem> &items);
    void setHUDVisible(bool on) { hudOn = on; update(); }
    void demoClickHUD(int idx);       // 演示钩子：走真实点击判定
    // 演示钩子：模拟按住 Gizmo 轴向拖拽（走真实鼠标事件管线）
    void demoGizmoDrag(int idx, int axis, ToolMode withTool, int dx, int dy);

    // ============ 网格吸附（Snap）：位置/旋转步长可调 ============
    bool snapOn = false;
    float snapPosStep = 1.0f;          // 位置吸附步长（米）
    float snapRotStep = 15.0f;         // 旋转吸附步长（度）
    QVector3D snapPosition(const QVector3D &p) const {
        if (!snapOn || snapPosStep <= 0) return p;
        return QVector3D(std::round(p.x() / snapPosStep) * snapPosStep,
                         std::round(p.y() / snapPosStep) * snapPosStep,
                         std::round(p.z() / snapPosStep) * snapPosStep);
    }
    float snapRotation(float deg) const {
        if (!snapOn || snapRotStep <= 0) return deg;
        return std::round(deg / snapRotStep) * snapRotStep;
    }

    // ============ Shift 多选 + 对齐/分布 ============
    QVector<int> multiSel;             // 框选/多选的物体下标
    void selectMulti(int idx, bool additive) {
        if (!additive) { multiSel.clear(); }
        if (idx >= 0 && !multiSel.contains(idx)) multiSel.append(idx);
        update();
    }
    void alignTop();                   // 顶端对齐（Y 取最高）
    void distributeX();                // X 轴等距分布

signals:
    void selectionChanged(int idx);
    void cameraMoved(const QString &log);      // 相机日志（终端 + 状态栏）
    void toolChanged(int mode);
    void materialDropped(int objIdx, const QString &matPath);
    void hudButtonClicked(int idx, QString name, QString script);   // 2D HUD 按钮被点击

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;   // 接收 .mat 材质拖拽
    void dropEvent(QDropEvent *e) override;
    bool event(QEvent *ev) override;           // QEvent::KeyPress 同步工具栏高亮

private:
    struct Mesh { GLuint vao = 0; int count = 0; bool indexed = false; };
    // ---- 外部模型（.vnmodel）渲染缓存：路径 -> GL 网格 + 蒙皮数据 ----
    struct ModelGL {
        Mesh mesh;
        GLuint vbo = 0;                 // 顶点 VBO（蒙皮后每帧更新）
        VNModel data;                   // 原始模型（骨骼 + 绑定顶点）
        VNAnim anim;                    // 当前挂载的动画剪辑
        bool hasAnim = false;
    };
    static std::map<QString, ModelGL *> &modelCache();
    void skinUpload(ModelGL *mg, const std::vector<float> &posn);   // 顶点回传 VBO（蒙皮）
public:
    // 注册/获取模型（MainWindow 拖入模型后调用；两个视口共享同一份缓存）
    static ModelGL *ensureModel(const QString &vnmodelPath);
    static void playAnim(SceneObject &o, const QString &vnanimPath);
    static QString animLog;             // 最近一条动画日志（Lua/插座联动）
private:
    GLuint compile(GLenum type, const char *src);
    void buildCube(Mesh &m);
    void buildSphere(Mesh &m);
    void buildPlane(Mesh &m);
    void buildGrass(Mesh &m);     // 草地：40x40 细分平面（顶点着色器风吹位移的承载网格）
    void buildCylinder(Mesh &m);
    void buildCapsule(Mesh &m);
    void buildAxis(GLuint &vao, const QVector3D &dir);
    void buildStars();
    void updateFly(float dt);                  // 平滑飞行
    QMatrix4x4 viewMatrix() const { return world->viewMatrix(); }
    QVector3D rayFromScreen(const QPoint &p);
    int pickObject(const QPoint &p);
    int pickGizmoHandle(const QPoint &p, int &axis);
    QPointF project(const QVector3D &wp) const;
    QVector3D objectAxis(int objIdx, int axisIdx) const;
    QMatrix4x4 worldMatrix(int objIdx) const { return worldMatrixOf(objIdx, world->objects); }
    void drawGizmoFor(const SceneObject &o, const QMatrix4x4 &wm);
    QWidget *buildToolbar();                   // 左上角模式工具栏

    World *world;
    bool gizmoEnabled;
    GLuint prog = 0;
    GLint locModel, locView, locProj, locColor, locLit;
    GLint locMetal = -1, locRough = -1, locHasTex = -1, locEye = -1, locTex = -1;
    // 动态自然 + 天气：着色器 uniform（u_Time / u_WindStrength / u_WindFreq / uAmbient / uAmbientI）
    GLint locTime = -1, locWindS = -1, locWindF = -1, locAmb = -1, locAmbI = -1;
    QElapsedTimer windClock;                  // u_Time 时钟（风吹相位）
public:
    bool windEnabled = true;                  // Dynamic Nature 插件开关（false = 全场风力归 0）
    class ParticleSystem *particles = nullptr; // 粒子系统（导演面板右键新建后注入）
    void setParticles(class ParticleSystem *p) { particles = p; }
private:
    TextureCache texCache;
    Mesh meshes[MESH_TYPE_COUNT];
    GLuint axisVao[3] = {0, 0, 0};
    GLuint gridVao = 0, gridVbo = 0; int gridCount = 0;
    GLuint dbgVao = 0, dbgVbo = 0;
    GLuint starVao = 0, starVbo = 0; int starCount = 0;
    QMatrix4x4 m_proj, m_view;
    int highlighted = -1;
    QPoint lastPos;
    QElapsedTimer keyTimer;                    // QWER 快切 vs 飞行按住 区分
    int heldToolKey = -1;

    // 飞行按键状态
    bool keys[256] = {false};
    qint64 lastFlyMs = 0;
    qint64 lastMoveLogMs = 0;

    // Gizmo 拖拽状态
    int dragAxis = -1;
    QPoint dragStartMouse;
    QVector3D dragStartVec;                    // 位置/缩放/欧拉角 起始值
    QPointF dragAxisDir;
    bool rightLooking = false;

    // 左上角模式工具栏
    QWidget *toolBar = nullptr;
    QToolButton *toolBtns[TOOL_MODE_COUNT] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    // ---- 跨维度 HUD（2D 覆盖层）----
    std::vector<HudItem> hudItems;
    bool hudOn = false;
    QRectF hudScreenRect(const HudItem &it, double sx, double sy) const;
    int hudHitTest(const QPoint &p);
    void drawHUDItems(QPainter &p);

    // ---- Gizmo 射线-平面精准拖拽 ----
    QVector3D dragPlaneNormal;
    QVector3D dragStartHit;
    QVector3D rayPlaneHit(const QPoint &p, const QVector3D &planePt, const QVector3D &normal);
};
