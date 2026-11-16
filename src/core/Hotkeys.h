// 全局热键：整个程序只有一个热键，按下就启动/停止「当前页签」的功能。
#pragma once

#include <QObject>
#include <QString>
#include "core/Config.h"

namespace ac {

class Hotkeys : public QObject {
    Q_OBJECT
public:
    explicit Hotkeys(QObject* parent = nullptr);

    bool start();                   // 建立监听（内部用平台热键）
    void apply(const Config& cfg);   // 按配置重新注册
    void stop();

signals:
    void triggered();

private:
    bool m_started = false;
};

}  // namespace ac
