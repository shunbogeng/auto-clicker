// Linux / X11 平台实现：XTest 模拟输入、XGetImage + XComposite 截图、EWMH 窗口操作、
// XGrabKey 全局热键、XRecord 全局钩子。Wayland 下这些能力大多不可用，程序会在日志里提示。
#include "platform/Platform.h"
#include "core/Keys.h"
#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>
#include <QHash>
#include <QProcess>
#include <QSize>
#include <QMutex>
#include <atomic>
#include <chrono>
#include <thread>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/record.h>
#include <X11/XKBlib.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstring>
#include <cstdio>

namespace ac {
namespace pl {

namespace {

Display* g_dpy = nullptr;
Window g_root = 0;
int g_sw = 0, g_sh = 0;
bool g_composite = false;
bool g_xtest = false;
bool g_record = false;

// 注入抑制：自己发出去的模拟输入不要被钩子录回来
std::atomic<bool> g_injecting{false};
std::atomic<int> g_injectDepth{0};

struct InjectGuard {
    InjectGuard() { g_injectDepth.fetch_add(1); g_injecting = true; }
    ~InjectGuard() {
        if (g_injectDepth.fetch_sub(1) == 1) g_injecting = false;
    }
};

// ---------------- portable VK <-> X keysym ----------------
KeySym vkToKeysym(int vk) {
    if (vk >= 0x41 && vk <= 0x5A) return XK_a + (vk - 0x41);
    if (vk >= 0x30 && vk <= 0x39) return XK_0 + (vk - 0x30);
    if (vk >= 0x70 && vk <= 0x7B) return XK_F1 + (vk - 0x70);
    if (vk >= 0x7C && vk <= 0x87) return XK_F13 + (vk - 0x7C);   // F13–F24
    if (vk >= 0x60 && vk <= 0x69) return XK_KP_0 + (vk - 0x60);  // 小键盘 0–9
    switch (vk) {
        case 0x20: return XK_space;
        case 0x0D: return XK_Return;
        case 0x09: return XK_Tab;
        case 0x1B: return XK_Escape;
        case 0x08: return XK_BackSpace;
        case 0x2E: return XK_Delete;
        case 0x2D: return XK_Insert;
        case 0x24: return XK_Home;
        case 0x23: return XK_End;
        case 0x21: return XK_Page_Up;
        case 0x22: return XK_Page_Down;
        case 0x26: return XK_Up;
        case 0x28: return XK_Down;
        case 0x25: return XK_Left;
        case 0x27: return XK_Right;
        case 0x10: return XK_Shift_L;
        case 0x11: return XK_Control_L;
        case 0x12: return XK_Alt_L;
        case 0x5B: return XK_Super_L;
        case 0x13: return XK_Pause;
        case 0x14: return XK_Caps_Lock;
        case 0x2C: return XK_Print;
        case 0x6A: return XK_KP_Multiply;
        case 0x6B: return XK_KP_Add;
        case 0x6D: return XK_KP_Subtract;
        case 0x6E: return XK_KP_Decimal;
        case 0x6F: return XK_KP_Divide;
        case 0x90: return XK_Num_Lock;
        case 0x91: return XK_Scroll_Lock;
        case 0xBD: return XK_minus;
        case 0xBB: return XK_equal;
        case 0xBC: return XK_comma;
        case 0xBE: return XK_period;
        case 0xBF: return XK_slash;
        case 0xDC: return XK_backslash;
        case 0xBA: return XK_semicolon;
        case 0xDE: return XK_apostrophe;
        case 0xDB: return XK_bracketleft;
        case 0xDD: return XK_bracketright;
        case 0xC0: return XK_grave;
        default: return NoSymbol;
    }
}

int keysymToVk(KeySym ks) {
    if (ks >= XK_a && ks <= XK_z) return 0x41 + (ks - XK_a);
    if (ks >= XK_A && ks <= XK_Z) return 0x41 + (ks - XK_A);
    if (ks >= XK_0 && ks <= XK_9) return 0x30 + (ks - XK_0);
    if (ks >= XK_F1 && ks <= XK_F12) return 0x70 + (ks - XK_F1);
    if (ks >= XK_F13 && ks <= XK_F24) return 0x7C + (ks - XK_F13);
    if (ks >= XK_KP_0 && ks <= XK_KP_9) return 0x60 + (ks - XK_KP_0);
    switch (ks) {
        case XK_space: return 0x20;
        case XK_Return: case XK_KP_Enter: return 0x0D;
        case XK_Tab: return 0x09;
        case XK_Escape: return 0x1B;
        case XK_BackSpace: return 0x08;
        case XK_Delete: return 0x2E;
        case XK_Insert: return 0x2D;
        case XK_Home: return 0x24;
        case XK_End: return 0x23;
        case XK_Page_Up: return 0x21;
        case XK_Page_Down: return 0x22;
        case XK_Up: return 0x26;
        case XK_Down: return 0x28;
        case XK_Left: return 0x25;
        case XK_Right: return 0x27;
        case XK_Shift_L: case XK_Shift_R: return 0x10;
        case XK_Control_L: case XK_Control_R: return 0x11;
        case XK_Alt_L: case XK_Alt_R: return 0x12;
        case XK_Super_L: case XK_Super_R: return 0x5B;
        case XK_Pause: return 0x13;
        case XK_Caps_Lock: return 0x14;
        case XK_Print: return 0x2C;
        case XK_KP_Multiply: return 0x6A;
        case XK_KP_Add: return 0x6B;
        case XK_KP_Subtract: return 0x6D;
        case XK_KP_Decimal: return 0x6E;
        case XK_KP_Divide: return 0x6F;
        case XK_Num_Lock: return 0x90;
        case XK_Scroll_Lock: return 0x91;
        case XK_minus: return 0xBD;
        case XK_equal: return 0xBB;
        case XK_comma: return 0xBC;
        case XK_period: return 0xBE;
        case XK_slash: return 0xBF;
        case XK_backslash: return 0xDC;
        case XK_semicolon: return 0xBA;
        case XK_apostrophe: return 0xDE;
        case XK_bracketleft: return 0xDB;
        case XK_bracketright: return 0xDD;
        case XK_grave: return 0xC0;
        default: return 0;
    }
}

KeyCode vkToKeycode(int vk) {
    KeySym ks = vkToKeysym(vk);
    if (ks == NoSymbol || !g_dpy) return 0;
    return XKeysymToKeycode(g_dpy, ks);
}

// ---------------- XImage -> QImage ----------------
QImage fromXImage(XImage* img) {
    if (!img) return {};
    const int w = img->width, h = img->height;
    QImage out(w, h, QImage::Format_RGB32);
    if (img->bits_per_pixel == 32 && img->byte_order == LSBFirst) {
        for (int y = 0; y < h; ++y) {
            const quint32* src = reinterpret_cast<const quint32*>(img->data + y * img->bytes_per_line);
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) {
                quint32 p = src[x];
                quint32 r = (p & img->red_mask) >> 16;
                quint32 g = (p & img->green_mask) >> 8;
                quint32 b = (p & img->blue_mask);
                dst[x] = qRgb(int(r), int(g), int(b));
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned long p = XGetPixel(img, x, y);
                auto chan = [&](unsigned long mask) {
                    if (!mask) return 0;
                    unsigned long v = p & mask;
                    int shift = 0;
                    while (!((mask >> shift) & 1)) ++shift;
                    int bits = 0;
                    unsigned long m = mask >> shift;
                    while (m & 1) { ++bits; m >>= 1; }
                    return int((v >> shift) * 255 / ((1UL << bits) - 1));
                };
                out.setPixel(x, y, qRgb(chan(img->red_mask), chan(img->green_mask), chan(img->blue_mask)));
            }
        }
    }
    return out;
}

// ---------------- atoms ----------------
Atom atom(const char* name) { return XInternAtom(g_dpy, name, False); }

QString windowTitle(Window w) {
    Atom netName = atom("_NET_WM_NAME");
    Atom utf8 = atom("UTF8_STRING");
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(g_dpy, w, netName, 0, 1024, False, utf8, &type, &format, &n, &after, &data) ==
            Success && data) {
        QString s = QString::fromUtf8(reinterpret_cast<char*>(data), int(n));
        XFree(data);
        if (!s.isEmpty()) return s;
    }
    char* name = nullptr;
    if (XFetchName(g_dpy, w, &name) && name) {
        QString s = QString::fromUtf8(name);
        XFree(name);
        return s;
    }
    return {};
}

quint32 windowPid(Window w) {
    Atom pidA = atom("_NET_WM_PID");
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(g_dpy, w, pidA, 0, 1, False, XA_CARDINAL, &type, &format, &n, &after, &data) ==
            Success && data) {
        quint32 pid = *reinterpret_cast<quint32*>(data);
        XFree(data);
        return pid;
    }
    return 0;
}

