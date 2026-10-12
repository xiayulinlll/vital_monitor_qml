#include "MainWindow.h"

#include "chart/TrendChartWidget.h"
#include "ui/VitalValuePanel.h"
#include "ui/ChannelConfigPanel.h"
#include "ui/AlarmBanner.h"
#include "core/HistoryRecorder.h"
#include "core/CoordinateMapper.h"
#include "core/AuditLogger.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupThreading();

    // 报警延时改用毫秒制（第二梯队），10 Hz 采样下 1000 ms ≈ 原 10 帧去抖。
    m_alarmDetector.setDefaultSampleIntervalMs(100);

    // ---- 审计日志：记录本次监护会话的启动（第一梯队第 3 项）----
    AuditLogger &audit = AuditLogger::instance();
    const QString auditDir = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("audit"));
    const QString auditPath = QDir(auditDir).filePath(
        QStringLiteral("audit_%1.log")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))));
    if (audit.openFile(auditPath)) {
        audit.log(AuditLogger::System, AuditLogger::Info,
                  QStringLiteral("会话启动"),
                  QStringLiteral("通道数=%1，患者类型=%2，采样间隔=100ms")
                      .arg(m_configMgr.count())
                      .arg(patientTypeName(m_configMgr.patientType())));
        appendLog(QStringLiteral("[审计] 日志已启用：%1").arg(auditPath));
    } else {
        appendLog(QStringLiteral("[审计] 日志文件不可写，事件将仅输出到控制台"));
    }

    // 同步进程级患者类型真源，保证各控件显示一致。
    ChannelConfigManager::setGlobalPatientType(m_configMgr.patientType());

    appendLog(QStringLiteral("[系统] 多通道生理信号趋势监测上位机已启动"));
    appendLog(QStringLiteral("[系统] 通道数：%1，历史缓冲：%2 帧")
                  .arg(m_configMgr.count())
                  .arg(m_historyBuffer.capacity()));
    updateStatusBar();
}

MainWindow::~MainWindow()
{
    // 先请求停止，再退出线程，最后释放模拟器。
    if (m_workerThread) {
        emit requestStop();
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }

    AuditLogger::instance().log(AuditLogger::System, AuditLogger::Info,
                                QStringLiteral("会话结束"),
                                QStringLiteral("累计采集 %1 帧，丢帧 %2 帧，告警 %3 条")
                                    .arg(m_frameCount)
                                    .arg(m_droppedFrames)
                                    .arg(m_alarmBuffer.size()));
    AuditLogger::instance().close();
}

