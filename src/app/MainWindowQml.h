#ifndef MAINWINDOWQML_H
#define MAINWINDOWQML_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVariant>
#include <QVariantList>
#include <QVector>

#include "core/AlarmDetector.h"
#include "core/AlarmNotifier.h"
#include "core/ChannelConfig.h"
#include "core/RingBuffer.h"
#include "data/SampleData.h"
#include "data/VitalSignSimulator.h"

class ChartBridge;
class ChannelModel;
class AlarmModel;
class LogModel;
class QQmlContext;
class QTimer;

/**
 * @brief QML 版业务宿主（对应 C++ 版 `MainWindow`，但**不含任何 UI 代码**）。
 *
 * ## 分层对照
 *
 * | Widgets 版                 | QML 版                              | 职责 |
 * |----------------------------|-------------------------------------|------|
 * | `MainWindow`（QMainWindow） | `MainWindowQml` + `Main.qml`       | 壳：业务宿主 / 视图 |
 * | `TrendChartWidget`          | `ChartBridge`（QQuickPaintedItem） | 绘图内核（原样复用）|
 * | `VitalValuePanel`           | `ChannelModel` + `VitalValueCard.qml` | 数值卡片 |
 * | `AlarmBanner`               | `AlarmModel` + `AlarmListPanel.qml` | 告警呈现 |
 * | `QPlainTextEdit` 日志       | `LogModel` + `LogPanel.qml`         | 日志 |
 * | `ChannelConfigPanel`        | `ChannelConfigPanel.qml`            | 配置编辑 |
 *
 * ## 数据面 / 控制面分离（本类存在的核心理由）
 *
 * 高频数据走 C++ → C++ 直连，**不跨越 QML 引擎**：
 *
 * ```
 * 采集线程 --queued--> MainWindowQml::onFrameReady()
 *                        |-> m_chart->appendFrames()   // 直连，零 QVariant 转换
 *                        |-> m_channelModel->applyFrame()
 *                        '-> 3 Hz 才推一次状态给 Backend.qml（QML 侧绑定）
 * ```
 *
 * 若把每帧 4 通道数据打包成 `QVariantList` 交给 QML，10 Hz × 4 通道
 * 会产生持续的堆分配与 JS 侧属性写入，是本方案最典型的性能陷阱。
 *
 * ## 线程模型（与 Widgets 版逐字一致）
 *
 * `VitalSignSimulator` 位于独立 `QThread`；本类仅通过 `QueuedConnection`
 * 接收 `frameReady`，因此**所有业务状态与 QML 访问都发生在 GUI 线程**。
 */
class MainWindowQml : public QObject
{
    Q_OBJECT

    // ---- 只读状态（供 Backend.qml 在 attach 时拉取一次）----
    // @note 这些值与 pushStatsToQml() 推送到 QML 的是同一份数据；
    //       提供 Q_PROPERTY 是为了让 QML 能「主动读」，
    //       而不必只依赖 C++ 的推送时机。
    Q_PROPERTY(bool    running          READ isRunning       NOTIFY runningChanged)
    Q_PROPERTY(bool    playingBack      READ isPlayingBack   NOTIFY playingBackChanged)
    Q_PROPERTY(int     frameCount       READ frameCount      NOTIFY statsChanged)
    Q_PROPERTY(int     droppedFrames    READ droppedFrames   NOTIFY statsChanged)
    Q_PROPERTY(int     historySize      READ historySize     NOTIFY statsChanged)
    Q_PROPERTY(int     activeAlarmCount READ activeAlarmCount NOTIFY statsChanged)
    Q_PROPERTY(int     totalAlarmCount  READ totalAlarmCount NOTIFY statsChanged)
    Q_PROPERTY(int     uptimeSec        READ uptimeSec       NOTIFY statsChanged)
    Q_PROPERTY(double  lastFrameMs      READ lastFrameMs     NOTIFY statsChanged)

