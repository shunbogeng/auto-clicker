// 全屏框选截图的浮层（跟着鼠标拖出一块区域）。
#pragma once

#include <QImage>
#include <QWidget>

namespace ac {

class CaptureOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CaptureOverlay(QWidget* parent = nullptr);

signals:
    void captured(const QImage& img);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QImage m_bg;
    QPoint m_start;
    QPoint m_cur;
    bool m_dragging = false;
    bool m_done = false;
};

}  // namespace ac
