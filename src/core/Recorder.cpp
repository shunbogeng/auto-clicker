#include "core/Recorder.h"

#include "core/Config.h"
#include "core/Keys.h"
#include "core/Log.h"
#include "platform/Platform.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>

namespace ac {

// ---------------------------------------------------------------------------
QString sanitizeScriptName(const QString& raw) {
    QString s = raw;
    s.replace(QRegularExpression("[\\\\/:*?\"<>|\\r\\n\\t]"), "_");
    s = s.trimmed();
    while (s.endsWith('.') || s.endsWith(' ')) s.chop(1);
    if (s.left(1) == ".") s = "_" + s.mid(1);
    if (s.length() > 40) s = s.left(40);
    if (s.isEmpty()) s = "脚本";
    return s;
}

QString saveScript(const QString& name, const QVector<pl::RecEvent>& events, const QSize& screen) {
    const QString clean = sanitizeScriptName(name);
    QDir().mkpath(Config::scriptsDir());
    QString path = Config::scriptsDir() + "/" + clean + ".json";
    QJsonArray arr;
    for (const pl::RecEvent& e : events) {
        QJsonArray one;
        one << e.kind << e.t;
        switch (e.kind) {
            case pl::E_Down:
            case pl::E_Up: one << e.x << e.y << e.btn; break;
            case pl::E_Move: one << e.x << e.y; break;
            case pl::E_Wheel: one << e.x << e.y << e.steps; break;
            case pl::E_KDown:
            case pl::E_KUp: one << e.vk << 0 << (e.ext ? 1 : 0); break;
            default: one << e.x << e.y; break;
        }
        arr.append(one);
    }
    QJsonObject root{
        {"app", "auto-clicker"},
        {"version", 1},
        {"name", clean},
        {"created", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")},
        {"screen", QJsonArray{screen.width(), screen.height()}},
        {"events", arr},
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();
    return path;
}

bool loadScript(const QString& path, Script* out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    QJsonObject root = doc.object();
    if (!root.value("events").isArray()) return false;

    Script s;
    s.path = path;
    s.name = root.value("name").toString(QFileInfo(path).completeBaseName());
    s.created = root.value("created").toString();
    QJsonArray sc = root.value("screen").toArray();
    if (sc.size() == 2) s.screen = QSize(sc.at(0).toInt(), sc.at(1).toInt());

    for (const QJsonValue& v : root.value("events").toArray()) {
        QJsonArray a = v.toArray();
        if (a.size() < 2) continue;
        pl::RecEvent e;
        e.kind = a.at(0).toInt();
        e.t = a.at(1).toInt();
        switch (e.kind) {
            case pl::E_Down:
            case pl::E_Up:
                if (a.size() >= 5) {
                    e.x = a.at(2).toInt();
                    e.y = a.at(3).toInt();
                    e.btn = a.at(4).toInt();
                }
                break;
            case pl::E_Move:
                if (a.size() >= 4) {
                    e.x = a.at(2).toInt();
                    e.y = a.at(3).toInt();
                }
                break;
            case pl::E_Wheel:
                if (a.size() >= 5) {
                    e.x = a.at(2).toInt();
                    e.y = a.at(3).toInt();
                    e.steps = a.at(4).toInt();
                }
                break;
            case pl::E_KDown:
            case pl::E_KUp:
                if (a.size() >= 3) e.vk = a.at(2).toInt();
                if (a.size() >= 5) e.ext = a.at(4).toInt() != 0;
                break;
            default:
                continue;
        }
        s.events.append(e);
    }
    *out = s;
    return true;
}

QVector<Script> listScripts() {
    QVector<Script> out;
    QDir dir(Config::scriptsDir());
    if (!dir.exists()) return out;
    QFileInfoList files = dir.entryInfoList(QStringList{"*.json"}, QDir::Files, QDir::Time);
    for (const QFileInfo& fi : files) {
        Script s;
        s.name = fi.completeBaseName();
        s.path = fi.absoluteFilePath();
        s.created = fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
        out.append(s);
    }
    return out;
}

bool deleteScript(const QString& path) {
    if (path.isEmpty()) return false;
    return QFile::remove(path);
}

QString actionSummary(const QVector<pl::RecEvent>& events) {
    int acts = 0;
    for (const pl::RecEvent& e : events) {
        if (e.kind == pl::E_Down || e.kind == pl::E_Wheel || e.kind == pl::E_KDown) ++acts;
    }
    const double dur = events.isEmpty() ? 0.0 : events.last().t / 1000.0;
    return QString("%1 个动作（点击/滚轮/按键 %2 次）· %3 秒")
        .arg(events.size())
        .arg(acts)
        .arg(dur, 0, 'f', 1);
}

// ---------------------------------------------------------------------------
Recorder::Recorder(QObject* parent) : QObject(parent) {}

Recorder::~Recorder() {
    if (m_recording) pl::removeHook();
}

bool Recorder::start() {
    if (m_recording) return true;
    {
        QMutexLocker lock(&m_mu);
        m_events.clear();
    }
    m_clock.start();
    m_recording = true;
    if (!pl::installHook([this](const pl::RecEvent& e) { onEvent(e); })) {
        m_recording = false;
        return false;
    }
    return true;
}

void Recorder::onEvent(const pl::RecEvent& ev) {
    QMutexLocker lock(&m_mu);
    if (!m_recording) return;
    pl::RecEvent e = ev;
    e.t = int(m_clock.elapsed());
    m_events.append(e);
    const int n = m_events.size();
    lock.unlock();
    emit countChanged(n);
}

QVector<pl::RecEvent> Recorder::stop() {
    if (!m_recording) return {};
    m_recording = false;
    pl::removeHook();
    QMutexLocker lock(&m_mu);
    return m_events;
}

int Recorder::count() {
    QMutexLocker lock(&m_mu);
    return m_events.size();
}

int Recorder::elapsedMs() const { return int(m_clock.elapsed()); }

// ---------------------------------------------------------------------------
Player::Player(QVector<pl::RecEvent> events, QSize screen, QString speedMode, int fixedIntervalMs, int loops,
               bool loopForever, QObject* parent)
    : Task(parent),
      m_events(std::move(events)),
      m_screen(std::move(screen)),
      m_mode(std::move(speedMode)),
      m_fixed(qMax(20, fixedIntervalMs)),
      m_loops(qMax(1, loops)),
      m_forever(loopForever) {}

void Player::main() {
    if (m_events.isEmpty()) {
        emit note("脚本里没有动作");
        return;
    }
    const QSize now = pl::screenSize();
    if (m_screen.isValid() && !m_screen.isEmpty() && now != m_screen) {
        emit note(QString("注意：脚本录于 %1x%2，当前屏幕 %3x%4，位置可能对不上，建议重录")
                      .arg(m_screen.width()).arg(m_screen.height())
                      .arg(now.width()).arg(now.height()));
    }

    int held = -1;
    for (int loop = 1; !stopped(); ++loop) {
        QElapsedTimer clock;
        clock.start();
        int lastT = 0;
        for (const pl::RecEvent& e : m_events) {
            if (stopped()) break;
            if (m_mode == "real") {
                while (!stopped() && clock.elapsed() < e.t) QThread::msleep(2);
                lastT = e.t;
            } else {
                const bool action = e.kind == pl::E_Down || e.kind == pl::E_Wheel || e.kind == pl::E_KDown;
                if (action || e.kind == pl::E_Up) sleepMs(m_fixed);
            }
            QPoint pt(e.x, e.y);
            switch (e.kind) {
                case pl::E_Move:
                    if (m_mode == "fixed") sleepMs(2);
                    pl::moveTo(pt);
                    break;
                case pl::E_Down:
                    pl::moveTo(pt);
                    pl::mouseDown(e.btn);
                    held = e.btn;
                    addClicks(1);
                    break;
                case pl::E_Up:
                    pl::moveTo(pt);
                    pl::mouseUp(e.btn);
                    if (held == e.btn) held = -1;
                    break;
                case pl::E_Wheel:
                    pl::moveTo(pt);
                    pl::wheel(e.steps);
                    addClicks(1);
                    break;
                case pl::E_KDown:
                    pl::key(e.vk, false, e.ext);
                    addClicks(1);
                    break;
                case pl::E_KUp:
                    pl::key(e.vk, true, e.ext);
                    break;
                default:
                    break;
            }
        }
        if (held >= 0) {
            pl::mouseUp(held);  // 收尾：别把按键一直按着
            held = -1;
        }
        Q_UNUSED(lastT);
        if (!m_forever && loop >= m_loops) break;
        sleepMs(250);
    }
    emit note("回放结束");
}

}  // namespace ac