QString procNameOfPid(quint32 pid) {
    if (!pid) return {};
    // 优先用 cmdline 第一个参数的文件名：/proc/<pid>/comm 只有 15 个字符，长名字会被截断
    QFile c(QString("/proc/%1/cmdline").arg(pid));
    if (c.open(QIODevice::ReadOnly)) {
        const QByteArray raw = c.readAll();
        c.close();
        const QByteArray first = raw.left(raw.indexOf('\0') < 0 ? raw.size() : raw.indexOf('\0'));
        if (!first.isEmpty()) {
            const QString base = QFileInfo(QString::fromUtf8(first)).fileName();
            if (!base.isEmpty()) return base;
        }
    }
    QFile f(QString("/proc/%1/comm").arg(pid));
    if (f.open(QIODevice::ReadOnly)) {
        QString s = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!s.isEmpty()) return s;
    }
    return {};
}

// 进程名模糊匹配：双向包含，且忽略 .exe 后缀（comm 还会截断到 15 个字符）
bool processMatches(const QString& actual, const QString& wanted) {
    QString a = actual.toLower();
    QString w = wanted.toLower();
    if (a.endsWith(".exe")) a.chop(4);
    if (w.endsWith(".exe")) w.chop(4);
    if (a.isEmpty() || w.isEmpty()) return false;
    return a.contains(w) || w.contains(a);
}

