// Windows 平台实现：SendInput 模拟输入、BitBlt / PrintWindow 截屏、EnumWindows 找窗口、
// RegisterHotKey 热键、WH_MOUSE_LL + WH_KEYBOARD_LL 全局钩子（录制）。
#include "platform/Platform.h"
#include "core/Keys.h"
#include "core/Log.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QThread>
#include <atomic>
#include <cstdlib>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>

namespace ac {
namespace pl {

namespace {

bool g_inited = false;
int g_vx = 0, g_vy = 0, g_vw = 0, g_vh = 0;

WORD vkToWin(int vk) { return WORD(vk); }

bool isExtendedVk(int vk) {
    switch (vk) {
        case 0x26: case 0x28: case 0x25: case 0x27:  // 方向键
        case 0x24: case 0x23:                        // home/end
        case 0x21: case 0x22:                        // pageup/pagedown
        case 0x2D: case 0x2E:                        // insert/delete
        case 0x2C:                                   // print screen
        case 0x5B:                                   // win
            return true;
        default:
            return false;
    }
}

QImage fromHBitmap(HBITMAP bmp, HDC hdc, int w, int h) {
    if (!bmp || w <= 0 || h <= 0) return {};
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QImage img(w, h, QImage::Format_RGB32);
    if (GetDIBits(hdc, bmp, 0, UINT(h), img.bits(), &bi, DIB_RGB_COLORS) == 0) return {};
    return img;
}

QImage grabRect(const QRect& r) {
    if (r.width() <= 0 || r.height() <= 0) return {};
    HDC screen = GetDC(nullptr);
    if (!screen) return {};
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, r.width(), r.height());
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, r.width(), r.height(), screen, r.x(), r.y(), SRCCOPY | CAPTUREBLT);
    QImage out = fromHBitmap(bmp, mem, r.width(), r.height());
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return out;
}

QString windowTitle(HWND hwnd) {
    WCHAR buf[512] = {0};
    if (GetWindowTextW(hwnd, buf, 511) > 0) return QString::fromWCharArray(buf);
    return {};
}

DWORD pidOfWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

QString procNameOfWindow(HWND hwnd) {
    DWORD pid = pidOfWindow(hwnd);
    if (!pid) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    WCHAR path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, path, &size)) {
        QString p = QString::fromWCharArray(path);
        name = p.mid(p.lastIndexOf('\\') + 1);
    }
    CloseHandle(h);
    return name;
}

// ---------------- 热键 ----------------
struct HotkeyState {
    std::thread thread;
    DWORD tid = 0;
    std::atomic<bool> running{false};
    HotkeySink sink;
    QMutex mu;
    QString combos[8];
    HANDLE readyEvent = nullptr;
};
HotkeyState g_hk;

const UINT WM_AC_APPLY = WM_APP + 1;

unsigned winMods(const KeyCombo& c) {
    unsigned m = 0;
    if (c.mods & ModCtrl) m |= MOD_CONTROL;
    if (c.mods & ModAlt) m |= MOD_ALT;
    if (c.mods & ModShift) m |= MOD_SHIFT;
    if (c.mods & ModWin) m |= MOD_WIN;
    return m;
}

void applyHotkeysLocked() {
    for (int slot = 1; slot < 8; ++slot) {
        UnregisterHotKey(nullptr, slot);
        const QString combo = g_hk.combos[slot - 1];
        if (combo.isEmpty()) continue;
        KeyCombo c = parseCombo(combo);
        if (!c.vk) continue;
        if (!RegisterHotKey(nullptr, slot, winMods(c) | MOD_NOREPEAT, vkToWin(c.vk)))
            logLine(QString("热键 %1 注册失败（可能被别的程序占了）").arg(c.label()));
    }
}

// ---------------- 钩子 ----------------
struct HookState {
    std::thread thread;
    DWORD tid = 0;
    std::atomic<bool> running{false};
    std::atomic<bool> active{false};
    EventSink sink;
    HHOOK mouseHook = nullptr;
    HHOOK keyHook = nullptr;
    QElapsedTimer clock;
    quint32 selfPid = 0;
};
HookState g_hook;

void emitEvent(const RecEvent& e) {
    if (g_hook.sink) g_hook.sink(e);
}

