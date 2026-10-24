import QtQuick
import QtQuick.Layouts

/**
 * 状态 / 告警日志面板（对应 Widgets 版 MainWindow 里的 QPlainTextEdit）。
 *
 * QML 相对 Widgets 版的增益：日志不再是「黑框文本」，而是结构化列表：
 *  - 三级着色（INFO / WARN / ERROR）圆点，扫一眼就能定位异常；
 *  - 关键字即时过滤（大小写不敏感）—— 旧版纯文本框无法筛选，
 *    长时间运行后要人肉滚 500 行找一条技术报警；
 *  - 上限由 C++ `LogModel::maxRows` 保证（默认 500，等价
 *    `setMaximumBlockCount(500)`），但内部是**环形下标**，
 *    插入 O(1)，不像 `removeFirst()` 那样每次搬移整块。
 */
Rectangle {
    id: panel

    property bool expanded: true
    /// 即时过滤关键字（大小写不敏感）。
    property string keyword: ""

    visible: expanded
    color: Theme.panelBg
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        // ---- 标题 + 过滤框 + 复制 / 清空 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Text {
                text: qsTr("运行日志")
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Text {
                text: logModel.rowCount() + " / " + logModel.maxRows
                color: Theme.textFaint
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // ---- 过滤输入框 ----
            Rectangle {
                Layout.preferredWidth: 150
                Layout.preferredHeight: 24
                radius: 3
                color: Theme.panelAlt
                border.width: 1
                border.color: filterInput.activeFocus ? Theme.accent : Theme.border

                TextInput {
                    id: filterInput
                    anchors.fill: parent
                    anchors.margins: 5
                    color: Theme.text
                    font.pixelSize: 11
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    clip: true
                    onTextChanged: panel.keyword = text

                    Text {
                        anchors.fill: parent
                        text: qsTr("过滤关键字…")
                        color: Theme.textFaint
                        font.pixelSize: 11
                        verticalAlignment: Text.AlignVCenter
                        visible: filterInput.text.length === 0
                    }
                }
            }

            ToolButton {
                text: qsTr("复制")
                onClicked: {
                    // 走剪贴板而不是「另存为」：医护常需要把日志粘进
                    // 病历系统 / 工单系统，多一步保存文件就多一次出错机会。
                    clipboardText.text = logModel.toPlainText();
                    clipboardText.selectAll();
                    clipboardText.copy();
                }
            }

            ToolButton {
                text: qsTr("清空")
                onClicked: logModel.clear()
            }
        }

        // ---- 日志列表 ----
        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            model: logModel
            boundsBehavior: Flickable.StopAtBounds
            // 新日志追加在末尾：自动滚到底部，保持「最新可见」。
            onCountChanged: positionViewAtEnd()

            delegate: Row {
                id: logRow

                required property string text
                required property string timeText
                required property int    level
                required property color  levelColor

                // 关键字过滤：大小写不敏感包含匹配。
                // @note 用 visible 而不是过滤 model，是为了让 ListView 的
                //       count 保持稳定、避免频繁重建；日志量级 ≤ 500 行，
                //       遍历成本可忽略。
                width: logList.width
                height: panel.keyword.length === 0
                        || logRow.text.toLowerCase().indexOf(panel.keyword.toLowerCase()) >= 0
                        ? 16 : 0
                visible: height > 0
                spacing: 6

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: logRow.levelColor
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: logRow.timeText
                    color: Theme.textFaint
                    font.pixelSize: 10
                    font.family: "Consolas"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: logList.width - 60
                    text: logRow.text
                    color: logRow.level === 2 ? Theme.danger
                          : (logRow.level === 1 ? Theme.warn : Theme.textDim)
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
    }

    // 剪贴板载体：TextEdit 提供 copy()，隐藏不显示。
    TextEdit {
        id: clipboardText
        visible: false
        width: 0
        height: 0
    }
}
