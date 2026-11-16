#include "ui/MainWindow.h"

#include "core/App.h"
#include "core/Keys.h"
#include "core/Log.h"
#include "platform/Platform.h"
#include "ui/CaptureOverlay.h"
#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace ac {

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color:#6b7280;");
    l->setWordWrap(true);
    return l;
}

QSpinBox* spin(int lo, int hi, int val, const QString& suffix, QWidget* parent, int step = 10) {
    auto* s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setValue(val);
    s->setSingleStep(step);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

QWidget* previewWidget(QLabel*& label, QWidget* parent) {
    label = new QLabel(parent);
    label->setMinimumHeight(64);
    label->setMaximumHeight(96);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("border:1px dashed #cbd5e1; border-radius:6px; color:#94a3b8; background:#f8fafc;");
    label->setText("模板预览（也可以直接把图片拖进来）");
    return label;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("连点器 2.0");
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);

    // 顶部：标题 + 状态 + 设置按钮
    auto* head = new QHBoxLayout;
    auto* title = new QLabel("连点器", central);
    title->setStyleSheet("font-size:15px; font-weight:600;");
    m_pill = new QLabel("空闲", central);
    m_pill->setAlignment(Qt::AlignCenter);
    m_pill->setMinimumWidth(66);
    setStatusPill("空闲", "#64748b");
    auto* settingsBtn = new QPushButton("设置", central);
    settingsBtn->setFixedWidth(56);
    head->addWidget(title);
    head->addWidget(m_pill);
    head->addStretch(1);
    head->addWidget(settingsBtn);

    m_tabs = new QTabWidget(central);
    m_tabs->setStyleSheet(
        "QTabWidget::pane{border:none;background:#ffffff;}"
        "QTabBar::tab{padding:5px 9px;margin-right:2px;border:none;border-bottom:2px solid transparent;}"
        "QTabBar::tab:selected{color:#0f766e;border-bottom:2px solid #0f766e;font-weight:600;}");
    m_tabs->addTab(buildMousePage(), "鼠标连点");
    m_tabs->addTab(buildImagePage(), "找图连点");
    m_tabs->addTab(buildWatchPage(), "监视窗口");
    m_tabs->addTab(buildRecordPage(), "录制回放");
    m_tabs->addTab(buildLogPage(), "日志");

    m_hint = hintLabel(QString(), central);

    root->addLayout(head);
    root->addWidget(m_tabs, 1);
    root->addWidget(m_hint);
    setCentralWidget(central);

    // 窗口大小：优先用上次记住的，否则按屏幕比例给个小窗
    QSize suggest = App::suggestWindowSize();
    const Config& c = App::instance().cfg();
    if (c.ui.w > 0 && c.ui.h > 0) {
        resize(c.ui.w, c.ui.h);
    } else {
        resize(suggest);
        setMinimumSize(350, 400);
    }
    if (c.ui.tab >= 0 && c.ui.tab < m_tabs->count()) m_tabs->setCurrentIndex(c.ui.tab);

    connect(settingsBtn, &QPushButton::clicked, this, [this] {
        SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            App::instance().saveCfg();
            refreshUi();
            logLine(QString("点击类型：%1%2")
                        .arg(App::instance().cfg().click.type)
                        .arg(App::instance().cfg().click.dbl ? "（双击）" : ""));
        }
    });

    connect(&App::instance(), &App::stateChanged, this, &MainWindow::refreshUi);
    connect(&App::instance(), &App::recordFinished, this, [this] {
        const auto events = App::instance().pendingRecordEvents();
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, "保存脚本",
            QString("这次录到 %1，给它起个名字：").arg(actionSummary(events)),
            QLineEdit::Normal,
            QString("脚本_%1").arg(QDateTime::currentDateTime().toString("MMdd_HHmm")), &ok);
        if (ok && !name.trimmed().isEmpty()) App::instance().saveRecording(name, events);
        else App::instance().discardRecording();
        refreshScripts();
        refreshUi();
    });
    connect(&App::instance(), &App::requestShowWindow, this, [this] {
        showNormal();
        raise();
        activateWindow();
        logLine("已有一个实例在运行，已切到前台");
    });

    connect(&Log::instance(), &Log::line, this, [this](const QString& t) {
        m_log->appendPlainText(t);
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    });
    for (const QString& line : Log::instance().history()) m_log->appendPlainText(line);

    m_poll = new QTimer(this);
    m_poll->setInterval(400);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (App::instance().mouseRunning()) {
            m_mouseBtn->setText(QString("停止（已点 %1 次）").arg(App::instance().mouseClicks()));
            m_mouseState->setText(QString("正在点击：%1 次，间隔 %2 ms")
                                      .arg(App::instance().mouseClicks())
                                      .arg(App::instance().cfg().mouse.interval));
        } else {
            m_mouseState->setText("还没开始");
        }
        if (App::instance().imageRunning())
            m_imgBtn->setText(QString("停止（已点 %1 次）").arg(App::instance().imageClicks()));
        if (App::instance().watchRunning())
            m_watchBtn->setText(QString("停止（已点 %1 次）").arg(App::instance().watchClicks()));
        if (App::instance().replayRunning())
            m_playBtn->setText(QString("停止回放（已执行 %1 次）").arg(App::instance().replayClicks()));
        if (App::instance().recording()) {
            m_recBtn->setText("停止录制");
            m_recState->setText(QString("录制中：已记录 %1 个动作，%2 秒")
                                    .arg(App::instance().recordCount())
                                    .arg(App::instance().recordElapsedMs() / 1000.0, 0, 'f', 1));
        }
        refreshUi();
    });
    m_poll->start();

    m_loading = true;
    bindEdits();
    m_loading = false;
    refreshScripts();
    refreshUi();
}

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildMousePage() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);

    auto* row = new QHBoxLayout;
    m_mouseInterval = spin(20, 600000, App::instance().cfg().mouse.interval, " 毫秒", page);
    row->addWidget(new QLabel("点击间隔", page));
    row->addWidget(m_mouseInterval);

    m_clickTypeLabel = new QLabel(this);
    m_clickTypeLabel->setStyleSheet("color:#334155;");
    row->addWidget(m_clickTypeLabel);
    row->addStretch(1);

    m_mouseBtn = new QPushButton("开始连点", page);
    m_mouseBtn->setMinimumHeight(34);
    m_mouseBtn->setStyleSheet("QPushButton{background:#0f766e;color:white;border-radius:6px;font-weight:600;}");

    m_mouseHk = makeHotkeyRow(page, "mouse", App::instance().cfg().mouse.hotkey,
                              App::instance().cfg().mouse.hotkeyEnabled);
    m_mouseState = new QLabel("还没开始", page);
    m_mouseState->setStyleSheet("color:#64748b;");

    lay->addLayout(row);
    lay->addWidget(hintLabel("在鼠标当前位置按上面的间隔反复点击；键盘/侧键在「设置」里切换。", page));
    lay->addWidget(m_mouseBtn);
    lay->addWidget(m_mouseHk.row);
    lay->addWidget(m_mouseState);
    lay->addStretch(1);

    connect(m_mouseBtn, &QPushButton::clicked, this, [] { App::instance().toggleMouse(); });
    return page;
}

