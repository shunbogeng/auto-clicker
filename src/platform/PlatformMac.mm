// macOS 平台实现：CGEvent 注入、CGWindowListCreateImage 截窗、Carbon 热键、CGEventTap 录制。
// 首次运行需要在「系统设置 → 隐私与安全性」里给本程序「辅助功能」和「屏幕录制」权限。
#include "platform/Platform.h"
#include "core/Keys.h"
#include "core/Log.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

#include <QElapsedTimer>
#include <dlfcn.h>
#include <QMutex>
#include <QString>
#include <QThread>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace ac {
namespace pl {

namespace {

bool g_inited = false;
CGFloat g_scale = 1.0;   // retina 上屏幕像素 = 逻辑点 × 2
quint32 g_selfPid = 0;

QString cfToQString(CFStringRef s) {
    if (!s) return {};
    return QString::fromCFString(s);
}

// 可移植 VK -> macOS 虚拟键码（kVK_*）
int vkToMac(int vk) {
    switch (vk) {
        case 0x41: return kVK_ANSI_A;
        case 0x42: return kVK_ANSI_B;
        case 0x43: return kVK_ANSI_C;
        case 0x44: return kVK_ANSI_D;
        case 0x45: return kVK_ANSI_E;
        case 0x46: return kVK_ANSI_F;
        case 0x47: return kVK_ANSI_G;
        case 0x48: return kVK_ANSI_H;
        case 0x49: return kVK_ANSI_I;
        case 0x4A: return kVK_ANSI_J;
        case 0x4B: return kVK_ANSI_K;
        case 0x4C: return kVK_ANSI_L;
        case 0x4D: return kVK_ANSI_M;
        case 0x4E: return kVK_ANSI_N;
        case 0x4F: return kVK_ANSI_O;
        case 0x50: return kVK_ANSI_P;
        case 0x51: return kVK_ANSI_Q;
        case 0x52: return kVK_ANSI_R;
        case 0x53: return kVK_ANSI_S;
        case 0x54: return kVK_ANSI_T;
        case 0x55: return kVK_ANSI_U;
        case 0x56: return kVK_ANSI_V;
        case 0x57: return kVK_ANSI_W;
        case 0x58: return kVK_ANSI_X;
        case 0x59: return kVK_ANSI_Y;
        case 0x5A: return kVK_ANSI_Z;
        case 0x30: return kVK_ANSI_0;
        case 0x31: return kVK_ANSI_1;
        case 0x32: return kVK_ANSI_2;
        case 0x33: return kVK_ANSI_3;
        case 0x34: return kVK_ANSI_4;
        case 0x35: return kVK_ANSI_5;
        case 0x36: return kVK_ANSI_6;
        case 0x37: return kVK_ANSI_7;
        case 0x38: return kVK_ANSI_8;
        case 0x39: return kVK_ANSI_9;
        case 0x70: return kVK_F1;
        case 0x71: return kVK_F2;
        case 0x72: return kVK_F3;
        case 0x73: return kVK_F4;
        case 0x74: return kVK_F5;
        case 0x75: return kVK_F6;
        case 0x76: return kVK_F7;
        case 0x77: return kVK_F8;
        case 0x78: return kVK_F9;
        case 0x79: return kVK_F10;
        case 0x7A: return kVK_F11;
        case 0x7B: return kVK_F12;
        case 0x20: return kVK_Space;
        case 0x0D: return kVK_Return;
        case 0x09: return kVK_Tab;
        case 0x1B: return kVK_Escape;
        case 0x08: return kVK_Delete;
        case 0x2E: return kVK_ForwardDelete;
        case 0x2D: return kVK_Help;
        case 0x24: return kVK_Home;
        case 0x23: return kVK_End;
        case 0x21: return kVK_PageUp;
        case 0x22: return kVK_PageDown;
        case 0x26: return kVK_UpArrow;
        case 0x28: return kVK_DownArrow;
        case 0x25: return kVK_LeftArrow;
        case 0x27: return kVK_RightArrow;
        case 0x10: return kVK_Shift;
        case 0x11: return kVK_Control;
        case 0x12: return kVK_Option;
        case 0x5B: return kVK_Command;
        case 0x7C: return kVK_F13;
        case 0x7D: return kVK_F14;
        case 0x7E: return kVK_F15;
        case 0x7F: return kVK_F16;
        case 0x80: return kVK_F17;
        case 0x81: return kVK_F18;
        case 0x82: return kVK_F19;
        case 0x83: return kVK_F20;
        case 0x60: return kVK_ANSI_Keypad0;
        case 0x61: return kVK_ANSI_Keypad1;
        case 0x62: return kVK_ANSI_Keypad2;
        case 0x63: return kVK_ANSI_Keypad3;
        case 0x64: return kVK_ANSI_Keypad4;
        case 0x65: return kVK_ANSI_Keypad5;
        case 0x66: return kVK_ANSI_Keypad6;
        case 0x67: return kVK_ANSI_Keypad7;
        case 0x68: return kVK_ANSI_Keypad8;
        case 0x69: return kVK_ANSI_Keypad9;
        case 0x6A: return kVK_ANSI_KeypadMultiply;
        case 0x6B: return kVK_ANSI_KeypadPlus;
        case 0x6D: return kVK_ANSI_KeypadMinus;
        case 0x6E: return kVK_ANSI_KeypadDecimal;
        case 0x6F: return kVK_ANSI_KeypadDivide;
        case 0xBD: return kVK_ANSI_Minus;
        case 0xBB: return kVK_ANSI_Equal;
        case 0xBC: return kVK_ANSI_Comma;
        case 0xBE: return kVK_ANSI_Period;
        case 0xBF: return kVK_ANSI_Slash;
        case 0xDC: return kVK_ANSI_Backslash;
        case 0xBA: return kVK_ANSI_Semicolon;
        case 0xDE: return kVK_ANSI_Quote;
        case 0xDB: return kVK_ANSI_LeftBracket;
        case 0xDD: return kVK_ANSI_RightBracket;
        case 0xC0: return kVK_ANSI_Grave;
        default: return -1;
    }
}

int macToVk(int code) {
    static const int kList[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,
                                0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
                                0x58, 0x59, 0x5A, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                0x38, 0x39, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
                                0x79, 0x7A, 0x7B, 0x20, 0x0D, 0x09, 0x1B, 0x08, 0x2E, 0x24, 0x23,
                                0x21, 0x22, 0x26, 0x28, 0x25, 0x27, 0x10, 0x11, 0x12, 0x5B};
    for (int vk : kList) {
        if (vkToMac(vk) == code) return vk;
    }
    return 0;
}

// macOS 15 起 CGWindowListCreateImage 被标记为 obsoleted（编译期不可用），
// 但运行时仍然存在；用 dlsym 取符号，兼容 12–15，若真的取不到就退回屏幕裁剪。
typedef CGImageRef (*WindowImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);

CGImageRef createWindowImage(CGRect rect, CGWindowListOption opts, CGWindowID wid, CGWindowImageOption imgOpts) {
    static WindowImageFn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        fn = reinterpret_cast<WindowImageFn>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
    }
    if (!fn) return nullptr;
    return fn(rect, opts, wid, imgOpts);
}

QImage fromCGImage(CGImageRef img) {
    if (!img) return {};
    const int w = int(CGImageGetWidth(img));
    const int h = int(CGImageGetHeight(img));
    if (w <= 0 || h <= 0) return {};
    QImage out(w, h, QImage::Format_RGB32);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(out.bits(), size_t(w), size_t(h), 8, size_t(out.bytesPerLine()), cs,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return out;
}

// ---------------- 热键（Carbon） ----------------
struct HotkeyState {
    bool started = false;
    EventHandlerRef handler = nullptr;
    EventHotKeyRef refs[8] = {nullptr};
    HotkeySink sink;
};
HotkeyState g_hk;

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef ev, void*) {
    EventHotKeyID hid{};
    GetEventParameter(ev, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hid), nullptr, &hid);
    const int slot = int(hid.id);
    if (g_hk.sink && slot >= 1 && slot < 8) g_hk.sink(slot);
    return noErr;
}