void MainWindow::setupThreading()
{
    m_workerThread = new QThread(this);
    m_simulator = new VitalSignSimulator();      // 无 parent，随后 moveToThread
    m_simulator->moveToThread(m_workerThread);

    // ---- 跨线程信号槽（QueuedConnection 自动生效） ----
    connect(this, &MainWindow::requestStart, m_simulator, &VitalSignSimulator::start);
    connect(this, &MainWindow::requestStop, m_simulator, &VitalSignSimulator::stop);
    connect(this, &MainWindow::requestUpdateConfigs,
            m_simulator, &VitalSignSimulator::updateConfigs);
    connect(m_simulator, &VitalSignSimulator::frameReady,
            this, &MainWindow::onFrameReady);       // 工作线程 -> 主线程
    connect(m_simulator, &VitalSignSimulator::statusText,
            this, &MainWindow::onStatusText);

    connect(m_workerThread, &QThread::finished,
            m_simulator, &QObject::deleteLater);

    // 线程退出时清理定时器依赖的线程亲和性。
    m_workerThread->start();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("多通道生理信号趋势监测上位机 — Vital Monitor"));
    resize(1360, 820);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(8, 8, 8, 8);
    centralLayout->setSpacing(6);

    // ================= 顶部：报警横幅（第一梯队第 1 项：声光报警） =================
    m_alarmBanner = new AlarmBanner(central);
    centralLayout->addWidget(m_alarmBanner);

    auto *rootLayout = new QHBoxLayout();
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);
    centralLayout->addLayout(rootLayout, 1);

    // ================= 左侧：绘图 + 数值面板 =================
    auto *leftSplitter = new QSplitter(Qt::Horizontal, central);

    // --- 中间绘图区 ---
    auto *chartHost = new QWidget(leftSplitter);
    auto *chartLayout = new QVBoxLayout(chartHost);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(6);

    // 工具栏：开始/停止、缩放、跟随、清空、导入、导出
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(6);

    m_startStopBtn = new QPushButton(QStringLiteral("▶ 开始采集"), chartHost);
    m_startStopBtn->setFixedWidth(110);
    toolbar->addWidget(m_startStopBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("清空"), chartHost);
    toolbar->addWidget(clearBtn);

    toolbar->addSpacing(12);
    toolbar->addWidget(new QLabel(QStringLiteral("视口跨度:"), chartHost));
    auto *spanCombo = new QComboBox(chartHost);
    spanCombo->addItem(QStringLiteral("10 秒 (100 点)"), 100);
    spanCombo->addItem(QStringLiteral("30 秒 (300 点)"), 300);
    spanCombo->addItem(QStringLiteral("60 秒 (600 点)"), 600);
    spanCombo->addItem(QStringLiteral("3 分钟 (1800 点)"), 1800);
    spanCombo->addItem(QStringLiteral("10 分钟 (6000 点)"), 6000);
    spanCombo->setCurrentIndex(2);
    toolbar->addWidget(spanCombo);

    auto *followBtn = new QPushButton(QStringLiteral("实时跟随: 开"), chartHost);
    followBtn->setCheckable(true);
    followBtn->setChecked(true);
    toolbar->addWidget(followBtn);

    toolbar->addStretch(1);

    auto *importBtn = new QPushButton(QStringLiteral("导入历史"), chartHost);
    auto *exportImgBtn = new QPushButton(QStringLiteral("导出截图"), chartHost);
    auto *exportDataBtn = new QPushButton(QStringLiteral("导出数据"), chartHost);
    toolbar->addWidget(importBtn);
    toolbar->addWidget(exportImgBtn);
    toolbar->addWidget(exportDataBtn);

    chartLayout->addLayout(toolbar);

    // 绘图控件
    m_chart = new TrendChartWidget(chartHost);
    m_chart->setChannels(m_configMgr.channels());
    chartLayout->addWidget(m_chart, 1);

    // 状态/视口提示
    m_viewportLabel = new QLabel(chartHost);
    m_viewportLabel->setStyleSheet(QStringLiteral("color:#8A97A8;"));
    chartLayout->addWidget(m_viewportLabel);

    // --- 右侧：实时数值面板 ---
    m_valuePanel = new VitalValuePanel(leftSplitter);
    // 实测数值面板原占 230 ~ 320px，砍掉约 1/3 宽度后收紧为 150 ~ 210px，
    // 把省下的水平空间让给中间趋势绘图区。
    m_valuePanel->setMinimumWidth(150);
    m_valuePanel->setMaximumWidth(210);
    m_valuePanel->buildPanels(m_configMgr.channels());

    leftSplitter->addWidget(chartHost);
    leftSplitter->addWidget(m_valuePanel);
    // 注意：setStretchFactor 会让 splitter 在每次 resize 时按比例重新分配空间，
    // 从而覆盖 setMaximumWidth 的限制。因此数值面板必须用 stretchFactor=0
    // （不参与拉伸），否则宽度砍掉 1/3 的设置会被重新撑回去。
    leftSplitter->setStretchFactor(0, 1);
    leftSplitter->setStretchFactor(1, 0);
    // 初始尺寸：左绘图区尽量宽，数值面板锁定在收窄后的宽度。
    leftSplitter->setSizes(QList<int>{960, 190});

    rootLayout->addWidget(leftSplitter, 4);

    // ================= 右侧栏：配置 + 日志 =================
    auto *rightSplitter = new QSplitter(Qt::Vertical);

    m_configPanel = new ChannelConfigPanel(rightSplitter);
    m_configPanel->setMinimumWidth(430);
    m_configPanel->setChannels(m_configMgr.channels());

    auto *logHost = new QWidget(rightSplitter);
    auto *logLayout = new QVBoxLayout(logHost);
    logLayout->setContentsMargins(6, 6, 6, 6);
    logLayout->setSpacing(6);
    auto *logTitle = new QLabel(QStringLiteral("状态 / 告警日志"), logHost);
    QFont lf = logTitle->font();
    lf.setBold(true);
    logTitle->setFont(lf);
    logLayout->addWidget(logTitle);
    m_logView = new QPlainTextEdit(logHost);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(QStringLiteral("background:#12161D; color:#A8B4C4;"));
    logLayout->addWidget(m_logView, 1);

    rightSplitter->addWidget(m_configPanel);
    rightSplitter->addWidget(logHost);
    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 2);

    rootLayout->addWidget(rightSplitter, 2);

    setCentralWidget(central);

    // 状态栏
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);

    // ================= 信号连接 =================
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::onImportClicked);
    connect(exportImgBtn, &QPushButton::clicked, this, &MainWindow::onExportImageClicked);
    connect(exportDataBtn, &QPushButton::clicked, this, &MainWindow::onExportDataClicked);
    connect(followBtn, &QPushButton::toggled, this, &MainWindow::onFollowToggled);
    connect(spanCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, spanCombo](int idx) {
                onSpanChanged(spanCombo->itemData(idx).toInt());
            });
    connect(m_configPanel, &ChannelConfigPanel::configApplied,
            this, &MainWindow::onConfigApplied);
    connect(m_configPanel, &ChannelConfigPanel::patientTypeChangeRequested,
            this, &MainWindow::onPatientTypeChanged);
    connect(m_alarmBanner, &AlarmBanner::acknowledgeRequested,
            this, &MainWindow::onAcknowledgeAlarms);
    connect(m_chart, &TrendChartWidget::viewportChanged, this, [this](quint64, quint64) {
        updateStatusBar();
    });

    // 回放定时器
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setInterval(50);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    updateStatusBar();
}


