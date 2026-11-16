#include "ui/SettingsDialog.h"

#include "core/App.h"
#include "core/Keys.h"
#include "core/Log.h"
#include "platform/Platform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ac {

namespace {

// 按下哪个键就记哪个键的按钮（不再限定 F1–F12）
class KeyCapButton : public QPushButton {
public:
    explicit KeyCapButton(QWidget* parent = nullptr) : QPushButton(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMinimumWidth(130);
        setToolTip("点一下这个按钮，然后按下你要用的键；Backspace 清空");
        updateText();
    }

    void setCapture(const QString& canonical) {
        m_combo = canonical;
        updateText();
    }
    QString capture() const { return m_combo; }

protected:
    void keyPressEvent(QKeyEvent* e) override {
        const int k = e->key();
        if (k == Qt::Key_Backspace || k == Qt::Key_Delete) {
            m_combo.clear();
            updateText();
            return;
        }
        if (k == Qt::Key_Escape) {   // Esc 交给对话框
            QPushButton::keyPressEvent(e);
            return;
        }
        // 按下的是纯修饰键时先不记录，等主键
        if (k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta)
            return;

        int mods = 0;
        const Qt::KeyboardModifiers m = e->modifiers();
        if (m & Qt::ControlModifier) mods |= ModCtrl;
        if (m & Qt::AltModifier) mods |= ModAlt;
        if (m & Qt::ShiftModifier) mods |= ModShift;
        if (m & Qt::MetaModifier) mods |= ModWin;

        const QString combo = comboFromQtKey(k, mods);
        if (combo.isEmpty()) return;
        m_combo = combo;
        updateText();
    }

private:
    void updateText() {
        if (m_combo.isEmpty()) {
            setText("按下要用的键…");
            return;
        }
        const KeyCombo c = parseCombo(m_combo);
        setText(c.valid() ? c.label() : m_combo);
    }

    QString m_combo;
};

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("全局设置");
    setModal(true);

    m_type = new QComboBox(this);
    m_type->addItem("左键", "left");
    m_type->addItem("右键", "right");
    m_type->addItem("中键", "middle");
    m_type->addItem("侧键 X1", "x1");
    m_type->addItem("侧键 X2", "x2");
    m_type->addItem("键盘按键", "key");

    m_key = new QComboBox(this);
    for (const QString& n : allKeyNames()) m_key->addItem(n);

    m_dbl = new QCheckBox("双击", this);

    m_hkKey = new KeyCapButton(this);
    m_hkOn = new QCheckBox("启用", this);

    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(m_type, 1);
    typeRow->addWidget(m_key, 1);
    typeRow->addWidget(m_dbl);

    auto* hkRow = new QHBoxLayout;
    hkRow->addWidget(m_hkKey, 1);
    hkRow->addWidget(m_hkOn);

    auto* form = new QFormLayout;
    form->addRow("点击类型", typeRow);
    form->addRow("全局热键", hkRow);

    auto* tip = new QLabel("提示：键盘按键与侧键只在「模拟点击」类功能里生效；"
                           "全局热键只有一个，按下就启动/停止「当前页签」的功能，"
                           "字母、数字、F 键、方向键、小键盘、符号键都支持，也可以带 Ctrl/Alt/Shift。",
                           this);
    tip->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        applyToConfig();
        accept();
        logLine("设置已保存");
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(tip);
    lay->addWidget(buttons);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        m_key->setEnabled(m_type->currentData().toString() == "key");
    });
    loadFromConfig();
}

void SettingsDialog::loadFromConfig() {
    const Config& c = App::instance().cfg();
    int idx = m_type->findData(c.click.type);
    m_type->setCurrentIndex(idx < 0 ? 0 : idx);
    int ki = m_key->findText(c.click.key);
    if (ki >= 0) m_key->setCurrentIndex(ki);
    m_key->setEnabled(c.click.type == "key");
    m_dbl->setChecked(c.click.dbl);

    static_cast<KeyCapButton*>(m_hkKey)->setCapture(c.hotkey.key);
    m_hkOn->setChecked(c.hotkey.enabled);
}

void SettingsDialog::applyToConfig() {
    Config& c = App::instance().cfg();
    c.click.type = m_type->currentData().toString();
    c.click.key = m_key->currentText();
    c.click.dbl = m_dbl->isChecked();
    const QString captured = static_cast<KeyCapButton*>(m_hkKey)->capture();
    if (!captured.isEmpty()) c.hotkey.key = captured;
    c.hotkey.enabled = m_hkOn->isChecked();
    App::instance().saveCfg();
    App::instance().applyHotkeysNow();
}

}  // namespace ac
