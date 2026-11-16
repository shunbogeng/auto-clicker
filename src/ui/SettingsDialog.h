// 全局设置：点击类型（含键盘/侧键）、双击、全局热键。
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QPushButton;

namespace ac {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void loadFromConfig();
    void applyToConfig();

private:
    QComboBox* m_type = nullptr;
    QComboBox* m_key = nullptr;
    QCheckBox* m_dbl = nullptr;
    QPushButton* m_hkKey = nullptr;   // 按下即录制键位
    QCheckBox* m_hkOn = nullptr;
};

}  // namespace ac
