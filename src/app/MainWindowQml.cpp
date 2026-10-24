#include "app/MainWindowQml.h"

#include "chart/ChartBridge.h"
#include "core/AuditLogger.h"
#include "core/HistoryRecorder.h"
#include "models/AlarmModel.h"
#include "models/ChannelModel.h"
#include "models/LogModel.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

/*
 * @note 关于 QFileDialog：本程序是 QGuiApplication（无 QtWidgets）。
 *       QFileDialog 属于 QtWidgets，在无 QtWidgets 环境下不可用；
 *       因此本文件**不使用** QFileDialog，导入 / 导出路径改为：
 *         - 默认写到 <应用目录>/exports/ 与 <应用目录>/imports/；
 *         - 路径与结果通过日志反馈给 QML。
 *       这样既保持「无 QtWidgets 依赖」的约束，也避免在自绘 QML 界面里
 *       混入原生对话框导致操作位置不一致。
 *       若后续需要文件选择器，应改用 QML 侧的 QtQuick.Dialogs。
 */

namespace {

/// 把告警类别映射到审计日志类别（与 Widgets 版 MainWindow.cpp 同名函数一致）。
AuditLogger::Category alarmCategoryOf(AlarmEvent::Category c)
{
    return c == AlarmEvent::Technical ? AuditLogger::TechAlarm : AuditLogger::Alarm;
}

/// 告警等级 → 审计严重度。
AuditLogger::Severity auditSeverityOf(AlarmEvent::Level lv)
{
    switch (lv) {
    case AlarmEvent::Escalated:
    case AlarmEvent::Critical: return AuditLogger::Critical;
    case AlarmEvent::Warning:  return AuditLogger::Warning;
    case AlarmEvent::Info:     return AuditLogger::Info;
    }
    return AuditLogger::Info;
}

/// 告警状态中文名（与 MainWindow::auditAlarm 的措辞保持一致）。
QString alarmStateName(AlarmEvent::State st)
{
    switch (st) {
    case AlarmEvent::Active:        return QStringLiteral("激活");
    case AlarmEvent::Acknowledged:  return QStringLiteral("已确认");
    case AlarmEvent::Cleared:       return QStringLiteral("解除");
    case AlarmEvent::LatchedSilent: return QStringLiteral("静音重报");
    }
    return QStringLiteral("激活");
}

/// 采样间隔（10 Hz，与 Widgets 版 requestStart(100) 完全一致）。
constexpr int kSampleIntervalMs = 100;
/// 每累积该数量帧才提交一次绘图（降低重绘频率）。
constexpr int kChartBatchFrames = 2;
/// 状态推送周期：3 Hz。数值卡片与曲线走各自的高频通道，
/// 只有状态栏文本这类低频内容才需要节流推送。
constexpr int kStatusPushMs = 333;
/// 回放推送间隔（等价 Widgets 版 m_playbackTimer->setInterval(50)）。
constexpr int kPlaybackIntervalMs = 50;
/// 每次回放 tick 推送的帧数（等价 Widgets 版 kBurst）。
constexpr int kPlaybackBurst = 3;

/// LogModel 的级别取值，与 models/LogModel.h 的 enum Level 对齐。
enum LogLevel { LogInfo = 0, LogWarn = 1, LogError = 2 };

} // namespace

