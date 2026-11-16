#include "platform/Platform.h"

#include <QProcess>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ac {
namespace pl {

QString buttonName(int btn) {
    switch (btn) {
        case BtnLeft: return "left";
        case BtnRight: return "right";
        case BtnMiddle: return "middle";
        case BtnX1: return "x1";
        case BtnX2: return "x2";
        default: return "left";
    }
}

int buttonFromName(const QString& name) {
    const QString n = name.trimmed().toLower();
    if (n == "left") return BtnLeft;
    if (n == "right") return BtnRight;
    if (n == "middle") return BtnMiddle;
    if (n == "x1") return BtnX1;
    if (n == "x2") return BtnX2;
    return -1;
}

quint32 selfPid() {
#if defined(Q_OS_WIN)
    return static_cast<quint32>(GetCurrentProcessId());
#else
    return static_cast<quint32>(getpid());
#endif
}

}  // namespace pl
}  // namespace ac
