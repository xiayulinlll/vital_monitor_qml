#include "ChannelConfigPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

ChannelConfigPanel::ChannelConfigPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("通道参数配置"), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 0.5);
    title->setFont(tf);
    outer->addWidget(title);

    // ---- 患者类型选择行（第二梯队：患者类型预设）----
    auto *patientRow = new QHBoxLayout();
    patientRow->setSpacing(6);
    patientRow->addWidget(new QLabel(QStringLiteral("患者类型:"), this));

    m_patientCombo = new QComboBox(this);
    m_patientCombo->addItem(QStringLiteral("成人"), static_cast<int>(PatientType::Adult));
    m_patientCombo->addItem(QStringLiteral("小儿"), static_cast<int>(PatientType::Pediatric));
    m_patientCombo->addItem(QStringLiteral("新生儿"), static_cast<int>(PatientType::Neonate));
    m_patientCombo->addItem(QStringLiteral("自定义"), static_cast<int>(PatientType::Custom));
    m_patientCombo->setToolTip(QStringLiteral(
        "切换患者类型将按预设剖面重载各通道量程与报警限\n"
        "（避免用成人报警限监护新生儿导致大量误报）"));
    patientRow->addWidget(m_patientCombo);
    patientRow->addStretch(1);
    outer->addLayout(patientRow);

    connect(m_patientCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const auto type = static_cast<PatientType>(
                    m_patientCombo->itemData(idx).toInt());
                emit patientTypeChangeRequested(type);
            });

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("显示"),
        QStringLiteral("通道"),
        QStringLiteral("颜色"),
        QStringLiteral("量程下限"),
        QStringLiteral("量程上限"),
        QStringLiteral("告警下限"),
        QStringLiteral("告警上限")
    });

    // 列宽策略：显示/颜色两列固定窄宽，量程/告警四列按内容自适应，
    // 让告警下限、告警上限各自独占一整列，避免两个 spinbox 挤在同一列里溢出。
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::Fixed);   // 显示
    hh->setSectionResizeMode(1, QHeaderView::Interactive); // 通道
    hh->setSectionResizeMode(2, QHeaderView::Fixed);   // 颜色
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents); // 量程下限
    hh->setSectionResizeMode(4, QHeaderView::ResizeToContents); // 量程上限
    hh->setSectionResizeMode(5, QHeaderView::Interactive); // 告警下限
    hh->setSectionResizeMode(6, QHeaderView::Interactive); // 告警上限
    // 宽度必须容纳"两字表头 + 单元格内边距(左右各约 4px)"，
    // 否则窄列下文字会被省略号截断。
    m_table->setColumnWidth(0, 56);
    m_table->setColumnWidth(2, 60);
    // 量程 / 告警四列宽度保持一致，避免"上限比下限宽"。
    for (int col = 3; col <= 6; ++col)
        m_table->setColumnWidth(col, 72);

    hh->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outer->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *applyBtn = new QPushButton(QStringLiteral("应用配置"), this);
    btnRow->addStretch(1);
    btnRow->addWidget(applyBtn);
    outer->addLayout(btnRow);

    connect(applyBtn, &QPushButton::clicked, this, &ChannelConfigPanel::onApplyClicked);
}

PatientType ChannelConfigPanel::patientType() const
{
    if (!m_patientCombo)
        return PatientType::Adult;
    return static_cast<PatientType>(m_patientCombo->currentData().toInt());
}

void ChannelConfigPanel::setChannels(const QVector<ChannelConfig> &channels)
{
    m_channels = channels;
    m_visible.clear();
    for (const ChannelConfig &c : channels)
        m_visible.append(c.visible);

    // 回灌配置时同步下拉框状态（阻塞信号避免触发二次重载）。
    if (m_patientCombo) {
        const int idx = m_patientCombo->findData(
            static_cast<int>(ChannelConfigManager::globalPatientType()));
        if (idx >= 0) {
            QSignalBlocker blocker(m_patientCombo);
            m_patientCombo->setCurrentIndex(idx);
        }
    }

    rebuildTable();
}

