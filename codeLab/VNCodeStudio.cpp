// ============================================================
// VNCodeStudio — 引擎生态的微型 VS Code（独立脚本编辑器）
// 多标签页 + 行号 + Lua 语法高亮 + Ctrl+S 保存 + QProcess 测试运行
// 控制台实时捕获 stdout / stderr，错误信息标红显示行号
// ============================================================
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QMainWindow>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
#include <QRegularExpression>
#include <QPainter>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QProcess>
#include <QSplitter>
#include <QVBoxLayout>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QScreen>
#include <QShortcut>
#include <QFontDatabase>
#include <QKeyEvent>
#include <vector>

// ================= 拍摄钩子 =================
static QString g_shotPath;
static void shoot() {
    if (g_shotPath.isEmpty()) return;
    QScreen *s = QGuiApplication::primaryScreen();
    if (s) s->grabWindow(0).save(g_shotPath);
}

// ================= Lua 语法高亮（关键字蓝 / 字符串绿 / 注释灰 / 数字橙） =================
struct HighlightRule {
    QRegularExpression pattern;
    QTextCharFormat format;
};

class LuaHighlighter : public QSyntaxHighlighter {
public:
    explicit LuaHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {
        // 关键字：蓝色加粗
        QTextCharFormat kw;
        kw.setForeground(QColor("#5ea1ff"));
        kw.setFontWeight(QFont::Bold);
        QStringList kws = {
            "and","break","do","else","elseif","end","false","for","function","goto","if",
            "in","local","nil","not","or","repeat","return","then","true","until","while" };
        QStringList pats;
        for (const QString &k : kws) pats << "\\b" + k + "\\b";
        rules.push_back({ QRegularExpression(pats.join("|")), kw });

        // 字符串：绿色
        QTextCharFormat st;
        st.setForeground(QColor("#7ce38b"));
        rules.push_back({ QRegularExpression("\"[^\"\\n]*\"|'[^'\\n]*'"), st });

        // 数字：橙色
        QTextCharFormat nm;
        nm.setForeground(QColor("#ffb86c"));
        rules.push_back({ QRegularExpression("\\b(0[xX][0-9a-fA-F]+|\\d+\\.?\\d*([eE][-+]?\\d+)?)\\b"), nm });

        // 引擎 API：淡黄高亮
        QTextCharFormat api;
        api.setForeground(QColor("#ffe08a"));
        rules.push_back({ QRegularExpression("\\b(self|Start|Update|Move|Rotate|GetPosition|SetColor|GetName|print)\\b"), api });

        // 单行注释：灰色斜体
        QTextCharFormat cm;
        cm.setForeground(QColor("#8a8f98"));
        cm.setFontItalic(true);
        ruleComment = { QRegularExpression("--[^[].*$|--\\[\\[.*$"), cm };

        // 多行注释 --[[ ]] 起止
        commentStartExp = QRegularExpression("--\\[\\[");
        commentEndExp = QRegularExpression("\\]\\]");
        commentFmt = cm;
    }

protected:
    void highlightBlock(const QString &text) override {
        setFormat(0, text.length(), ruleComment.format);
        for (const HighlightRule &r : rules) {
            auto it = r.pattern.globalMatch(text);
            while (it.hasNext()) {
                auto m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.format);
            }
        }
        // 注释里还原字符串等：用注释起止单独覆盖
        auto it = ruleComment.pattern.globalMatch(text);
        while (it.hasNext()) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), ruleComment.format); }

        setCurrentBlockState(0);
        int startIndex = 0;
        if (previousBlockState() != 1)
            startIndex = text.indexOf(commentStartExp);
        while (startIndex >= 0) {
            auto endMatch = commentEndExp.match(text, startIndex);
            int commentLen;
            if (!endMatch.hasMatch()) {
                setCurrentBlockState(1);
                commentLen = text.length() - startIndex;
            } else {
                commentLen = endMatch.capturedEnd() - startIndex;
            }
            setFormat(startIndex, commentLen, commentFmt);
            startIndex = text.indexOf(commentStartExp, startIndex + commentLen);
        }
    }

