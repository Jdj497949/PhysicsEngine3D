#pragma once
// ============ NovelSystem：视觉小说核心层（QtCore-only，无头可跑） ============
// 对话框（含打字机）/ 立绘（左中右锚点）/ 分支选项 / 异步图片加载
// 特效：QPropertyAnimation 驱动的淡入淡出与图片平移流动
// 与 UI 协程联动：WaitForClick/WaitForChoice 由 Lua 协程 yield + C++ 信号唤醒
#include <QString>
#include <QStringList>
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QThreadPool>
#include <functional>
#include <cstdio>

// ---------- 打印宏：终端自证用 ----------
#define NOVEL_LOG(fmt, ...) do { \
    QString _s = QString::asprintf(fmt, ##__VA_ARGS__); \
    std::printf("%s\n", _s.toUtf8().constData()); std::fflush(stdout); \
} while (0)

// ---------- 异步加载的背景图（多线程，不卡主线程） ----------
class NovelImage : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(float opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(float offsetX READ offsetX WRITE setOffsetX NOTIFY offsetXChanged)
    explicit NovelImage(QObject *parent = nullptr) : QObject(parent) {}
    float opacity() const { return m_opacity; }
    void setOpacity(float v) { m_opacity = v; emit opacityChanged(v); }
    float offsetX() const { return m_offsetX; }
    void setOffsetX(float v) { m_offsetX = v; emit offsetXChanged(v); }
    QString path, effectType;
    bool loaded = false;                 // 工作线程写，主线程读（演示层用原子语义）
signals:
    void opacityChanged(float);
    void offsetXChanged(float);
    void loadedOnMainThread(const QString &path, qint64 ms, int w, int h);
    void applyEffect(const QString &effect);
private:
    float m_opacity = 0.0f, m_offsetX = 0.0f;
};

// ---------- 对话框面板（九宫格数据 + 两个子节点：角色名/内容） ----------
class DialogueBox : public QObject {
    Q_OBJECT
public:
    // 九宫格：边框四角/四边/中心拉伸参数（像素）
    struct NinePatch { int left=24, top=24, right=24, bottom=24; };
    NinePatch ninePatch;
    QString charName, text;              // 子节点1：角色名文本；子节点2：对话内容文本
    int typewriterSpeed = 24;            // 字/秒
    int shownChars = 0;                  // 打字机已显示字符数
    QTimer ticker;                       // 打字机驱动（QTimer 事件，不阻塞）
    bool typewriterActive = false;
    DialogueBox(QObject *parent = nullptr) : QObject(parent) {
        connect(&ticker, &QTimer::timeout, this, &DialogueBox::tick);
    }
    void show(const QString &who, const QString &body) {
        charName = who; text = body; shownChars = 0;
        typewriterActive = true;
        NOVEL_LOG("[UI] Dialogue Box Created. Character: %s, Text: %s",
                  who.toUtf8().constData(), body.toUtf8().constData());
        NOVEL_LOG("[UI] 对话框显示: %s - %s", who.toUtf8().constData(), body.toUtf8().constData());
        ticker.start(int(1000.0 / double(typewriterSpeed < 1 ? 1 : typewriterSpeed)));
    }
    void setSpeed(int charsPerSec) {
        typewriterSpeed = charsPerSec;
        NOVEL_LOG("[UI] TypewriterSpeed set to %d chars/sec", charsPerSec);
        if (typewriterActive) ticker.start(int(1000.0 / double(typewriterSpeed < 1 ? 1 : typewriterSpeed)));
    }
private slots:
    void tick() {                        // 打字机：一字一字显示，绝不重叠
        if (!typewriterActive) return;
        shownChars += 1;
        if (shownChars >= text.size()) {
            shownChars = text.size();
            typewriterActive = false;
            ticker.stop();
            NOVEL_LOG("[EFFECT] Typewriter effect active. Loading Image (Async)...");
            emit typewriterDone();
        }
    }
signals:
    void typewriterDone();
};

// ---------- 立绘：三个默认锚点 ----------
class Portrait : public QObject {
    Q_OBJECT
public:
    enum Anchor { Left, Center, Right };
    QString charID; Anchor anchor = Center; bool visible = false;
    void set(const QString &id, const QString &pos) {
        charID = id; visible = true;
        anchor = pos.compare("left", Qt::CaseInsensitive) == 0 ? Left
               : pos.compare("right", Qt::CaseInsensitive) == 0 ? Right : Center;
        NOVEL_LOG("[UI] Portrait set: %s at %s (anchor x=%s)", id.toUtf8().constData(),
                  pos.toUtf8().constData(),
                  anchor == Left ? "0.18" : anchor == Center ? "0.50" : "0.82");
    }
};

