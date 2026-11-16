#include "ui/CaptureOverlay.h"

#include "core/Log.h"
#include "platform/Platform.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace ac {

CaptureOverlay::CaptureOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::CrossCursor);
    QRect virt = pl::virtualScreen();
    if (virt.isEmpty()) {
        QScreen* scr = QGuiApplication::primaryScreen();
        if (scr) virt = scr->geometry();
    }
    m_bg = pl::grabScreen(virt);
    setGeometry(virt);
}

void CaptureOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawImage(rect(), m_bg);
    p.fillRect(rect(), QColor(0, 0, 0, 90));
    QRect sel = QRect(m_start, m_cur).normalized();
    if (m_dragging && sel.width() > 1 && sel.height() > 1) {
        p.drawImage(sel, m_bg, QRect(sel.topLeft() - geometry().topLeft(), sel.size()));
        QPen pen(QColor(56, 189, 248), 2);
        p.setPen(pen);
        p.drawRect(sel);
        p.setPen(QColor(255, 255, 255));
        p.drawText(sel.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop,
                   QString("%1 × %2").arg(sel.width()).arg(sel.height()));
    } else {
        p.setPen(QColor(255, 255, 255));
        p.drawText(rect(), Qt::AlignCenter, "拖动鼠标框出要查找的图片区域，Esc 取消");
    }
}

void CaptureOverlay::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_start = e->pos();
    m_cur = m_start;
    m_dragging = true;
    update();
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) return;
    m_cur = e->pos();
    update();
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_dragging || e->button() != Qt::LeftButton) return;
    m_dragging = false;
    m_cur = e->pos();
    QRect sel = QRect(m_start, m_cur).normalized();
    if (sel.width() < 8 || sel.height() < 8) {
        logLine("框选太小了，重新截一次");
        close();
        return;
    }
    m_done = true;
    QImage img = m_bg.copy(QRect(sel.topLeft(), sel.size()));
    emit captured(img);
    close();
}

void CaptureOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

}  // namespace ac