QWidget* MainWindow::buildImagePage() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    auto* top = new QHBoxLayout;
    m_imgPath = new QLineEdit(page);
    m_imgPath->setReadOnly(true);
    m_imgPath->setPlaceholderText("还没选模板图片");
    auto* pick = new QPushButton("选图片", page);
    auto* capture = new QPushButton("截屏框选", page);
    auto* clear = new QPushButton("清空", page);
    top->addWidget(m_imgPath, 1);
    top->addWidget(pick);
    top->addWidget(capture);
    top->addWidget(clear);

    auto* preview = previewWidget(m_imgPreview, page);

    auto* confRow = new QHBoxLayout;
    m_imgConf = new QSlider(Qt::Horizontal, page);
    m_imgConf->setRange(50, 99);
    m_imgConf->setValue(int(App::instance().cfg().image.confidence * 100));
    m_imgConfVal = new QLabel(page);
    confRow->addWidget(new QLabel("匹配度", page));
    confRow->addWidget(m_imgConf, 1);
    confRow->addWidget(m_imgConfVal);

    auto* row2 = new QHBoxLayout;
    m_imgInterval = spin(50, 600000, App::instance().cfg().image.interval, " 毫秒", page);
    m_imgOffX = spin(-2000, 2000, App::instance().cfg().image.offx, "", page, 1);
    m_imgOffY = spin(-2000, 2000, App::instance().cfg().image.offy, "", page, 1);
    row2->addWidget(new QLabel("间隔", page));
    row2->addWidget(m_imgInterval);
    row2->addWidget(new QLabel("偏移X", page));
    row2->addWidget(m_imgOffX);
    row2->addWidget(new QLabel("Y", page));
    row2->addWidget(m_imgOffY);
    row2->addStretch(1);

    auto* row3 = new QHBoxLayout;
    m_imgLimitOn = new QCheckBox("只点", page);
    m_imgLimitOn->setChecked(App::instance().cfg().image.limitEnabled);
    m_imgLimit = spin(1, 99999, App::instance().cfg().image.limitCount, " 次", page, 1);
    m_imgOnMiss = new QComboBox(page);
    m_imgOnMiss->addItem("没找到就继续等", "wait");
    m_imgOnMiss->addItem("没找到就停止", "stop");
    m_imgOnMiss->setCurrentIndex(App::instance().cfg().image.onMiss == "stop" ? 1 : 0);
    row3->addWidget(m_imgLimitOn);
    row3->addWidget(m_imgLimit);
    row3->addWidget(new QLabel("没找到时", page));
    row3->addWidget(m_imgOnMiss);
    row3->addStretch(1);

    m_imgBtn = new QPushButton("开始找图连点", page);
    m_imgBtn->setMinimumHeight(34);
    m_imgBtn->setStyleSheet("QPushButton{background:#0f766e;color:white;border-radius:6px;font-weight:600;}");
    m_imgTestBtn = new QPushButton("测一下", page);
    m_imgTestBtn->setToolTip("只截屏匹配一次，不点击，用来确认模板能不能找到");
    m_imgState = new QLabel(page);
    m_imgState->setStyleSheet("color:#475569;");
    m_imgState->setWordWrap(true);

    m_imgHk = makeHotkeyRow(page, "image", App::instance().cfg().image.hotkey,
                            App::instance().cfg().image.hotkeyEnabled);

    lay->addLayout(top);
    lay->addWidget(preview);
    lay->addLayout(confRow);
    lay->addLayout(row2);
    lay->addLayout(row3);
    lay->addWidget(m_imgBtn);
    auto* testRow = new QHBoxLayout;
    testRow->addWidget(m_imgTestBtn);
    testRow->addWidget(m_imgHk.row, 1);
    lay->addLayout(testRow);
    lay->addWidget(hintLabel("命中后点它的中心；最高匹配度会写进日志。", page));
    lay->addWidget(m_imgState);
    lay->addStretch(1);

    connect(pick, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "选模板图片", templatesDirHint(),
                                                          "图片 (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty()) return;
        setTemplateForCurrentTab(path);
    });
    connect(capture, &QPushButton::clicked, this, [this] {
        auto* ov = new CaptureOverlay();
        connect(ov, &CaptureOverlay::captured, this, [this](const QImage& img) {
            const QString path = templatesDirHint() + "/模板_" +
                                 QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
            if (img.save(path, "PNG")) setTemplateForCurrentTab(path);
        });
        connect(ov, &QWidget::destroyed, ov, &QObject::deleteLater);
        ov->show();
        ov->raise();
        ov->activateWindow();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        const int tab = m_tabs->currentIndex();
        if (tab == 1) App::instance().cfg().image.tmpl.clear();
        else if (tab == 2) App::instance().cfg().watch.tmpl.clear();
        App::instance().saveCfg();
        updatePreview();
    });
    connect(m_imgBtn, &QPushButton::clicked, this, [] { App::instance().toggleImage(); });
    connect(m_imgTestBtn, &QPushButton::clicked, this, [this] {
        const QString r = App::instance().probeMatch(false);
        m_imgState->setText(r);
        logLine("测试找图：" + r);
    });
    connect(m_imgLimitOn, &QCheckBox::toggled, m_imgLimit, &QSpinBox::setEnabled);
    m_imgLimit->setEnabled(m_imgLimitOn->isChecked());

    return page;
}

