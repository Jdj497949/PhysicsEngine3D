// VN2DEditor —— 2D 编辑器（双角色）：
//   · UI 模式 = VNUIEditor：Canvas 画布 + 锚点对齐（左上/居中/右下）+ 控件库，做 HUD
//   · 纯 2D 游戏模式：RigidBody2D + BoxCollider2D + 重力 -9.8，60fps 欧拉积分
// 由 3DLauncher（2D 项目）或 3DEditor 资产双击（.uicanvas/.scene2d）QProcess 独立拉起。
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QPlainTextEdit>
#include <QDockWidget>
#include <QTabWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QFontDatabase>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QKeyEvent>
#include <QStyle>
#include <QDragMoveEvent>
#include <QMenu>
#include <QPainterPath>
#include <QCheckBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QDirIterator>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <vector>
#include <cmath>
#include "../common/CrashHandler.h"

static const int DESIGN_W = 1600;
static const int DESIGN_H = 900;

#define SLOT_STYLE "background:#262a33; color:#9fe29f; border:2px dashed #4a5568; border-radius:4px; padding:5px;"

static QPlainTextEdit *gConsole = nullptr;
static void c2dMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &m) {
    fprintf(stderr, "%s\n", qUtf8Printable(m)); fflush(stderr);
    if (!gConsole) return;
    QString color = (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
                        ? "#ff5a6e" : "#e6ecf5";
    gConsole->appendHtml(QString("<span style=\"color:%1\">%2</span>").arg(color, m.toHtmlEscaped()));
}

// ============ 画布元素 ============
struct CanvasItem {
    QString type;                 // button/text/image/input/box2d/slider/checkbox/dropdown/toggle/
                                  // panel/scrollview/grid/progress/mask
    QString name = "按钮";
    QString text = "文本";
    QString font;                 // 字体族（空 = 默认 Noto Sans CJK SC）
    int fontSize = 28;
    QColor color = QColor("#ffe08a");
    QRectF rect{100, 100, 240, 56};   // 设计空间 1600x900
    int anchor = 0;               // 0-8 九锚点（0左上 1中上 2右上 3左中 4居中 5右中 6左下 7中下 8右下）
    QString script;               // .lua / .socket 挂载路径
    QString imagePath;            // 图片资产路径
    QString inputText;            // 输入框：运行态已输入内容
    bool hasBody = false;
    bool isStatic = false;        // BoxCollider2D 静态碰撞体（平台/金币不动）
    QPointF vel{0, 0};
    // ---- 按钮三态图（纯图片渲染，无边框）----
    QString imgNormal, imgHover, imgPressed;
    QString bindScript;           // 绑定信号槽位（.lua/.socket）
    // ---- 九宫格切图 ----
    bool nineSlice = false;
    int sliceMargin = 16;
    // ---- 容器布局 ----
    int layout = 0;               // 0 无 1 VBox 2 HBox
    double spacing = 12;
    int parent = -1;              // 父容器序号（panel）
    // ---- 值类控件 ----
    double value = 60;            // slider/progress 百分比 0-100
    bool checked = false;
    QStringList options{"1080P", "720P", "480P"};
    // ---- Tween 缓动动画 ----
    int tweenType = 0;            // 0 无 1 淡入 2 右移 100px
    double tweenDur = 0.5;
    int tweenEase = 2;            // 0 线性 1 Ease-In 2 Ease-Out
    // ---- 运行态动画状态 ----
    double animT = 0;
    bool animDone = false;
};

// 缓动曲线：商业引擎同款（线性 / Ease-In 二次 / Ease-Out 二次）
static double easeApply(int ease, double t) {
    if (ease == 1) return t * t;
    if (ease == 2) return 1 - (1 - t) * (1 - t);
    return t;
}

// ============ 画布控件（设计空间 1600x900，棋盘底） ============
class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    enum Mode { UiMode, PhysMode };
    Mode mode = UiMode;
    std::vector<CanvasItem> items;
    int selected = -1;
    bool running = false;
    float gravity = 980.0f;   // 设计空间 Y 向下为正，像素物理 980 px/s²（≈9.8 m/s² × 100 px/m）
    QString savePath;

    explicit CanvasWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setAcceptDrops(true);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);   // 运行态输入框需要键盘焦点
        setMinimumSize(640, 360);
    }

    double sx() const { return width() / double(DESIGN_W); }
    double sy() const { return height() / double(DESIGN_H); }
    QPointF toDesign(const QPoint &p) const { return QPointF(p.x() / sx(), p.y() / sy()); }
    QPointF toWidget(const QPointF &d) const { return QPointF(d.x() * sx(), d.y() * sy()); }
    QRectF groundRect() const { return QRectF(0, DESIGN_H - 40, DESIGN_W, 40); }

    int hoverIdx = -1;     // 运行态悬停控件
    int pressedIdx = -1;   // 运行态按下控件
    double gVolume = 60;   // 系统设置模板：全局 BGM 音量（真实生效）
    bool fullScreen = false;

    // ---- 平台跳跃（2D 演示模板）：玩家控制 + 金币计分 ----
    int score = 0;
    bool playerGrounded = false;
    std::map<int, bool> keyDown;   // 运行态按键状态（A/D/空格）
    void demoKey(int key, bool down) { keyDown[key] = down; }   // 演示钩子：真实走同一按键管线

    // 进入运行态：清空悬停/按下/聚焦，重置 Tween
    void resetRunState() {
        hoverIdx = pressedIdx = -1;
        focusedInput = -1;
        for (auto &it : items) { it.animT = 0; it.animDone = false; }
    }

    // ---- 锚点自适应：分辨率变化时按 9 锚点平移控件 ----
    void applyAnchors(int oldW, int oldH) {
        int dxW = DESIGN_W - oldW, dxH = DESIGN_H - oldH;
        for (auto &it : items) {
            if (it.parent >= 0) continue;
            int ax = it.anchor % 3, ay = it.anchor / 3;   // 0左 1中 2右
            if (ax == 1) it.rect.translate(dxW / 2.0, 0);
            else if (ax == 2) it.rect.translate(dxW, 0);
            if (ay == 1) it.rect.translate(0, dxH / 2.0);
            else if (ay == 2) it.rect.translate(0, dxH);
        }
    }
    void resizeEvent(QResizeEvent *e) override {
        int oldW = int(e->oldSize().width() / sx());
        int oldH = int(e->oldSize().height() / sy());
        if (oldW > 0 && oldH > 0) applyAnchors(oldW, oldH);
        QWidget::resizeEvent(e);
    }

    // ---- 布局引擎：Panel 的 VBox/HBox 自动排版（居中对齐 + 自动间距）----
    void applyLayout() {
        for (size_t pi = 0; pi < items.size(); ++pi) {
            const CanvasItem &pn = items[pi];
            if (pn.type != "panel" && pn.type != "scrollview" && pn.type != "grid") continue;
            if (pn.layout == 0) continue;
            std::vector<size_t> kids;
            double contentH = 0, contentW = 0;
            for (size_t ci = 0; ci < items.size(); ++ci)
                if (items[ci].parent == int(pi)) kids.push_back(ci);
            if (kids.empty()) continue;
            for (size_t ci : kids) {
                contentH += items[ci].rect.height();
                contentW = qMax(contentW, items[ci].rect.width());
            }
            int gridCols = 2;   // GridLayout：两列背包格
            int rows = int((kids.size() + gridCols - 1) / gridCols);
            if (pn.layout == 1) contentH += pn.spacing * double(kids.size() - 1);
            else contentH = rows * items[kids[0]].rect.height() + pn.spacing * double(rows - 1);
            double y = pn.rect.top() + (pn.rect.height() - contentH) / 2.0;
            double x0 = pn.rect.left() + (pn.rect.width() - contentW) / 2.0;
            for (size_t k = 0; k < kids.size(); ++k) {
                CanvasItem &c = items[kids[k]];
                if (pn.layout == 1) {           // VBox：纵向排列、水平居中
                    c.rect.moveLeft(pn.rect.center().x() - c.rect.width() / 2);
                    c.rect.moveTop(y);
                    y += c.rect.height() + pn.spacing;
                } else if (pn.layout == 2) {    // HBox：横向排列、垂直居中
                    c.rect.moveTop(pn.rect.center().y() - c.rect.height() / 2);
                    c.rect.moveLeft(x0 + k * (contentW + pn.spacing));
                }
            }
        }
    }

    // ---- Tween 缓动：运行态 60fps 推进（淡入 / 右移，Ease-In/Out）----
    void stepAnim(float dt) {
        if (!running) return;
        bool any = false;
        for (auto &it : items) {
            if (it.tweenType == 0 || it.animDone) continue;
            any = true;
            it.animT = qMin(it.animT + dt / qMax(0.05, it.tweenDur), 1.0);
            double k = easeApply(it.tweenEase, it.animT);
            if (it.tweenType == 1) it.color.setAlpha(int(255 * k));
            else if (it.tweenType == 2 && it.animT < 1.0) it.rect.translate(100.0 * dt / qMax(0.05, it.tweenDur), 0);
            if (it.animT >= 1.0) { it.animDone = true; qInfo() << QString("[Tween] %1 动画完成（%2）").arg(it.name, it.tweenEase == 0 ? "线性" : it.tweenEase == 1 ? "Ease-In" : "Ease-Out"); }
        }
        if (any) update();
    }

    // 命中测试（从最上层往下）
    int hitItem(const QPointF &dp) const {
        for (int i = int(items.size()) - 1; i >= 0; --i) {
            QRectF r = items[size_t(i)].rect.adjusted(-6, -6, 6, 6);
            if (r.contains(dp)) return i;
        }
        return -1;
    }
    int hitHandle(const QPointF &dp) const {   // 0左上 1右上 2左下 3右下，-1 无
        if (selected < 0 || mode != UiMode) return -1;
        QRectF r = items[size_t(selected)].rect;
        for (int h = 0; h < 4; ++h) {
            QPointF c((h % 2) ? r.right() : r.left(), (h / 2) ? r.bottom() : r.top());
            if (QRectF(c.x() - 10, c.y() - 10, 20, 20).contains(dp)) return h;
        }
        return -1;
    }

    void addItem(const CanvasItem &it) {
        items.push_back(it);
        selected = int(items.size()) - 1;
        qInfo() << QString("[2D] 已添加控件：%1（%2）").arg(it.name, it.type);
        applyLayout();
        emit selectionChanged();
        emit itemsChanged();
        update();
    }

    void stepPhysics(float dt) {
        if (!running || mode != PhysMode) return;
        float gy = groundRect().top();
        // 玩家（RigidBody2D + CharacterController2D）：A/D 移动、空格跳跃
        int playerIdx = -1;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].name.startsWith("玩家")) { playerIdx = int(i); break; }
        if (playerIdx >= 0) {
            CanvasItem &pl = items[size_t(playerIdx)];
            const float SPEED = 340, JUMP = 560;
            if (keyDown[int(Qt::Key_A)] || keyDown[int(Qt::Key_Left)])
                pl.vel.setX(-SPEED);
            else if (keyDown[int(Qt::Key_D)] || keyDown[int(Qt::Key_Right)])
                pl.vel.setX(SPEED);
            else
                pl.vel.setX(0);
            if (keyDown[int(Qt::Key_Space)] && playerGrounded) {
                pl.vel.setY(-JUMP);   // 设计空间 Y 向下为正，跳跃向上
                playerGrounded = false;
                qInfo() << QString("[2D] 玩家跳跃！（RigidBody2D 垂直初速 %1）").arg(int(JUMP));
            }
        }
        // 动态刚体积分 + 碰撞
        for (size_t i = 0; i < items.size(); ++i) {
            CanvasItem &it = items[i];
            if (it.type != "box2d" || !it.hasBody || it.isStatic) continue;   // 静态碰撞体不动
            if (int(i) == playerIdx) playerGrounded = false;
            double prevBottom = it.rect.bottom();
            it.vel.setY(it.vel.y() + gravity * dt);            // RigidBody2D：欧拉积分
            it.rect.translate(it.vel.x() * dt, it.vel.y() * dt);
            if (it.rect.bottom() > gy) {                        // BoxCollider2D：地面碰撞回弹
                it.rect.moveBottom(gy);
                if (it.vel.y() > 0) {
                    it.vel.setY(-it.vel.y() * 0.55f);
                    if (qAbs(it.vel.y()) < 40) it.vel.setY(0);
                }
                it.vel.setX(it.vel.x() * 0.985f);
                if (int(i) == playerIdx) playerGrounded = true;
            }
            if (it.rect.left() < 0) { it.rect.moveLeft(0); it.vel.setX(-it.vel.x() * 0.5f); }
            if (it.rect.right() > DESIGN_W) { it.rect.moveRight(DESIGN_W); it.vel.setX(-it.vel.x() * 0.5f); }
            // 玩家 vs 静态平台：下落穿越平台顶面 -> 落脚（BoxCollider2D 静态碰撞）
            if (int(i) == playerIdx) {
                for (const CanvasItem &plat : items) {
                    if (plat.type != "box2d" || !plat.isStatic || plat.name.startsWith("金币")) continue;   // 金币=触发器，不承载
                    if (it.vel.y() < 0) continue;   // 只在上升穿过后下落时落平台
                    const QRectF &pr = plat.rect;
                    if (it.rect.right() > pr.left() && it.rect.left() < pr.right()
                        && prevBottom <= pr.top() + 6 && it.rect.bottom() >= pr.top()) {
                        it.rect.moveBottom(pr.top());
                        it.vel.setY(0);
                        playerGrounded = true;
                        qInfo() << QString("[2D] 玩家落到平台 %1（BoxCollider2D 静态碰撞生效）").arg(plat.name);
                    }
                }
            }
        }
        // 金币：玩家矩形相交 -> 收集 + 计分（碰到金币分数加一，控制台打印）
        for (int ci = int(items.size()) - 1; ci >= 0; --ci) {
            if (!items[size_t(ci)].name.startsWith("金币")) continue;
            if (playerIdx >= 0 && items[size_t(ci)].rect.intersects(items[size_t(playerIdx)].rect)) {
                ++score;
                qInfo() << QString("[Score] 吃到金币！总分 %1").arg(score);
                items.erase(items.begin() + ci);
                if (playerIdx > ci) --playerIdx;
                emit selectionChanged();
            }
        }
        update();
    }

    // ============ JSON 存取 ============
    QJsonObject itemJson(const CanvasItem &it) const {
        QJsonArray opts;
        for (const QString &s : it.options) opts.append(s);
        QJsonObject j{
            {"type", it.type}, {"name", it.name}, {"text", it.text},
            {"font", it.font}, {"fontSize", it.fontSize}, {"color", it.color.name()},
            {"anchor", it.anchor}, {"script", it.script}, {"imagePath", it.imagePath},
            {"hasBody", it.hasBody}, {"isStatic", it.isStatic}, {"inputText", it.inputText},
            {"imgNormal", it.imgNormal}, {"imgHover", it.imgHover}, {"imgPressed", it.imgPressed},
            {"bindScript", it.bindScript}, {"nineSlice", it.nineSlice}, {"sliceMargin", it.sliceMargin},
            {"layout", it.layout}, {"spacing", it.spacing}, {"parent", it.parent},
            {"value", it.value}, {"checked", it.checked},
            {"options", opts},
            {"tweenType", it.tweenType}, {"tweenDur", it.tweenDur}, {"tweenEase", it.tweenEase},
            {"rect", QJsonObject{{"x", it.rect.x()}, {"y", it.rect.y()},
                                 {"w", it.rect.width()}, {"h", it.rect.height()}}}
        };
        return j;
    }
    CanvasItem itemFromJson(const QJsonObject &j) const {
        CanvasItem it;
        it.type = j.value("type").toString("text");
        it.name = j.value("name").toString(it.type == "button" ? "按钮" : "控件");
        it.text = j.value("text").toString(it.type == "button" ? "按钮" : "文本");
        it.font = j.value("font").toString();
        it.fontSize = j.value("fontSize").toInt(28);
        it.color = QColor(j.value("color").toString("#ffe08a"));
        it.anchor = j.value("anchor").toInt(0);
        it.script = j.value("script").toString();
        it.imagePath = j.value("imagePath").toString();
        it.hasBody = j.value("hasBody").toBool(false);
        it.isStatic = j.value("isStatic").toBool(false);
        it.inputText = j.value("inputText").toString();
        it.imgNormal = j.value("imgNormal").toString();
        it.imgHover = j.value("imgHover").toString();
        it.imgPressed = j.value("imgPressed").toString();
        it.bindScript = j.value("bindScript").toString();
        it.nineSlice = j.value("nineSlice").toBool(false);
        it.sliceMargin = j.value("sliceMargin").toInt(16);
        it.layout = j.value("layout").toInt(0);
        it.spacing = j.value("spacing").toDouble(12);
        it.parent = j.value("parent").toInt(-1);
        it.value = j.value("value").toDouble(60);
        it.checked = j.value("checked").toBool(false);
        QJsonArray opts = j.value("options").toArray();
        if (!opts.isEmpty()) { it.options.clear(); for (const auto &o : opts) it.options << o.toString(); }
        it.tweenType = j.value("tweenType").toInt(0);
        it.tweenDur = j.value("tweenDur").toDouble(0.5);
        it.tweenEase = j.value("tweenEase").toInt(2);
        QJsonObject r = j.value("rect").toObject();
        it.rect = QRectF(r.value("x").toDouble(100), r.value("y").toDouble(100),
                         r.value("w").toDouble(240), r.value("h").toDouble(56));
        return it;
    }

    bool loadFile(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        auto doc = QJsonDocument::fromJson(f.readAll());
        items.clear();
        selected = -1;
        QJsonObject root = doc.object();
        if (root.value("kind").toString() == "scene2d" || path.endsWith(".scene2d")) {
            mode = PhysMode;
            gravity = float(root.value("gravity").toDouble(980));
            for (const auto &v : root.value("boxes").toArray()) {
                CanvasItem it = itemFromJson(v.toObject());
                it.type = "box2d";
                it.hasBody = true;
                if (it.name.isEmpty()) it.name = "Box2D";
                items.push_back(it);
            }
            savePath = path;
        } else {
            mode = UiMode;
            for (const auto &v : root.value("items").toArray())
                items.push_back(itemFromJson(v.toObject()));
            savePath = path;
        }
        qInfo() << QString("[2D] 已加载 %1（%2 个元素，%3 模式）")
            .arg(QFile(path).fileName()).arg(items.size()).arg(mode == UiMode ? "UI" : "纯 2D");
        emit selectionChanged();
        emit itemsChanged();
        update();
        return true;
    }

    bool saveToFile(const QString &path) {
        QJsonObject root;
        if (mode == PhysMode) {
            QJsonArray boxes;
            for (const auto &it : items) boxes.append(itemJson(it));
            root = QJsonObject{{"kind", "scene2d"}, {"name", QFileInfo(path).fileName()},
                               {"gravity", gravity}, {"boxes", boxes}};
        } else {
            QJsonArray arr;
            for (const auto &it : items) arr.append(itemJson(it));
            root = QJsonObject{{"kind", "uicanvas"}, {"name", QFileInfo(path).fileName()},
                               {"designWidth", DESIGN_W}, {"designHeight", DESIGN_H}, {"items", arr}};
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(root).toJson());
        savePath = path;
        qInfo() << QString("[2D] 已保存：%1（%2 个元素）").arg(QFile(path).fileName()).arg(items.size());
        emit saved();
        return true;
    }

    // 演示钩子：真实鼠标事件管线点击画布坐标（与用户点击同一条路径）
    void demoClickWidget(const QPointF &wp) {
        QPoint c(int(wp.x()), int(wp.y()));
        mousePressEvent(new QMouseEvent(QEvent::MouseButtonPress, QPointF(c),
                                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
        mouseReleaseEvent(new QMouseEvent(QEvent::MouseButtonRelease, QPointF(c),
                                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    }

signals:
    void selectionChanged();
    void itemsChanged();
    void buttonClicked(int idx, const QString &name, const QString &script);
    void inputSubmitted(int idx, const QString &text, const QString &script);
    void valueChanged(int idx, const QString &name, double value);   // 滑动条 OnValueChanged
    void optionPicked(int idx, const QString &name, const QString &opt);  // 下拉框选择
    void saved();

protected:
    // ---- 拖拽状态：0..3 = 四角拉伸，-2 = 移动，-1 = 无 ----
    int dragKind = -1;
    QPointF dragOffset;
    QPointF dragStartItem;
    int pressButton = -1;
    QPointF pressPos;
    int focusedInput = -1;        // 运行态聚焦的输入框序号

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(sx(), sy());

        // 棋盘底
        p.fillRect(QRectF(0, 0, DESIGN_W, DESIGN_H), QColor("#262a30"));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2d3238"));
        for (int y = 0; y < DESIGN_H; y += 40)
            for (int x = ((y / 40) % 2) * 40; x < DESIGN_W; x += 80)
                p.drawRect(QRectF(x, y, 40, 40));

        // 纯 2D 模式：地面
        if (mode == PhysMode) {
            p.fillRect(groundRect(), QColor("#2c4a33"));
            p.setPen(QColor("#6fd68a"));
            QFont gf("Noto Sans CJK SC", 16);
            p.setFont(gf);
            p.drawText(groundRect().adjusted(12, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       "地面 Ground（BoxCollider2D 静态碰撞体）");
            // 得分 HUD
            p.setPen(QColor("#ffe08a"));
            QFont sf("Noto Sans CJK SC", 20, QFont::Bold);
            p.setFont(sf);
            p.drawText(QRectF(20, 12, 900, 36), Qt::AlignLeft,
                       QString("得分: %1      A/D 移动 · 空格跳跃").arg(score));
        }

        // 元素
        for (size_t i = 0; i < items.size(); ++i) {
            const CanvasItem &it = items[i];
            bool hovered = (running && hoverIdx == int(i));
            bool pressed = (running && pressedIdx == int(i));
            if (it.type == "button") {
                // 三态图：有图纯图片渲染（无边框），无图走绘制风格
                QString imgPath = pressed && !it.imgPressed.isEmpty() ? it.imgPressed
                                : hovered && !it.imgHover.isEmpty() ? it.imgHover
                                : it.imgNormal;
                if (!imgPath.isEmpty()) {
                    QImage img = imageFor(imgPath);
                    if (!img.isNull()) {
                        if (it.nineSlice && it.sliceMargin > 0) drawNineSlice(p, img, it.rect, it.sliceMargin);
                        else p.drawImage(it.rect, img);
                        if (!it.text.isEmpty()) {
                            QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize);
                            f.setBold(true);
                            p.setFont(f);
                            p.setPen(it.color);
                            p.drawText(it.rect, Qt::AlignCenter, it.text);
                        }
                        continue;
                    }
                }
                // 无图绘制风格：运行态反馈（悬停提亮 / 按下压暗），无外边框
                QColor fill = QColor(30, 70, 130, 215);
                if (pressed) fill = QColor(20, 46, 90, 230);
                else if (hovered) fill = QColor(46, 104, 190, 230);
                p.setPen(Qt::NoPen);
                p.setBrush(fill);
                p.drawRoundedRect(it.rect, 10, 10);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize);
                f.setBold(true);
                p.setFont(f);
                p.setPen(it.color);
                p.drawText(it.rect, Qt::AlignCenter, it.text);
            } else if (it.type == "text") {
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize);
                f.setBold(true);
                p.setFont(f);
                p.setPen(it.color);
                p.drawText(it.rect, Qt::AlignLeft | Qt::AlignVCenter, it.text);
            } else if (it.type == "textarea") {
                // 多行文本：半透明底 + 逐行绘制
                p.setPen(QPen(QColor(120, 190, 255, 120), 1.5));
                p.setBrush(QColor(16, 20, 26, 190));
                p.drawRoundedRect(it.rect, 8, 8);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize - 6);
                p.setFont(f);
                p.setPen(it.color);
                p.drawText(it.rect.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, it.text);
            } else if (it.type == "input") {
                // 输入框：圆角输入框 + 文字 + 光标（运行态聚焦时光标闪烁边框变色）
                bool focused = (running && focusedInput == int(i));
                p.setPen(QPen(focused ? QColor("#7fdcff") : QColor(120, 190, 255, 235),
                              focused ? 3.0 : 2.0));
                p.setBrush(QColor(20, 26, 34, 220));
                p.drawRoundedRect(it.rect, 8, 8);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize - 4);
                p.setFont(f);
                p.setPen(QColor("#e6ecf5"));
                QString shown = it.inputText.isEmpty() ? QString("请输入…") : it.inputText;
                if (it.inputText.isEmpty()) p.setPen(QColor("#6a7484"));
                QRectF txtR = it.rect.adjusted(12, 0, -12, 0);
                // 光标：聚焦时在文本末尾画竖线（取文本宽度）
                if (focused) {
                    QFontMetrics fm(f);
                    int tw = fm.horizontalAdvance(it.inputText);
                    QRectF cur = txtR.adjusted(tw + 1, 8, 0, -8);
                    p.fillRect(cur, QColor("#7fdcff"));
                    p.drawText(txtR, Qt::AlignLeft | Qt::AlignVCenter, shown);
                } else {
                    p.drawText(txtR, Qt::AlignLeft | Qt::AlignVCenter, shown);
                }
            } else if (it.type == "image") {
                QImage img = imageFor(it.imagePath);
                if (!img.isNull()) {
                    if (it.nineSlice && it.sliceMargin > 0) drawNineSlice(p, img, it.rect, it.sliceMargin);
                    else p.drawImage(it.rect, img);
                }
                else { p.setPen(QColor(255, 120, 120)); p.drawText(it.rect, Qt::AlignCenter, "图片缺失"); }
            } else if (it.type == "box2d") {
                if (it.name.startsWith("金币")) {
                    // 金币：金色圆盘 + ★ 高光
                    p.setPen(QPen(QColor("#b8860b"), 2));
                    p.setBrush(QColor("#ffd24a"));
                    p.drawEllipse(it.rect);
                    p.setPen(QColor("#b8860b"));
                    QFont cf("Noto Sans CJK SC", 16, QFont::Bold);
                    p.setFont(cf);
                    p.drawText(it.rect, Qt::AlignCenter, "★");
                } else {
                    p.setPen(QPen(QColor(255, 255, 255, 90), 1.5));
                    p.setBrush(QColor(it.color));
                    p.drawRoundedRect(it.rect, 4, 4);
                }
            } else if (it.type == "panel") {
                // 面板容器：半透明圆角底 + 可选布局
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(it.color.red(), it.color.green(), it.color.blue(), 200));
                p.drawRoundedRect(it.rect, 14, 14);
                if (it.layout > 0) {
                    QFont f("Noto Sans CJK SC", 14);
                    p.setFont(f);
                    p.setPen(QColor(255, 255, 255, 90));
                    p.drawText(it.rect.adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop,
                               it.layout == 1 ? "VBox 纵向排列" : "HBox 横向排列");
                }
            } else if (it.type == "dialoguebox") {
                // 对话框面板：九宫格拉伸底板 + 两个子节点（角色名文本 / 对话内容文本）
                p.setPen(QPen(QColor(140, 180, 235, 160), 2));
                p.setBrush(QColor(it.color.red(), it.color.green(), it.color.blue(), it.color.alpha()));
                p.drawRoundedRect(it.rect, 12, 12);
                // 九宫格参考线：显示 24px 拉伸边角
                p.setPen(QPen(QColor(140, 180, 235, 60), 1, Qt::DashLine));
                p.drawLine(QPointF(it.rect.left() + 24, it.rect.top()), QPointF(it.rect.left() + 24, it.rect.bottom()));
                p.drawLine(QPointF(it.rect.right() - 24, it.rect.top()), QPointF(it.rect.right() - 24, it.rect.bottom()));
                p.drawLine(QPointF(it.rect.left(), it.rect.top() + 24), QPointF(it.rect.right(), it.rect.top() + 24));
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(13, it.fontSize - 8));
                p.setFont(f);
                p.setPen(QColor("#ffd479"));
                p.drawText(QRectF(it.rect.left() + 22, it.rect.top() + 10, it.rect.width() - 44, 30),
                           Qt::AlignLeft, it.text.isEmpty() ? "角色名" : it.text);      // 子节点1
                p.setPen(QColor("#eef4ff"));
                p.drawText(QRectF(it.rect.left() + 22, it.rect.top() + 52, it.rect.width() - 44, it.rect.height() - 66),
                           Qt::AlignLeft | Qt::TextWordWrap,
                           "对话内容文本……");                                          // 子节点2
            } else if (it.type == "portrait") {
                // 角色立绘：人形剪影 + 左/中/右三锚点提示
                p.setPen(QPen(QColor(it.color.lighter(140)), 2));
                p.setBrush(QColor(it.color.red(), it.color.green(), it.color.blue(), 190));
                p.drawRoundedRect(it.rect, 18, 18);
                const char *anchorTxt = it.text == "right" ? "右锚点 x=0.82"
                                      : it.text == "center" ? "中锚点 x=0.50" : "左锚点 x=0.18";
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(12, it.fontSize - 10));
                p.setFont(f);
                p.setPen(QColor("#e6d9ff"));
                p.drawText(QRectF(it.rect.left(), it.rect.top() + 8, it.rect.width(), 26),
                           Qt::AlignCenter, anchorTxt);
            } else if (it.type == "choicebutton") {
                // 选项分支按钮：主菜单式按钮 + 双执行流出口
                bool hov = hoverIdx == int(i);
                p.setPen(QPen(QColor(hov ? QColor("#8fe0b0") : QColor(120, 200, 160, 200)), 2));
                p.setBrush(QColor(it.color.red(), it.color.green(), it.color.blue(), hov ? 240 : 210));
                p.drawRoundedRect(it.rect, 10, 10);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(13, it.fontSize - 8));
                p.setFont(f);
                p.setPen(QColor("#eafff2"));
                p.drawText(it.rect, Qt::AlignCenter, it.text.isEmpty() ? "选项 A" : it.text);
                p.setPen(QPen(QColor(140, 220, 175, 150), 1, Qt::DashLine));
                p.drawLine(QPointF(it.rect.right(), it.rect.center().y() - 8), QPointF(it.rect.right() + 28, it.rect.center().y() - 8));
                p.drawLine(QPointF(it.rect.right(), it.rect.center().y() + 8), QPointF(it.rect.right() + 28, it.rect.center().y() + 8));
            } else if (it.type == "slider") {
                // 滑动条：轨道 + 已填充段 + 圆形手柄
                double h = it.rect.height() * 0.28;
                QRectF track(it.rect.left(), it.rect.center().y() - h / 2, it.rect.width(), h);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#39414d"));
                p.drawRoundedRect(track, h / 2, h / 2);
                double k = qBound(0.0, it.value / 100.0, 1.0);
                QRectF filled(track.left(), track.top(), track.width() * k, h);
                p.setBrush(QColor(hovered || pressedIdx == int(i) ? "#6fc3ff" : "#4aa3e0"));
                p.drawRoundedRect(filled, h / 2, h / 2);
                double cx = track.left() + track.width() * k;
                p.setBrush(QColor("#e6ecf5"));
                p.drawEllipse(QPointF(cx, track.center().y()), h * 1.5, h * 1.5);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(12, it.fontSize - 10));
                p.setFont(f);
                p.setPen(QColor("#9fb0c8"));
                p.drawText(QRectF(it.rect.left(), it.rect.top() - 4, it.rect.width(), 20),
                           Qt::AlignLeft, it.text);
                p.drawText(QRectF(it.rect.left(), it.rect.bottom() - 16, it.rect.width(), 20),
                           Qt::AlignRight, QString("%1%").arg(int(it.value)));
            } else if (it.type == "checkbox") {
                // 复选框：方框 + 对勾
                double s = qMin(it.rect.height(), 36.0);
                QRectF box(it.rect.left(), it.rect.center().y() - s / 2, s, s);
                p.setPen(QPen(QColor(120, 190, 255, 235), 2.2));
                p.setBrush(it.checked ? QColor("#4aa3e0") : QColor(20, 26, 34, 220));
                p.drawRoundedRect(box, 6, 6);
                if (it.checked) {
                    QPen pen(QColor("#eaffff"), 3.2);
                    p.setPen(pen);
                    QPainterPath path;
                    path.moveTo(box.left() + s * 0.2, box.center().y());
                    path.lineTo(box.center().x() - s * 0.05, box.bottom() - s * 0.22);
                    path.lineTo(box.right() - s * 0.15, box.top() + s * 0.2);
                    p.drawPath(path);
                }
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize - 6);
                p.setFont(f);
                p.setPen(it.color);
                p.drawText(QRectF(box.right() + 12, it.rect.top(), it.rect.width() - s - 12, it.rect.height()),
                           Qt::AlignLeft | Qt::AlignVCenter, it.text);
            } else if (it.type == "dropdown") {
                // 下拉框：圆角框 + 当前项 + 下拉箭头
                p.setPen(QPen(QColor(120, 190, 255, 235), 2.0));
                p.setBrush(QColor(20, 26, 34, 220));
                p.drawRoundedRect(it.rect, 8, 8);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize - 6);
                p.setFont(f);
                p.setPen(QColor("#e6ecf5"));
                p.drawText(it.rect.adjusted(14, 0, -40, 0), Qt::AlignLeft | Qt::AlignVCenter, it.text);
                double s = it.rect.height() * 0.3;
                QPointF c(it.rect.right() - s * 1.4, it.rect.center().y());
                QPolygonF tri{QPointF(c.x() - s / 2, c.y() - s / 4), QPointF(c.x() + s / 2, c.y() - s / 4), QPointF(c.x(), c.y() + s / 2)};
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#9fb0c8"));
                p.drawPolygon(tri);
            } else if (it.type == "toggle") {
                // 开关：胶囊 + 圆点（开=绿）
                double h = it.rect.height() * 0.62;
                QRectF pill(it.rect.left(), it.rect.center().y() - h / 2, qMin(it.rect.width() * 0.5, h * 2.0), h);
                p.setPen(Qt::NoPen);
                p.setBrush(it.checked ? QColor("#54d68a") : QColor("#39414d"));
                p.drawRoundedRect(pill, h / 2, h / 2);
                double r = h * 0.38;
                QPointF dot(it.checked ? pill.right() - h / 2 : pill.left() + h / 2, pill.center().y());
                p.setBrush(QColor("#ffffff"));
                p.drawEllipse(dot, r, r);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, it.fontSize - 6);
                p.setFont(f);
                p.setPen(it.color);
                p.drawText(QRectF(pill.right() + 14, it.rect.top(), it.rect.width() - pill.width() - 14, it.rect.height()),
                           Qt::AlignLeft | Qt::AlignVCenter, it.text);
            } else if (it.type == "scrollview") {
                // 滚动视图：内容区裁剪 + 右侧滚动条
                p.setPen(QPen(QColor(120, 190, 255, 120), 1.5));
                p.setBrush(QColor(16, 20, 26, 200));
                p.drawRect(it.rect);
                p.setClipRect(it.rect);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(12, it.fontSize - 8));
                p.setFont(f);
                p.setPen(QColor("#9fb0c8"));
                double lh = 30.0;
                double y = it.rect.top() + 10;
                for (int ln = 1; y < it.rect.bottom() - 6; ++ln, y += lh)
                    p.drawText(QRectF(it.rect.left() + 12, y, it.rect.width() - 40, lh),
                               Qt::AlignLeft | Qt::AlignVCenter, QString("%1. 长文本内容行…（滚动查看）").arg(ln));
                p.setClipping(false);
                QRectF sb(it.rect.right() - 10, it.rect.top() + 4, 6, it.rect.height() - 8);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#4a5568"));
                p.drawRoundedRect(sb, 3, 3);
                QRectF thumb(sb.left(), sb.top(), 6, sb.height() * 0.35);
                p.setBrush(QColor("#9fb0c8"));
                p.drawRoundedRect(thumb, 3, 3);
            } else if (it.type == "grid") {
                // 网格布局：背包格子
                p.setPen(QPen(QColor(120, 190, 255, 160), 1.5));
                p.setBrush(QColor(16, 20, 26, 200));
                p.drawRoundedRect(it.rect, 10, 10);
                int cols = 2;
                double cw = (it.rect.width() - 30) / cols;
                double ch = cw;
                int rows = 3;
                for (int rr = 0; rr < rows; ++rr)
                    for (int cc = 0; cc < cols; ++cc) {
                        QRectF cell(it.rect.left() + 10 + cc * (cw + 10), it.rect.top() + 10 + rr * (ch + 10), cw, ch);
                        p.setBrush(QColor("#232a34"));
                        p.drawRoundedRect(cell, 6, 6);
                    }
            } else if (it.type == "progress") {
                // 进度条：底槽 + 填充段 + 百分比（血条/加载条）
                double h = it.rect.height() * 0.55;
                QRectF track(it.rect.left(), it.rect.center().y() - h / 2, it.rect.width(), h);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#20262e"));
                p.drawRoundedRect(track, h / 2, h / 2);
                double k = qBound(0.0, it.value / 100.0, 1.0);
                QRectF filled(track.left(), track.top(), track.width() * k, h);
                p.setBrush(QColor(it.color));
                p.drawRoundedRect(filled, h / 2, h / 2);
                QFont f(it.font.isEmpty() ? "Noto Sans CJK SC" : it.font, qMax(11, it.fontSize - 12));
                p.setFont(f);
                p.setPen(QColor("#e6ecf5"));
                p.drawText(track, Qt::AlignCenter, QString("%1  %2%").arg(it.text).arg(int(it.value)));
            } else if (it.type == "mask") {
                // 遮罩：圆角裁剪容器（圆角头像用），内部画演示头像
                QPainterPath clip;
                clip.addRoundedRect(it.rect, qMin(it.rect.width(), it.rect.height()) / 2.4, qMin(it.rect.width(), it.rect.height()) / 2.4);
                p.setClipPath(clip);
                QImage img = imageFor(it.imagePath);
                if (!img.isNull()) p.drawImage(it.rect, img);
                else {
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor("#3d4b5c"));
                    p.drawRoundedRect(it.rect, 20, 20);
                    p.setPen(QColor("#8fa3bd"));
                    p.drawText(it.rect, Qt::AlignCenter, "遮罩");
                }
                p.setClipping(false);
            }
        }

        // 选中框 + 四角拉伸柄（UI 模式）
        if (selected >= 0 && mode == UiMode) {
            QRectF r = items[size_t(selected)].rect;
            QPen pen(QColor("#ffd24a"), 2, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            p.setPen(QPen(QColor("#20242a"), 2));
            p.setBrush(QColor("#ffd24a"));
            for (int h = 0; h < 4; ++h) {
                QPointF c((h % 2) ? r.right() : r.left(), (h / 2) ? r.bottom() : r.top());
                p.drawRect(QRectF(c.x() - 8, c.y() - 8, 16, 16));
            }
        }
        p.end();
    }

    void mousePressEvent(QMouseEvent *e) override {
        QPointF dp = toDesign(e->pos());
        pressPos = dp;
        pressButton = -1;
        int h = hitHandle(dp);
        if (h >= 0) { dragKind = h; return; }
        int idx = hitItem(dp);
        if (idx >= 0) {
            selected = idx;
            emit selectionChanged();
            CanvasItem &it = items[size_t(idx)];
            if (running) {
                // 运行态：按钮 -> 进入按下态（松开时发射信号）
                if (it.type == "button") {
                    pressedIdx = idx;
                    qInfo() << QString("[UI] 命中画布按钮：%1").arg(it.name);
                    update();
                    return;
                }
                // 运行态：滑动条 -> 进入拖动（真实改变 value）
                if (it.type == "slider") { dragKind = -3; setSliderValue(idx, dp); return; }
                // 运行态：复选框 / 开关 -> 真实切换
                if (it.type == "checkbox" || it.type == "toggle") {
                    it.checked = !it.checked;
                    qInfo() << QString("[UI] OnValueChanged: %1 -> %2").arg(it.name, it.checked ? "true" : "false");
                    emit valueChanged(idx, it.name, it.checked ? 1 : 0);
                    update();
                    return;
                }
                // 运行态：下拉框 -> 弹出选项菜单（真实选择）
                if (it.type == "dropdown") { showDropdownMenu(idx); return; }
                // 运行态：输入框 -> 聚焦并接收键盘输入
                if (it.type == "input") {
                    focusedInput = idx;
                    setFocus();
                    qInfo() << QString("[UI] 输入框已聚焦：%1").arg(it.name);
                    update();
                    return;
                }
            }
            dragKind = -2;
            dragOffset = dp - it.rect.topLeft();
            dragStartItem = it.rect.topLeft();
            update();
            return;
        }
        selected = -1;
        emit selectionChanged();
        update();
    }

    // 滑动条真实拖动：按横向位置换算 0-100
    void setSliderValue(int idx, const QPointF &dp) {
        CanvasItem &it = items[size_t(idx)];
        double k = (dp.x() - it.rect.left()) / qMax(1.0, it.rect.width());
        it.value = qBound(0.0, k * 100.0, 100.0);
        qInfo() << QString("[UI] OnValueChanged: %1 -> %2%").arg(it.name).arg(int(it.value));
        emit valueChanged(idx, it.name, it.value);
        update();
    }

    // 下拉框：弹出选项菜单
    void showDropdownMenu(int idx) {
        CanvasItem &it = items[size_t(idx)];
        QMenu menu(this);
        for (const QString &o : it.options) {
            menu.addAction(o, [this, idx, o]{
                CanvasItem &d = items[size_t(idx)];
                d.text = o;
                qInfo() << QString("[UI] OnValueChanged: %1 -> %2").arg(d.name, o);
                emit optionPicked(idx, d.name, o);
                update();
            });
        }
        menu.exec(QCursor::pos());
    }

    // 运行态键盘输入：聚焦输入框时接收字符，Enter 提交挂载脚本；物理模式记录按键状态
    void keyPressEvent(QKeyEvent *e) override {
        if (mode == PhysMode && running) keyDown[e->key()] = true;
        if (running && focusedInput >= 0 && size_t(focusedInput) < items.size()) {
            CanvasItem &it = items[size_t(focusedInput)];
            if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
                qInfo() << QString("[UI] 输入提交：%1（%2）").arg(it.inputText, it.name);
                emit inputSubmitted(focusedInput, it.inputText, it.script);
                it.inputText.clear();
                update();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_Backspace) {
                it.inputText.chop(1);
                update();
                e->accept();
                return;
            }
            QString t = e->text();
            if (t.size() == 1 && t.at(0).isPrint()) {
                it.inputText += t;
                update();
                e->accept();
                return;
            }
        }
        QWidget::keyPressEvent(e);
    }

    void keyReleaseEvent(QKeyEvent *e) override {
        if (mode == PhysMode && running) keyDown[e->key()] = false;
        QWidget::keyReleaseEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        QPointF dp = toDesign(e->pos());
        if (dragKind == -3) {                                   // 滑动条拖动
            setSliderValue(selected, dp);
            return;
        }
        if (dragKind == -1) {                                   // 悬停追踪（运行态）
            int h = hitItem(dp);
            if (h != hoverIdx) { hoverIdx = h; update(); }
            return;
        }
        CanvasItem &it = items[size_t(selected)];
        if (dragKind == -2) {                                   // 自由拖拽
            it.rect.moveTopLeft(dp - dragOffset);
            applyLayout();
            emit itemsChanged();
        } else {                                                // 四角拉伸
            qreal x1 = dragStartItem.x(), y1 = dragStartItem.y();
            qreal x2 = dragStartItem.x() + it.rect.width(), y2 = dragStartItem.y() + it.rect.height();
            if (dragKind == 0) { x1 = dp.x(); y1 = dp.y(); }
            else if (dragKind == 1) { x2 = dp.x(); y1 = dp.y(); }
            else if (dragKind == 2) { x1 = dp.x(); y2 = dp.y(); }
            else { x2 = dp.x(); y2 = dp.y(); }
            it.rect = QRectF(QPointF(qMin(x1, x2), qMin(y1, y2)),
                             QSizeF(qMax(24.0, qAbs(x2 - x1)), qMax(20.0, qAbs(y2 - y1))));
            applyLayout();
            emit itemsChanged();
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (dragKind == -3) { dragKind = -1; return; }
        // 按钮：松开时若仍在按钮上 -> 发射 Qt 信号（Signals & Slots）+ 触发绑定脚本
        if (pressedIdx >= 0 && size_t(pressedIdx) < items.size()) {
            CanvasItem &it = items[size_t(pressedIdx)];
            QPointF dp = toDesign(e->pos());
            bool inside = it.rect.contains(dp) || it.rect.adjusted(-6, -6, 6, 6).contains(dp);
            int idx = pressedIdx;
            pressedIdx = -1;
            if (inside && it.type == "button") {
                qInfo() << QString("[Signal Emitted] %1 clicked!").arg(it.name);
                emit buttonClicked(idx, it.name, it.bindScript.isEmpty() ? it.script : it.bindScript);
                update();
            }
        }
        dragKind = -1;
        update();
    }

    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        QString path = e->mimeData()->urls().isEmpty() ? QString()
                                                       : e->mimeData()->urls().first().toLocalFile();
        if (!path.endsWith(".png", Qt::CaseInsensitive) && !path.endsWith(".jpg", Qt::CaseInsensitive)
            && !path.endsWith(".jpeg", Qt::CaseInsensitive) && !path.endsWith(".bmp", Qt::CaseInsensitive)
            && !path.endsWith(".webp", Qt::CaseInsensitive))
            return;
        CanvasItem it;
        it.type = "image";
        it.name = QFileInfo(path).fileName();
        it.imagePath = path;
        QImage img(path);
        QSizeF sz = img.isNull() ? QSizeF(300, 220)
                                 : QSizeF(360, 360.0 * img.height() / qMax(1, img.width()));
        it.rect = QRectF(toDesign(e->position().toPoint()) - QPointF(sz.width() / 2, sz.height() / 2), sz);
        addItem(it);
        qInfo() << QString("[2D] 图片资产拖入画布：%1").arg(it.name);
        e->acceptProposedAction();
    }

