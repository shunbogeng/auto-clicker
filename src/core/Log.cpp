#include "core/Log.h"

#include <QDateTime>

namespace ac {

Log& Log::instance() {
    static Log inst;
    return inst;
}

void Log::add(const QString& text) {
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString line = QString("[%1] %2").arg(stamp, text);
    {
        QMutexLocker lock(&m_mu);
        m_lines.append(line);
        if (m_lines.size() > 20000) m_lines.removeFirst();
    }
    emit this->line(line);
}

QStringList Log::history() const {
    QMutexLocker lock(&m_mu);
    return m_lines;
}

void Log::clear() {
    QMutexLocker lock(&m_mu);
    m_lines.clear();
}

}  // namespace ac
