// VNSocketEditor —— 插座编辑器。由 3DEditor 文件树双击 .socket 通过 QProcess 拉起，argv[1] 为插座路径。
// 左：属性面板（节点名/Target 选择器）| 中：无限画布（节点+贝塞尔连线）| 右：模块库（socket_nodes.json 数据驱动）| 底：控制台
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QMainWindow>
#include <QScrollArea>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QPainterPath>
#include <vector>
#include <map>
#include <cmath>

// ============ 数据模型（与 3DEditor 的 SocketSystem.load 共用一份 JSON 格式） ============
struct SPort { QString name, type; };
struct SLink { int fromNode = 0, fromPort = 0, toNode = 0, toPort = 0; };
struct SNode {
    int id = 0;
    QString typeId, name, category, color = "#4a5568", target;
    QJsonObject params;
    std::vector<SPort> inputs, outputs;
    int x = 60, y = 60;
    // ---- 子图（复合节点）：内部藏着一份独立小图 ----
    bool isSub = false;
    std::vector<SNode> subNodes;
    std::vector<SLink> subLinks;
};

static QColor portColor(const QString &t) {
    if (t == "Flow") return QColor(255, 180, 84);      // 橙
    if (t == "Number") return QColor(127, 220, 127);   // 绿
    if (t == "String") return QColor(185, 167, 255);   // 紫
    if (t == "Bool") return QColor(255, 90, 110);      // 红
    if (t == "Object") return QColor(127, 179, 255);   // 蓝
    return QColor(160, 170, 185);
}

// ============ 节点画布：自绘节点 + 贝塞尔连线 + 端口拉线 + 类型校验 ============
class NodeCanvas : public QWidget {
    Q_OBJECT
public:
    std::vector<SNode> *nodes = nullptr;
    std::vector<SLink> *links = nullptr;
    int nextId = 2;
    int selected = -1;                       // 选中节点下标
    bool modified = false;
    // ---- 子图视图：openSub>=0 表示正在查看 (*nodes)[openSub] 的内部 ----
    int openSub = -1;
    std::vector<SNode> viewNodes;
    std::vector<SLink> viewLinks;
    // ---- 框选多选 ----
    QVector<int> selGroup;
    QRectF bandRect; bool banding = false;

    NodeCanvas(std::vector<SNode> *n, std::vector<SLink> *l, QWidget *p = nullptr)
        : QWidget(p), nodes(n), links(l) {
        setFixedSize(2000, 1400);
        setMouseTracking(false);
    }
    void setLogger(std::function<void(const QString &, bool)> fn) { log = fn; }

    // 当前视图的图（根图 or 子图内部）
    std::vector<SNode> &vn() { return openSub >= 0 ? viewNodes : *nodes; }
    std::vector<SLink> &vl() { return openSub >= 0 ? viewLinks : *links; }
    const std::vector<SNode> &cvn() const { return openSub >= 0 ? viewNodes : *nodes; }
    const std::vector<SLink> &cvl() const { return openSub >= 0 ? viewLinks : *links; }

    // ---- 子图：折叠选中的多个节点为一个复合节点 ----
    void collapseSelection() {
        if (openSub >= 0 || selGroup.size() < 2) return;
        SNode sub;
        sub.id = nextId++;
        sub.typeId = "子图_0001"; sub.name = QString("子图（%1 个节点）").arg(selGroup.size());
        sub.category = "子图"; sub.color = "#8e44ad"; sub.isSub = true;
        sub.x = 640; sub.y = 200;
        sub.inputs.push_back({"Flow", "Flow"});
        sub.outputs.push_back({"Flow", "Flow"});
        // 选中的节点与它们之间的连线搬进子图内部
        std::vector<int> ids;
        for (int idx : selGroup) ids.push_back(vn()[size_t(idx)].id);
        for (int idx : selGroup) sub.subNodes.push_back(vn()[size_t(idx)]);
        for (const auto &l : vl()) {
            bool fa = std::find(ids.begin(), ids.end(), l.fromNode) != ids.end();
            bool ta = std::find(ids.begin(), ids.end(), l.toNode) != ids.end();
            if (fa && ta) sub.subLinks.push_back(l);
        }
        // 主图删除被折叠节点（连带连线），换成复合节点
        std::vector<SNode> kept;
        for (auto &n : vn()) {
            bool in = std::find(ids.begin(), ids.end(), n.id) != ids.end();
            if (!in) kept.push_back(n);
        }
        vn() = kept;
        std::vector<SLink> keptL;
        for (const auto &l : vl()) {
            bool fa = std::find(ids.begin(), ids.end(), l.fromNode) != ids.end();
            bool ta = std::find(ids.begin(), ids.end(), l.toNode) != ids.end();
            if (!fa && !ta) keptL.push_back(l);
        }
        vl() = keptL;
        vn().push_back(sub);
        selected = int(vn().size()) - 1;
        selGroup.clear();
        modified = true;
        if (log) log(QString("[子图] 已把 %1 个节点折叠为 1 个复合节点（双击可进入查看内部）").arg(ids.size()), false);
        update();
        emit selectionChanged(selected);
    }

    // ---- 子图：双击复合节点进入内部 ----
    bool enterSub(int i) {
        if (openSub >= 0) return false;
        SNode &sub = (*nodes)[size_t(i)];
        if (!sub.isSub) return false;
        viewNodes = sub.subNodes;
        viewLinks = sub.subLinks;
        openSub = i;
        selected = -1; selGroup.clear();
        if (log) log(QString("[子图] 进入「%1」内部（共 %2 个节点），点击左侧【返回上级】退出")
                     .arg(sub.name).arg(viewNodes.size()), false);
        update();
        emit selectionChanged(-1);
        return true;
    }