QRect windowScreenRect(Window w) {
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(g_dpy, w, &attr)) return {};
    int rx = 0, ry = 0;
    Window child = 0;
    if (!XTranslateCoordinates(g_dpy, w, g_root, 0, 0, &rx, &ry, &child)) return {};
    return QRect(rx, ry, attr.width, attr.height);
}

// ---------------- hotkeys ----------------
struct HotkeyDef {
    int slot = 0;
    KeyCode code = 0;
    unsigned mods = 0;
};

struct HotkeyState {
    Display* dpy = nullptr;
    Window root = 0;
    HotkeySink sink;
    QVector<HotkeyDef> defs;   // 已成功 grab 的组合
    std::thread thread;
    std::atomic<bool> running{false};
    QMutex mu;
};
HotkeyState g_hk;

void ungrabAllInternal() {
    if (!g_hk.dpy) return;
    for (const HotkeyDef& d : g_hk.defs) {
        const unsigned extras[3] = {0u, unsigned(LockMask), unsigned(Mod2Mask)};
        for (unsigned extra : extras) {
            XUngrabKey(g_hk.dpy, d.code, d.mods | extra, g_hk.root);
        }
    }
    g_hk.defs.clear();
    XFlush(g_hk.dpy);
}

// ---------------- hook ----------------
struct HookState {
    Display* ctl = nullptr;
    Display* data = nullptr;
    XRecordContext ctx = 0;
    EventSink sink;
    std::thread thread;
    std::atomic<bool> active{false};
    std::atomic<bool> stopping{false};
    QElapsedTimer clock;
    quint32 selfPid = 0;
};
HookState g_hook;

}  // namespace

// ---------------------------------------------------------------------------
bool init() {
    if (g_dpy) return true;
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) {
        logLine("X11：连接不到显示器（DISPLAY 为空或不是 X11 会话）");
        return false;
    }
    g_root = DefaultRootWindow(g_dpy);
    g_sw = DisplayWidth(g_dpy, DefaultScreen(g_dpy));
    g_sh = DisplayHeight(g_dpy, DefaultScreen(g_dpy));

    int ev = 0, err = 0;
    if (XTestQueryExtension(g_dpy, &ev, &ev, &ev, &err)) g_xtest = true;
    int major = 0, minor = 0;
    if (XCompositeQueryExtension(g_dpy, &major, &minor)) g_composite = true;
    if (XRecordQueryVersion(g_dpy, &major, &minor)) g_record = true;

    logLine(QString("X11 初始化完成：屏幕 %1x%2，模拟输入=%3，顶层窗口截取=%4，录制=%5")
                .arg(g_sw).arg(g_sh)
                .arg(g_xtest ? "可用" : "不可用")
                .arg(g_composite ? "可用" : "不可用")
                .arg(g_record ? "可用" : "不可用"));
    return true;
}