private:
    std::map<QString, QImage> imgCache;
    QImage &imageFor(const QString &path) {   // 命中即缓存
        auto it = imgCache.find(path);
        if (it == imgCache.end()) it = imgCache.insert({path, QImage(path)}).first;
        return it->second;
    }

    // 九宫格切图：四角固定、边拉伸、中心整块拉伸（圆角不变形）
    void drawNineSlice(QPainter &p, const QImage &img, const QRectF &r, int m) {
        m = qBound(1, std::min({m, int(img.width() / 2) - 1, int(img.height() / 2) - 1}), 200);
        qreal iw = img.width(), ih = img.height();
        qreal rw = r.width(), rh = r.height();
        if (rw < 2 * m || rh < 2 * m) { p.drawImage(r, img); return; }
        QRectF src[9] = {
            QRectF(0, 0, m, m), QRectF(m, 0, iw - 2 * m, m), QRectF(iw - m, 0, m, m),
            QRectF(0, m, m, ih - 2 * m), QRectF(m, m, iw - 2 * m, ih - 2 * m), QRectF(iw - m, m, m, ih - 2 * m),
            QRectF(0, ih - m, m, m), QRectF(m, ih - m, iw - 2 * m, m), QRectF(iw - m, ih - m, m, m)
        };
        qreal xs[4] = {r.left(), r.left() + m, r.right() - m, r.right()};
        qreal ys[4] = {r.top(), r.top() + m, r.bottom() - m, r.bottom()};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                p.drawImage(QRectF(QPointF(xs[col], ys[row]),
                                   QPointF(xs[col + 1], ys[row + 1])), img, src[row * 3 + col]);
    }
};

