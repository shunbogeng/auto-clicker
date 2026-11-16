#include "core/MouseTask.h"

namespace ac {

MouseTask::MouseTask(int intervalMs, int button, bool dbl, int vkKey, QObject* parent)
    : Task(parent), m_interval(qMax(1, intervalMs)), m_button(button), m_dbl(dbl), m_vkKey(vkKey) {}

void MouseTask::main() {
    while (!stopped()) {
        if (m_vkKey > 0) {
            pl::keyTap(m_vkKey);
        } else {
            pl::click(m_button, m_dbl);
        }
        addClicks(1);
        sleepMs(m_interval);
    }
}

}  // namespace ac
