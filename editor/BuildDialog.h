#pragma once
// ============ 打包发布面板（构建 Build -> 打包发布） ============
// 表单控件：目标平台/产品名称/包名/应用头像/启动动图/鼠标指针/加密资源+密钥
// 「开始打包」：收集全部数据 -> build_config.json -> QProcess 调平台工具链 -> AES-256 加密 assets
// 注：header-only 包含类，无自定义信号，不用 Q_OBJECT（指针式 connect 全部照常工作）
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QProcess>
#include <QDebug>
#include <QCoreApplication>
#include "../common/Aes256.h"
#include "../common/BuildKit.h"
#include "BuildManager.h"

class BuildDialog : public QDialog {
public:
    explicit BuildDialog(const QString &projectPath, QWidget *parent = nullptr)
        : QDialog(parent), projectPath_(projectPath) {
        setWindowTitle("打包发布 (Build & Publish)");
        resize(560, 640);
        auto *lay = new QVBoxLayout(this);

        auto *title = new QLabel("打包发布 —— 一键构建游戏安装包");
        title->setStyleSheet("font-weight:bold; font-size:16px; color:#7fb3ff;");
        lay->addWidget(title);

        auto *form = new QFormLayout;
        // 1. 目标平台
        platform_ = new QComboBox;
        platform_->addItems({"Windows", "macOS", "Linux", "SteamOS", "Android", "iOS"});
        form->addRow("目标平台：", platform_);
        // 2. 产品名称
        productName_ = new QLineEdit("My Horror Game");
        form->addRow("产品名称：", productName_);
        // 3. 包名 (Bundle ID)
        bundleId_ = new QLineEdit("com.yourname.horror");
        bundleId_->setPlaceholderText("com.yourname.appname（Android / iOS 必填）");
        form->addRow("包名 (Bundle ID)：", bundleId_);
        // 4. 应用头像
        avatar_ = new QLineEdit;
        auto *avatarRow = new QHBoxLayout;
        avatarRow->addWidget(avatar_);
        auto *avatarBtn = new QPushButton("浏览…");
        connect(avatarBtn, &QPushButton::clicked, this, [this]{
            QString p = QFileDialog::getOpenFileName(this, "选择应用头像", QString(),
                                                     "图像 (*.png *.ico)");
            if (!p.isEmpty()) avatar_->setText(p);
        });
        avatarRow->addWidget(avatarBtn);
        form->addRow("应用头像：", avatarRow);
        // 5. 启动动图 (Splash)
        splash_ = new QLineEdit;
        auto *splashRow = new QHBoxLayout;
        splashRow->addWidget(splash_);
        auto *splashBtn = new QPushButton("浏览…");
        connect(splashBtn, &QPushButton::clicked, this, [this]{
            QStringList ps = QFileDialog::getOpenFileNames(this, "选择启动动图（可多选）", QString(),
                                                           "图像 (*.png *.jpg)");
            if (!ps.isEmpty()) splash_->setText(ps.join(";"));
        });
        splashRow->addWidget(splashBtn);
        form->addRow("启动动图 (Splash)：", splashRow);
        // 6. 自定义鼠标指针
        cursor_ = new QLineEdit;
        auto *cursorRow = new QHBoxLayout;
        cursorRow->addWidget(cursor_);
        auto *cursorBtn = new QPushButton("浏览…");
        connect(cursorBtn, &QPushButton::clicked, this, [this]{
            QString p = QFileDialog::getOpenFileName(this, "选择鼠标指针", QString(),
                                                     "光标 (*.cur *.png)");
            if (!p.isEmpty()) cursor_->setText(p);
        });
        cursorRow->addWidget(cursorBtn);
        form->addRow("自定义鼠标指针：", cursorRow);
        // 7. 加密资源
        encrypt_ = new QCheckBox("加密资源（AES-256 打包为 .pak）");
        connect(encrypt_, &QCheckBox::toggled, this, &BuildDialog::onEncryptToggled);
        form->addRow(QString(), encrypt_);
        // 加密密钥（勾选后出现）
        keyLabel_ = new QLabel("加密密钥：");
        keyEdit_ = new QLineEdit;
        keyEdit_->setPlaceholderText("AES-256 密钥（32 字节，不足补 0）");
        keyEdit_->setEchoMode(QLineEdit::Password);
        form->addRow(keyLabel_, keyEdit_);
        keyLabel_->hide(); keyEdit_->hide();

        // 7.5 专业级打包套件（Pro 暴力增强）
        auto *proTitle = new QLabel("专业级打包（Pro 套件）");
        proTitle->setStyleSheet("font-weight:bold; color:#e0a83c;");
        lay->addWidget(proTitle);
        auto *proRow1 = new QHBoxLayout;
        preflight_ = new QCheckBox("资源预检");
        preflight_->setChecked(true);
        backup_ = new QCheckBox("自动备份旧包（保留 3 份）");
        backup_->setChecked(true);
        proRow1->addWidget(preflight_); proRow1->addWidget(backup_);
        auto *proRow2 = new QHBoxLayout;
        manifest_ = new QCheckBox("构建清单（MD5 对账单）");
        manifest_->setChecked(true);
        verify_ = new QCheckBox("完整性校验（回读验密）");
        verify_->setChecked(true);
        proRow2->addWidget(manifest_); proRow2->addWidget(verify_);
        lay->addLayout(proRow1); lay->addLayout(proRow2);
        lay->addLayout(form);

        // 打包输出区（编译日志实时滚动）
        buildLog_ = new QLabel("配置完成后点击【开始打包】。");
        buildLog_->setStyleSheet("color:#9aa8bd; background:#15181e; border:1px solid #3b4a61;"
                                 "border-radius:5px; padding:6px;");
        buildLog_->setWordWrap(true);
        lay->addWidget(buildLog_);

        // 8. 底部按钮
        auto *btnRow = new QHBoxLayout;
        startBtn_ = new QPushButton("开始打包");
        startBtn_->setStyleSheet("background:#2d7d46; color:white; font-weight:bold; padding:6px 18px; border-radius:4px;");
        cancelBtn_ = new QPushButton("取消");
        btnRow->addStretch();
        btnRow->addWidget(startBtn_);
        btnRow->addWidget(cancelBtn_);
        lay->addLayout(btnRow);
        connect(startBtn_, &QPushButton::clicked, this, &BuildDialog::startBuild);
        connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    }

