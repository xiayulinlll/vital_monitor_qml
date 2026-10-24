import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// @note 刻意**不** import VitalMonitor：本文件就是该模块的 Main 组件，
//       自引用会让引擎在解析期循环查找模块而报
//       "Failed to import VitalMonitor"。同目录的 Theme / Backend 等
//       组件由同目录隐式解析（qmldir 的 prefer 前缀生效）。
//
// @note 使用 `Window` 而不是 `ApplicationWindow`：后者属于
//       QtQuick.Controls，本项目全部控件自绘、不引入 Controls
//       （保持「纯 QtQuick + QPainter」的依赖面）。`Window` 在
//       QtQuick.Window 中，是 QtQuick 自带的顶层窗口类型。

/**
 * 应用主视图（对应 Widgets 版 MainWindow::setupUi 的布局）。
 *
 * 布局对照：
 * ```
 *  ┌──────────────────────── 报警横幅 (AlarmBanner) ────────────────────────┐
 *  ├──────────────┬──────────────────────────┬──────────────────────────────┤
 *  │ 通道数值卡片  │        趋势曲线          │        活动告警列表          │
 *  │ (Repeater)   │      (ChartPanel)        │     (AlarmListPanel)         │
 *  ├──────────────┴──────────────────────────┴──────────────────────────────┤
 *  │ 通道配置 (ChannelConfigPanel，可折叠)                                   │
 *  ├────────────────────────────────────────────────────────────────────────┤
 *  │ 状态栏 (Backend::statusLine)              │ 日志 (LogPanel, 可折叠)     │
 *  └────────────────────────────────────────────────────────────────────────┘
 * ```
 *
 * @note 左侧数值面板用 Repeater + ColumnLayout 而非 ListView：
 *       通道数固定为 4 且不做滚动/懒加载，Repeater 更省内存与层级。
 *       右侧告警列表则必须用 ListView —— 告警条数不定，需虚拟化。
 */
