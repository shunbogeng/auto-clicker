#include "core/App.h"

#include "core/Hotkeys.h"
#include "core/ImageMatch.h"
#include "core/Keys.h"
#include "core/Log.h"
#include "core/MouseTask.h"
#include "core/TargetTask.h"

#include <QApplication>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <cmath>

namespace ac {

namespace {
const char* kServerName = "auto-clicker-single-instance-v2";
}

App& App::instance() {
    static App inst;
    return inst;
}

bool App::init() {
    if (m_ready) return true;

    if (!pl::init()) {
        QMessageBox::critical(nullptr, "连点器",
                              "连点器：无法连接显示服务器。\n"
                              "Linux 下请确认在 X11 会话里运行（Wayland 会限制截屏与模拟输入）。");
        return false;
    }

    m_cfg = Config::load();

    // 单实例：先看有没有人在听；能连上就把 "show" 发过去然后自己退出
    QLocalSocket probe;
    probe.connectToServer(kServerName);
    if (probe.waitForConnected(400)) {
        probe.write("show\n");
        probe.flush();
        probe.waitForBytesWritten(400);
        probe.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(kServerName);  // 上次异常退出留下的 socket 文件
    m_server = new QLocalServer(this);
    if (!m_server->listen(kServerName)) {
        logLine(QString("单实例服务监听失败：%1（可能是权限问题）").arg(m_server->errorString()));
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (m_server->hasPendingConnections()) {
            QLocalSocket* s = m_server->nextPendingConnection();
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                const QByteArray data = s->readAll();
                if (data.contains("show")) {
                    logLine("检测到重复启动，已把窗口切到前台");
                    emit requestShowWindow();
                }
            });
            connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        }
    });

    m_hotkeys = new Hotkeys(this);
    connect(m_hotkeys, &Hotkeys::triggered, this, &App::onGlobalHotkey);
    applyHotkeysNow();

    logLine(QString("连点器启动：平台 %1，窗口 %2x%3")
                .arg(pl::backendName())
                .arg(pl::screenSize().width())
                .arg(pl::screenSize().height()));
    m_ready = true;
    return true;
}

void App::saveCfg() {
    m_cfg.save();
}

void App::applyHotkeysNow() {
    if (m_hotkeys) m_hotkeys->apply(m_cfg);
}

// ---------------------------------------------------------------------------
bool App::mouseRunning() const { return m_mouse && m_mouse->running(); }
bool App::imageRunning() const { return m_image && m_image->running(); }
bool App::watchRunning() const { return m_watch && m_watch->running(); }
bool App::replayRunning() const { return m_player && m_player->running(); }
bool App::recording() const { return m_rec && m_rec->recording(); }
int App::mouseClicks() const { return m_mouse ? m_mouse->clicks() : 0; }
int App::imageClicks() const { return m_image ? m_image->clicks() : 0; }
int App::watchClicks() const { return m_watch ? m_watch->clicks() : 0; }
int App::replayClicks() const { return m_player ? m_player->clicks() : 0; }
int App::recordCount() const { return m_rec ? m_rec->count() : 0; }
int App::recordElapsedMs() const { return m_rec ? m_rec->elapsedMs() : 0; }

QString App::clickTypeText() const {
    if (m_cfg.click.type == "key") return QString("键盘 %1").arg(parseCombo(m_cfg.click.key).label());
    return pl::buttonName(pl::buttonFromName(m_cfg.click.type));
}

void App::stopPointerTasks() {
    if (m_mouse) {
        m_mouse->stop();
        m_mouse->waitDone(1500);
        delete m_mouse;
        m_mouse = nullptr;
    }
    if (m_image) {
        m_image->stop();
        m_image->waitDone(1500);
        delete m_image;
        m_image = nullptr;
    }
    if (m_watch) {
        m_watch->stop();
        m_watch->waitDone(1500);
        delete m_watch;
        m_watch = nullptr;
    }
    if (m_player) {
        m_player->stop();
        m_player->waitDone(1500);
        delete m_player;
        m_player = nullptr;
    }
}

void App::toggleMouse() {
    if (recording()) {
        logLine("正在录制，先结束录制再启动鼠标连点");
        return;
    }
    if (replayRunning()) {
        logLine("正在回放，先停止回放再启动鼠标连点");
        return;
    }
    if (mouseRunning()) {
        m_mouse->stop();
        logLine("鼠标连点已停止");
        emit stateChanged();
        return;
    }
    stopPointerTasks();
    int btn = pl::BtnLeft;
    int vk = 0;
    if (m_cfg.click.type == "key") {
        vk = keyVk(m_cfg.click.key);
        if (!vk) {
            logLine("点击类型是键盘按键，但按键没设置好");
            return;
        }
    } else {
        btn = pl::buttonFromName(m_cfg.click.type);
        if (btn < 0) btn = pl::BtnLeft;
    }
    m_mouse = new MouseTask(m_cfg.mouse.interval, btn, m_cfg.click.dbl, vk, this);
    connect(m_mouse, &Task::note, this, [](const QString& t) { logLine(t); });
    connect(m_mouse, &Task::finished, this, &App::stateChanged);
    m_mouse->launch();
    logLine(QString("鼠标连点已启动：间隔 %1 ms，点击 %2%3")
                .arg(m_cfg.mouse.interval)
                .arg(clickTypeText(), m_cfg.click.dbl ? "（双击）" : ""));
    emit stateChanged();
}