MainWindowQml::MainWindowQml(QObject *parent)
    : QObject(parent)
{
    m_chart        = new ChartBridge();
    m_channelModel = new ChannelModel(this);
    m_alarmModel   = new AlarmModel(this);
    m_logModel     = new LogModel(this);

    // 首屏即按默认通道构建数值卡片与曲线图例，避免启动瞬间空白。
    const QVector<ChannelConfig> cfgs = m_configMgr.channels();
    m_chart->setChannels(cfgs);
    m_channelModel->resetChannels(cfgs);

    // 报警延时使用毫秒制（与 Widgets 版一致：10 Hz 下 1000 ms ≈ 原 10 帧去抖）。
    m_alarmDetector.setDefaultSampleIntervalMs(kSampleIntervalMs);

    // 声光提示控制器：声音节奏 / 静音 / 暂停的唯一真源。
    // 放在 C++ 而不是 QML，是为了与 AlarmDetector 的 alarmRepeatMs 共用同一套
    // 计时语义，避免「横幅在闪、声音却没跟上」。
    m_notifier = new AlarmNotifier(this);

    // 暂停状态变化必须留痕：抑制报警属影响患者安全的操作（IEC 60601-1-8）。
    connect(m_notifier, &AlarmNotifier::pauseStateChanged, this,
            [this](bool paused, int seconds) {
                if (paused) {
                    appendLog(QStringLiteral("[告警] 报警提示已暂停 %1 秒（到期自动恢复）")
                                  .arg(seconds),
                              LogWarn);
                    AuditLogger::instance().log(
                        AuditLogger::Alarm, AuditLogger::Warning,
                        QStringLiteral("报警暂停"),
                        QStringLiteral("暂停时长=%1s 活动告警=%2 条 最高等级=%3")
                            .arg(seconds)
                            .arg(m_alarmDetector.activeCount())
                            .arg(static_cast<int>(m_alarmDetector.highestPendingLevel())));
                } else {
                    appendLog(QStringLiteral("[告警] 报警提示已恢复"), LogInfo);
                    AuditLogger::instance().log(
                        AuditLogger::Alarm, AuditLogger::Info,
                        QStringLiteral("报警暂停结束"),
                        QStringLiteral("活动告警=%1 条")
                            .arg(m_alarmDetector.activeCount()));
                }
            });

    // 静音同样留痕（只关声音，不等于处理了报警）。
    connect(m_notifier, &AlarmNotifier::mutedChanged, this, [this](bool muted) {
        appendLog(QStringLiteral("[告警] 声音提示%1")
                      .arg(muted ? QStringLiteral("已静音（视觉提示保留）")
                                 : QStringLiteral("已恢复")),
                  muted ? LogWarn : LogInfo);
        AuditLogger::instance().log(
            AuditLogger::Alarm, muted ? AuditLogger::Warning : AuditLogger::Info,
            muted ? QStringLiteral("报警静音") : QStringLiteral("取消静音"),
            QStringLiteral("活动告警=%1 条").arg(m_alarmDetector.activeCount()));
    });

    // 任何提示状态变化都要同步给 QML（倒计时 / 按钮状态 / 横幅外观）。
    connect(m_notifier, &AlarmNotifier::pausedChanged, this,
            [this](bool) { pushStatsToQml(); });
    connect(m_notifier, &AlarmNotifier::pauseChanged, this,
            [this](int) { pushStatsToQml(); });
    connect(m_notifier, &AlarmNotifier::audioAvailableChanged, this,
            [this](bool available) {
                if (!available)
                    appendLog(QStringLiteral("[告警] 未检测到可用音频输出，将只有视觉报警"),
                              LogWarn);
                pushStatsToQml();
            });

    // 回放定时器（等价 MainWindow::setupUi 中的 m_playbackTimer）。
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setInterval(kPlaybackIntervalMs);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindowQml::onPlaybackTick);

    // 低频状态推送：QML 侧的 Text/绑定只在 3 Hz 重新求值，
    // 而不是每帧（10 Hz）重算整条状态栏字符串。
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(kStatusPushMs);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindowQml::onStatusTick);
    m_statusTimer->start();

    // 视口变化（滚轮缩放 / 拖拽平移）也会改变状态栏文本。
    connect(m_chart, &ChartBridge::viewportInfoChanged, this, [this]() {
        emit viewportInfoChanged(m_chart->viewportInfo());
        emit statsChanged();
    });
}

MainWindowQml::~MainWindowQml()
{
    // 先请求停止，再退出线程，最后释放模拟器（顺序与 Widgets 版一致）。
    if (m_workerThread) {
        emit requestStop();
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }

    AuditLogger::instance().log(
        AuditLogger::System, AuditLogger::Info,
        QStringLiteral("会话结束"),
        QStringLiteral("累计采集 %1 帧，丢帧 %2 帧，告警 %3 条")
            .arg(m_frameCount)
            .arg(m_droppedFrames)
            .arg(m_alarmBuffer.size()));
    AuditLogger::instance().close();

    // ChartBridge 是 QQuickPaintedItem，若已交给 QML 引擎管理则由引擎析构；
    // 未成功加载 QML 时（例如启动失败）其 parent 仍为空，需要在此释放。
    if (m_chart && !m_chart->parent())
        delete m_chart;
}

void MainWindowQml::setupThreading()
{
    m_workerThread = new QThread(this);
    m_simulator    = new VitalSignSimulator();   // 无 parent，随后 moveToThread
    m_simulator->moveToThread(m_workerThread);

    // ---- 跨线程信号槽（QueuedConnection 自动生效）----
    connect(this, &MainWindowQml::requestStart,
            m_simulator, &VitalSignSimulator::start);
    connect(this, &MainWindowQml::requestStop,
            m_simulator, &VitalSignSimulator::stop);
    connect(this, &MainWindowQml::requestUpdateConfigs,
            m_simulator, &VitalSignSimulator::updateConfigs);

    connect(m_simulator, &VitalSignSimulator::frameReady,
            this, &MainWindowQml::onFrameReady);        // 工作线程 -> 主线程
    connect(m_simulator, &VitalSignSimulator::statusText,
            this, &MainWindowQml::onStatusText);

    connect(m_workerThread, &QThread::finished,
            m_simulator, &QObject::deleteLater);

    m_workerThread->start();
}

void MainWindowQml::openAuditLog()
{
    AuditLogger &audit = AuditLogger::instance();
    const QString auditDir = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("audit"));
    const QString auditPath = QDir(auditDir).filePath(
        QStringLiteral("audit_%1.log")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))));

    if (audit.openFile(auditPath)) {
        audit.log(AuditLogger::System, AuditLogger::Info,
                  QStringLiteral("会话启动"),
                  QStringLiteral("通道数=%1，患者类型=%2，采样间隔=%3ms")
                      .arg(m_configMgr.count())
                      .arg(patientTypeName(m_configMgr.patientType()))
                      .arg(kSampleIntervalMs));
        appendLog(QStringLiteral("[审计] 日志已启用：%1").arg(auditPath),
                  LogInfo);
    } else {
        appendLog(QStringLiteral("[审计] 日志文件不可写，事件将仅输出到控制台"),
                  LogWarn);
    }
}


