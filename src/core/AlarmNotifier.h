#ifndef ALARMNOTIFIER_H
#define ALARMNOTIFIER_H

#include <QHash>
#include <QObject>
#include <QString>

#include "data/SampleData.h"

class QTimer;
class QSoundEffect;

/**
 * @brief 报警声光提示的「听觉 + 抑制状态」控制器（对应优化文档第二梯队第 6、7 项）。
 *
 * ## 为什么放在 C++ 而不是 QML
 *
 * Widgets 版把提示音 / 静音 / 暂停逻辑写在 `AlarmBanner` 里 —— 因为那里的横幅
 * 就是 QWidget。QML 版若在 `AlarmBanner.qml` 里用 `Timer` 实现，会与 C++
 * 的 `AlarmDetector`（`alarmRepeatMs` / `alarmEscalateMs`）形成**两套计时真源**：
 * 声音节奏由 QML 决定、重报节奏由 C++ 决定，二者必然漂移，
 * 最终表现为「横幅在闪、声音却没跟上」。
 *
 * 因此本类承担三件事，且**只有本类**负责：
 *  1. **分级提示音**：危急 3 连响 / 900 ms，一般 1 响 / 1800 ms，提示 1 响 / 3000 ms
 *     （与 `AlarmBanner` 的闪烁节奏同源，见 `docs/可用性工程文件.md` §6）；
 *  2. **静音（Mute）**：只关声音，视觉提示照常 —— 适用于「暂时不想听」；
 *  3. **暂停（Pause）**：声音与闪烁**全部**停止，但必须在明确时长后自动恢复，
 *     且暂停期间横幅仍显示剩余秒数与「已抑制」外观。
 *
 * ## IEC 60601-1-8 的三条硬约束（实现即按此校验）
 *
 *  1. 暂停必须有**明确时长**，不得无限期静默；
 *  2. 到期**自动恢复**，恢复时若仍有未处理告警立即重新提示；
 *  3. 暂停状态必须**可见**（`pauseRemainingSeconds` 推给 QML 显示倒计时）。
 *
 * 危急报警（`Critical` / `Escalated`）的暂停上限被强制收紧到
 * `kCriticalPauseMaxSeconds`（30 s），否则操作者可以用「暂停」把一条
 * 危及生命的报警长期按住。
 *
 * ## 与服务端的边界
 *
 * 本类**不判断报警是否成立**（那是 `AlarmDetector` 的职责），只接收
 * 「当前最高待提示等级」，据此决定是否发声。这样两者可以独立测试：
 * `alarm_state_test` 覆盖状态机，本类的策略由 `alarm_pause_test` 覆盖。
 */
class AlarmNotifier : public QObject
{
    Q_OBJECT

    /// 是否静音（只影响声音，不影响视觉）。
    Q_PROPERTY(bool muted READ isMuted NOTIFY mutedChanged)
    /// 是否处于暂停中（声音 + 闪烁均停止）。
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    /// 暂停剩余秒数（未暂停时为 0）；QML 用它渲染倒计时。
    Q_PROPERTY(int pauseRemainingSeconds READ pauseRemainingSeconds
               NOTIFY pauseChanged)
    /// 当前暂停允许的最长秒数（随最高告警等级变化，供 QML 显示提示）。
    Q_PROPERTY(int pauseMaxSeconds READ pauseMaxSeconds NOTIFY pauseChanged)
    /// 声卡 / 音频后端是否可用；不可用时界面需明确告知「无声」，
    /// 不能让用户误以为「设备没报警」。
    Q_PROPERTY(bool audioAvailable READ isAudioAvailable NOTIFY audioAvailableChanged)
    /// 是否至少响过一次（用于验证声音链路确实被触发过）。
    Q_PROPERTY(int beepCount READ beepCount NOTIFY beepCountChanged)

public:
    explicit AlarmNotifier(QObject *parent = nullptr);
    ~AlarmNotifier() override;

    /// 危急值允许的最长暂停秒数（规范要求危急报警不可长时间静默）。
    static constexpr int kCriticalPauseMaxSeconds = 30;
    /// 非危急告警允许的最长暂停秒数。
    static constexpr int kNormalPauseMaxSeconds   = 120;

