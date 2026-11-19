#pragma once
// ============ 音频服务（引擎底层真实播放） ============
// BGM：QMediaPlayer + QAudioOutput（可循环、可停止）
// SFX：QSoundEffect（低延迟一次性音效）
// 路径解析：相对路径 → <项目>/assets/ 下查找
#include <QString>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSoundEffect>
#include <QDebug>

class AudioService {
public:
    static AudioService &instance() {
        static AudioService s;
        return s;
    }
    static void setProjectPath(const QString &p) { instance().projRoot = p; }

    // 相对路径 → 项目 assets 下绝对路径
    QString resolve(const QString &file) const {
        if (QFile(file).exists()) return file;
        QString cand = projRoot + "/assets/" + file;
        if (QFile(cand).exists()) return cand;
        return file;   // 交给底层报错
    }

    void playBGM(const QString &file) {
        QString p = resolve(file);
        if (!QFile(p).exists()) { qWarning() << QString("[Audio] PlayBGM：找不到音频文件 %1").arg(file); return; }
        ensureBgm();
        bgmPlayer->setSource(QUrl::fromLocalFile(p));
        bgmPlayer->loops();  // 触发循环设置（下方 QMediaPlayer::setLoops 已设置）
        bgmPlayer->play();
        qInfo() << QString("[Audio] BGM 播放：%1").arg(QFileInfo(p).fileName());
    }
    void stopBGM() {
        ensureBgm();
        bgmPlayer->stop();
        qInfo() << "[Audio] BGM 已停止";
    }
    void playSFX(const QString &file) {
        QString p = resolve(file);
        if (!QFile(p).exists()) { qWarning() << QString("[Audio] PlaySFX：找不到音频文件 %1").arg(file); return; }
        if (!sfx) {
            sfx = new QSoundEffect;
            sfx->setVolume(0.8f);
        }
        sfx->setSource(QUrl::fromLocalFile(p));
        sfx->play();
        qInfo() << QString("[Audio] SFX 播放：%1").arg(QFileInfo(p).fileName());
    }

private:
    void ensureBgm() {
        if (bgmPlayer) return;
        bgmPlayer = new QMediaPlayer;
        bgmOutput = new QAudioOutput;
        bgmOutput->setVolume(0.8f);
        bgmPlayer->setAudioOutput(bgmOutput);
        bgmPlayer->setLoops(QMediaPlayer::Infinite);
    }
    QString projRoot;
    QMediaPlayer *bgmPlayer = nullptr;
    QAudioOutput *bgmOutput = nullptr;
    QSoundEffect *sfx = nullptr;
};