void App::toggleImage() {
    if (recording()) {
        logLine("正在录制，先结束录制再启动找图连点");
        return;
    }
    if (replayRunning()) {
        logLine("正在回放，先停止回放再启动找图连点");
        return;
    }
    if (imageRunning()) {
        m_image->stop();
        logLine("找图连点已停止");
        emit stateChanged();
        return;
    }
    TargetParams p;
    p.watch = false;
    p.tmpl = QImage(m_cfg.image.tmpl);
    p.confidence = m_cfg.image.confidence;
    p.interval = m_cfg.image.interval;
    p.offx = m_cfg.image.offx;
    p.offy = m_cfg.image.offy;
    p.multi = m_cfg.image.multi;
    p.limitEnabled = m_cfg.image.limitEnabled;
    p.limitCount = m_cfg.image.limitCount;
    p.onMiss = m_cfg.image.onMiss;
    {
        int b = pl::buttonFromName(m_cfg.click.type);
        p.button = b < 0 ? pl::BtnLeft : b;   // 键盘模式不支持，退化成左键
    }
    p.dbl = m_cfg.click.dbl;

    stopPointerTasks();
    m_image = new TargetTask(p, this);
    connect(m_image, &Task::note, this, [](const QString& t) { logLine(t); });
    connect(m_image, &Task::finished, this, &App::stateChanged);
    m_image->launch();
    logLine(QString("找图连点已启动：阈值 %1，间隔 %2 ms").arg(p.confidence, 0, 'f', 2).arg(p.interval));
    emit stateChanged();
}

void App::toggleWatch() {
    if (recording()) {
        logLine("正在录制，先结束录制再启动监视窗口");
        return;
    }
    if (replayRunning()) {
        logLine("正在回放，先停止回放再启动监视窗口");
        return;
    }
    if (watchRunning()) {
        m_watch->stop();
        logLine("监视窗口已停止");
        emit stateChanged();
        return;
    }
    TargetParams p;
    p.watch = true;
    p.tmpl = QImage(m_cfg.watch.tmpl);
    p.confidence = m_cfg.watch.confidence;
    p.interval = m_cfg.watch.interval;
    p.offx = m_cfg.watch.offx;
    p.offy = m_cfg.watch.offy;
    p.multi = false;
    p.limitEnabled = m_cfg.watch.limitEnabled;
    p.limitCount = m_cfg.watch.limitCount;
    p.onMiss = m_cfg.watch.onMiss;
    p.process = m_cfg.watch.process;
    p.title = m_cfg.watch.title;
    p.button = pl::BtnLeft;
    p.dbl = m_cfg.click.dbl;

    stopPointerTasks();
    m_watch = new TargetTask(p, this);
    connect(m_watch, &Task::note, this, [](const QString& t) { logLine(t); });
    connect(m_watch, &Task::finished, this, &App::stateChanged);
    m_watch->launch();
    logLine(QString("监视窗口已启动：目标 %1，间隔 %2 ms")
                .arg(p.process.isEmpty() ? p.title : p.process)
                .arg(p.interval));
    emit stateChanged();
}

void App::toggleRecord() {
    if (recording()) {
        m_pending = m_rec->stop();
        const int n = m_pending.size();
        if (n == 0) {
            logLine("这次没有录到任何操作（点在连点器自己的窗口上不会录）");
            emit stateChanged();
            return;
        }
        m_pendingName = QString("脚本_%1").arg(QDateTime::currentDateTime().toString("MMdd_HHmmss"));
        logLine(QString("录制结束：%1").arg(actionSummary(m_pending)));
        emit recordFinished();
        emit stateChanged();
        return;
    }
    if (replayRunning()) {
        m_player->stop();
        m_player->waitDone(1200);
    }
    stopPointerTasks();
    if (!m_rec) {
        m_rec = new Recorder(this);
        connect(m_rec, &Recorder::countChanged, this, [this](int) { emit stateChanged(); });
    }
    if (!m_rec->start()) {
        logLine("录制没能启动（这个平台/会话不支持全局钩子）");
        emit stateChanged();
        return;
    }
    logLine("开始录制：照平常操作一遍，再按一次录制热键结束");
    emit stateChanged();
}

