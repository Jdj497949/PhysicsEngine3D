#pragma once
// ============ AES-256 CTR（自包含 header-only，资源包加密用） ============
// 打包发布「加密资源」勾选后：assets 目录全量文件 → assets.pak 自定义包
// NIST SP 800-38A CTR 模式：密钥 32 字节，计数器 16 字节（前 8 字节随机 IV + 后 8 字节块序号）
#include <QString>
#include <QFile>
#include <QDir>
#include <QByteArray>
#include <QDateTime>
#include <vector>
#include <cstdint>

class Aes256 {
public:
    // 加密明文（CTR：加解密同函数，XOR keystream）
    static QByteArray crypt(const QByteArray &key, const QByteArray &iv8, const QByteArray &plain) {
        uint8_t rk[240];   // 15 轮 × 16 字节 = 240 字节（60 个字），60 只够 15 个字会越界砸栈
        expandKey(key, rk);
        QByteArray out;
        out.reserve(plain.size());
        uint8_t ctr[16], stream[16];
        memcpy(ctr, iv8.constData(), 8);
        memset(ctr + 8, 0, 8);
        uint64_t block = 0;
        for (int i = 0; i < plain.size(); ++i) {
            if (i % 16 == 0) {
                // 计数器后 8 字节 = 块序号（大端）
                for (int b = 0; b < 8; ++b) ctr[8 + b] = uint8_t(block >> (56 - 8 * b));
                encryptBlock(ctr, rk, stream);
                ++block;
            }
            out.append(char(plain.at(i) ^ char(stream[i % 16])));
        }
        return out;
    }

    // 打包加密 assets 目录 → pakPath。返回加密文件数，-1 = 失败
    static int packAssets(const QString &assetsDir, const QString &pakPath, const QString &keyStr) {
        QByteArray key(32, 0);
        QByteArray ks = keyStr.toUtf8();
        for (int i = 0; i < qMin(32, ks.size()); ++i) key[i] = ks.at(i);   // 密钥不足 32 字节补 0
        qsizetype seed = QDateTime::currentMSecsSinceEpoch();
        QByteArray iv8(8, 0);
        for (int i = 0; i < 8; ++i) iv8[i] = char((seed >> (8 * (7 - i))) & 0xFF);

        QFile out(pakPath);
        if (!out.open(QIODevice::WriteOnly)) return -1;
        // 文件头：魔数 + IV8 + 条目数
        out.write("VNPAK01", 7);
        out.write(iv8.constData(), 8);
        QDir dir(assetsDir);
        QStringList files;
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
            files << fi.fileName();
        quint32 n = quint32(files.size());
        out.write(reinterpret_cast<const char *>(&n), 4);
        int count = 0;
        for (const QString &name : files) {
            QFile in(assetsDir + "/" + name);
            if (!in.open(QIODevice::ReadOnly)) continue;
            QByteArray plain = in.readAll();
            QByteArray enc = crypt(key, iv8, plain);
            QByteArray nm = name.toUtf8();
            quint32 nl = quint32(nm.size()), pl = quint32(plain.size());
            out.write(reinterpret_cast<const char *>(&nl), 4);
            out.write(nm.constData(), int(nl));
            out.write(reinterpret_cast<const char *>(&pl), 4);   // 原始大小（解包校验）
            out.write(enc.constData(), enc.size());
            ++count;
        }
        return out.isOpen() && count == int(n) ? count : count;
    }

private:
    static const uint8_t *sbox() {
        static const uint8_t box[256] = {
            0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
            0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
            0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
            0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
            0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
            0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
            0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
            0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
            0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
            0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
            0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
            0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
            0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
            0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
            0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
            0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
        return box;
    }
    static uint8_t xtime(uint8_t x) { return uint8_t((x << 1) ^ ((x >> 7) * 0x1b)); }
    static void expandKey(const QByteArray &key, uint8_t *rk) {
        static const uint8_t rcon[7] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40};
        const uint8_t *k = reinterpret_cast<const uint8_t *>(key.constData());
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) rk[i * 4 + j] = k[i * 4 + j];
        for (int i = 8; i < 60; ++i) {
            uint8_t t[4] = {rk[(i - 1) * 4], rk[(i - 1) * 4 + 1], rk[(i - 1) * 4 + 2], rk[(i - 1) * 4 + 3]};
            if (i % 8 == 0) {
                uint8_t tmp = t[0];
                t[0] = uint8_t(sbox()[t[1]] ^ rcon[i / 8 - 1]);
                t[1] = sbox()[t[2]]; t[2] = sbox()[t[3]]; t[3] = sbox()[tmp];
            } else if (i % 8 == 4) {
                for (int j = 0; j < 4; ++j) t[j] = sbox()[t[j]];
            }
            for (int j = 0; j < 4; ++j) rk[i * 4 + j] = uint8_t(rk[(i - 8) * 4 + j] ^ t[j]);
        }
    }
    static void encryptBlock(const uint8_t in[16], const uint8_t *rk, uint8_t out_[16]) {
        uint8_t s[16];
        for (int i = 0; i < 16; ++i) s[i] = uint8_t(in[i] ^ rk[i]);
        for (int round = 1; round <= 14; ++round) {
            // SubBytes
            for (int i = 0; i < 16; ++i) s[i] = sbox()[s[i]];
            // ShiftRows
            uint8_t t;
            t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
            t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
            t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
            // MixColumns（最后一轮省略）
            if (round < 14) {
                for (int c = 0; c < 4; ++c) {
                    uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
                    s[c * 4]     = uint8_t(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                    s[c * 4 + 1] = uint8_t(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                    s[c * 4 + 2] = uint8_t(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                    s[c * 4 + 3] = uint8_t((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
                }
            }
            // AddRoundKey
            for (int i = 0; i < 16; ++i) s[i] = uint8_t(s[i] ^ rk[round * 16 + i]);
        }
        for (int i = 0; i < 16; ++i) out_[i] = s[i];
    }
};
