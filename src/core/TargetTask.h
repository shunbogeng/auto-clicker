// 找图连点 + 监视窗口：两套模式共用同一段找图/点击逻辑，差别只在「画面从哪来、点之前要不要置顶」。
#pragma once

#include "core/Task.h"
#include <QImage>
#include <QString>

namespace ac {

struct TargetParams {
    bool watch = false;          // true = 监视某个窗口
    QImage tmpl;
    double confidence = 0.85;
    int interval = 300;
    int offx = 0;
    int offy = 0;
    bool multi = false;
    bool limitEnabled = false;
    int limitCount = 1;
    QString onMiss = "wait";
    QString process;             // watch 用
    QString title;               // watch 用
    int button = 0;              // pl::MouseButton
    bool dbl = false;
};

class TargetTask : public Task {
    Q_OBJECT
public:
    TargetTask(TargetParams p, QObject* parent = nullptr);

signals:
    void hitInfo(double score, QPoint pt);

protected:
    void main() override;

private:
    TargetParams m_p;
};

}  // namespace ac