    // ---- 声光提示状态（第二梯队第 6、7 项）----
    // @note Backend.qml 的 syncFromHost() 会逐项读取这些属性；
    //       缺任何一个都会在启动时打印
    //       “Cannot assign [undefined] to bool / int”，并让首屏按钮状态错乱
    //       （典型症状：报警已静音但静音按钮没亮）。
    Q_PROPERTY(bool alarmMuted           READ alarmMuted           NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(bool alarmPaused          READ alarmPaused          NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(int  alarmPauseRemaining  READ alarmPauseRemaining  NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(int  alarmPauseMaxSeconds READ alarmPauseMaxSeconds NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(bool audioAvailable       READ audioAvailable       NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(int  alarmBeepCount       READ alarmBeepCount       NOTIFY alarmNotifyStateChanged)
    Q_PROPERTY(int  highestPendingLevel  READ highestPendingLevel  NOTIFY alarmNotifyStateChanged)

public:
    explicit MainWindowQml(QObject *parent = nullptr);
    ~MainWindowQml() override;

    /// 装配采集线程（必须在 QML 引擎加载前调用）。
    void setupThreading();

    /// 打开审计日志并写入「会话启动」记录（等价 MainWindow 构造中的审计段）。
    void openAuditLog();

    /// 把模型 / 绘图桥注册进 QML 上下文，并注入 Backend.qml 的回调。
    void exposeToQml(QQmlContext *context);

    // ---- 供 QML 侧持有（`exposeToQml` 中 setContextProperty）----
    ChartBridge  *chart()        const { return m_chart; }
    ChannelModel *channelModel() const { return m_channelModel; }
    AlarmModel   *alarmModel()   const { return m_alarmModel; }
    LogModel     *logModel()     const { return m_logModel; }

    // ---- 上面 Q_PROPERTY 的读取器 ----
    bool   isRunning()        const { return m_running; }
    bool   isPlayingBack()    const { return m_playingBack; }
    int    frameCount()       const { return int(m_frameCount); }
    int    droppedFrames()    const { return int(m_droppedFrames); }
    int    historySize()      const { return m_historyBuffer.size(); }
    int    activeAlarmCount() const { return m_alarmDetector.activeCount(); }
    int    totalAlarmCount()  const { return m_alarmBuffer.size(); }
    int    uptimeSec()        const
    {
        return m_uptime.isValid() ? int(m_uptime.elapsed() / 1000) : 0;
    }
    double lastFrameMs()      const { return double(m_lastFrameMs); }

    // ---- 声光提示状态（供 Backend.qml 首屏同步）----
    bool alarmMuted()           const { return m_notifier && m_notifier->isMuted(); }
    bool alarmPaused()          const { return m_notifier && m_notifier->isPaused(); }
    int  alarmPauseRemaining()  const { return m_notifier ? m_notifier->pauseRemainingSeconds() : 0; }
    int  alarmPauseMaxSeconds() const { return m_notifier ? m_notifier->pauseMaxSeconds()
                                                          : AlarmNotifier::kNormalPauseMaxSeconds; }
    bool audioAvailable()       const { return m_notifier && m_notifier->isAudioAvailable(); }
    int  alarmBeepCount()       const { return m_notifier ? m_notifier->beepCount() : 0; }
    /// 当前最高「待提示」等级（活动且未确认）；无活动告警时为 0（Info）。
    int  highestPendingLevel()  const { return int(m_alarmDetector.highestPendingLevel()); }

public slots:
    /// 开始采集（QML「开始采集」按钮）。
    void startAcquisition(int intervalMs = 100);
    /// 停止采集。
    void stopAcquisition();
    /// 提交配置面板的编辑结果（QML 传入 JS 对象数组）。
    void applyConfigFromQml(const QVariantList &configs);
    /// 切换患者类型：0 成人 / 1 小儿 / 2 新生儿 / 3 自定义。
    void changePatientType(int type);
    /// 确认（签收）全部活动告警。
    void acknowledgeAllAlarms();
    /// 清空历史 / 告警 / 曲线。
    void clearAll();
    /// 导入历史文件并回放。
    void importHistory();
    /// 导出趋势截图。
    void exportChartImage();
    /// 导出历史数据 CSV。
    void exportHistoryData();
    /// 设置视口跨度（采样点数）。
    void setVisibleSpan(int spanPoints);
    /// 设置是否实时跟随最新数据。
    void setFollowLatest(bool on);
    /// 每通道独立 Y 轴刻度开关。
    void setMultiAxis(bool on);

    // ---- 界面信息登记（9.1 第 4、5 项）----
    /// QML 侧要求记一条「提示」日志（不写审计）。
    /// @note 用于「演示模式」标识被点击时的声明文本：日志模型在 C++，
    ///       QML 不应直接拼装内部数据结构。
    void logNotice(const QString &text);
    /// 患者信息（床号 / ID / 姓名）变更：写日志 + 写审计（谁在何时改了患者上下文）。
    void recordPatientInfo(const QString &summary);

    // ---- 报警声光提示（第二梯队第 6、7 项）----
    /// 是否静音（只关声音，视觉提示保留）。
    void setAlarmMuted(bool muted);
    /// 暂停报警提示若干秒（声音 + 闪烁），到期自动恢复；返回实际生效秒数。
    int  pauseAlarms(int seconds = AlarmNotifier::kNormalPauseMaxSeconds);
    /// 提前结束暂停并恢复提示。
    void resumeAlarms();

    /**
     * @brief 由 QML 侧把 `Backend` 单例实例交给 C++。
     *
     * @note 之所以需要这个「回传」：Qt 6.2 的
     *       `QQmlEngine::singletonInstance()` 只接受 `int qmlTypeId`，
     *       而**文件式 QML 单例**（qmldir 里的 `singleton Backend`）
     *       不在 qmlTypeId 的检索范围内。与其依赖版本相关的私有 API，
     *       不如让 Main.qml 在 `Component.onCompleted` 里显式交回实例。
     *
     *       传 `nullptr` 表示 QML 侧卸载（弱引用自动置空）。
     */
    void attachBackend(QObject *backend);

signals:
    // ---- 跨线程：通知采集线程启停（自动排队）----
    void requestStart(int intervalMs);
    void requestStop();
    void requestUpdateConfigs(const QVector<ChannelConfig> &configs);

    // ---- 供 Backend.qml 绑定的状态变化通知 ----
    void runningChanged(bool running);
    void playingBackChanged(bool playingBack);
    void statsChanged();
    void activeAlarmsChanged();
    void viewportInfoChanged(const QString &info);

    /// 报警提示状态（静音 / 暂停 / 声卡可用性）变化 → 推送到 Backend.qml。
    void alarmNotifyStateChanged();

private slots:
    void onFrameReady(const SampleFrame &frame);
    void onStatusText(const QString &text);
    void onPlaybackTick();
    /// 低频（3 Hz）把业务状态推给 Backend.qml。
    void onStatusTick();

private:
    /// 依据告警侦测器当前状态刷新告警模型与数值卡片边框。
    void refreshAlarmUi();
    /// 把「告警事件」写入审计日志（按类别 / 状态分级）。
    void auditAlarm(const AlarmEvent &a);
    /// 记录配置变更差异到审计日志（含旧值 -> 新值）。
    void auditConfigDiff(const QVector<ChannelConfig> &oldCfgs,
                         const QVector<ChannelConfig> &newCfgs);
    /// 统一日志入口：写入 QML 日志模型。
    void appendLog(const QString &text, int level = 0);
    /// 把 C++ 状态镜像同步进 Backend.qml 的属性。
    void pushStatsToQml();

    /// 解析 QML 传入的 JS 对象数组为 ChannelConfig（保持 id 对齐）。
    static QVector<ChannelConfig> parseConfigs(const QVariantList &list,
                                               const QVector<ChannelConfig> &existing);

    // ---- 业务与数据管理（主线程）----
    ChannelConfigManager    m_configMgr;
    RingBuffer<SampleFrame> m_historyBuffer{60000};   ///< 历史采样环形缓冲
    RingBuffer<AlarmEvent>  m_alarmBuffer{2000};      ///< 告警事件环形缓冲
    AlarmDetector           m_alarmDetector;
    AlarmNotifier          *m_notifier = nullptr;   ///< 声音 / 静音 / 暂停（声光提示）

    /// 批量提交缓存：每 2 帧合并一次，降低绘图重绘次数（与 Widgets 版一致）。
    QVector<SampleFrame> m_pendingFrames;
    QVector<AlarmEvent>  m_pendingAlarms;

    // ---- 采集线程 ----
    QThread            *m_workerThread = nullptr;
    VitalSignSimulator *m_simulator    = nullptr;

    // ---- QML 侧数据源 ----
    ChartBridge  *m_chart        = nullptr;
    ChannelModel *m_channelModel = nullptr;
    AlarmModel   *m_alarmModel   = nullptr;
    LogModel     *m_logModel     = nullptr;

    // ---- 回放 ----
    QTimer              *m_playbackTimer = nullptr;
    QVector<SampleFrame> m_playbackFrames;
    int  m_playbackPos = 0;
    bool m_playingBack = false;
    bool m_running     = false;

    /// 3 Hz 状态推送定时器（替代 Widgets 版的逐帧 updateStatusBar）。
    QTimer *m_statusTimer = nullptr;

    /// QML 侧回传的 Backend 单例（弱引用，随 QML 引擎生命周期）。
    /// @note 用 QPointer 而不是裸指针：Backend 是 QML 引擎拥有的对象，
    ///       引擎析构时它会被删除，QPointer 能自动置空避免悬挂。
    QPointer<QObject> m_backend;

    /// 首次 attachBackend() 之前，pushStatsToQml() 什么都不做。
    bool m_pendingBackendInit = true;

    qint64  m_frameCount    = 0;
    quint64 m_droppedFrames = 0;   ///< 累计丢帧数（数据完整性统计）
    /// 最近一帧的采集时间戳，作为告警确认的统一时基（必须与 detect() 一致）。
    qint64  m_lastFrameMs = 0;
    QElapsedTimer m_uptime;

    /// 抑制标志：避免 pushStatsToQml() 写属性时触发递归。
    bool m_pushingStats = false;
};

#endif // MAINWINDOWQML_H
