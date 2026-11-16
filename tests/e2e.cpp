// 端到端测试：在 Xvfb(X11) 下真注入输入，验证「找图命中 -> 点击落点 -> 录制 -> 回放」。
// 用法：ac_e2e
#include "core/ImageMatch.h"
#include "core/Log.h"
#include "core/Recorder.h"
#include "core/TargetTask.h"
#include "core/ImageMatch.h"
#include "platform/Platform.h"

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <atomic>
#include <QDebug>
#include <iostream>

using namespace ac;

namespace {

int g_fail = 0;

void check(bool ok, const QString& what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what.toStdString() << std::endl;
    if (!ok) ++g_fail;
}

QStringList readLog(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
}

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

// 子进程模式：从另一个进程注入点击，这样父进程的录制钩子不会把它当成「自己注入的」过滤掉
int injectClicks(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "用法: ac_e2e --inject-clicks x1 y1 x2 y2" << std::endl;
        return 2;
    }
    if (!pl::init()) return 3;
    const int x1 = atoi(argv[2]), y1 = atoi(argv[3]), x2 = atoi(argv[4]), y2 = atoi(argv[5]);
    pl::moveTo(QPoint(x1, y1));
    QThread::msleep(40);
    pl::click(pl::BtnLeft, false);
    QThread::msleep(120);
    pl::moveTo(QPoint(x2, y2));
    QThread::msleep(40);
    pl::click(pl::BtnLeft, false);
    QThread::msleep(120);
    pl::shutdown();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::cout << "== auto-clicker 端到端测试 ==" << std::endl;

    if (argc > 1 && QString(argv[1]) == "--inject-clicks") {
        return injectClicks(argc, argv);
    }
    if (argc > 2 && QString(argv[1]) == "--inject-key") {
        if (!pl::init()) return 3;
        const int vk = argv[2][0] == '0' ? QString(argv[2]).toInt(nullptr, 16) : atoi(argv[2]);
        pl::keyTap(vk);
        QThread::msleep(120);
        pl::shutdown();
        return 0;
    }

    if (!pl::init()) {
        std::cout << "[FAIL] 平台初始化失败（X11 环境下才能跑这个测试）" << std::endl;
        return 1;
    }

    const QString logPath = "/tmp/ac_e2e_target.log";
    QFile::remove(logPath);
    QProcess target;
    target.setProgram(QCoreApplication::applicationDirPath() + "/ac_target_window");
    target.setArguments({logPath});
    target.start();
    check(target.waitForStarted(3000), "靶子窗口进程能启动");

