#pragma once
// ============ 专业打包套件 BuildKit（编辑器 / 3DHeadless 无头版共用） ============
// 五件套：资源预检 / 构建号自增 / 旧包备份 / 构建清单（MD5 对账）/ .pak 完整性校验
// header-only，仅依赖 QtCore（无 GUI），编辑器 BuildDialog 与无头版 build 命令共用。
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QStringList>
#include "Aes256.h"

class BuildKit {
public:
    // ---------- 1. 资源预检：打包前体检 ----------
    // 只查 assets 顶层文件（packAssets 同口径），子目录列警告（不会被加密进包）
    static QJsonObject preflight(const QString &assetsDir, QStringList &log) {
        QJsonObject rep;
        qint64 total = 0; int files = 0;
        QJsonArray zero, big, subs;
        QDir dir(assetsDir);
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
            ++files; total += fi.size();
            if (fi.size() == 0) zero.append(fi.fileName());
            if (fi.size() > 10LL * 1024 * 1024) big.append(fi.fileName());
        }
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            subs.append(fi.fileName());
        rep["fileCount"] = files;
        rep["totalBytes"] = double(total);
        rep["zeroByte"] = zero;
        rep["bigFiles"] = big;
        rep["subdirsNotPacked"] = subs;
        log << QString("[预检] 资源体检完成：%1 个文件，共 %2 KB").arg(files).arg(total / 1024);
        if (zero.size()) log << QString("[预检] 警告：%1 个 0 字节空文件：%2")
            .arg(zero.size()).arg(toStringList(zero).join("、"));
        if (big.size()) log << QString("[预检] 提示：%1 个超过 10MB 的大文件（拖慢加载）：%2")
            .arg(big.size()).arg(toStringList(big).join("、"));
        if (subs.size()) log << QString("[预检] 注意：%1 个子目录不会被加密进包：%2")
            .arg(subs.size()).arg(toStringList(subs).join("、"));
        if (!zero.size() && !big.size() && !subs.size())
            log << "[预检] 全部干净，无警告";
        return rep;
    }

    // ---------- 2. 构建号自增：build/build.info ----------
    static int bumpBuildNo(const QString &projectPath, QStringList &log) {
        const QString infoPath = projectPath + "/build/build.info";
        int buildNo = 1;
        QFile f(infoPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            buildNo = o.value("buildNo").toInt() + 1;
            f.close();
        }
        QJsonObject o; o["buildNo"] = buildNo;
        o["lastBuildTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        QDir().mkpath(projectPath + "/build");
        QFile w(infoPath);
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            w.write(QJsonDocument(o).toJson()); w.close();
        }
        log << QString("[构建号] 本次构建号 #%1（build/build.info 已自增）").arg(buildNo);
        return buildNo;
    }

    // ---------- 3. 旧包备份：MyGame_Release -> build/backup，保留最近 3 份 ----------
    static void backupOldRelease(const QString &projectPath, int buildNo, QStringList &log) {
        const QString rel = projectPath + "/MyGame_Release";
        if (!QDir(rel).exists()) { log << "[备份] 无旧包，跳过备份"; return; }
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        const QString dest = projectPath + "/build/backup/Backup_B" + QString::number(buildNo - 1)
                             + "_" + stamp;
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (QDir().rename(rel, dest))
            log << QString("[备份] 旧包已挪走 -> build/backup/%1").arg(QFileInfo(dest).fileName());
        else
            log << "[备份] 备份失败（旧包保留在原地）";
        // 只保留最近 3 份，更老的暴力清掉
        QDir bd(projectPath + "/build/backup");
        QStringList olds = bd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        while (olds.size() > 3) {
            const QString victim = olds.takeFirst();
            if (QDir(projectPath + "/build/backup/" + victim).removeRecursively())
                log << QString("[备份] 超出保留数，删除最老备份：%1").arg(victim);
        }
    }

    // ---------- 4. 构建清单：MD5 对账单 -> MyGame_Release/build_manifest.json ----------
    static bool writeManifest(const QString &projectPath, const QString &platform,
                              const QString &product, int buildNo, bool encryptOn,
                              const QString &exePath, const QString &pakPath,
                              const QJsonObject &preflightRep, QStringList &log) {
        QJsonObject m;
        m["engine"] = "PhysicsEngine3D";
        m["engineVersion"] = "v1.2.0";
        m["buildNo"] = buildNo;
        m["platform"] = platform;
        m["product"] = product;
        m["buildTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        m["encrypted"] = encryptOn;
        QJsonArray assets;
        qint64 atotal = 0;
        QDir adir(projectPath + "/assets");
        for (const QFileInfo &fi : adir.entryInfoList(QDir::Files)) {
            QJsonObject e;
            e["file"] = fi.fileName();
            e["bytes"] = double(fi.size());
            e["md5"] = fileMd5(fi.absoluteFilePath());
            assets.append(e); atotal += fi.size();
        }
        m["assets"] = assets;
        m["assetCount"] = assets.size();
        m["assetTotalBytes"] = double(atotal);
        if (!pakPath.isEmpty() && QFileInfo::exists(pakPath)) {
            QJsonObject p; p["file"] = QFileInfo(pakPath).fileName();
            p["bytes"] = double(QFileInfo(pakPath).size());
            p["md5"] = fileMd5(pakPath);
            m["pak"] = p;
        }
        if (!exePath.isEmpty() && QFileInfo::exists(exePath)) {
            QJsonObject x; x["file"] = QFileInfo(exePath).fileName();
            x["bytes"] = double(QFileInfo(exePath).size());
            x["md5"] = fileMd5(exePath);
            m["exe"] = x;
        }
        m["preflightWarnings"] = preflightRep.value("zeroByte").toArray().size()
                               + preflightRep.value("bigFiles").toArray().size()
                               + preflightRep.value("subdirsNotPacked").toArray().size();
        const QString out = projectPath + "/MyGame_Release/build_manifest.json";
        QDir().mkpath(QFileInfo(out).absolutePath());
        QFile f(out);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log << "[清单] 错误：构建清单写入失败"; return false;
        }
        f.write(QJsonDocument(m).toJson()); f.close();
        log << QString("[清单] 构建清单已写入 MyGame_Release/build_manifest.json"
                       "（%1 项资产 MD5 对账 + 引擎版本 + 构建号 #%2）").arg(assets.size()).arg(buildNo);
        return true;
    }

    // ---------- 5. .pak 完整性校验：解密回读，逐文件 MD5 对账 ----------
    struct VerifyResult { bool ok = false; int checked = 0; QStringList bad; };
    static VerifyResult verifyPak(const QString &pakPath, const QString &assetsDir,
                                  const QString &keyStr, QStringList &log) {
        VerifyResult r;
        QFile f(pakPath);
        if (!f.open(QIODevice::ReadOnly)) { log << "[校验] 失败：.pak 文件打不开"; return r; }
        QByteArray all = f.readAll();
        if (all.size() < 19 || !all.startsWith("VNPAK01")) {
            log << "[校验] 失败：.pak 魔数不对（不是 VNPAK01 格式）"; return r;
        }
        QByteArray key(32, 0);
        QByteArray ks = keyStr.toUtf8();
        for (int i = 0; i < qMin(32, ks.size()); ++i) key[i] = ks.at(i);
        QByteArray iv8 = all.mid(7, 8);
        quint32 n = 0;
        memcpy(&n, all.constData() + 15, 4);
        int pos = 19, good = 0;
        for (quint32 i = 0; i < n && pos + 8 <= all.size(); ++i) {
            quint32 nl = 0, pl = 0;
            memcpy(&nl, all.constData() + pos, 4); pos += 4;
            QString name = QString::fromUtf8(all.mid(pos, int(nl))); pos += int(nl);
            memcpy(&pl, all.constData() + pos, 4); pos += 4;
            QByteArray plain = Aes256::crypt(key, iv8, all.mid(pos, int(pl)));
            pos += int(pl);
            ++r.checked;
            QFile src(assetsDir + "/" + name);
            if (!src.open(QIODevice::ReadOnly)) { r.bad << name + "（磁盘上已找不到原件）"; continue; }
            QByteArray orig = src.readAll();
            if (orig.size() != plain.size()
                || QCryptographicHash::hash(orig, QCryptographicHash::Md5)
                   != QCryptographicHash::hash(plain, QCryptographicHash::Md5))
                r.bad << name;
            else ++good;
        }
        r.ok = r.bad.isEmpty() && good == int(n);
        log << QString("[校验] 完整性校验：回读解密 %1 个文件，MD5 对账 %2")
            .arg(r.checked).arg(r.ok ? "全部一致 ✅" : QString("不一致 %1 个 ❌").arg(r.bad.size()));
        for (const QString &b : r.bad) log << "[校验] 不一致：" << b;
        return r;
    }

    static QString fileMd5(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5).toHex());
    }

private:
    static QStringList toStringList(const QJsonArray &a) {
        QStringList out;
        for (const auto &v : a) out << v.toString();
        return out;
    }
};
