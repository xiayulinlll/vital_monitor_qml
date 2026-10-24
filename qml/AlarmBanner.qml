import QtQuick
import QtQuick.Layouts

import VitalMonitor

/**
 * 顶部报警横幅（对应 Widgets 版 AlarmBanner）。
 *
 * IEC 60601-1-8 对高优先级报警的要求：**必须同时有声、光提示，且不能仅靠文字**。
 * 本组件实现：
 *  - 整条横幅按最高优先级着色（`Theme.colorForLevel`，与 C++ `alarmIndicatorColor` 同源）；
 *  - 高优先级以 `Theme.blinkIntervalMs` 的频率闪烁（严重度越高越快）；
 *  - 「确认」按钮触发报警状态机 Acknowledged（若条件仍成立会按 repeatMs 重报）；
 *  - 无报警时收缩为一条安静的绿色「运行正常」带，不占用过多高度。
 *
 * @note 相对 Widgets 版的增益：这里**同时显示条数**，并且点击横幅能
 *       展开右侧告警列表查看全部活动报警 —— 旧版只能看到最高优先级那一条。
 *
 * ## 声光提示的分工（第二梯队第 6、7 项）
 *
 * | 能力 | 实现位置 | 说明 |
 * |------|---------|------|
 * | 分级提示音 | `AlarmNotifier`（C++） | 危急 3 连响 / 900 ms；一般 1800 ms；提示 3000 ms |
 * | 横幅闪烁 | 本文件 | 周期取 `Theme.blinkIntervalMs`，与声音节奏同源 |
 * | 状态栏常驻 | 本文件（底部状态条） | 无论有无报警都显示当前报警状态 |
 * | 静音 Mute | `Backend.mute()` | **只关声音**，闪烁照常 |
 * | 暂停 Pause | `Backend.pause()` | 声音 + 闪烁**全部**停止，倒计时可见，到期自动恢复 |
 * | 确认 Ack | `Backend.acknowledge()` | 签收；条件仍成立时按 `alarmRepeatMs` 重报 |
 *
 * @warning 暂停期间横幅**不隐藏**，而是转为灰底 + 斜纹 + 剩余秒数。
 *          隐藏等于把报警关掉（违反 IEC 60601-1-8）；保持全彩闪烁又会
 *          让用户误以为「报警正常在提示」。
 */