    quintptr hwnd = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        pump(50);
        hwnd = pl::findWindow("", "AC-Target");
        if (hwnd) break;
    }
    check(hwnd != 0, "能按标题找到靶子窗口");
    if (!hwnd) {
        target.kill();
        return 1;
    }
    pump(300);

    // 读取靶子窗口里的 marker 矩形（客户端坐标）
    QRect marker;
    for (const QString& line : readLog(logPath)) {
        if (line.startsWith("marker ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() == 5) marker = QRect(parts[1].toInt(), parts[2].toInt(), parts[3].toInt(), parts[4].toInt());
        }
    }
    check(!marker.isEmpty(), "拿到了靶心矩形");

    QRect wr = pl::windowRect(hwnd);
    check(wr.width() > 100 && wr.height() > 60, QString("窗口矩形合理（%1x%2）").arg(wr.width()).arg(wr.height()));
    QPoint absCenter = wr.topLeft() + marker.center();

    // 1) 截图 + 找图
    QImage frame = pl::grabWindow(hwnd);
    check(!frame.isNull(), "能截到窗口画面");
    QImage tpl = frame.copy(marker);
    MatchOutcome mo = findTemplate(frame, tpl, 0.9, false, 1);
    check(!mo.hits.isEmpty(), "在自己的截图里能命中靶心");
    if (!mo.hits.isEmpty()) {
        check((mo.hits.first().center - marker.center()).manhattanLength() <= 3, "命中位置落在靶心");
    }

    // 2) 注入点击，确认落点正确
    QFile::remove(logPath);
    target.kill();
    target.waitForFinished(500);
    target.start();
    target.waitForStarted(3000);
    timer.restart();
    while (timer.elapsed() < 6000) {
        pump(50);
        hwnd = pl::findWindow("", "AC-Target");
        if (hwnd) break;
    }
    pump(400);
    wr = pl::windowRect(hwnd);
    marker = QRect();
    for (const QString& line : readLog(logPath)) {
        if (line.startsWith("marker ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() == 5) marker = QRect(parts[1].toInt(), parts[2].toInt(), parts[3].toInt(), parts[4].toInt());
        }
    }
    absCenter = wr.topLeft() + marker.center();
    pl::moveTo(absCenter);
    QThread::msleep(50);
    pl::click(pl::BtnLeft, false);
    pump(400);
    const QStringList log1 = readLog(logPath);
    bool gotClick = false;
    int cx = 0, cy = 0;
    for (const QString& line : log1) {
        if (line.startsWith("click ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                gotClick = true;
                cx = parts[1].toInt();
                cy = parts[2].toInt();
            }
        }
    }
    check(gotClick, "靶子窗口收到了注入的点击");
    check(qAbs(cx - marker.center().x()) <= 4 && qAbs(cy - marker.center().y()) <= 4,
          QString("点击落点正确（收到 %1,%2，期望 %3,%4）").arg(cx).arg(cy)
              .arg(marker.center().x()).arg(marker.center().y()));

    // 3) 录制：由子进程注入两次点击，父进程用钩子录下来
    QVector<pl::RecEvent> recorded;
    {
        Recorder rec;
        check(rec.start(), "录制钩子能装上");
        if (rec.recording()) {
            QProcess injector;
            injector.setProgram(QCoreApplication::applicationFilePath());
            injector.setArguments({"--inject-clicks", QString::number(absCenter.x()), QString::number(absCenter.y()),
                                   QString::number(absCenter.x() + 40), QString::number(absCenter.y() + 20)});
            injector.start();
            injector.waitForFinished(8000);
            check(injector.exitCode() == 0, QString("注入子进程正常结束（exit=%1）").arg(injector.exitCode()));
            pump(300);
            recorded = rec.stop();
        }
    }
    int downs = 0;
    for (const pl::RecEvent& e : recorded) {
        if (e.kind == pl::E_Down) ++downs;
    }
    check(downs >= 2, QString("录到了至少两次按下（%1）").arg(downs));
    if (downs >= 1) {
        bool near = false;
        for (const pl::RecEvent& e : recorded) {
            if (e.kind == pl::E_Down && (QPoint(e.x, e.y) - absCenter).manhattanLength() <= 6) near = true;
        }
        check(near, "录到的坐标接近注入点");
    }

    // 4) 回放：用录下来的动作再点一次
    if (!recorded.isEmpty()) {
        QFile::remove(logPath);
        target.kill();
        target.waitForFinished(500);
        target.start();
        target.waitForStarted(3000);
        timer.restart();
        while (timer.elapsed() < 6000) {
            pump(50);
            hwnd = pl::findWindow("", "AC-Target");
            if (hwnd) break;
        }
        pump(400);
        Player player(recorded, pl::screenSize(), "real", 200, 1, false);
        player.launch();
        player.waitDone(6000);
        pump(400);
        bool played = false;
        for (const QString& line : readLog(logPath)) {
            if (line.startsWith("click ")) played = true;
        }
        check(played, "回放能把录到的点击再打出去");
        check(!player.running(), "回放能正常结束");
    }

    // 6) 监视窗口：目标被别的窗口挡住时，也要能抓到画面并点中
    {
        pump(300);
        hwnd = pl::findWindow("ac_target_window", "");
        QRect wr2 = pl::windowRect(hwnd);
        QImage frame2 = pl::grabWindow(hwnd);
        // 拿窗口图里靶心那块当模板（先假设 marker 在客户端坐标 (40,40,200,80)）
        marker = QRect(40, 40, 200, 80);
        QImage tpl2 = frame2.copy(marker);
        check(!tpl2.isNull(), "监视模式能取到窗口画面");

        // 用一个置顶的大窗口盖住它
        QWidget cover;
        cover.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        cover.setStyleSheet("background:#7f1d1d;");
        cover.setGeometry(wr2.adjusted(-10, -40, 10, 10));
        cover.show();
        cover.raise();
        pump(500);

        QImage hidden = pl::grabWindow(hwnd);
        MatchOutcome mo2 = findTemplate(hidden, tpl2, 0.9, false, 1);
        check(!mo2.hits.isEmpty(), "窗口被遮挡后仍然抓到了目标图（XComposite）");

        TargetParams tp;
        tp.watch = true;
        tp.tmpl = tpl2;
        tp.confidence = 0.9;
        tp.interval = 250;
        tp.limitEnabled = true;
        tp.limitCount = 1;
        tp.process = "ac_target_window";
        tp.button = pl::BtnLeft;
        TargetTask task(tp);
        int clicks = 0;
        QObject::connect(&task, &Task::clicksChanged, [&clicks](int n) { clicks = n; });
        task.launch();
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 8000 && task.running()) pump(50);
        task.stop();
        task.waitDone(2000);
        pump(400);
        Qt::WindowFlags flags = cover.windowFlags();
        cover.hide();
        Q_UNUSED(flags);
        bool watchClicked = false;
        for (const QString& line : readLog(logPath)) {
            if (line.startsWith("click ")) watchClicked = true;
        }
        check(clicks >= 1, QString("监视任务自己报告点了 %1 次").arg(clicks));
        check(watchClicked, "监视任务真的点到了目标窗口");
    }

    // 5) 全局热键：抓 F8，由子进程按一下
    {
        static std::atomic<int> fired{0};
        check(pl::startHotkeys([](int slot) {
            if (slot == 1) fired.fetch_add(1);
        }), "热键监听能建立");
        pl::setHotkey(1, "f8");
        QThread::msleep(120);
        QProcess presser;
        presser.setProgram(QCoreApplication::applicationFilePath());
        presser.setArguments({"--inject-key", "0x77"});
        presser.start();
        presser.waitForFinished(4000);
        pump(400);
        check(fired.load() >= 1, QString("按 F8 能触发热键回调（%1 次）").arg(fired.load()));
        pl::setHotkey(1, QString());
        pl::stopHotkeys();
    }

    target.kill();
    target.waitForFinished(1000);
    pl::shutdown();
    std::cout << (g_fail == 0 ? "全部通过" : QString("%1 项失败").arg(g_fail).toStdString()) << std::endl;
    return g_fail == 0 ? 0 : 1;
}
