// 业务总控：配置、任务、热键、单实例都收在这里，界面只调用这里的方法。
#pragma once

#include <QObject>
#include <QSize>
#include <QVector>

#include "core/Config.h"
#include "core/Recorder.h"
#include "platform/Platform.h"

class QLocalServer;

namespace ac {

class Task;
class MouseTask;
class TargetTask;
class Hotkeys;

class App : public QObject {
    Q_OBJECT
public:
    static App& instance();

    bool init();          // 平台初始化 + 配置 + 热键 + 单实例；false 表示已有实例在跑，应退出
    Config& cfg() { return m_cfg; }
    void saveCfg();
    void applyHotkeysNow();   // 配置改了之后重挂热键

    // 状态
    bool mouseRunning() const;
    bool imageRunning() const;
    bool watchRunning() const;
    bool replayRunning() const;
    bool recording() const;
    int mouseClicks() const;
    int imageClicks() const;
    int watchClicks() const;
    int replayClicks() const;
    int recordCount() const;
    int recordElapsedMs() const;

    // 命令
    void toggleMouse();
    void toggleImage();
    void toggleWatch();
    void toggleRecord();
    void toggleReplay();
    void stopAll(bool quiet = false);      // 停掉所有任务
    void setActiveTab(int index);          // 界面切换页签时同步，全局热键按它决定作用对象
    void toggleActive();                   // 启动/停止当前页签的功能

    // 录制结果
    void saveRecording(const QString& name, const QVector<pl::RecEvent>& events);
    void discardRecording();
    QVector<pl::RecEvent> pendingRecordEvents() const { return m_pending; }

    // 找图/监视的即时测试（不点击）
    QString probeMatch(bool watch);        // 返回人看的结论

    // 尺寸建议
    static QSize suggestWindowSize();

signals:
    void stateChanged();
    void recordFinished();                 // 有录制结果待命名
    void requestShowWindow();              // 第二个实例请求把窗口提到前面

private:
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void stopPointerTasks();
    void onGlobalHotkey();
    QString clickTypeText() const;

    Config m_cfg;
    bool m_ready = false;
    int m_activeTab = 0;

    MouseTask* m_mouse = nullptr;
    TargetTask* m_image = nullptr;
    TargetTask* m_watch = nullptr;
    Player* m_player = nullptr;
    Recorder* m_rec = nullptr;
    Hotkeys* m_hotkeys = nullptr;
    QLocalServer* m_server = nullptr;

    QVector<pl::RecEvent> m_pending;
    QString m_pendingName;
};

}  // namespace ac
