import QtQuick

import VitalMonitor

/**
 * 单通道实时数值卡片（对应 Widgets 版 VitalValuePanel 中的一块面板）。
 *
 * 三重状态语义与 C++ 版**完全一致**（`VitalValuePanel::updateValues`）：
 *  1. `!valid`（导联脱落 / 测量失败）→ 显示 `--`，曲线色变灰；
 *  2. `valid && quality < 0.35`（信号差但可读）→ 显示数值 + SQI 警示条；
 *  3. `value` 越限 → 数值转为报警色并加粗。
 *
 * @note 本组件**专用于 Repeater / ListView 委托**（全工程仅 Main.qml 一处使用），
 *       因此下列角色属性一律声明为 `required property`，由 QML 引擎按
 *       名字从模型角色自动注入，无需在委托处手工绑定。
 *
 * @warning 为什么必须用 `required property` 而不是 `property string name: ""`
 *          再在委托里写 `name: name`：
 *          委托继承自 QQuickItem，`valid` / `quality` / `alarmed` 等属性名
 *          与卡片自身属性同名时，`valid: valid` 右侧会解析为**卡片自己的属性**
 *          （而非模型角色），成为自引用 no-op 绑定 —— 不报错、不报警告，
 *          值恒为默认值。实测症状：四个通道数值全部显示 `--`
 *          （因为 `hasValue` 恒 false，`valid` 恒 false）。
 *          `required property` 会在加载期由模型角色赋值并**遮蔽**同名内建属性，
 *          从根本上消除这个歧义。
 *
 * @warning 角色名 `channelVisible` 不能写成 `visible`：
 *          QQuickItem::visible 是 FINAL 属性，声明 `required property visible`
 *          会直接报 "Cannot override FINAL property" 导致组件无法创建。
 *          故 ChannelModel::roleNames() 侧也相应改名为 channelVisible。
 */