bool isSelfWindowAtPoint(const POINT& pt) {
    HWND w = WindowFromPoint(pt);
    if (!w) return false;
    return pidOfWindow(w) == g_hook.selfPid;
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        MSLLHOOKSTRUCT* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        if (ms && !(ms->flags & LLMHF_INJECTED) && !isSelfWindowAtPoint(ms->pt)) {
            RecEvent e;
            e.t = int(g_hook.clock.elapsed());
            e.x = int(ms->pt.x);
            e.y = int(ms->pt.y);
            switch (wp) {
                case WM_MOUSEMOVE: {
                    static QPoint last;
                    if ((QPoint(e.x, e.y) - last).manhattanLength() < 2) break;
                    last = QPoint(e.x, e.y);
                    e.kind = E_Move;
                    emitEvent(e);
                    break;
                }
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP: {
                    const bool down = wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN;
                    e.kind = down ? E_Down : E_Up;
                    e.btn = (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP)
                                ? BtnLeft
                                : ((wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP) ? BtnRight : BtnMiddle);
                    emitEvent(e);
                    break;
                }
                case WM_XBUTTONDOWN:
                case WM_XBUTTONUP: {
                    const WORD which = HIWORD(ms->mouseData);
                    e.kind = wp == WM_XBUTTONDOWN ? E_Down : E_Up;
                    e.btn = which == XBUTTON1 ? BtnX1 : BtnX2;
                    emitEvent(e);
                    break;
                }
                case WM_MOUSEWHEEL: {
                    const int delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);
                    e.kind = E_Wheel;
                    e.steps = delta > 0 ? 1 : -1;
                    emitEvent(e);
                    break;
                }
                default:
                    break;
            }
        }
    }
    return CallNextHookEx(g_hook.mouseHook, code, wp, lp);
}