void App::toggleReplay() {
    if (recording()) {
        logLine("正在录制，先结束录制再回放");
        return;
    }
    if (replayRunning()) {
        m_player->stop();
        logLine("回放已停止");
        emit stateChanged();
        return;
    }
    if (m_cfg.record.script.isEmpty()) {
        logLine("还没有选脚本：先在「录制回放」页录一段，或从下拉里选一个");
        return;
    }
    Script sc;
    if (!loadScript(m_cfg.record.script, &sc) || sc.events.isEmpty()) {
        logLine("脚本读取失败或里面没有动作");
        return;
    }
    stopPointerTasks();
    if (m_player) {
        m_player->stop();
        m_player->waitDone(1200);
    }
    m_player = new Player(sc.events, sc.screen, m_cfg.record.speedMode, m_cfg.record.fixedInterval,
                          m_cfg.record.loops, m_cfg.record.loopForever, this);
    connect(m_player, &Task::note, this, [](const QString& t) { logLine(t); });
    connect(m_player, &Task::finished, this, &App::stateChanged);
    m_player->launch();
    logLine(QString("回放已启动：%1，%2")
                .arg(sc.name)
                .arg(m_cfg.record.speedMode == "real"
                         ? "原速"
                         : QString("固定间隔 %1 ms").arg(m_cfg.record.fixedInterval)));
    emit stateChanged();
}

void App::saveRecording(const QString& name, const QVector<pl::RecEvent>& events) {
    if (events.isEmpty()) return;
    const QString path = saveScript(name, events, pl::screenSize());
    if (path.isEmpty()) {
        logLine("脚本保存失败（目录写不进去？）");
        return;
    }
    m_cfg.record.script = path;
    m_cfg.save();
    logLine(QString("录制完成：%1，已保存为「%2」").arg(actionSummary(events), QFileInfo(path).fileName()));
    m_pending.clear();
    emit stateChanged();
}

void App::discardRecording() {
    if (m_pending.isEmpty()) return;
    logLine("这次录制已丢弃");
    m_pending.clear();
    emit stateChanged();
}

QString App::probeMatch(bool watch) {
    const QString tmplPath = watch ? m_cfg.watch.tmpl : m_cfg.image.tmpl;
    const double conf = watch ? m_cfg.watch.confidence : m_cfg.image.confidence;
    QImage tmpl(tmplPath);
    if (tmpl.isNull()) return "模板图片无效或未设置";

    QImage frame;
    if (watch) {
        quintptr hw = pl::findWindow(m_cfg.watch.process, m_cfg.watch.title);
        if (!hw) return "还没找到目标窗口（先确认窗口开着、进程名/标题填对）";
        frame = pl::grabWindow(hw);
        if (frame.isNull()) return "取不到目标窗口的画面（最小化或不允许截取）";
    } else {
        frame = pl::grabScreen(pl::virtualScreen());
    }
    MatchOutcome mo = findTemplate(frame, tmpl, conf, false, 1);
    if (mo.tooFlat) return mo.reason;
    if (mo.hits.isEmpty()) return QString("没命中，最高匹配度 %1（阈值 %2）").arg(mo.best, 0, 'f', 3).arg(conf, 0, 'f', 2);
    const MatchHit& h = mo.hits.first();
    QPoint center = h.center;
    if (watch) center += pl::windowRect(pl::findWindow(m_cfg.watch.process, m_cfg.watch.title)).topLeft();
    return QString("命中，匹配度 %1，位置 (%2, %3)").arg(h.score, 0, 'f', 3).arg(center.x()).arg(center.y());
}

void App::setActiveTab(int index) { m_activeTab = index; }

void App::onGlobalHotkey() { toggleActive(); }

// 全局热键的作用对象是「当前页签」：
// 鼠标页 -> 鼠标连点；找图页 -> 找图连点；监视页 -> 监视；录制页 -> 录制/回放。
void App::toggleActive() {
    switch (m_activeTab) {
        case 0: toggleMouse(); break;
        case 1: toggleImage(); break;
        case 2: toggleWatch(); break;
        case 3:
            if (recording()) toggleRecord();
            else if (replayRunning()) toggleReplay();
            else toggleRecord();
            break;
        default:
            logLine("日志页没有可切换的功能，切到前四个页签再用全局热键");
            break;
    }
}

void App::stopAll(bool quiet) {
    if (!quiet) logLine("停止所有任务");
    if (recording()) {
        m_pending = m_rec->stop();
        if (!m_pending.isEmpty()) {
            const QString name = QString("脚本_中断_%1").arg(QDateTime::currentDateTime().toString("MMdd_HHmmss"));
            saveRecording(name, m_pending);
            m_pending.clear();
        }
    }
    stopPointerTasks();
    emit stateChanged();
}

QSize App::suggestWindowSize() {
    QScreen* scr = QApplication::primaryScreen();
    if (!scr) return QSize(420, 340);
    const QRect g = scr->availableGeometry();
    // 约屏幕面积的 1/16，宽度略大于高度，看起来不占地方
    const double target = double(g.width()) * g.height() / 16.0;
    const double ratio = 1.19;
    int w = qBound(360, int(std::sqrt(target / ratio)), 560);
    int h = qBound(410, int(w * ratio), 620);
    return QSize(w, h);
}

}  // namespace ac