private:
    std::vector<HighlightRule> rules;
    HighlightRule ruleComment;
    QRegularExpression commentStartExp, commentEndExp;
    QTextCharFormat commentFmt;
};

// ================= 带行号的代码编辑区 =================
class CodeEditor;
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *e);
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;
private:
    CodeEditor *ed;
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *p = nullptr) : QPlainTextEdit(p) {
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        QFont f = font(); f.setPointSize(12); setFont(f);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
        setStyleSheet("QPlainTextEdit{background:#1b1f27;color:#e6e6e6;border:none;}"
                      "QPlainTextEdit::selection{background:#314365;}");
        area = new LineNumberArea(this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int){ updateArea(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy){ updateArea(r, dy); });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]{
            auto c = textCursor();
            // 当前行高亮
            QList<QTextEdit::ExtraSelection> sels;
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(QColor("#252c3a"));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = c;
            sels.append(sel);
            setExtraSelections(sels);
            updateArea();
        });
        updateArea();
    }
    ~CodeEditor() override { if (area) area->setParent(nullptr); }

    QString filePath;   // 本标签对应的 .lua 文件

    int lineNumberWidth() const {
        int d = 1;
        for (int n = blockCount(); n >= 10; n /= 10) ++d;
        return 14 + fontMetrics().horizontalAdvance('9') * (d + 1);
    }
    void paintLineNumbers(QWidget *w) {
        QPainter p(w);
        p.fillRect(w->rect(), QColor("#161a22"));
        p.setPen(QColor("#5c6470"));
        QTextBlock block = firstVisibleBlock();
        int n = block.blockNumber();
        int top = qint64(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + int(blockBoundingRect(block).height());
        while (block.isValid() && top <= w->height()) {
            if (block.isVisible()) {
                p.drawText(0, top, w->width() - 6, fontMetrics().height(), Qt::AlignRight,
                           QString::number(n + 1));
            }
            block = block.next(); ++n;
            top = bottom;
            bottom = top + int(blockBoundingRect(block).height());
        }
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QPlainTextEdit::resizeEvent(e);
        QRect cr = contentsRect();
        area->setGeometry(QRect(cr.left(), cr.top(), lineNumberWidth(), cr.height()));
    }
    // Ctrl+S 直接接住（编辑器内部也响应）
    void keyPressEvent(QKeyEvent *e) override {
        if (e->matches(QKeySequence::Save)) { emit saveRequested(); e->accept(); return; }
        QPlainTextEdit::keyPressEvent(e);
    }

private:
    void updateArea(const QRect & = QRect(), int = 0) {
        setViewportMargins(lineNumberWidth(), 0, 0, 0);
        area->setFixedWidth(lineNumberWidth());
        area->update();
    }
    LineNumberArea *area = nullptr;

public:
    signals:
        void saveRequested();
};

// ---- 行号区实现（需 CodeEditor 完整定义）----
LineNumberArea::LineNumberArea(CodeEditor *e) : QWidget(e), ed(e) {}
void LineNumberArea::paintEvent(QPaintEvent *) { ed->paintLineNumbers(this); }
QSize LineNumberArea::sizeHint() const { return QSize(ed->lineNumberWidth(), 0); }

