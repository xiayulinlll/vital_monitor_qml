#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QVector>
#include <QElapsedTimer>

#include "data/SampleData.h"
#include "data/VitalSignSimulator.h"
#include "core/ChannelConfig.h"
#include "core/RingBuffer.h"
#include "core/AlarmDetector.h"

class TrendChartWidget;
class VitalValuePanel;
class ChannelConfigPanel;
class AlarmBanner;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QTimer;

/**
 * @brief 主窗口（对应架构 2.4 主 UI 面板层，并充当业务与数据管理层宿主）。
 *
 * 线程模型：
 *  - VitalSignSimulator 位于独立 QThread；
 *  - 采样帧经 QueuedConnection 信号到达本窗口后才能访问 UI；
 *  - 历史数据保存在环形缓冲区（RingBuffer），内存恒定。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    /// 通知采集线程启停（跨线程调用，自动排队）。
    void requestStart(int intervalMs);
    void requestStop();
    void requestUpdateConfigs(const QVector<ChannelConfig> &configs);

private slots:
    void onStartStopClicked();
    void onFrameReady(const SampleFrame &frame);
    void onStatusText(const QString &text);
    void onConfigApplied(const QVector<ChannelConfig> &channels);
    void onClearClicked();
    void onImportClicked();
    void onExportImageClicked();
    void onExportDataClicked();
    void onFollowToggled(bool on);
    void onSpanChanged(int value);
    void onPlaybackTick();
    /// 报警条「确认」按钮：签收全部活动告警（对应报警状态机 Acknowledged）。
    void onAcknowledgeAlarms();
    /// 配置面板切换患者类型：加载对应报警限剖面。
    void onPatientTypeChanged(PatientType type);

private:
    void setupUi();
    void setupThreading();
    void appendLog(const QString &text);
    void updateStatusBar();

    /// 把「告警事件」写入审计日志（按类别 / 状态分级）。
    void auditAlarm(const AlarmEvent &a);
    /// 记录配置变更差异到审计日志（含旧值 -> 新值）。
    void auditConfigDiff(const QVector<ChannelConfig> &oldCfgs,
                         const QVector<ChannelConfig> &newCfgs);
    /// 依据告警侦测器当前状态刷新报警条与数值面板边框。
    void refreshAlarmUi();

    // ---- 业务与数据管理（主线程） ----
    ChannelConfigManager m_configMgr;
    RingBuffer<SampleFrame> m_historyBuffer{60000};   ///< 历史采样环形缓冲
    RingBuffer<AlarmEvent>  m_alarmBuffer{2000};      ///< 告警事件环形缓冲
    AlarmDetector           m_alarmDetector;

    /// 按通道缓存最近若干帧，用于绘图控件（兼容局部重绘）。
    QVector<SampleFrame> m_pendingFrames;
    QVector<AlarmEvent>  m_pendingAlarms;

    // ---- 采集线程 ----
    QThread           *m_workerThread = nullptr;
    VitalSignSimulator *m_simulator = nullptr;

    // ---- UI ----
    TrendChartWidget   *m_chart = nullptr;
    VitalValuePanel    *m_valuePanel = nullptr;
    ChannelConfigPanel *m_configPanel = nullptr;
    AlarmBanner        *m_alarmBanner = nullptr;
    QPlainTextEdit     *m_logView = nullptr;
    QLabel             *m_statusLabel = nullptr;
    QLabel             *m_viewportLabel = nullptr;
    QPushButton        *m_startStopBtn = nullptr;

    // ---- 回放 ----
    QTimer        *m_playbackTimer = nullptr;
    QVector<SampleFrame> m_playbackFrames;
    int            m_playbackPos = 0;
    bool           m_playingBack = false;
    bool           m_running = false;

    qint64 m_frameCount = 0;
    quint64 m_droppedFrames = 0;   ///< 累计丢帧数（数据完整性统计）
    /// 最近一帧的采集时间戳（毫秒），作为告警确认等操作的统一时基。
    qint64 m_lastFrameMs = 0;
    QElapsedTimer m_uptime;
};

#endif // MAINWINDOW_H
