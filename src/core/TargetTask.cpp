#include "core/TargetTask.h"

#include "core/ImageMatch.h"
#include "core/Log.h"
#include "platform/Platform.h"

#include <QElapsedTimer>

namespace ac {

TargetTask::TargetTask(TargetParams p, QObject* parent) : Task(parent), m_p(std::move(p)) {}

void TargetTask::main() {
    if (m_p.tmpl.isNull()) {
        emit note("模板图片无效或未设置");
        return;
    }
    const int interval = qMax(50, m_p.interval);
    quintptr hwnd = 0;
    int clicksDone = 0;
    QElapsedTimer nagTimer;
    nagTimer.start();
    bool firstRound = true;

    while (!stopped()) {
        QImage frame;
        QPoint origin(0, 0);

        if (m_p.watch) {
            if (hwnd && !pl::windowExists(hwnd)) hwnd = 0;
            if (!hwnd) {
                hwnd = pl::findWindow(m_p.process, m_p.title);
                if (hwnd) {
                    emit note(QString("已锁定目标窗口：%1").arg(pl::processNameOf(hwnd)));
                } else if (nagTimer.elapsed() > 1500 || firstRound) {
                    emit note(QString("还没找到目标窗口（%1），继续等…")
                                  .arg(m_p.process.isEmpty() ? m_p.title : m_p.process));
                    nagTimer.restart();
                }
                sleepMs(interval);
                firstRound = false;
                continue;
            }
            frame = pl::grabWindow(hwnd);
            if (frame.isNull()) {
                if (nagTimer.elapsed() > 1500) {
                    emit note("取不到目标窗口的画面（可能被最小化或不允许截取），继续等…");
                    nagTimer.restart();
                }
                sleepMs(interval);
                continue;
            }
        } else {
            frame = pl::grabScreen(pl::virtualScreen());
            if (frame.isNull()) {
                emit note("截屏失败");
                sleepMs(interval);
                continue;
            }
        }

        MatchOutcome mo = findTemplate(frame, m_p.tmpl, m_p.confidence, m_p.multi, 64);
        if (mo.hits.isEmpty()) {
            if (mo.tooFlat) {
                emit note(mo.reason);
                return;  // 模板本身不可用，直接结束
            }
            if (nagTimer.elapsed() > 1000) {
                emit note(QString("没找到：最高匹配度 %1（阈值 %2）")
                              .arg(mo.best, 0, 'f', 3)
                              .arg(m_p.confidence, 0, 'f', 2));
                nagTimer.restart();
            }
            if (m_p.onMiss == "stop") {
                emit note("目标图没出现，按设置停止");
                return;
            }
            sleepMs(interval);
            continue;
        }

        int rounds = m_p.multi ? mo.hits.size() : 1;
        for (int k = 0; k < rounds && !stopped(); ++k) {
            const MatchHit& hit = mo.hits[k];
            QPoint inFrame = hit.center + QPoint(m_p.offx, m_p.offy);

            if (m_p.watch) {
                // 目标图出现了：先把这个窗口提到最上层，等它真的画出来再点
                pl::activateWindow(hwnd);
                sleepMs(250);
                QRect r = pl::windowRect(hwnd);
                if (r.isEmpty()) continue;
                origin = r.topLeft();
            }
            QPoint target = origin + inFrame;
            emit hitInfo(hit.score, target);
            emit note(QString("命中 %1（匹配度 %2）→ 点击 (%3, %4)")
                          .arg(k + 1)
                          .arg(hit.score, 0, 'f', 3)
                          .arg(target.x())
                          .arg(target.y()));
            pl::moveTo(target);
            sleepMs(20);
            pl::click(m_p.button, m_p.dbl);
            clicksDone += 1;
            addClicks(1);
            if (m_p.limitEnabled && clicksDone >= qMax(1, m_p.limitCount)) {
                emit note(QString("已完成 %1 次点击，自动停止").arg(clicksDone));
                return;
            }
            sleepMs(120);
        }
        sleepMs(interval);
        firstRound = false;
    }
}

}  // namespace ac
