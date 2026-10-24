pragma Singleton
import QtQuick 2.15

/**
 * 仪表盘后端（QML 版「业务与数据管理层宿主」）。
 *
 * 职责与 C++ 版 MainWindow 完全对应（见 vital_monitor/src/ui/MainWindow.cpp）：
 *  - 持有 ChannelConfigManager，作为通道配置的**单一真源**；
 *  - 管理采集线程（QThread + VitalSignSimulator），跨线程信号槽自动排队；
 *  - 每帧执行 AlarmDetector::detect()，把告警写入环形缓冲与审计日志；
 *  - 通过 *Changed 信号把不可绑定的业务状态（日志、状态栏、报警集合）推给 QML。
 *
 * @note 数据通路（原始采样点）刻意**不经过本类**，而是由 C++ 的
 *       ChartBridge 直接投递给 QQuickPaintedItem，避免每帧 10 次
 *       QVariant/QVariantList 往返（见 docs/QML迁移说明.md 第 3 节）。
 */
QtObject {
    id: backend

    // =====================================================================
    // 对外只读状态（QML 直接绑定，无需手动刷新）
    // =====================================================================

    /// 采集运行中。
    property bool running: false
    /// 历史回放中。
    property bool playingBack: false
    /// 已采集帧数。
    property int frameCount: 0
    /// 累计丢帧数（数据完整性统计）。
    property int droppedFrames: 0
    /// 历史环形缓冲中的帧数。
    property int historySize: 0
    /// 当前活动告警条数。
    property int activeAlarmCount: 0
    /// 累计告警条数（本次会话）。
    property int totalAlarmCount: 0
    /// 会话运行秒数。
    property int uptimeSec: 0
    /// 首次采集前的占位时间（避免告警确认时基为 0）。
    property double lastFrameMs: 0

    // =====================================================================
    // 报警声光提示状态（第二梯队第 6、7 项）
    //
    // @note 这些值由 C++ 的 AlarmNotifier 计算后 3 Hz 推送。
    //       QML **不得**自己维护暂停倒计时：那会与 C++ 的
    //       alarmRepeatMs / alarmEscalateMs 形成两套计时真源而必然漂移。
    // =====================================================================

    /// 是否静音（只关声音，视觉提示保留）。
    property bool alarmMuted: false
    /// 是否处于暂停（声音 + 闪烁全部停止）。
    property bool alarmPaused: false
    /// 暂停剩余秒数（未暂停为 0）。
    property int alarmPauseRemaining: 0
    /// 当前允许的最长暂停秒数（危急 30 s / 其余 120 s）。
    property int alarmPauseMaxSeconds: 120
    /// 声卡 / 音频后端是否可用；false 时界面须提示「无声音输出」。
    property bool audioAvailable: false
    /// 策略上要求发声的累计次数（用于自检声音链路是否被触发过）。
    property int alarmBeepCount: 0
    /// 当前最高「待提示」等级（活动且未确认）。
    property int highestPendingLevel: 0

    // =====================================================================
    // QML 视图需要的通道模型 / 数据模型（由 C++ 侧填充并发出 Changed）
    // =====================================================================

    /// QVariantList<QAbstractItemModel*>，每个元素是一个 ChannelModel，
    /// 供 Repeater / ListView 直接作为 model 使用。
    property var channelModels: []
    /// 当前活动告警的 QVariantList<QVariantMap>（由 AlarmModel 导出）。
    property var activeAlarms: []
    /// 状态 / 告警日志（QStringList）。
    property var logLines: []
    /// 视口提示文本（由 ChartBridge::viewportInfo 提供）。
    property string viewportInfo: ""

    // =====================================================================
    // 信号：把「不可绑定」的业务事件推给 QML
    // =====================================================================

    /// 曲线数据或视口发生变化 → 图表需要重绘。
    signal chartDirty()
    /// 报警状态变化（用于触发通知 / 声音）。
    signal alarmStateChanged()
    /// 请求在曲线上叠加告警标记（chartIndex 为「曲线序号」而非采样序号）。
    signal alarmMarked(int chartIndex, double value, int level)
    /// 回放进度（0.0 ~ 1.0）。
    signal playbackProgressChanged(double progress)

    // =====================================================================
    // C++ 侧注入的回调（由 MainWindowQml 在构造时赋值）
    // =====================================================================

    /// 启动采集：function(intervalMs)
    property var startAcquisition: null
    /// 停止采集：function()
    property var stopAcquisition: null
    /// 应用通道配置：function(configsMap)
    property var applyConfig: null
    /// 切换患者类型：function(typeInt)
    property var changePatientType: null
    /// 确认（签收）全部活动告警：function()
    property var acknowledgeAll: null
    /// 切换静音：function(bool)
    property var setAlarmMutedFn: null
    /// 暂停报警提示：function(seconds) → 返回实际生效秒数
    property var pauseAlarmFn: null
    /// 结束暂停并恢复提示：function()
    property var resumeAlarmFn: null
    /// 清空历史 / 告警 / 曲线：function()
    property var clearAll: null
    /// 导入历史文件并回放：function()
    property var importHistory: null
    /// 导出趋势截图：function()
    property var exportImage: null
    /// 导出历史数据 CSV：function()
    property var exportData: null
    /// 设置视口跨度（采样点数）：function(span)
    property var setVisibleSpan: null
    /// 设置是否跟随最新数据：function(on)
    property var setFollowLatest: null
    /// 每通道独立 Y 轴开关：function(on)
    property var setMultiAxis: null
    /// 记一条提示日志：function(text)
    property var logNotice: null
    /// 患者信息变更（写日志 + 写审计）：function(summary)
    property var recordPatientInfo: null

    // =====================================================================
    // 启动装配：把 C++ 宿主（controller）的动作接到回调属性上
    // =====================================================================

    /**
     * @brief 由 Main.qml 在 Component.onCompleted 中调用一次。
     *
     * 为什么不在 Backend.qml 里直接引用 `controller`：
     *   `controller` 是 main.cpp 注入的上下文属性，单例作用域里
     *   访问上下文属性虽然可行，但会形成「单例依赖上下文」的隐式耦合，
     *   在 qmllint / qmlcachegen 下会报 Unqualified access。
     *   因此由 Main.qml 显式传入，依赖关系变成显式的、可静态检查的。
     *
     * @param host C++ 的 MainWindowQml 实例（含全部 public slots）
     */
    function attachToCpp(host) {
        if (!host)
            return;

        // 采集控制
        startAcquisition   = function (ms)      { host.startAcquisition(ms); };
        stopAcquisition    = function ()        { host.stopAcquisition(); };
        // 配置 / 患者类型
        applyConfig        = function (configs) { host.applyConfigFromQml(configs); };
        changePatientType  = function (t)       { host.changePatientType(t); };
        // 告警
        acknowledgeAll     = function ()        { host.acknowledgeAllAlarms(); };
        setAlarmMutedFn    = function (on)      { host.setAlarmMuted(on); };
        pauseAlarmFn       = function (secs)    { return host.pauseAlarms(secs); };
        resumeAlarmFn      = function ()        { host.resumeAlarms(); };
        // 数据操作
        clearAll           = function ()        { host.clearAll(); };
        importHistory      = function ()        { host.importHistory(); };
        exportImage        = function ()        { host.exportChartImage(); };
        exportData         = function ()        { host.exportHistoryData(); };
        // 视图设置
        setVisibleSpan     = function (span)    { host.setVisibleSpan(span); };
        setFollowLatest    = function (on)      { host.setFollowLatest(on); };
        setMultiAxis       = function (on)      { host.setMultiAxis(on); };
        // 界面信息登记（患者上下文 / 演示模式声明）
        logNotice          = function (text)    { host.logNotice(text); };
        recordPatientInfo  = function (s)       { host.recordPatientInfo(s); };

        // 接通后立刻把 C++ 的当前状态拉过来，避免首屏显示全 0。
        syncFromHost(host);
    }

    /// 从 C++ 宿主读一次真实状态（attach 时调用一次即可；
    /// 之后由 C++ 侧 3 Hz 主动 push，见 MainWindowQml::pushStatsToQml）。
    function syncFromHost(host) {
        running      = host.running;
        playingBack  = host.playingBack;
        frameCount   = host.frameCount;
        droppedFrames= host.droppedFrames;
        historySize  = host.historySize;
        activeAlarmCount = host.activeAlarmCount;
        totalAlarmCount  = host.totalAlarmCount;
        uptimeSec    = host.uptimeSec;
        lastFrameMs  = host.lastFrameMs;

        // 声光提示状态（首屏即显示真实按钮状态，避免「静音了但按钮没亮」）。
        alarmMuted           = host.alarmMuted;
        alarmPaused          = host.alarmPaused;
        alarmPauseRemaining  = host.alarmPauseRemaining;
        alarmPauseMaxSeconds = host.alarmPauseMaxSeconds;
        audioAvailable       = host.audioAvailable;
        alarmBeepCount       = host.alarmBeepCount;
        highestPendingLevel  = host.highestPendingLevel;
    }

    // =====================================================================
    // QML → C++ 的动作封装
    // =====================================================================
    // @note Qt 6.2 的 QML 对 `typeof fn === "function"` 的取值需要
    //       JS 包装，这里统一用 `if (x)` 判空后调用，避免 null 调用异常。

    function start(intervalMs) {
        if (startAcquisition)
            startAcquisition(intervalMs === undefined ? 100 : intervalMs);
    }

    function stop() {
        if (stopAcquisition)
            stopAcquisition();
    }

    function toggleRunning() {
        if (running)
            stop();
        else
            start(100);
    }

    function acknowledge() {
        if (acknowledgeAll)
            acknowledgeAll();
    }

    /// 切换静音：只关声音，横幅仍闪烁（IEC 60601-1-8：不得只有声音或只有画面）。
    function mute(on) {
        if (setAlarmMutedFn)
            setAlarmMutedFn(on !== false);
    }

    /// 暂停报警提示（声音 + 闪烁）。
    /// @return 实际生效秒数；0 表示当前没有可暂停的告警。
    /// @note 时长上限由 C++ 强制收紧（危急 30 s / 其余 120 s），
    ///       QML 传再大的值也不会突破 —— 界面不可成为绕过规范的入口。
    function pause(seconds) {
        var secs = (seconds === undefined) ? alarmPauseMaxSeconds : seconds;
        return pauseAlarmFn ? pauseAlarmFn(secs) : 0;
    }

    /// 结束暂停并恢复提示。
    function resume() {
        if (resumeAlarmFn)
            resumeAlarmFn();
    }

    /// 暂停 / 恢复切换（按钮常用）。
    function togglePause() {
        if (alarmPaused)
            resume();
        else
            pause();
    }

    function clear() {
        if (clearAll)
            clearAll();
    }

    function importFile() {
        if (importHistory)
            importHistory();
    }

    function exportPng() {
        if (exportImage)
            exportImage();
    }

    function exportCsv() {
        if (exportData)
            exportData();
    }

    function span(spanPoints) {
        if (setVisibleSpan)
            setVisibleSpan(spanPoints);
    }

    function followLatest(on) {
        if (setFollowLatest)
            setFollowLatest(on);
    }

    function multiAxis(on) {
        if (setMultiAxis)
            setMultiAxis(on);
    }

    /// 提交配置面板的编辑结果。
    /// @param configs JS 数组，每项 {id,name,unit,color,minValue,maxValue,
    ///        alarmLow,alarmHigh,hasLowAlarm,hasHighAlarm,visible}
    function commitConfig(configs) {
        if (applyConfig)
            applyConfig(configs);
    }

    /// 切换患者类型（0 成人 / 1 小儿 / 2 新生儿 / 3 自定义）。
    function setPatientType(type) {
        if (changePatientType)
            changePatientType(type);
    }

    /// 复位视口到最新（等价 C++ TrendChartWidget::resetViewToLatest）。
    function resetView() {
        followLatest(true);
    }

    // =====================================================================
    // 状态文本（供状态栏 / 标题栏显示，等价 MainWindow::updateStatusBar）
    // =====================================================================

    function stateText() {
        if (running)
            return "采集运行中";
        if (playingBack)
            return "历史回放中";
        return "已停止";
    }

    function statusLine() {
        return "状态：" + stateText()
                + "   |   已采集 " + frameCount + " 帧"
                + "   |   丢帧 " + droppedFrames
                + "   |   历史缓冲 " + historySize + " 帧"
                + "   |   活动告警 " + activeAlarmCount + " 条"
                + "   |   累计告警 " + totalAlarmCount + " 条"
                + "   |   运行 " + uptimeSec + " 秒";
    }
}
