// VNAudioPlayer —— 第八个程序：极简音频测试器。
// 拖入 .mp3/.wav → QMediaPlayer 真实解析（显示总时长；损坏文件弹窗报错）
// 播放/暂停 + 音量滑动条 + 进度条（可拖动跳转）
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QStyle>
#include <QDebug>

// 拖拽槽位：接受 mp3/wav，双击也可打开文件对话框
class DropSlot : public QLabel {
    Q_OBJECT
public:
    explicit DropSlot(QWidget *parent = nullptr) : QLabel(parent) {
        setAcceptDrops(true);
        setAlignment(Qt::AlignCenter);
        setText("将 .mp3 / .wav 拖到这里\n\n（双击也可以选择文件）");
        setStyleSheet("QLabel{border:2px dashed #5a6a7a; border-radius:12px;"
                      "background:#1d232c; color:#8fa3b8; font-size:16px; padding:24px;}");
        setMinimumHeight(170);
    }
    QString file;
signals:
    void fileDropped(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) { e->acceptProposedAction(); setStyleSheet(
            "QLabel{border:2px dashed #7fdcff; border-radius:12px; background:#223040; color:#7fdcff; font-size:16px; padding:24px;}"); }
    }
    void dragLeaveEvent(QDragLeaveEvent *) override { resetStyle(); }
    void dropEvent(QDropEvent *e) override {
        resetStyle();
        for (const QUrl &u : e->mimeData()->urls()) {
            QString p = u.toLocalFile();
            if (p.endsWith(".mp3", Qt::CaseInsensitive) || p.endsWith(".wav", Qt::CaseInsensitive)
                || p.endsWith(".ogg", Qt::CaseInsensitive)) {
                file = p; emit fileDropped(p); return;
            }
        }
        QMessageBox::warning(this, "格式不支持", "只支持 .mp3 / .wav / .ogg 音频文件");
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {
        QString p = QFileDialog::getOpenFileName(this, "选择音频", QString(),
                                                 "音频文件 (*.mp3 *.wav *.ogg)");
        if (!p.isEmpty()) { file = p; emit fileDropped(p); }
    }
private:
    void resetStyle() {
        setStyleSheet("QLabel{border:2px dashed #5a6a7a; border-radius:12px;"
                      "background:#1d232c; color:#8fa3b8; font-size:16px; padding:24px;}");
    }
};

class AudioPlayerWindow : public QMainWindow {
    Q_OBJECT
public:
    AudioPlayerWindow() {
        setWindowTitle("VNAudioPlayer — 音频测试器");
        resize(680, 460);

        auto *central = new QWidget;
        auto *v = new QVBoxLayout(central);
        v->setContentsMargins(24, 24, 24, 24);
        v->setSpacing(16);

        slot = new DropSlot;
        v->addWidget(slot);

        fileLabel = new QLabel("未加载音频");
        fileLabel->setStyleSheet("color:#c9d4e4; font-size:13px;");
        v->addWidget(fileLabel);

        timeLabel = new QLabel("--:-- / --:--");
        timeLabel->setAlignment(Qt::AlignCenter);
        timeLabel->setStyleSheet("font-size:20px; font-weight:bold; color:#7fdcff;");
        v->addWidget(timeLabel);

        progress = new QSlider(Qt::Horizontal);
        progress->setRange(0, 1000);
        v->addWidget(progress);

        auto *h = new QHBoxLayout;
        btnPlay = new QPushButton("播放");
        btnPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        btnPlay->setMinimumHeight(42);
        h->addWidget(btnPlay);
        h->addWidget(new QLabel("音量"));
        volume = new QSlider(Qt::Horizontal);
        volume->setRange(0, 100);
        volume->setValue(80);
        volume->setToolTip("音量");
        h->addWidget(volume, 1);
        v->addLayout(h);

        auto *tip = new QLabel("音频经 QMediaPlayer 真实解析；损坏或格式不支持会弹窗报错。");
        tip->setStyleSheet("color:#5a6a7a; font-size:11px;");
        v->addWidget(tip);
        setCentralWidget(central);

        audio = new QAudioOutput;
        player = new QMediaPlayer;
        player->setAudioOutput(audio);
        audio->setVolume(0.8f);
        volume->setValue(80);

        connect(slot, &DropSlot::fileDropped, this, &AudioPlayerWindow::loadFile);
        connect(btnPlay, &QPushButton::clicked, this, [this]{
            if (player->source().isEmpty()) return;
            if (player->playbackState() == QMediaPlayer::PlayingState) {
                player->pause();
                btnPlay->setText("播放");
                btnPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            } else {
                player->play();
                btnPlay->setText("暂停");
                btnPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            }
        });
        connect(volume, &QSlider::valueChanged, this, [this](int v){ audio->setVolume(v / 100.0f); });
        connect(progress, &QSlider::sliderMoved, this, [this](int v){
            if (player->duration() > 0) player->setPosition(qint64(v) * player->duration() / 1000);
        });
        connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
            if (!progress->isSliderDown() && player->duration() > 0)
                progress->setValue(int(pos * 1000 / player->duration()));
            timeLabel->setText(QString("%1 / %2").arg(fmt(pos)).arg(fmt(player->duration())));
        });
        connect(player, &QMediaPlayer::durationChanged, this, [this](qint64 d){
            qInfo() << QString("[Audio] 解析完成：总时长 %1").arg(fmt(d));
        });
        connect(player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg){
            QMessageBox::critical(this, "音频文件损坏或格式不支持",
                QString("QMediaPlayer 无法解析该文件：\n%1\n\n%2").arg(player->source().toString(), msg));
        });
        connect(player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st){
            if (st == QMediaPlayer::InvalidMedia)
                QMessageBox::critical(this, "音频文件损坏或格式不支持",
                    QString("QMediaPlayer 无法解析该文件：\n%1").arg(player->source().toString()));
        });
    }

    void loadFile(const QString &path) {
        qInfo() << QString("[Audio] 加载音频：%1").arg(path);
        fileLabel->setText(QString("已加载：%1").arg(QFileInfo(path).fileName()));
        player->setSource(QUrl::fromLocalFile(path));
        player->play();
        btnPlay->setText("暂停");
        btnPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    }
    QString loadedFile() const { return slot->file; }

    static QString fmt(qint64 ms) {
        if (ms < 0) ms = 0;
        int s = int(ms / 1000);
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }

private:
    DropSlot *slot = nullptr;
    QLabel *fileLabel = nullptr, *timeLabel = nullptr;
    QPushButton *btnPlay = nullptr;
    QSlider *volume = nullptr, *progress = nullptr;
    QMediaPlayer *player = nullptr;
    QAudioOutput *audio = nullptr;
};

static int runApp(QApplication &app, int argc, char **argv) {
    

    AudioPlayerWindow win;
    win.show();

    QString autoMode = qEnvironmentVariable("AUDIO_AUTO_MODE");
    QString shot = qEnvironmentVariable("AUDIO_SHOT");
    QString file = qEnvironmentVariable("AUDIO_FILE");

    if (!autoMode.isEmpty() && !shot.isEmpty()) {
        if (!file.isEmpty()) QTimer::singleShot(600, &win, [&win, file]{ win.loadFile(file); });
        QTimer::singleShot(2800, &app, [shot]{
            if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
        });
        QTimer::singleShot(3800, &app, []{ QApplication::quit(); });
    }
    return app.exec();
}
#include "VNAudioPlayer.moc"

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNAudioPlayer", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
