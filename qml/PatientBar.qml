import QtQuick
import QtQuick.Layouts

/**
 * 顶部患者信息栏（对应 Widgets 版 PatientBarWidget）。
 *
 * 解决优化文档 9.1 第 4、5 项：
 *  1. 「无患者信息区」—— 缺少患者 ID / 姓名 / 床号，报警时无法确认
 *     「这条报警是谁的」，正式产品必需；
 *  2. 「模拟数据未显著标识」—— Demo 数据可能被误认为真实测量值。
 *     这里用**常驻的高对比橙色标识**，而不是只写在 README 里；
 *     只要数据源是模拟器，标识就必须一直可见。
 *
 * @note 与 Widgets 版的差异：QML 版刻意不依赖 QtQuick.Controls，
 *       因此床号 / ID / 姓名用**可点击编辑的内联文本**实现：
 *       点击后进入编辑，回车提交、Esc 取消；提交时写审计日志。
 */
Rectangle {
    id: bar

    implicitHeight: 30
    radius: Theme.radius
    color: Theme.panelBg
    border.width: 1
    border.color: Theme.border

    /// 患者上下文（演示项目不做持久化，仅会话内有效）。
    property string bedNo: "01"
    property string patientId: "DEMO-0001"
    property string patientName: "模拟患者"
    /// 当前患者类型（0 成人 / 1 小儿 / 2 新生儿 / 3 自定义），由 Main.qml 传入。
    property int patientType: 0

    /// 患者信息被修改（供 Main.qml 转交 C++ 写审计）。
    signal patientInfoChanged(string summary)

    /// 把三项文本拼成审计用摘要。
    function summary() {
        return qsTr("床位=%1；患者ID=%2；姓名=%3；类型=%4")
                .arg(bedNo).arg(patientId).arg(patientName)
                .arg(patientTypeName(patientType));
    }

    function patientTypeName(t) {
        switch (t) {
        case 0: return qsTr("成人");
        case 1: return qsTr("小儿");
        case 2: return qsTr("新生儿");
        default: return qsTr("自定义");
        }
    }

    /// 「标签 + 可编辑值」的复用单元。
    /// @note 必须声明在使用它的 RowLayout **之前**：QML 内联组件是
    ///       按声明顺序绑定到对象作用域的，后声明的组件不能被前面的
    ///       对象引用（会报 `Field is not a type`）。
    component Field: RowLayout {
        id: field
        property string label: ""
        property string value: ""
        signal committed(string text)

        spacing: 3

        Text {
            text: field.label
            color: Theme.textFaint
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            Layout.preferredWidth: Math.max(56, valueText.implicitWidth + 10)
            Layout.preferredHeight: 19
            radius: 3
            color: editArea.editing ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
            border.width: editArea.editing ? 1 : 0
            border.color: Theme.accent

            Text {
                id: valueText
                anchors.centerIn: parent
                text: field.value
                color: Theme.text
                font.pixelSize: 12
                font.bold: true
            }

            TextInput {
                id: editArea
                property bool editing: false
                anchors.fill: parent
                anchors.margins: 3
                visible: editing
                clip: true
                color: Theme.text
                font.pixelSize: 12
                selectionColor: Theme.accent
                selectByMouse: true

                // 提交：空值不采纳，避免出现「床号为空」这种更糟的状态。
                function commit() {
                    if (text.trim().length > 0 && text !== field.value)
                        field.committed(text.trim());
                    editing = false;
                }

                Keys.onReturnPressed: commit()
                Keys.onEnterPressed: commit()
                Keys.onEscapePressed: {
                    text = field.value;
                    editing = false;
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                enabled: !editArea.editing
                onClicked: {
                    editArea.text = field.value;
                    editArea.editing = true;
                    editArea.forceActiveFocus();
                    editArea.selectAll();
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 8

        // ================= 演示模式标识 =================
        // @note 放在最左侧且用背景色块而非纯文字：纯文字在深色界面上
        //       很容易被当作普通标签忽略，色块能保证「一眼看见」。
        Rectangle {
            Layout.preferredWidth: demoText.implicitWidth + 14
            Layout.preferredHeight: 19
            radius: 3
            color: "#F0A020"

            Text {
                id: demoText
                anchors.centerIn: parent
                text: qsTr("⚠ 演示模式 · 模拟数据")
                color: "#1B1200"
                font.pixelSize: 11
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.WhatsThisCursor
                // 点击标识 → 在日志与审计里再声明一次数据来源。
                // @note 走 C++ 的 logNotice() 而不是 QML 侧的字符串拼接：
                //       日志模型与审计文件都在 C++，QML 只负责表达意图。
                onClicked: Backend.logNotice(qsTr(
                    "[声明] 本程序所有生理数据由内置模拟器生成，仅用于学习与技术演示，"
                    + "不得用于任何临床诊断或治疗。"))
            }
        }

        // ================= 患者信息（内联编辑） =================
        Field {
            label: qsTr("床号")
            value: bar.bedNo
            onCommitted: (text) => { bar.bedNo = text; bar.patientInfoChanged(bar.summary()); }
        }
        Field {
            label: qsTr("患者ID")
            value: bar.patientId
            onCommitted: (text) => { bar.patientId = text; bar.patientInfoChanged(bar.summary()); }
        }
        Field {
            label: qsTr("姓名")
            value: bar.patientName
            onCommitted: (text) => { bar.patientName = text; bar.patientInfoChanged(bar.summary()); }
        }

        Text {
            text: qsTr("类型:")
            color: Theme.textFaint
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: bar.patientTypeName(bar.patientType)
            color: Theme.accent
            font.pixelSize: 12
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        // ================= 报警计数（9.1 第 1 项：状态常驻） =================
        // 与状态栏徽标的分工：状态栏描述「系统处于什么状态」（含暂停 / 静音），
        // 这里给出「活动 / 已确认」的计数对比，便于快速判断是否需要签收。
        Text {
            text: qsTr("活动报警 %1 / 已确认 %2")
                  .arg(Backend.activeAlarmCount)
                  .arg(Math.max(0, Backend.totalAlarmCount - Backend.activeAlarmCount))
            color: Backend.activeAlarmCount > 0 ? Theme.danger : Theme.textDim
            font.pixelSize: 11
            font.bold: Backend.activeAlarmCount > 0
            verticalAlignment: Text.AlignVCenter
        }
    }
}