QWidget* MainWindow::buildWatchPage() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    m_watchProcess = new QLineEdit(page);
    m_watchProcess->setPlaceholderText("如 game.exe / chrome");
    m_watchTitle = new QLineEdit(page);
    m_watchTitle->setPlaceholderText("标题关键词，可空");
    auto* winRow = new QHBoxLayout;
    winRow->addWidget(new QLabel("进程", page));
    winRow->addWidget(m_watchProcess, 1);
    winRow->addWidget(new QLabel("标题", page));
    winRow->addWidget(m_watchTitle, 1);

    m_watchWindows = new QComboBox(page);
    auto* scan = new QPushButton("刷新窗口", page);
    auto* winRow2 = new QHBoxLayout;
    winRow2->addWidget(new QLabel("窗口", page));
    winRow2->addWidget(m_watchWindows, 1);
    winRow2->addWidget(scan);

    auto* top = new QHBoxLayout;
    m_watchPath = new QLineEdit(page);
    m_watchPath->setReadOnly(true);
    m_watchPath->setPlaceholderText("目标图");
    auto* pick = new QPushButton("选图片", page);
    auto* capture = new QPushButton("截屏框选", page);
    top->addWidget(m_watchPath, 1);
    top->addWidget(pick);
    top->addWidget(capture);

    auto* preview = previewWidget(m_watchPreview, page);

    auto* confRow = new QHBoxLayout;
    m_watchConf = new QSlider(Qt::Horizontal, page);
    m_watchConf->setRange(50, 99);
    m_watchConf->setValue(int(App::instance().cfg().watch.confidence * 100));
    m_watchConfVal = new QLabel(page);
    confRow->addWidget(new QLabel("匹配度", page));
    confRow->addWidget(m_watchConf, 1);
    confRow->addWidget(m_watchConfVal);

    auto* row2 = new QHBoxLayout;
    m_watchInterval = spin(100, 600000, App::instance().cfg().watch.interval, " 毫秒", page);
    m_watchLimitOn = new QCheckBox("只点", page);
    m_watchLimitOn->setChecked(App::instance().cfg().watch.limitEnabled);
    m_watchLimit = spin(1, 99999, App::instance().cfg().watch.limitCount, " 次", page, 1);
    row2->addWidget(new QLabel("检测间隔", page));
    row2->addWidget(m_watchInterval);
    row2->addWidget(m_watchLimitOn);
    row2->addWidget(m_watchLimit);
    row2->addStretch(1);

    m_watchBtn = new QPushButton("开始监视", page);
    m_watchBtn->setMinimumHeight(34);
    m_watchBtn->setStyleSheet("QPushButton{background:#0f766e;color:white;border-radius:6px;font-weight:600;}");
    m_watchState = new QLabel(page);
    m_watchState->setWordWrap(true);
    m_watchState->setStyleSheet("color:#475569;");

    m_watchHk = makeHotkeyRow(page, "watch", App::instance().cfg().watch.hotkey,
                              App::instance().cfg().watch.hotkeyEnabled);

    lay->addLayout(winRow);
    lay->addLayout(winRow2);
    lay->addLayout(top);
    lay->addWidget(preview);
    lay->addLayout(confRow);
    lay->addLayout(row2);
    lay->addWidget(m_watchBtn);
    lay->addWidget(hintLabel("窗口被挡住也能看到；命中后先置顶再点。", page));
    lay->addWidget(m_watchHk.row);
    lay->addWidget(m_watchState);
    lay->addStretch(1);

    connect(scan, &QPushButton::clicked, this, [this] { refreshWindows(); });
    connect(m_watchWindows, &QComboBox::activated, this, [this](int idx) {
        const auto ws = pl::windows();
        if (idx <= 0 || idx - 1 >= ws.size()) return;
        const auto& w = ws[idx - 1];
        m_watchProcess->setText(w.process);
        m_watchTitle->setText(w.title);
        syncConfigFromUi();
    });
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "选目标图片", templatesDirHint(),
                                                          "图片 (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty()) return;
        setTemplateForCurrentTab(path);
    });
    connect(capture, &QPushButton::clicked, this, [this] {
        auto* ov = new CaptureOverlay();
        connect(ov, &CaptureOverlay::captured, this, [this](const QImage& img) {
            const QString path = templatesDirHint() + "/目标_" +
                                 QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
            if (img.save(path, "PNG")) setTemplateForCurrentTab(path);
        });
        connect(ov, &QWidget::destroyed, ov, &QObject::deleteLater);
        ov->show();
        ov->raise();
        ov->activateWindow();
    });
    connect(m_watchBtn, &QPushButton::clicked, this, [] { App::instance().toggleWatch(); });
    connect(m_watchLimitOn, &QCheckBox::toggled, m_watchLimit, &QSpinBox::setEnabled);
    m_watchLimit->setEnabled(m_watchLimitOn->isChecked());
    return page;
}

