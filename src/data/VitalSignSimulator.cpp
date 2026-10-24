#include "VitalSignSimulator.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <QtMath>

VitalSignSimulator::VitalSignSimulator(QObject *parent)
    : QObject(parent)
{
    // 预置一套默认配置，避免 start() 之前无配置可用。
    ChannelConfigManager mgr;
    m_configs = mgr.channels();

    m_current.resize(m_configs.size());
    m_phase.resize(m_configs.size());
    m_quality.resize(m_configs.size());
    m_poorQualityFrames.resize(m_configs.size());
    for (int i = 0; i < m_configs.size(); ++i) {
        const ChannelConfig &c = m_configs.at(i);
        // 以量程中值附近为起点。
        m_current[i] = (c.minValue + c.maxValue) * 0.5;
        m_phase[i]   = QRandomGenerator::global()->generateDouble() * 6.28318;
        m_quality[i] = 1.0;
        m_poorQualityFrames[i] = 0;
    }
}

VitalSignSimulator::~VitalSignSimulator()
{
    stop();
}

void VitalSignSimulator::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        // 定时器随对象所属线程创建，因此回调也在工作线程执行。
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &VitalSignSimulator::onTick);
    }
    m_intervalMs = qMax(1, intervalMs);
    m_lastTickMs = 0;
    m_lastMonotonicMs = 0;
    // 单调时钟从 0 起算，与告警计时的时基一致（墙钟只用于显示 / 存档）。
    m_clock.start();
    m_timer->start(m_intervalMs);
    emit statusText(QStringLiteral("采集已启动，采样周期 %1 ms").arg(m_intervalMs));
}

void VitalSignSimulator::stop()
{
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
        emit statusText(QStringLiteral("采集已停止"));
    }
}

void VitalSignSimulator::updateConfigs(const QVector<ChannelConfig> &configs)
{
    if (configs.isEmpty())
        return;
    m_configs = configs;
    if (m_current.size() != m_configs.size()) {
        m_current.resize(m_configs.size());
        m_phase.resize(m_configs.size());
        m_quality.resize(m_configs.size());
        m_poorQualityFrames.resize(m_configs.size());
        for (int i = 0; i < m_configs.size(); ++i) {
            m_current[i] = (m_configs.at(i).minValue + m_configs.at(i).maxValue) * 0.5;
            m_quality[i] = 1.0;
            m_poorQualityFrames[i] = 0;
        }
    }
}

void VitalSignSimulator::reset()
{
    m_sampleIndex = 0;
    m_lastTickMs  = 0;
    m_lastMonotonicMs = 0;
    m_clock.invalidate();
}

void VitalSignSimulator::onTick()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    SampleFrame frame;
    /// 单调时钟值：不受系统校时影响，供告警计时使用。
    const qint64 monoMs = m_clock.isValid() ? m_clock.elapsed() : 0;
    frame.monotonicMs = monoMs;

    // ---- 数据完整性（第二梯队第 8 项）----
    // 用单调时钟的实际间隔判定丢帧（不能只比较墙钟时间戳：校时会误判），
    // 若实际间隔明显大于采样周期，说明发生了抖动 / 丢帧，
    // 需要如实上报而非静默忽略。
    if (m_lastTickMs > 0 && m_lastMonotonicMs > 0) {
        const qint64 monoDelta = monoMs - m_lastMonotonicMs;
        frame.deltaMs = monoDelta;
        const qint64 expected = m_intervalMs;
        if (monoDelta > expected * 3 / 2) {
            frame.droppedFrames = static_cast<quint64>(
                qMax<qint64>(1, (monoDelta / qMax(1, expected)) - 1));
        }
    }
    m_lastTickMs = nowMs;
    m_lastMonotonicMs = monoMs;

    // ---- 模拟偶发丢帧事件，便于演示「数据不连续」技术报警 ----
    // @note 真实设备中该事件由驱动层上报；此处由模拟器主动制造。
    if (m_simulatedDropCountdown <= 0 && QRandomGenerator::global()->bounded(600) == 0)
        m_simulatedDropCountdown = 3;   // 3 帧后丢弃一帧

    if (m_simulatedDropCountdown > 0) {
        --m_simulatedDropCountdown;
        if (m_simulatedDropCountdown == 0) {
            frame.droppedFrames += 1;
            // 丢弃该帧：序号照常推进，但数据不发出。
            m_sampleIndex++;
            emit statusText(QStringLiteral("检测到 1 帧数据丢失（模拟）"));
            return;
        }
    }

    frame.sampleIndex = m_sampleIndex++;
    frame.timestampMs = nowMs;

    for (int i = 0; i < m_configs.size(); ++i) {
        ChannelSample s;
        s.channelId = m_configs.at(i).id;
        s.value     = nextValueFor(m_configs.at(i));
        s.quality   = m_quality.at(i);
        // SQI 极低时把该点标记为无效测量（等价于探头未测得有效信号）。
        s.invalid   = (m_quality.at(i) < 0.15);
        frame.channels.append(s);
    }

    emit frameReady(frame);
}