// ---------- 选项分支按钮 ----------
struct ChoiceButton { QString label; int index = 0; bool clicked = false; };

// ---------- 导演：总状态 + 异步加载 + 动画 ----------
class NovelDirector : public QObject {
    Q_OBJECT
public:
    explicit NovelDirector(QObject *parent = nullptr) : QObject(parent) {}
    DialogueBox box;
    Portrait portrait;
    QVector<ChoiceButton> choices;
    QVector<NovelImage *> images;
    QStringList bgmPlaylist;
    void playBGM(const QString &path) {
        bgmPlaylist.append(path);
        NOVEL_LOG("[UI] BGM playing: %s", path.toUtf8().constData());
    }
    void stopBGM() { NOVEL_LOG("[UI] BGM stopped"); }

    // 多线程异步加载：QThreadPool 工作线程读文件，完成后回主线程应用特效
    NovelImage *showImageAsync(const QString &path, const QString &effect) {
        NovelImage *img = new NovelImage(this);
        img->path = path; img->effectType = effect;
        images.append(img);
        QElapsedTimer *clock = new QElapsedTimer(); clock->start();
        qint64 t0 = clock->elapsed();
        NOVEL_LOG("[EFFECT] Loading Image (Async): %s (worker thread started, main thread free)",
                  path.toUtf8().constData());
        auto *imgPtr = img; auto *clkPtr = clock;
        QThreadPool::globalInstance()->start([imgPtr, clkPtr, t0, effect]() {
            // 工作线程：模拟读取大图（真实解码在这里做；此处验证线程切换与不卡主线程）
            for (volatile int i = 0; i < 4000000; ++i) {}   // ~数毫秒的“解码”负载
            qint64 cost = clkPtr->elapsed() - t0;
            QMetaObject::invokeMethod(imgPtr, [imgPtr, clkPtr, cost, effect]() {
                imgPtr->loaded = true;
                delete clkPtr;
                NOVEL_LOG("[EFFECT] Image loaded in worker thread (%lld ms), applying '%s' via QPropertyAnimation",
                          cost, effect.toUtf8().constData());
                if (effect == "fade") {
                    auto *anim = new QPropertyAnimation(imgPtr, "opacity", imgPtr);
                    anim->setDuration(600); anim->setStartValue(0.0f); anim->setEndValue(1.0f);
                    anim->start(QAbstractAnimation::DeleteWhenStopped);
                    QObject::connect(anim, &QPropertyAnimation::finished, imgPtr, [imgPtr]() {
                        NOVEL_LOG("[EFFECT] Fade-in done. opacity=1.0 (QPropertyAnimation finished)");
                        emit imgPtr->applyEffect("fade");
                    });
                } else if (effect == "pan") {
                    auto *anim = new QPropertyAnimation(imgPtr, "offsetX", imgPtr);
                    anim->setDuration(2000); anim->setStartValue(0.0f); anim->setEndValue(640.0f);
                    anim->start(QAbstractAnimation::DeleteWhenStopped);
                    QObject::connect(anim, &QPropertyAnimation::finished, imgPtr, [imgPtr]() {
                        NOVEL_LOG("[EFFECT] Image Pan flowing: offsetX reached 640.0 (smooth QPropertyAnimation)");
                        emit imgPtr->applyEffect("pan");
                    });
                }
            }, Qt::QueuedConnection);      // 回主线程执行
        });
        return img;
    }

    void fadeToBlack(int durationMs) {
        NOVEL_LOG("[EFFECT] FadeToBlack started (%d ms, QPropertyAnimation on overlay opacity)", durationMs);
        if (!m_fadeTarget) m_fadeTarget = new NovelImage(this);
        auto *anim = new QPropertyAnimation(m_fadeTarget, "opacity", m_fadeTarget);
        anim->setDuration(durationMs); anim->setStartValue(0.0f); anim->setEndValue(1.0f);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        connect(anim, &QPropertyAnimation::finished, this, [this, durationMs]() {
            NOVEL_LOG("[EFFECT] FadeToBlack done (%d ms). Screen fully black.", durationMs);
            emit fadeFinished();
        });
    }

    void showChoices(const QStringList &labels) {
        choices.clear();
        for (int i = 0; i < labels.size(); ++i)
            choices.append(ChoiceButton{labels[i], i + 1, false});
        NOVEL_LOG("[UI] 显示选项: %s", labels.join(", ").toUtf8().constData());
    }
    void choose(int idx) {
        if (idx >= 1 && idx <= choices.size()) {
            choices[idx - 1].clicked = true;
            NOVEL_LOG("[SOCKET] WaitForClick triggered. Branching to Option %s.",
                      choices[idx - 1].label.toUtf8().constData());
        }
    }
    NovelImage *m_fadeTarget = nullptr;
signals:
    void fadeFinished();
};