Rectangle {
    id: card

    // ---- Repeater / ListView 注入的行角色 ----
    // @warning 属性名**必须与 ChannelModel::roleNames() 逐字相同**，因为
    //          `required property` 是按名字与模型角色配对的：
    //          名字对不上会在运行时报 "Required property X was not initialized"
    //          并导致**整个委托创建失败**（列表空白 / 数值栏空白）。
    //          故这里直接采用角色原名：name / minValue / maxValue / value。
    //          （早期版本用 channelName/rangeMin/rangeMax/sampleValue 另起别名，
    //            正是委托静默创建失败、四路数值全为 -- 的直接原因之一。）
    // @warning 不可退化为带默认值的 `property`：那样与 required 重复定义。
    required property int    channelId
    required property string name
    required property string unit
    required property color  curveColor
    required property double minValue
    required property double maxValue
    required property double alarmLow
    required property double alarmHigh
    required property bool   hasLowAlarm
    required property bool   hasHighAlarm
    /// 该通道曲线是否显示（对应模型角色 channelVisible，非 QQuickItem::visible）。
    required property bool   channelVisible
    required property double value
    required property double quality
    required property bool   valid
    required property bool   hasValue
    required property bool   alarmed
    required property string alarmBorder
    /// 原始值超出量程（不裁剪，用 ↑ / ↓ 角标提示）。
    required property bool   outOfRange
    /// 本帧样本被判定为伪迹（用橙色 ? 角标提示）。
    required property bool   artifact

    /// 告警边框色：`alarmBorder` 为空表示当前无告警。
    readonly property bool hasAlarm: alarmBorder !== ""
    /// 信号质量差但数值仍可读（与 ChannelModel 的门控阈值一致）。
    readonly property bool poorQuality: valid && quality < Theme.lowQualityThreshold

    radius: Theme.radius
    color: Theme.panelBg
    border.width: hasAlarm ? 2 : 1
    border.color: hasAlarm ? alarmBorder : Theme.border

    // 高优先级告警时整卡脉冲，形成「余光可及」的视觉提示。
    SequentialAnimation on opacity {
        running: hasAlarm && !Backend.playingBack
        loops: Animation.Infinite
        // 闪烁频率随严重度提高而加快（与 Theme.blinkIntervalMs 同源）。
        PauseAnimation { duration: 700 }
        NumberAnimation { to: 0.55; duration: 120; easing.type: Easing.OutQuad }
        NumberAnimation { to: 1.0;  duration: 120; easing.type: Easing.InQuad }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.cardPad
        spacing: 2

        // ---- 通道名：左侧色条用于和曲线颜色对应 ----
        Row {
            width: parent.width
            spacing: 4

            Rectangle {
                width: 3
                height: nameText.height
                radius: 1.5
                // 隐藏通道的色条减淡，提示「曲线已关闭」。
                color: card.channelVisible ? card.curveColor
                                    : Qt.rgba(card.curveColor.r,
                                              card.curveColor.g,
                                              card.curveColor.b, 0.3)
            }

            Text {
                id: nameText
                text: card.name
                color: card.channelVisible ? Theme.textDim : Theme.textFaint
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // ---- 主数值 ----
        Row {
            width: parent.width
            spacing: 3

            Text {
                id: valueText
                // 未收到任何帧 → 尚未有值；invalid → 测量失败。
                text: {
                    if (!card.hasValue)
                        return "--";
                    if (!card.valid)
                        return "--";
                    // 整数通道（心率 / 呼吸）不显示多余小数位，与 Widgets 版一致。
                    var t = card.value.toFixed(card.unit === "bpm" || card.unit === "rpm" ? 0 : 1);
                    // 不裁剪数值，但必须让使用者知道这个读数已超出量程、精度不可保证。
                    // 用符号而非颜色提示：色觉障碍者同样能读到「越界」这一事实。
                    if (card.outOfRange)
                        t += card.value > card.maxValue ? "\u2191" : "\u2193";
                    // 伪迹：测到了但物理上不可能，与「越界」（真实高值）区分开。
                    if (card.artifact)
                        t += "?";
                    return t;
                }
                color: {
                    if (!card.hasValue || !card.valid)
                        return Theme.textFaint;
                    if (card.artifact)
                        return Theme.warn;          // 伪迹：橙色，区别于真实越限的红色
                    if (card.alarmed)
                        return card.hasAlarm ? card.alarmBorder : Theme.danger;
                    if (card.poorQuality)
                        return Theme.warn;
                    return card.curveColor;
                }
                font.pixelSize: 26
                font.bold: card.alarmed
                font.family: "Consolas"
            }

            Text {
                anchors.bottom: valueText.bottom
                anchors.bottomMargin: 4
                text: card.unit
                color: Theme.textFaint
                font.pixelSize: 11
            }
        }

        // ---- 信号质量：仅在质量下降时出现，正常时完全隐藏（避免视觉噪音）----
        Rectangle {
            width: parent.width
            height: 3
            radius: 1.5
            visible: card.hasValue && card.quality < 0.999
            color: Theme.panelAlt

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, card.quality))
                height: parent.height
                radius: parent.radius
                color: card.quality < Theme.lowQualityThreshold ? Theme.danger
                                                                : Theme.ok
            }
        }

        // ---- 阈值提示：让医护一眼看到当前量的限值 ----
        Text {
            width: parent.width
            text: {
                var lo = card.hasLowAlarm ? card.alarmLow.toFixed(0) : "--";
                var hi = card.hasHighAlarm ? card.alarmHigh.toFixed(0) : "--";
                return lo + " ~ " + hi;
            }
            color: Theme.textFaint
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        // 导联脱落 / 信号不可信的显式提示（不能只靠 `--` 让人猜）。
        Text {
            width: parent.width
            visible: card.hasValue && !card.valid
            text: qsTr("信号不可信")
            color: Theme.danger
            font.pixelSize: 10
        }
    }

    MouseArea {
        anchors.fill: parent
        // 点击卡片：高亮该通道曲线；双击则切换该通道的显示开关。
        // @note 通过根窗口的属性桥而不是直接引用 `chartPanel.id`：
        //       本组件由 Repeater 在 ColumnLayout 中创建，
        //       `chartPanel` 不在其作用域内，直接引用会报 Unqualified access。
        onClicked: {
            var win = card.Window.window;
            if (win && win.highlightChannel !== undefined)
                win.highlightChannel = card.channelId;
        }
        onDoubleClicked: {
            var win = card.Window.window;
            if (win && win.toggleChannel)
                win.toggleChannel(card.channelId);
        }
    }
}