void shutdown() {
    removeHook();
    stopHotkeys();
    if (g_dpy) {
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
    }
}

QString backendName() { return "x11"; }
QRect virtualScreen() { return QRect(0, 0, g_sw, g_sh); }
QSize screenSize() { return QSize(g_sw, g_sh); }

QPoint cursorPos() {
    if (!g_dpy) return {};
    Window rootRet = 0, childRet = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    XQueryPointer(g_dpy, g_root, &rootRet, &childRet, &rx, &ry, &wx, &wy, &mask);
    return QPoint(rx, ry);
}

void moveTo(const QPoint& pt) {
    if (!g_dpy || !g_xtest) return;
    InjectGuard guard;
    XTestFakeMotionEvent(g_dpy, -1, pt.x(), pt.y(), CurrentTime);
    XFlush(g_dpy);
}

static int xButton(int btn) {
    switch (btn) {
        case BtnLeft: return 1;
        case BtnMiddle: return 2;
        case BtnRight: return 3;
        case BtnX1: return 8;
        case BtnX2: return 9;
        default: return 1;
    }
}

void mouseDown(int btn) {
    if (!g_dpy || !g_xtest) return;
    InjectGuard guard;
    XTestFakeButtonEvent(g_dpy, xButton(btn), True, CurrentTime);
    XFlush(g_dpy);
}

void mouseUp(int btn) {
    if (!g_dpy || !g_xtest) return;
    InjectGuard guard;
    XTestFakeButtonEvent(g_dpy, xButton(btn), False, CurrentTime);
    XFlush(g_dpy);
}

void click(int btn, bool dbl) {
    mouseDown(btn);
    mouseUp(btn);
    if (dbl) {
        QThread::msleep(80);
        mouseDown(btn);
        mouseUp(btn);
    }
}

void wheel(int steps) {
    if (!g_dpy || !g_xtest || steps == 0) return;
    const int button = steps > 0 ? 4 : 5;
    for (int i = 0; i < qAbs(steps); ++i) {
        InjectGuard guard;
        XTestFakeButtonEvent(g_dpy, button, True, CurrentTime);
        XTestFakeButtonEvent(g_dpy, button, False, CurrentTime);
        XFlush(g_dpy);
        QThread::msleep(2);
    }
}

void key(int vk, bool up, bool extended, int scan) {
    Q_UNUSED(extended);
    Q_UNUSED(scan);
    if (!g_dpy || !g_xtest) return;
    KeyCode code = vkToKeycode(vk);
    if (!code) return;
    InjectGuard guard;
    XTestFakeKeyEvent(g_dpy, code, up ? False : True, CurrentTime);
    XFlush(g_dpy);
}

void keyTap(int vk) {
    key(vk, false);
    key(vk, true);
}

// ---------------------------------------------------------------------------
QImage grabScreen(const QRect& area) {
    if (!g_dpy) return {};
    QRect a = area.isNull() ? QRect(0, 0, g_sw, g_sh) : area;
    a = a.intersected(QRect(0, 0, g_sw, g_sh));
    if (a.width() <= 0 || a.height() <= 0) return {};
    XImage* img = XGetImage(g_dpy, g_root, a.x(), a.y(), unsigned(a.width()), unsigned(a.height()),
                            AllPlanes, ZPixmap);
    QImage out = fromXImage(img);
    if (img) XDestroyImage(img);
    return out;
}

QRect windowRect(quintptr id) {
    if (!g_dpy || !id) return {};
    return windowScreenRect(Window(id));
}