LRESULT CALLBACK keyHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        KBDLLHOOKSTRUCT* ks = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (ks && !(ks->flags & LLKHF_INJECTED)) {
            int vk = int(ks->vkCode);
            bool ext = (ks->flags & LLKHF_EXTENDED) != 0;
            switch (vk) {  // 左右修饰键统一成一个
                case VK_LSHIFT: case VK_RSHIFT: vk = VK_SHIFT; break;
                case VK_LCONTROL: case VK_RCONTROL: vk = VK_CONTROL; break;
                case VK_LMENU: case VK_RMENU: vk = VK_MENU; break;
                case VK_LWIN: case VK_RWIN: vk = 0x5B; break;
                default: break;
            }
            if (vk == 0x5B) ext = false;
            RecEvent e;
            e.t = int(g_hook.clock.elapsed());
            e.kind = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) ? E_KDown : E_KUp;
            e.vk = vk;
            e.ext = ext;
            emitEvent(e);
        }
    }
    return CallNextHookEx(g_hook.keyHook, code, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
bool init() {
    if (g_inited) return true;
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_vx = vx;
    g_vy = vy;
    g_vw = vw > 0 ? vw : GetSystemMetrics(SM_CXSCREEN);
    g_vh = vh > 0 ? vh : GetSystemMetrics(SM_CYSCREEN);
    g_inited = true;
    logLine(QString("Win32 初始化完成：虚拟桌面 %1x%2 @ (%3,%4)").arg(g_vw).arg(g_vh).arg(g_vx).arg(g_vy));
    return true;
}

void shutdown() {
    removeHook();
    stopHotkeys();
}

QString backendName() { return "win32"; }
QRect virtualScreen() { init(); return QRect(g_vx, g_vy, g_vw, g_vh); }
QSize screenSize() { init(); return QSize(g_vw, g_vh); }

QPoint cursorPos() {
    POINT p{};
    GetCursorPos(&p);
    return QPoint(int(p.x), int(p.y));
}

void moveTo(const QPoint& pt) {
    init();
    const int w = g_vw > 1 ? g_vw - 1 : 1;
    const int h = g_vh > 1 ? g_vh - 1 : 1;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in.mi.dx = LONG((pt.x() - g_vx) * 65535LL / w);
    in.mi.dy = LONG((pt.y() - g_vy) * 65535LL / h);
    SendInput(1, &in, sizeof(INPUT));
}

static void sendButton(int btn, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    switch (btn) {
        case BtnLeft: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
        case BtnRight: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
        case BtnMiddle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        case BtnX1:
        case BtnX2:
            in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            in.mi.mouseData = btn == BtnX1 ? XBUTTON1 : XBUTTON2;
            break;
        default:
            return;
    }
    SendInput(1, &in, sizeof(INPUT));
}

void mouseDown(int btn) { sendButton(btn, true); }
void mouseUp(int btn) { sendButton(btn, false); }

void click(int btn, bool dbl) {
    sendButton(btn, true);
    sendButton(btn, false);
    if (dbl) {
        QThread::msleep(80);
        sendButton(btn, true);
        sendButton(btn, false);
    }
}

void wheel(int steps) {
    if (!steps) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = DWORD(steps * WHEEL_DELTA);
    SendInput(1, &in, sizeof(INPUT));
}

void key(int vk, bool up, bool extended, int scan) {
    Q_UNUSED(scan);
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vkToWin(vk);
    in.ki.wScan = 0;
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | ((extended || isExtendedVk(vk)) ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(INPUT));
}

void keyTap(int vk) {
    key(vk, false);
    key(vk, true);
}

QImage grabScreen(const QRect& area) {
    const QRect a = area.isNull() ? virtualScreen() : area;
    return grabRect(a);
}

QRect windowRect(quintptr id) {
    HWND hwnd = reinterpret_cast<HWND>(id);
    if (!hwnd || !IsWindow(hwnd)) return {};
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return {};
    return QRect(int(r.left), int(r.top), int(r.right - r.left), int(r.bottom - r.top));
}

QImage grabWindow(quintptr id) {
    HWND hwnd = reinterpret_cast<HWND>(id);
    if (!hwnd || !IsWindow(hwnd)) return {};
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return {};
    const int w = int(r.right - r.left), h = int(r.bottom - r.top);
    if (w <= 0 || h <= 0) return {};

    HDC wdc = GetWindowDC(hwnd);
    if (wdc) {
        HDC mem = CreateCompatibleDC(wdc);
        HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
        SelectObject(mem, bmp);
        const UINT flags = 0x2;  // PW_RENDERFULLCONTENT：能抓到被遮挡 / 后台渲染的窗口
        const BOOL ok = PrintWindow(hwnd, mem, flags);
        QImage out = fromHBitmap(bmp, mem, w, h);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(hwnd, wdc);
        if (ok && !out.isNull()) {
            const QColor c = out.pixel(0, 0);
            if (!(c.red() == 0 && c.green() == 0 && c.blue() == 0)) return out;
        }
    }
    return grabRect(QRect(int(r.left), int(r.top), w, h));
}

bool supportsOccludedCapture() { return true; }

QVector<WindowInfo> windows() {
    QVector<WindowInfo> out;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* list = reinterpret_cast<QVector<WindowInfo>*>(lp);
            if (!IsWindowVisible(hwnd)) return TRUE;
            if (GetWindow(hwnd, GW_OWNER)) return TRUE;
            const LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_TOOLWINDOW) return TRUE;
            if (pidOfWindow(hwnd) == selfPid()) return TRUE;
            const QString title = windowTitle(hwnd);
            const QString proc = procNameOfWindow(hwnd);
            if (title.isEmpty() && proc.isEmpty()) return TRUE;
            RECT r{};
            GetWindowRect(hwnd, &r);
            WindowInfo wi;
            wi.id = quintptr(hwnd);
            wi.title = title;
            wi.process = proc;
            wi.rect = QRect(int(r.left), int(r.top), int(r.right - r.left), int(r.bottom - r.top));
            wi.visible = true;
            list->append(wi);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

quintptr findWindow(const QString& processLike, const QString& titleLike) {
    const QString p = processLike.trimmed().toLower();
    const QString t = titleLike.trimmed().toLower();
    quintptr best = 0;
    int bestArea = -1;
    for (const WindowInfo& wi : windows()) {
        bool ok = true;
        if (!p.isEmpty()) {
            QString proc = wi.process.toLower();
            QString want = p;
            if (proc.endsWith(".exe")) proc.chop(4);
            if (want.endsWith(".exe")) want.chop(4);
            ok = proc == want || proc.contains(want);
        }
        if (ok && !t.isEmpty()) ok = wi.title.toLower().contains(t);
        if (!ok) continue;
        const int area = wi.rect.width() * wi.rect.height();
        if (area >= bestArea) {
            bestArea = area;
            best = wi.id;
        }
    }
    return best;
}

bool windowExists(quintptr id) {
    HWND hwnd = reinterpret_cast<HWND>(id);
    return hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd);
}