QWidget* MainWindow::buildRecordPage() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    auto* row1 = new QHBoxLayout;
    m_scriptCombo = new QComboBox(page);
    auto* refresh = new QPushButton("刷新", page);
    auto* del = new QPushButton("删除", page);
    row1->addWidget(new QLabel("脚本", page));
    row1->addWidget(m_scriptCombo, 1);
    row1->addWidget(refresh);
    row1->addWidget(del);
    m_scriptInfo = new QLabel(page);
    m_scriptInfo->setStyleSheet("color:#64748b;");

    auto* row2 = new QHBoxLayout;
    m_speedMode = new QComboBox(page);
    m_speedMode->addItem("原速回放", "real");
    m_speedMode->addItem("固定间隔", "fixed");
    m_speedMode->setCurrentIndex(App::instance().cfg().record.speedMode == "fixed" ? 1 : 0);
    m_fixedInterval = spin(20, 600000, App::instance().cfg().record.fixedInterval, " 毫秒", page);
    row2->addWidget(new QLabel("节奏", page));
    row2->addWidget(m_speedMode);
    row2->addWidget(m_fixedInterval);
    row2->addStretch(1);

    auto* row3 = new QHBoxLayout;
    m_loops = spin(1, 99999, App::instance().cfg().record.loops, " 轮", page, 1);
    m_loopForever = new QCheckBox("一直循环", page);
    m_loopForever->setChecked(App::instance().cfg().record.loopForever);
    row3->addWidget(new QLabel("循环", page));
    row3->addWidget(m_loops);
    row3->addWidget(m_loopForever);
    row3->addStretch(1);

    m_recBtn = new QPushButton("开始录制", page);
    m_recBtn->setMinimumHeight(34);
    m_recBtn->setStyleSheet("QPushButton{background:#b45309;color:white;border-radius:6px;font-weight:600;}");
    m_playBtn = new QPushButton("开始回放", page);
    m_playBtn->setMinimumHeight(34);
    m_playBtn->setStyleSheet("QPushButton{background:#0f766e;color:white;border-radius:6px;font-weight:600;}");
    m_recState = new QLabel(page);
    m_recState->setStyleSheet("color:#475569;");
    m_recState->setWordWrap(true);

    m_recHk = makeHotkeyRow(page, "record", App::instance().cfg().record.hotkey,
                            App::instance().cfg().record.hotkeyEnabled);

    lay->addLayout(row1);
    lay->addWidget(m_scriptInfo);
    lay->addLayout(row2);
    lay->addLayout(row3);
    lay->addWidget(m_recBtn);
    lay->addWidget(m_playBtn);
    lay->addWidget(m_recHk.row);
    lay->addWidget(m_recState);
    lay->addStretch(1);

    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshScripts);
    connect(del, &QPushButton::clicked, this, [this] {
        const int idx = m_scriptCombo->currentIndex();
        if (idx < 0) return;
        const QString path = m_scriptCombo->itemData(idx).toString();
        if (deleteScript(path)) {
            logLine("脚本已删除");
            App::instance().cfg().record.script.clear();
            App::instance().saveCfg();
            refreshScripts();
        }
    });
    connect(m_scriptCombo, &QComboBox::currentIndexChanged, this, [this](int) { syncConfigFromUi(); });
    connect(m_recBtn, &QPushButton::clicked, this, [] { App::instance().toggleRecord(); });
    connect(m_playBtn, &QPushButton::clicked, this, [] { App::instance().toggleReplay(); });
    connect(m_loopForever, &QCheckBox::toggled, m_loops, [this](bool on) {
        m_loops->setEnabled(!on);
        syncConfigFromUi();
    });
    connect(m_speedMode, &QComboBox::currentIndexChanged, this, [this](int) {
        m_fixedInterval->setEnabled(m_speedMode->currentData().toString() == "fixed");
        syncConfigFromUi();
    });
    m_fixedInterval->setEnabled(m_speedMode->currentData().toString() == "fixed");
    m_loops->setEnabled(!m_loopForever->isChecked());
    return page;
}

