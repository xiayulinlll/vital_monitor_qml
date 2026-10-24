import QtQuick

/**
 * 深色下拉框（与 ToolButton 视觉统一）。
 *
 * 支持两种 model：
 *  - 字符串数组 → 直接显示字符串；
 *  - 对象数组 `{ text, value }` → 显示 text，`model[index].value` 为业务值
 *    （本项目视口跨度用这种，与 Widgets 版 `QComboBox::itemData` 等价）。
 *
 * @note 与 Controls 的 ComboBox 不同，这里**不提供** editable / delegate 定制：
 *       监护仪上不允许自由输入，只能在预设档位间切换，
 *       从 UI 层面消除非法值（例如跨度 0 或负数）。
 */
Rectangle {
    id: combo

    property var    model: []
    property int    currentIndex: 0
    property int    popupWidth: width
    signal activated(int index)

    implicitWidth: 140
    implicitHeight: 28

    readonly property var currentItem: (model && model.length > 0
                                        && currentIndex >= 0
                                        && currentIndex < model.length)
                                       ? model[currentIndex] : null

    radius: 4
    color: hoverArea.containsMouse ? "#252C38" : Theme.panelAlt
    border.width: 1
    border.color: Theme.border

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.right: arrow.left
        anchors.verticalCenter: parent.verticalCenter
        text: {
            if (combo.currentItem === null)
                return "";
            // 字符串 model 与对象 model 都支持。
            return (typeof combo.currentItem === "string")
                   ? combo.currentItem
                   : (combo.currentItem.text !== undefined
                      ? combo.currentItem.text : "");
        }
        color: Theme.text
        font.pixelSize: 12
        elide: Text.ElideRight
    }

    Text {
        id: arrow
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textDim
        font.pixelSize: 11
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.opened = !popup.opened
    }

    // ---- 下拉列表：向上或向下弹出，取决于剩余空间 ----
    Rectangle {
        id: popup
        property bool opened: false

        width: Math.max(combo.popupWidth, combo.width)
        height: Math.min(list.contentHeight + 2, 220)
        radius: 4
        color: Theme.panelBg
        border.width: 1
        border.color: Theme.border
        visible: opened
        z: 100

        // 默认向下弹；若下方空间不足则翻到上方。
        // @note 用 `combo.Window.window`（附加属性）而不是 `combo.window`：
        //       `window` 是 Window 附加属性，需要显式限定，
        //       直接写 combo.window 会报 "Property window not found"。
        y: (combo.mapToItem(null, 0, 0).y + combo.height + height
            < combo.Window.window.height)
           ? combo.height + 2
           : -height - 2
        x: 0

        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: combo.model
            currentIndex: combo.currentIndex
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: list.width
                height: 26
                color: (index === combo.currentIndex)
                       ? Theme.border
                       : (itemMouse.containsMouse ? "#252C38" : "transparent")

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: (typeof modelData === "string")
                          ? modelData
                          : (modelData.text !== undefined ? modelData.text : "")
                    color: (index === combo.currentIndex) ? Theme.accent : Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: itemMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        combo.currentIndex = index;
                        popup.opened = false;
                        combo.activated(index);
                    }
                }
            }
        }
    }
}
