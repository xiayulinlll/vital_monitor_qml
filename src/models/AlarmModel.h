#ifndef ALARMMODEL_H
#define ALARMMODEL_H

#include <QAbstractListModel>
#include <QVector>

#include "data/SampleData.h"

/**
 * @brief 活动告警列表模型（QML 相对 Widgets 的**主要增益点之一**）。
 *
 * Widgets 版把告警只做成一条横幅（`AlarmBanner`），无法同时查看多条活动告警；
 * 而 IEC 60601-1-8 要求操作者能**同时看到全部活动报警及其优先级**。
 * 在 QML 中一份 `ListView` + 委托即可实现：
 *  - 按严重度降序排列（Escalated > Critical > Warning > Info）；
 *  - 同严重度按发生时间倒序（最新在上）；
 *  - 高优先级带脉冲光晕（`SequentialAnimation`），低优先级静态显示。
 *
 * 交付给 QML 的 `model` 直接就是本对象（见 `qml/AlarmListPanel.qml`）。
 */
class AlarmModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ChannelIdRole = Qt::UserRole + 1,
        LevelRole,
        LevelColorRole,
        LevelNameRole,
        CategoryRole,
        StateRole,
        StateNameRole,
        HighLimitRole,
        ValueRole,
        LimitRole,
        TextRole,
        TimeTextRole,
        SampleIndexRole,
        PulsingRole,     ///< 是否需要脉冲动画（Escalated / Critical）
        IsTechnicalRole, ///< 是否技术报警（用于文案前缀区分）
        TrackKeyRole,    ///< 跟踪键（与 AlarmDetector::activeDurationsMs() 同一键空间）
        DurationMsRole,  ///< 已持续毫秒数（-1 表示未知）
        DurationTextRole ///< 已持续时长文本（「持续 12.3 s」/「—」）
    };
    Q_ENUM(Roles)

    explicit AlarmModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 用新的活动告警集合整体替换（数量变化时 beginResetModel）。
    /// @param durations 各跟踪键的持续毫秒数，取自
    ///        `AlarmDetector::activeDurationsMs()`；键为 `AlarmEvent::trackKey`
    ///        （**不能**用 `conditionKey()`：技术报警不在那个编码空间里）。
    void setAlarms(const QVector<AlarmEvent> &alarms,
                   const QHash<QString, qint64> &durations = QHash<QString, qint64>());

    /// 当前活动告警（已排序），空列表表示无报警。
    QVector<AlarmEvent> alarms() const { return m_alarms; }

    /// 是否存在需要提示的告警（供 QML 决定是否显示横幅 / 播放声音）。
    Q_PROPERTY(bool empty READ isEmpty NOTIFY alarmsChanged)
    bool isEmpty() const { return m_alarms.isEmpty(); }

    /// 最高严重度等级（无告警时返回 0 = Info）。
    Q_PROPERTY(int topLevel READ topLevel NOTIFY alarmsChanged)
    int topLevel() const;

signals:
    void alarmsChanged();

private:
    /**
     * @brief 排序键：严重度降序 → 未确认优先 → 已持续时长降序。
     *
     * 「同严重度按发生时间倒序」改为「按持续时长降序」的等价表述，
     * 但后者**显式使用与告警同一键空间查出的时长**，因此对技术报警
     * 同样正确（技术报警的 onsetMs 与生理报警一样被跟踪）。
     */
    bool lessThan(const AlarmEvent &a, const AlarmEvent &b) const;

    QVector<AlarmEvent> m_alarms;

    /// 跟踪键 → 已持续毫秒数（`AlarmDetector::activeDurationsMs()` 的快照）。
    QHash<QString, qint64> m_durations;
};

#endif // ALARMMODEL_H