// ---------------------------------------------------------------------------
// 暴露给 QML
// ---------------------------------------------------------------------------

void MainWindowQml::exposeToQml(QQmlContext *context)
{
    Q_ASSERT(context);

    // ---- 1) 上下文属性：QML 可直接把模型当 ListView / Repeater 的 model ----
    context->setContextProperty(QStringLiteral("chartBridge"),  m_chart);
    context->setContextProperty(QStringLiteral("channelModel"), m_channelModel);
    context->setContextProperty(QStringLiteral("alarmModel"),   m_alarmModel);
    context->setContextProperty(QStringLiteral("logModel"),     m_logModel);

    // ---- 2) 找到 qml/Backend.qml 单例，注入初始状态 ----
    //
    // @note 为什么不用 QQmlEngine::singletonInstance()：
    //       Qt 6.2 的签名是 `singletonInstance<T>(int qmlTypeId)`，
    //       而 **文件式 QML 单例（qmldir 里的 `singleton Backend`）
    //       不会被 qmlTypeId 检索到** —— qmlTypeId 只覆盖由 C++
    //       注册的类型（qmlRegisterSingletonType / QML_ELEMENT 等）。
    //       因此这里改用「QML 侧自注册」方案：
    //         Main.qml 在 Component.onCompleted 里调用
    //         controller.attachBackend(Backend) 把实例交给 C++，
    //       由 C++ 保存弱引用，后续 pushStatsToQml() 直接写它的属性。
    //       这样既不依赖 6.2 缺失的 API，也避免 C++ 端硬编码类型 id。
    m_pendingBackendInit = true;

    appendLog(QStringLiteral("[系统] 多通道生理信号趋势监测上位机（QML 版）已启动"),
              LogInfo);
    appendLog(QStringLiteral("[系统] 通道数：%1，历史缓冲：%2 帧")
                  .arg(m_configMgr.count())
                  .arg(m_historyBuffer.capacity()),
              LogInfo);
}

// ---------------------------------------------------------------------------
// 采集控制
// ---------------------------------------------------------------------------

void MainWindowQml::startAcquisition(int intervalMs)
{
    if (m_running)
        return;

    // 回放中启动实时采集：先停掉回放，避免两个数据源同时写曲线。
    if (m_playingBack) {
        m_playbackTimer->stop();
        m_playingBack = false;
        emit playingBackChanged(false);
    }

    if (intervalMs <= 0)
        intervalMs = kSampleIntervalMs;

    emit requestStart(intervalMs);
    m_running = true;
    m_uptime.start();

    appendLog(QStringLiteral("[采集] 开始采集（%1 ms 周期）").arg(intervalMs),
              LogInfo);
    AuditLogger::instance().log(
        AuditLogger::System, AuditLogger::Info,
        QStringLiteral("开始采集"),
        QStringLiteral("采样间隔 %1 ms").arg(intervalMs));

    emit runningChanged(true);
    pushStatsToQml();
}

void MainWindowQml::stopAcquisition()
{
    if (!m_running)
        return;

    emit requestStop();
    m_running = false;

    appendLog(QStringLiteral("[采集] 停止采集，累计 %1 帧").arg(m_frameCount),
              LogInfo);
    AuditLogger::instance().log(
        AuditLogger::System, AuditLogger::Info,
        QStringLiteral("停止采集"),
        QStringLiteral("累计 %1 帧，丢帧 %2 帧")
            .arg(m_frameCount)
            .arg(m_droppedFrames));

    emit runningChanged(false);
    pushStatsToQml();
}

// ---------------------------------------------------------------------------
// 数据热路径（本函数在 GUI 线程执行，每 100 ms 一次）
// ---------------------------------------------------------------------------