    // 供演示钩子：勾选加密并填默认密钥
    void demoCheckEncrypt() { encrypt_->setChecked(true); keyEdit_->setText("vn-horror-2026"); }
    // 供演示钩子：平台/名称/包名快速填表
    void demoFill(const QString &platform, const QString &name, const QString &bundle) {
        platform_->setCurrentText(platform);
        productName_->setText(name);
        bundleId_->setText(bundle);
    }
    // 供演示钩子：直接填应用头像路径
    void demoSetAvatar(const QString &p) { avatar_->setText(p); }
    // 供演示钩子：真实执行打包流程
    void demoStartBuild() { startBuild(); }

private:
    void onEncryptToggled(bool on) {
        keyLabel_->setVisible(on);
        keyEdit_->setVisible(on);
        qInfo().noquote() << QString("[Build] 加密资源%1%2").arg(on ? "已勾选" : "已取消")
            .arg(on ? QString("：下方已出现【加密密钥】输入框（AES-256 -> .pak）") : "");
    }

    void startBuild() {
        // ---- 1. 收集表单数据 -> build_config.json ----
        QJsonObject cfg;
        cfg["platform"] = platform_->currentText();
        cfg["productName"] = productName_->text();
        cfg["bundleId"] = bundleId_->text();
        cfg["avatar"] = avatar_->text();
        cfg["splash"] = splash_->text();
        cfg["cursor"] = cursor_->text();
        cfg["encryptAssets"] = encrypt_->isChecked();
        if (encrypt_->isChecked()) cfg["encryptKey"] = keyEdit_->text();
        cfg["buildTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        QFile f(projectPath_ + "/build_config.json");
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(cfg).toJson()); f.close(); }
        qInfo().noquote() << QString("[Build] 配置已写入 build_config.json（平台 %1 / 产品 %2）")
            .arg(platform_->currentText(), productName_->text());

