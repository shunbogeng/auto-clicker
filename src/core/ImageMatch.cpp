#include "core/ImageMatch.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace ac {

namespace {

cv::Mat toBgr(const QImage& img) {
    if (img.isNull()) return {};
    QImage src = img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32
                     ? img
                     : img.convertToFormat(QImage::Format_RGB32);
    cv::Mat rgba(src.height(), src.width(), CV_8UC4,
                 const_cast<uchar*>(reinterpret_cast<const uchar*>(src.constBits())),
                 size_t(src.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

}  // namespace

MatchOutcome findTemplate(const QImage& haystack, const QImage& tmpl, double threshold, bool multi,
                          int maxCount) {
    MatchOutcome out;
    if (haystack.isNull()) {
        out.reason = "画面为空";
        return out;
    }
    if (tmpl.isNull()) {
        out.reason = "模板图片无效或未设置";
        return out;
    }
    cv::Mat hay = toBgr(haystack);
    cv::Mat tpl = toBgr(tmpl);
    if (hay.empty() || tpl.empty() || tpl.cols > hay.cols || tpl.rows > hay.rows) {
        out.reason = "模板比画面还大（或图片读不出来）";
        return out;
    }

    // 纯色/渐变模板没有可靠的定位特征，直接判为不可用（Python 版同样处理）
    cv::Mat gray;
    cv::cvtColor(tpl, gray, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, stddev;
    cv::meanStdDev(gray, mean, stddev);
    if (stddev[0] < 2.0) {
        out.tooFlat = true;
        out.reason = "这张图几乎是纯色，没法可靠定位，请换一块有文字/图案的区域";
        return out;
    }

    cv::Mat result;
    cv::matchTemplate(hay, tpl, result, cv::TM_CCOEFF_NORMED);

    // 收集所有超过阈值的候选，再做简单去重（同一目标只留最高分）
    struct Cand {
        double score;
        int x, y;
    };
    QVector<Cand> cands;
    for (int y = 0; y < result.rows; ++y) {
        const float* row = result.ptr<float>(y);
        for (int x = 0; x < result.cols; ++x) {
            const double s = row[x];
            if (!std::isfinite(s)) continue;
            if (s > out.best) out.best = s;
            if (s >= threshold) cands.append(Cand{s, x, y});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });

    const int tw = tpl.cols, th = tpl.rows;
    for (const Cand& c : cands) {
        QRect box(c.x, c.y, tw, th);
        bool overlaps = false;
        for (const MatchHit& h : out.hits) {
            if (box.intersects(h.box.adjusted(tw / 3, th / 3, -tw / 3, -th / 3))) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) continue;
        MatchHit hit;
        hit.score = c.score;
        hit.box = box;
        hit.center = QPoint(c.x + tw / 2, c.y + th / 2);
        out.hits.append(hit);
        if (!multi || out.hits.size() >= qMax(1, maxCount)) break;
    }

    if (out.hits.isEmpty()) {
        out.reason = QString("没找到：最高匹配度只有 %1").arg(out.best, 0, 'f', 3);
    }
    return out;
}

}  // namespace ac