void MainWindowQml::onFrameReady(const SampleFrame &frame)
{
    // 跨线程信号槽已排队，此处运行在主线程，可安全访问模型与绘图控件。
    ++m_frameCount;

    // 记录最新帧时间戳：后续「确认告警」需与 detect() 使用同一时基，
    // 否则重复提示 / 升级的计时会因时基错乱而失效。
    // 必须取单调时钟（与 AlarmDetector::frameTimeMs() 一致），
    // 而不是墙钟 timestampMs —— 两者相差数十年，混用会让计时完全失效。
    m_lastFrameMs = frame.monotonicMs > 0 ? frame.monotonicMs : frame.timestampMs;

    // 1) 写入历史环形缓冲（内存恒定，难点 2）。
    m_historyBuffer.append(frame);

    // 1.1) 数据完整性统计：累计丢帧用于状态栏展示。
    if (frame.droppedFrames > 0)
        m_droppedFrames += frame.droppedFrames;

    // 2) 告警判断（报警状态机 + SQI 门控 + 技术报警）。
    const QVector<AlarmEvent> alarms =
        m_alarmDetector.detect(frame, m_configMgr.channels());

    for (const AlarmEvent &a : alarms) {
        m_alarmBuffer.append(a);
        m_pendingAlarms.append(a);

        // 日志级别按告警严重度分级，便于 QML 侧着色与过滤。
        int level = LogInfo;
        if (a.level == AlarmEvent::Warning)
            level = LogWarn;
        else if (a.level == AlarmEvent::Critical || a.level == AlarmEvent::Escalated)
            level = LogError;

        appendLog(QStringLiteral("[告警] %1").arg(a.text), level);
        auditAlarm(a);
    }

    // 3) 交给绘图内核（**直连**，不经过 QML 引擎）与数值卡片模型。
    m_pendingFrames.append(frame);

    // 批量提交，减少重绘次数（每 2 帧合并一次，与 Widgets 版一致）。
    if (m_pendingFrames.size() >= kChartBatchFrames) {
        m_chart->appendFrames(m_pendingFrames);
        if (!m_pendingAlarms.isEmpty())
            m_chart->appendAlarms(m_pendingAlarms);
        m_pendingFrames.clear();
        m_pendingAlarms.clear();
    }

    // 数值卡片：按行发 dataChanged，只有数值真正变化的行才重绘。
    m_channelModel->applyFrame(frame);

    // 4) 同步告警模型与通道卡片边框（声光 + 视觉联动）。
    if (!alarms.isEmpty() || m_alarmDetector.activeCount() > 0)
        refreshAlarmUi();

    // 4.1) 驱动声音提示：只统计「活动且未确认」的告警。
    //      已确认的告警不发声，直到 AlarmDetector 按 alarmRepeatMs 重新上报
    //      （那时 highestPendingLevel() 会重新升高）。这样「确认」与
    //      「永久关闭」被严格区分开 —— 前者是签收，后者不是本程序的能力。
    if (m_notifier) {
        m_notifier->updateTopLevel(m_alarmDetector.highestPendingLevel(),
                                   m_alarmDetector.hasPendingAlarm());
    }

    // 5) 状态栏走 3 Hz 定时推送（见 onStatusTick），此处不逐帧刷新。
}

void MainWindowQml::onStatusText(const QString &text)
{
    appendLog(QStringLiteral("[采集线程] %1").arg(text), LogInfo);
}

void MainWindowQml::onStatusTick()
{
    pushStatsToQml();
}

void MainWindowQml::attachBackend(QObject *backend)
{
    m_backend = backend;
    m_pendingBackendInit = false;

    if (!backend) {
        qWarning("MainWindowQml: QML 侧已断开 Backend，状态推送将暂停。");
        return;
    }

    // 立刻注入一次当前状态，避免 QML 首屏显示全 0 直到第一次 3 Hz 推送。
    pushStatsToQml();

    appendLog(QStringLiteral("[系统] 已与 QML Backend 建立状态通道"), LogInfo);
}

void MainWindowQml::pushStatsToQml()
{
    if (m_pushingStats)
        return;
    m_pushingStats = true;

    // m_backend 由 Main.qml 在 Component.onCompleted 里通过
    // attachBackend() 交回；未接通前直接跳过（首帧可能早于 QML 完成加载）。
    QObject *backend = m_backend.data();
    if (backend) {
        backend->setProperty("running",          m_running);
        backend->setProperty("playingBack",      m_playingBack);
        backend->setProperty("frameCount",       int(m_frameCount));
        backend->setProperty("droppedFrames",    int(m_droppedFrames));
        backend->setProperty("historySize",      m_historyBuffer.size());
        backend->setProperty("activeAlarmCount", m_alarmDetector.activeCount());
        backend->setProperty("totalAlarmCount",  m_alarmBuffer.size());
        backend->setProperty("uptimeSec",
                             int(m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0));
        backend->setProperty("lastFrameMs",      double(m_lastFrameMs));
        backend->setProperty("viewportInfo",     m_chart->viewportInfo());

        // ---- 声光提示状态（第二梯队第 6、7 项）----
        // 这些是「可绑定」的标量，3 Hz 推送足够：
        // 倒计时按秒变化，按钮外观变化也远低于 10 Hz。
        if (m_notifier) {
            backend->setProperty("alarmMuted",
                                 m_notifier->isMuted());
            backend->setProperty("alarmPaused",
                                 m_notifier->isPaused());
            backend->setProperty("alarmPauseRemaining",
                                 m_notifier->pauseRemainingSeconds());
            backend->setProperty("alarmPauseMaxSeconds",
                                 m_notifier->pauseMaxSeconds());
            backend->setProperty("audioAvailable",
                                 m_notifier->isAudioAvailable());
            backend->setProperty("alarmBeepCount",
                                 m_notifier->beepCount());
            backend->setProperty("highestPendingLevel",
                                 int(m_alarmDetector.highestPendingLevel()));
        }
    }

    emit statsChanged();
    m_pushingStats = false;
}

// ---------------------------------------------------------------------------
// 报警 / 审计
// ---------------------------------------------------------------------------