QImage grabWindow(quintptr id) {
    if (!g_dpy || !id) return {};
    Window w = Window(id);
    QRect r = windowScreenRect(w);
    if (qgetenv("AC_DEBUG_GRAB") == "1") qDebug() << "grabWin" << quintptr(w) << "rect" << r;
    if (r.isEmpty()) return {};

    // 1) XComposite：把窗口画面重定向到离屏 pixmap，被遮挡也能取到
    if (g_composite) {
        Pixmap pix = XCompositeNameWindowPixmap(g_dpy, w);
        if (pix) {
            XImage* img = XGetImage(g_dpy, pix, 0, 0, unsigned(r.width()), unsigned(r.height()),
                                    AllPlanes, ZPixmap);
            QImage out = fromXImage(img);
            if (img) XDestroyImage(img);
            XFreePixmap(g_dpy, pix);
            if (qgetenv("AC_DEBUG_GRAB") == "1") qDebug() << "xgetimage out" << out.size();
        if (!out.isNull()) return out;
            if (qgetenv("AC_DEBUG_GRAB") == "1") qDebug() << "composite failed, img=" << (void*)img;
        }
    }
    // 2) 直接取窗口 drawable（未被遮挡时有效）
    {
        XImage* img = XGetImage(g_dpy, w, 0, 0, unsigned(r.width()), unsigned(r.height()), AllPlanes, ZPixmap);
        QImage out = fromXImage(img);
        if (img) XDestroyImage(img);
        if (qgetenv("AC_DEBUG_GRAB") == "1") qDebug() << "xgetimage out" << out.size();
        if (!out.isNull()) return out;
    }
    // 3) 兜底：截屏幕上这块区域
    return grabScreen(r);
}

bool supportsOccludedCapture() { return g_composite; }

QVector<WindowInfo> windows() {
    QVector<WindowInfo> out;
    if (!g_dpy) return out;
    Atom clientList = atom("_NET_CLIENT_LIST");
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(g_dpy, g_root, clientList, 0, 4096, False, XA_WINDOW, &type, &format, &n, &after,
                           &data) != Success || !data) {
        return out;
    }
    Window* ws = reinterpret_cast<Window*>(data);
    for (unsigned long i = 0; i < n; ++i) {
        Window w = ws[i];
        XWindowAttributes attr{};
        if (!XGetWindowAttributes(g_dpy, w, &attr)) continue;
        if (attr.override_redirect) continue;
        WindowInfo info;
        info.id = quintptr(w);
        info.title = windowTitle(w);
        const quint32 pid = windowPid(w);
        info.process = procNameOfPid(pid);
        info.rect = windowScreenRect(w);
        info.visible = attr.map_state == IsViewable;
        if (info.title.isEmpty() && info.process.isEmpty()) continue;
        out.append(info);
    }
    XFree(data);
    return out;
}

quintptr findWindow(const QString& processLike, const QString& titleLike) {
    const QString p = processLike.trimmed().toLower();
    const QString t = titleLike.trimmed().toLower();
    quintptr best = 0;
    for (const WindowInfo& wi : windows()) {
        if (!wi.visible) continue;
        bool ok = true;
        if (!p.isEmpty()) ok = processMatches(wi.process, p);
        if (ok && !t.isEmpty()) ok = wi.title.toLower().contains(t);
        if (ok) {
            if (best == 0 || wi.rect.width() * wi.rect.height() > windowRect(best).width() * windowRect(best).height())
                best = wi.id;
        }
    }
    return best;
}

bool windowExists(quintptr id) {
    if (!g_dpy || !id) return false;
    XWindowAttributes attr{};
    return XGetWindowAttributes(g_dpy, Window(id), &attr) != 0;
}

bool activateWindow(quintptr id) {
    if (!g_dpy || !id) return false;
    Window w = Window(id);
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(g_dpy, w, &attr)) return false;

    // 最小化的先还原
    if (attr.map_state != IsViewable) {
        XMapWindow(g_dpy, w);
        XFlush(g_dpy);
        QThread::msleep(60);
    }
    XRaiseWindow(g_dpy, w);

    // EWMH：请求窗口管理器把焦点交给它（比 XSetInputFocus 稳）
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = atom("_NET_ACTIVE_WINDOW");
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;  // source indication: pager
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = 0;
    XSendEvent(g_dpy, g_root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);

    XSetInputFocus(g_dpy, w, RevertToParent, CurrentTime);
    XFlush(g_dpy);
    QThread::msleep(120);
    return true;
}