void ChannelConfigPanel::rebuildTable()
{
    m_table->setRowCount(m_channels.size());

    for (int row = 0; row < m_channels.size(); ++row) {
        const ChannelConfig &c = m_channels.at(row);

        // 列 0：可见性复选框
        auto *chk = new QCheckBox(m_table);
        chk->setChecked(c.visible);
        connect(chk, &QCheckBox::toggled, this, [this, row](bool on) {
            if (row >= 0 && row < m_visible.size())
                m_visible[row] = on;
        });
        m_table->setCellWidget(row, 0, chk);

        // 列 1：通道名称
        auto *nameItem = new QTableWidgetItem(QStringLiteral("%1 (%2)").arg(c.name, c.unit));
        m_table->setItem(row, 1, nameItem);

        // 列 2：颜色按钮。不设置固定宽度，让按钮自动填满单元格宽度；
        // 仅锁定高度，避免行高被撑开。
        auto *colorBtn = new QPushButton(m_table);
        colorBtn->setFixedHeight(22);
        colorBtn->setToolTip(QStringLiteral("点击选择曲线颜色"));
        colorBtn->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;")
                                    .arg(c.color.name()));
        colorBtn->setProperty("row", row);
        connect(colorBtn, &QPushButton::clicked, this, &ChannelConfigPanel::onColorButtonClicked);
        m_table->setCellWidget(row, 2, colorBtn);

        // 列 3：量程下限
        auto *minSpin = new QDoubleSpinBox(m_table);
        minSpin->setRange(-1000.0, 10000.0);
        minSpin->setValue(c.minValue);
        connect(minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, row](double v) {
                    if (row >= 0 && row < m_channels.size())
                        m_channels[row].minValue = v;
                });
        m_table->setCellWidget(row, 3, minSpin);

        // 列 4：量程上限
        auto *maxSpin = new QDoubleSpinBox(m_table);
        maxSpin->setRange(-1000.0, 10000.0);
        maxSpin->setValue(c.maxValue);
        connect(maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, row](double v) {
                    if (row >= 0 && row < m_channels.size())
                        m_channels[row].maxValue = v;
                });
        m_table->setCellWidget(row, 4, maxSpin);

        // 列 5：告警下限（独立整列，占满列宽）
        auto *lowSpin = new QDoubleSpinBox(m_table);
        lowSpin->setRange(-1000.0, 10000.0);
        lowSpin->setDecimals(1);
        lowSpin->setValue(c.alarmLow);
        lowSpin->setToolTip(QStringLiteral("告警下限"));
        lowSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        connect(lowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, row](double v) {
                    if (row >= 0 && row < m_channels.size())
                        m_channels[row].alarmLow = v;
                });
        m_table->setCellWidget(row, 5, lowSpin);

        // 列 6：告警上限（独立整列，占满列宽）
        auto *highSpin = new QDoubleSpinBox(m_table);
        highSpin->setRange(-1000.0, 10000.0);
        highSpin->setDecimals(1);
        highSpin->setValue(c.alarmHigh);
        highSpin->setToolTip(QStringLiteral("告警上限"));
        highSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        connect(highSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, row](double v) {
                    if (row >= 0 && row < m_channels.size())
                        m_channels[row].alarmHigh = v;
                });
        m_table->setCellWidget(row, 6, highSpin);
    }

    m_table->resizeRowsToContents();
}


void ChannelConfigPanel::refreshRow(int row)
{
    Q_UNUSED(row);
}

void ChannelConfigPanel::onRowChanged(int row, int column)
{
    Q_UNUSED(row);
    Q_UNUSED(column);
}

void ChannelConfigPanel::onColorButtonClicked()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    const int row = btn->property("row").toInt();
    if (row < 0 || row >= m_channels.size())
        return;

    const QColor color = QColorDialog::getColor(m_channels.at(row).color, this,
                                                QStringLiteral("选择曲线颜色"));
    if (!color.isValid())
        return;

    m_channels[row].color = color;
    btn->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(color.name()));
}

void ChannelConfigPanel::onApplyClicked()
{
    // 把可见性写回配置。
    for (int i = 0; i < m_channels.size() && i < m_visible.size(); ++i)
        m_channels[i].visible = m_visible.at(i);

    emit configApplied(m_channels);
}
