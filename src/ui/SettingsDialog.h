// 全局设置：点击类型（含键盘/侧键）、双击、急停热键。
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;

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
    QComboBox* m_emgKey = nullptr;
    QCheckBox* m_emgOn = nullptr;
};

}  // namespace ac