// ================= 主窗口 =================
class Studio : public QMainWindow {
    Q_OBJECT
public:
    explicit Studio(const QString &initFile) {
        setWindowTitle("VNCodeStudio — Lua 脚本工作室");
        resize(1280, 820);
        setStyleSheet("QMainWindow{background:#12151b;} "
                      "QMenuBar{background:#1a1e26;color:#d8dde6;} "
                      "QMenuBar::item:selected{background:#2a3242;} "
                      "QMenu{background:#1a1e26;color:#d8dde6;} QMenu::item:selected{background:#2a3242;} "
                      "QTabWidget::pane{border:none;} "
                      "QTabBar::tab{background:#1a1e26;color:#9aa3b0;padding:7px 16px;border-top-left-radius:6px;border-top-right-radius:6px;}"
                      "QTabBar::tab:selected{background:#252c3a;color:#7ea6ff;}"
                      "QStatusBar{background:#161a22;color:#8a93a3;} "
                      "QLabel#consoleTitle{color:#7ea6ff;padding:2px 8px;font-weight:bold;}");
        QFont menuFont = font(); menuFont.setPointSize(12); setFont(menuFont);

        // ---- 中间：多标签代码区 ----
        tabs = new QTabWidget;
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int i){
            QWidget *w = tabs->widget(i); tabs->removeTab(i); delete w; });

        // ---- 底部：控制台 ----
        console = new QPlainTextEdit;
        console->setReadOnly(true);
        console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        QFont cf = console->font(); cf.setPointSize(11); console->setFont(cf);
        console->setStyleSheet("QPlainTextEdit{background:#0e1116;color:#cfd6e0;border:none;}"
                               "QPlainTextEdit::selection{background:#314365;}");
        console->setMaximumBlockCount(2000);
        consoleTitle = new QLabel("Console 控制台");
        consoleTitle->setObjectName("consoleTitle");

        consoleWrap = new QWidget;
        auto *cl = new QVBoxLayout(consoleWrap);
        cl->setContentsMargins(0, 2, 0, 0); cl->setSpacing(0);
        cl->addWidget(consoleTitle);
        cl->addWidget(console, 1);
        console->setFixedHeight(240);

        // ---- 分割：上代码下控制台 ----
        split = new QSplitter(Qt::Vertical);
        split->addWidget(tabs);
        split->addWidget(consoleWrap);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);
        setCentralWidget(split);

        // ---- 顶部菜单栏 ----
        QMenu *mFile = menuBar()->addMenu("文件");
        QAction *aOpen = mFile->addAction("打开脚本…");
        connect(aOpen, &QAction::triggered, this, [this]{
            QString p = QFileDialog::getOpenFileName(this, "打开 Lua 脚本", QString(), "Lua (*.lua)");
            if (!p.isEmpty()) openFile(p);
        });
        QAction *aSave = mFile->addAction("保存");
        aSave->setShortcut(QKeySequence::Save);          // Ctrl+S
        connect(aSave, &QAction::triggered, this, &Studio::saveCurrent);
        QAction *aClose = mFile->addAction("关闭标签");
        connect(aClose, &QAction::triggered, this, [this]{
            int i = tabs->currentIndex(); if (i >= 0) { QWidget *w = tabs->widget(i); tabs->removeTab(i); delete w; } });

        QMenu *mRun = menuBar()->addMenu("运行");
        QAction *aRun = mRun->addAction("测试运行 (Run)");
        aRun->setShortcut(QKeySequence("Ctrl+R"));
        connect(aRun, &QAction::triggered, this, &Studio::runCurrent);
        QAction *aClear = mRun->addAction("清空控制台");
        connect(aClear, &QAction::triggered, this, [this]{ console->clear(); });

        QMenu *mHelp = menuBar()->addMenu("帮助");
        mHelp->addAction("关于 VNCodeStudio", this, [this]{
            QMessageBox::information(this, "关于", "VNCodeStudio — PhysicsEngine3D 引擎生态的微型 VS Code\n\n多标签 · Lua 语法高亮 · Ctrl+S 保存 · QProcess 测试运行 · 控制台实时输出");
        });

        // ---- 底部状态栏 ----
        stat = new QLabel("就绪");
        statusBar()->addWidget(stat);
        posInfo = new QLabel("Ln 1, Col 1");
        statusBar()->addPermanentWidget(posInfo);
        connect(console, &QPlainTextEdit::cursorPositionChanged, this, [this]{
            auto c = console->textCursor();
            posInfo->setText(QString("Console Ln %1, Col %2").arg(c.blockNumber()+1).arg(c.columnNumber()+1));
        });

        consoleMsg("VNCodeStudio 就绪。Ctrl+S 保存 · Ctrl+R 测试运行 · 解释器：/usr/bin/lua5.4");

        if (auto *scr = QGuiApplication::primaryScreen()) {
            QRect av = scr->availableGeometry();
            move(qMax(0,(av.width()-width())/2), qMax(0,(av.height()-height())/2));
        }
        if (!initFile.isEmpty()) openFile(initFile);

        // ---- 演示模式 ----
        mode = qEnvironmentVariable("CODE_AUTO_MODE");
        if (!mode.isEmpty()) {
            QTimer::singleShot(4000, this, &Studio::autoQuit);
            if (mode == "open") {
                demoOpen();
                QTimer::singleShot(2200, this, &Studio::shootNow);
            } else if (mode == "save") {
                demoSave();
            } else if (mode == "run") {
                demoRun();
            } else if (mode == "runok") {
                demoRunOk();
            }
        }
    }

    // ---- 打开文件（多标签）----
    void openFile(const QString &path) {
        for (int i = 0; i < tabs->count(); ++i) {                 // 已打开则切换
            auto *e = qobject_cast<CodeEditor*>(tabs->widget(i));
            if (e && e->filePath == path) { tabs->setCurrentIndex(i); return; }
        }
        QFile f(path);
        auto *ed = new CodeEditor;
        ed->filePath = path;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            ed->setPlainText(QString::fromUtf8(f.readAll()));
        new LuaHighlighter(ed->document());
        connect(ed, &CodeEditor::saveRequested, this, &Studio::saveCurrent);
        int idx = tabs->addTab(ed, QFileInfo(path).fileName());
        tabs->setTabToolTip(idx, path);
        tabs->setCurrentIndex(idx);
        stat->setText(QString("已打开：%1").arg(QFileInfo(path).fileName()));
    }

