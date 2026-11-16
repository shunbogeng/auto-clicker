// 主窗口：五个页签（鼠标连点 / 找图连点 / 监视窗口 / 录制回放 / 日志），界面只调 App。
#pragma once

#include <QMainWindow>
#include <QVector>

#include "core/Recorder.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace ac {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QWidget* buildMousePage();
    QWidget* buildImagePage();
    QWidget* buildWatchPage();
    QWidget* buildRecordPage();
    QWidget* buildLogPage();

    void refreshUi();
    void refreshScripts();
    void refreshWindows();
    void setTemplateForCurrentTab(const QString& path);
    void updatePreview();
    void bindEdits();
    void syncConfigFromUi();
    void loadTemplatePreview(QLabel* label, const QString& path);
    void setStatusPill(const QString& text, const QString& color);
    QString templatesDirHint();

    QTabWidget* m_tabs = nullptr;
    QLabel* m_pill = nullptr;
    QLabel* m_hint = nullptr;
    QTimer* m_poll = nullptr;

    // 鼠标页
    QSpinBox* m_mouseInterval = nullptr;
    QPushButton* m_mouseBtn = nullptr;
    QLabel* m_clickTypeLabel = nullptr;
    QLabel* m_mouseState = nullptr;


    // 找图页
    QLabel* m_imgPreview = nullptr;
    QLineEdit* m_imgPath = nullptr;
    QLabel* m_imgState = nullptr;
    QSlider* m_imgConf = nullptr;
    QLabel* m_imgConfVal = nullptr;
    QSpinBox* m_imgInterval = nullptr;
    QSpinBox* m_imgOffX = nullptr;
    QSpinBox* m_imgOffY = nullptr;
    QCheckBox* m_imgLimitOn = nullptr;
    QSpinBox* m_imgLimit = nullptr;
    QComboBox* m_imgOnMiss = nullptr;
    QPushButton* m_imgBtn = nullptr;
    QPushButton* m_imgTestBtn = nullptr;


    // 监视页
    QLabel* m_watchPreview = nullptr;
    QLineEdit* m_watchPath = nullptr;
    QLineEdit* m_watchProcess = nullptr;
    QLineEdit* m_watchTitle = nullptr;
    QComboBox* m_watchWindows = nullptr;
    QSlider* m_watchConf = nullptr;
    QLabel* m_watchConfVal = nullptr;
    QSpinBox* m_watchInterval = nullptr;
    QCheckBox* m_watchLimitOn = nullptr;
    QSpinBox* m_watchLimit = nullptr;
    QPushButton* m_watchBtn = nullptr;
    QLabel* m_watchState = nullptr;


    // 录制页
    QComboBox* m_scriptCombo = nullptr;
    QLabel* m_scriptInfo = nullptr;
    QComboBox* m_speedMode = nullptr;
    QSpinBox* m_fixedInterval = nullptr;
    QSpinBox* m_loops = nullptr;
    QCheckBox* m_loopForever = nullptr;
    QPushButton* m_recBtn = nullptr;
    QPushButton* m_playBtn = nullptr;
    QLabel* m_recState = nullptr;


    QPlainTextEdit* m_log = nullptr;
    bool m_loading = false;
};

}  // namespace ac
