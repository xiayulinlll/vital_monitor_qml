import QtQuick
import QtQuick.Layouts

import VitalMonitor

/**
 * 单条活动报警的委托（`AlarmListPanel` 的 ListView delegate）。
 *
 * 抽出独立文件的原因：委托含 4 行信息（等级 / 分类 / 状态 / 数值阈值），
 * 内联会让主文件难以阅读；同时独立后便于在别处（例如报警详情弹窗）复用。
 *
 * @note 使用 `required property` 声明角色，QML 引擎会在加载期校验角色名，
 *       拼错不会静默变成 undefined。这是 Qt 6 相比 Qt 5 的实质改进。
 */
Rectangle {
    id: row

    // ---- AlarmModel 的角色（见 models/AlarmModel.h）----
    // @warning 名字**必须与 roleNames() 逐字一致**：required property 是按
    //          名字与模型角色配对的，写错（例如角色叫 value 这里写 sampleValue）
    //          会在运行时报 "Required property sampleValue was not initialized"
    //          并导致**委托整体创建失败**，列表直接空白。
    // @note 角色名 `value` 可以安全地声明为 required property：经 Qt 6.2.4
    //       实测，QQuickItem 上唯一不能重声明的是 FINAL 属性 `visible`
    //       （会报 "Cannot override FINAL property"），`value` 并不在其中。
    required property int    channelId
    required property int    level
    required property color  levelColor
    required property string levelName
    required property int    category
    required property int    state
    required property string stateName
    required property bool   highLimit
    required property double value
    required property double limit
    required property string text
    required property string timeText
    required property bool   pulsing
    required property bool   isTechnical
    required property string trackKey
    required property int    durationMs
    required property string durationText

    /// 语义别名：下文一律用 sampleValue 指代模型角色 value，
    /// 使「这是当前采样值」的意图比裸 `value` 更清晰。
    readonly property double sampleValue: value

    radius: 4
    color: Theme.panelAlt
    border.width: 1
    border.color: Qt.rgba(levelColor.r, levelColor.g, levelColor.b, 0.55)
    implicitHeight: Math.max(42, content.implicitHeight + 12)

    // 高优先级脉冲光晕：余光即可察觉，无需一直盯着屏幕。
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        radius: row.radius + 1
        color: "transparent"
        border.width: 2
        border.color: row.levelColor
        opacity: 0
        visible: row.pulsing
        z: -1

        SequentialAnimation on opacity {
            running: row.pulsing
            loops: Animation.Infinite
            NumberAnimation { to: 0.75; duration: 260 }
            NumberAnimation { to: 0.0;  duration: 620 }
        }
    }

    // 左侧严重度色条。
    Rectangle {
        width: 4
        height: parent.height - 8
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: row.levelColor
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 8
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            // 等级徽标：文字 + 颜色双重编码，色觉障碍者亦可辨。
            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 15
                radius: 2
                color: row.levelColor

                Text {
                    anchors.centerIn: parent
                    text: Theme.levelName(row.level)
                    color: "#FFFFFF"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            // 生理 / 技术分类徽标（技术报警是独立通道，必须能一眼区分）。
            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 15
                radius: 2
                color: row.isTechnical ? Theme.alarmTechnical
                                       : Qt.rgba(1, 1, 1, 0.10)
                border.width: row.isTechnical ? 0 : 1
                border.color: Theme.border

                Text {
                    anchors.centerIn: parent
                    text: row.isTechnical ? qsTr("技术") : qsTr("生理")
                    color: row.isTechnical ? "#FFFFFF" : Theme.textDim
                    font.pixelSize: 10
                }
            }

            // 生命周期状态：已确认的告警减淡显示，表示「已处理但未恢复」。
            Text {
                text: row.stateName
                color: row.state === 1 ? Theme.textFaint : Theme.warn
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }

            // 已持续时长：医护判断「恶化速度」的关键信息，
            // 同时也是可用性评价（响应是否及时）的客观依据。
            Text {
                text: row.durationText
                color: row.durationMs < 0 ? Theme.textFaint : Theme.textDim
                font.pixelSize: 10
            }

            Text {
                text: row.timeText
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }

        Text {
            Layout.fillWidth: true
            text: row.text
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        // 数值与阈值必须同时给出：只说「超限」而不给量，无法判断紧急程度。
        Text {
            Layout.fillWidth: true
            text: qsTr("通道 %1 · 阈值 %2 %3 · 实测 %4")
                  .arg(row.channelId)
                  .arg(row.highLimit ? ">" : "<")
                  .arg(row.limit.toFixed(1))
                  .arg(row.sampleValue.toFixed(1))
            color: Theme.textFaint
            font.pixelSize: 10
        }
    }
}
