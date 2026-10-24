#include "core/AlarmNotifier.h"

#include <QDir>
#include <QFile>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>

#include <cmath>

/*
 * 关于发声方式（为什么是 QSoundEffect）：
 *
 *  - Widgets 版用 `QApplication::beep()`，那是 QtWidgets 的接口；本工程是
 *    QGuiApplication（刻意不链接 QtWidgets），**不能**调用它。
 *  - Windows 原生 `Beep()` 只能产生固定方波、无法随严重度变化音高，
 *    且会阻塞调用线程（GUI 线程里阻塞 = 界面卡顿）。
 *  - `QSoundEffect` 属于 QtMultimedia，异步播放、可调音量，能在 QGuiApplication
 *    下使用。代价是 CMake 里多依赖一个模块，并需部署 Qt6Multimedia 运行时。
 *
 * 因此本类把「发声」收敛到 `beep()`：若音源初始化失败（无音频设备 / 缺插件），
 * 只把 `audioAvailable` 置 false，**不崩溃、也不假装成功** —— 界面据此
 * 显式提示「无声音输出」，避免用户误以为「设备在监护却不会报警」。
 */

namespace {

/**
 * @brief 生成一段短促的「滴滴」声并写成 wav 到临时目录。
 *
 * 参数按 IEC 60601-1-8 的工程常识取值：
 *  - 基频 800~1000 Hz：穿透力强于低频，又不至于刺耳；
 *  - 单次 120~150 ms 的短音，便于「3 连响」时分辨次数而不糊成一片。
 *
 * @param freqHz 基频
 * @param ms     时长（毫秒）
 * @param volume 幅度（0~1）
 * @return wav 文件路径；失败返回空串。
 *
 * @note 16-bit / 单声道 / 44.1 kHz，QSoundEffect 对 PCM wav 兼容性最好。
 *       这只是演示级音源，真实监护仪应使用经临床确认的报警音库。
 */
QString makeBeepWav(double freqHz, int ms, double volume)
{
    const int sampleRate = 44100;
    const int frames     = sampleRate * ms / 1000;

    QByteArray pcm;
    pcm.reserve(frames * 2);

    // 两端各 5 ms 淡入淡出：直接切断波形会发出「咔」声，
    // 在报警音里很容易被误听成设备故障。
    const int ramp = qMax(1, sampleRate * 5 / 1000);

    for (int i = 0; i < frames; ++i) {
        double env = 1.0;
        if (i < ramp)
            env = double(i) / ramp;
        else if (i > frames - ramp)
            env = double(frames - i) / ramp;

        const double t = double(i) / sampleRate;
        const double s = std::sin(2.0 * M_PI * freqHz * t) * volume * env;
        const qint16 v = static_cast<qint16>(qBound(-32767.0, s * 32767.0, 32767.0));

        pcm.append(char(v & 0xFF));
        pcm.append(char((v >> 8) & 0xFF));
    }

    // ---- 组装 44 字节标准 wav 头 ----
    QByteArray wav;
    auto put32 = [&wav](quint32 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
        wav.append(char((v >> 16) & 0xFF));
        wav.append(char((v >> 24) & 0xFF));
    };
    auto put16 = [&wav](quint16 v) {
        wav.append(char(v & 0xFF));
        wav.append(char((v >> 8) & 0xFF));
    };

    const quint32 dataSize = quint32(pcm.size());
    wav.append("RIFF", 4);
    put32(36 + dataSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    put32(16);              // fmt 块长度
    put16(1);               // PCM
    put16(1);               // 单声道
    put32(sampleRate);
    put32(sampleRate * 2);  // 字节率
    put16(2);               // 块对齐
    put16(16);              // 位深
    wav.append("data", 4);
    put32(dataSize);
    wav.append(pcm);

    const QString dir = QDir::tempPath() + QStringLiteral("/vital_monitor_qml");
    QDir().mkpath(dir);

    const QString path = dir + QStringLiteral("/beep_%1hz_%2ms.wav")
                                 .arg(int(freqHz))
                                 .arg(ms);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(wav);
    f.close();
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// 节奏参数（与 qml/Theme.qml 的 soundIntervalMs() 一一对应）
// ---------------------------------------------------------------------------

int AlarmNotifier::soundIntervalMs(AlarmEvent::Level level)
{
    // 严重度越高，重复越急促。改动此处必须同时改 Theme.qml，
    // 否则会出现「横幅闪得快、声音却慢」的撕裂感。
    switch (level) {
    case AlarmEvent::Escalated:
    case AlarmEvent::Critical: return 900;
    case AlarmEvent::Warning:  return 1800;
    case AlarmEvent::Info:     return 3000;
    }
    return 3000;
}

int AlarmNotifier::beepsFor(AlarmEvent::Level level)
{
    // 「3 连响」是听觉上的严重度编码：无需看屏幕即可分辨危急与否。
    return (level >= AlarmEvent::Critical) ? 3 : 1;
}

AlarmNotifier::AlarmNotifier(QObject *parent)
    : QObject(parent)
{
    m_soundTimer = new QTimer(this);
    m_soundTimer->setTimerType(Qt::PreciseTimer);
    connect(m_soundTimer, &QTimer::timeout, this, &AlarmNotifier::onSoundTick);

    m_pauseTimer = new QTimer(this);
    m_pauseTimer->setInterval(1000);   // 秒级倒计时
    connect(m_pauseTimer, &QTimer::timeout, this, &AlarmNotifier::onPauseTick);

    // ---- 音源：危急 1000 Hz（更尖锐）/ 常规 800 Hz ----
    m_sound = new QSoundEffect(this);
    m_sound->setVolume(0.55);

    const QString normalWav = makeBeepWav(800.0, 150, 0.70);
    if (!normalWav.isEmpty()) {
        m_sound->setSource(QUrl::fromLocalFile(normalWav));
        m_audioAvailable = true;
    }

    // QSoundEffect 的加载是异步的：status 变化后才能确认声卡 / 插件是否可用。
    // 缺少 multimedia 插件时 status 会变成 Error，界面据此提示「无声音输出」。
    connect(m_sound, &QSoundEffect::statusChanged, this, [this]() {
        const bool ok = (m_sound->status() == QSoundEffect::Ready
                         || m_sound->status() == QSoundEffect::Loading);
        if (ok != m_audioAvailable) {
            m_audioAvailable = ok;
            emit audioAvailableChanged(ok);
        }
    });
}

AlarmNotifier::~AlarmNotifier() = default;

void AlarmNotifier::setAudioEnabled(bool enabled)
{
    m_audioEnabled = enabled;

    // 关闭音频后仍继续走定时器与 beepCount 计数（只是不送到声卡），
    // 这样策略逻辑与真实输出解耦，可在无声卡环境下测试。
    if (!enabled && m_sound)
        m_sound->stop();
}

void AlarmNotifier::updateTopLevel(AlarmEvent::Level level, bool hasPending)
{
    m_level      = level;
    m_hasPending = hasPending;

    // 暂停 / 静音期间由 syncSoundTimer() 直接停表，不做任何声音活动。
    syncSoundTimer();
}

void AlarmNotifier::setMuted(bool muted)
{
    if (m_muted == muted)
        return;

    m_muted = muted;
    emit mutedChanged(muted);

    // 静音只影响声音；横幅闪烁不受影响（视觉提示必须保留）。
    if (muted)
        m_soundTimer->stop();
    else
        syncSoundTimer();
}

int AlarmNotifier::pauseMaxSeconds() const
{
    return (m_level >= AlarmEvent::Critical) ? kCriticalPauseMaxSeconds
                                             : kNormalPauseMaxSeconds;
}

int AlarmNotifier::startPause(int seconds)
{
    // 没有待提示告警时暂停无意义：返回 0 让调用方提示「当前无报警」，
    // 而不是进入一个「看起来按下了、实际没生效」的假状态。
    if (!m_hasPending)
        return 0;

    // 危急值不允许长时间静默：强制收紧上限（IEC 60601-1-8）。
    const int limit = pauseMaxSeconds();
    const int secs  = qBound(1, qMin(seconds, limit), limit);

    m_paused           = true;
    m_pauseRemaining   = secs;
    m_levelBeforePause = m_level;

    // 暂停期间声音停止；闪烁由 QML 依据 paused 属性停止。
    m_soundTimer->stop();
    m_pauseTimer->start();

    emit pausedChanged(true);
    emit pauseStateChanged(true, secs);
    emit pauseChanged(secs);

    return secs;
}

void AlarmNotifier::cancelPause()
{
    if (!m_paused)
        return;

    m_paused         = false;
    m_pauseRemaining = 0;
    m_pauseTimer->stop();

    emit pausedChanged(false);
    emit pauseStateChanged(false, 0);
    emit pauseChanged(0);

    // 恢复后立即按当前告警重新提示，不等下一个 repeatMs 周期。
    syncSoundTimer();
}

void AlarmNotifier::onPauseTick()
{
    if (!m_paused) {
        m_pauseTimer->stop();
        return;
    }

    if (m_pauseRemaining > 0)
        --m_pauseRemaining;

    emit pauseChanged(m_pauseRemaining);

    if (m_pauseRemaining > 0)
        return;

    // ---- 到期自动恢复：这是「不得无限期静默」的落地方式 ----
    m_pauseTimer->stop();
    m_paused = false;

    emit pausedChanged(false);
    emit pauseStateChanged(false, 0);
    emit pauseChanged(0);

    // 若仍有未处理告警，syncSoundTimer() 会立即重新起声；
    // 告警条件已消失时它什么都不做 —— 恢复不应凭空造出声音。
    syncSoundTimer();
}

bool AlarmNotifier::shouldBeep() const
{
    if (!m_hasPending)
        return false;
    if (m_paused)
        return false;
    if (m_muted)
        return false;
    // @note 这里**不**检查 m_audioEnabled：它只决定「要不要真的送到声卡」，
    //       不决定「策略上该不该响」。若在此处一并检查，测试里关闭音频后
    //       连 beepCount 都不再累加，静音 / 暂停策略将无法验证。
    return true;
}

void AlarmNotifier::syncSoundTimer()
{
    if (!shouldBeep()) {
        m_soundTimer->stop();
        return;
    }

    if (!m_soundTimer->isActive()) {
        // 首声立即响：「发现延迟」比「节奏整齐」更重要。
        onSoundTick();
        m_soundTimer->start(soundIntervalMs(m_level));
    } else if (m_soundTimer->interval() != soundIntervalMs(m_level)
               || m_lastBeepLevel != m_level) {
        // 严重度变化时立刻按新节奏响一次，而不是等旧周期走完 ——
        // 这正好是「升级」场景：患者恶化后 900 ms 内必须更急促地提示。
        onSoundTick();
        m_soundTimer->start(soundIntervalMs(m_level));
    }
}

void AlarmNotifier::onSoundTick()
{
    if (!shouldBeep())
        return;

    // 记录本次发声时的等级：用于区分「同一等级的下一个周期」
    // 与「等级刚变化」两种情形（后者必须立即补声，见 syncSoundTimer）。
    m_lastBeepLevel = m_level;

    // 危急用高频音源，其余用常规音源。
    if (m_sound && m_audioAvailable) {
        const QString wav = (m_level >= AlarmEvent::Critical)
            ? makeBeepWav(1000.0, 120, 0.85)
            : makeBeepWav(800.0, 150, 0.70);
        if (!wav.isEmpty())
            m_sound->setSource(QUrl::fromLocalFile(wav));
    }

    for (int i = 0; i < beepsFor(m_level); ++i)
        beep();
}

void AlarmNotifier::beep()
{
    // 计数的是「策略上要求发声的次数」，而非「声卡实际响了几次」：
    // 这样即使在没有音频设备的 CI 上也能验证节奏逻辑。
    ++m_beepCount;
    emit beepCountChanged(m_beepCount);

    if (!m_audioEnabled || !m_audioAvailable || !m_sound)
        return;

    // play() 会打断上一声，正是「连响」需要的行为。
    m_sound->play();
}