// ---------------------------------------------------------------------------
// 核心业务槽函数
// ---------------------------------------------------------------------------

void MainWindow::onStartStopClicked()
{
    if (!m_running) {
        emit requestStart(100);   // 10 Hz 采样
        m_running = true;
        m_startStopBtn->setText(QStringLiteral("■ 停止采集"));
        m_uptime.start();
        appendLog(QStringLiteral("[采集] 开始采集（10 Hz）"));

        AuditLogger::instance().log(AuditLogger::System, AuditLogger::Info,
                                    QStringLiteral("开始采集"),
                                    QStringLiteral("采样间隔 100 ms（10 Hz）"));
    } else {
        emit requestStop();
        m_running = false;
        m_startStopBtn->setText(QStringLiteral("▶ 开始采集"));
        appendLog(QStringLiteral("[采集] 停止采集，累计 %1 帧").arg(m_frameCount));

        AuditLogger::instance().log(AuditLogger::System, AuditLogger::Info,
                                    QStringLiteral("停止采集"),
                                    QStringLiteral("累计 %1 帧，丢帧 %2 帧")
                                        .arg(m_frameCount)
                                        .arg(m_droppedFrames));
    }
    updateStatusBar();
}

void MainWindow::onFrameReady(const SampleFrame &frame)
{
    // 本函数运行在主线程（跨线程信号槽已排队），可安全访问 UI。
    ++m_frameCount;

    // 记录最新帧时间戳：后续「确认告警」等操作需与 detect() 使用同一时基。
    m_lastFrameMs = frame.timestampMs;

    // 1) 写入历史环形缓冲（内存恒定，难点 2）。
    m_historyBuffer.append(frame);

    // 1.1) 数据完整性统计：累计丢帧用于状态栏展示（第二梯队第 8 项）。
    if (frame.droppedFrames > 0)
        m_droppedFrames += frame.droppedFrames;

    // 2) 告警判断（报警状态机 + SQI 门控 + 技术报警）。
    const QVector<AlarmEvent> alarms = m_alarmDetector.detect(frame, m_configMgr.channels());
    for (const AlarmEvent &a : alarms) {
        m_alarmBuffer.append(a);
        m_pendingAlarms.append(a);

        // 事件文本按状态前缀区分，便于日志阅读。
        appendLog(QStringLiteral("[告警] %1").arg(a.text));
        auditAlarm(a);
    }

    // 3) 交给绘图控件（局部重绘）与数值面板。
    m_pendingFrames.append(frame);

    // 批量提交，减少重绘次数（每 2 帧合并一次，降低 CPU）。
    if (m_pendingFrames.size() >= 2) {
        m_chart->appendFrames(m_pendingFrames);
        if (!m_pendingAlarms.isEmpty())
            m_chart->appendAlarms(m_pendingAlarms);
        m_pendingFrames.clear();
        m_pendingAlarms.clear();
    }

    m_valuePanel->updateValues(frame);

    // 4) 同步报警条与通道卡片边框（声光 + 视觉联动）。
    if (!alarms.isEmpty() || m_alarmDetector.activeCount() > 0 || m_alarmBanner->hasAlarm()) {
        if (!m_alarmBanner->hasAlarm() && m_alarmDetector.activeCount() == 0)
            refreshAlarmUi();
        else if (!alarms.isEmpty())
            refreshAlarmUi();
    }

    if (m_frameCount % 10 == 0)
        updateStatusBar();
}