    /**
     * @brief 由宿主每帧（或每次状态变化）调用，告知当前最高**待提示**等级。
     *
     * @param level `AlarmDetector::highestPendingLevel()` 的返回值：
     *              只统计「活动且未确认」的告警。已确认的告警不发声，
     *              直到 `AlarmDetector` 按 `alarmRepeatMs` 重新上报
     *              （此时 `highestPendingLevel()` 会再次升高）。
     *
     * @note 传 `AlarmEvent::Info` 表示「无待提示告警」，会停止发声。
     *       但 `Info` 同时也是合法的低优先级等级 —— 二者无法区分，
     *       因此约定：**低优先级告警也用 `Info` 表示**，由
     *       `AlarmDetector::hasPendingAlarm()` 另行告知「是否有告警」。
     */
    void updateTopLevel(AlarmEvent::Level level, bool hasPending);

    /// 切换静音。静音不改变暂停状态，也不影响视觉提示。
    Q_INVOKABLE void setMuted(bool muted);

    /**
     * @brief 开始暂停（声音 + 闪烁）。
     *
     * @param seconds 请求的时长；会被强制收紧到
     *                `kCriticalPauseMaxSeconds` / `kNormalPauseMaxSeconds`。
     * @return 实际生效的秒数；0 表示未生效（当前没有待提示告警）。
     *
     * @note 没有告警时暂停毫无意义，且会掩盖「暂停按钮点了没反应」的困惑，
     *       因此直接返回 0，由调用方给出提示。
     */
    Q_INVOKABLE int startPause(int seconds = kNormalPauseMaxSeconds);

    /// 提前结束暂停并立即恢复提示。
    Q_INVOKABLE void cancelPause();

    bool isMuted()   const { return m_muted; }
    bool isPaused()  const { return m_paused; }
    int  pauseRemainingSeconds() const { return m_paused ? m_pauseRemaining : 0; }
    int  pauseMaxSeconds() const;
    bool isAudioAvailable() const { return m_audioAvailable; }
    int  beepCount() const { return m_beepCount; }

    /// 当前是否应该出声（供测试断言与调试）。
    bool shouldBeep() const;

    /**
     * @brief 关闭 / 开启真实声卡输出（自动化测试里关闭，避免 CI 上无音频设备）。
     *
     * @note 关闭后 `beepCount` 与声音定时器**仍照常运行**：`beepCount` 统计的是
     *       「策略上要求发声的次数」，而不是「声卡实际响了几次」。
     *       因此测试可以完整验证节奏与抑制逻辑，而无需真实声卡。
     */
    void setAudioEnabled(bool enabled);

    /// 当前是否允许送到声卡（与「策略上是否该响」无关，见 shouldBeep()）。
    bool isAudioEnabled() const { return m_audioEnabled; }

signals:
    void mutedChanged(bool muted);
    void pausedChanged(bool paused);
    void pauseChanged(int remainingSeconds);
    void audioAvailableChanged(bool available);
    void beepCountChanged(int count);

    /// 暂停开始 / 结束（供宿主写审计与日志）。
    /// @param seconds 开始时为设定时长，结束时为 0。
    void pauseStateChanged(bool paused, int seconds);

private slots:
    void onSoundTick();
    /// 暂停倒计时（1 Hz）；归零即自动恢复提示。
    void onPauseTick();

private:
    /// 按等级取提示音节奏（毫秒）：危急 900 / 一般 1800 / 提示 3000。
    static int soundIntervalMs(AlarmEvent::Level level);
    /// 按等级取连响次数：危急 3 声，其余 1 声（简易分级声提示）。
    static int beepsFor(AlarmEvent::Level level);

    /// 依据当前状态重启 / 停止声音定时器。
    void syncSoundTimer();
    /// 实际发声（不可用时只累加计数）。
    void beep();

    QTimer       *m_soundTimer = nullptr;
    QSoundEffect *m_sound      = nullptr;

    AlarmEvent::Level m_level      = AlarmEvent::Info;
    bool              m_hasPending = false;

    /// 上一次真正发声时的等级。
    /// @note 用来识别「等级刚变化」：此时必须立即按新节奏补一声，
    ///       否则升级要等旧周期走完才生效，等于延迟了更紧急的提示。
    AlarmEvent::Level m_lastBeepLevel = AlarmEvent::Info;

    bool m_muted          = false;
    bool m_paused         = false;
    int  m_pauseRemaining = 0;      ///< 暂停剩余秒数
    QTimer *m_pauseTimer  = nullptr;///< 秒级倒计时

    /// 暂停期开始时是否本应提示；用于「到期后若仍有未处理告警立即重报」。
    AlarmEvent::Level m_levelBeforePause = AlarmEvent::Info;

    bool m_audioAvailable = false;
    bool m_audioEnabled   = true;
    int  m_beepCount      = 0;
};

#endif // ALARMNOTIFIER_H