double VitalSignSimulator::nextValueFor(const ChannelConfig &cfg)
{
    const int idx = cfg.id;
    if (idx < 0 || idx >= m_current.size())
        return 0.0;

    // 使用“缓慢随机游走 + 周期分量”叠加，模拟真实体征的平滑波动。
    m_phase[idx] += 0.05;
    if (m_phase[idx] > 6.28318)
        m_phase[idx] -= 6.28318;

    const double span = qMax(1.0, cfg.maxValue - cfg.minValue);
    const double wave = qSin(m_phase[idx]) * span * 0.03;   // 周期波动
    const double noise = (QRandomGenerator::global()->generateDouble() - 0.5) * span * 0.01;

    // 让数值缓慢回归量程中心，避免长期漂移出界。
    const double center = (cfg.minValue + cfg.maxValue) * 0.5;
    m_current[idx] += (center - m_current[idx]) * 0.02;

    // 偶发越限：每隔一段时间制造一次“异常事件”，用于演示告警标记。
    if (m_spikeCountdown[idx] < 0 && QRandomGenerator::global()->bounded(300) == 0)
        m_spikeCountdown[idx] = 15; // 持续约 15 个采样周期

    double spike = 0.0;
    if (m_spikeCountdown[idx] > 0) {
        --m_spikeCountdown[idx];
        // 超过上限的冲动（如心率骤升 / 体温升高）。
        spike = span * 0.18;
        if (m_spikeCountdown[idx] == 0)
            m_spikeCountdown[idx] = -1;
    }

    // ---- 模拟信号质量劣化（第二梯队第 7 项）----
    // 真实设备中 SQI 由波形形态 / 灌注指数等计算；此处用「偶发低质量窗口」
    // 模拟指夹脱落、末梢灌注不足、电极干扰等场景，使 SQI 相关逻辑可测试。
    if (idx < m_poorQualityFrames.size()) {
        if (m_poorQualityFrames[idx] <= 0
            && QRandomGenerator::global()->bounded(500) == 0) {
            m_poorQualityFrames[idx] = 20;   // 低质量窗口长度（采样周期数）
        }

        if (m_poorQualityFrames[idx] > 0) {
            --m_poorQualityFrames[idx];
            // 在窗口内让质量在 [0.05, 0.30] 之间波动（低于 0.35 抑制阈值）。
            m_quality[idx] = 0.05 + QRandomGenerator::global()->generateDouble() * 0.25;
            // 低质量期间读数本身也不可信，叠加较大噪声。
            if (idx < m_current.size())
                m_current[idx] += (QRandomGenerator::global()->generateDouble() - 0.5) * span * 0.06;
        } else {
            // 缓慢恢复，避免质量值跳变。
            m_quality[idx] += (1.0 - m_quality[idx]) * 0.2;
            if (m_quality[idx] > 0.999)
                m_quality[idx] = 1.0;
        }
    }

    double v = m_current[idx] + wave + noise + spike;
    v = cfg.clamp(v);
    m_current[idx] = v;
    return v;
}
