// 鼠标连点：在光标当前位置按间隔点击，直到被停止。
#pragma once

#include "core/Task.h"
#include "platform/Platform.h"

namespace ac {

class MouseTask : public Task {
    Q_OBJECT
public:
    MouseTask(int intervalMs, int button, bool dbl, int vkKey = 0, QObject* parent = nullptr);

protected:
    void main() override;

private:
    int m_interval;
    int m_button;
    bool m_dbl;
    int m_vkKey;   // >0 时改为按这个键
};

}  // namespace ac
