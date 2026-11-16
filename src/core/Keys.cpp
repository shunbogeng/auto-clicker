#include "core/Keys.h"

#include <QHash>

namespace ac {

namespace {

struct Entry {
    const char* name;
    int vk;
};

const Entry kKeys[] = {
    {"space", 0x20}, {"enter", 0x0D}, {"tab", 0x09}, {"esc", 0x1B},
    {"backspace", 0x08}, {"delete", 0x2E}, {"insert", 0x2D},
    {"home", 0x24}, {"end", 0x23}, {"pageup", 0x21}, {"pagedown", 0x22},
    {"up", 0x26}, {"down", 0x28}, {"left", 0x25}, {"right", 0x27},
    {"shift", 0x10}, {"ctrl", 0x11}, {"alt", 0x12}, {"win", 0x5B},
    {"pause", 0x13}, {"capslock", 0x14}, {"printscreen", 0x2C},
    {"numlock", 0x90}, {"scrolllock", 0x91},
    // 主键盘符号键（Windows OEM 键位）
    {"minus", 0xBD}, {"equal", 0xBB}, {"comma", 0xBC}, {"period", 0xBE},
    {"slash", 0xBF}, {"backslash", 0xDC}, {"semicolon", 0xBA}, {"quote", 0xDE},
    {"lbracket", 0xDB}, {"rbracket", 0xDD}, {"grave", 0xC0},
    // 小键盘
    {"num0", 0x60}, {"num1", 0x61}, {"num2", 0x62}, {"num3", 0x63}, {"num4", 0x64},
    {"num5", 0x65}, {"num6", 0x66}, {"num7", 0x67}, {"num8", 0x68}, {"num9", 0x69},
    {"nummul", 0x6A}, {"numadd", 0x6B}, {"numsub", 0x6D}, {"numdec", 0x6E}, {"numdiv", 0x6F},
};

QHash<QString, int>& nameMap() {
    static QHash<QString, int> m = [] {
        QHash<QString, int> h;
        for (const Entry& e : kKeys) h.insert(QString::fromLatin1(e.name), e.vk);
        for (int i = 0; i < 24; ++i) h.insert(QString("f%1").arg(i + 1), 0x70 + i);
        for (char c = 'a'; c <= 'z'; ++c) h.insert(QString(QChar(c)), 0x41 + (c - 'a'));
        for (char c = '0'; c <= '9'; ++c) h.insert(QString(QChar(c)), 0x30 + (c - '0'));
        return h;
    }();
    return m;
}

QHash<int, QString>& vkMap() {
    static QHash<int, QString> m = [] {
        QHash<int, QString> h;
        for (auto it = nameMap().cbegin(); it != nameMap().cend(); ++it) {
            if (!h.contains(it.value())) h.insert(it.value(), it.key());
        }
        return h;
    }();
    return m;
}

}  // namespace

QString keyName(int vk) {
    auto it = vkMap().find(vk);
    return it == vkMap().end() ? QString("?") : it.value();
}

int keyVk(const QString& name) {
    return nameMap().value(name.trimmed().toLower(), 0);
}

QStringList allKeyNames() {
    QStringList out;
    for (const Entry& e : kKeys) out << QString::fromLatin1(e.name);
    for (int i = 1; i <= 24; ++i) out << QString("f%1").arg(i);
    for (char c = 'a'; c <= 'z'; ++c) out << QString(QChar(c));
    for (char c = '0'; c <= '9'; ++c) out << QString(QChar(c));
    return out;
}

QString KeyCombo::label() const {
    QStringList parts;
    if (mods & ModCtrl) parts << "Ctrl";
    if (mods & ModAlt) parts << "Alt";
    if (mods & ModShift) parts << "Shift";
    if (mods & ModWin) parts << "Win";
    QString k = keyName(vk);
    if (k.length() == 1) k = k.toUpper();
    else if (k.startsWith("f") && k.length() <= 3) k = k.toUpper();
    else if (!k.isEmpty()) k = k.left(1).toUpper() + k.mid(1);
    parts << k;
    return parts.join("+");
}

KeyCombo parseCombo(const QString& text) {
    KeyCombo c;
    QStringList parts = text.toLower().split('+', Qt::SkipEmptyParts);
    QStringList canon;
    for (const QString& raw : parts) {
        QString p = raw.trimmed();
        if (p.isEmpty()) continue;
        if (p == "ctrl" || p == "control") {
            c.mods |= ModCtrl;
            canon << "ctrl";
        } else if (p == "alt") {
            c.mods |= ModAlt;
            canon << "alt";
        } else if (p == "shift") {
            c.mods |= ModShift;
            canon << "shift";
        } else if (p == "win" || p == "super" || p == "meta") {
            c.mods |= ModWin;
            canon << "win";
        } else {
            int vk = keyVk(p);
            if (vk == 0) return KeyCombo{};
            c.vk = vk;
            canon << keyName(vk);
        }
    }
    c.canonical = canon.join("+");
    return c;
}

int vkFromQtKey(int qtKey) {
    // 字母 / 数字
    if (qtKey >= 0x41 && qtKey <= 0x5A) return qtKey;
    if (qtKey >= 0x30 && qtKey <= 0x39) return qtKey;
    // F1–F24（Qt 里 F1 起连续排列）
    if (qtKey >= 0x01000030 && qtKey <= 0x01000047) return 0x70 + (qtKey - 0x01000030);
    switch (qtKey) {
        case 0x20: return 0x20;                       // Space
        case 0x01000004: case 0x01000005: return 0x0D; // Return / Enter
        case 0x01000001: return 0x09;                 // Tab
        case 0x01000000: return 0x1B;                 // Escape
        case 0x01000003: return 0x08;                 // Backspace
        case 0x01000007: return 0x2E;                 // Delete
        case 0x01000006: return 0x2D;                 // Insert
        case 0x01000010: return 0x24;                 // Home
        case 0x01000011: return 0x23;                 // End
        case 0x01000012: return 0x21;                 // PageUp
        case 0x01000013: return 0x22;                 // PageDown
        case 0x01000014: return 0x26;                 // Up
        case 0x01000015: return 0x28;                 // Down
        case 0x01000016: return 0x25;                 // Left
        case 0x01000017: return 0x27;                 // Right
        case 0x2D: return 0xBD;                       // -
        case 0x3D: return 0xBB;                       // =
        case 0x2C: return 0xBC;                       // ,
        case 0x2E: return 0xBE;                       // .
        case 0x2F: return 0xBF;                       // /
        case 0x5C: return 0xDC;                       // \\
        case 0x3B: return 0xBA;                       // ;
        case 0x27: return 0xDE;                       // '
        case 0x5B: return 0xDB;                       // [
        case 0x5D: return 0xDD;                       // ]
        case 0x60: return 0xC0;                       // `
        default: return 0;
    }
}

QString comboFromQtKey(int qtKey, int mods) {
    const int vk = vkFromQtKey(qtKey);
    if (!vk) return QString();
    return comboFromVk(mods, vk);
}

QString comboFromVk(int mods, int vk) {
    QStringList canon;
    if (mods & ModCtrl) canon << "ctrl";
    if (mods & ModAlt) canon << "alt";
    if (mods & ModShift) canon << "shift";
    if (mods & ModWin) canon << "win";
    QString k = keyName(vk);
    if (!k.isEmpty() && k != "?") canon << k;
    return canon.join("+");
}

}  // namespace ac
