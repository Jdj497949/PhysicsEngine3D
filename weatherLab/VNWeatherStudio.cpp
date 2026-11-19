// VNWeatherStudio —— 天气工作室（第九个程序）。
// 编辑 .sky 天空配置：天气状态（晴/雨/雾/夜）+ 环境光颜色/强度 + 天空盒贴图槽。
// 保存后把 .sky 拖入 3DEditor 资产树双击即可立即改变场景天空（SkySystem::applyToWorld）。
#include <QApplication>
#include "../common/CrashHandler.h"
#include <QMainWindow>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QColorDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>

// ============ 天空盒导入槽：拖拽图片即填路径 ============
class SkyboxSlot : public QLineEdit {
    Q_OBJECT
public:
    SkyboxSlot(const QString &label, QWidget *p = nullptr) : QLineEdit(p) {
        setReadOnly(true);
        setPlaceholderText(QString("%1：拖拽图片到此处…").arg(label));
        setStyleSheet("background:#20242c; color:#9fd6ff; border:2px dashed #4a5568;"
                      "border-radius:4px; padding:5px;");
    }
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        for (const QUrl &u : e->mimeData()->urls()) {
            QString p = u.toLocalFile();
            if (p.endsWith(".png", Qt::CaseInsensitive) || p.endsWith(".jpg", Qt::CaseInsensitive)
                || p.endsWith(".jpeg", Qt::CaseInsensitive) || p.endsWith(".bmp", Qt::CaseInsensitive)) {
                setText(p);
                qInfo() << QString("[Sky] 天空盒槽位填入：%1").arg(p);
                break;
            }
        }
        e->acceptProposedAction();
    }
};

// ============ 天空预览画布：QPainter 按天气手绘 ============
class SkyPreview : public QWidget {
    Q_OBJECT
public:
    QString weather = "晴天";
    QColor ambient = QColor(255, 255, 255);
    float intensity = 1.0f;
    QImage skyboxImage;          // 天空盒 1 槽位的图（有图优先画图）

    explicit SkyPreview(QWidget *p = nullptr) : QWidget(p) {
        setMinimumHeight(360);
        setStyleSheet("border:1px solid #333a46; border-radius:4px;");
    }
    void refresh() { update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QRect r = rect();
        // 底：按天气的垂直渐变
        QLinearGradient g(0, 0, 0, height());
        if (weather == "晴天")          { g.setColorAt(0, QColor(96, 165, 250)); g.setColorAt(1, QColor(196, 224, 255)); }
        else if (weather == "雨天")     { g.setColorAt(0, QColor(52, 60, 74));   g.setColorAt(1, QColor(96, 104, 118)); }
        else if (weather == "大雾")     { g.setColorAt(0, QColor(148, 154, 162)); g.setColorAt(1, QColor(196, 200, 206)); }
        else /* 夜晚 */                 { g.setColorAt(0, QColor(10, 14, 34));   g.setColorAt(1, QColor(30, 40, 80)); }
        p.fillRect(r, g);

        // 天空盒图（有图则整体铺底，渐变退为氛围）
        if (!skyboxImage.isNull())
            p.drawImage(r, skyboxImage);

        if (weather == "晴天") {
            // 太阳 + 两朵云
            p.setBrush(QColor(255, 224, 130)); p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(width() - 140, 110), 52, 52);
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawEllipse(QPoint(200, 130), 46, 20);
            p.drawEllipse(QPoint(250, 122), 34, 16);
            p.drawEllipse(QPoint(950, 230), 40, 18);
        } else if (weather == "雨天") {
            // 云层 + 雨丝
            p.setBrush(QColor(40, 44, 52, 230)); p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(240, 90), 130, 42);
            p.drawEllipse(QPoint(760, 70), 170, 48);
            p.drawEllipse(QPoint(1200, 100), 120, 40);
            p.setPen(QPen(QColor(170, 190, 230, 160), 2));
            for (int i = 0; i < 90; ++i) {
                float seed = float(i) * 97.13f;
                int x = int(seed) % width();
                int y = int(seed * 1.7) % height();
                p.drawLine(x, y, x - 6, y + 22);
            }
        } else if (weather == "大雾") {
            // 远山剪影 + 三层雾带
            p.setBrush(QColor(96, 104, 116, 180)); p.setPen(Qt::NoPen);
            p.drawPolygon(QVector<QPoint>{{0, height()}, {int(width()*0.2), int(height()*0.55)},
                {int(width()*0.4), height()}, {int(width()*0.65), int(height()*0.6)},
                {int(width()*0.85), height()}, {width(), int(height()*0.5)}, {width(), height()}});
            for (int i = 0; i < 3; ++i)
                p.fillRect(QRect(0, int(height() * (0.42 + 0.18 * i)), width(), 56),
                           QColor(210, 214, 220, 70 - 18 * i));
        } else {
            // 星空 + 月亮（星点用确定性伪随机，重绘不闪）
            for (int i = 0; i < 140; ++i) {
                float s1 = float(i) * 0.6180339887f;   // 黄金比例散射
                int x = int((s1 - int(s1)) * width());
                int y = int((float(i) * 0.7548776662f - int(float(i) * 0.7548776662f)) * height() * 0.8);
                int b = 140 + (int(s1 * 100) % 115);
                p.fillRect(QRect(x, y, 2, 2), QColor(255, 255, 230, b));
            }
            p.setBrush(QColor(240, 238, 210)); p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(width() - 160, 100), 44, 44);
            p.setBrush(QColor(10, 14, 34));
            p.drawEllipse(QPoint(width() - 145, 88), 38, 38);
        }

        // 环境光罩染：颜色 + 强度（对应 3D 场景的 uAmbient * uAmbientI）
        if (intensity > 0.01f) {
            QColor tint(ambient.red(), ambient.green(), ambient.blue(),
                        qBound(0, int(intensity * 90), 255));
            p.fillRect(r, tint);
        }

        // 标注
        p.setPen(QColor(255, 255, 255, 200));
        p.setFont(QFont("Noto Sans CJK SC", 12, QFont::Bold));
        p.drawText(14, 26, QString("预览：%1 ｜ 环境光 %2 ｜ 强度 %3")
            .arg(weather, ambient.name()).arg(intensity, 0, 'f', 2));
    }
};