    // ---- 子图：返回上级 ----
    void exitSub() {
        if (openSub < 0) return;
        SNode &sub = (*nodes)[size_t(openSub)];
        sub.subNodes = viewNodes;
        sub.subLinks = viewLinks;
        if (log) log(QString("[子图] 已返回上级，内部 %1 个节点的改动已保留在复合节点里").arg(viewNodes.size()), false);
        openSub = -1;
        viewNodes.clear(); viewLinks.clear();
        selected = -1; selGroup.clear();
        modified = true;
        update();
        emit selectionChanged(-1);
    }

    // ---- 真实代码路径：从模块库定义添加节点（双击/演示共用） ----
    int addNodeFromDef(const QJsonObject &def, int x, int y) {
        SNode n;
        n.typeId = def.value("id").toString();
        n.name = def.value("name").toString();
        n.category = def.value("category").toString();
        n.color = def.value("color").toString("#4a5568");
        n.x = x; n.y = y;
        for (auto v : def.value("inputs").toArray()) {
            auto pj = v.toObject();
            n.inputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
        }
        for (auto v : def.value("outputs").toArray()) {
            auto pj = v.toObject();
            n.outputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
        }
        n.id = nextId++;
        vn().push_back(n);
        modified = true;
        if (log) log(QString("添加节点：%1（%2）").arg(n.name, n.category), false);
        update();
        return int(vn().size()) - 1;
    }

    // ---- 真实代码路径：端口连线（鼠标拉线/演示共用，含硬核类型校验） ----
    bool connectPorts(int fromIdx, int outPort, int toIdx, int inPort) {
        const SNode &f = vn()[size_t(fromIdx)];
        const SNode &t = vn()[size_t(toIdx)];
        QString outType = f.outputs.size() > size_t(outPort) ? f.outputs[size_t(outPort)].type : "";
        QString inType = t.inputs.size() > size_t(inPort) ? t.inputs[size_t(inPort)].type : "";
        if (outType.isEmpty() || inType.isEmpty()) return false;
        if (outType != inType) {
            if (log) log(QString("错误：连线失败！类型不匹配：'%1' 输出 %2 ≠ '%3' 输入 %4")
                .arg(f.name, outType, t.name, inType), true);
            update();
            return false;
        }
        vl().push_back({f.id, outPort, t.id, inPort});
        modified = true;
        if (log) log(QString("连线成功：%1 [%2] → %3 [%4]（%5）")
            .arg(f.name, f.outputs[size_t(outPort)].name, t.name, t.inputs[size_t(inPort)].name, inType), false);
        update();
        return true;
    }

    // ---- 真实代码路径：保存（节点+连线+params+target 全量 JSON） ----
    bool save(const QString &path) {
        QJsonObject root;
        QJsonArray nj;
        for (const auto &n : *nodes) {
            QJsonObject j;
            j["id"] = n.id; j["typeId"] = n.typeId; j["name"] = n.name;
            j["category"] = n.category; j["color"] = n.color; j["target"] = n.target;
            j["x"] = n.x; j["y"] = n.y;
            j["params"] = n.params;
            if (n.isSub) {
                j["isSub"] = true;
                QJsonArray sj;
                for (const auto &sn : n.subNodes) {
                    QJsonObject so;
                    so["id"] = sn.id; so["typeId"] = sn.typeId; so["name"] = sn.name;
                    so["category"] = sn.category; so["color"] = sn.color; so["x"] = sn.x; so["y"] = sn.y;
                    QJsonArray si;
                    for (auto &pp : sn.inputs) si.append(QJsonObject{{"name", pp.name}, {"type", pp.type}});
                    so["inputs"] = si;
                    QJsonArray so2;
                    for (auto &pp : sn.outputs) so2.append(QJsonObject{{"name", pp.name}, {"type", pp.type}});
                    so["outputs"] = so2;
                    sj.append(so);
                }
                j["subNodes"] = sj;
                QJsonArray sl;
                for (const auto &slk : n.subLinks)
                    sl.append(QJsonObject{{"fromNode", slk.fromNode}, {"fromPort", slk.fromPort},
                                           {"toNode", slk.toNode}, {"toPort", slk.toPort}});
                j["subLinks"] = sl;
            }
            QJsonArray ins;
            for (auto &p : n.inputs) ins.append(QJsonObject{{"name", p.name}, {"type", p.type}});
            j["inputs"] = ins;
            QJsonArray outs;
            for (auto &p : n.outputs) outs.append(QJsonObject{{"name", p.name}, {"type", p.type}});
            j["outputs"] = outs;
            nj.append(j);
        }
        root["nodes"] = nj;
        QJsonArray cj;
        for (const auto &l : *links)
            cj.append(QJsonObject{{"fromNode", l.fromNode}, {"fromPort", l.fromPort},
                                   {"toNode", l.toNode}, {"toPort", l.toPort}});
        root["connections"] = cj;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) { if (log) log(QString("错误：无法写 %1").arg(path), true); return false; }
        f.write(QJsonDocument(root).toJson());
        if (log) log(QString("插座已保存：%1（节点=%2 连线=%3）")
            .arg(QFile(path).fileName()).arg(nodes->size()).arg(links->size()), false);
        modified = false;
        return true;
    }

    int findByNameContains(const QString &s) {
        for (size_t i = 0; i < vn().size(); ++i) if (vn()[i].name.contains(s)) return int(i);
        return -1;
    }