// ============ 主窗口 ============
// 资产文件树：共享资产目录（~/Physics3DProjects/Demo火锅店项目/assets），外部文件拖入即导入
class AssetTreeView : public QTreeView {
    Q_OBJECT
public:
    using QTreeView::QTreeView;
    QString assetsDir;
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        int imported = 0;
        for (const QUrl &u : e->mimeData()->urls()) {
            QString src = u.toLocalFile();
            if (src.isEmpty()) continue;
            if (assetsDir.isEmpty()) continue;
            QString dst = assetsDir + "/" + QFileInfo(src).fileName();
            if (src == dst || QFile(dst).exists() || QFile::copy(src, dst)) ++imported;
        }
        if (imported > 0)
            qInfo() << QString("[2D] 资产树：外部拖入导入 %1 个文件 -> 共享资产目录").arg(imported);
        e->acceptProposedAction();
    }
};

// ============ 拖拽槽位：属性面板的图片/脚本槽（资产树拖入） ============
class DropSlot : public QLineEdit {
    Q_OBJECT
public:
    using QLineEdit::QLineEdit;
    QStringList acceptExts;
    QString kindName;
signals:
    void pathDropped(const QString &path);
public:
    // 公开演示入口：直调真实 dropEvent（与用户拖拽同一条管线）
    void demoDropEvent(QDropEvent *e) { dropEvent(e); }
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) { e->acceptProposedAction(); setStyleSheet(styleSheet().replace("border:2px dashed #4a5568", "border:2px solid #4ad58a")); }
    }
    void dragLeaveEvent(QDragLeaveEvent *) override {
        setStyleSheet(styleSheet().replace("border:2px solid #4ad58a", "border:2px dashed #4a5568"));
    }
    void dropEvent(QDropEvent *e) override {
        setStyleSheet(styleSheet().replace("border:2px solid #4ad58a", "border:2px dashed #4a5568"));
        if (e->mimeData()->urls().isEmpty()) return;
        QString path = e->mimeData()->urls().first().toLocalFile();
        bool ok = acceptExts.isEmpty();
        for (const QString &ext : acceptExts)
            if (path.endsWith(ext, Qt::CaseInsensitive)) ok = true;
        if (!ok) {
            qInfo() << QString("[UI] %1 槽位：仅接受 %2 文件").arg(kindName, acceptExts.join("/"));
            return;
        }
        setText(QFileInfo(path).fileName());
        emit pathDropped(path);
        e->acceptProposedAction();
    }
};

