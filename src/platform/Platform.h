// 平台抽象层：把「模拟输入 / 截屏 / 窗口 / 全局热键 / 全局钩子」收在一处。
// 上层 core 与 ui 只依赖这里的接口，具体实现在 PlatformWin.cpp / PlatformX11.cpp / PlatformMac.mm。
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>

namespace ac {
namespace pl {

// ---------------- 鼠标按键 ----------------
enum MouseButton { BtnLeft = 0, BtnRight = 1, BtnMiddle = 2, BtnX1 = 3, BtnX2 = 4 };
QString buttonName(int btn);              // "left" / "x1" ...
int buttonFromName(const QString& name);  // 未知返回 -1

// ---------------- 录制事件 ----------------
enum RecKind { E_Down = 1, E_Up = 2, E_Move = 3, E_Wheel = 4, E_KDown = 5, E_KUp = 6 };

struct RecEvent {
    int kind = 0;
    int t = 0;      // 毫秒，相对录制开始
    int x = 0;
    int y = 0;
    int btn = 0;    // E_Down / E_Up
    int steps = 0;  // E_Wheel
    int vk = 0;     // E_KDown / E_KUp（可移植 VK）
    bool ext = false;
};

// ---------------- 窗口 ----------------
struct WindowInfo {
    quintptr id = 0;
    QString title;
    QString process;
    QRect rect;
    bool visible = false;
};

// ---------------- 生命周期 ----------------
bool init();          // 建立与窗口系统的连接，失败返回 false
void shutdown();
QString backendName();  // "win32" / "x11" / "macos"
QRect virtualScreen();
QSize screenSize();

// ---------------- 模拟输入 ----------------
QPoint cursorPos();
void moveTo(const QPoint& pt);
void mouseDown(int btn);
void mouseUp(int btn);
void click(int btn, bool dbl);
void wheel(int steps);
void key(int vk, bool up, bool extended = false, int scan = 0);
void keyTap(int vk);

// ---------------- 截图 ----------------
QImage grabScreen(const QRect& area);   // 物理像素区域
QImage grabWindow(quintptr id);         // 整窗（尽量支持被遮挡）
QRect windowRect(quintptr id);          // 屏幕坐标（物理像素）
bool supportsOccludedCapture();         // 能否抓到被遮挡窗口的画面

// ---------------- 窗口查找/激活 ----------------
QVector<WindowInfo> windows();
quintptr findWindow(const QString& processLike, const QString& titleLike);
bool windowExists(quintptr id);
bool activateWindow(quintptr id);       // 还原 + 置顶 + 前台
QString processNameOf(quintptr id);

// ---------------- 全局热键 ----------------
typedef std::function<void(int slot)> HotkeySink;
bool startHotkeys(HotkeySink sink);              // 建立热键监听
void setHotkey(int slot, const QString& canonical);  // "" = 取消该 slot
void stopHotkeys();

// ---------------- 全局钩子（录制用） ----------------
typedef std::function<void(const RecEvent&)> EventSink;
bool installHook(EventSink sink);
void removeHook();
bool hookActive();

quint32 selfPid();

}  // namespace pl
}  // namespace ac