public slots:
    // ---- 保存：真实覆写 + 提示 ----
    void saveCurrent() {
        auto *ed = qobject_cast<CodeEditor*>(tabs->currentWidget());
        if (!ed || ed->filePath.isEmpty()) { stat->setText("没有可保存的标签"); return; }
        QFile f(ed->filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::critical(this, "保存失败", "无法写入文件：" + ed->filePath);
            return;
        }
        f.write(ed->toPlainText().toUtf8());
        f.close();
        stat->setText(QString("保存成功：%1").arg(QFileInfo(ed->filePath).fileName()));
        consoleMsg(QString("已保存：%1").arg(QFileInfo(ed->filePath).fileName()));
        // 弹窗提示（演示模式下自动关闭，避免阻塞截图）
        auto *box = new QMessageBox(QMessageBox::Information, "保存成功",
                                    QString("文件已真实覆写：\n%1").arg(ed->filePath),
                                    QMessageBox::Ok, this);
        if (!mode.isEmpty()) QTimer::singleShot(1400, box, &QMessageBox::close);
        box->exec();
        box->deleteLater();
    }

    // ---- 测试运行：QProcess 调 Lua 解释器，捕获 stdout/stderr ----
    void runCurrent() {
        auto *ed = qobject_cast<CodeEditor*>(tabs->currentWidget());
        if (!ed || ed->filePath.isEmpty()) { stat->setText("没有可运行的脚本"); return; }
        // 运行前先落盘，保证执行的是最新代码
        QFile f(ed->filePath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(ed->toPlainText().toUtf8()); f.close();
        }
        if (proc) { proc->kill(); proc->waitForFinished(500); proc->deleteLater(); }
        proc = new QProcess(this);
        connect(proc, &QProcess::readyReadStandardOutput, this, [this]{
            consoleMsg2(QString::fromUtf8(proc->readAllStandardOutput()), "#e6e6e6");
        });
        connect(proc, &QProcess::readyReadStandardError, this, [this]{
            consoleErr(QString::fromUtf8(proc->readAllStandardError()));   // stderr 标红
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus st){
            consoleMsg(code == 0 && st == QProcess::NormalExit
                       ? QString("运行结束：退出码 %1").arg(code)
                       : QString("运行结束：异常退出（退出码 %1）").arg(code));
            stat->setText("测试运行结束");
        });
        consoleMsg2(QString("$ lua5.4 \"%1\"").arg(QFileInfo(ed->filePath).fileName()), "#7ea6ff");
        stat->setText(QString("测试运行中：%1").arg(QFileInfo(ed->filePath).fileName()));
        proc->start("/usr/bin/lua5.4", { ed->filePath });
    }

private slots:
    void autoQuit() { QApplication::quit(); }
    void shootNow() { shoot(); }

