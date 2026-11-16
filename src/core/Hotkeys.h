// 全局热键注册表：把「动作名 <-> 槽位 <-> 组合键」对上，按配置启用/禁用。
#pragma once

#include <QObject>
#include <QString>
#include "core/Config.h"

namespace ac {

class Hotkeys : public QObject {
    Q_OBJECT
public:
    explicit Hotkeys(QObject* parent = nullptr);
    static int slotFor(const QString& action);   // mouse=1 image=2 watch=3 record=4 emergency=5

    bool start();                 // 建立监听（内部用平台热键）
    void apply(const Config& cfg);  // 按配置把所有键重新注册
    void stop();

signals:
    void triggered(const QString& action);

private:
    bool m_started = false;
    QString m_slotAction[8];
};

}  // namespace ac