void MainWindowQml::refreshAlarmUi()
{
    const QVector<AlarmEvent> active = m_alarmDetector.activeAlarms();

    // 持续时长必须来自与 detect() 相同的时基（单调时钟），
    // 否则系统校时后会出现「已持续 -3600 秒」这类荒谬数值。
    const QHash<QString, qint64> durations =
        m_alarmDetector.activeDurationsMs(m_lastFrameMs);

    // 结构化告警列表（QML 相对 Widgets 版的主要增益：可同时显示多条）。
    // 注意 durations 的键是 trackKey（技术报警为 "1/2/SQI"），
    // 与 AlarmEvent::conditionKey() 不是同一空间，不能互换。
    m_alarmModel->setAlarms(active, durations);
    emit activeAlarmsChanged();

    // 数值卡片边框按告警严重度着色，形成全局视觉报警。
    m_channelModel->applyAlarmState(active);
}

void MainWindowQml::acknowledgeAllAlarms()
{
    // 确认全部活动告警：状态机进入 Acknowledged，声音提示停止；
    // 若条件仍成立将按 repeatMs 重新提示（不会永久静默）。
    //
    // 显式传入最近一帧的采集时间戳作为时基，保证与 detect() 完全一致；
    // 若尚未收到任何帧（m_lastFrameMs 仍是 0），回退到系统当前时间。
    // acknowledge() 是幂等的：连点按钮不会产生重复审计记录。
    const QVector<AlarmEvent> acked =
        m_alarmDetector.acknowledge(QString(), m_lastFrameMs);

    if (acked.isEmpty()) {
        appendLog(QStringLiteral("[告警] 确认操作：当前无活动告警"), LogInfo);
        return;
    }

    appendLog(QStringLiteral("[告警] 用户确认 %1 条告警").arg(acked.size()),
              LogInfo);

    // 记录「持续时长」：确认动作发生在告警出现多久之后，
    // 是可用性工程（IEC 62366）与报警系统评价的关键指标
    // （响应过慢说明提示不足，过频说明误报太多）。
    const QHash<QString, qint64> durations =
        m_alarmDetector.activeDurationsMs(m_lastFrameMs);

    for (const AlarmEvent &a : acked) {
        // 用 trackKey 回查：技术报警的键形如 "1/2/SQI"，
        // 而 conditionKey() 会生成 "1/2/HI"，永远查不到时长。
        const qint64 ms = durations.value(a.trackKey, -1);
        AuditLogger::instance().log(
            alarmCategoryOf(a.category), AuditLogger::Info,
            QStringLiteral("告警确认"),
            QStringLiteral("通道=%1 条件=%2 持续时间=%3s 描述=%4")
                .arg(a.channelId)
                .arg(a.trackKey,
                     ms >= 0 ? QString::number(ms / 1000.0, 'f', 1)
                             : QStringLiteral("?"),
                     a.text));
    }

    refreshAlarmUi();

    // 确认后声音应立即停止：highestPendingLevel() 此时已回到待提示之外。
    if (m_notifier) {
        m_notifier->updateTopLevel(m_alarmDetector.highestPendingLevel(),
                                   m_alarmDetector.hasPendingAlarm());
    }

    pushStatsToQml();
}

void MainWindowQml::setAlarmMuted(bool muted)
{
    if (!m_notifier)
        return;

    // 静音只影响声音，横幅仍按优先级闪烁 —— 视觉提示不得被静音一起关掉。
    m_notifier->setMuted(muted);
    pushStatsToQml();
}

int MainWindowQml::pauseAlarms(int seconds)
{
    if (!m_notifier)
        return 0;

    const int secs = m_notifier->startPause(seconds);

    // 返回 0 表示「没有可暂停的告警」：此时不进入暂停状态，
    // 必须明确告诉用户原因，否则会以为按钮失灵。
    // @note 已处于暂停时再次调用会按新时长重新计时（延长 / 缩短），
    //       这是刻意行为：护理操作超过预期时可以续期，但仍受危急 30 s 上限约束。
    if (secs == 0) {
        appendLog(QStringLiteral("[告警] 暂停未生效：当前没有未确认的告警"), LogInfo);
        pushStatsToQml();
        return 0;
    }

    pushStatsToQml();
    return secs;
}

void MainWindowQml::resumeAlarms()
{
    if (!m_notifier)
        return;

    // cancelPause() 内部会在仍有未处理告警时立刻重新起声（见 AlarmNotifier）。
    m_notifier->cancelPause();
    pushStatsToQml();
}

void MainWindowQml::auditAlarm(const AlarmEvent &a)
{
    AuditLogger::Severity sev = auditSeverityOf(a.level);
    // 解除属于状态变化，降一档严重度，避免审计噪音。
    if (a.state == AlarmEvent::Cleared)
        sev = AuditLogger::Info;

    AuditLogger::instance().log(
        alarmCategoryOf(a.category), sev,
        QStringLiteral("告警%1").arg(alarmStateName(a.state)),
        QStringLiteral("通道=%1 条件=%2 值=%3 阈值=%4 级别=%5 描述=%6")
            .arg(a.channelId)
            // 用 trackKey 而非 conditionKey()：技术报警的键是 "1/2/SQI"，
            // conditionKey() 会把它伪造成 "1/2/HI"，日志里看不出真实来源。
            .arg(a.trackKey)
            .arg(a.value, 0, 'f', 1)
            .arg(a.limit, 0, 'f', 1)
            .arg(static_cast<int>(a.level))
            .arg(a.text));
}