signals:
    void selectionChanged(int idx);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // 网格背景
        p.fillRect(rect(), QColor(21, 24, 30));
        p.setPen(QPen(QColor(40, 46, 58), 1));
        for (int x = 0; x < width(); x += 32) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 32) p.drawLine(0, y, width(), y);

        // 连线（贝塞尔）
        for (const auto &l : cvl()) {
            int fi = idxById(l.fromNode), ti = idxById(l.toNode);
            if (fi < 0 || ti < 0) continue;
            QPointF a = outPortPos(size_t(fi), l.fromPort);
            QPointF b = inPortPos(size_t(ti), l.toPort);
            QString t = cvn()[size_t(fi)].outputs[size_t(l.fromPort)].type;
            p.setPen(QPen(portColor(t), 3));
            QPainterPath path(a);
            float dx = qMax(40.0, fabs(b.x() - a.x()) * 0.5);
            path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
            p.drawPath(path);
        }
        // 正在拉动的临时线
        if (dragOut >= 0 && dragNode >= 0) {
            QPointF a = outPortPos(size_t(dragNode), dragOut);
            QPointF b = mapFromGlobal(QCursor::pos());
            p.setPen(QPen(portColor(dragType), 3, Qt::DashLine));
            QPainterPath path(a);
            float dx = qMax(40.0, fabs(b.x() - a.x()) * 0.5);
            path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
            p.drawPath(path);
        }

        // 框选矩形
        if (banding && !bandRect.isNull()) {
            p.setPen(QPen(QColor(127, 179, 255), 1, Qt::DashLine));
            p.setBrush(QColor(127, 179, 255, 40));
            p.drawRect(bandRect);
        }
        // 节点
        for (size_t i = 0; i < cvn().size(); ++i) {
            const SNode &n = cvn()[i];
            int h = nodeHeight(n);
            QRectF r(n.x, n.y, NODE_W, h);
            QColor head(n.color);
            bool inGroup = selGroup.contains(int(i));
            p.setPen(QPen((i == size_t(selected) || inGroup) ? QColor(255, 255, 255) : QColor(70, 80, 100),
                          (i == size_t(selected) || inGroup) ? 2 : 1));
            p.setBrush(QColor(38, 42, 51));
            p.drawRoundedRect(r, 6, 6);
            p.setBrush(head);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(n.x, n.y, NODE_W, HEADER_H), 6, 6);
            p.setPen(QColor(255, 255, 255));
            p.setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
            QString headTxt = n.name;
            if (n.isSub) headTxt += "  ⧉";
            p.drawText(QRectF(n.x + 6, n.y, NODE_W - 12, HEADER_H), Qt::AlignVCenter | Qt::AlignLeft, headTxt);

            p.setFont(QFont("Microsoft YaHei", 8));
            if (n.isSub) {
                p.setPen(QColor(214, 160, 255));
                p.drawText(QRectF(n.x + 6, n.y + HEADER_H, NODE_W - 12, 18), Qt::AlignVCenter,
                           "⧉ 复合节点：双击进入内部");
            }
            for (size_t k = 0; k < n.inputs.size(); ++k) {
                float py = n.y + HEADER_H + PORT_H * k;
                p.setBrush(portColor(n.inputs[k].type));
                p.drawEllipse(QPointF(n.x, py), 5, 5);
                p.setPen(QColor(200, 210, 225));
                p.drawText(QRectF(n.x + 10, py - 9, NODE_W / 2, 18), Qt::AlignVCenter, n.inputs[k].name);
            }
            for (size_t k = 0; k < n.outputs.size(); ++k) {
                float py = n.y + HEADER_H + PORT_H * k;
                p.setBrush(portColor(n.outputs[k].type));
                p.drawEllipse(QPointF(n.x + NODE_W, py), 5, 5);
                p.setPen(QColor(200, 210, 225));
                p.drawText(QRectF(n.x + NODE_W / 2, py - 9, NODE_W / 2 - 10, 18), Qt::AlignVCenter | Qt::AlignRight,
                           n.outputs[k].name);
            }
        }
    }

    void mousePressEvent(QMouseEvent *e) override {
        QPointF pos = e->position();
        int ni; int port; bool isOut;
        if (hitPort(pos, ni, port, isOut)) {
            if (isOut) { dragNode = ni; dragOut = port; dragType = vn()[size_t(ni)].outputs[size_t(port)].type; }
            return;
        }
        int i = hitNode(pos);
        if (i >= 0) {
            selected = i;
            emit selectionChanged(i);
            dragNodeIdx = i;
            dragStartMouse = pos;
            dragStartPos = QPoint(vn()[size_t(i)].x, vn()[size_t(i)].y);
            update();
            return;
        }
        // 空白处按下：开始框选（真实橡皮筋多选）
        selected = -1;
        selGroup.clear();
        banding = true;
        bandRect = QRectF(pos, pos);
        emit selectionChanged(-1);
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        QPointF pos = e->position();
        int i = hitNode(pos);
        if (i >= 0) enterSub(i);   // 双击复合节点 → 进入子图内部
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (dragNodeIdx >= 0) {
            QPointF d = e->position() - dragStartMouse;
            vn()[size_t(dragNodeIdx)].x = dragStartPos.x() + int(d.x());
            vn()[size_t(dragNodeIdx)].y = dragStartPos.y() + int(d.y());
            modified = true;
            update();
        } else if (banding) {
            bandRect = QRectF(bandRect.topLeft(), e->position());
            update();
        } else if (dragOut >= 0) {
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (dragNodeIdx >= 0) { dragNodeIdx = -1; return; }
        if (banding) {
            banding = false;
            QRectF r = bandRect.normalized();
            selGroup.clear();
            for (size_t i = 0; i < vn().size(); ++i) {
                const SNode &n = vn()[i];
                QRectF nr(n.x, n.y, NODE_W, nodeHeight(n));
                if (r.intersects(nr)) selGroup.push_back(int(i));
            }
            bandRect = QRectF();
            if (log && selGroup.size() >= 2)
                log(QString("[子图] 框选了 %1 个节点（右键 → 折叠为子图）").arg(selGroup.size()), false);
            update();
            return;
        }
        if (dragOut >= 0) {
            QPointF pos = e->position();
            int ni, port; bool isOut;
            if (hitPort(pos, ni, port, isOut) && !isOut && ni != dragNode) {
                bool ok = connectPorts(dragNode, dragOut, ni, port);
                if (!ok) {                       // 硬核校验：类型不匹配 → 立即断开 + 红色报错 + 弹窗
                    QMessageBox::warning(this, "连线失败", "⚠️ 连线失败：类型不匹配！");
                }
            }
            dragOut = -1; dragNode = -1;
            update();
        }
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        int i = hitNode(e->pos());
        if (i < 0) return;
        selected = i;
        emit selectionChanged(i);
        QMenu menu(this);
        QAction *actCollapse = nullptr;
        if (selGroup.size() >= 2 && openSub < 0)
            actCollapse = menu.addAction(QString("折叠为子图（%1 个节点）").arg(selGroup.size()));
        QAction *actEnter = (i >= 0 && vn()[size_t(i)].isSub) ? menu.addAction("进入子图内部") : nullptr;
        QAction *del = menu.addAction("删除节点");
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == actCollapse) { collapseSelection(); return; }
        if (chosen == actEnter) { enterSub(i); return; }
        if (chosen == del) {
            const SNode &n = vn()[size_t(i)];
            if (n.name.startsWith("Start")) {
                if (log) log(QString("根节点 %1 不可删除").arg(n.name), true);
                return;
            }
            // 连带删除相关连线
            int id = n.id;
            std::vector<SLink> &L = vl();
            L.erase(std::remove_if(L.begin(), L.end(),
                [id](const SLink &l){ return l.fromNode == id || l.toNode == id; }), L.end());
            std::vector<SNode> &N = vn();
            N.erase(N.begin() + i);
            selected = -1;
            emit selectionChanged(-1);
            if (log) log(QString("节点已删除（%1）").arg(id), false);
            update();
        }
    }