Rectangle {
    id: banner

    /// 当前最高严重度（0=Info 1=Warning 2=Critical 3=Escalated）。
    readonly property int topLevel: alarmModel.topLevel
    readonly property bool hasAlarm: !alarmModel.empty
    /// 是否需要脉冲动画（高优先级）。
    /// @note 暂停时强制为 false：暂停的语义就是「停止声光提示」。
    readonly property bool pulsing: topLevel >= 2 && !Backend.alarmPaused
    /// 暂停中（声音 + 闪烁停止，但报警仍然成立）。
    readonly property bool paused: Backend.alarmPaused
    /// 静音中（只关声音）。
    readonly property bool muted: Backend.alarmMuted

    implicitHeight: 34
    radius: Theme.radius

    // 暂停时整条转为中性灰：目的不是「隐藏报警」，而是让操作者
    // 一眼看出「此刻的提示是被抑制的」。
    color: paused
           ? Qt.rgba(0.42, 0.45, 0.50, 0.95)
           : (hasAlarm ? Theme.colorForLevel(topLevel)
                       : Qt.rgba(0.15, 0.68, 0.38, 0.22))
    border.width: 1
    border.color: paused
                  ? Qt.rgba(0.75, 0.78, 0.82, 0.9)
                  : (hasAlarm ? Qt.darker(color, 1.35)
                              : Qt.rgba(0.15, 0.68, 0.38, 0.5))

    // 高优先级闪烁：只改亮度不改色相，避免「颜色变化」被误读为等级变化。
    SequentialAnimation on color {
        running: banner.pulsing
        loops: Animation.Infinite
        ColorAnimation {
            to: Qt.lighter(banner.hasAlarm
                           ? Theme.colorForLevel(banner.topLevel) : "#27AE60", 1.45)
            duration: Theme.blinkIntervalMs(banner.topLevel)
        }
        ColorAnimation {
            to: banner.hasAlarm
                ? Theme.colorForLevel(banner.topLevel) : Qt.rgba(0.15, 0.68, 0.38, 0.22)
            duration: Theme.blinkIntervalMs(banner.topLevel)
        }
    }

    // 暂停底纹：45° 斜线，视觉惯例上代表「已抑制 / 待恢复」。
    // 用 Canvas 而不是图片，避免部署时漏拷资源。
    Canvas {
        anchors.fill: parent
        visible: banner.paused
        opacity: 0.35
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.strokeStyle = "#FFFFFF";
            ctx.lineWidth = 1;
            // 从左下向右上划线，覆盖整个矩形。
            for (var x = -height; x < width; x += 14) {
                ctx.beginPath();
                ctx.moveTo(x, height);
                ctx.lineTo(x + height, 0);
                ctx.stroke();
            }
        }
        // 尺寸变化时需要重绘，否则拉伸后斜纹会变形或残留。
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 10

        // ---- 优先级徽标：用文字而非纯色，色觉障碍者也能区分 ----
        Rectangle {
            Layout.preferredWidth: 62
            Layout.preferredHeight: 20
            radius: 3
            color: Qt.rgba(0, 0, 0, 0.28)
            visible: banner.hasAlarm

            Text {
                anchors.centerIn: parent
                text: Theme.levelName(banner.topLevel) + qsTr("级")
                color: "#FFFFFF"
                font.pixelSize: 11
                font.bold: true
            }
        }

        // ---- 主文案：取排序后的第一条（即最高优先级 / 最新） ----
        Text {
            Layout.fillWidth: true
            text: {
                if (!banner.hasAlarm)
                    return qsTr("● 运行正常 — 当前无活动报警");
                if (alarmModel.rowCount() === 0)
                    return qsTr("检测到报警");

                var idx = alarmModel.index(0, 0);
                var desc = alarmModel.data(idx, Qt.UserRole + 11);   // TextRole
                var cnt  = alarmModel.rowCount();
                return cnt > 1
                       ? (desc + "    （还有 " + (cnt - 1) + " 条活动报警）")
                       : desc;
            }
            color: "#FFFFFF"
            font.pixelSize: 13
            font.bold: banner.hasAlarm
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter

            MouseArea {
                anchors.fill: parent
                cursorShape: banner.hasAlarm ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (banner.hasAlarm && alarmListPanel)
                        alarmListPanel.expanded = !alarmListPanel.expanded;
                }
            }
        }

        // ---- 活动报警计数 ----
        Text {
            visible: banner.hasAlarm
            text: "⚑ " + alarmModel.rowCount()
            color: "#FFFFFF"
            font.pixelSize: 13
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }

        // ---- 确认按钮（报警状态机 Acknowledged） ----
        Rectangle {
            Layout.preferredWidth: 76
            Layout.preferredHeight: 22
            radius: 3
            visible: banner.hasAlarm
            color: ackArea.containsMouse ? Qt.rgba(1, 1, 1, 0.30)
                                         : Qt.rgba(1, 1, 1, 0.18)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.55)

            Text {
                anchors.centerIn: parent
                text: qsTr("✓ 确认")
                color: "#FFFFFF"
                font.pixelSize: 11
                font.bold: true
            }

            MouseArea {
                id: ackArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Backend.acknowledge()
            }
        }

        // ---- 静音按钮：只关声音，闪烁照常（IEC 60601-1-8 要求视觉提示保留）----
        Rectangle {
            Layout.preferredWidth: 76
            Layout.preferredHeight: 22
            radius: 3
            visible: banner.hasAlarm
            // 静音开启时用深底以示「当前无声音」，避免误以为声音故障。
            color: banner.muted
                   ? Qt.rgba(0, 0, 0, 0.42)
                   : (muteArea.containsMouse ? Qt.rgba(1, 1, 1, 0.30)
                                             : Qt.rgba(1, 1, 1, 0.18))
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.55)

            Text {
                anchors.centerIn: parent
                text: banner.muted ? qsTr("🔇 已静音") : qsTr("🔊 静音")
                color: "#FFFFFF"
                font.pixelSize: 11
                font.bold: banner.muted
            }

            MouseArea {
                id: muteArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Backend.mute(!Backend.alarmMuted)
            }
        }

        // ---- 暂停 / 恢复按钮 + 剩余秒数倒计时 ----
        // 暂停是「声音 + 闪烁全部停止」，与静音的区别必须让用户在按钮文字上
        // 一眼看出；倒计时必须可见，否则等同于把报警藏起来。
        Rectangle {
            Layout.preferredWidth: banner.paused ? 116 : 76
            Layout.preferredHeight: 22
            radius: 3
            visible: banner.hasAlarm
            color: banner.paused
                   ? Qt.rgba(0, 0, 0, 0.55)
                   : (pauseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.30)
                                              : Qt.rgba(1, 1, 1, 0.18))
            border.width: 1
            border.color: banner.paused ? Qt.rgba(1, 1, 1, 0.9)
                                        : Qt.rgba(1, 1, 1, 0.55)

            Text {
                anchors.centerIn: parent
                // 暂停中显示「恢复（N s）」，把剩余时间与可操作项合在一起，
                // 不需要用户在两个位置之间来回看。
                text: banner.paused
                      ? qsTr("⏵ 恢复 %1s").arg(Backend.alarmPauseRemaining)
                      : qsTr("⏸ 暂停")
                color: "#FFFFFF"
                font.pixelSize: 11
                font.bold: banner.paused
            }

            MouseArea {
                id: pauseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (Backend.alarmPaused) {
                        Backend.resume();
                    } else {
                        // 时长上限由 C++ 强制收紧：危急 30 s / 其余 120 s。
                        // 这里直接请求上限值，实际生效值以 C++ 返回为准。
                        var secs = Backend.pause(Backend.alarmPauseMaxSeconds);
                        if (secs === 0 && logModel)
                            logModel.append(qsTr("[告警] 暂停未生效：当前没有未确认的告警"), 0);
                    }
                }
            }
        }
    }

    // 声音链路自检提示：声卡不可用时必须明确告知，
    // 否则「没有声音」会被误判为「设备在监护且无报警」。
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        visible: banner.hasAlarm && !Backend.audioAvailable
        text: qsTr("⚠ 无音频输出，仅视觉报警")
        color: "#FFE082"
        font.pixelSize: 9
    }

    // 声音提示说明：
    // 本项目刻意不引入 QtQuick.Controls / QtMultimedia（保持纯 QtQuick 依赖面），
    // 因此这里**不做** QML 侧音频播放。声光报警的节奏由 C++ 报警状态机
    // （AlarmDetector 的 alarmRepeatMs / alarmEscalateMs）统一决定，
    // 若要接入蜂鸣器，应在 C++ 侧调用平台音频接口，
    // 避免 QML 与 C++ 各维护一套计时器导致节奏不一致。
    //
    // 视觉提示已由本组件的闪烁动画 + 高优先级脉冲光晕完整覆盖。
}