QWidget* MainWindow::buildLogPage() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 8, 8, 8);
    auto* top = new QHBoxLayout;
    top->addStretch(1);
    auto* clear = new QPushButton("清空", page);
    top->addWidget(clear);
    m_log = new QPlainTextEdit(page);
    m_log->setReadOnly(true);
    m_log->setStyleSheet("font-family:Consolas,Menlo,monospace; font-size:11px;");
    lay->addLayout(top);
    lay->addWidget(m_log, 1);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_log->clear();
        Log::instance().clear();
    });
    return page;
}

MainWindow::HotkeyWidgets MainWindow::makeHotkeyRow(QWidget* parent, const QString& action,
                                                    const QString& current, bool enabled) {
    HotkeyWidgets w;
    Q_UNUSED(action);
    w.row = new QWidget(parent);
    auto* box = new QHBoxLayout(w.row);
    box->setContentsMargins(0, 0, 0, 0);
    w.enabled = new QCheckBox("全局热键开关", w.row);
    w.enabled->setChecked(enabled);
    w.key = new QComboBox(w.row);
    for (int i = 1; i <= 12; ++i) w.key->addItem(QString("f%1").arg(i));
    w.key->addItem("pause");
    int idx = w.key->findText(current);
    if (idx >= 0) w.key->setCurrentIndex(idx);
    w.key->setEnabled(enabled);
    w.key->setFixedWidth(96);
    box->addWidget(w.enabled);
    box->addWidget(w.key);
    box->addStretch(1);
    connect(w.enabled, &QCheckBox::toggled, w.key, &QComboBox::setEnabled);
    connect(w.enabled, &QCheckBox::toggled, this, [this](bool) { syncConfigFromUi(); });
    connect(w.key, &QComboBox::currentIndexChanged, this, [this](int) { syncConfigFromUi(); });
    return w;
}