QString processNameOf(quintptr id) {
    if (!g_dpy || !id) return {};
    return procNameOfPid(windowPid(Window(id)));
}

// ---------------------------------------------------------------------------
// 全局热键：独立连接抓 root 的按键，线程里读事件
// ---------------------------------------------------------------------------
bool startHotkeys(HotkeySink sink) {
    if (g_hk.dpy) return true;
    g_hk.dpy = XOpenDisplay(nullptr);
    if (!g_hk.dpy) return false;
    g_hk.root = DefaultRootWindow(g_hk.dpy);
    g_hk.sink = std::move(sink);
    XSelectInput(g_hk.dpy, g_hk.root, KeyPressMask);
    g_hk.running = true;
    g_hk.thread = std::thread([] {
        while (g_hk.running) {
            while (XPending(g_hk.dpy)) {
                XEvent ev{};
                XNextEvent(g_hk.dpy, &ev);
                if (ev.type != KeyPress) continue;
                unsigned mods = ev.xkey.state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
                for (const HotkeyDef& d : g_hk.defs) {
                    if (d.code == ev.xkey.keycode && d.mods == mods) {
                        if (g_hk.sink) g_hk.sink(d.slot);
                        break;
                    }
                }
            }
            QThread::msleep(8);
        }
    });
    return true;
}

