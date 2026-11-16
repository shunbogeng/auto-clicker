// 全局日志总线：任何线程都可以写，界面按行接收。
#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QStringList>

namespace ac {

class Log : public QObject {
    Q_OBJECT
public:
    static Log& instance();

    void add(const QString& text);           // 任意线程调用
    QStringList history() const;
    void clear();

signals:
    void line(const QString& text);

private:
    Log() = default;
    mutable QMutex m_mu;
    QStringList m_lines;
};

inline void logLine(const QString& t) { Log::instance().add(t); }

}  // namespace ac
