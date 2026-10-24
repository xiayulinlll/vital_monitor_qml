#ifndef VITALSIGNSIMULATOR_H
#define VITALSIGNSIMULATOR_H

#include <QObject>
#include <QVector>
#include <QAtomicInt>
#include <QElapsedTimer>

#include "data/SampleData.h"
#include "core/ChannelConfig.h"

class QTimer;

/**
 * @brief 生理信号模拟器（对应架构 2.1 数据模拟采集层）。
 *
 * 运行在**后台子线程**中，周期性生成多通道模拟体征数据，
 * 通过信号 sendFrame() 把采样帧投递到主线程业务层。
 *
 * 线程模型（对应难点 3）：
 *  - 本对象 moveToThread() 到工作线程；
 *  - 定时器在该线程内触发，只负责“生产数据”，绝不触碰任何 UI 对象；
 *  - 与主线程之间仅通过 QueuedConnection 信号槽通信，天然线程安全。
 */
class VitalSignSimulator : public QObject
{
    Q_OBJECT

public:
    explicit VitalSignSimulator(QObject *parent = nullptr);
    ~VitalSignSimulator() override;

public slots:
    /// 启动采集（应在工作线程内被调用）。
    void start(int intervalMs = 100);
    /// 停止采集。
    void stop();
    /// 更新通道配置（阈值/量程变化后同步，用于模拟越限）。
    void updateConfigs(const QVector<ChannelConfig> &configs);
    /// 重置采样序号发生器。
    void reset();

signals:
    /// 产生一帧多通道采样数据。
    void frameReady(const SampleFrame &frame);
    /// 采集线程内部状态文本（供状态栏显示）。
    void statusText(const QString &text);

private slots:
    void onTick();

private:
    double nextValueFor(const ChannelConfig &cfg);

    QTimer        *m_timer = nullptr;
    QVector<ChannelConfig> m_configs;
    quint64        m_sampleIndex = 0;
    int            m_intervalMs  = 100;      ///< 当前采样周期（用于估算丢帧）

    // 每个通道使用独立的随机游走状态，保证曲线连续、形态自然。
    QVector<double> m_current;
    QVector<double> m_phase;

    /// 每通道信号质量（0~1）与"低质量剩余帧数"，用于模拟偶发低灌注 / 导联干扰。
    QVector<double> m_quality;
    QVector<int>    m_poorQualityFrames;

    /// 用于模拟偶发的越限事件（例如运动后心率骤升）。
    int m_spikeCountdown[4] = { -1, -1, -1, -1 };

    /// 上一帧发出时刻（毫秒），用于数据完整性校验（第二梯队第 8 项）。
    qint64 m_lastTickMs = 0;
    /// 上一帧发出的单调时钟值（毫秒），用于数据完整性校验。
    qint64 m_lastMonotonicMs = 0;
    /// 单调时钟：自 start() 起算，不受系统校时影响，供告警计时使用。
    QElapsedTimer m_clock;
    /// 模拟丢帧：计数达到该值后跳过一帧并计入 droppedFrames。
    int m_simulatedDropCountdown = 0;
};

#endif // VITALSIGNSIMULATOR_H
