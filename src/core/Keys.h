// 键名 <-> 可移植虚拟键码（沿用 Windows VK 编号，平台层再翻译成原生码）。
#pragma once

#include <QString>
#include <QStringList>

namespace ac {

enum Mod { ModCtrl = 1, ModAlt = 2, ModShift = 4, ModWin = 8 };

struct KeyCombo {
    int mods = 0;
    int vk = 0;          // 主键，0 表示无效
    QString canonical;   // 统一小写写法，如 "ctrl+alt+f9"

    bool valid() const { return vk != 0; }
    QString label() const;  // 界面显示用，如 "Ctrl+Alt+F9"
};

KeyCombo parseCombo(const QString& text);   // "ctrl+alt+f9" / "F9" / "ctrl+shift+F8"
QString comboFromVk(int mods, int vk);      // 反向拼字符串

QString keyName(int vk);                    // 0x78 -> "f9"；未知返回 "?"
int keyVk(const QString& name);             // "f9" -> 0x78；未知返回 0

QStringList allKeyNames();                  // 可绑定的键（用于文档/自检）

int vkFromQtKey(int qtKey);                 // Qt::Key -> 可移植 VK（只用于界面上按键捕获）
QString comboFromQtKey(int qtKey, int mods); // Qt 按键 + 修饰位 -> "ctrl+alt+g"

}  // namespace ac
