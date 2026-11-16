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
};

QHash<QString, int>& nameMap() {
    static QHash<QString, int> m = [] {
        QHash<QString, int> h;
        for (const Entry& e : kKeys) h.insert(QString::fromLatin1(e.name), e.vk);
        for (int i = 0; i < 12; ++i) h.insert(QString("f%1").arg(i + 1), 0x70 + i);
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
    for (int i = 1; i <= 12; ++i) out << QString("f%1").arg(i);
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