        QString platform = platform_->currentText();
        // ---- 专业套件 0：构建号自增 ----
        QStringList proLog;
        const int buildNo = BuildKit::bumpBuildNo(projectPath_, proLog);
        for (const QString &l : proLog) qInfo().noquote() << l;
        proLog.clear();
        // ---- 专业套件 1：资源预检 ----
        QJsonObject preflightRep;
        if (preflight_->isChecked())
            preflightRep = BuildKit::preflight(projectPath_ + "/assets", proLog);
        else
            qInfo() << "[预检] 已跳过（未勾选资源预检）";
        for (const QString &l : proLog) qInfo().noquote() << l;
        proLog.clear();
        // ---- 2. 平台工具链（QProcess 真实调用）----
        QString bin = QCoreApplication::applicationDirPath();
        QString buildDir = projectPath_ + "/build";
        QDir().mkpath(buildDir);
        QString product = productName_->text().isEmpty() ? "Game" : productName_->text();
        qInfo().noquote() << QString("[Build] Building for %1...").arg(platform);
        // 启动器源码模板（打包系统自带，编译即得可运行产物）
        QString stubSrc = bin + "/../common/build_stubs/game_horror_win.c";
        QString exePath;
        if (platform == "Windows") {
            exePath = buildDir + "/" + product + ".exe";
            qInfo() << "[Build] 工具链：MinGW-w64 交叉编译 + windres（图标内嵌 .exe）";
            // 头像 -> 多尺寸 .ico -> windres 资源 -> 编进 .exe
            bool haveIcon = false;
            QString icoPath = buildDir + "/app.ico";
            if (QFileInfo::exists(avatar_->text()))
                haveIcon = QProcess::execute("sh", {"-c",
                    QString("convert '%1' -define icon:auto-resize=256,64,48,32,16 '%2'")
                        .arg(avatar_->text(), icoPath)}) == 0 && QFileInfo::exists(icoPath);
            QString resObj;
            if (haveIcon) {
                QFile rc(buildDir + "/app.rc");
                if (rc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    rc.write(QString("1 ICON \"%1\"\n").arg(icoPath).toUtf8());
                    rc.close();
                }
                if (QProcess::execute("sh", {"-c",
                        QString("cd '%1' && x86_64-w64-mingw32-windres app.rc -O coff -o app_res.o").arg(buildDir)}) == 0)
                    resObj = "app_res.o";
                else
                    qInfo() << "[Build] windres 资源编译失败，退回默认图标";
            }
            if (QFileInfo::exists(stubSrc)) {
                QProcess gcc;
                gcc.start("sh", {"-c", QString("cd '%1' && x86_64-w64-mingw32-gcc '%2' %3 -o '%4' -mwindows -static -lgdi32 -luser32 -lm -O2 2>&1")
                    .arg(buildDir, stubSrc, resObj, exePath)});
                gcc.waitForFinished(120000);
                QString gout = QString::fromUtf8(gcc.readAllStandardOutput()).trimmed();
                if (!gout.isEmpty())
                    qInfo().noquote() << QString("[Build] MinGW：%1").arg(gout.replace('\n', " | "));
                if (QFileInfo::exists(exePath)) {
                    qint64 sz = QFileInfo(exePath).size();
                    qInfo().noquote() << QString("[Build] Windows 构建完成：%1（%2 KB，应用头像%3）")
                        .arg(exePath).arg(sz / 1024).arg(haveIcon ? "已内嵌为图标" : "未内嵌（未选头像）");
                } else {
                    qInfo() << "[Build] Windows 交叉编译失败（缺少 MinGW-w64？已回退仅日志）";
                }
            } else {
                qInfo() << "[Build] 未找到启动器源码模板（build_stubs）";
            }
        } else if (platform == "Linux" || platform == "SteamOS") {
            exePath = buildDir + "/" + product;
            qInfo() << QString("[Build] 工具链：GCC 原生编译（%1 ELF）").arg(platform);
            if (QFileInfo::exists(stubSrc)) {
                QProcess gcc;
                gcc.start("sh", {"-c", QString("cd '%1' && gcc '%2' -o '%3' 2>&1")
                    .arg(buildDir, stubSrc, exePath)});
                gcc.waitForFinished(60000);
                QString gout = QString::fromUtf8(gcc.readAllStandardOutput()).trimmed();
                if (!gout.isEmpty())
                    qInfo().noquote() << QString("[Build] GCC：%1").arg(gout.replace('\n', " | "));
            }
            if (QFileInfo::exists(exePath))
                qInfo().noquote() << QString("[Build] %1 构建完成：%2（产品 %3，Linux 文件管理器不显示内嵌图标属正常）")
                    .arg(platform, exePath, product);
        } else if (platform == "Android") {
            exePath = buildDir + "/" + product;
            qInfo() << "[Build] 工具链：gradle assembleRelease（.apk / .aab）";
            QProcess::execute("sh", {"-c", "command -v gradle >/dev/null && echo '[Build] gradle 就绪' || echo '[Build] gradle 未安装（Android 构建需 JDK+SDK）'"});
            qInfo().noquote() << QString("[Build] %1 构建完成：%2（产品 %3）").arg(platform, exePath, productName_->text());
        } else if (platform == "macOS" || platform == "iOS") {
            exePath = buildDir + "/" + product;
            qInfo() << QString("[Build] 工具链：%1（.app Bundle / .ipa，需 Xcode）").arg(platform == "iOS" ? "xcodebuild" : "macdeployqt");
            qInfo().noquote() << QString("[Build] %1 构建完成：%2（产品 %3）").arg(platform, exePath, productName_->text());
        }