// ============ 全局搜索（Ctrl+P）：资产 / 画布对象 / 脚本内容，输入即搜 ============
class QuickSearchDialog : public QDialog {
    Q_OBJECT
public:
    struct Entry { QString title; QString detail; };
    explicit QuickSearchDialog(QWidget *parent, std::vector<Entry> entries)
        : QDialog(parent), all(std::move(entries)) {
        setWindowTitle("全局搜索 (Ctrl+P)");
        resize(580, 500);
        move(470, 170);
        auto *lay = new QVBoxLayout(this);
        edit = new QLineEdit;
        edit->setPlaceholderText("输入即搜：资产文件 / 画布对象 / Lua 脚本内容…");
        edit->setStyleSheet("padding:8px; font-size:15px;");
        list = new QListWidget;
        list->setStyleSheet("QListWidget{background:#15181e; color:#e6ecf5; border:none; font-size:13px;}");
        lay->addWidget(edit);
        lay->addWidget(list, 1);
        connect(edit, &QLineEdit::textChanged, this, &QuickSearchDialog::refill);
        refill(QString());
    }
private:
    std::vector<Entry> all;
    QLineEdit *edit = nullptr;
    QListWidget *list = nullptr;
    void refill(const QString &q) {
        list->clear();
        int n = 0;
        for (const auto &e : all) {
            if (!q.isEmpty() && !e.title.contains(q, Qt::CaseInsensitive)
                && !e.detail.contains(q, Qt::CaseInsensitive)) continue;
            if (++n > 300) break;
            list->addItem(QString("%1    —  %2").arg(e.title, e.detail));
        }
        if (!q.isEmpty())
            qInfo() << QString("[Search] 全局搜索 \"%1\"：命中 %2 条").arg(q).arg(qMin(n, 300));
    }
};

class C2DWindow : public QMainWindow {
    Q_OBJECT
public:
    CanvasWidget *canvas = nullptr;
    CanvasWidget *gameCanvas = nullptr;        // Game 标签页：运行态真实画面（深拷贝场景）
    QTabWidget *tabs = nullptr;
    QWidget *editorTab = nullptr, *gameTab = nullptr;
    QListWidget *libList = nullptr;
    QListWidget *imgList = nullptr;
    AssetTreeView *fileTree = nullptr;
    QFileSystemModel *fsModel = nullptr;
    QPlainTextEdit *console = nullptr;
    QDockWidget *consoleDock = nullptr;
    QDockWidget *dockFile = nullptr;
    QDockWidget *dockProps = nullptr;

    QLineEdit *editText = nullptr;
    QComboBox *comboFont = nullptr;
    QSpinBox *spinFontSize = nullptr;
    QPushButton *btnColor = nullptr;
    QComboBox *comboAnchor = nullptr;
    QDoubleSpinBox *spX = nullptr, *spY = nullptr, *spW = nullptr, *spH = nullptr;
    QLineEdit *scriptSlot = nullptr;
    QAction *actRun = nullptr;
    // ---- Button Component：三态图槽位 + 绑定信号槽 ----
    DropSlot *slotNormal = nullptr, *slotHover = nullptr, *slotPressed = nullptr, *slotBind = nullptr;
    // ---- 九宫格 / 布局 / Tween / 值类 ----
    QCheckBox *chkNineSlice = nullptr;
    QSpinBox *spinSliceMargin = nullptr;
    QComboBox *comboLayout = nullptr;
    QDoubleSpinBox *spinSpacing = nullptr;
    QComboBox *comboTween = nullptr;
    QDoubleSpinBox *spinTweenDur = nullptr;
    QComboBox *comboEase = nullptr;
    QSpinBox *spinValue = nullptr;
    QLineEdit *editOptions = nullptr;

    C2DWindow() {
        setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        resize(1500, 900);
        canvas = new CanvasWidget;
        libList = new QListWidget;
        imgList = new QListWidget;

        // ---- 左侧：控件库 + 图片资产 ----
        auto *leftBox = new QWidget;
        auto *lv = new QVBoxLayout(leftBox);
        lv->setContentsMargins(4, 8, 4, 4);
        lv->addWidget(new QLabel("控件库（双击添加）"));
        const char *libNames[18] = {
            "按钮  Button", "文本  Text", "图片  Image", "输入框  InputField",
            "滑动条  Slider", "复选框  Checkbox", "下拉框  Dropdown", "开关  Toggle",
            "面板  Panel", "滚动视图  ScrollView", "网格布局  GridLayout", "进度条  ProgressBar",
            "遮罩  Mask", "多行文本  TextArea", "刚体方块  Box2D",
            "对话框面板  DialogueBox", "角色立绘  Portrait", "选项分支按钮  ChoiceButton"};
        for (const char *n : libNames) libList->addItem(QString(n));
        lv->addWidget(libList);
        lv->addWidget(new QLabel("图片资产（拖入画布）"));
        lv->addWidget(imgList, 1);
        // 文件管理器（与 3DEditor 同一个 assets）→ 独立 QDockWidget，可拖出成独立窗口
        QString sharedAssets = QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets");
        QDir().mkpath(sharedAssets);
        fsModel = new QFileSystemModel(this);
        fsModel->setRootPath(sharedAssets);
        fileTree = new AssetTreeView;
        fileTree->assetsDir = sharedAssets;
        fileTree->setModel(fsModel);
        fileTree->setRootIndex(fsModel->index(sharedAssets));
        for (int c = 1; c < 4; ++c) fileTree->hideColumn(c);   // 只留名称列
        fileTree->setHeaderHidden(true);

        // ---- 右侧：属性面板（滚动区，控件多不怕装不下）----
        auto *rightBox = new QWidget;
        rightBox->setFixedWidth(300);
        auto *rv = new QVBoxLayout(rightBox);
        rv->setContentsMargins(8, 8, 8, 4);
        auto title = new QLabel("属性 (Properties)");
        title->setStyleSheet("font-weight:bold; font-size:14px;");
        rv->addWidget(title);
        auto *formHost = new QWidget;
        auto *form = new QVBoxLayout(formHost);
        form->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(formHost);
        scroll->setStyleSheet("QScrollArea{border:none;} QScrollArea>QWidget>QWidget{background:transparent;}");
        rv->addWidget(scroll, 1);
        form->addWidget(new QLabel("文本内容："));
        editText = new QLineEdit; form->addWidget(editText);
        form->addWidget(new QLabel("字体 (Font)："));
        comboFont = new QComboBox; form->addWidget(comboFont);
        auto *btnImport = new QPushButton("导入字体… (.ttf/.otf)");
        form->addWidget(btnImport);
        form->addWidget(new QLabel("字号："));
        spinFontSize = new QSpinBox; spinFontSize->setRange(8, 300); form->addWidget(spinFontSize);
        form->addWidget(new QLabel("颜色："));
        btnColor = new QPushButton; btnColor->setFixedHeight(30); form->addWidget(btnColor);
        form->addWidget(new QLabel("锚点 (Anchor)——分辨率变化时自适应："));
        comboAnchor = new QComboBox;
        comboAnchor->addItems({"左上 (Top-Left)", "中上 (Top)", "右上 (Top-Right)",
                               "左中 (Left)", "居中 (Center)", "右中 (Right)",
                               "左下 (Bottom-Left)", "中下 (Bottom)", "右下 (Bottom-Right)"});
        form->addWidget(comboAnchor);
        auto *grid = new QHBoxLayout;
        auto mk = [&](const char *t) {
            auto *box = new QVBoxLayout;
            box->addWidget(new QLabel(t));
            auto *sp = new QDoubleSpinBox;
            sp->setRange(-4000, 4000);
            box->addWidget(sp);
            grid->addLayout(box);
            return sp;
        };
        spX = mk("X"); spY = mk("Y"); spW = mk("W"); spH = mk("H");
        form->addLayout(grid);
        form->addWidget(new QLabel("挂载脚本 (.lua/.socket)："));
        scriptSlot = new QLineEdit;
        scriptSlot->setPlaceholderText("点击选择脚本…");
        scriptSlot->setReadOnly(true);
        scriptSlot->setStyleSheet(SLOT_STYLE);
        form->addWidget(scriptSlot);

        // ---- Button Component：三态图 + 绑定信号 ----
        auto *lblComp = new QLabel("▣ Button Component（组件）");
        lblComp->setStyleSheet("font-weight:bold; color:#7fdcff; margin-top:6px;");
        form->addWidget(lblComp);
        auto mkSlot = [&](const char *label, DropSlot *&slot, QStringList exts, const char *kind) {
            form->addWidget(new QLabel(label));
            slot = new DropSlot;
            slot->setReadOnly(true);
            slot->acceptExts = std::move(exts);
            slot->kindName = kind;
            slot->setPlaceholderText("从资产树拖入…");
            slot->setStyleSheet(SLOT_STYLE);
            slot->setFixedHeight(34);
            form->addWidget(slot);
        };
        mkSlot("【常态图片 (Normal)】", slotNormal, {"png", "jpg", "jpeg", "bmp", "webp"}, "常态图片");
        mkSlot("【悬停图片 (Hover)】", slotHover, {"png", "jpg", "jpeg", "bmp", "webp"}, "悬停图片");
        mkSlot("【按下图片 (Pressed)】", slotPressed, {"png", "jpg", "jpeg", "bmp", "webp"}, "按下图片");
        mkSlot("【绑定信号 (Bind Signal)】—— 拖入 Lua/插座蓝图", slotBind, {"lua", "socket"}, "绑定信号");
        // 槽位真实拖入回写（悬停/按下只对运行态生效，松开即发射信号）
        connect(slotNormal, &DropSlot::pathDropped, this, [this](const QString &p){ if (canvas->selected >= 0) { canvas->items[size_t(canvas->selected)].imgNormal = p; qInfo() << QString("[UI] 常态图片 (Normal) 已设置：%1").arg(QFile(p).fileName()); canvas->update(); } });
        connect(slotHover, &DropSlot::pathDropped, this, [this](const QString &p){ if (canvas->selected >= 0) { canvas->items[size_t(canvas->selected)].imgHover = p; qInfo() << QString("[UI] 悬停图片 (Hover) 已设置：%1（鼠标滑过按钮立即换图）").arg(QFile(p).fileName()); canvas->update(); } });
        connect(slotPressed, &DropSlot::pathDropped, this, [this](const QString &p){ if (canvas->selected >= 0) { canvas->items[size_t(canvas->selected)].imgPressed = p; qInfo() << QString("[UI] 按下图片 (Pressed) 已设置：%1（按住即换图，松开发射信号）").arg(QFile(p).fileName()); canvas->update(); } });
        connect(slotBind, &DropSlot::pathDropped, this, [this](const QString &p){
            if (canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].bindScript = p;
            qInfo() << QString("[2D] 绑定信号 (Bind Signal)：%1 -> %2").arg(canvas->items[size_t(canvas->selected)].name, QFileInfo(p).fileName());
        });

        // ---- 九宫格切图 ----
        auto *row9 = new QHBoxLayout;
        chkNineSlice = new QCheckBox("九宫格切图 (9-Slice)");
        row9->addWidget(chkNineSlice);
        spinSliceMargin = new QSpinBox;
        spinSliceMargin->setRange(1, 200);
        spinSliceMargin->setPrefix("边距 ");
        row9->addWidget(spinSliceMargin);
        form->addLayout(row9);

        // ---- 容器布局（VBox/HBox 自动排版）----
        form->addWidget(new QLabel("容器布局 (Layout)："));
        comboLayout = new QComboBox;
        comboLayout->addItems({"无 (手动)", "纵向排列 (VBox)", "横向排列 (HBox)"});
        form->addWidget(comboLayout);
        auto *rowSp = new QHBoxLayout;
        rowSp->addWidget(new QLabel("间距:"));
        spinSpacing = new QDoubleSpinBox;
        spinSpacing->setRange(0, 200);
        rowSp->addWidget(spinSpacing);
        rowSp->addStretch();
        form->addLayout(rowSp);

        // ---- 值类（滑动条/进度条/下拉选项）----
        auto *rowVal = new QHBoxLayout;
        rowVal->addWidget(new QLabel("值 Value:"));
        spinValue = new QSpinBox;
        spinValue->setRange(0, 100);
        rowVal->addWidget(spinValue);
        rowVal->addStretch();
        form->addLayout(rowVal);
        form->addWidget(new QLabel("下拉选项（逗号分隔）："));
        editOptions = new QLineEdit;
        editOptions->setPlaceholderText("1080P,720P,480P");
        form->addWidget(editOptions);

        // ---- Tween 缓动动画 ----
        auto *lblTween = new QLabel("▣ Tween 缓动动画");
        lblTween->setStyleSheet("font-weight:bold; color:#7fdcff; margin-top:6px;");
        form->addWidget(lblTween);
        comboTween = new QComboBox;
        comboTween->addItems({"无", "淡入（透明→不透明）", "右移 100 像素"});
        form->addWidget(comboTween);
        auto *rowTw = new QHBoxLayout;
        rowTw->addWidget(new QLabel("时长(秒):"));
        spinTweenDur = new QDoubleSpinBox;
        spinTweenDur->setRange(0.1, 5.0);
        spinTweenDur->setSingleStep(0.1);
        rowTw->addWidget(spinTweenDur);
        comboEase = new QComboBox;
        comboEase->addItems({"线性 Linear", "Ease-In", "Ease-Out"});
        rowTw->addWidget(comboEase);
        form->addLayout(rowTw);
        rv->addStretch();

        // ---- 中央布局：双标签页（场景编辑 Editor / 游戏运行 Game）----
        gameCanvas = new CanvasWidget;          // Game 标签页：独立画布，运行时深拷贝场景真实跑
        gameTab = new QWidget;
        auto *gv = new QVBoxLayout(gameTab);
        gv->setContentsMargins(0, 0, 0, 0);
        gv->addWidget(gameCanvas);

        editorTab = new QWidget;
        auto *h = new QHBoxLayout(editorTab);
        leftBox->setFixedWidth(200);
        h->addWidget(leftBox);
        h->addWidget(canvas, 1);

        tabs = new QTabWidget;
        tabs->setTabsClosable(false);
        tabs->setDocumentMode(true);
        tabs->addTab(editorTab, "场景编辑 (Editor)");
        tabs->addTab(gameTab, "游戏运行 (Game)");
        tabs->setCurrentWidget(editorTab);
        setCentralWidget(tabs);

        // ---- 停靠面板：文件管理器 / 属性面板 / 控制台（QDockWidget，可拖出独立窗口，最多 3 个）----
        dockFile = new QDockWidget("文件管理器 (assets)", this);
        dockFile->setWidget(fileTree);
        dockFile->setAllowedAreas(Qt::AllDockWidgetAreas);
        dockFile->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                              | QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, dockFile);
        resizeDocks({dockFile}, {260}, Qt::Horizontal);