// ---------------- 录制（CGEventTap） ----------------
struct HookState {
    std::thread thread;
    CFRunLoopRef runLoop = nullptr;
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef source = nullptr;
    std::atomic<bool> active{false};
    EventSink sink;
    QElapsedTimer clock;
};
HookState g_hook;

const qint64 kSelfUserData = 0x4143434B;  // 'ACK'，用于标记我们自己注入的事件

CGEventRef tapCallback(CGEventTapProxy, CGEventType type, CGEventRef ev, void* userInfo) {
    Q_UNUSED(userInfo);
    if (!ev) return ev;
    if (CGEventGetIntegerValueField(ev, kCGEventSourceUserData) == kSelfUserData) return ev;

    RecEvent e;
    e.t = int(g_hook.clock.elapsed());
    const CGPoint pt = CGEventGetLocation(ev);
    e.x = int(pt.x * g_scale);
    e.y = int(pt.y * g_scale);
    switch (type) {
        case kCGEventLeftMouseDown:
        case kCGEventRightMouseDown:
        case kCGEventOtherMouseDown:
            e.kind = E_Down;
            e.btn = type == kCGEventLeftMouseDown ? BtnLeft : (type == kCGEventRightMouseDown ? BtnRight : BtnMiddle);
            break;
        case kCGEventLeftMouseUp:
        case kCGEventRightMouseUp:
        case kCGEventOtherMouseUp:
            e.kind = E_Up;
            e.btn = type == kCGEventLeftMouseUp ? BtnLeft : (type == kCGEventRightMouseUp ? BtnRight : BtnMiddle);
            break;
        case kCGEventMouseMoved:
        case kCGEventLeftMouseDragged:
        case kCGEventRightMouseDragged:
            e.kind = E_Move;
            break;
        case kCGEventScrollWheel: {
            const qint64 v = CGEventGetIntegerValueField(ev, kCGScrollWheelEventDeltaAxis1);
            if (v == 0) return ev;
            e.kind = E_Wheel;
            e.steps = v > 0 ? 1 : -1;
            break;
        }
        case kCGEventKeyDown:
        case kCGEventKeyUp: {
            const int vk = macToVk(int(CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode)));
            if (!vk) return ev;
            e.kind = type == kCGEventKeyDown ? E_KDown : E_KUp;
            e.vk = vk;
            break;
        }
        default:
            return ev;
    }
    if (g_hook.sink) g_hook.sink(e);
    return ev;
}