void MainWindow::bindEdits() {
    const QVector<QObject*> widgets = {
        m_mouseInterval, m_imgInterval, m_imgOffX, m_imgOffY, m_imgLimit, m_watchInterval, m_watchLimit,
        m_fixedInterval, m_loops,
    };
    for (QObject* o : widgets) {
        if (auto* s = qobject_cast<QSpinBox*>(o)) connect(s, &QSpinBox::valueChanged, this, [this](int) { syncConfigFromUi(); });
    }
    if (m_imgConf) connect(m_imgConf, &QSlider::valueChanged, this, [this](int) { syncConfigFromUi(); });
    if (m_watchConf) connect(m_watchConf, &QSlider::valueChanged, this, [this](int) { syncConfigFromUi(); });
    if (m_imgLimitOn) connect(m_imgLimitOn, &QCheckBox::toggled, this, [this](bool) { syncConfigFromUi(); });
    if (m_watchLimitOn) connect(m_watchLimitOn, &QCheckBox::toggled, this, [this](bool) { syncConfigFromUi(); });
    if (m_imgOnMiss) connect(m_imgOnMiss, &QComboBox::currentIndexChanged, this, [this](int) { syncConfigFromUi(); });
    if (m_watchProcess) connect(m_watchProcess, &QLineEdit::editingFinished, this, [this] { syncConfigFromUi(); });
    if (m_watchTitle) connect(m_watchTitle, &QLineEdit::editingFinished, this, [this] { syncConfigFromUi(); });
}