Window {
    id: root

    width: 1360
    height: 820
    visible: true
    title: qsTr("多通道生理信号趋势监测上位机 — Vital Monitor (QML)")
    color: Theme.bg

    /// 数值卡片 → 曲线面板的属性桥。
    /// 卡片在 Repeater 里创建，无法直接引用 ChartPanel 的 id，
    /// 因此把这两个接口提升到根窗口，卡片通过 Window.window 访问。
    property int highlightChannel: -1
    function toggleChannel(chId) {
        if (chartPanel)
            chartPanel.toggleChannel(chId);
    }

    // `Window` 没有 default property，因此所有子项都必须挂在一个根 Item 下。
    Item {
        id: content
        anchors.fill: parent

        // 把 Backend 单例交回 C++，建立状态推送通道。
        // @note Qt 6.2 的 QQmlEngine::singletonInstance() 只认 C++ 注册的
        //       类型 id，查不到文件式 QML 单例，所以由 QML 主动回传。
        Component.onCompleted: {
            Backend.attachToCpp(controller);
            controller.attachBackend(Backend);
        }

        // ---- 顶部患者信息栏：床号 / 患者ID / 姓名 + 常驻「演示模式·模拟数据」标识 ----
        // 优化文档 9.1 第 4、5 项：报警必须能回答「这是谁的数据」，
        // 模拟数据必须一眼可辨，二者都要求常驻可见。
        PatientBar {
            id: patientBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.margin
            // 类型下拉在配置面板里，这里只跟随显示（单向绑定，避免双真源）。
            patientType: configPanel.patientTypeIndex
            onPatientInfoChanged: (summary) => Backend.recordPatientInfo(summary)
        }

        // ---- 顶部报警横幅：IEC 60601-1-8 声光提示 ----
        AlarmBanner {
            id: banner
            anchors.top: patientBar.bottom
            anchors.topMargin: Theme.gap
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.margin
        }

    // ---- 主体：左（数值 + 曲线） / 右（告警列表） ----
    RowLayout {
        id: body
        anchors.top: banner.bottom
        anchors.topMargin: Theme.gap
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.margin
        anchors.bottom: configPanel.top
        anchors.bottomMargin: Theme.gap
        spacing: Theme.gap

        // ================= 左：数值卡片列 =================
        ColumnLayout {
            // 与 Widgets 版一致：数值面板收窄到 ~170px，
            // 把省下的水平空间全部让给趋势绘图区。
            Layout.preferredWidth: 172
            Layout.minimumWidth: 150
            Layout.maximumWidth: 210
            Layout.fillHeight: true
            spacing: Theme.gap

            Repeater {
                // channelModel 是 C++ 侧 ChannelModel，注册为上下文属性。
                model: channelModel

                ChannelValueCard {
                    Layout.fillWidth: true
                    // 卡片高度按通道数均分剩余空间，窗口越大单卡越高。
                    Layout.fillHeight: true

                    // ------------------------------------------------------------
                    // @note 这里**不需要**任何角色 → 属性映射。
                    //
                    // ChannelValueCard 已把全部行角色声明为 `required property`，
                    // 且属性名与 ChannelModel::roleNames() 逐字一致
                    // （name / minValue / maxValue / value / channelVisible ...）。
                    // QML 引擎在实例化委托时会按名字自动注入模型角色值，
                    // 因此这里留空即可。
                    //
                    // @warning 切勿写成 `valid: valid` / `quality: quality` 这类
                    //          「同名赋值」：委托继承自 QQuickItem，右侧会解析到
                    //          卡片自身的属性，形成自引用 no-op 绑定 ——
                    //          不报错、不报警告，值恒为默认值，
                    //          症状就是四个通道数值全部显示 `--`。
                    // @warning 也切勿写成 `channelName: name` 这种别名映射：
                    //          卡片已无 channelName 属性（为避免与角色名不一致），
                    //          且 Repeater 委托上不存在 `model` 上下文对象，
                    //          `model.name` 会直接报 `ReferenceError: model is not defined`。
                    //
                    // @warning 角色 `visible` 已改名 `channelVisible`：
                    //          QQuickItem::visible 是 FINAL 属性，委托中
                    //          `required property bool visible` 会直接报
                    //          "Cannot override FINAL property"（实测于 Qt 6.2.4）。
                    //          故 ChannelModel::roleNames() 同步改名为 channelVisible。
                }
            }
        }

        // ================= 中：趋势曲线 =================
        ChartPanel {
            id: chartPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // ---- 通道配置面板（底部，可折叠） ----
    ChannelConfigPanel {
        id: configPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.margins: Theme.margin
        anchors.bottomMargin: Theme.gap
    }

    // ---- 状态栏 ----
    Rectangle {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 26
        color: Theme.panelAlt

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.margin
            anchors.rightMargin: Theme.margin
            spacing: Theme.gap

            Text {
                Layout.fillWidth: true
                text: Backend.statusLine()
                color: Theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            // ---- 报警状态常驻指示（第二梯队第 6 项）----
            // IEC 60601-1-8 要求「报警系统状态」始终可见：即使横幅被暂停、
            // 被静音，操作者也必须能从状态栏一眼看出当前处于哪种抑制状态。
            Rectangle {
                Layout.preferredWidth: alarmStateText.implicitWidth + 14
                Layout.preferredHeight: 18
                radius: 3
                color: Backend.alarmPaused
                       ? Qt.rgba(0.42, 0.45, 0.50, 0.9)
                       : (alarmModel.empty
                          ? Qt.rgba(0.15, 0.68, 0.38, 0.20)
                          : Theme.colorForLevel(alarmModel.topLevel))
                border.width: 1
                border.color: Backend.alarmPaused
                              ? Qt.rgba(0.75, 0.78, 0.82, 0.9)
                              : Qt.rgba(1, 1, 1, 0.18)

                Text {
                    id: alarmStateText
                    anchors.centerIn: parent
                    font.pixelSize: 11
                    font.bold: !alarmModel.empty
                    color: alarmModel.empty && !Backend.alarmPaused
                           ? Theme.text : "#FFFFFF"
                    // 状态优先级：暂停 > 静音 > 无报警 > 报警等级，
                    // 因为「暂停」比「有报警」更需要被意识到。
                    text: {
                        if (Backend.alarmPaused)
                            return qsTr("报警已暂停 %1s").arg(Backend.alarmPauseRemaining);
                        if (alarmModel.empty)
                            return qsTr("无报警");
                        var lv = Theme.levelName(alarmModel.topLevel);
                        if (Backend.alarmMuted)
                            return qsTr("%1报警 · 已静音").arg(lv);
                        if (!Backend.audioAvailable)
                            return qsTr("%1报警 · 无音频").arg(lv);
                        return qsTr("%1报警 %2 条").arg(lv).arg(alarmModel.rowCount());
                    }
                }
            }

            // 视口提示（缩放 / 平移后由 ChartBridge 提供）。
            Text {
                text: Backend.viewportInfo
                color: Theme.textFaint
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }

            // 日志面板开关：默认收起，避免占用曲线高度。
            Text {
                text: logPanel.expanded ? qsTr("▾ 隐藏日志") : qsTr("▴ 显示日志")
                color: logToggleArea.containsMouse ? Theme.accent : Theme.text
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter

                MouseArea {
                    id: logToggleArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: logPanel.expanded = !logPanel.expanded
                }
            }
        }
    }

    // ---- 日志面板：悬浮在主体之上，不挤压曲线布局 ----
    LogPanel {
        id: logPanel
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.margins: Theme.margin
        anchors.bottomMargin: Theme.gap
        width: Math.min(560, parent.width * 0.45)
        height: 220
        expanded: false
    }

    // 全局快捷键：空格开始/停止采集（监护仪常见操作习惯）。
        Shortcut {
            sequence: "Space"
            onActivated: Backend.toggleRunning()
        }
        Shortcut {
            sequence: "Ctrl+A"
            onActivated: Backend.acknowledge()
        }
        // 静音 / 暂停的键盘入口：监护现场常常戴手套操作，
        // 能用单键完成比精确点中 76px 按钮更可靠。
        Shortcut {
            sequence: "M"
            onActivated: Backend.mute(!Backend.alarmMuted)
        }
        Shortcut {
            sequence: "P"
            onActivated: Backend.togglePause()
        }
    }
}