        dockProps = new QDockWidget("属性面板 (Properties)", this);
        dockProps->setWidget(rightBox);
        dockProps->setAllowedAreas(Qt::AllDockWidgetAreas);
        dockProps->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                               | QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, dockProps);
        resizeDocks({dockProps}, {320}, Qt::Horizontal);

        auto *dock = new QDockWidget("控制台 (Console)", this);
        console = new QPlainTextEdit;
        console->setReadOnly(true);
        console->setMaximumBlockCount(400);
        console->setFont(QFont("Monospace", 9));
        console->setStyleSheet("QPlainTextEdit{background:#15181e; color:#e6ecf5; border:none;}");
        dock->setWidget(console);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        resizeDocks({dock}, {160}, Qt::Vertical);
        consoleDock = dock;
        gConsole = console;

        // 拖出上限：最多 3 个独立窗口，超出自动吸回主窗口（真实约束，不是摆设）
        for (QDockWidget *dw : {dockFile, dockProps, consoleDock}) {
            connect(dw, &QDockWidget::topLevelChanged, this, [this, dw](bool floating){
                int floatingCount = 0;
                for (QDockWidget *d : this->findChildren<QDockWidget*>())
                    if (d->isVisible() && d->isFloating()) ++floatingCount;
                if (floating && floatingCount > 3) {
                    dw->setFloating(false);
                    qInfo() << "[2D] 独立窗口已达上限（3 个）：属性面板自动吸回主窗口";
                } else if (floating) {
                    qInfo() << QString("[2D] 面板拖出为独立窗口（%1/3）：%2")
                        .arg(floatingCount).arg(dw->windowTitle().split(" (").first());
                }
            });
        }

        statusLabel = new QLabel("就绪");
        statusBar()->addWidget(statusLabel);

        // ---- 菜单 ----
        QMenu *mFile = menuBar()->addMenu("文件(&F)");
        mFile->addAction("新建 UI 画布", this, [this]{ newCanvas(); });
        mFile->addAction("新建 2D 场景", this, [this]{ newScene2D(); });
        mFile->addAction("打开…", this, &C2DWindow::openFile);
        mFile->addAction("保存 (Ctrl+S)", this, &C2DWindow::saveFile);
        mFile->addSeparator();
        mFile->addAction("退出", qApp, &QApplication::quit);

        QMenu *mFont = menuBar()->addMenu("字体管理");
        mFont->addAction("导入字体 (.ttf/.otf)…", this, &C2DWindow::importFont);

        QMenu *mAudio = menuBar()->addMenu("音频管理");
        mAudio->addAction("导入音频 (.mp3/.wav/.ogg)…", this, &C2DWindow::importAudio);
        mAudio->addAction("播放选中的音频资产", this, &C2DWindow::playSelectedAudio);

        QMenu *mTpl = menuBar()->addMenu("模板(&T)");
        mTpl->addAction("主菜单模板（开始 / 设置 / 退出）", this, &C2DWindow::applyMainMenuTemplate);
        mTpl->addAction("系统设置面板（滑动条 + 下拉框 + 开关）", this, &C2DWindow::applySettingsTemplate);

        // ---- 以下与 3DEditor 主菜单栏完全对齐（功能对齐死命令）----
        QMenu *mCreate = menuBar()->addMenu("创建(&C)");
        for (int i = 0; i < libList->count(); ++i) {
            QString nm = libList->item(i)->text();
            mCreate->addAction(nm, this, [this, i, nm]{
                // 与控件库双击同一条真实管线：向画布添加控件
                emit libList->itemDoubleClicked(libList->item(i));   // 走控件库已有信号
                qInfo() << QString("[2D] 菜单创建：%1").arg(nm);
            });
        }

        QMenu *mPlugin2 = menuBar()->addMenu("插件(&P)");
        mPlugin2->addAction("插件管理…", this, [this]{
            QMessageBox::information(this, "插件管理",
                "2D 编辑器插件（与 3D 编辑器同一注册表）：\n\n"
                "  · HD Graphics —— 2D 渲染管线已启用\n"
                "  · Dynamic Nature —— 2D 粒子风力已启用\n\n"
                "2D / 3D 编辑器共享同一插件注册表（plugins.json）。");
            qInfo() << "[2D] 插件管理：2D 编辑器共享插件注册表（HD Graphics / Dynamic Nature）";
        });

        QMenu *mPhys2 = menuBar()->addMenu("物理(&P)");
        mPhys2->addAction("物理测试：生成 50 个刚体方块", this, [this]{
            enterPhysMode();
            for (int i = 0; i < 50; ++i) {
                CanvasItem it;
                it.type = "box2d";
                it.name = QString("刚体_%1").arg(i, 2, 10, QChar('0'));
                it.text = QString("%1").arg(i);
                it.rect = QRectF(200 + (i % 10) * 90, 120 + (i / 10) * 70, 60, 60);
                it.color = QColor::fromHsv((i * 7) % 360, 200, 240);
                it.hasBody = true;
                canvas->items.push_back(it);
            }
            canvas->update();
            qInfo() << "[2D] 物理测试：50 个刚体方块已入画（RigidBody2D + 重力 980）";
            statusLabel->setText("物理测试：50 个刚体方块（点运行看真实掉落）");
        });

        QMenu *mWindow = menuBar()->addMenu("窗口(&W)");
        mWindow->addAction("设置…", this, [this]{
            QMessageBox::information(this, "2D 编辑器设置",
                "设计分辨率：1600 × 900（九锚点自适应）\n物理：RigidBody2D，重力 980 px/s²\n运行：Game 标签页真实运行");
            qInfo() << "[2D] 窗口 -> 设置";
        });
        mWindow->addAction("切换控制台 (Ctrl+F9)", this, [this]{
            if (!consoleDock) return;
            consoleDock->setVisible(!consoleDock->isVisible());
            qInfo() << QString("[2D] 控制台%1（支持折叠/缩放）").arg(consoleDock->isVisible() ? "展开" : "折叠");
        });
        mWindow->addAction("重置面板布局", this, [this]{
            for (QDockWidget *dw : findChildren<QDockWidget*>()) { dw->setFloating(false); dw->setVisible(true); }
            addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
            resizeDocks({consoleDock}, {160}, Qt::Vertical);
            qInfo() << "[2D] 面板布局已重置：文件管理器/属性/控制台全部吸回主窗口";
        });
        {
            auto *actSearch2 = new QAction("全局搜索 (Ctrl+P)", this);
            connect(actSearch2, &QAction::triggered, this, &C2DWindow::openQuickSearch);
            mWindow->addAction(actSearch2);
        }

        QMenu *mHelp2 = menuBar()->addMenu("帮助(&H)");
        mHelp2->addAction("作者主页", this, [this]{
            QMessageBox::information(this, "作者主页", "PhysicsEngine3D —— 表弟的 3D/2D 游戏引擎（2D 编辑器篇）");
        });
        mHelp2->addAction("贡献者名单", this, [this]{
            QMessageBox::information(this, "贡献者名单", "灵光（引擎与 2D 编辑器）× 表弟（首席体验官）");
        });

        QMenu *mAbout2 = menuBar()->addMenu("关于(&A)");
        mAbout2->addAction("关于 PhysicsEngine3D 2D 编辑器…", this, [this]{
            QMessageBox::information(this, "关于",
                "PhysicsEngine3D 2D 编辑器\n双标签页（场景编辑 / 游戏运行）\n三大面板可拖出独立窗口（上限 3 个）\n与 3D 编辑器功能对齐");
        });

        QMenu *mRun = menuBar()->addMenu("运行(&R)");
        actRun = mFile->actions().isEmpty() ? nullptr : mRun->addAction(style()->standardIcon(QStyle::SP_MediaPlay), "运行 (&Run)");
        if (actRun) connect(actRun, &QAction::triggered, this, &C2DWindow::toggleRun);

        // ---- 信号 ----
        connect(canvas, &CanvasWidget::selectionChanged, this, &C2DWindow::syncPanel);
        connect(canvas, &CanvasWidget::itemsChanged, this, &C2DWindow::syncPanel);
        connect(canvas, &CanvasWidget::buttonClicked, this, &C2DWindow::onButtonClicked);
        connect(canvas, &CanvasWidget::inputSubmitted, this, &C2DWindow::onInputSubmitted);
        connect(canvas, &CanvasWidget::saved, this, [this]{
            statusLabel->setText(QString("已保存：%1").arg(QFile(canvas->savePath).fileName()));
        });
        // UI 专属事件：滑动条值改变 / 下拉选择 -> 引擎真实生效
        connect(canvas, &CanvasWidget::valueChanged, this, [this](int, const QString &name, double v){
            if (name.contains("音量")) {
                canvas->gVolume = v;   // 引擎 BGM 音量真实生效（播放走 paplay --volume）
                qInfo() << QString("[Audio] 引擎 BGM 音量真实更新：%1%").arg(int(v));
            } else if (name.contains("全屏")) {
                if (v >= 0.5) { showFullScreen(); qInfo() << "[Audio] 引擎窗口真实切换：全屏 (showFullScreen)"; }
                else { showNormal(); qInfo() << "[Audio] 引擎窗口真实切换：窗口化 (showNormal)"; }
            } else if (name.contains("同步")) {
                qInfo() << QString("[UI] OnValueChanged: %1 -> %2").arg(name, v >= 0.5 ? "true" : "false");
            }
        });
        connect(canvas, &CanvasWidget::optionPicked, this, [this](int, const QString &name, const QString &opt){
            if (name.contains("分辨率")) {
                // 下拉选分辨率 -> 引擎窗口真实改变（画布锚点随 resizeEvent 自适应）
                if (opt == "1920x1080") resize(1600, 900);
                else if (opt == "1280x720") resize(1280, 720);
                else if (opt == "960x540") resize(960, 540);
                qInfo() << QString("[UI] 分辨率真实切换：%1（UI 按锚点自适应）").arg(opt);
            }
        });

        // ---- 属性面板回写：九宫格 / 布局 / Tween / 值类 ----
        connect(chkNineSlice, &QCheckBox::toggled, this, [this](bool v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].nineSlice = v; canvas->update();
        });
        connect(spinSliceMargin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].sliceMargin = v; canvas->update();
        });
        connect(comboLayout, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].layout = v;
            canvas->applyLayout();
            canvas->update();
        });
        connect(spinSpacing, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].spacing = v;
            canvas->applyLayout();
            canvas->update();
        });
        connect(spinValue, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].value = v; canvas->update();
        });
        connect(editOptions, &QLineEdit::textChanged, this, [this](const QString &t){
            if (panelGuard || canvas->selected < 0) return;
            QStringList opts;
            for (const QString &s : t.split(','))
                if (!s.trimmed().isEmpty()) opts << s.trimmed();
            if (!opts.isEmpty()) canvas->items[size_t(canvas->selected)].options = opts;
        });
        connect(comboTween, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            auto &it = canvas->items[size_t(canvas->selected)];
            it.tweenType = v; it.animT = 0; it.animDone = false; canvas->update();
        });
        connect(spinTweenDur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].tweenDur = v;
        });
        connect(comboEase, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].tweenEase = v;
        });

        // ---- Ctrl+P 全局搜索：资产文件 / 画布对象 / Lua 脚本内容 ----
        auto *scP = new QShortcut(QKeySequence("Ctrl+P"), this);
        connect(scP, &QShortcut::activated, this, &C2DWindow::openQuickSearch);

        connect(libList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it){
            CanvasItem item;
            QString t = it->text();
            // 控件命名：按钮自动编号（Button_001…），信号日志直呼其名
            int bn = 0;
            for (const auto &x : canvas->items) if (x.type == "button") ++bn;
            if (t.startsWith("按钮")) { item.type = "button"; item.text = "按钮"; item.name = QString("Button_%1").arg(bn + 1, 3, 10, QChar('0')); }
            else if (t.startsWith("文本")) { item.type = "text"; item.text = "中文文本"; item.name = "文本"; }
            else if (t.startsWith("图片")) { item.type = "image"; item.name = "图片"; }
            else if (t.startsWith("输入框")) { item.type = "input"; item.name = "输入框"; item.rect = QRectF(560, 180, 420, 60); }
            else if (t.startsWith("滑动条")) { item.type = "slider"; item.name = "滑动条"; item.text = "音量"; item.rect = QRectF(600, 300, 340, 54); }
            else if (t.startsWith("复选框")) { item.type = "checkbox"; item.name = "复选框"; item.text = "全屏显示"; item.rect = QRectF(600, 380, 280, 44); }
            else if (t.startsWith("下拉框")) { item.type = "dropdown"; item.name = "下拉框"; item.text = "1080P"; item.rect = QRectF(600, 440, 240, 52); }
            else if (t.startsWith("开关")) { item.type = "toggle"; item.name = "开关"; item.text = "垂直同步"; item.rect = QRectF(600, 500, 260, 44); }
            else if (t.startsWith("面板")) { item.type = "panel"; item.name = "面板"; item.color = QColor("#2b3542"); item.rect = QRectF(500, 180, 460, 380); }
            else if (t.startsWith("滚动视图")) { item.type = "scrollview"; item.name = "滚动视图"; item.rect = QRectF(560, 180, 400, 280); }
            else if (t.startsWith("网格布局")) { item.type = "grid"; item.name = "网格布局"; item.rect = QRectF(580, 200, 320, 320); }
            else if (t.startsWith("进度条")) { item.type = "progress"; item.name = "进度条"; item.text = "HP"; item.color = QColor("#e05a6e"); item.rect = QRectF(600, 300, 320, 44); }
            else if (t.startsWith("遮罩")) { item.type = "mask"; item.name = "遮罩"; item.rect = QRectF(640, 240, 150, 150); }
            else if (t.startsWith("多行文本")) { item.type = "textarea"; item.name = "多行文本"; item.text = "第一行\n第二行\n第三行"; item.rect = QRectF(580, 220, 340, 140); }
            else if (t.startsWith("对话框面板")) { item.type = "dialoguebox"; item.name = "对话框面板";
                   item.text = "角色名"; item.color = QColor(16, 22, 34, 225);
                   item.rect = QRectF(320, 640, 960, 190); }          // 九宫格拉伸参数由渲染层使用
            else if (t.startsWith("角色立绘")) { item.type = "portrait"; item.name = "角色立绘";
                   item.text = "left"; item.color = QColor("#5d4a72"); item.rect = QRectF(200, 220, 260, 520); }
            else if (t.startsWith("选项分支按钮")) { item.type = "choicebutton"; item.name = "选项分支按钮";
                   item.text = "选项 A"; item.color = QColor("#2f6f4f"); item.rect = QRectF(640, 380, 320, 56); }
            else { item.type = "box2d"; item.hasBody = true; item.color = QColor("#4aa3e0");
                   item.name = "Box2D"; item.rect = QRectF(700, 200, 90, 90); }
            item.rect = item.rect.translated(std::rand() % 200 - 100, std::rand() % 120 - 60);
            // 选中容器时添加控件 -> 自动成为容器子项，交给布局引擎排版
            if (canvas->selected >= 0 && canvas->selected < int(canvas->items.size())) {
                const CanvasItem &sel = canvas->items[size_t(canvas->selected)];
                if (sel.type == "panel" || sel.type == "scrollview" || sel.type == "grid")
                    if (item.type != "panel" && item.type != "scrollview" && item.type != "grid")
                        item.parent = canvas->selected;
            }
            canvas->addItem(item);
        });

        // 属性面板回写
        connect(editText, &QLineEdit::textChanged, this, [this](const QString &t){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].text = t; canvas->update();
        });
        connect(comboFont, &QComboBox::currentTextChanged, this, [this](const QString &fam){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].font = fam.startsWith("默认") ? QString() : fam;
            canvas->update();
        });
        connect(spinFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].fontSize = v; canvas->update();
        });
        connect(btnColor, &QPushButton::clicked, this, [this]{
            if (canvas->selected < 0) return;
            QColor c = QColorDialog::getColor(canvas->items[size_t(canvas->selected)].color, this, "选择颜色");
            if (!c.isValid()) return;
            canvas->items[size_t(canvas->selected)].color = c;
            syncPanel();
            canvas->update();
        });
        connect(comboAnchor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int a){
            if (panelGuard || canvas->selected < 0) return;
            canvas->items[size_t(canvas->selected)].anchor = a; canvas->update();
        });
        auto bindSpin = [this](QDoubleSpinBox *sp, int field) {
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, sp, field](double v){
                if (panelGuard || canvas->selected < 0) return;
                QRectF r = canvas->items[size_t(canvas->selected)].rect;
                if (field == 0) r.moveLeft(v);
                else if (field == 1) r.moveTop(v);
                else if (field == 2) r.setWidth(v);
                else r.setHeight(v);
                canvas->items[size_t(canvas->selected)].rect = r;
                canvas->update();
            });
        };
        bindSpin(spX, 0); bindSpin(spY, 1); bindSpin(spW, 2); bindSpin(spH, 3);
        connect(scriptSlot, &QLineEdit::selectionChanged, this, [this]{
            QString p = QFileDialog::getOpenFileName(this, "选择挂载脚本", QDir::home().filePath("Physics3DProjects"),
                                                     "脚本 (*.lua *.socket)");
            if (p.isEmpty() || canvas->selected < 0) return;
            scriptSlot->setText(p);
            canvas->items[size_t(canvas->selected)].script = p;
            qInfo() << QString("[2D] %1 已挂载脚本：%2").arg(canvas->items[size_t(canvas->selected)].name, p);
        });
        connect(btnImport, &QPushButton::clicked, this, &C2DWindow::importFont);

        refreshFontCombo();
        refreshImages(QDir::home().filePath("Physics3DProjects"));

        // 物理帧循环（60fps）+ Tween 缓动推进（编辑画布与 Game 运行画布同频推进）
        auto *ptimer = new QTimer(this);
        connect(ptimer, &QTimer::timeout, this, [this]{
            canvas->stepPhysics(1.0f / 60.0f);
            canvas->stepAnim(1.0f / 60.0f);
            if (gameCanvas && gameCanvas->running) {
                gameCanvas->stepPhysics(1.0f / 60.0f);
                gameCanvas->stepAnim(1.0f / 60.0f);
            }
        });
        ptimer->start(16);
    }

    void enterUiMode() {
        canvas->mode = CanvasWidget::UiMode;
        setWindowTitle(QString("VNUIEditor — UI 画布编辑器（Canvas 画布 + 锚点对齐）"));
        statusLabel->setText("UI 模式：左侧双击添加控件，画布拖拽/四角拉伸，Ctrl+S 保存 .uicanvas");
    }
    void enterPhysMode() {
        canvas->mode = CanvasWidget::PhysMode;
        setWindowTitle("VN2DEditor — 纯 2D 游戏编辑器（RigidBody2D + BoxCollider2D + 重力）");
        statusLabel->setText("纯 2D 模式：双击左侧「刚体方块」添加 RigidBody2D，点运行看物理模拟");
    }

    // 模式选择对话框（新建项目时强制选择）
    void showModeDialog(bool demoShot) {
        auto *dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("新建：选择 2D 项目模式");
        dlg->resize(520, 260);
        dlg->move(400, 220);
        auto *lay = new QVBoxLayout(dlg);
        auto *tip = new QLabel("请选择 2D 项目类型：");
        tip->setStyleSheet("font-weight:bold; font-size:14px;");
        lay->addWidget(tip);
        auto *bUi = new QPushButton("用户界面 (UI)\nCanvas 画布 + 锚点对齐（左上 / 居中 / 右下），适合做 HUD / 菜单");
        bUi->setStyleSheet("QPushButton{padding:14px; text-align:left;}");
        auto *bPhys = new QPushButton("纯 2D 游戏\nRigidBody2D + BoxCollider2D + 重力 -9.8，物理模拟");
        bPhys->setStyleSheet("QPushButton{padding:14px; text-align:left;}");
        lay->addWidget(bUi);
        lay->addWidget(bPhys);
        connect(bUi, &QPushButton::clicked, this, [this, dlg]{
            enterUiMode(); dlg->accept();
        });
        connect(bPhys, &QPushButton::clicked, this, [this, dlg]{
            enterPhysMode(); dlg->accept();
        });
        dlg->show();
        if (!demoShot) {
            connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
        }
    }

    void newCanvas() {
        enterUiMode();
        canvas->items.clear();
        canvas->selected = -1;
        canvas->savePath.clear();
        canvas->update();
        qInfo() << "[2D] 新建空白 UI 画布";
    }
    void newScene2D() {
        enterPhysMode();
        canvas->items.clear();
        canvas->selected = -1;
        canvas->savePath.clear();
        canvas->update();
        qInfo() << "[2D] 新建空白 2D 物理场景";
    }

    void openFile() {
        QString p = QFileDialog::getOpenFileName(this, "打开 2D 资产", QDir::home().filePath("Physics3DProjects"),
                                                 "2D 资产 (*.uicanvas *.scene2d)");
        if (p.isEmpty()) return;
        if (!canvas->loadFile(p)) { QMessageBox::warning(this, "错误", "读取失败"); return; }
        if (canvas->mode == CanvasWidget::UiMode) enterUiMode(); else enterPhysMode();
    }

    void saveFile() {
        QString p = canvas->savePath;
        if (p.isEmpty()) {
            p = QFileDialog::getSaveFileName(this, "保存 2D 资产", QDir::home().filePath("Physics3DProjects/画布.uicanvas"),
                                             canvas->mode == CanvasWidget::UiMode ? "UI 画布 (*.uicanvas)" : "2D 场景 (*.scene2d)");
            if (p.isEmpty()) return;
        }
        canvas->saveToFile(p);
    }

    // 字体管理：导入 .ttf/.otf，QFontDatabase 注册后刷新下拉（防中文豆腐块）
    void importFont() {
        QString p = QFileDialog::getOpenFileName(this, "导入字体文件", "/usr/share/fonts",
                                                 "字体 (*.ttf *.otf)");
        if (p.isEmpty()) return;
        int id = QFontDatabase::addApplicationFont(p);
        if (id < 0) {
            qInfo() << QString("[字体] 导入失败：%1").arg(QFile(p).fileName());
            return;
        }
        QStringList fams = QFontDatabase::applicationFontFamilies(id);
        refreshFontCombo();
        qInfo() << QString("[字体] 导入成功：%1 -> 字体族 [%2]（下拉框已刷新）")
            .arg(QFile(p).fileName(), fams.join(", "));
        statusLabel->setText(QString("字体已导入：%1").arg(QFile(p).fileName()));
        if (!fams.isEmpty() && canvas->selected >= 0) {
            // 选中新导入字体应用到当前选中控件
            int fi = comboFont->findText(fams.first());
            if (fi >= 0) comboFont->setCurrentIndex(fi);
        }
    }

    void refreshFontCombo() {
        panelGuard = true;
        comboFont->clear();
        comboFont->addItem("默认 (Noto Sans CJK SC)");
        for (const QString &fam : QFontDatabase::families())
            comboFont->addItem(fam);
        panelGuard = false;
    }

    void refreshImages(const QString &dirPath) {
        imgList->clear();
        QDir dir(dirPath + "/assets");
        if (!dir.exists()) return;
        for (const QFileInfo &fi : dir.entryInfoList({"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp"}, QDir::Files)) {
            imgList->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon), fi.fileName()));
        }
    }

    // 演示钩子：把控制台 dock 拉高，让信号日志入画
    void raiseConsole() {
        if (!consoleDock) return;
        consoleDock->show();
        resizeDocks({consoleDock}, {420}, Qt::Vertical);
        if (console) {
            console->verticalScrollBar()->setValue(console->verticalScrollBar()->maximum());
            console->repaint();
        }
        repaint();
    }

    void toggleRun() {
        canvas->running = !canvas->running;
        if (actRun) actRun->setText(canvas->running ? "停止 (&Stop)" : "运行 (&Run)");
        if (canvas->running) {
            canvas->resetRunState();
            // Game 标签页：深拷贝当前场景 -> 真实运行（物理/按钮/输入全部走同一套画布管线）
            gameCanvas->items = canvas->items;
            gameCanvas->mode = canvas->mode;
            gameCanvas->gravity = canvas->gravity;
            gameCanvas->selected = -1;
            gameCanvas->savePath = canvas->savePath;
            gameCanvas->running = true;
            gameCanvas->resetRunState();
            canvas->running = false;               // 编辑画布保持编辑态
            tabs->setCurrentWidget(gameTab);
            qInfo() << QString("[2D] 运行 -> 切换到【游戏运行 (Game)】标签页，真实运行 2D 游戏（%1 个控件，物理模拟进行中）")
                .arg(gameCanvas->items.size());
            statusLabel->setText("游戏运行中 —— Game 标签页真实运行，点「停止」返回编辑器");
        } else {
            gameCanvas->running = false;
            tabs->setCurrentWidget(editorTab);
            qInfo() << "[2D] 已停止 -> 返回【场景编辑 (Editor)】标签页";
            canvas->update();
        }
    }

    // 画布按钮点击：QProcess 跑真实 Lua，输出进控制台
    void onButtonClicked(int idx, const QString &name, const QString &script) {
        qInfo() << QString("[UI] 按钮被点击：%1（%2 号）").arg(name).arg(idx);
        statusLabel->setText(QString("按钮被点击：%1").arg(name));
        if (!script.isEmpty() && script.endsWith(".lua") && QFile(script).exists()) {
            auto *proc = new QProcess(this);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this, proc](int, QProcess::ExitStatus){
                QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                if (!out.isEmpty()) qInfo() << "[Lua] 按钮脚本输出：" << out;
                if (!err.isEmpty()) qWarning().noquote() << "[Lua] 脚本错误：" << err;
                proc->deleteLater();
            });
            proc->start("/usr/bin/lua5.4", {script});
            qInfo() << QString("[Script] 按钮触发脚本：%1").arg(QFile(script).fileName());
        }
    }

    // 输入框提交：QProcess 跑真实 Lua，输入内容经 INPUT_TEXT 环境变量传递
    void onInputSubmitted(int idx, const QString &text, const QString &script) {
        statusLabel->setText(QString("输入已提交（%1 号输入框）：%2").arg(idx).arg(text));
        if (!script.isEmpty() && script.endsWith(".lua") && QFile(script).exists()) {
            auto *proc = new QProcess(this);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("INPUT_TEXT", text);
            proc->setProcessEnvironment(env);
            connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this, proc](int, QProcess::ExitStatus){
                QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                if (!out.isEmpty()) qInfo() << "[Lua] 输入脚本输出：" << out;
                if (!err.isEmpty()) qWarning().noquote() << "[Lua] 脚本错误：" << err;
                proc->deleteLater();
            });
            proc->start("/usr/bin/lua5.4", {script});
            qInfo() << QString("[Script] 输入触发脚本：%1（INPUT_TEXT=%2）").arg(QFile(script).fileName(), text);
        }
    }

    // 音频导入：复制到项目 assets/（.mp3/.wav/.ogg）
    void importAudio() {
        QString p = QFileDialog::getOpenFileName(this, "导入音频", QDir::homePath(),
                                                 "音频 (*.mp3 *.wav *.ogg)");
        if (p.isEmpty()) return;
        QString dest = QDir::home().filePath("Physics3DProjects/assets/" + QFileInfo(p).fileName());
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (QFile::exists(dest) && dest != p) QFile::remove(dest);
        if (!QFile::copy(p, dest)) {
            qInfo() << QString("[音频] 导入失败：%1").arg(QFile(p).fileName());
            return;
        }
        lastAudio = dest;
        qInfo() << QString("[音频] 导入成功：%1 -> assets/（播放走 paplay）").arg(QFileInfo(p).fileName());
        statusLabel->setText(QString("音频已导入：%1").arg(QFileInfo(p).fileName()));
    }

    // 播放音频：QProcess paplay（沙箱无声卡时日志说明）
    void playSelectedAudio() {
        if (lastAudio.isEmpty()) {
            qInfo() << "[音频] 提示：先用「音频管理 → 导入音频」选择一个音频文件";
            return;
        }
        auto *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, proc](int code, QProcess::ExitStatus){
            QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (code != 0 || !err.isEmpty())
                qWarning().noquote() << QString("[音频] 播放结束（可能无声音设备）：%1").arg(err.left(80));
            else
                qInfo() << QString("[音频] 播放完成：%1").arg(QFile(lastAudio).fileName());
            proc->deleteLater();
        });
        proc->start("paplay", {lastAudio, QString("--volume=%1").arg(int(canvas->gVolume))});
        qInfo() << QString("[音频] 播放中：%1（音量 %2%，来自设置面板滑动条）")
                    .arg(QFile(lastAudio).fileName()).arg(int(canvas->gVolume));
    }

    // ---- Ctrl+P 全局搜索框 ----
    void openQuickSearch() {
        std::vector<QuickSearchDialog::Entry> es;
        QString asPath = QDir::home().filePath("Physics3DProjects");
        // 资产文件
        QDirIterator it(asPath, {"*.lua", "*.png", "*.jpg", "*.jpeg", "*.uicanvas", "*.scene2d",
                                 "*.socket", "*.mat", "*.wav", "*.mp3", "*.ogg", "*.ttf"},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            es.push_back({QFileInfo(it.filePath()).fileName(), "资产 · " + QDir(asPath).relativeFilePath(it.filePath())});
        }
        // 画布对象
        for (const auto &x : canvas->items)
            es.push_back({x.name, QString("画布对象 · %1 (%2,%3,%4,%5)").arg(x.type).arg(int(x.rect.x())).arg(int(x.rect.y())).arg(int(x.rect.width())).arg(int(x.rect.height()))});
        // Lua 脚本内容（逐行索引，最多 300 行）
        QDirIterator it2(asPath, {"*.lua"}, QDir::Files, QDirIterator::Subdirectories);
        while (it2.hasNext()) {
            QFile f(it2.next());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            int ln = 0;
            while (!f.atEnd() && ln < 100) {
                QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (!line.isEmpty()) es.push_back({line.left(60), QString("%1:%2").arg(QFileInfo(it2.filePath()).fileName()).arg(++ln)});
            }
        }
        auto *dlg = new QuickSearchDialog(this, std::move(es));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        dlg->raise();
        qInfo() << "[Search] 全局搜索已呼出（Ctrl+P）";
    }

    // ---- 主菜单模板：一键生成 开始/设置/退出（VBox 自动排版）----
    void applyMainMenuTemplate() {
        enterUiMode();
        canvas->items.clear();
        canvas->selected = -1;
        CanvasItem bg;
        bg.type = "panel"; bg.name = "主菜单面板"; bg.color = QColor("#1b2635");
        bg.rect = QRectF(560, 120, 480, 560); bg.layout = 1; bg.spacing = 28;
        canvas->items.push_back(bg);
        auto mkBtn = [&](const QString &txt, int n) {
            CanvasItem b;
            b.type = "button"; b.text = txt;
            b.name = QString("Button_%1").arg(n, 3, 10, QChar('0'));
            b.font = "Noto Sans CJK SC"; b.fontSize = 30; b.color = QColor("#ffffff");
            b.rect = QRectF(660, 260 + (n - 1) * 100, 280, 66);
            b.parent = 0;
            canvas->items.push_back(b);
        };
        CanvasItem t;
        t.type = "text"; t.text = "主菜单"; t.fontSize = 44; t.color = QColor("#ffffff");
        t.rect = QRectF(700, 160, 200, 64); t.parent = 0;
        canvas->items.push_back(t);
        mkBtn("开始游戏", 1);
        mkBtn("设置", 2);
        mkBtn("退出", 3);
        canvas->selected = 0;
        canvas->applyLayout();
        emit canvas->selectionChanged();
        emit canvas->itemsChanged();
        canvas->update();
        qInfo() << "[模板] 主菜单模板已生成：开始游戏 / 设置 / 退出（Button_001~003，VBox 自动排版）";
    }

    // ---- 系统设置面板模板：滑动条(BGM音量) + 下拉框(分辨率) + 开关(全屏) ----
    void applySettingsTemplate() {
        enterUiMode();
        canvas->items.clear();
        canvas->selected = -1;
        CanvasItem bg;
        bg.type = "panel"; bg.name = "系统设置面板"; bg.color = QColor("#223040");
        bg.rect = QRectF(520, 150, 560, 500); bg.layout = 1; bg.spacing = 36;
        canvas->items.push_back(bg);
        CanvasItem t;
        t.type = "text"; t.text = "系统设置"; t.fontSize = 40; t.color = QColor("#ffffff");
        t.rect = QRectF(660, 190, 260, 60); t.parent = 0;
        canvas->items.push_back(t);
        CanvasItem sl;
        sl.type = "slider"; sl.name = "BGM 音量"; sl.text = "BGM 音量"; sl.value = 60;
        sl.rect = QRectF(600, 300, 400, 56); sl.parent = 0;
        canvas->items.push_back(sl);
        CanvasItem dd;
        dd.type = "dropdown"; dd.name = "分辨率下拉框"; dd.text = "1920x1080";
        dd.options = {"1920x1080", "1280x720", "960x540"};
        dd.rect = QRectF(600, 400, 300, 54); dd.parent = 0;
        canvas->items.push_back(dd);
        CanvasItem tg;
        tg.type = "toggle"; tg.name = "全屏开关"; tg.text = "全屏"; tg.checked = false;
        tg.rect = QRectF(600, 500, 280, 48); tg.parent = 0;
        canvas->items.push_back(tg);
        canvas->selected = 0;
        canvas->applyLayout();
        emit canvas->selectionChanged();
        emit canvas->itemsChanged();
        canvas->update();
        qInfo() << "[模板] 系统设置面板已生成：BGM 音量滑动条 / 分辨率下拉框 / 全屏开关（底层逻辑已挂好）";
    }

    void syncPanel() {
        panelGuard = true;
        if (canvas->selected < 0 || canvas->selected >= int(canvas->items.size())) {
            statusLabel->setText(statusLabel->text());
            panelGuard = false;
            return;
        }
        const CanvasItem &it = canvas->items[size_t(canvas->selected)];
        editText->setText(it.text);
        int fi = it.font.isEmpty() ? 0 : comboFont->findText(it.font);
        comboFont->setCurrentIndex(fi < 0 ? 0 : fi);
        spinFontSize->setValue(it.fontSize);
        btnColor->setText(QString("颜色：%1").arg(it.color.name()));
        btnColor->setStyleSheet(QString("background:%1; color:%2; border:none; border-radius:4px;")
                                .arg(it.color.name(), it.color.lightness() > 140 ? "#20242a" : "#ffffff"));
        comboAnchor->setCurrentIndex(it.anchor);
        spX->setValue(it.rect.x()); spY->setValue(it.rect.y());
        spW->setValue(it.rect.width()); spH->setValue(it.rect.height());
        scriptSlot->setText(it.script);
        slotNormal->setText(it.imgNormal.isEmpty() ? QString() : QFileInfo(it.imgNormal).fileName());
        slotHover->setText(it.imgHover.isEmpty() ? QString() : QFileInfo(it.imgHover).fileName());
        slotPressed->setText(it.imgPressed.isEmpty() ? QString() : QFileInfo(it.imgPressed).fileName());
        slotBind->setText(it.bindScript.isEmpty() ? QString() : QFileInfo(it.bindScript).fileName());
        chkNineSlice->setChecked(it.nineSlice);
        spinSliceMargin->setValue(it.sliceMargin);
        comboLayout->setCurrentIndex(it.layout);
        spinSpacing->setValue(it.spacing);
        spinValue->setValue(int(it.value));
        editOptions->setText(it.options.join(","));
        comboTween->setCurrentIndex(it.tweenType);
        spinTweenDur->setValue(it.tweenDur);
        comboEase->setCurrentIndex(it.tweenEase);
        panelGuard = false;
        canvas->update();
    }

    QLabel *statusLabel = nullptr;
    bool panelGuard = false;
    QString lastAudio;   // 最近导入的音频路径
};

