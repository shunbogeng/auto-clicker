#include "core/Hotkeys.h"

#include "core/Keys.h"
#include "core/Log.h"
#include "platform/Platform.h"

namespace ac {

namespace {
const int kSlot = 1;
}

Hotkeys::Hotkeys(QObject* parent) : QObject(parent) {}

bool Hotkeys::start() {
    if (m_started) return true;
    bool ok = pl::startHotkeys([this](int slot) {
        if (slot == kSlot) emit triggered();
    });
    if (!ok) logLine("全局热键监听没能建立（这个平台可能不支持）");
    m_started = ok;
    return ok;
}

void Hotkeys::apply(const Config& cfg) {
    if (!start()) return;
    const QString combo = cfg.hotkey.enabled ? cfg.hotkey.key : QString();
    pl::setHotkey(kSlot, combo);
    if (cfg.hotkey.enabled) {
        const KeyCombo c = parseCombo(cfg.hotkey.key);
        logLine(QString("全局热键已启用：%1（作用于当前页签）")
                    .arg(c.valid() ? c.label() : cfg.hotkey.key));
    }
}

void Hotkeys::stop() {
    if (!m_started) return;
    pl::stopHotkeys();
    m_started = false;
}

}  // namespace ac