private:
    static constexpr int NODE_W = 190, HEADER_H = 26, PORT_H = 20;
    std::function<void(const QString &, bool)> log;
    int dragNodeIdx = -1; QPointF dragStartMouse; QPoint dragStartPos;
    int dragOut = -1, dragNode = -1; QString dragType;

    int nodeHeight(const SNode &n) const {
        return HEADER_H + PORT_H * int(std::max(n.inputs.size(), n.outputs.size())) + 8;
    }
    int idxById(int id) {
        for (size_t i = 0; i < vn().size(); ++i) if (vn()[size_t(i)].id == id) return int(i);
        return -1;
    }
    QPointF outPortPos(size_t i, int port) const {
        const SNode &n = cvn()[i];
        return QPointF(n.x + NODE_W, n.y + HEADER_H + PORT_H * port);
    }
    QPointF inPortPos(size_t i, int port) const {
        const SNode &n = cvn()[i];
        return QPointF(n.x, n.y + HEADER_H + PORT_H * port);
    }
    int hitNode(const QPointF &pos) const {
        for (int i = int(cvn().size()) - 1; i >= 0; --i) {
            const SNode &n = cvn()[size_t(i)];
            if (pos.x() >= n.x && pos.x() <= n.x + NODE_W &&
                pos.y() >= n.y && pos.y() <= n.y + nodeHeight(n)) return i;
        }
        return -1;
    }
    bool hitPort(const QPointF &pos, int &ni, int &port, bool &isOut) {
        for (int i = int(cvn().size()) - 1; i >= 0; --i) {
            const SNode &n = cvn()[size_t(i)];
            for (size_t k = 0; k < n.outputs.size(); ++k) {
                QPointF c = outPortPos(size_t(i), int(k));
                if (QLineF(pos, c).length() <= 9) { ni = i; port = int(k); isOut = true; return true; }
            }
            for (size_t k = 0; k < n.inputs.size(); ++k) {
                QPointF c = inPortPos(size_t(i), int(k));
                if (QLineF(pos, c).length() <= 9) { ni = i; port = int(k); isOut = false; return true; }
            }
        }
        return false;
    }
};

// ============ 主窗口 ============
class SocketLabWindow : public QMainWindow {
    Q_OBJECT
public:
    SocketLabWindow(const QString &socketPath, QWidget *parent = nullptr) : QMainWindow(parent) {
        socketFile = socketPath;
        projectRoot = QDir(QFileInfo(socketPath).dir().absolutePath() + "/..").absolutePath();  // assets/ 上一级

        setWindowTitle(QString("VNSocketEditor 插座编辑器 - %1").arg(QFileInfo(socketPath).fileName()));
        resize(1500, 900);
        loadGraph();
        setupUI();
        connect(canvas, &NodeCanvas::selectionChanged, this, &SocketLabWindow::onSelect);

        // 演示钩子（沙箱截图用）
        QString mode = qEnvironmentVariable("SOCKET_AUTO_MODE");
        QString shot = qEnvironmentVariable("SOCKET_SHOT");
        if (!mode.isEmpty() && !shot.isEmpty()) runAuto(mode, shot);
    }

private slots:
    void onSelect(int idx) { refreshProps(idx); syncCrumb(); }

    void onModuleDoubleClicked(QTreeWidgetItem *item, int) {
        if (!item || item->data(0, Qt::UserRole).type() != QVariant::String) return;  // 分类行
        QString defJson = item->data(0, Qt::UserRole).toString();
        QJsonDocument d = QJsonDocument::fromJson(defJson.toUtf8());
        canvas->addNodeFromDef(d.object(), 620 + (canvas->nextId % 3) * 40, 200 + canvas->nextId * 26);
        refreshProps(int(canvas->vn().size()) - 1);
    }

