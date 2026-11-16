// 测试用靶子窗口：画一块特征明显的图案，并把收到的点击/按键写进日志文件。
// 用法：ac_target_window <日志路径>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

namespace {

class TargetWindow : public QWidget {
public:
    explicit TargetWindow(const QString& logPath) : m_logPath(logPath) {
        setWindowTitle("AC-Target");
        resize(360, 220);
        m_marker = QRect(40, 40, 200, 80);
        QTimer::singleShot(120, this, [this] {
            write(QString("geom %1 %2 %3 %4").arg(x()).arg(y()).arg(width()).arg(height()));
            write(QString("marker %1 %2 %3 %4")
                      .arg(m_marker.x()).arg(m_marker.y()).arg(m_marker.width()).arg(m_marker.height()));
            write("ready");
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(245, 247, 250));
        // 靶心图案：深底 + 白十字 + 高对比小方块，模板匹配能稳定命中
        p.fillRect(m_marker, QColor(30, 41, 59));
        p.setPen(QPen(QColor(250, 204, 21), 6));
        p.drawLine(m_marker.left() + 10, m_marker.center().y(), m_marker.right() - 10, m_marker.center().y());
        p.drawLine(m_marker.center().x(), m_marker.top() + 8, m_marker.center().x(), m_marker.bottom() - 8);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 4; ++i) {
            p.setBrush(i % 2 ? QColor(248, 250, 252) : QColor(239, 68, 68));
            p.drawRect(m_marker.left() + 12 + i * 46, m_marker.bottom() - 22, 26, 12);
        }
        p.setPen(QColor(15, 23, 42));
        p.setBrush(Qt::NoBrush);
        p.drawText(QRect(0, height() - 60, width(), 24), Qt::AlignCenter, "AC target window");
    }

    void mousePressEvent(QMouseEvent* e) override {
        hits += 1;
        write(QString("click %1 %2 hits=%3").arg(e->pos().x()).arg(e->pos().y()).arg(hits));
        update();
    }

    void keyPressEvent(QKeyEvent* e) override {
        write(QString("key %1").arg(e->key()));
    }

    void write(const QString& line) {
        QFile f(m_logPath);
        if (f.open(QIODevice::Append)) {
            QTextStream ts(&f);
            ts << line << "\n";
        }
    }

private:
    QString m_logPath;
    QRect m_marker;
    int hits = 0;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString logPath = argc > 1 ? QString(argv[1]) : "/tmp/ac_target.log";
    TargetWindow w(logPath);
    w.show();
    return app.exec();
}
