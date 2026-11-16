// 任务基类：内部开一条 std::thread 干活，用原子标志停止，界面侧只收信号。
#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

namespace ac {

class Task : public QObject {
    Q_OBJECT
public:
    explicit Task(QObject* parent = nullptr);
    ~Task() override;

    virtual void stop();               // 可跨线程调用
    bool running() const { return m_running.load(); }
    int clicks() const { return m_clicks.load(); }
    void waitDone(int ms = 4000);
    void launch();                     // 启动线程（派生任务构造好后调用）

signals:
    void note(const QString& text);
    void clicksChanged(int total);
    void finished();

protected:
    virtual void main() = 0;           // 子线程本体
    bool stopped() const { return m_stop.load(); }
    void sleepMs(int ms);              // 可被 stop 打断的等待
    void addClicks(int n) { emit clicksChanged((m_clicks += n)); }

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<int> m_clicks{0};
    std::thread m_thr;
};

}  // namespace ac
