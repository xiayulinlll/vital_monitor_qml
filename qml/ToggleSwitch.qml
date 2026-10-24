import QtQuick

/**
 * 开关（用于「启用下限 / 启用上限 / 显示曲线」）。
 *
 * 相对 `CheckBox` 的优势：在监护仪的实际使用距离（0.5 ~ 1.5 m）下，
 * 滑块位置比勾选框的对勾更容易辨认，且「开 = 亮色 + 滑块靠右」
 * 是无需阅读文字的直觉语义。
 *
 * @note 与 `ToolButton` 一样不使用 QtQuick.Controls：
 *       本项目全部控件自绘，避免样式插件依赖与跨平台外观差异。
 */
Rectangle {
    id: sw

    property bool checked: false
    signal toggled(bool checked)

    implicitHeight: 22
    radius: 11
    color: checked ? "#2E7D4F" : "#2A3240"
    border.width: 1
    border.color: checked ? "#3E9E66" : Theme.border

    Behavior on color {
        ColorAnimation { duration: 120 }
    }

    // 状态文字：既显示滑块位置也用文字确认，双重编码。
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: sw.checked ? qsTr("开") : qsTr("关")
        color: sw.checked ? "#D8F5E4" : Theme.textFaint
        font.pixelSize: 10
    }

    // 滑块
    Rectangle {
        width: 16
        height: 16
        radius: 8
        anchors.verticalCenter: parent.verticalCenter
        x: sw.checked ? sw.width - width - 3 : 3
        color: sw.checked ? "#EAFBF1" : "#5A6675"

        Behavior on x {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            sw.checked = !sw.checked;
            sw.toggled(sw.checked);
        }
    }
}