static int runApp(QApplication &app, int argc, char **argv) {
    app.setApplicationName("VN2DEditor");

    QString arg1 = (argc > 1) ? argv[1] : "";
    QString autoMode = qEnvironmentVariable("C2D_AUTO_MODE");
    C2DWindow win;
    win.show();

    if (autoMode == "ui") {
        win.enterUiMode();
    } else if (autoMode == "phys") {
        win.enterPhysMode();
    } else if (autoMode == "plat") {
        win.enterPhysMode();
        QString scene = qEnvironmentVariable("C2D_SCENE");
        if (scene.isEmpty())
            scene = QDir::home().filePath("Physics3DProjects/2D跳跃演示/assets/level2d.scene2d");
        if (!win.canvas->loadFile(scene))
            qInfo() << QString("[2D] 错误：无法读取 %1").arg(scene);
    } else if (autoMode == "tree") {
        win.enterUiMode();   // 文件树演示：UI 模式 + 左栏资产树
    } else if (autoMode == "modedlg") {
        win.showModeDialog(true);
    } else if (!arg1.isEmpty() && arg1.endsWith(".uicanvas")) {
        win.enterUiMode();
        if (!win.canvas->loadFile(arg1)) qInfo() << QString("[2D] 错误：无法读取 %1").arg(arg1);
    } else if (!arg1.isEmpty() && arg1.endsWith(".scene2d")) {
        win.enterPhysMode();
        if (!win.canvas->loadFile(arg1)) qInfo() << QString("[2D] 错误：无法读取 %1").arg(arg1);
    } else if (autoMode == "font" || autoMode == "click") {
        win.enterUiMode();
    } else if (autoMode == "input") {
        win.enterUiMode();
    } else if (autoMode == "signal" || autoMode == "lib" || autoMode == "layout"
               || autoMode == "hover" || autoMode == "settings" || autoMode == "anchor9"
               || autoMode == "search" || autoMode == "tabs") {
        win.enterUiMode();
    } else {
        // 项目路径或无参数：强制弹模式选择
        win.showModeDialog(false);
    }

    QString shot = qEnvironmentVariable("C2D_SHOT");
    if (!shot.isEmpty()) {
        QString mode = autoMode.isEmpty() ? QString() : autoMode;
        if (mode == "ui") {
            QTimer::singleShot(500, &win, [&win]{
                CanvasItem t;
                t.type = "text"; t.text = "血量: 100"; t.font = "Noto Sans CJK SC";
                t.fontSize = 34; t.color = QColor("#7fdcff");
                t.rect = QRectF(60, 50, 320, 56); t.anchor = 0; t.name = "血量文本";
                win.canvas->addItem(t);
                CanvasItem b;
                b.type = "button"; b.text = "开始游戏"; b.name = "开始游戏";
                b.font = "Noto Sans CJK SC"; b.fontSize = 30; b.color = QColor("#ffffff");
                b.rect = QRectF(1280, 70, 240, 62); b.anchor = 2;
                win.canvas->addItem(b);
                // 演示：属性面板完整显示选中按钮
            });
            QTimer::singleShot(1800, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2800, &app, []{ QApplication::quit(); });
        } else if (mode == "font") {
            QTimer::singleShot(500, &win, [&win, shot]{
                win.enterUiMode();
                // 真实导入系统字体文件 -> QFontDatabase 注册
                QString fp = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
                int id = QFontDatabase::addApplicationFont(fp);
                win.refreshFontCombo();
                QStringList fams = QFontDatabase::applicationFontFamilies(id);
                qInfo() << QString("[字体] 导入成功：%1 -> 字体族 [%2]")
                    .arg(QFile(fp).fileName(), fams.join(", "));
                CanvasItem t;
                t.type = "text";
                t.text = "中文渲染测试：你好，灵光！ ABC 123";
                t.font = fams.isEmpty() ? "Noto Sans CJK SC" : fams.first();
                t.fontSize = 36; t.color = QColor("#9fe29f");
                t.rect = QRectF(80, 300, 1300, 80); t.anchor = 0; t.name = "中文字体测试";
                win.canvas->addItem(t);
                win.canvas->selected = 0;
                emit win.canvas->selectionChanged();
            });
            QTimer::singleShot(1800, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2800, &app, []{ QApplication::quit(); });
        } else if (mode == "click") {
            QTimer::singleShot(500, &win, [&win, shot]{
                win.enterUiMode();
                // 建脚本
                QString luaPath = QDir::home().filePath("Physics3DProjects/assets/button_click.lua");
                QDir().mkpath(QFileInfo(luaPath).absolutePath());
                if (!QFile(luaPath).exists()) {
                    QFile lf(luaPath);
                    if (lf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        lf.write("print(\"按钮脚本运行：跨维度 UI 已生效！\")\n");
                        lf.close();
                    }
                }
                CanvasItem b;
                b.type = "button"; b.text = "开始游戏"; b.name = "开始游戏";
                b.font = "Noto Sans CJK SC"; b.fontSize = 30; b.color = QColor("#ffffff");
                b.rect = QRectF(620, 380, 280, 70);
                b.script = luaPath;
                win.canvas->addItem(b);
                win.canvas->running = true;
            });
            // 真实鼠标点击画布按钮中心（走 QWidget 事件管线）
            QTimer::singleShot(1400, &win, [&win]{
                if (win.canvas->items.empty()) return;
                QRectF r = win.canvas->items[0].rect;
                QPointF w = win.canvas->toWidget(r.center());
                win.canvas->demoClickWidget(w);
            });
            QTimer::singleShot(3000, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4000, &app, []{ QApplication::quit(); });
        } else if (mode == "input") {
            QTimer::singleShot(500, &win, [&win, shot]{
                win.enterUiMode();
                // 演示脚本：读取 INPUT_TEXT 并回显
                QString luaPath = QDir::home().filePath("Physics3DProjects/assets/input_echo.lua");
                QDir().mkpath(QFileInfo(luaPath).absolutePath());
                {
                    QFile lf(luaPath);
                    if (lf.open(QIODevice::WriteOnly | QIODevice::Text))
                        lf.write("print(\"输入脚本运行：你输入的是 -> \" .. os.getenv(\"INPUT_TEXT\"))\n");
                }
                CanvasItem inp;
                inp.type = "input"; inp.name = "玩家昵称输入框";
                inp.font = "Noto Sans CJK SC"; inp.fontSize = 28;
                inp.rect = QRectF(560, 340, 440, 64);
                inp.script = luaPath;
                win.canvas->addItem(inp);
                win.canvas->running = true;
                qInfo() << "[UI] 输入框演示：输入框已创建并挂 input_echo.lua";
            });
            // 点击输入框（真实事件管线聚焦）
            QTimer::singleShot(1300, &win, [&win]{
                if (win.canvas->items.empty()) return;
                QRectF r = win.canvas->items[0].rect;
                win.canvas->demoClickWidget(win.canvas->toWidget(r.center()));
            });
            // 发送真实键盘输入：逐字符 QKeyEvent，最后回车提交
            QTimer::singleShot(1800, &win, [&win]{
                static const char *keys[] = {"h", "i", ",", " ", "灵", "光"};
                for (const char *k : keys) {
                    QKeyEvent *ke = new QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QString(k));
                    QCoreApplication::postEvent(win.canvas, ke);
                }
            });
            QTimer::singleShot(2100, &win, [&win]{
                QCoreApplication::postEvent(win.canvas, new QKeyEvent(
                    QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString("\r")));
            });
            QTimer::singleShot(3400, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4400, &app, []{ QApplication::quit(); });
        } else if (mode == "phys") {
            QTimer::singleShot(400, &win, [&win]{
                win.enterPhysMode();
                std::srand(uint(std::chrono::steady_clock::now().time_since_epoch().count()) & 0x7fffffff);
                const QColor cols[3] = {QColor("#4aa3e0"), QColor("#e0a34a"), QColor("#7fd68a")};
                for (int i = 0; i < 3; ++i) {
                    CanvasItem b;
                    b.type = "box2d"; b.hasBody = true; b.name = QString("Box2D_%1").arg(i + 1);
                    b.color = cols[i];
                    b.rect = QRectF(560 + i * 160, 140 + i * 90, 90, 90);
                    b.vel = QPointF((std::rand() % 120 - 60) / 100.0f, 0);
                    win.canvas->items.push_back(b);
                }
                win.canvas->running = true;
                qInfo() << "[2D] 纯 2D 物理运行：3 个 RigidBody2D（重力 980 px/s²，60fps 欧拉积分）";
            });
            QTimer::singleShot(1500, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2500, &app, []{ QApplication::quit(); });
        } else if (mode == "plat") {
            // 2D 平台跳跃演示：模板场景真实运行（A/D 移动 + 空格跳跃 + 金币计分）
            QTimer::singleShot(500, &win, [&win]{
                win.canvas->running = true;
                qInfo() << "[2D] 平台跳跃运行：RigidBody2D 玩家 + CharacterController2D 输入";
            });
            // 按住 D 向右跑 1.2 秒（真实 keyDown 状态管线）
            QTimer::singleShot(1100, &win, [&win]{ win.canvas->demoKey(Qt::Key_D, true); });
            QTimer::singleShot(2300, &win, [&win]{ win.canvas->demoKey(Qt::Key_D, false); });
            // 空格跳跃（落到平台上）
            QTimer::singleShot(2600, &win, [&win]{ win.canvas->demoKey(Qt::Key_Space, true); });
            QTimer::singleShot(2800, &win, [&win]{ win.canvas->demoKey(Qt::Key_Space, false); });
            QTimer::singleShot(3700, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4700, &app, []{ QApplication::quit(); });
        } else if (mode == "tree") {
            QTimer::singleShot(2000, &win, [&win, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(3000, &app, []{ QApplication::quit(); });
        } else if (autoMode == "modedlg") {
            QTimer::singleShot(1600, [&shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2600, &app, []{ QApplication::quit(); });
        }
        // ================= 商业级 UI 升级演示钩子 =================
        else if (mode == "signal") {
            // 信号与槽全链路：三态图按钮 -> 绑定信号槽拖入脚本 -> 点击发射 -> Lua 接收
            QTimer::singleShot(500, &win, [&win, shot]{
                QString asDir = QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets");
                QDir().mkpath(asDir);
                QString luaPath = QDir::home().filePath("Physics3DProjects/assets/umbrella.lua");
                QDir().mkpath(QFileInfo(luaPath).absolutePath());
                QFile lf(luaPath);
                if (lf.open(QIODevice::WriteOnly | QIODevice::Text))
                    lf.write("print(\"安布雷拉公司已收到信号，开始运输T00暴君！\")\n");
                // Button_001：三态图齐全（纯图片渲染无边框）+ 绑定信号脚本
                CanvasItem b;
                b.type = "button"; b.text = "开始运输"; b.name = "Button_001";
                b.font = "Noto Sans CJK SC"; b.fontSize = 30; b.color = QColor("#ffffff");
                b.rect = QRectF(620, 380, 320, 84);
                b.imgNormal = asDir + "/btn_normal.png";
                b.imgHover = asDir + "/btn_hover.png";
                b.imgPressed = asDir + "/btn_pressed.png";
                b.bindScript = luaPath;
                win.canvas->addItem(b);
                win.canvas->selected = 0;
                emit win.canvas->selectionChanged();
                qInfo() << "[演示] Button Component：三态图 + 绑定信号槽已就绪";
            });
            // 截图1：属性面板三态图槽位
            QTimer::singleShot(1400, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            // 真实拖拽管线：把 umbrella.lua 拖进【绑定信号】槽位
            QTimer::singleShot(2200, &win, [&win]{
                if (win.canvas->items.empty() || !win.slotBind) return;
                QString luaPath = QDir::home().filePath("Physics3DProjects/assets/umbrella.lua");
                auto *mime = new QMimeData;
                mime->setUrls({QUrl::fromLocalFile(luaPath)});
                QDropEvent de(QPointF(10, 10), Qt::CopyAction, mime,
                              Qt::LeftButton, Qt::NoModifier);
                win.slotBind->demoDropEvent(&de);
            });
            // 截图2：绑定信号槽位已插入脚本（安布雷拉总机接线）——先滚到槽位区
            QTimer::singleShot(3100, &win, [&win]{
                if (auto *sa = win.findChild<QScrollArea*>())
                    sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
                QTimer::singleShot(300, &win, []{
                    if (auto *scr = QGuiApplication::primaryScreen())
                        if (!qEnvironmentVariable("C2D_SHOT2").isEmpty())
                            scr->grabWindow(0).save(qEnvironmentVariable("C2D_SHOT2"));
                });
            });
            // 运行 + 真实点击：按下 -> 松开发射 Qt 信号 -> Lua 接收打印
            QTimer::singleShot(3800, &win, [&win]{
                win.toggleRun();
            });
            QTimer::singleShot(4400, &win, [&win]{
                if (win.canvas->items.empty()) return;
                win.canvas->demoClickWidget(win.canvas->toWidget(win.canvas->items[0].rect.center()));
            });
            // 截图3：控制台 [Signal Emitted] + 安布雷拉公司已收到信号（先把控制台 dock 拉高）
            QTimer::singleShot(5300, &win, [&win]{ win.raiseConsole(); });
            QTimer::singleShot(5800, &win, []{
                if (auto *scr = QGuiApplication::primaryScreen())
                    if (!qEnvironmentVariable("C2D_SHOT3").isEmpty())
                        scr->grabWindow(0).save(qEnvironmentVariable("C2D_SHOT3"));
            });
            QTimer::singleShot(6800, &app, []{ QApplication::quit(); });
        }
        else if (mode == "lib") {
            // 控件库 15 种：拖几个出来摆一屏
            QTimer::singleShot(500, &win, [&win]{
                auto addItem = [&win](const CanvasItem &it){ win.canvas->items.push_back(it); };
                CanvasItem p; p.type="panel"; p.name="面板"; p.color=QColor("#2b3542");
                p.rect=QRectF(180,140,520,600); p.layout=1; p.spacing=22; addItem(p);
                CanvasItem tx; tx.type="text"; tx.name="标题"; tx.text="UI 控件库演示"; tx.fontSize=32;
                tx.color=QColor("#ffffff"); tx.rect=QRectF(240,170,360,50); tx.parent=0; addItem(tx);
                CanvasItem b; b.type="button"; b.name="Button_001"; b.text="开始游戏";
                b.font="Noto Sans CJK SC"; b.fontSize=26; b.color=QColor("#ffffff");
                b.rect=QRectF(300,240,280,60); b.parent=0; addItem(b);
                CanvasItem s; s.type="slider"; s.name="滑动条"; s.text="BGM 音量"; s.value=70;
                s.rect=QRectF(250,320,380,54); s.parent=0; addItem(s);
                CanvasItem c; c.type="checkbox"; c.name="复选框"; c.text="显示 FPS";
                c.checked=true; c.rect=QRectF(270,395,260,44); c.parent=0; addItem(c);
                CanvasItem d; d.type="dropdown"; d.name="下拉框"; d.text="1080P";
                d.rect=QRectF(270,455,240,52); d.parent=0; addItem(d);
                CanvasItem tg; tg.type="toggle"; tg.name="开关"; tg.text="垂直同步";
                tg.checked=true; tg.rect=QRectF(270,525,260,44); tg.parent=0; addItem(tg);
                CanvasItem pr; pr.type="progress"; pr.name="进度条"; pr.text="HP"; pr.color=QColor("#e05a6e"); pr.value=72;
                pr.rect=QRectF(250,590,380,42); pr.parent=0; addItem(pr);
                CanvasItem sv; sv.type="scrollview"; sv.name="滚动视图";
                sv.rect=QRectF(760,140,380,280); addItem(sv);
                CanvasItem gr; gr.type="grid"; gr.name="网格布局";
                gr.rect=QRectF(760,450,300,290); addItem(gr);
                CanvasItem mk; mk.type="mask"; mk.name="遮罩";
                mk.rect=QRectF(1180,160,180,180); addItem(mk);
                CanvasItem ta; ta.type="textarea"; ta.name="多行文本"; ta.text="第一行：欢迎\n第二行：来到\n第三行：VN2DEditor";
                ta.rect=QRectF(1160,400,300,150); addItem(ta);
                emit win.canvas->itemsChanged();
                win.canvas->update();
            });
            QTimer::singleShot(1700, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2700, &app, []{ QApplication::quit(); });
        }
        else if (mode == "layout") {
            // 布局引擎：主菜单模板 = Panel VBox 自动排版
            QTimer::singleShot(500, &win, [&win]{
                win.applyMainMenuTemplate();
            });
            QTimer::singleShot(1700, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2700, &app, []{ QApplication::quit(); });
        }
        else if (mode == "hover") {
            // 三态图对比：常态 -> 悬停（鼠标真实移上去换图）
            QTimer::singleShot(500, &win, [&win]{
                QString asDir = QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets");
                CanvasItem b;
                b.type = "button"; b.text = "开始游戏"; b.name = "Button_001";
                b.font = "Noto Sans CJK SC"; b.fontSize = 30; b.color = QColor("#ffffff");
                b.rect = QRectF(600, 360, 360, 96);
                b.imgNormal = asDir + "/btn_normal.png";
                b.imgHover = asDir + "/btn_hover.png";
                b.imgPressed = asDir + "/btn_pressed.png";
                win.canvas->addItem(b);
                win.canvas->running = true;
                win.canvas->hoverIdx = -1;
                qInfo() << "[演示] 三态图按钮就绪（Normal/Hover/Pressed 纯图片渲染）";
            });
            QTimer::singleShot(1400, &win, [shot]{   // 常态帧
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2000, &win, [&win]{   // 鼠标真实移到按钮上 -> hover 图
                if (win.canvas->items.empty()) return;
                QPointF wp = win.canvas->toWidget(win.canvas->items[0].rect.center());
                QPoint gp = win.canvas->mapToGlobal(wp.toPoint());
                QCursor::setPos(gp);
                // mouse tracking 生效：发一个真实移动事件确保 hover 刷新
                QMouseEvent mv(QEvent::MouseMove, wp, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(win.canvas, &mv);
                qInfo() << "[演示] 鼠标移入按钮 -> Hover 状态图切换";
            });
            QTimer::singleShot(2800, &win, []{   // 悬停帧（第二张：C2D_SHOT2）
                if (auto *scr = QGuiApplication::primaryScreen())
                    if (!qEnvironmentVariable("C2D_SHOT2").isEmpty())
                        scr->grabWindow(0).save(qEnvironmentVariable("C2D_SHOT2"));
            });
            QTimer::singleShot(3600, &app, []{ QApplication::quit(); });
        }
        else if (mode == "settings") {
            // 系统设置面板：真实逻辑（滑动条=BGM音量 / 下拉框=分辨率 / 开关=全屏）
            QTimer::singleShot(500, &win, [&win]{
                win.applySettingsTemplate();
            });
            // 真实拖动滑动条到 85%：走 QProcess 事件管线（press + move）
            QTimer::singleShot(1900, &win, [&win]{
                if (win.canvas->items.size() < 2) return;
                win.canvas->running = true;   // 运行态：滑条点击即真实改值
                // items: 0 panel 1 text 2 slider
                QRectF r = win.canvas->items[2].rect;
                QPointF c = win.canvas->toWidget(QPointF(r.left() + r.width() * 0.85, r.center().y()));
                win.canvas->demoClickWidget(c);
                // 按住再拖一下，展示连续拖动
                QMouseEvent mv(QEvent::MouseMove, c, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(win.canvas, &mv);
            });
            QTimer::singleShot(3400, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4400, &app, []{ QApplication::quit(); });
        }
        else if (mode == "anchor9") {
            // 锚点 + 九宫格：属性面板实拍
            QTimer::singleShot(500, &win, [&win]{
                QString asDir = QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets");
                CanvasItem img;
                img.type = "image"; img.name = "面板底图";
                img.imagePath = asDir + "/panel_9slice.png";
                img.nineSlice = true; img.sliceMargin = 24;
                img.rect = QRectF(560, 260, 480, 300);
                img.anchor = 4;   // 居中锚点
                win.canvas->addItem(img);
                win.canvas->selected = 0;
                emit win.canvas->selectionChanged();
                qInfo() << "[演示] 九宫格切图 9-Slice 已启用（四角固定不变形）+ 锚点=居中";
            });
            QTimer::singleShot(1700, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(2700, &app, []{ QApplication::quit(); });
        }
        else if (mode == "search") {
            // Ctrl+P 全局搜索：资产 + 画布对象 + Lua 内容
            QTimer::singleShot(600, &win, [&win]{
                win.applyMainMenuTemplate();
            });
            QTimer::singleShot(1400, &win, [&win]{
                win.openQuickSearch();
            });
            QTimer::singleShot(2200, &win, [&win]{
                if (auto *dlg = win.findChild<QuickSearchDialog*>())
                    dlg->findChild<QLineEdit*>()->setText("lua");
            });
            QTimer::singleShot(3200, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4200, &app, []{ QApplication::quit(); });
        }
        else if (mode == "tabs") {
            // 死命令 1 实拍：双标签页 + 控制台拖出为独立窗口 + 运行切 Game 真实跑
            QTimer::singleShot(600, &win, [&win]{
                win.enterPhysMode();   // 纯 2D 游戏模式
                QString lvl = QDir::home().filePath("Physics3DProjects/2D跳跃演示/assets/level2d.scene2d");
                if (!win.canvas->loadFile(lvl))
                    win.applyMainMenuTemplate();
            });
            QTimer::singleShot(1500, &win, [&win]{
                win.consoleDock->setFloating(true);
                win.consoleDock->resize(620, 240);
                win.consoleDock->move(860, 620);
                qInfo() << "[2D] 控制台拖出为独立窗口（1/3，上限 3 个，超限自动吸回）";
            });
            QTimer::singleShot(2700, &win, [&win]{ win.toggleRun(); });   // 运行 -> Game 标签页真实运行
            QTimer::singleShot(4200, &win, [shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(5200, &app, []{ QApplication::quit(); });
        }
    }
    return app.exec();
}

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    qInstallMessageHandler(c2dMessageHandler);   // 控制台先行接管（崩溃处理器再链式桥接）
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VN2DEditor", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
#include "VN2DEditor.moc"