void MainWindow::onStatusText(const QString &text)
{
    appendLog(QStringLiteral("[采集线程] %1").arg(text));
}

// ---------------------------------------------------------------------------
// 报警 / 审计（第一梯队第 1、3 项；第二梯队第 5、7、9 项）
// ---------------------------------------------------------------------------

namespace {
/// 把告警类别映射到审计日志类别。
AuditLogger::Category alarmCategoryOf(AlarmEvent::Category c)
{
    return c == AlarmEvent::Technical ? AuditLogger::TechAlarm : AuditLogger::Alarm;
}
} // namespace

void MainWindow::refreshAlarmUi()
{
    const QVector<AlarmEvent> active = m_alarmDetector.activeAlarms();

    if (m_alarmBanner)
        m_alarmBanner->setActiveAlarms(active);

    // 数值面板卡片边框按告警严重度着色，形成全局视觉报警。
    if (m_valuePanel)
        m_valuePanel->applyAlarmState(active);
}

void MainWindow::onAcknowledgeAlarms()
{
    // 确认全部活动告警：状态机进入 Acknowledged，
    // 声音提示停止，但若条件仍成立将按 repeatMs 重新提示（不会永久静默）。
    //
    // 显式传入最近一帧的采集时间戳作为时基，保证与 detect() 完全一致；
    // 若尚未收到任何帧（lastFrameMs 泄漏 = 0），回退到系统当前时间。
    const QVector<AlarmEvent> acked = m_alarmDetector.acknowledge(QString(), m_lastFrameMs);
    if (acked.isEmpty()) {
        appendLog(QStringLiteral("[告警] 确认操作：当前无活动告警"));
        return;
    }

    appendLog(QStringLiteral("[告警] 用户确认 %1 条告警").arg(acked.size()));
    for (const AlarmEvent &a : acked) {
        AuditLogger::instance().log(
            alarmCategoryOf(a.category), AuditLogger::Info,
            QStringLiteral("告警确认"),
            QStringLiteral("通道=%1 条件=%2 描述=%3")
                .arg(a.channelId).arg(a.conditionKey(), a.text));
    }

    refreshAlarmUi();
    updateStatusBar();
}