void postMouse(CGEventType type, CGMouseButton button, int clickState = 1) {
    CGEventRef loc = CGEventCreate(nullptr);
    const CGPoint p = CGEventGetLocation(loc);
    CFRelease(loc);
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type, p, button);
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, kSelfUserData);
    CGEventSetIntegerValueField(ev, kCGMouseEventClickState, clickState);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

}  // namespace

// ---------------------------------------------------------------------------
bool init() {
    if (g_inited) return true;
    g_selfPid = selfPid();
    NSScreen* scr = [NSScreen mainScreen];
    g_scale = scr ? [scr backingScaleFactor] : 1.0;
    g_inited = true;

    if (!AXIsProcessTrusted()) {
        logLine("macOS：还没有「辅助功能」权限，模拟点击/热键会失效。"
                "请到 系统设置 → 隐私与安全性 → 辅助功能 里勾选本程序后重启。");
        NSDictionary* opt = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt : @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opt);
    }
    if (@available(macOS 10.15, *)) {
        if (!CGPreflightScreenCaptureAccess()) {
            logLine("macOS：还没有「屏幕录制」权限，找图会截到黑屏。"
                    "请到 系统设置 → 隐私与安全性 → 屏幕录制 里勾选本程序后重启。");
            CGRequestScreenCaptureAccess();
        }
    }
    logLine(QString("macOS 初始化完成：缩放 %1x").arg(double(g_scale)));
    return true;
}

void shutdown() {
    removeHook();
    stopHotkeys();
}

QString backendName() { return "macos"; }