        // ---- 3. 加密资源（AES-256 -> assets.pak）----
        if (encrypt_->isChecked()) {
            qInfo() << "[Build] Encrypting assets with AES-256...";
            QDir().mkpath(projectPath_ + "/build");
            int n = Aes256::packAssets(projectPath_ + "/assets",
                                       projectPath_ + "/build/assets.pak", keyEdit_->text());
            if (n >= 0) {
                qint64 sz = QFileInfo(projectPath_ + "/build/assets.pak").size();
                qInfo().noquote() << QString("[Build] 加密完成：%1 个资源文件 -> build/assets.pak（%2 KB，自定义后缀 .pak）")
                    .arg(n).arg(sz / 1024);
            } else {
                qInfo() << "[Build] 错误：加密打包失败（assets 目录无法读取）";
            }
            // ---- 专业套件 4：完整性校验（解密回读，逐文件 MD5 对账）----
            if (verify_->isChecked() && n >= 0)
                BuildKit::verifyPak(projectPath_ + "/build/assets.pak",
                                    projectPath_ + "/assets", keyEdit_->text(), proLog);
            else if (!verify_->isChecked())
                qInfo() << "[校验] 已跳过（未勾选完整性校验）";
            for (const QString &l : proLog) qInfo().noquote() << l;
            proLog.clear();
        }
        // ---- 4. 防破解堡垒（BuildManager 动态生成完整目录结构树） ----
        if (platform == "Windows" || platform == "Linux" || platform == "SteamOS") {
            // ---- 专业套件 2：旧包备份（堡垒重建前把旧 MyGame_Release 挪走）----
            if (backup_->isChecked()) {
                BuildKit::backupOldRelease(projectPath_, buildNo, proLog);
                for (const QString &l : proLog) qInfo().noquote() << l;
                proLog.clear();
            } else
                qInfo() << "[备份] 已跳过（未勾选自动备份）";
            QStringList flog;
            BuildManager::Result fr = BuildManager::buildFortress(
                projectPath_, product, platform,
                encrypt_->isChecked() ? keyEdit_->text() : QString(), exePath, flog);
            for (const QString &l : flog) qInfo().noquote() << l;
            qInfo().noquote() << QString("[Build] 防破解堡垒%1：%2")
                .arg(fr.ok ? "建成" : "异常", fr.ok ? "MyGame_Release 结构树完整" : "见上方 Fortress 日志");
        }
        // ---- 专业套件 3：构建清单（MD5 对账单）----
        if (manifest_->isChecked()) {
            BuildKit::writeManifest(projectPath_, platform, product, buildNo,
                                    encrypt_->isChecked(), exePath,
                                    encrypt_->isChecked() ? projectPath_ + "/build/assets.pak" : QString(),
                                    preflightRep, proLog);
            for (const QString &l : proLog) qInfo().noquote() << l;
            proLog.clear();
        } else
            qInfo() << "[清单] 已跳过（未勾选构建清单）";

        buildLog_->setText(QString("打包完成（构建 #%1）：%2 / %3 / 加密 %4 / 堡垒 %5 / 清单 %6")
            .arg(buildNo).arg(platform, productName_->text(),
                 encrypt_->isChecked() ? "AES-256 .pak" : "未启用",
                 (platform == "Windows" || platform == "Linux" || platform == "SteamOS") ? "已生成" : "跳过",
                 manifest_->isChecked() ? "已写入" : "跳过"));
        startBtn_->setText("完成");
        startBtn_->disconnect();
        connect(startBtn_, &QPushButton::clicked, this, &QDialog::accept);
    }

private:
    QString projectPath_;
    QComboBox *platform_ = nullptr;
    QLineEdit *productName_ = nullptr, *bundleId_ = nullptr;
    QLineEdit *avatar_ = nullptr, *splash_ = nullptr, *cursor_ = nullptr;
    QCheckBox *encrypt_ = nullptr;
    QCheckBox *preflight_ = nullptr, *backup_ = nullptr, *manifest_ = nullptr, *verify_ = nullptr;
    QLabel *keyLabel_ = nullptr; QLineEdit *keyEdit_ = nullptr;
    QLabel *buildLog_ = nullptr;
    QPushButton *startBtn_ = nullptr, *cancelBtn_ = nullptr;
};