bool activateWindow(quintptr id) {
    HWND hwnd = reinterpret_cast<HWND>(id);
    if (!hwnd || !IsWindow(hwnd)) return false;

    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    else ShowWindow(hwnd, SW_SHOW);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    if (!SetForegroundWindow(hwnd)) {
        // 系统限制前台切换时，用 Alt 键的技巧绕过
        INPUT alt{};
        alt.type = INPUT_KEYBOARD;
        alt.ki.wVk = VK_MENU;
        SendInput(1, &alt, sizeof(INPUT));
        INPUT altUp = alt;
        altUp.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &altUp, sizeof(INPUT));
        SetForegroundWindow(hwnd);
    }
    SetActiveWindow(hwnd);
    QThread::msleep(120);
    return true;
}

QString processNameOf(quintptr id) {
    HWND hwnd = reinterpret_cast<HWND>(id);
    if (!hwnd) return {};
    return procNameOfWindow(hwnd);
}

// ---------------------------------------------------------------------------
bool startHotkeys(HotkeySink sink) {
    if (g_hk.running) {
        g_hk.sink = std::move(sink);
        return true;
    }
    g_hk.sink = std::move(sink);
    g_hk.running = true;
    g_hk.readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hk.thread = std::thread([] {
        MSG msg;
        PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        g_hk.tid = GetCurrentThreadId();
        {
            QMutexLocker lock(&g_hk.mu);
            applyHotkeysLocked();
        }
        SetEvent(g_hk.readyEvent);
        while (g_hk.running) {
            if (GetMessageW(&msg, nullptr, 0, 0) == 0) break;
            if (msg.message == WM_HOTKEY) {
                const int slot = int(msg.wParam);
                if (g_hk.sink) g_hk.sink(slot);
            } else if (msg.message == WM_AC_APPLY) {
                QMutexLocker lock(&g_hk.mu);
                applyHotkeysLocked();
            }
        }
        for (int slot = 1; slot < 8; ++slot) UnregisterHotKey(nullptr, slot);
    });
    WaitForSingleObject(g_hk.readyEvent, 2000);
    CloseHandle(g_hk.readyEvent);
    g_hk.readyEvent = nullptr;
    return true;
}

void setHotkey(int slot, const QString& canonical) {
    if (!g_hk.running || slot <= 0 || slot >= 8) return;
    {
        QMutexLocker lock(&g_hk.mu);
        g_hk.combos[slot - 1] = canonical;
    }
    if (g_hk.tid) PostThreadMessageW(g_hk.tid, WM_AC_APPLY, 0, 0);
}

void stopHotkeys() {
    if (!g_hk.running) return;
    g_hk.running = false;
    if (g_hk.tid) PostThreadMessageW(g_hk.tid, WM_QUIT, 0, 0);
    if (g_hk.thread.joinable()) g_hk.thread.join();
    g_hk.tid = 0;
    g_hk.sink = nullptr;
}

// ---------------------------------------------------------------------------
bool installHook(EventSink sink) {
    if (g_hook.active) {
        g_hook.sink = std::move(sink);
        return true;
    }
    g_hook.sink = std::move(sink);
    g_hook.selfPid = selfPid();
    g_hook.clock.start();
    g_hook.running = true;
    g_hook.thread = std::thread([] {
        MSG msg;
        PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        g_hook.tid = GetCurrentThreadId();
        g_hook.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, nullptr, 0);
        g_hook.keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyHookProc, nullptr, 0);
        g_hook.active = g_hook.mouseHook != nullptr;
        while (g_hook.running) {
            if (GetMessageW(&msg, nullptr, 0, 0) == 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_hook.mouseHook) UnhookWindowsHookEx(g_hook.mouseHook);
        if (g_hook.keyHook) UnhookWindowsHookEx(g_hook.keyHook);
        g_hook.mouseHook = g_hook.keyHook = nullptr;
        g_hook.active = false;
    });
    for (int i = 0; i < 50 && !g_hook.active; ++i) QThread::msleep(20);
    if (g_hook.active) logLine("Win32：全局录制的钩子已装上");
    return g_hook.active;
}

void removeHook() {
    if (!g_hook.running) return;
    g_hook.running = false;
    if (g_hook.tid) PostThreadMessageW(g_hook.tid, WM_QUIT, 0, 0);
    if (g_hook.thread.joinable()) g_hook.thread.join();
    g_hook.tid = 0;
    g_hook.sink = nullptr;
    logLine("Win32：录制钩子已卸载");
}

bool hookActive() { return g_hook.active; }

}  // namespace pl
}  // namespace ac
