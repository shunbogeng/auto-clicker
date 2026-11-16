// 模板匹配：与 Python 版同一套做法（TM_CCOEFF_NORMED + 纯色图特判）。
#pragma once

#include <QImage>
#include <QPoint>
#include <QString>
#include <QVector>

namespace ac {

struct MatchHit {
    double score = 0;
    QPoint center;   // 屏幕/画面坐标
    QRect box;       // 命中矩形
};

struct MatchOutcome {
    QVector<MatchHit> hits;
    double best = 0;      // 最高匹配度（即使低于阈值也会填）
    bool tooFlat = false; // 模板几乎是纯色，无法可靠定位
    QString reason;       // 给人看的原因
};

MatchOutcome findTemplate(const QImage& haystack, const QImage& tmpl, double threshold,
                          bool multi, int maxCount);

}  // namespace ac