void MainWindowQml::auditConfigDiff(const QVector<ChannelConfig> &oldCfgs,
                                    const QVector<ChannelConfig> &newCfgs)
{
    for (const ChannelConfig &nc : newCfgs) {
        const ChannelConfig *oc = nullptr;
        for (const ChannelConfig &c : oldCfgs) {
            if (c.id == nc.id) { oc = &c; break; }
        }
        if (!oc)
            continue;

        // 逐字段比对，只有真正变化的才写入审计（避免噪声）。
        if (!qFuzzyCompare(oc->alarmLow + 1.0, nc.alarmLow + 1.0))
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("alarmLow"),
                QString::number(oc->alarmLow, 'f', 1),
                QString::number(nc.alarmLow, 'f', 1));

        if (!qFuzzyCompare(oc->alarmHigh + 1.0, nc.alarmHigh + 1.0))
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("alarmHigh"),
                QString::number(oc->alarmHigh, 'f', 1),
                QString::number(nc.alarmHigh, 'f', 1));

        if (!qFuzzyCompare(oc->minValue + 1.0, nc.minValue + 1.0))
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("minValue"),
                QString::number(oc->minValue, 'f', 1),
                QString::number(nc.minValue, 'f', 1));

        if (!qFuzzyCompare(oc->maxValue + 1.0, nc.maxValue + 1.0))
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("maxValue"),
                QString::number(oc->maxValue, 'f', 1),
                QString::number(nc.maxValue, 'f', 1));

        if (oc->visible != nc.visible)
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("visible"),
                oc->visible ? QStringLiteral("显示") : QStringLiteral("隐藏"),
                nc.visible ? QStringLiteral("显示") : QStringLiteral("隐藏"));

        if (oc->color != nc.color)
            AuditLogger::instance().logConfigChange(
                nc.name, QStringLiteral("color"),
                oc->color.name(), nc.color.name());
    }
}

void MainWindowQml::appendLog(const QString &text, int level)
{
    if (m_logModel)
        m_logModel->append(text, level);
}


// ---------------------------------------------------------------------------
// 配置 / 患者类型
// ---------------------------------------------------------------------------

QVector<ChannelConfig> MainWindowQml::parseConfigs(const QVariantList &list,
                                                  const QVector<ChannelConfig> &existing)
{
    QVector<ChannelConfig> out;
    out.reserve(list.size());

    for (int i = 0; i < list.size(); ++i) {
        const QVariantMap m = list.at(i).toMap();

        // 以既有配置为模板：QML 只发它关心的字段，
        // 未出现的字段（延时 / 优先级 / 二级阈值等）保持原值不被清零。
        ChannelConfig cfg = existing.value(i);
        if (m.contains(QStringLiteral("id")))
            cfg.id = m.value(QStringLiteral("id")).toInt();

        if (m.contains(QStringLiteral("name")))
            cfg.name = m.value(QStringLiteral("name")).toString();
        if (m.contains(QStringLiteral("unit")))
            cfg.unit = m.value(QStringLiteral("unit")).toString();
        if (m.contains(QStringLiteral("color"))) {
            const QColor c(m.value(QStringLiteral("color")).toString());
            if (c.isValid())
                cfg.color = c;
        }
        if (m.contains(QStringLiteral("minValue")))
            cfg.minValue = m.value(QStringLiteral("minValue")).toDouble();
        if (m.contains(QStringLiteral("maxValue")))
            cfg.maxValue = m.value(QStringLiteral("maxValue")).toDouble();
        if (m.contains(QStringLiteral("alarmLow")))
            cfg.alarmLow = m.value(QStringLiteral("alarmLow")).toDouble();
        if (m.contains(QStringLiteral("alarmHigh")))
            cfg.alarmHigh = m.value(QStringLiteral("alarmHigh")).toDouble();
        if (m.contains(QStringLiteral("hasLowAlarm")))
            cfg.hasLowAlarm = m.value(QStringLiteral("hasLowAlarm")).toBool();
        if (m.contains(QStringLiteral("hasHighAlarm")))
            cfg.hasHighAlarm = m.value(QStringLiteral("hasHighAlarm")).toBool();
        if (m.contains(QStringLiteral("visible")))
            cfg.visible = m.value(QStringLiteral("visible")).toBool();

        out.append(cfg);
    }
    return out;
}

