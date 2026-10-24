#ifndef CHANNELMODEL_H
#define CHANNELMODEL_H

#include <QAbstractListModel>
#include <QColor>
#include <QVector>

#include "data/SampleData.h"
#include "core/ChannelConfig.h"

/**
 * @brief 单通道的 QML 数据模型（对应 C++ 版 VitalValuePanel 的数值卡片）。
 *
 * 采用 `QAbstractListModel` 而不是 `QVariantMap`：
 *  - 数值每 100 ms 更新一次，`dataChanged` 只刷新**变化的那一行**，
 *    避免整块面板的 QML 绑定全部重新求值；
 *  - 通道名称 / 单位 / 量程 / 阈值随配置变化，用 `resetChannels()` 整体重建。
 *
 * 与 Widgets 版一致的三重状态区分（`VitalValuePanel::updateValues`）：
 *  1. `valid == false`（导联脱落 / 测量失败）→ 显示 `--`
 *  2. `quality < 0.35`（信号差但可读）      → 数值 + SQI 警示色
 *  3. 数值越限                              → 数值红色加粗
 */
class ChannelModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// 与 QML 侧约定的角色（见 qml/ChannelValueCard.qml）。
    /// @warning 角色名必须与 ChannelValueCard 的 required property 逐字相同；
    ///          两者靠名字配对，改名要同步改，否则委托创建失败、数值全为 --。
    enum Roles {
        ChannelIdRole = Qt::UserRole + 1,
        NameRole,
        UnitRole,
        ColorRole,
        MinValueRole,
        MaxValueRole,
        AlarmLowRole,
        AlarmHighRole,
        HasLowAlarmRole,
        HasHighAlarmRole,
        VisibleRole,
        ValueRole,
        QualityRole,
        ValidRole,
        HasValueRole,     ///< 是否已收到过至少一帧（未收到时显示占位符）
        AlarmedRole,      ///< 当前数值是否越限（用于红色高亮）
        AlarmBorderRole,  ///< 告警边框色（无告警时为空字符串）
        /// 原始值是否超出量程（优化文档 3.1 第 5 项：不裁剪数值，改用角标提示）。
        OutOfRangeRole,
        /// 该样本是否被判定为伪迹（生理上不可能的跳变）。
        ArtifactRole
    };
    Q_ENUM(Roles)

    explicit ChannelModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 用新配置重建全部行（通道数 / 名称 / 阈值 / 颜色变化时调用）。
    void resetChannels(const QVector<ChannelConfig> &channels);

    /// 写入一帧数值；只对发生变化的行发 dataChanged。
    void applyFrame(const SampleFrame &frame);

    /// 依据活动告警给对应行打上告警边框色（等价 VitalValuePanel::applyAlarmState）。
    void applyAlarmState(const QVector<AlarmEvent> &activeAlarms);

    /// 当前行的配置快照（供配置面板初始化编辑区）。
    QVector<ChannelConfig> configs() const { return m_configs; }

private:
    struct Row {
        ChannelConfig cfg;
        double  value    = 0.0;
        double  quality  = 1.0;
        bool    valid    = false;   ///< 当前帧是否可信（非 invalid 且 SQI 达标）
        bool    alarmed  = false;
        bool    hasValue = false;
        /// 原始值超出量程（不在 [minValue, maxValue] 内）。
        bool    outOfRange = false;
        /// 本帧样本被判定为伪迹。
        bool    artifact = false;
        /// 告警边框色；`isValid() == false` 表示无告警。
        QColor  alarmBorder;
    };

    QVector<Row> m_rows;
    QVector<ChannelConfig> m_configs;
};

#endif // CHANNELMODEL_H
