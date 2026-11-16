// 无界面依赖的自检：键名映射、配置回环、找图、脚本读写、任务启停。
#include "core/Config.h"
#include "core/ImageMatch.h"
#include "core/Keys.h"
#include "core/MouseTask.h"
#include "core/Recorder.h"
#include "platform/Platform.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <iostream>

using namespace ac;

namespace {

int g_fail = 0;

void check(bool ok, const QString& what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what.toStdString() << std::endl;
    if (!ok) ++g_fail;
}

QImage makeHaystack() {
    QImage img(300, 200, QImage::Format_RGB32);
    img.fill(QColor(210, 214, 220));
    QPainter p(&img);
    // 一些无关的噪声块
    for (int i = 0; i < 30; ++i) {
        p.fillRect((i * 37) % 280, (i * 23) % 180, 8, 6, QColor(120 + i, 90, 140));
    }
    // 目标图案：黑白棋子格
    const QRect target(120, 80, 40, 30);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            p.fillRect(target.x() + x * 5, target.y() + y * 5, 5, 5,
                       (x + y) % 2 ? QColor(20, 20, 20) : QColor(240, 240, 240));
        }
    }
    p.end();
    return img;
}

void testKeys() {
    KeyCombo c = parseCombo("ctrl+alt+F9");
    check(c.valid(), "组合键能解析");
    check(c.canonical == "ctrl+alt+f9", "组合键规范化 = ctrl+alt+f9（实际 " + c.canonical + "）");
    check(c.label() == "Ctrl+Alt+F9", "显示名 = Ctrl+Alt+F9（实际 " + c.label() + "）");
    check(keyVk("f9") == 0x78, "f9 -> 0x78");
    check(keyVk("space") == 0x20, "space -> 0x20");
    check(keyName(0x78) == "f9", "0x78 -> f9");
    check(comboFromVk(ModCtrl | ModShift, 0x70) == "ctrl+shift+f1", "反向拼串 ctrl+shift+f1");
    check(!parseCombo("ctrl+").valid(), "残缺组合键判为无效");
    // 不再只有 F1–F12：字母、方向键、小键盘、F13+、符号键都要能解析
    check(parseCombo("g").vk == 0x47, "字母 g 可绑定");
    check(parseCombo("ctrl+alt+x").mods == (ModCtrl | ModAlt), "组合键修饰位正确");
    check(parseCombo("up").vk == 0x26, "方向键 up 可绑定");
    check(parseCombo("num5").vk == 0x65, "小键盘 5 可绑定");
    check(parseCombo("f13").vk == 0x7C, "F13 可绑定");
    check(parseCombo("minus").vk == 0xBD, "符号键 - 可绑定");
}

void testConfig() {
    Config c;
    c.mouse.interval = 777;
    c.image.confidence = 0.93;
    c.image.tmpl = "/tmp/x.png";
    c.watch.process = "game.exe";
    c.record.loops = 7;
    c.click.type = "x1";
    c.hotkey.key = "ctrl+shift+g";
    c.hotkey.enabled = true;
    check(c.save(), "配置能写盘");
    Config back = Config::load();
    check(back.mouse.interval == 777, "间隔回读一致");
    check(qAbs(back.image.confidence - 0.93) < 1e-6, "置信度回读一致");
    check(back.watch.process == "game.exe", "进程名回读一致");
    check(back.click.type == "x1", "点击类型回读一致");
    check(back.hotkey.key == "ctrl+shift+g", "全局热键回读一致");
    check(back.hotkey.enabled, "全局热键开关回读一致");
}

void testMatch() {
    QImage hay = makeHaystack();
    QImage tpl = hay.copy(QRect(120, 80, 40, 30));
    MatchOutcome mo = findTemplate(hay, tpl, 0.85, false, 1);
    check(!mo.hits.isEmpty(), "同图同区域能命中");
    if (!mo.hits.isEmpty()) {
        check(mo.best > 0.95, QString("匹配度够高（%1）").arg(mo.best, 0, 'f', 3));
        check((mo.hits.first().center - QPoint(140, 95)).manhattanLength() <= 3,
              "命中中心接近 (140,95)");
    }
    QImage flat(40, 30, QImage::Format_RGB32);
    flat.fill(QColor(200, 200, 200));
    MatchOutcome flatOut = findTemplate(hay, flat, 0.85, false, 1);
    check(flatOut.tooFlat, "纯色模板被判为不可用");
}

void testScript() {
    QVector<pl::RecEvent> events;
    pl::RecEvent a; a.kind = pl::E_Move; a.t = 0; a.x = 10; a.y = 20; events << a;
    pl::RecEvent b; b.kind = pl::E_Down; b.t = 120; b.x = 10; b.y = 20; b.btn = 0; events << b;
    pl::RecEvent c; c.kind = pl::E_Up; c.t = 160; c.x = 10; c.y = 20; c.btn = 0; events << c;
    pl::RecEvent d; d.kind = pl::E_KDown; d.t = 300; d.vk = 0x70; events << d;
    pl::RecEvent e; e.kind = pl::E_Wheel; e.t = 420; e.x = 5; e.y = 6; e.steps = 2; events << e;

    const QString path = saveScript("自检脚本_删除我", events, QSize(1920, 1080));
    check(!path.isEmpty(), "脚本能保存");
    Script sc;
    check(loadScript(path, &sc), "脚本能读回");
    check(sc.events.size() == events.size(), QString("事件数量一致（%1）").arg(sc.events.size()));
    if (sc.events.size() == events.size()) {
        check(sc.events[3].vk == 0x70, "按键事件回读一致");
        check(sc.events[4].steps == 2, "滚轮事件回读一致");
    }
    check(sc.screen == QSize(1920, 1080), "屏幕尺寸回读一致");
    check(deleteScript(path), "脚本能删除");
}

void testTask() {
    pl::init();
    MouseTask task(30, pl::BtnLeft, false, 0);
    task.launch();
    QThread::msleep(260);
    task.stop();
    task.waitDone(2000);
    check(!task.running(), "任务能被停止");
    check(task.clicks() > 0, QString("任务确实点击了（%1 次）").arg(task.clicks()));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::cout << "== auto-clicker 自检 ==" << std::endl;
    testKeys();
    testConfig();
    testMatch();
    testScript();
    testTask();
    std::cout << (g_fail == 0 ? "全部通过" : QString("%1 项失败").arg(g_fail).toStdString()) << std::endl;
    return g_fail == 0 ? 0 : 1;
}
