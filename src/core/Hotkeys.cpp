#include "core/Hotkeys.h"

#include "core/Log.h"
#include "platform/Platform.h"

namespace ac {

Hotkeys::Hotkeys(QObject* parent) : QObject(parent) {}

int Hotkeys::slotFor(const QString& action) {
    if (action == "mouse") return 1;
    if (action == "image") return 2;
    if (action == "watch") return 3;
    if (action == "record") return 4;
    if (action == "emergency") return 5;
    return 0;
}

bool Hotkeys::start() {
    if (m_started) return true;
    bool ok = pl::startHotkeys([this](int slot) {
        const QString action = (slot >= 0 && slot < 8) ? m_slotAction[slot] : QString();
        if (!action.isEmpty()) emit triggered(action);
    });
    if (!ok) logLine("全局热键监听没能建立（这个平台可能不支持）");
    m_started = ok;
    return ok;
}

void Hotkeys::apply(const Config& cfg) {
    if (!start()) return;
    auto set = [this](const QString& action, const QString& combo, bool enabled) {
        const int slot = slotFor(action);
        if (slot == 0) return;
        m_slotAction[slot] = enabled ? action : QString();
        pl::setHotkey(slot, enabled ? combo : QString());
    };
    set("mouse", cfg.mouse.hotkey, cfg.mouse.hotkeyEnabled);
    set("image", cfg.image.hotkey, cfg.image.hotkeyEnabled);
    set("watch", cfg.watch.hotkey, cfg.watch.hotkeyEnabled);
    set("record", cfg.record.hotkey, cfg.record.hotkeyEnabled);
    set("emergency", cfg.emergency.hotkey, cfg.emergency.enabled);
}

void Hotkeys::stop() {
    if (!m_started) return;
    pl::stopHotkeys();
    m_started = false;
}

}  // namespace ac