void MainWindow::onPatientTypeChanged(PatientType type)
{
    const PatientType before = m_configMgr.patientType();

    // 按患者类型重载量程与报警限剖面（避免成人限值监护新生儿）。
    m_configMgr.loadProfile(type, /*overwriteCustom=*/true);
    ChannelConfigManager::setGlobalPatientType(type);

    const QVector<ChannelConfig> cfgs = m_configMgr.channels();

    // 回灌到各视图与采集线程。
    m_chart->setChannels(cfgs);
    m_valuePanel->buildPanels(cfgs);
    emit requestUpdateConfigs(cfgs);

    // 面板同步（会阻塞信号，避免递归触发）。
    m_configPanel->setChannels(cfgs);

    // 报警状态机需要清空：旧阈值下建立的跟踪状态已不适用。
    m_alarmDetector.reset();
    refreshAlarmUi();

    appendLog(QStringLiteral("[配置] 患者类型：%1 → %2，已按预设剖面重载报警限")
                  .arg(patientTypeName(before), patientTypeName(type)));

    AuditLogger::instance().log(
        AuditLogger::Config, AuditLogger::Warning,
        QStringLiteral("患者类型切换"),
        QStringLiteral("%1 -> %2").arg(patientTypeName(before), patientTypeName(type)));

    updateStatusBar();
}

void MainWindow::auditAlarm(const AlarmEvent &a)
{
    using Sev = AuditLogger::Severity;

    // 严重度映射：升级 > 严重 > 一般告警 > 提示。
    Sev sev = Sev::Info;
    switch (a.level) {
    case AlarmEvent::Escalated: sev = Sev::Critical; break;
    case AlarmEvent::Critical:  sev = Sev::Critical; break;
    case AlarmEvent::Warning:   sev = Sev::Warning;  break;
    case AlarmEvent::Info:      sev = Sev::Info;     break;
    }

    // 解除 / 确认属于状态变化，降一档严重度，避免审计噪音。
    QString stateName;
    switch (a.state) {
    case AlarmEvent::Active:        stateName = QStringLiteral("激活"); break;
    case AlarmEvent::Acknowledged:  stateName = QStringLiteral("已确认"); break;
    case AlarmEvent::Cleared:       stateName = QStringLiteral("解除"); sev = Sev::Info; break;
    case AlarmEvent::LatchedSilent: stateName = QStringLiteral("静音重报"); break;
    }

    AuditLogger::instance().log(
        alarmCategoryOf(a.category), sev,
        QStringLiteral("告警%1").arg(stateName),
        QStringLiteral("通道=%1 条件=%2 值=%3 阈值=%4 级别=%5 描述=%6")
            .arg(a.channelId)
            .arg(a.conditionKey())
            .arg(a.value, 0, 'f', 1)
            .arg(a.limit, 0, 'f', 1)
            .arg(static_cast<int>(a.level))
            .arg(a.text));
}

void MainWindow::auditConfigDiff(const QVector<ChannelConfig> &oldCfgs,
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

void MainWindow::onConfigApplied(const QVector<ChannelConfig> &channels)
{
    // 先取旧配置快照，用于生成审计差异（第一梯队第 3 项）。
    const QVector<ChannelConfig> oldCfgs = m_configMgr.channels();

    m_configMgr.channelsMutable() = channels;

    // 同步到绘图控件、数值面板、采集线程。
    // 注意：不要调用 m_configPanel->setChannels()，否则会重建表格、
    // 用 m_configMgr 中的旧值覆盖用户刚勾选的可见性，导致取消勾选后曲线仍显示。
    m_chart->setChannels(channels);
    m_valuePanel->buildPanels(channels);
    emit requestUpdateConfigs(channels);

    // 审计：逐字段记录变更。
    auditConfigDiff(oldCfgs, channels);

    appendLog(QStringLiteral("[配置] 通道配置已更新并下发"));
    updateStatusBar();
}

void MainWindow::onClearClicked()
{
    m_historyBuffer.clear();
    m_alarmBuffer.clear();
    m_alarmDetector.reset();
    m_pendingFrames.clear();
    m_pendingAlarms.clear();
    m_chart->clearData();
    m_frameCount = 0;
    m_droppedFrames = 0;

    // 清空后同步复位报警条与通道边框，避免残留上次的报警状态。
    refreshAlarmUi();

    AuditLogger::instance().log(AuditLogger::Data, AuditLogger::Info,
                                QStringLiteral("清空历史数据"),
                                QStringLiteral("清除历史缓冲、告警记录与绘图数据"));

    appendLog(QStringLiteral("[系统] 已清空历史数据与告警记录"));
    updateStatusBar();
}

void MainWindow::onFollowToggled(bool on)
{
    m_chart->setFollowLatest(on);
    updateStatusBar();
}

void MainWindow::onSpanChanged(int value)
{
    if (value > 0) {
        m_chart->setVisibleIndexSpan(static_cast<quint64>(value));
        updateStatusBar();
    }
}

void MainWindow::onImportClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入历史数据"), QString(),
        QStringLiteral("监护历史数据 (*.csv *.vital);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QString err;
    QVector<SampleFrame> frames;
    if (!HistoryRecorder::loadFrames(path, frames, &err)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), err);
        appendLog(QStringLiteral("[导入] 失败：%1").arg(err));
        return;
    }

    // 停止实时采集，进入回放模式（流程 3）。
    if (m_running)
        onStartStopClicked();

    onClearClicked();
    m_playbackFrames = frames;
    m_playbackPos = 0;
    m_playingBack = true;
    m_playbackTimer->start();

    appendLog(QStringLiteral("[回放] 已载入 %1 帧，开始回放").arg(frames.size()));
}

