import QtQuick

/**
 * 配置数值输入框（只接受数字，供阈值 / 量程编辑）。
 *
 * 与普通 `TextInput` 的差异：
 *  - `validator` 限制为实数，从输入层杜绝把「abc」写进阈值；
 *  - 失焦（或回车）时才发 `edited()`，避免每敲一个字符就触发一次审计写入
 *    —— 否则输入 "100" 会产生三条审计记录（1 / 10 / 100）；
 *  - 内容非法（空 / 只有减号）时回滚到上一次有效值，不会留下半截数字。
 */
Rectangle {
    id: spin

    property string text: ""
    property real   lastValid: 0
    /// 输入完成（失焦或回车）且内容合法时发出。
    signal edited(real value)

    implicitHeight: 22
    radius: 3
    color: input.activeFocus ? "#20262F" : Theme.panelAlt
    border.width: 1
    border.color: input.activeFocus ? Theme.accent : Theme.border

    function commit() {
        var v = parseFloat(input.text);
        if (isNaN(v)) {
            // 非法输入：回滚，避免把半截数字当成阈值下发。
            input.text = String(spin.lastValid);
            return;
        }
        spin.lastValid = v;
        spin.text = input.text;
        spin.edited(v);
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        text: spin.text
        color: Theme.text
        font.pixelSize: 11
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        clip: true
        validator: DoubleValidator { notation: DoubleValidator.StandardNotation }

        onEditingFinished: spin.commit()
        onActiveFocusChanged: if (!activeFocus) spin.commit()
        Keys.onReturnPressed: spin.commit()
    }
}