void MainWindow::syncConfigFromUi() {
    if (m_loading) return;
    Config& c = App::instance().cfg();
    c.mouse.interval = m_mouseInterval->value();
    c.mouse.hotkey = m_mouseHk.key->currentText();
    c.mouse.hotkeyEnabled = m_mouseHk.enabled->isChecked();

    c.image.confidence = m_imgConf->value() / 100.0;
    c.image.interval = m_imgInterval->value();
    c.image.offx = m_imgOffX->value();
    c.image.offy = m_imgOffY->value();
    c.image.limitEnabled = m_imgLimitOn->isChecked();
    c.image.limitCount = m_imgLimit->value();
    c.image.onMiss = m_imgOnMiss->currentData().toString();
    c.image.hotkey = m_imgHk.key->currentText();
    c.image.hotkeyEnabled = m_imgHk.enabled->isChecked();

    c.watch.confidence = m_watchConf->value() / 100.0;
    c.watch.interval = m_watchInterval->value();
    c.watch.limitEnabled = m_watchLimitOn->isChecked();
    c.watch.limitCount = m_watchLimit->value();
    c.watch.process = m_watchProcess->text().trimmed();
    c.watch.title = m_watchTitle->text().trimmed();
    c.watch.hotkey = m_watchHk.key->currentText();
    c.watch.hotkeyEnabled = m_watchHk.enabled->isChecked();

    c.record.speedMode = m_speedMode->currentData().toString();
    c.record.fixedInterval = m_fixedInterval->value();
    c.record.loops = m_loops->value();
    c.record.loopForever = m_loopForever->isChecked();
    c.record.hotkey = m_recHk.key->currentText();
    c.record.hotkeyEnabled = m_recHk.enabled->isChecked();
    if (m_scriptCombo->currentIndex() >= 0)
        c.record.script = m_scriptCombo->itemData(m_scriptCombo->currentIndex()).toString();

    m_imgConfVal->setText(QString("%1").arg(c.image.confidence, 0, 'f', 2));
    m_watchConfVal->setText(QString("%1").arg(c.watch.confidence, 0, 'f', 2));

    App::instance().saveCfg();
    App::instance().applyHotkeysNow();
    refreshUi();
}

void MainWindow::setStatusPill(const QString& text, const QString& color) {
    m_pill->setText(text);
    m_pill->setStyleSheet(QString("background:%1;color:white;border-radius:9px;padding:2px 8px;font-size:11px;")
                             .arg(color));
}

void MainWindow::refreshUi() {
    const auto& app = App::instance();
    if (app.recording()) {
        setStatusPill("录制中", "#b45309");
        m_recBtn->setText("停止录制");
    } else if (app.replayRunning()) {
        setStatusPill("回放中", "#0f766e");
        m_playBtn->setText("停止回放");
    } else if (app.mouseRunning()) {
        setStatusPill("连点中", "#0f766e");
        m_mouseBtn->setText("停止连点");
    } else if (app.imageRunning()) {
        setStatusPill("找图中", "#0f766e");
        m_imgBtn->setText("停止找图");
    } else if (app.watchRunning()) {
        setStatusPill("监视中", "#0f766e");
        m_watchBtn->setText("停止监视");
    } else {
        setStatusPill("空闲", "#64748b");
    }

    m_mouseBtn->setText(app.mouseRunning() ? "停止连点" : "开始连点");
    m_imgBtn->setText(app.imageRunning() ? "停止找图" : "开始找图连点");
    m_watchBtn->setText(app.watchRunning() ? "停止监视" : "开始监视");
    m_recBtn->setText(app.recording() ? "停止录制" : "开始录制");
    m_playBtn->setText(app.replayRunning() ? "停止回放" : "开始回放");

    if (m_imgConfVal)
        m_imgConfVal->setText(QString("%1").arg(App::instance().cfg().image.confidence, 0, 'f', 2));
    if (m_watchConfVal)
        m_watchConfVal->setText(QString("%1").arg(App::instance().cfg().watch.confidence, 0, 'f', 2));
    m_clickTypeLabel->setText(QString("当前点击：%1%2").arg(App::instance().cfg().click.type)
                                  .arg(App::instance().cfg().click.dbl ? "（双击）" : ""));

    QString hint;
    if (app.recording())
        hint = "录制中：勾选的「热键开关」可以被其它程序占用，按热键再结束";
    else if (app.mouseRunning() || app.imageRunning() || app.watchRunning())
        hint = "任务运行中，把鼠标留空就能让它点；急停键随时可以叫停";
    else if (App::instance().cfg().emergency.enabled)
        hint = QString("急停键：%1").arg(parseCombo(App::instance().cfg().emergency.hotkey).label());
    else
        hint = "急停键已关闭，可在「设置」里打开";
    m_hint->setText(hint);

    updatePreview();
}