void MainWindowQml::applyConfigFromQml(const QVariantList &configs)
{
    // 先取旧配置快照，用于生成审计差异。
    const QVector<ChannelConfig> oldCfgs = m_configMgr.channels();
    const QVector<ChannelConfig> newCfgs = parseConfigs(configs, oldCfgs);
    if (newCfgs.isEmpty())
        return;

    m_configMgr.channelsMutable() = newCfgs;

    // 同步到绘图内核、数值卡片与采集线程。
    // 注意：不重建 QML 配置面板本身，否则会用旧值覆盖用户刚做的编辑。
    m_chart->setChannels(newCfgs);
    m_channelModel->resetChannels(newCfgs);
    emit requestUpdateConfigs(newCfgs);

    // 阈值变了，旧阈值下建立的报警跟踪状态已不适用，必须复位，
    // 否则会出现「按新阈值判断正常、却仍挂着旧阈值告警」的不一致。
    m_alarmDetector.reset();
    refreshAlarmUi();

    // 跟踪状态已清空，声音与暂停必须同步停止；否则会出现
    // 「界面上已无任何告警，蜂鸣器却还在响」这种自相矛盾的状态。
    if (m_notifier) {
        if (m_notifier->isPaused())
            m_notifier->cancelPause();
        m_notifier->updateTopLevel(AlarmEvent::Info, /*hasPending=*/false);
    }

    // 审计：逐字段记录变更。
    auditConfigDiff(oldCfgs, newCfgs);

    appendLog(QStringLiteral("[配置] 通道配置已更新并下发"), LogInfo);
    pushStatsToQml();
}

void MainWindowQml::changePatientType(int type)
{
    if (type < static_cast<int>(PatientType::Adult)
        || type > static_cast<int>(PatientType::Custom)) {
        appendLog(QStringLiteral("[配置] 忽略非法患者类型：%1").arg(type),
                  LogWarn);
        return;
    }

    const PatientType before = m_configMgr.patientType();
    const PatientType after  = static_cast<PatientType>(type);

    // 按患者类型重载量程与报警限剖面（避免成人限值监护新生儿）。
    m_configMgr.loadProfile(after, /*overwriteCustom=*/true);
    ChannelConfigManager::setGlobalPatientType(after);

    const QVector<ChannelConfig> cfgs = m_configMgr.channels();

    // 回灌到各视图与采集线程。
    m_chart->setChannels(cfgs);
    m_channelModel->resetChannels(cfgs);
    emit requestUpdateConfigs(cfgs);

    // 报警状态机需要清空：旧阈值下建立的跟踪状态已不适用。
    m_alarmDetector.reset();
    refreshAlarmUi();

    // 声音与暂停随状态机一并复位（患者换人后旧报警不得继续发声）。
    if (m_notifier) {
        if (m_notifier->isPaused())
            m_notifier->cancelPause();
        m_notifier->updateTopLevel(AlarmEvent::Info, /*hasPending=*/false);
    }

    appendLog(QStringLiteral("[配置] 患者类型：%1 → %2，已按预设剖面重载报警限")
                  .arg(patientTypeName(before), patientTypeName(after)),
              LogInfo);

    AuditLogger::instance().log(
        AuditLogger::Config, AuditLogger::Warning,
        QStringLiteral("患者类型切换"),
        QStringLiteral("%1 -> %2").arg(patientTypeName(before),
                                       patientTypeName(after)));

    pushStatsToQml();
}

// ---------------------------------------------------------------------------
// 清空 / 回放 / 导入导出
// ---------------------------------------------------------------------------

void MainWindowQml::clearAll()
{
    m_historyBuffer.clear();
    m_alarmBuffer.clear();
    m_alarmDetector.reset();
    m_pendingFrames.clear();
    m_pendingAlarms.clear();

    m_chart->clearData();
    m_frameCount    = 0;
    m_droppedFrames = 0;

    // 清空后同步复位告警列表与通道边框，避免残留上次的报警状态。
    refreshAlarmUi();

    // 告警已被清空，声音必须立即停止；暂停状态也一并解除，
    // 否则下一次报警会「继承」上一次的抑制状态而不再发声。
    if (m_notifier) {
        if (m_notifier->isPaused())
            m_notifier->cancelPause();
        m_notifier->updateTopLevel(AlarmEvent::Info, /*hasPending=*/false);
    }

    AuditLogger::instance().log(AuditLogger::Data, AuditLogger::Info,
                                QStringLiteral("清空历史数据"),
                                QStringLiteral("清除历史缓冲、告警记录与绘图数据"));

    appendLog(QStringLiteral("[系统] 已清空历史数据与告警记录"), LogInfo);
    pushStatsToQml();
}

void MainWindowQml::importHistory()
{
    // 见文件头部注释：QGuiApplication 下不使用 QFileDialog。
    // 导入目录固定为 <应用目录>/imports，取其中最新修改的历史文件。
    const QString dirPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("imports"));
    QDir dir(dirPath);

    const QStringList filters{QStringLiteral("*.csv"), QStringLiteral("*.vital"),
                              QStringLiteral("*.txt")};
    const QFileInfoList entries =
        dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time);

    if (entries.isEmpty()) {
        appendLog(QStringLiteral("[导入] %1 下未找到历史文件（*.csv / *.vital）")
                      .arg(dirPath),
                  LogWarn);
        return;
    }

    const QString path = entries.first().absoluteFilePath();

    QString err;
    QVector<SampleFrame> frames;
    if (!HistoryRecorder::loadFrames(path, frames, &err)) {
        appendLog(QStringLiteral("[导入] 失败：%1").arg(err), LogError);
        return;
    }

    // 停止实时采集，进入回放模式。
    if (m_running)
        stopAcquisition();

    clearAll();

    m_playbackFrames = frames;
    m_playbackPos    = 0;
    m_playingBack    = true;
    m_playbackTimer->start();

    emit playingBackChanged(true);
    appendLog(QStringLiteral("[回放] 已载入 %1 帧，开始回放").arg(frames.size()),
              LogInfo);
    pushStatsToQml();
}