    void onTargetChanged(const QString &t) {
        if (canvas->selected < 0) return;
        canvas->vn()[size_t(canvas->selected)].target = t;
        canvas->modified = true;
        consoleLog(QString("目标选择器：'%1' 的 Target 设为 %2")
            .arg((*canvas->nodes)[size_t(canvas->selected)].name, t.isEmpty() ? "Self" : t), false);
    }

    void onSave() {
        canvas->save(socketFile);
    }

    void syncCrumb() {
        if (canvas->openSub >= 0) {
            crumbLab->setText(QString("📍 主图 → 子图「%1」").arg((*canvas->nodes)[size_t(canvas->openSub)].name));
            backBtn->setVisible(true);
        } else {
            crumbLab->setText("📍 主图（根）");
            backBtn->setVisible(false);
        }
    }

private:
    static void grabFullscreen(const QString &shot) {
        if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
    }
    void consoleLog(const QString &s, bool err) {
        if (!console) return;
        console->appendHtml(QString("<span style=\"color:%1\">%2</span>")
            .arg(err ? "#ff5a6e" : "#ffb454", s.toHtmlEscaped()));
    }

    void loadGraph() {
        QFile f(socketFile);
        if (f.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(f.readAll());
            auto root = doc.object();
            for (auto v : root.value("nodes").toArray()) {
                auto j = v.toObject();
                SNode n;
                n.id = j.value("id").toInt();
                n.typeId = j.value("typeId").toString();
                n.name = j.value("name").toString();
                n.category = j.value("category").toString();
                n.color = j.value("color").toString("#4a5568");
                n.target = j.value("target").toString();
                n.params = j.value("params").toObject();
                n.x = j.value("x").toInt(60); n.y = j.value("y").toInt(60);
                for (auto po : j.value("inputs").toArray()) {
                    auto pj = po.toObject();
                    n.inputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
                }
                for (auto po : j.value("outputs").toArray()) {
                    auto pj = po.toObject();
                    n.outputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
                }
                n.isSub = j.value("isSub").toBool();
                if (n.isSub) {
                    for (auto sv : j.value("subNodes").toArray()) {
                        auto sj = sv.toObject();
                        SNode sn;
                        sn.id = sj.value("id").toInt();
                        sn.typeId = sj.value("typeId").toString();
                        sn.name = sj.value("name").toString();
                        sn.category = sj.value("category").toString();
                        sn.color = sj.value("color").toString("#4a5568");
                        sn.x = sj.value("x").toInt(60); sn.y = sj.value("y").toInt(60);
                        for (auto po : sj.value("inputs").toArray()) {
                            auto pj = po.toObject();
                            sn.inputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
                        }
                        for (auto po : sj.value("outputs").toArray()) {
                            auto pj = po.toObject();
                            sn.outputs.push_back({pj.value("name").toString(), pj.value("type").toString()});
                        }
                        n.subNodes.push_back(sn);
                    }
                    for (auto sv : j.value("subLinks").toArray()) {
                        auto sj = sv.toObject();
                        n.subLinks.push_back({sj.value("fromNode").toInt(), sj.value("fromPort").toInt(),
                                              sj.value("toNode").toInt(), sj.value("toPort").toInt()});
                    }
                }
                graph.nodes.push_back(n);
            }
            for (auto v : root.value("connections").toArray()) {
                auto j = v.toObject();
                graph.links.push_back({j.value("fromNode").toInt(), j.value("fromPort").toInt(),
                                       j.value("toNode").toInt(), j.value("toPort").toInt()});
            }
        }
        // 空文件：自带不可删除根节点 Start
        if (graph.nodes.empty()) {
            SNode start;
            start.id = 1; start.typeId = "场景管理_0000"; start.name = "Start（基础启动）";
            start.category = "场景管理"; start.color = "#3f51b5";
            start.outputs.push_back({"Flow", "Flow"});
            start.x = 80; start.y = 160;
            graph.nodes.push_back(start);
            consoleLog("新文件：已创建不可删除根节点 Start（基础启动）", false);
        }
        canvas_nextId = 2;
        for (auto &n : graph.nodes) canvas_nextId = std::max(canvas_nextId, n.id + 1);
    }

