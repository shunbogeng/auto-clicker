#include "core/Task.h"

#include <QThread>
#include <chrono>

namespace ac {

Task::Task(QObject* parent) : QObject(parent) {}

Task::~Task() {
    stop();
    if (m_thr.joinable()) m_thr.join();
}

void Task::launch() {
    m_stop = false;
    m_running = true;
    m_thr = std::thread([this] {
        main();
        m_running = false;
        emit finished();
    });
}

void Task::stop() { m_stop = true; }

void Task::waitDone(int ms) {
    if (!m_thr.joinable()) return;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (m_running.load() && std::chrono::steady_clock::now() < deadline) QThread::msleep(5);
}

void Task::sleepMs(int ms) {
    if (ms <= 0) return;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!stopped()) {
        if (std::chrono::steady_clock::now() >= end) break;
        QThread::msleep(5);
    }
}

}  // namespace ac