void MainWindow::loadTemplatePreview(QLabel* label, const QString& path) {
    if (!label) return;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        label->setPixmap(QPixmap());
        label->setText("模板预览（也可以直接把图片拖进来）");
        return;
    }
    QPixmap pm(path);
    if (pm.isNull()) {
        label->setText("图片读不出来");
        return;
    }
    label->setText(QString());
    label->setPixmap(pm.scaled(label->width() > 0 ? label->width() : 320, 56, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation));
}

void MainWindow::updatePreview() {
    const Config& c = App::instance().cfg();
    m_imgPath->setText(QFileInfo(c.image.tmpl).fileName());
    m_watchPath->setText(QFileInfo(c.watch.tmpl).fileName());
    loadTemplatePreview(m_imgPreview, c.image.tmpl);
    loadTemplatePreview(m_watchPreview, c.watch.tmpl);
    m_scriptInfo->setText(c.record.script.isEmpty() ? "还没有选脚本" : QFileInfo(c.record.script).fileName());
}

void MainWindow::setTemplateForCurrentTab(const QString& path) {
    // 统一拷到 templates 目录，避免原图被移动/删除后失效
    QString dest = path;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (!path.startsWith(Config::templatesDir())) {
        dest = Config::templatesDir() + "/" +
               (m_tabs->currentIndex() == 2 ? "目标_" : "模板_") +
               QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "." + (ext.isEmpty() ? "png" : ext);
        if (!QFile::copy(path, dest)) dest = path;
    }
    if (m_tabs->currentIndex() == 2) {
        App::instance().cfg().watch.tmpl = dest;
        logLine(QString("目标图已设置：%1").arg(QFileInfo(dest).fileName()));
    } else {
        App::instance().cfg().image.tmpl = dest;
        logLine(QString("模板图已设置：%1").arg(QFileInfo(dest).fileName()));
    }
    App::instance().saveCfg();
    refreshUi();
}

void MainWindow::refreshScripts() {
    const QSignalBlocker block(m_scriptCombo);
    const QString keep = App::instance().cfg().record.script;
    m_scriptCombo->clear();
    const auto scripts = listScripts();
    for (const Script& s : scripts) {
        m_scriptCombo->addItem(s.name, s.path);
    }
    int idx = m_scriptCombo->findData(keep);
    if (idx < 0 && m_scriptCombo->count() > 0) idx = 0;
    if (idx >= 0) m_scriptCombo->setCurrentIndex(idx);
    if (idx >= 0) {
        App::instance().cfg().record.script = m_scriptCombo->itemData(idx).toString();
        Script sc;
        if (loadScript(App::instance().cfg().record.script, &sc))
            m_scriptInfo->setText(QString("%1 · %2").arg(actionSummary(sc.events)).arg(sc.created));
    }
}

void MainWindow::refreshWindows() {
    m_watchWindows->clear();
    m_watchWindows->addItem("（从下面选一个窗口）");
    for (const pl::WindowInfo& w : pl::windows()) {
        m_watchWindows->addItem(QString("%1 — %2").arg(w.title).arg(w.process));
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    Config& c = App::instance().cfg();
    c.ui.w = width();
    c.ui.h = height();
    c.ui.tab = m_tabs->currentIndex();
    App::instance().saveCfg();
    App::instance().stopAll(true);
    QMainWindow::closeEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& u : e->mimeData()->urls()) {
            const QString sfx = QFileInfo(u.toLocalFile()).suffix().toLower();
            if (QStringList{"png", "jpg", "jpeg", "bmp", "webp"}.contains(sfx)) {
                e->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (path.isEmpty()) continue;
        const QString sfx = QFileInfo(path).suffix().toLower();
        if (!QStringList{"png", "jpg", "jpeg", "bmp", "webp"}.contains(sfx)) continue;
        setTemplateForCurrentTab(path);
        e->acceptProposedAction();
        return;
    }
}

QString MainWindow::templatesDirHint() { return Config::templatesDir(); }

}  // namespace ac