private:
    QTabWidget *tabs = nullptr;
    QPlainTextEdit *console = nullptr;
    QWidget *consoleWrap = nullptr;
    QLabel *consoleTitle = nullptr, *stat = nullptr, *posInfo = nullptr;
    QSplitter *split = nullptr;
    QProcess *proc = nullptr;
    QString mode;

    void consoleMsg(const QString &s)  { consoleMsg2(s, "#9aa3b0"); }
    void consoleMsg2(const QString &s, const QString &color) {
        console->appendHtml(QString("<span style=\"color:%1\">%2</span>").arg(color, s.toHtmlEscaped().replace("\n", "<br>")));
    }
    void consoleErr(const QString &s) {   // 错误：红字
        console->appendHtml(QString("<span style=\"color:#ff5f56\"><b>%1</b></span>").arg(s.toHtmlEscaped().replace("\n", "<br>")));
    }

    // ---- 演示钩子 ----
    void demoOpen() {
        // 现场生成三个真实脚本（含一个带语法错误），三个标签同时打开
        QDir().mkpath(QDir::homePath() + "/Physics3DProjects/Demo火锅店项目/assets");
        QString dir = QDir::homePath() + "/Physics3DProjects/Demo火锅店项目/assets";
        auto write = [&dir](const QString &name, const QString &body) {
            QFile f(dir + "/" + name);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) f.write(body.toUtf8());
        };
        write("demo_ok.lua", "-- 引擎脚本：演示 print 输出\n"
               "local total = 0\n"
               "for i = 1, 5 do\n"
               "    total = total + i * i\n"
               "    print(\"第 \" .. i .. \" 项，累计 = \" .. total)\n"
               "end\n"
               "print(\"任务完成，总数 = \" .. total)\n");
        write("demo_broken.lua", "-- 故意写错的脚本：少了一个右括号\n"
               "print(\"开始执行……\")\n"
               "local hp = 100\n"
               "print(\"剩余血量 \" .. hp)\n"
               "print(\"这一行少了右括号…… \" .. hp\n");
        write("spin.lua", "-- 火锅转盘：让物体绕 Y 轴旋转\n"
               "function Start(self)\n"
               "    print(\"转盘启动！\")\n"
               "end\n"
               "function Update(self, dt)\n"
               "    self:Rotate(0, 120 * dt, 0)\n"
               "end\n");
        openFile(dir + "/spin.lua");
        openFile(dir + "/demo_ok.lua");
        openFile(dir + "/demo_broken.lua");
    }
    void demoSave() {
        demoOpen();
        // 在 demo_ok.lua 末尾追加一行再保存（走真实 Ctrl+S 管线）
        auto *ed = qobject_cast<CodeEditor*>(tabs->currentWidget());
        if (ed) {
            QTextCursor c = ed->textCursor();
            c.movePosition(QTextCursor::End);
            ed->setTextCursor(c);
            ed->insertPlainText("\n-- 灵光在 VNCodeStudio 里补的一行：print(\"保存成功！\")\n");
        }
        QTimer::singleShot(1200, this, &Studio::saveCurrent);   // 真实保存管线
        QTimer::singleShot(2100, this, &Studio::shootNow);
    }
    void demoRun() {
        demoOpen();
        tabs->setCurrentIndex(tabs->count() - 1);   // 选中带错的 demo_broken.lua
        QTimer::singleShot(1200, this, &Studio::runCurrent);
        QTimer::singleShot(2800, this, &Studio::shootNow);
    }
    void demoRunOk() {
        demoOpen();
        tabs->setCurrentIndex(1);                    // demo_ok.lua
        QTimer::singleShot(1200, this, &Studio::runCurrent);
        QTimer::singleShot(2600, this, &Studio::shootNow);
    }
};

#include "VNCodeStudio.moc"

static int runApp(QApplication &app, int argc, char **argv) {
    QApplication::setApplicationName("VNCodeStudio");
    g_shotPath = qEnvironmentVariable("CODE_SHOT");
    QString init;
    if (argc > 1) init = QString::fromLocal8Bit(argv[1]);
    Studio w(init);
    w.show();
    return app.exec();
}

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNCodeStudio", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
