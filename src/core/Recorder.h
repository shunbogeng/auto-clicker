// 录制 / 回放 / 脚本文件读写。
#pragma once

#include "core/Task.h"
#include "platform/Platform.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QString>
#include <QMutex>
#include <QVector>

namespace ac {

// ---------------- 脚本 ----------------
struct Script {
    QString name;
    QString path;
    QString created;
    QSize screen;
    QVector<pl::RecEvent> events;
};

QString sanitizeScriptName(const QString& raw);
QString saveScript(const QString& name, const QVector<pl::RecEvent>& events, const QSize& screen);
bool loadScript(const QString& path, Script* out);
QVector<Script> listScripts();          // 按修改时间倒序，最近录的在前
bool deleteScript(const QString& path);
QString actionSummary(const QVector<pl::RecEvent>& events);  // "9 个动作（点击/滚轮/按键 4 次）"

// ---------------- 录制 ----------------
class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    bool start();
    QVector<pl::RecEvent> stop();
    bool recording() const { return m_recording; }
    int count();
    int elapsedMs() const;

signals:
    void countChanged(int n);

private:
    void onEvent(const pl::RecEvent& ev);

    QMutex m_mu;
    QVector<pl::RecEvent> m_events;
    bool m_recording = false;
    QElapsedTimer m_clock;
};

// ---------------- 回放 ----------------
class Player : public Task {
    Q_OBJECT
public:
    Player(QVector<pl::RecEvent> events, QSize screen, QString speedMode, int fixedIntervalMs, int loops,
           bool loopForever, QObject* parent = nullptr);

protected:
    void main() override;

private:
    QVector<pl::RecEvent> m_events;
    QSize m_screen;
    QString m_mode;
    int m_fixed;
    int m_loops;
    bool m_forever;
};

}  // namespace ac
