#ifndef ALARMDETECTOR_H
#define ALARMDETECTOR_H

#include <QVector>
#include <QHash>
#include <QString>

#include "data/SampleData.h"
#include "core/ChannelConfig.h"

/**
 * @brief 告警判断模块（对应架构 2.2 第 4 点，并按优化文档第一梯队第 2 项重写）。
 *
 * 相较初版的关键修复（原缺陷：`highLatched` 锁存导致「患者持续超阈值只报警一次」）：
 *
 *  1. **持续报警**：越限条件持续成立时，按 `alarmRepeatMs` 周期重复上报，
 *     绝不因为「报过一次」就永久静默，避免漏掉持续恶化的患者。
 *  2. **毫秒制延时**：去抖改用 `alarmDelayMs`（毫秒）而非帧数，
 *     采样率变化时行为一致（第一梯队第 4 项）。
 *  3. **报警状态机**：Active → Acknowledged / Cleared / Escalated / LatchedSilent
 *     （第二梯队第 5 项）。
 *  4. **信号质量门控**：SQI 过低时抑制生理报警并改发技术报警，
 *     避免基于不可靠数据误报（第二梯队第 7 项）。
 *  5. **技术报警通道**：数据丢帧 / 信号不良 / 数值无效独立成类（第二梯队第 9 项）。
 *  6. **优先级语义**：报警等级来自通道显式配置的 `AlarmPriority` 与二级阈值，
 *     不再按「超出阈值半个量程」折算（优化文档 2.1 第 1 项）。
 */
class AlarmDetector
{
public:
    AlarmDetector();

    /**
     * @brief 设置越限去抖判据（兼容旧接口的帧数版本）。
     *
     * @note 该接口保留是为了向后兼容；采样间隔已知时请优先使用
     *       setDefaultSampleIntervalMs() 走毫秒制，避免采样率变化导致
     *       实际延时漂移。此处假定采样间隔为 100 ms。
     */
    void setDebounceFrames(int frames);

    /// 设置默认采样间隔（毫秒），用于在通道未显式配置时换算延时。
    void setDefaultSampleIntervalMs(int ms);

    /**
     * @brief 检测一帧数据。
     * @param frame   采样帧
     * @param configs 当前通道配置
     * @return 本帧产生的告警事件列表（可能为空）
     *
     * 返回的事件既可能是「新产生的告警」，也可能是「重复提示 / 升级 / 解除」，
     * 通过 `AlarmEvent::state` 区分。
     */
    QVector<AlarmEvent> detect(const SampleFrame &frame,
                               const QVector<ChannelConfig> &configs);

    /// 确认（签收）某条活动告警；返回被确认的事件（供审计）。
    /// @param conditionKey 由 AlarmEvent::conditionKey() 生成；
    ///                     传空字符串表示确认全部活动告警。
    /// @param nowMs        当前时间基准（毫秒），必须与 detect() 传入的
    ///                     SampleFrame::timestampMs 使用同一时基，
    ///                     否则后续「重复提示 / 升级」的计时会因时基错乱而失效。
    ///                     传 0 表示使用系统当前时间（仅在无采集数据时使用）。
    QVector<AlarmEvent> acknowledge(const QString &conditionKey = QString(),
                                    qint64 nowMs = 0);

    /// 当前活动（未解除）告警数量。
    int activeCount() const;

    /// 当前处于「未确认且未静音」状态的最高严重度（供报警条 / 声音使用）。
    /// 无告警时返回 AlarmEvent::Info。
    AlarmEvent::Level highestPendingLevel() const;

    /// 是否存在需要引起注意的未确认告警。
    bool hasPendingAlarm() const;

    /// 遍历当前活动告警（含状态），用于界面刷新。
    QVector<AlarmEvent> activeAlarms() const;

    /// 清空内部状态（停止采集 / 清空时调用）。
    void reset();

private:
    /// 单条告警条件的跟踪状态。
    struct Track {
        bool    active      = false;  ///< 条件当前是否成立（已越过延时）
        bool    acknowledged= false;  ///< 是否已被人工确认
        bool    escalated   = false;  ///< 是否已升级
        bool    announced   = false;  ///< 是否已经首次上报过
        qint64  onsetMs     = 0;      ///< 条件首次成立的时间（毫秒）
        qint64  lastReportMs= 0;      ///< 上一次上报/重复提示时间
        /// 上一次「升级判定」参考时刻（毫秒）。
        ///
        /// @note 必须与 lastReportMs 分离：若复用同一次重复提示的计时，
        ///       重复周期（如 2 s）短于升级时限（如 3 s）时，计时器会被
        ///       重复提示不断刷新，导致升级**永远无法触发**。
        qint64  escalateAnchorMs = 0;
        double  lastValue   = 0.0;    ///< 最近一次数值
        double  limit       = 0.0;    ///< 阈值
        AlarmEvent::Level  level = AlarmEvent::Warning;
        AlarmEvent::Category category = AlarmEvent::Physiological;
        int     channelId   = -1;
        bool    highLimit   = true;
        QString text;                 ///< 首次上报时的描述文本
    };

    /// 取（或创建）指定条件的跟踪记录。
    Track &trackFor(const QString &key);

    /// 构造一条面向外部的 AlarmEvent。
    AlarmEvent makeEvent(const Track &t, const SampleFrame &frame,
                         AlarmEvent::State state) const;

    /// 处理一个「上限越限 / 下限越限 / 技术事件」条件在某一帧的状态迁移。
    /// @return 若本帧需要向外发出事件，则返回 true。
    bool updateCondition(const QString &key,
                         bool conditionMet,
                         bool highLimit,
                         int channelId,
                         double value,
                         double limit,
                         AlarmEvent::Category category,
                         const QString &textIfNew,
                         AlarmEvent::Level levelIfNew,
                         qint64 nowMs,
                         qint64 delayMs,
                         qint64 repeatMs,
                         qint64 escalateMs,
                         const SampleFrame &frame,
                         QVector<AlarmEvent> &out);

    int m_defaultSampleIntervalMs = 100;

    /// 累计丢帧计数（相对增量需自行累加，见 detect()）。
    quint64 m_accumulatedDrops = 0;

    /// conditionKey -> 跟踪状态
    QHash<QString, Track> m_tracks;

    /// 已上报过的技术报警种类（避免每帧重复刷屏），key 为 conditionKey。
    QHash<QString, qint64> m_lastTechReportMs;
};

#endif // ALARMDETECTOR_H