QRect virtualScreen() {
    QRect r;
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    std::vector<CGDirectDisplayID> ids(count);
    CGGetActiveDisplayList(count, ids.data(), &count);
    for (uint32_t i = 0; i < count; ++i) {
        CGRect b = CGDisplayBounds(ids[i]);
        QRect one(int(b.origin.x * g_scale), int(b.origin.y * g_scale), int(b.size.width * g_scale),
                  int(b.size.height * g_scale));
        r = r.isNull() ? one : r.united(one);
    }
    if (r.isNull()) r = QRect(0, 0, 1920, 1080);
    return r;
}

QSize screenSize() { return virtualScreen().size(); }

QPoint cursorPos() {
    CGEventRef ev = CGEventCreate(nullptr);
    const CGPoint p = CGEventGetLocation(ev);
    CFRelease(ev);
    return QPoint(int(p.x * g_scale), int(p.y * g_scale));
}

void moveTo(const QPoint& pt) {
    CGWarpMouseCursorPosition(CGPointMake(pt.x() / g_scale, pt.y() / g_scale));
}

void mouseDown(int btn) {
    if (btn == BtnLeft) postMouse(kCGEventLeftMouseDown, kCGMouseButtonLeft);
    else if (btn == BtnRight) postMouse(kCGEventRightMouseDown, kCGMouseButtonRight);
    else postMouse(kCGEventOtherMouseDown, kCGMouseButtonCenter);
}

void mouseUp(int btn) {
    if (btn == BtnLeft) postMouse(kCGEventLeftMouseUp, kCGMouseButtonLeft);
    else if (btn == BtnRight) postMouse(kCGEventRightMouseUp, kCGMouseButtonRight);
    else postMouse(kCGEventOtherMouseUp, kCGMouseButtonCenter);
}

void click(int btn, bool dbl) {
    if (btn == BtnLeft) {
        postMouse(kCGEventLeftMouseDown, kCGMouseButtonLeft, 1);
        postMouse(kCGEventLeftMouseUp, kCGMouseButtonLeft, 1);
        if (dbl) {
            QThread::msleep(80);
            postMouse(kCGEventLeftMouseDown, kCGMouseButtonLeft, 2);
            postMouse(kCGEventLeftMouseUp, kCGMouseButtonLeft, 2);
        }
    } else if (btn == BtnRight) {
        postMouse(kCGEventRightMouseDown, kCGMouseButtonRight, 1);
        postMouse(kCGEventRightMouseUp, kCGMouseButtonRight, 1);
        if (dbl) {
            QThread::msleep(80);
            postMouse(kCGEventRightMouseDown, kCGMouseButtonRight, 2);
            postMouse(kCGEventRightMouseUp, kCGMouseButtonRight, 2);
        }
    } else {
        postMouse(kCGEventOtherMouseDown, kCGMouseButtonCenter);
        postMouse(kCGEventOtherMouseUp, kCGMouseButtonCenter);
    }
}

void wheel(int steps) {
    if (!steps) return;
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, int(steps));
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, kSelfUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void key(int vk, bool up, bool extended, int scan) {
    Q_UNUSED(extended);
    Q_UNUSED(scan);
    const int code = vkToMac(vk);
    if (code < 0) return;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, CGKeyCode(code), !up);
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, kSelfUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void keyTap(int vk) {
    key(vk, false);
    key(vk, true);
}

QImage grabScreen(const QRect& area) {
    const QRect a = area.isNull() ? virtualScreen() : area;
    const CGRect r = CGRectMake(a.x() / g_scale, a.y() / g_scale, a.width() / g_scale, a.height() / g_scale);
    CGImageRef img = createWindowImage(r, kCGWindowListOptionOnScreenOnly, kCGNullWindowID,
                                       kCGWindowImageBestResolution);
    QImage out = fromCGImage(img);
    if (img) CFRelease(img);
    if (!out.isNull() && (out.width() != a.width() || out.height() != a.height()))
        out = out.scaled(a.width(), a.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return out;
}

QRect windowRect(quintptr id) {
    if (!id) return {};
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, CGWindowID(id));
    QRect r;
    if (list && CFArrayGetCount(list) > 0) {
        CFDictionaryRef info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
        CFDictionaryRef bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        CGRect box{};
        if (bounds) CGRectMakeWithDictionaryRepresentation(bounds, &box);
        r = QRect(int(box.origin.x * g_scale), int(box.origin.y * g_scale), int(box.size.width * g_scale),
                  int(box.size.height * g_scale));
    }
    if (list) CFRelease(list);
    return r;
}

