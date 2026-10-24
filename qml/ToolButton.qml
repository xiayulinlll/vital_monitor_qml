import QtQuick

/**
 * 深色工具条按钮（本项目的统一按钮外观）。
 *
 * @note 刻意不用 QtQuick.Controls 的 Button：本项目全部控件自绘，
 *       避免额外依赖 Controls 模块与样式插件，也避免其
 *       「默认样式在各平台差异极大」的问题影响医疗界面的一致性。
 */
Rectangle {
    id: btn

    property string text: ""
    property bool checkable: false
    property bool checked: false
    signal clicked()

    implicitWidth: label.implicitWidth + 20
    implicitHeight: 28

    radius: 4
    color: {
        if (!btn.enabled)
            return Theme.panelAlt;
        if (btn.checkable && btn.checked)
            return hoverArea.containsMouse ? "#3A4658" : Theme.border;
        return hoverArea.containsMouse ? "#252C38" : Theme.panelAlt;
    }
    border.width: 1
    border.color: (btn.checkable && btn.checked) ? Theme.accent : Theme.border
    opacity: btn.enabled ? 1.0 : 0.45

    Behavior on color {
        ColorAnimation { duration: 90 }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        color: !btn.enabled ? Theme.textFaint
                            : ((btn.checkable && btn.checked) ? Theme.accent
                                                              : Theme.text)
        font.pixelSize: 12
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: btn.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (btn.checkable)
                btn.checked = !btn.checked;
            btn.clicked();
        }
    }
}