void MainWindowQml::onPlaybackTick()
{
    if (!m_playingBack || m_playbackPos >= m_playbackFrames.size()) {
        m_playbackTimer->stop();
        m_playingBack = false;
        emit playingBackChanged(false);
        appendLog(QStringLiteral("[回放] 回放结束"), LogInfo);
        return;
    }

    // 每 tick 推送若干帧，模拟实时采集节奏。
    QVector<SampleFrame> burst;
    burst.reserve(kPlaybackBurst);
    for (int i = 0; i < kPlaybackBurst && m_playbackPos < m_playbackFrames.size();
         ++i, ++m_playbackPos) {
        burst.append(m_playbackFrames.at(m_playbackPos));
    }

    m_chart->appendFrames(burst);

    // 回放帧同样要过告警判断，否则回放时报警状态与曲线不一致。
    for (const SampleFrame &f : burst) {
        m_historyBuffer.append(f);
        m_channelModel->applyFrame(f);

        const QVector<AlarmEvent> alarms =
            m_alarmDetector.detect(f, m_configMgr.channels());
        for (const AlarmEvent &a : alarms) {
            m_alarmBuffer.append(a);
            m_pendingAlarms.append(a);
            auditAlarm(a);
        }

        m_lastFrameMs = f.monotonicMs > 0 ? f.monotonicMs : f.timestampMs;
    }

    if (!m_pendingAlarms.isEmpty()) {
        m_chart->appendAlarms(m_pendingAlarms);
        m_pendingAlarms.clear();
    }

    refreshAlarmUi();
    pushStatsToQml();
}

void MainWindowQml::exportChartImage()
{
    // 导出目录固定为 <应用目录>/exports，文件名带时间戳避免覆盖。
    const QString dirPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("exports"));
    QDir().mkpath(dirPath);

    const QString path = QDir(dirPath).filePath(
        QStringLiteral("trend_%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));

    if (m_chart->exportImage(path))
        appendLog(QStringLiteral("[导出] 截图已保存：%1").arg(path), LogInfo);
    else
        appendLog(QStringLiteral("[导出] 失败：无法保存图片到 %1").arg(path),
                  LogError);
}

void MainWindowQml::exportHistoryData()
{
    const QString dirPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("exports"));
    QDir().mkpath(dirPath);

    const QString path = QDir(dirPath).filePath(
        QStringLiteral("vital_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));

    QString err;
    const QVector<SampleFrame> frames = m_historyBuffer.snapshot();
    if (HistoryRecorder::saveFrames(path, frames, &err)) {
        appendLog(QStringLiteral("[导出] 已保存 %1 帧到：%2")
                      .arg(frames.size())
                      .arg(path),
                  LogInfo);
        AuditLogger::instance().log(AuditLogger::Data, AuditLogger::Info,
                                    QStringLiteral("导出历史数据"),
                                    QStringLiteral("%1 帧 -> %2")
                                        .arg(frames.size())
                                        .arg(path));
    } else {
        appendLog(QStringLiteral("[导出] 失败：%1").arg(err), LogError);
    }
}

// ---------------------------------------------------------------------------
// 视图设置
// ---------------------------------------------------------------------------

void MainWindowQml::setVisibleSpan(int spanPoints)
{
    if (spanPoints <= 0)
        return;

    m_chart->setVisibleIndexSpan(static_cast<quint64>(spanPoints));
    emit viewportInfoChanged(m_chart->viewportInfo());
    pushStatsToQml();
}

void MainWindowQml::setFollowLatest(bool on)
{
    m_chart->setFollowLatest(on);
    emit viewportInfoChanged(m_chart->viewportInfo());
    pushStatsToQml();
}

void MainWindowQml::setMultiAxis(bool on)
{
    m_chart->setMultiAxis(on);
}

// ---------------------------------------------------------------------------
// 界面信息登记
// ---------------------------------------------------------------------------

void MainWindowQml::logNotice(const QString &text)
{
    appendLog(text, LogInfo);
}

void MainWindowQml::recordPatientInfo(const QString &summary)
{
    appendLog(QStringLiteral("[患者] 信息更新：%1").arg(summary), LogInfo);

    // 患者上下文是「这条报警属于谁」的唯一依据，必须留痕。
    // 与配置变更同级（Config / Info）：它不是危险操作，但必须可追溯。
    AuditLogger::instance().log(
        AuditLogger::Config, AuditLogger::Info,
        QStringLiteral("患者信息更新"), summary);
}

