import QtQuick
import QtQuick.Layouts

import VitalMonitor

/**
 * 活动报警列表（QML 相对 Widgets 版的**主要增益点**）。
 *
 * Widgets 版把告警只做成一条横幅（`AlarmBanner`），同一时刻只能看到
 * 最高优先级的那一条；而 IEC 60601-1-8 明确要求操作者能
 * **同时看到全部活动报警及其优先级**。
 *
 * 本组件用一份 `ListView` + 委托实现（委托见 `AlarmListDelegate.qml`），并做到：
 *  - 排序交给 C++ 的 `AlarmModel`（严重度降序 → 采样序号降序），
 *    QML 不做任何排序 —— 排序逻辑只有一处，不会与 C++ 漂移；
 *  - 高优先级行带脉冲光晕，低优先级静态显示；
 *  - 技术报警（导联脱落等）用蓝色徽标区分生理报警，
 *    与 `ChannelModel::applyAlarmState` 的边框配色完全一致。
 */
Rectangle {
    id: panel

    property bool expanded: true

    color: Theme.panelBg
    radius: Theme.radius
    border.width: 1
    border.color: alarmModel.empty ? Theme.border
                                   : Theme.colorForLevel(alarmModel.topLevel)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        // ---- 标题栏 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Rectangle {
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: alarmModel.empty ? Theme.ok
                                        : Theme.colorForLevel(alarmModel.topLevel)
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("活动报警")
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Text {
                text: alarmModel.rowCount()
                color: alarmModel.empty ? Theme.textFaint
                                        : Theme.colorForLevel(alarmModel.topLevel)
                font.pixelSize: 13
                font.bold: true
            }
        }

        // ---- 无报警占位：明确告知「空」而不是留给用户猜 ----
        Text {
            Layout.fillWidth: true
            visible: alarmModel.empty
            text: qsTr("当前无活动报警")
            color: Theme.textFaint
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }

        // ---- 报警列表 ----
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !alarmModel.empty
            clip: true
            spacing: 4
            model: alarmModel
            boundsBehavior: Flickable.StopAtBounds
            delegate: AlarmListDelegate {}
        }
    }
}