QImage grabWindow(quintptr id) {
    if (!id) return {};
    CGImageRef img = createWindowImage(CGRectNull, kCGWindowListOptionIncludingWindow, CGWindowID(id),
                                       kCGWindowImageBoundsIgnoreFraming);
    QImage out = fromCGImage(img);
    if (img) CFRelease(img);
    if (out.isNull()) {
        const QRect r = windowRect(id);
        if (!r.isEmpty()) out = grabScreen(r);   // 兜底：直接从屏幕裁这块
    }
    return out;
}

bool supportsOccludedCapture() { return true; }

QVector<WindowInfo> windows() {
    QVector<WindowInfo> out;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return out;
    const int n = int(CFArrayGetCount(list));
    for (int i = 0; i < n; ++i) {
        CFDictionaryRef info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        CFNumberRef layerRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
        int layer = 0;
        if (layerRef) CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
        if (layer != 0) continue;
        CFNumberRef numRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
        int wid = 0;
        if (numRef) CFNumberGetValue(numRef, kCFNumberIntType, &wid);
        CFNumberRef pidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
        int pid = 0;
        if (pidRef) CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
        if (quint32(pid) == g_selfPid) continue;
        CFStringRef owner = static_cast<CFStringRef>(CFDictionaryGetValue(info, kCGWindowOwnerName));
        CFStringRef name = static_cast<CFStringRef>(CFDictionaryGetValue(info, kCGWindowName));
        WindowInfo wi;
        wi.id = quintptr(wid);
        wi.process = cfToQString(owner);
        wi.title = cfToQString(name);
        wi.rect = windowRect(quintptr(wid));
        wi.visible = true;
        if (wi.title.isEmpty() && wi.process.isEmpty()) continue;
        out.append(wi);
    }
    CFRelease(list);
    return out;
}

quintptr findWindow(const QString& processLike, const QString& titleLike) {
    const QString p = processLike.trimmed().toLower();
    const QString t = titleLike.trimmed().toLower();
    quintptr best = 0;
    int bestArea = -1;
    for (const WindowInfo& wi : windows()) {
        bool ok = true;
        if (!p.isEmpty()) ok = wi.process.toLower().contains(p);
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
    if (!id) return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, CGWindowID(id));
    const bool ok = list && CFArrayGetCount(list) > 0;
    if (list) CFRelease(list);
    return ok;
}

bool activateWindow(quintptr id) {
    if (!id) return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, CGWindowID(id));
    if (!list || CFArrayGetCount(list) == 0) {
        if (list) CFRelease(list);
        return false;
    }
    CFDictionaryRef info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, 0));
    pid_t pid = 0;
    CFNumberRef pidRef = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
    if (pidRef) CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
    CFRelease(list);
    if (!pid) return false;

    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (app) [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];

    AXUIElementRef axApp = AXUIElementCreateApplication(pid);
    if (axApp) {
        CFTypeRef winsRef = nullptr;
        if (AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute, &winsRef) == kAXErrorSuccess && winsRef) {
            CFArrayRef wins = static_cast<CFArrayRef>(winsRef);
            const int n = int(CFArrayGetCount(wins));
            for (int i = 0; i < n; ++i) {
                AXUIElementRef w = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(wins, i));
                CFBooleanRef minRef = nullptr;
                if (AXUIElementCopyAttributeValue(w, kAXMinimizedAttribute, (CFTypeRef*)&minRef) == kAXErrorSuccess &&
                    minRef) {
                    if (CFBooleanGetValue(minRef))
                        AXUIElementSetAttributeValue(w, kAXMinimizedAttribute, kCFBooleanFalse);
                    CFRelease(minRef);
                }
                AXUIElementPerformAction(w, kAXRaiseAction);
            }
            CFRelease(winsRef);
        }
        CFRelease(axApp);
    }
    QThread::msleep(120);
    return true;
}

QString processNameOf(quintptr id) {
    if (!id) return {};
    for (const WindowInfo& wi : windows()) {
        if (wi.id == id) return wi.process;
    }
    return {};
}