void setHotkey(int slot, const QString& canonical) {
    if (!g_hk.dpy) return;
    // 先撤掉这个 slot 已有的
    for (int i = g_hk.defs.size() - 1; i >= 0; --i) {
        if (g_hk.defs[i].slot == slot) {
            HotkeyDef d = g_hk.defs[i];
            const unsigned extras[3] = {0u, unsigned(LockMask), unsigned(Mod2Mask)};
            for (unsigned extra : extras)
                XUngrabKey(g_hk.dpy, d.code, d.mods | extra, g_hk.root);
            g_hk.defs.removeAt(i);
        }
    }
    XFlush(g_hk.dpy);
    if (canonical.isEmpty()) return;

    KeyCombo combo = parseCombo(canonical);
    KeyCode code = vkToKeycode(combo.vk);
    if (!code) {
        logLine(QString("热键 %1 在这个系统上映射不到对应按键").arg(canonical));
        return;
    }
    unsigned mods = 0;
    if (combo.mods & ModCtrl) mods |= ControlMask;
    if (combo.mods & ModAlt) mods |= Mod1Mask;
    if (combo.mods & ModShift) mods |= ShiftMask;
    if (combo.mods & ModWin) mods |= Mod4Mask;

    const unsigned extras[3] = {0u, unsigned(LockMask), unsigned(Mod2Mask)};
    for (unsigned extra : extras) {
        XGrabKey(g_hk.dpy, code, mods | extra, g_hk.root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(g_hk.dpy, False);
    g_hk.defs.append(HotkeyDef{slot, code, mods});
}

void stopHotkeys() {
    if (!g_hk.dpy) return;
    g_hk.running = false;
    if (g_hk.thread.joinable()) g_hk.thread.join();
    ungrabAllInternal();
    XCloseDisplay(g_hk.dpy);
    g_hk.dpy = nullptr;
    g_hk.sink = nullptr;
}

// ---------------------------------------------------------------------------
// 全局钩子：XRecord
// ---------------------------------------------------------------------------
// XRecord 回调拿到的是服务端 32 字节原始事件，字段布局与设备事件一致
struct RawEvent {
    quint8 type;
    quint8 detail;
    quint16 sequence;
    quint32 time;
    quint32 root;
    quint32 event;
    quint32 child;
    qint16 rootX;
    qint16 rootY;
    qint16 eventX;
    qint16 eventY;
    quint16 state;
    quint8 sameScreen;
    quint8 pad;
};

static void recordCallback(XPointer closure, XRecordInterceptData* data) {
    Q_UNUSED(closure);
    if (!data) return;
    if (qEnvironmentVariableIsSet("AC_DEBUG_RECORD"))
        fprintf(stderr, "[rec] cat=%d len=%d first=%d inject=%d\n", int(data->category), int(data->data_len),
                data->data ? int(reinterpret_cast<const unsigned char*>(data->data)[0]) : -1,
                int(g_injecting.load()));
    if (data->category == XRecordFromServer && data->data_len * 4 >= int(sizeof(RawEvent))) {
        RawEvent ev;
        std::memcpy(&ev, data->data, sizeof(RawEvent));
        if (!g_injecting) {
            RecEvent out;
            out.t = int(g_hook.clock.elapsed());
            bool shouldEmit = false;
            switch (ev.type) {
                case ButtonPress:
                case ButtonRelease: {
                    const int detail = ev.detail;
                    out.x = ev.rootX;
                    out.y = ev.rootY;
                    if (detail == 4 || detail == 5) {
                        if (ev.type == ButtonPress) {
                            out.kind = E_Wheel;
                            out.steps = detail == 4 ? 1 : -1;
                            shouldEmit = true;
                        }
                    } else {
                        out.kind = ev.type == ButtonPress ? E_Down : E_Up;
                        if (detail == 1) out.btn = BtnLeft;
                        else if (detail == 2) out.btn = BtnMiddle;
                        else if (detail == 3) out.btn = BtnRight;
                        else if (detail == 8) out.btn = BtnX1;
                        else if (detail == 9) out.btn = BtnX2;
                        else break;
                        shouldEmit = true;
                    }
                    break;
                }
                case MotionNotify:
                    out.kind = E_Move;
                    out.x = ev.rootX;
                    out.y = ev.rootY;
                    shouldEmit = true;
                    break;
                case KeyPress:
                case KeyRelease: {
                    const KeyCode code = ev.detail;
                    KeySym ks = XkbKeycodeToKeysym(g_hook.data, code, 0, 0);
                    int vk = keysymToVk(ks);
                    if (!vk) break;
                    out.kind = ev.type == KeyPress ? E_KDown : E_KUp;
                    out.vk = vk;
                    shouldEmit = true;
                    break;
                }
                default:
                    break;
            }
            if (shouldEmit && g_hook.sink) g_hook.sink(out);
        }
    }
    XRecordFreeData(data);
}

bool installHook(EventSink sink) {
    if (g_hook.active) return true;
    if (!g_record) {
        logLine("X11：这个会话不支持 XRecord 扩展，录制功能不可用");
        return false;
    }
    g_hook.ctl = XOpenDisplay(nullptr);
    g_hook.data = XOpenDisplay(nullptr);
    if (!g_hook.ctl || !g_hook.data) {
        logLine("X11：录制所需的第二个显示连接创建失败");
        return false;
    }
    XRecordClientSpec clients = XRecordAllClients;
    XRecordRange* range = XRecordAllocRange();
    if (!range) return false;
    range->device_events.first = KeyPress;
    range->device_events.last = MotionNotify;
    g_hook.ctx = XRecordCreateContext(g_hook.ctl, 0, &clients, 1, &range, 1);
    XFree(range);
    if (!g_hook.ctx) {
        logLine("X11：XRecordCreateContext 失败");
        return false;
    }
    XSync(g_hook.ctl, False);
    g_hook.sink = std::move(sink);
    g_hook.clock.start();
    g_hook.stopping = false;
    g_hook.active = true;
    g_hook.thread = std::thread([] {
        XRecordEnableContext(g_hook.data, g_hook.ctx, recordCallback, nullptr);
    });
    logLine("X11：全局录制的钩子已装上（XRecord）");
    return true;
}

void removeHook() {
    if (!g_hook.active) return;
    g_hook.active = false;
    if (g_hook.ctl && g_hook.ctx) XRecordDisableContext(g_hook.ctl, g_hook.ctx);
    if (g_hook.ctl) XFlush(g_hook.ctl);
    if (g_hook.thread.joinable()) g_hook.thread.join();
    if (g_hook.ctx) XRecordFreeContext(g_hook.ctl, g_hook.ctx);
    g_hook.ctx = 0;
    if (g_hook.data) XCloseDisplay(g_hook.data);
    if (g_hook.ctl) XCloseDisplay(g_hook.ctl);
    g_hook.data = nullptr;
    g_hook.ctl = nullptr;
    g_hook.sink = nullptr;
    logLine("X11：录制钩子已卸载");
}

bool hookActive() { return g_hook.active; }

}  // namespace pl
}  // namespace ac
