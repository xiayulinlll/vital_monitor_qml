pragma Singleton
import QtQuick 2.15

/**
 * 全局视觉主题（QML 版单一真源）。
 *
 * 目的与 C++ 版 TrendChartWidget 中的 kBgColor / kGridColor 等常量一致：
 * 深色医疗配色集中定义一处，任何页面不得硬编码色值，
 * 避免「同一告警在不同控件显示不同颜色」。
 *
 * @note 使用方式：import ".." 后直接引用 Theme.bg；
 *       需要 import QtQuick 与 singletons 注册（见 CMakeLists qt_add_qml_module）。
 */
QtObject {
    // ---- 画布 ----
    readonly property color bg:        "#141820"
    readonly property color plotBg:    "#0D1016"
    readonly property color panelBg:   "#1B2029"
    readonly property color panelAlt:  "#161B23"
    readonly property color gridMinor: "#2A3240"
    readonly property color gridMajor: "#3A4658"
    readonly property color axis:      "#6B7A90"
    readonly property color text:      "#B8C4D4"
    readonly property color textDim:   "#8A97A8"
    readonly property color textFaint: "#6E7A8A"
    readonly property color border:    "#2A3240"

    // ---- 报警指示色（IEC 60601-1-8，与 C++ alarmIndicatorColor() 完全一致）----
    /// 低优先级（提示）：青
    readonly property color alarmInfo:     "#2A9EB0"
    /// 中优先级（一般告警）：琥珀
    readonly property color alarmWarning:  "#D69A09"
    /// 高优先级 / 升级（危及生命）：红
    readonly property color alarmCritical: "#D32F2F"
    /// 技术报警（传感器异常 / 导联脱落）：蓝，与生理报警区分
    readonly property color alarmTechnical: "#2A6E8A"

    // ---- 状态语义色 ----
    readonly property color ok:      "#27AE60"
    readonly property color warn:    "#E0A030"
    readonly property color danger:  "#FF5252"
    readonly property color accent:  "#F39C12"

    // ---- 尺寸 ----
    readonly property int radius:   6
    readonly property int gap:      6
    readonly property int margin:   8
    readonly property int cardPad:  8

    /// 信号质量低于该阈值即视为「不可信」（与 AlarmDetector 的门控阈值一致）。
    readonly property real lowQualityThreshold: 0.35

    /**
     * @brief 取报警等级对应的指示色。
     * @param level AlarmEvent::Level 的整数值（供 QML 直接沿用 C++ 枚举）
     */
    function colorForLevel(level) {
        switch (level) {
        case 3:                   // Escalated
        case 2: return alarmCritical;
        case 1: return alarmWarning;
        default: return alarmInfo;
        }
    }

    /// 报警等级中文名（与 AlarmBanner::levelName 保持一致）。
    function levelName(level) {
        switch (level) {
        case 3: return "升级";
        case 2: return "严重";
        case 1: return "一般";
        default: return "提示";
        }
    }

    /// 闪烁周期：严重度越高闪得越快（毫秒）。
    function blinkIntervalMs(level) {
        switch (level) {
        case 3:
        case 2: return 300;
        case 1: return 550;
        default: return 900;
        }
    }

    /// 提示音节奏：严重度越高越急促（毫秒）。
    function soundIntervalMs(level) {
        switch (level) {
        case 3:
        case 2: return 900;
        case 1: return 1800;
        default: return 3000;
        }
    }
}
