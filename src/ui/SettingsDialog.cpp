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
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ac {

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

    m_emgKey = new QComboBox(this);
    for (int i = 1; i <= 12; ++i) m_emgKey->addItem(QString("f%1").arg(i));
    m_emgKey->addItem("pause");
    m_emgOn = new QCheckBox("启用", this);

    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(m_type, 1);
    typeRow->addWidget(m_key, 1);
    typeRow->addWidget(m_dbl);

    auto* emgRow = new QHBoxLayout;
    emgRow->addWidget(new QLabel("键位：", this));
    emgRow->addWidget(m_emgKey, 1);
    emgRow->addWidget(m_emgOn);

    auto* form = new QFormLayout;
    form->addRow("点击类型", typeRow);
    form->addRow("全局急停", emgRow);

    auto* tip = new QLabel("提示：键盘按键与侧键只在「模拟点击」类功能里生效；"
                           "急停键按下会立刻停掉所有任务（录制中的会先存成脚本）。",
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

    int ei = m_emgKey->findText(c.emergency.hotkey);
    if (ei >= 0) m_emgKey->setCurrentIndex(ei);
    m_emgOn->setChecked(c.emergency.enabled);
}

void SettingsDialog::applyToConfig() {
    Config& c = App::instance().cfg();
    c.click.type = m_type->currentData().toString();
    c.click.key = m_key->currentText();
    c.click.dbl = m_dbl->isChecked();
    c.emergency.hotkey = m_emgKey->currentText();
    c.emergency.enabled = m_emgOn->isChecked();
    App::instance().saveCfg();
}

}  // namespace ac
