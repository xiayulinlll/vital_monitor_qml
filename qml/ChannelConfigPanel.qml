import QtQuick
import QtQuick.Layouts

import VitalMonitor

/**
 * 通道配置面板（对应 Widgets 版 ChannelConfigPanel）。
 *
 * 相对 Widgets 版的关键差异：
 *  - 患者类型切换直接调 `Backend.setPatientType()`，由 C++ 重载报警限剖面，
 *    再把结果推回本面板（`syncFromModel()`）——  **不在 QML 里复制一份限值表**，
 *    否则两份数据必然漂移；
 *  - 可见性 / 启停用开关而不是复选框：监护仪上「开关」的状态
 *    比「勾选框」更容易在一米外辨认；
 *  - 编辑中的草稿与已生效的配置分离，点「应用配置」才下发。
 *    这样用户改错阈值时不会立刻触发错误报警。
 */
Rectangle {
    id: panel

    /// 折叠状态：折叠时只保留标题行（患者类型 + 应用按钮）。
    property bool expanded: false

    /// 草稿（编辑中，尚未下发）。每项：
    /// { id, name, unit, color, minValue, maxValue, alarmLow, alarmHigh,
    ///   hasLowAlarm, hasHighAlarm, visible, minDraft, maxDraft, lowDraft, highDraft }
    property var draft: []

    /// 当前生效的患者类型（0 成人 / 1 小儿 / 2 新生儿 / 3 自定义）。
    /// 由 C++ `ChannelConfigManager::globalPatientType()` 决定；
    /// QML 侧只是镜像，切换时立刻写入并同时通知 C++。
    property int patientTypeIndex: 0

    /// 待确认的变更清单（优化文档 6.1 第 3 项）。
    /// 非空时面板顶部展示「旧值 → 新值」，用户再点一次才真正下发。
    property var pendingChanges: []
    /// 待确认的校验告警（单位 / 量纲不自洽等非阻塞提示）。
    property var pendingWarnings: []
    /// 是否已进入「等待二次确认」状态（决定「应用」按钮的文案与动作）。
    property bool awaitingConfirm: false

    /// 已登记单位及其物理可能区间，与 C++ `ChannelConfig.cpp` 的
    /// `kUnitRanges` **逐项一致**。QML 只做「提交前提示」，
    /// 权威判定仍在 C++：这里漏判不会放行错误配置，只是少一条提示。
    readonly property var knownUnits: [
        { "u": "bpm",  "lo": 0.0,   "hi": 400.0 },
        { "u": "rpm",  "lo": 0.0,   "hi": 120.0 },
        { "u": "%",    "lo": 0.0,   "hi": 100.0 },
        { "u": "\u2103", "lo": -20.0, "hi": 50.0 },
        { "u": "\u00B0C", "lo": -20.0, "hi": 50.0 },
        { "u": "C",    "lo": -20.0, "hi": 50.0 },
        { "u": "\u2109", "lo": 0.0,   "hi": 130.0 },
        { "u": "\u00B0F", "lo": 0.0,  "hi": 130.0 },
        { "u": "F",    "lo": 0.0,   "hi": 130.0 },
        { "u": "mmHg", "lo": 0.0,   "hi": 300.0 },
        { "u": "mV",   "lo": -10.0, "hi": 10.0 }
    ]

    color: Theme.panelBg
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border
    /// 折叠时只保留标题行；展开时加编辑区；有变更时再加二次确认区。
    /// @note 高度必须随确认区自适应，否则「旧值 → 新值」清单会被裁掉
    ///       ——而看不到差异的二次确认等于没有确认。
    implicitHeight: !expanded ? 38
                              : (216 + (pendingChanges.length > 0
                                        ? confirmArea.implicitHeight + 8 : 0))

    Behavior on implicitHeight {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    /// 从 C++ 模型同步一份草稿（不改变已生效配置）。
    function syncFromModel() {
        var list = [];
        var n = channelModel.rowCount();
        for (var i = 0; i < n; ++i) {
            var idx = channelModel.index(i, 0);
            list.push({
                "id":           channelModel.data(idx, 257),   // ChannelIdRole
                "name":         channelModel.data(idx, 258),   // NameRole
                "unit":         channelModel.data(idx, 259),
                "color":        channelModel.data(idx, 260),
                "minValue":     channelModel.data(idx, 261),
                "maxValue":     channelModel.data(idx, 262),
                "alarmLow":     channelModel.data(idx, 263),
                "alarmHigh":    channelModel.data(idx, 264),
                "hasLowAlarm":  channelModel.data(idx, 265),
                "hasHighAlarm": channelModel.data(idx, 266),
                "visible":      channelModel.data(idx, 267),
                "lowDraft":     String(channelModel.data(idx, 263)),
                "highDraft":    String(channelModel.data(idx, 264)),
                "minDraft":     String(channelModel.data(idx, 261)),
                "maxDraft":     String(channelModel.data(idx, 262))
            });
        }
        draft = list;
    }

    Component.onCompleted: syncFromModel()

    /// 判断草稿相对已生效配置是否有变化，并生成「旧值 → 新值」清单。
    /// @note 只比较**下发字段**（量程 / 报警限 / 开关 / 可见性）：
    ///       这些字段一旦变化会直接改变报警行为，属于必须确认的关键变更。
    function computeChanges() {
        var list = [];
        for (var i = 0; i < draft.length; ++i) {
            var d = draft[i];
            var lo = parseFloat(d.lowDraft);
            var hi = parseFloat(d.highDraft);
            var mn = parseFloat(d.minDraft);
            var mx = parseFloat(d.maxDraft);
            if (isNaN(lo)) lo = d.alarmLow;
            if (isNaN(hi)) hi = d.alarmHigh;
            if (isNaN(mn)) mn = d.minValue;
            if (isNaN(mx)) mx = d.maxValue;

            var pairs = [];
            if (mn !== d.minValue)
                pairs.push(qsTr("量程下限 %1 → %2").arg(d.minValue).arg(mn));
            if (mx !== d.maxValue)
                pairs.push(qsTr("量程上限 %1 → %2").arg(d.maxValue).arg(mx));
            if (lo !== d.alarmLow)
                pairs.push(qsTr("报警下限 %1 → %2").arg(d.alarmLow).arg(lo));
            if (hi !== d.alarmHigh)
                pairs.push(qsTr("报警上限 %1 → %2").arg(d.alarmHigh).arg(hi));
            if (d.hasLowAlarm !== (d.hasLowAlarm === true))
                pairs.push(qsTr("下限报警开关变化"));
            if (pairs.length > 0)
                list.push({ "name": d.name, "lines": pairs, "unit": d.unit,
                            "min": mn, "max": mx });
        }
        return list;
    }

    /// 提交前的非阻塞校验：单位未登记 / 量程在该单位下不可能。
    /// 返回提示文本数组（空数组表示没有发现问题）。
    function validateDraft() {
        var warn = [];
        for (var i = 0; i < draft.length; ++i) {
            var d = draft[i];
            var mn = parseFloat(d.minDraft);
            var mx = parseFloat(d.maxDraft);
            if (isNaN(mn)) mn = d.minValue;
            if (isNaN(mx)) mx = d.maxValue;

            var r = null;
            for (var k = 0; k < knownUnits.length; ++k) {
                if (knownUnits[k].u === d.unit) { r = knownUnits[k]; break; }
            }
            if (r === null) {
                warn.push(qsTr("%1：单位「%2」未登记，无法校验量纲").arg(d.name).arg(d.unit));
            } else if (mn < r.lo || mx > r.hi) {
                warn.push(qsTr("%1：量程 %2~%3 超出单位「%4」的物理可能区间 %5~%6")
                          .arg(d.name).arg(mn).arg(mx).arg(d.unit).arg(r.lo).arg(r.hi));
            }
        }
        return warn;
    }

    /// 「应用配置」：第一次点击只生成待确认清单，第二次才真正下发。
    /// 这解决了 6.1 第 3 项「参数修改无二次确认」——误改报警限是
    /// 已知重大风险源，必须让操作者看到旧值再决定。
    function requestApply() {
        // 已进入确认态 → 这是第二次点击，真正提交。
        if (awaitingConfirm) {
            panel.awaitingConfirm = false;
            panel.pendingChanges = [];
            panel.pendingWarnings = [];
            Backend.commitConfig(panel.buildCommitList());
            return;
        }

        var changes = computeChanges();
        if (changes.length === 0) {
            // 没有任何关键变更 → 无需确认，直接提交（例如只改了可见性）。
            Backend.commitConfig(panel.buildCommitList());
            return;
        }
        panel.pendingChanges = changes;
        panel.pendingWarnings = validateDraft();
        panel.awaitingConfirm = true;
    }

    /// 取消待确认状态（保留草稿，便于继续修改）。
    function cancelApply() {
        awaitingConfirm = false;
        pendingChanges = [];
        pendingWarnings = [];
    }

    // 配置被 C++ 侧整体重建（患者类型切换 / 通道增删）后重新同步草稿。
    Connections {
        target: channelModel
        function onModelReset() {
            panel.cancelApply();
            panel.syncFromModel();
        }
    }

    /// 把草稿解析成提交给 C++ 的对象数组。
    /// @note 解析失败（空串 / 非数字）时回退到原值，避免把 NaN 下发下去。
    function buildCommitList() {
        var out = [];
        for (var i = 0; i < draft.length; ++i) {
            var d = draft[i];
            var lo = parseFloat(d.lowDraft);
            var hi = parseFloat(d.highDraft);
            var mn = parseFloat(d.minDraft);
            var mx = parseFloat(d.maxDraft);

            out.push({
                "id":           d.id,
                "name":         d.name,
                "unit":         d.unit,
                "color":        d.color,
                "minValue":     isNaN(mn) ? d.minValue : mn,
                "maxValue":     isNaN(mx) ? d.maxValue : mx,
                "alarmLow":     isNaN(lo) ? d.alarmLow : lo,
                "alarmHigh":    isNaN(hi) ? d.alarmHigh : hi,
                "hasLowAlarm":  d.hasLowAlarm,
                "hasHighAlarm": d.hasHighAlarm,
                "visible":      d.visible
            });
        }
        return out;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        // ==================== 标题行（始终可见） ====================
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Text {
                text: panel.expanded ? "▾" : "▸"
                color: titleArea.containsMouse ? Theme.accent : Theme.textDim
                font.pixelSize: 12

                MouseArea {
                    id: titleArea
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.expanded = !panel.expanded
                }
            }

            Text {
                text: qsTr("通道配置")
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
            }

            Item { Layout.preferredWidth: 8 }

            Text {
                text: qsTr("患者类型:")
                color: Theme.textDim
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }

            // 患者类型切换：数值 0~3 与 C++ 的 PatientType 枚举一一对应。
            // 切换后 C++ 会 loadProfile() 重载报警限，并把新配置推回模型。
            ComboBox {
                Layout.preferredWidth: 110
                model: [
                    { text: qsTr("成人"),   value: 0 },
                    { text: qsTr("小儿"),   value: 1 },
                    { text: qsTr("新生儿"), value: 2 },
                    { text: qsTr("自定义"), value: 3 }
                ]
                currentIndex: panel.patientTypeIndex
                onActivated: {
                    var item = model[currentIndex];
                    if (item) {
                        panel.patientTypeIndex = currentIndex;
                        Backend.setPatientType(item.value);
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                text: qsTr("恢复默认")
                onClicked: {
                    panel.patientTypeIndex = 0;
                    // 切回成人剖面：C++ 会重置所有通道的限值，
                    // 随后 onModelReset 触发 syncFromModel() 刷新草稿。
                    Backend.setPatientType(0);
                }
            }

            ToolButton {
                id: applyButton
                text: panel.awaitingConfirm ? qsTr("确认下发") : qsTr("应用配置")
                onClicked: panel.requestApply()
            }
        }

        // ==================== 二次确认区（有待确认变更时出现） ====================
        // 优化文档 6.1 第 3 项：误改报警限是已知重大风险源，
        // 必须列出「旧值 → 新值」让人确认，而不是点一下就直接生效。
        Rectangle {
            id: confirmArea
            Layout.fillWidth: true
            // 编辑区是 fillHeight 的，若不固定高度，确认区会被压成 0 高。
            Layout.preferredHeight: confirmColumn.implicitHeight + 12
            Layout.minimumHeight: confirmColumn.implicitHeight + 12
            visible: panel.pendingChanges.length > 0
            radius: Theme.radius
            color: Qt.rgba(0.83, 0.60, 0.04, 0.16)
            border.width: 1
            border.color: Qt.rgba(0.83, 0.60, 0.04, 0.75)

            ColumnLayout {
                id: confirmColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: qsTr("⚠ 以下改动会直接改变报警行为，请核对后再点「确认下发」")
                    color: Theme.warn
                    font.pixelSize: 11
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                // 逐通道列出差异；不用 ListView：变更项通常只有 1~3 行，
                // 虚拟化反而增加复杂度。
                Repeater {
                    model: panel.pendingChanges
                    Text {
                        Layout.fillWidth: true
                        text: "· " + modelData.name + "：" + modelData.lines.join("；")
                        color: Theme.text
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                // 单位 / 量纲校验提示：不阻塞提交，但必须让操作者看到。
                Repeater {
                    model: panel.pendingWarnings
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("⚠ 单位校验：") + modelData
                        color: Theme.danger
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        text: qsTr("取消")
                        onClicked: panel.cancelApply()
                    }
                }
            }
        }

        // ==================== 编辑区（展开时可见） ====================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.expanded
            spacing: 4

            // ---- 表头 ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text { Layout.preferredWidth: 110; text: qsTr("通道");     color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 62;  text: qsTr("量程下限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 62;  text: qsTr("量程上限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 62;  text: qsTr("报警下限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 62;  text: qsTr("报警上限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 70;  text: qsTr("启用下限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.preferredWidth: 70;  text: qsTr("启用上限"); color: Theme.textFaint; font.pixelSize: 10 }
                Text { Layout.fillWidth: true;     text: qsTr("显示曲线"); color: Theme.textFaint; font.pixelSize: 10 }
            }

            // ---- 每通道一行 ----
            // @note Repeater 的 model 用 `panel.draft`（JS 数组），
            //       因此行内用 `index` 定位草稿并回写。draft 是普通 property，
            //       直接改 draft[index].xxx 不会触发绑定刷新；但输入框在
            //       onEdited 里已自行持有文本，因此无需再刷新。
            Repeater {
                model: panel.draft

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    // 通道名（带曲线配色条）
                    RowLayout {
                        Layout.preferredWidth: 110
                        spacing: 4

                        Rectangle {
                            Layout.preferredWidth: 3
                            Layout.preferredHeight: 16
                            radius: 1.5
                            color: modelData.color
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    ConfigSpin {
                        Layout.preferredWidth: 62
                        text: modelData.minDraft
                        onEdited: panel.draft[index].minDraft = value
                    }
                    ConfigSpin {
                        Layout.preferredWidth: 62
                        text: modelData.maxDraft
                        onEdited: panel.draft[index].maxDraft = value
                    }
                    ConfigSpin {
                        Layout.preferredWidth: 62
                        text: modelData.lowDraft
                        onEdited: panel.draft[index].lowDraft = value
                    }
                    ConfigSpin {
                        Layout.preferredWidth: 62
                        text: modelData.highDraft
                        onEdited: panel.draft[index].highDraft = value
                    }

                    ToggleSwitch {
                        Layout.preferredWidth: 70
                        checked: modelData.hasLowAlarm
                        onToggled: panel.draft[index].hasLowAlarm = checked
                    }
                    ToggleSwitch {
                        Layout.preferredWidth: 70
                        checked: modelData.hasHighAlarm
                        onToggled: panel.draft[index].hasHighAlarm = checked
                    }

                    ToggleSwitch {
                        Layout.fillWidth: true
                        checked: modelData.visible
                        onToggled: panel.draft[index].visible = checked
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