// ============ 主窗口 ============
class WeatherWindow : public QMainWindow {
    Q_OBJECT
public:
    SkyPreview *preview = nullptr;
    QComboBox *weatherCombo = nullptr;
    SkyboxSlot *skySlots[3] = {};
    QPushButton *ambientBtn = nullptr;
    QSlider *intensitySlider = nullptr;
    QLabel *intensityVal = nullptr;
    QColor ambient = QColor(255, 255, 255);
    float intensity = 1.0f;

    WeatherWindow() {
        setWindowTitle("VNWeatherStudio 天气工作室 - 编辑 .sky 天空配置");
        resize(1280, 820);

        // ---- 左：3 个天空盒导入槽 + 生成星空渐变图 ----
        auto *left = new QWidget;
        left->setFixedWidth(340);
        auto *lv = new QVBoxLayout(left);
        lv->setContentsMargins(8, 10, 8, 8);
        lv->addWidget(new QLabel("天空盒贴图（拖拽导入，槽位 1 即预览底图）"));
        const char *names[3] = {"天空盒 1 (Skybox)", "天空盒 2 (Skybox)", "天空盒 3 (Skybox)"};
        for (int i = 0; i < 3; ++i) {
            skySlots[i] = new SkyboxSlot(names[i]);
            lv->addWidget(skySlots[i]);
        }
        auto *genSky = new QPushButton("生成星空渐变图");
        genSky->setStyleSheet("background:#5b4a9e;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        lv->addWidget(genSky);
        connect(genSky, &QPushButton::clicked, this, &WeatherWindow::generateStarrySky);
        lv->addWidget(new QLabel("保存后把 .sky 拖入 3DEditor 资产树，\n双击即可立即改变 3D 场景的天空。"));
        lv->addStretch();

        // ---- 中：预览画布 ----
        preview = new SkyPreview;

        // ---- 右：天气 + 环境光控制 ----
        auto *right = new QWidget;
        right->setFixedWidth(300);
        auto *rv = new QVBoxLayout(right);
        rv->setContentsMargins(8, 10, 8, 8);
        auto *form = new QFormLayout;
        weatherCombo = new QComboBox;
        weatherCombo->addItems({"晴天", "雨天", "大雾", "夜晚"});
        form->addRow("天气状态", weatherCombo);
        ambientBtn = new QPushButton;
        ambientBtn->setFixedHeight(32);
        form->addRow("环境光颜色", ambientBtn);
        intensitySlider = new QSlider(Qt::Horizontal);
        intensitySlider->setRange(0, 200);
        intensitySlider->setValue(100);
        intensityVal = new QLabel("1.00");
        auto *iRow = new QHBoxLayout; iRow->addWidget(intensitySlider); iRow->addWidget(intensityVal);
        form->addRow("环境光强度", iRow);
        rv->addLayout(form);
        auto *save = new QPushButton("保存 .sky 文件");
        save->setStyleSheet("background:#2f6fab;color:white;padding:8px;border-radius:4px;font-weight:bold;");
        rv->addWidget(save);
        rv->addStretch();

        auto *central = new QWidget;
        auto *hl = new QHBoxLayout(central);
        hl->addWidget(left);
        hl->addWidget(preview, 1);
        hl->addWidget(right);
        setCentralWidget(central);

        connect(weatherCombo, &QComboBox::currentTextChanged, this, [this](const QString &w){
            preview->weather = w; preview->refresh();
            qInfo() << QString("[Sky] 天气切换：%1（预设已刷新预览）").arg(w);
        });
        connect(ambientBtn, &QPushButton::clicked, this, [this]{
            QColor c = QColorDialog::getColor(ambient, this, "选择环境光颜色");
            if (!c.isValid()) return;
            ambient = c;
            ambientBtn->setStyleSheet(QString("background:%1;").arg(c.name()));
            preview->ambient = c; preview->refresh();
        });
        connect(intensitySlider, &QSlider::valueChanged, this, [this](int v){
            intensity = v / 100.0f;
            intensityVal->setText(QString::number(intensity, 'f', 2));
            preview->intensity = intensity; preview->refresh();
        });
        connect(save, &QPushButton::clicked, this, &WeatherWindow::saveSky);

        // 同步初始状态
        ambientBtn->setStyleSheet(QString("background:%1;").arg(ambient.name()));
        preview->ambient = ambient;
        preview->intensity = intensity;

        // 演示钩子：WEATHER_AUTO_MODE=demo + WEATHER_SHOT=xx.png
        QString autoMode = qEnvironmentVariable("WEATHER_AUTO_MODE");
        QString shot = qEnvironmentVariable("WEATHER_SHOT");
        if (autoMode == "demo" && !shot.isEmpty()) {
            QTimer::singleShot(600, this, [this]{
                // 预填：夜晚 + 环境光 #4D5B9E（0.30,0.36,0.62）+ 强度 0.9
                weatherCombo->setCurrentText("夜晚");
                ambient = QColor::fromRgbF(0.30f, 0.36f, 0.62f);
                ambientBtn->setStyleSheet(QString("background:%1;").arg(ambient.name()));
                intensitySlider->setValue(90);
                preview->ambient = ambient;
                preview->refresh();
                qInfo() << "[Sky] 演示配置：夜晚 + 环境光 #4d5b9e + 强度 0.90，保存后拖入 3D 场景即变夜空";
            });
            QTimer::singleShot(3000, this, [this, shot]{
                if (auto *scr = QGuiApplication::primaryScreen()) scr->grabWindow(0).save(shot);
            });
            QTimer::singleShot(4000, qApp, &QCoreApplication::quit);
        }
    }

private:
    // 程序生成一张星空渐变 PNG（渐变夜空底 + 随机星点），填入天空盒 1 槽位
    void generateStarrySky() {
        QString out = QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets/星空渐变.png");
        QDir().mkpath(QFileInfo(out).absolutePath());
        int w = 1024, h = 512;
        QImage img(w, h, QImage::Format_RGB32);
        QPainter p(&img);
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0.0f, QColor(8, 10, 28));
        g.setColorAt(0.6f, QColor(24, 34, 78));
        g.setColorAt(1.0f, QColor(64, 50, 110));
        p.fillRect(0, 0, w, h, g);
        for (int i = 0; i < 420; ++i) {
            int x = QRandomGenerator::global()->bounded(w);
            int y = QRandomGenerator::global()->bounded(int(h * 0.9));
            int b = 90 + QRandomGenerator::global()->bounded(165);
            int sz = QRandomGenerator::global()->bounded(1, 3);
            p.fillRect(QRect(x, y, sz, sz), QColor(255, 255, 235, b));
        }
        // 一轮小月亮
        p.setBrush(QColor(240, 238, 210)); p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(w - 180, 90), 40, 40);
        p.setBrush(QColor(14, 18, 40));
        p.drawEllipse(QPoint(w - 166, 78), 34, 34);
        p.end();
        img.save(out);
        skySlots[0]->setText(out);
        preview->skyboxImage = img;
        preview->refresh();
        qInfo() << QString("[Sky] 星空渐变图已生成：%1（已填入天空盒 1 槽位）").arg(out);
    }

    void saveSky() {
        // 输出路径：WEATHER_OUT 环境变量优先，否则弹保存对话框
        QString out = qEnvironmentVariable("WEATHER_OUT");
        if (out.isEmpty())
            out = QFileDialog::getSaveFileName(this, "保存天空配置",
                QDir::home().filePath("Physics3DProjects/Demo火锅店项目/assets/新天空.sky"),
                "天空配置 (*.sky)");
        if (out.isEmpty()) return;

        QJsonObject j{
            {"kind", "sky"},
            {"name", QFileInfo(out).fileName()},
            {"weather", weatherCombo->currentText()},
            {"ambient", QJsonArray{ambient.redF(), ambient.greenF(), ambient.blueF()}},
            {"intensity", intensity},
            {"skybox", skySlots[0]->text()},
        };
        QFile f(out);
        if (!f.open(QIODevice::WriteOnly)) { qInfo() << "[Sky] 保存失败：无法写入" << out; return; }
        f.write(QJsonDocument(j).toJson());
        qInfo().noquote() << QString("[Sky] 已保存：%1（天气=%2 强度=%3 拖入 3DEditor 双击生效）")
            .arg(out, weatherCombo->currentText()).arg(intensity, 0, 'f', 2);
    }
};

#include "VNWeatherStudio.moc"

static int runApp(QApplication &app, int argc, char **argv) {
    
    WeatherWindow w;
    w.show();
    return app.exec();
}

// 崩溃捕获主入口：崩溃 → 日志 → 双层弹窗 → 自爆重启启动器
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString appDir = QCoreApplication::applicationDirPath();
    CrashHandler::install("VNWeatherStudio", appDir + "/../logs", appDir + "/3DLauncher", false);
    if (CRASH_SAVE_POINT()) {
        CrashHandler::showCrashDialogs();
        return 0;
    }
    return runApp(app, argc, argv);
}