// ---------------------------------------------------------------------------
bool startHotkeys(HotkeySink sink) {
    if (g_hk.started) {
        g_hk.sink = std::move(sink);
        return true;
    }
    g_hk.sink = std::move(sink);
    EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    InstallEventHandler(GetApplicationEventTarget(), hotkeyHandler, 1, &spec, nullptr, &g_hk.handler);
    g_hk.started = true;
    return true;
}

void setHotkey(int slot, const QString& canonical) {
    if (!g_hk.started || slot <= 0 || slot >= 8) return;
    if (g_hk.refs[slot]) {
        UnregisterEventHotKey(g_hk.refs[slot]);
        g_hk.refs[slot] = nullptr;
    }
    if (canonical.isEmpty()) return;
    KeyCombo c = parseCombo(canonical);
    const int code = vkToMac(c.vk);
    if (code < 0) {
        logLine(QString("热键 %1 在 macOS 上映射不到对应按键").arg(canonical));
        return;
    }
    UInt32 mods = 0;
    if (c.mods & ModCtrl) mods |= controlKey;
    if (c.mods & ModAlt) mods |= optionKey;
    if (c.mods & ModShift) mods |= shiftKey;
    if (c.mods & ModWin) mods |= cmdKey;
    EventHotKeyID hid{};
    hid.signature = 'ACKR';
    hid.id = UInt32(slot);
    EventHotKeyRef ref = nullptr;
    const OSStatus st = RegisterEventHotKey(UInt32(code), mods, hid, GetApplicationEventTarget(), 0, &ref);
    if (st != noErr) logLine(QString("热键 %1 注册失败（可能被别的程序占了）").arg(canonical));
    else g_hk.refs[slot] = ref;
}

void stopHotkeys() {
    if (!g_hk.started) return;
    for (int i = 1; i < 8; ++i) {
        if (g_hk.refs[i]) {
            UnregisterEventHotKey(g_hk.refs[i]);
            g_hk.refs[i] = nullptr;
        }
    }
    if (g_hk.handler) RemoveEventHandler(g_hk.handler);
    g_hk.handler = nullptr;
    g_hk.started = false;
    g_hk.sink = nullptr;
}

// ---------------------------------------------------------------------------
bool installHook(EventSink sink) {
    if (g_hook.active) {
        g_hook.sink = std::move(sink);
        return true;
    }
    g_hook.sink = std::move(sink);
    g_hook.clock.start();
    g_hook.thread = std::thread([] {
        CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
                           CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
                           CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp) |
                           CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDragged) |
                           CGEventMaskBit(kCGEventRightMouseDragged) | CGEventMaskBit(kCGEventScrollWheel) |
                           CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp);
        g_hook.tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask,
                                      tapCallback, nullptr);
        if (!g_hook.tap) {
            logLine("macOS：录制钩子创建失败（缺少「辅助功能」权限？）");
            return;
        }
        g_hook.source = CFMachPortCreateRunLoopSource(nullptr, g_hook.tap, 0);
        g_hook.runLoop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(g_hook.runLoop, g_hook.source, kCFRunLoopCommonModes);
        CGEventTapEnable(g_hook.tap, true);
        g_hook.active = true;
        CFRunLoopRun();
        g_hook.active = false;
    });
    for (int i = 0; i < 100 && !g_hook.active; ++i) QThread::msleep(20);
    if (g_hook.active) logLine("macOS：全局录制的钩子已装上");
    return g_hook.active;
}

void removeHook() {
    if (!g_hook.thread.joinable()) return;
    if (g_hook.tap) CGEventTapEnable(g_hook.tap, false);
    if (g_hook.runLoop) CFRunLoopStop(g_hook.runLoop);
    g_hook.thread.join();
    if (g_hook.source) CFRelease(g_hook.source);
    if (g_hook.tap) CFRelease(g_hook.tap);
    g_hook.source = nullptr;
    g_hook.tap = nullptr;
    g_hook.runLoop = nullptr;
    g_hook.sink = nullptr;
    logLine("macOS：录制钩子已卸载");
}

bool hookActive() { return g_hook.active; }

}  // namespace pl
}  // namespace ac