void MainWindow::onPlaybackTick()
{
    if (!m_playingBack || m_playbackPos >= m_playbackFrames.size()) {
        m_playbackTimer->stop();
        m_playingBack = false;
        appendLog(QStringLiteral("[回放] 回放结束"));
        return;
    }

    // 每 tick 推送若干帧，模拟实时采集节奏。
    constexpr int kBurst = 3;
    QVector<SampleFrame> burst;
    for (int i = 0; i < kBurst && m_playbackPos < m_playbackFrames.size(); ++i, ++m_playbackPos)
        burst.append(m_playbackFrames.at(m_playbackPos));

    m_chart->appendFrames(burst);
    for (const SampleFrame &f : burst) {
        m_historyBuffer.append(f);
        m_valuePanel->updateValues(f);
    }
    updateStatusBar();
}

void MainWindow::onExportImageClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出趋势截图"),
        QStringLiteral("trend_%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (path.isEmpty())
        return;

    if (m_chart->exportImage(path)) {
        appendLog(QStringLiteral("[导出] 截图已保存：%1").arg(path));
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法保存图片到：%1").arg(path));
    }
}

void MainWindow::onExportDataClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出历史数据"),
        QStringLiteral("vital_%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("监护历史数据 (*.csv)"));
    if (path.isEmpty())
        return;

    QString err;
    const QVector<SampleFrame> frames = m_historyBuffer.snapshot();
    if (HistoryRecorder::saveFrames(path, frames, &err)) {
        appendLog(QStringLiteral("[导出] 已保存 %1 帧到：%2").arg(frames.size()).arg(path));
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), err);
    }
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

void MainWindow::appendLog(const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_logView->appendPlainText(QStringLiteral("%1  %2").arg(ts, text));

    // 限制日志长度，避免长时间运行内存增长。
    constexpr int kMaxLogBlocks = 500;
    if (m_logView->document()->blockCount() > kMaxLogBlocks)
        m_logView->document()->setMaximumBlockCount(kMaxLogBlocks);
}

void MainWindow::updateStatusBar()
{
    const QString state = m_running ? QStringLiteral("采集运行中")
                                    : (m_playingBack ? QStringLiteral("历史回放中")
                                                     : QStringLiteral("已停止"));
    const qint64 secs = m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0;

    m_statusLabel->setText(
        QStringLiteral("状态：%1   |   已采集 %2 帧   |   丢帧 %3   |   历史缓冲 %4 帧   |   活动告警 %5 条   |   累计告警 %6 条   |   运行 %7 秒")
            .arg(state)
            .arg(m_frameCount)
            .arg(m_droppedFrames)
            .arg(m_historyBuffer.size())
            .arg(m_alarmDetector.activeCount())
            .arg(m_alarmBuffer.size())
            .arg(secs));

    if (m_viewportLabel)
        m_viewportLabel->setText(m_chart->viewportInfo());
}

