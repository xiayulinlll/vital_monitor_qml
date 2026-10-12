#ifndef CHANNELCONFIGPANEL_H
#define CHANNELCONFIGPANEL_H

#include <QWidget>
#include <QVector>

#include "core/ChannelConfig.h"

class QTableWidget;
class QCheckBox;
class QComboBox;

/**
 * @brief 参数配置面板（对应架构 2.4 第 2 点）。
 *
 * 以表格形式展示每路通道的：可见性、颜色、量程下限/上限、告警下限/上限。
 * 用户修改后点击「应用」触发配置下发信号。
 *
 * 第二梯队增强：
 *  - 顶部提供「患者类型」下拉框，切换后按预设剖面填充报警限；
 *  - 应用配置时同步发出审计所需的新旧值差异（由 MainWindow 落盘）。
 */
class ChannelConfigPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelConfigPanel(QWidget *parent = nullptr);

    /// 载入配置到表格。
    void setChannels(const QVector<ChannelConfig> &channels);

    /// 读取表格中当前编辑的配置。
    QVector<ChannelConfig> channels() const { return m_channels; }

    /// 当前选中的患者类型。
    PatientType patientType() const;

signals:
    /// 用户点击应用后发出更新后的配置。
    void configApplied(const QVector<ChannelConfig> &channels);

    /**
     * @brief 用户切换患者类型（第二梯队：患者类型预设）。
     *
     * MainWindow 收到后调用 ChannelConfigManager::loadProfile()，
     * 再把结果回灌到面板（setChannels）。
     */
    void patientTypeChangeRequested(PatientType type);

private slots:
    void onApplyClicked();
    void onRowChanged(int row, int column);
    void onColorButtonClicked();

private:
    void rebuildTable();
    void refreshRow(int row);

    QTableWidget *m_table = nullptr;
    QComboBox    *m_patientCombo = nullptr;
    QVector<ChannelConfig> m_channels;
    QVector<bool> m_visible;   ///< 每行的可见性勾选状态
};

#endif // CHANNELCONFIGPANEL_H