    void setupUI() {
        // 中：无限画布（滚动区域承载）
        canvas = new NodeCanvas(&graph.nodes, &graph.links);
        canvas->nextId = canvas_nextId;
        canvas->setLogger([this](const QString &s, bool err){ consoleLog(s, err); });
        auto *scroll = new QScrollArea;
        scroll->setWidget(canvas);
        scroll->setWidgetResizable(false);

        // 右：模块库（socket_nodes.json 数据驱动，1014 个模块）
        lib = new QTreeWidget;
        lib->setHeaderLabel("模块库（双击添加）");
        lib->setUniformRowHeights(true);
        loadLibrary();

        // 左：属性面板
        auto *left = new QWidget;
        left->setStyleSheet("background:#1d2129;");
        auto *ll = new QVBoxLayout(left);
        propLay = ll;
        auto *propTitle = new QLabel("属性面板");
        propTitle->setStyleSheet("color:#c9d4e4; font-weight:bold;");
        ll->addWidget(propTitle);
        nodeNameLab = new QLabel("未选中节点");
        nodeNameLab->setStyleSheet("font-weight:bold; color:#7fb3ff;");
        ll->addWidget(nodeNameLab);
        auto *form = new QFormLayout;
        targetCombo = new QComboBox;
        auto *targetLab = new QLabel("Target");
        targetLab->setStyleSheet("color:#c9d4e4;");
        form->addRow(targetLab, targetCombo);
        ll->addLayout(form);
        ll->addStretch();
        auto *saveBtn = new QPushButton("保存 (Ctrl+S)");
        saveBtn->setStyleSheet("background:#2f6fab;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        connect(saveBtn, &QPushButton::clicked, this, &SocketLabWindow::onSave);
        ll->addWidget(saveBtn);
        // ---- 子图导航：面包屑 + 返回上级 ----
        crumbLab = new QLabel("📍 主图（根）");
        crumbLab->setStyleSheet("color:#d6a0ff; font-weight:bold;");
        ll->addWidget(crumbLab);
        backBtn = new QPushButton("← 返回上级（退出子图）");
        backBtn->setStyleSheet("background:#8e44ad;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        backBtn->setVisible(false);
        connect(backBtn, &QPushButton::clicked, this, [this]{ canvas->exitSub(); syncCrumb(); });
        ll->addWidget(backBtn);

        // 底：控制台
        console = new QPlainTextEdit;
        console->setReadOnly(true);
        console->setMaximumBlockCount(400);
        console->setFont(QFont("Monospace", 9));
        console->setStyleSheet("QPlainTextEdit{background:#15181e; color:#ffb454; border:none;}");

        auto *central = new QWidget;
        auto *vl = new QVBoxLayout(central);
        auto *hl = new QHBoxLayout;
        hl->addWidget(left, 1);
        hl->addWidget(scroll, 4);
        hl->addWidget(lib, 2);
        vl->addLayout(hl, 4);
        vl->addWidget(console, 1);
        setCentralWidget(central);
    }

    void loadLibrary() {
        QFile f(projectRoot + "/socket_nodes.json");
        if (!f.open(QIODevice::ReadOnly)) { consoleLog(QString("错误：找不到模块库 socket_nodes.json"), true); return; }
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        std::map<QString, std::vector<QJsonObject>> cats;
        for (auto v : root.value("nodes").toArray()) cats[v.toObject().value("category").toString()].push_back(v.toObject());
        for (auto &kv : cats) {
            auto *catItem = new QTreeWidgetItem(lib, {QString("%1 (%2)").arg(kv.first).arg(kv.second.size())});
            for (auto &def : kv.second) {
                auto *mi = new QTreeWidgetItem(catItem, {def.value("name").toString()});
                mi->setData(0, Qt::UserRole, QString::fromUtf8(QJsonDocument(def).toJson(QJsonDocument::Compact)));
            }
        }
        connect(lib, &QTreeWidget::itemDoubleClicked, this, &SocketLabWindow::onModuleDoubleClicked);
        consoleLog(QString("模块库已加载：%1 个模块，%2 个分类").arg(root.value("count").toInt()).arg(cats.size()), false);
    }

    void refreshProps(int idx) {
        targetCombo->blockSignals(true);
        targetCombo->clear();
        if (idx < 0) {
            nodeNameLab->setText("未选中节点");
            targetCombo->setEnabled(false);
            targetCombo->blockSignals(false);
            return;
        }
        const SNode &n = canvas->vn()[size_t(idx)];
        nodeNameLab->setText(QString("%1（%2）").arg(n.name, n.category));
        // Target 下拉：Self / Player / Scene.X（从 director.json 动态读取）
        targetCombo->addItem("Self");
        QStringList names{"Player"};
        QFile df(projectRoot + "/director.json");
        if (df.open(QIODevice::ReadOnly)) {
            names.clear();
            for (auto v : QJsonDocument::fromJson(df.readAll()).object().value("objects").toArray())
                names << v.toString();
        }
        for (const QString &s : names) targetCombo->addItem(s.startsWith("Scene.") ? s : "Scene." + s);
        targetCombo->setEnabled(true);
        // 当前值回显
        if (n.target.isEmpty() || n.target == "Self") targetCombo->setCurrentText("Self");
        else targetCombo->setCurrentText(n.target);
        connect(targetCombo, &QComboBox::currentTextChanged, this, &SocketLabWindow::onTargetChanged, Qt::UniqueConnection);
        targetCombo->blockSignals(false);

        // 音频节点：音频文件选择器（assets 目录真实枚举）
        if (audioCombo) { audioCombo->deleteLater(); audioCombo = nullptr; }
        if (n.name.contains("BGM") || n.name.contains("音效")) {
            auto *fileLab = new QLabel("音频文件（assets）");
            fileLab->setStyleSheet("color:#c9d4e4;");
            audioCombo = new QComboBox;
            QDir adir(projectRoot + "/assets");
            QStringList audioFiles;
            for (const QFileInfo &fi : adir.entryInfoList(QStringList{"*.wav", "*.mp3", "*.ogg"}, QDir::Files))
                audioFiles << fi.fileName();
            audioCombo->addItems(audioFiles);
            QString cur = n.params.value("file").toString();
            if (!cur.isEmpty()) audioCombo->setCurrentText(cur);
            int nidx = idx;   // 当前选中节点
            connect(audioCombo, &QComboBox::currentTextChanged, this, [this, nidx](const QString &v){
                if (nidx < 0 || size_t(nidx) >= canvas->vn().size()) return;
                (*canvas->nodes)[size_t(nidx)].params["file"] = v;
                consoleLog(QString("音频文件已设为：%1").arg(v), false);
            });
            propLay->addWidget(fileLab);
            propLay->addWidget(audioCombo);
        }
    }

    QJsonObject libDef(const QString &nameMatch) const {
        for (int i = 0; i < lib->topLevelItemCount(); ++i) {
            auto *cat = lib->topLevelItem(i);
            for (int c = 0; c < cat->childCount(); ++c) {
                if (cat->child(c)->text(0).contains(nameMatch)) {
                    auto d = QJsonDocument::fromJson(cat->child(c)->data(0, Qt::UserRole).toString().toUtf8());
                    if (!d.isNull()) return d.object();
                }
            }
        }
        return {};
    }
    int addDef(const QJsonObject &def, int x, int y) {
        if (def.isEmpty()) return -1;
        return canvas->addNodeFromDef(def, x, y);
    }

    void runAuto(const QString &mode, const QString &shot) {
        if (mode == "full") {
            // 真实代码路径：模块库取定义 → 添加节点 → 连线 → Target 选 Self → 保存
            QTimer::singleShot(1200, this, [this]{
                int a = addDef(libDef("OnKeyDown 键W"), 180, 220);
                int b = addDef(libDef("AddForce向上"), 620, 220);
                fullA = a; fullB = b;
            });
            QTimer::singleShot(1800, this, [this]{
                canvas->connectPorts(fullA, 0, fullB, 0);      // Flow → Flow
                canvas->selected = fullB;
                refreshProps(fullB);
            });
            QTimer::singleShot(2400, this, [this]{
                onTargetChanged("Self");                        // Target 选择器：Self
                targetCombo->setCurrentText("Self");
            });
            QTimer::singleShot(3000, this, [this]{ onSave(); });   // 保存 .socket
            QTimer::singleShot(4200, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5200, qApp, &QCoreApplication::quit);
        } else if (mode == "typeerr") {
            // 硬核校验：Number 插 Flow → 立即断开 + 红色报错 + 弹窗
            QTimer::singleShot(1200, this, [this]{
                int f = addDef(libDef("Branch"), 180, 220);      // 输入 Flow
                int n = addDef(libDef("Add（加）"), 620, 420);    // 输出 Number
                fullA = f; fullB = n;
            });
            QTimer::singleShot(2000, this, [this]{
                consoleLog("尝试连线：Add（加） 输出 Number → Branch 输入 Flow", false);
                bool ok = canvas->connectPorts(fullB, 0, fullA, 0);   // Number → Flow：失败
                if (!ok) errBox = new QMessageBox(QMessageBox::Warning, "连线失败", "⚠️ 连线失败：类型不匹配！", QMessageBox::NoButton, this);
                if (errBox) errBox->show();
            });
            QTimer::singleShot(3400, this, [shot]{ grabFullscreen(shot); });   // 全屏拍弹窗+红字
            QTimer::singleShot(4600, this, [this]{ if (errBox) errBox->close(); });
            QTimer::singleShot(5600, qApp, &QCoreApplication::quit);
        } else if (mode == "target") {
            // Target 下拉展开（Self / Scene.X 动态读取）
            QTimer::singleShot(1200, this, [this]{
                fullB = addDef(libDef("AddForce向上"), 420, 240);
                canvas->selected = fullB;
                refreshProps(fullB);
            });
            QTimer::singleShot(2400, this, [this]{ targetCombo->showPopup(); });
            QTimer::singleShot(3600, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4600, qApp, &QCoreApplication::quit);
        } else if (mode == "lib") {
            // 模块库分类树密密麻麻
            QTimer::singleShot(1400, this, [this]{ lib->expandAll(); });
            QTimer::singleShot(3200, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(4200, qApp, &QCoreApplication::quit);
        } else if (mode == "event") {
            // 跨对象事件分发：Start → 发送事件 →（广播）→ 接收事件 → 生成掉落物
            // 附 Cast 防调错：Object 直连 Player 输入报类型错误 → 经 Cast 转换成功
            QTimer::singleShot(1200, this, [this]{
                fullB = addDef(libDef("发送事件（Send Event）"), 520, 140);
                evR  = addDef(libDef("接收事件（Receive Event）"), 520, 360);
                evD  = addDef(libDef("Spawn Object（生成物体）"), 940, 360);
                evC  = addDef(libDef("Cast（类型转换 Object→Player）"), 200, 560);
                evF  = addDef(libDef("ForEach（遍历）"), 200, 760);
                evP  = addDef(libDef("TakeDamage（玩家受伤）"), 620, 620);
            });
            QTimer::singleShot(2000, this, [this]{
                canvas->connectPorts(0, 0, fullB, 0);       // 根 Start → 发送事件
            });
            QTimer::singleShot(2700, this, [this]{
                // Object 直连 Player 输入：类型不匹配，连线失败弹红字
                consoleLog("【防调错】尝试：ForEach 输出 Object → TakeDamage 输入 Player", false);
                if (!canvas->connectPorts(evF, 1, evP, 1)) {
                    errBox = new QMessageBox(QMessageBox::Warning, "连线失败",
                        "⚠️ 连线失败：Object ≠ Player\nObject 必须先经过 Cast 类型转换才能接 Player！", QMessageBox::NoButton, this);
                    if (errBox) errBox->show();
                }
            });
            QTimer::singleShot(4800, this, [this]{
                if (errBox) { errBox->close(); errBox = nullptr; }
                consoleLog("【防调错】改为：ForEach → Cast 的 Object 输入 → Cast 输出 Player → TakeDamage", false);
                canvas->connectPorts(evF, 1, evC, 1);       // Object → Cast
                canvas->connectPorts(evC, 1, evP, 1);       // Player → Player，成功
            });
            QTimer::singleShot(5800, this, [this]{
                canvas->connectPorts(evR, 0, evD, 0);       // 接收事件 → 生成掉落物
                consoleLog("[Event] Boss 死亡 → 发送事件「BossDie」→ 跨对象广播", false);
            });
            QTimer::singleShot(6600, this, [this]{
                consoleLog("[Event] 「BossDie」→ 接收事件（掉落物管理器）→ 生成掉落物 T00暴君心脏", false);
                consoleLog("[Event] 事件分发完成：发送方与接收方无需互相引用，解耦成功", false);
            });
            QTimer::singleShot(7800, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(9000, qApp, &QCoreApplication::quit);
        } else if (mode == "subgraph") {
            // 子图折叠：建 3 节点连线 → 全选 → 折叠为复合节点 → 进入内部
            QTimer::singleShot(1200, this, [this]{
                fullA = addDef(libDef("AddForce向上"), 200, 160);
                fullB = addDef(libDef("AddForce向上"), 460, 300);
                evR   = addDef(libDef("AddForce向上"), 720, 440);
            });
            QTimer::singleShot(1900, this, [this]{
                canvas->connectPorts(fullA, 0, fullB, 0);
                canvas->connectPorts(fullB, 0, evR, 0);
            });
            QTimer::singleShot(2600, this, [this]{
                // 模拟框选：三个 AddForce 节点（索引 1~3，0 是根 Start）全部选中 → 折叠
                canvas->selGroup = {1, 2, 3};
                consoleLog("已框选 3 个节点，右键 →【折叠为子图】", false);
                canvas->collapseSelection();
            });
            QTimer::singleShot(3800, this, [shot]{ grabFullscreen(shot); });   // 复合节点
            QTimer::singleShot(4600, this, [this]{
                // 双击复合节点进入内部
                canvas->enterSub(int(canvas->cvn().size()) - 1);
            });
            QTimer::singleShot(5600, this, [shot]{ grabFullscreen(shot + ".b.png"); }); // 内部
            QTimer::singleShot(6600, qApp, &QCoreApplication::quit);
        } else if (mode == "filedlg") {
            // 死命令 3 实拍：系统调用分类 -> 拖出【打开文件选择器】节点 -> 连到【图片UI（ShowImage）】节点
            QTimer::singleShot(1200, this, [this]{
                consoleLog("新节点已就位：系统调用 → 打开文件选择器（OpenFileDialog）；交互动作 → OnInteract / 打开车门 / 开关手电筒 / 强制下车", false);
                fullA = addDef(libDef("打开文件选择器"), 300, 180);
                fullB = addDef(libDef("ShowImage"), 760, 340);
            });
            QTimer::singleShot(2200, this, [this]{
                canvas->connectPorts(fullA, 1, fullB, 1);   // FilePath(String) → ImagePath(String)
                consoleLog("连线成功：打开文件选择器 输出 FilePath → 显示图片 输入 ImagePath", false);
                canvas->selected = fullA;
                refreshProps(fullA);
            });
            QTimer::singleShot(3400, this, [this]{
                lib->expandAll();   // 露出新增的「系统调用」「交互动作」分类
            });
            QTimer::singleShot(4400, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5400, qApp, &QCoreApplication::quit);
        } else if (mode == "horror") {
            // 第十三阶段：恐怖蓝图 —— 按键 F（OnKeyDown 键F）→ 切换灯光 (Toggle Light) → 旋转物体 (Rotate Object)
            QTimer::singleShot(1200, this, [this]{
                consoleLog("恐怖游戏蓝图就位：交互动作 → 按键按下 (F) / 切换灯光 (Toggle Light) / 旋转物体 (Rotate Object)", false);
                fullA = addDef(libDef("按键按下 (F)"), 300, 180);
                fullB = addDef(libDef("切换灯光 (Toggle Light)"), 760, 220);
                evR   = addDef(libDef("旋转物体 (Rotate Object)"), 760, 460);
            });
            QTimer::singleShot(2200, this, [this]{
                canvas->connectPorts(fullA, 0, fullB, 0);      // 按键 F Flow → Toggle Light
                canvas->connectPorts(fullB, 0, evR, 0);        // Toggle Light → Rotate Object
                consoleLog("连线成功：按键 F → 切换灯光 → 旋转物体（游戏里按 F 手电筒亮/灭并触发开门旋转）", false);
                canvas->selected = fullB;
                refreshProps(fullB);
            });
            QTimer::singleShot(3400, this, [this]{ lib->expandAll(); });
            QTimer::singleShot(4400, this, [shot]{ grabFullscreen(shot); });
            QTimer::singleShot(5400, qApp, &QCoreApplication::quit);
        }
    }

    QString socketFile, projectRoot;
    struct { std::vector<SNode> nodes; std::vector<SLink> links; } graph;
    int canvas_nextId = 2;
    NodeCanvas *canvas = nullptr;
    QTreeWidget *lib = nullptr;
    QPlainTextEdit *console = nullptr;
    QLabel *nodeNameLab = nullptr;
    QComboBox *targetCombo = nullptr;
    QComboBox *audioCombo = nullptr;
    QVBoxLayout *propLay = nullptr;
    QLabel *crumbLab = nullptr;
    QPushButton *backBtn = nullptr;
    int fullA = -1, fullB = -1, evR = -1, evC = -1, evF = -1, evP = -1, evD = -1;
    QMessageBox *errBox = nullptr;
};

#include "VNSocketEditor.moc"

static int runApp(QApplication &app, int argc, char **argv) {
    app.setApplicationName("VNSocketEditor");

    if (argc < 2) {
        qInfo() << "用法：VNSocketEditor <插座路径.socket>";
        return 1;
    }
    QString path = argv[1];
    if (!QFile(path).exists()) {
        qInfo() << QString("错误：插座文件不存在：%1").arg(path);
        return 1;
    }
    SocketLabWindow w(path);
    w.show();
    return app.exec();
}

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNSocketEditor", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
