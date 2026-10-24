import QtQuick
import QtQuick.Layouts

import VitalMonitor

/**
 * 趋势曲线面板：工具栏 + QPainter 绘图内核（ChartBridge）。
 *
 * ## 为什么中间那块不是 QML Canvas / Shape
 *
 * 见 `src/chart/ChartBridge.h` 的说明：4 通道 × 数百点 × 10 Hz 的路径重建
 * 若放在 JS 里解释执行，CPU 占用会显著高于已编译的 QPainter 路径。
 * 因此这里只用 QML 做**外壳**（工具条、按钮、提示），像素绘制完全交给 C++：
 *
 * ```
 * ChartBridge (QQuickPaintedItem)
 *    |-- 静态网格背景预渲染为 QImage，paint() 仅贴图
 *    |-- appendFrames() 用 update(rect) 只刷新新增数据覆盖的矩形
 *    '-- 缩放/平移只改 CoordinateMapper 视口参数，原始数据一个不动
 * ```
 *
 * ## QML 侧的绑定职责
 *
 * 工具栏控件**双向绑定**到 ChartBridge 的 Q_PROPERTY
 * （`visibleIndexSpan` / `followLatest` / `multiAxis`），
 * 这样滚轮缩放、拖拽平移（C++ 内部触发）也会让工具栏同步更新，
 * 无需在 QML 里手写反向同步逻辑。
 */
Rectangle {
    id: panel

    /// 当前被点击高亮的通道 id（-1 = 无）。
    property int highlightChannel: -1

    /// 切换某通道的曲线显示（供数值卡片双击调用）。
    /// 通过 `channelModel` 的角色读取当前配置，改掉 visible 后整表回传。
    function toggleChannel(chId) {
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
                "visible":      channelModel.data(idx, 267)
            });
        }
        for (var j = 0; j < list.length; ++j) {
            if (list[j].id === chId)
                list[j].visible = !list[j].visible;
        }
        Backend.commitConfig(list);
    }

    color: Theme.panelBg
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        // ==================== 工具栏 ====================
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap


            // ---- 开始 / 停止采集 ----
            Rectangle {
                Layout.preferredWidth: 108
                Layout.preferredHeight: 28
                radius: 4
                color: Backend.running ? "#8A2E2E" : "#1E6E3C"
                border.width: 1
                border.color: Qt.darker(color, 1.3)

                Text {
                    anchors.centerIn: parent
                    text: Backend.running ? qsTr("■ 停止采集") : qsTr("▶ 开始采集")
                    color: "#FFFFFF"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: Backend.toggleRunning()
                }
            }

            // ---- 清空 ----
            ToolButton {
                text: qsTr("清空")
                onClicked: Backend.clear()
            }

            Item { Layout.preferredWidth: 8 }

            Text {
                text: qsTr("视口跨度:")
                color: Theme.textDim
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }

            // ---- 视口跨度下拉（取值与 Widgets 版 spanCombo 一致）----
            ComboBox {
                id: spanCombo
                Layout.preferredWidth: 152
                model: [
                    { text: qsTr("10 秒 (100 点)"),    value: 100  },
                    { text: qsTr("30 秒 (300 点)"),    value: 300  },
                    { text: qsTr("60 秒 (600 点)"),    value: 600  },
                    { text: qsTr("3 分钟 (1800 点)"),  value: 1800 },
                    { text: qsTr("10 分钟 (6000 点)"), value: 6000 }
                ]
                currentIndex: 2   // 默认 60 秒，与 Widgets 版 setCurrentIndex(2) 一致

                onActivated: {
                    var item = model[currentIndex];
                    if (item)
                        Backend.span(item.value);
                }
            }

            // ---- 实时跟随开关 ----
            ToolButton {
                text: Backend.playingBack
                      ? qsTr("跟随: —")
                      : (chartBridge.followLatest ? qsTr("实时跟随: 开")
                                                  : qsTr("实时跟随: 关"))
                checked: chartBridge.followLatest
                enabled: !Backend.playingBack
                checkable: true
                onClicked: Backend.followLatest(!chartBridge.followLatest)
            }

            // ---- 每通道独立 Y 轴开关（各通道量纲差异大时必须开启）----
            ToolButton {
                text: chartBridge.multiAxis ? qsTr("独立Y轴: 开") : qsTr("独立Y轴: 关")
                checkable: true
                checked: chartBridge.multiAxis
                onClicked: Backend.multiAxis(!chartBridge.multiAxis)
            }

            Item { Layout.fillWidth: true }

            // ---- 导入 / 导出 ----
            ToolButton {
                text: qsTr("导入历史")
                enabled: !Backend.running
                onClicked: Backend.importFile()
            }
            ToolButton {
                text: qsTr("导出截图")
                onClicked: Backend.exportPng()
            }
            ToolButton {
                text: qsTr("导出数据")
                onClicked: Backend.exportCsv()
            }
        }


        // ==================== 绘图区 ====================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 由 C++ 侧注册的上下文属性 chartBridge，直接作为 QQuickItem 使用。
            // 数据入口在 MainWindowQml::onFrameReady() 里直连 appendFrames()，
            // 因此这里不需要任何 model / 信号绑定。
            ChartBridge {
                id: bridge
                anchors.fill: parent
                // 见 ChartBridge.qml：把 C++ 创建的绘图项挂载到场景图，
                // 因为 ChartBridge 是 setContextProperty 暴露的实例，
                // 而非 qmlRegisterType 注册的可实例化类型（Qt 6.2 无 QML_FOREIGN）。
                signalBridge: chartBridge
            }

            // 历史浏览提示：跟随关闭时明确告知「画面不再滚动」，
            // 避免医护误以为监护仪卡死。
            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 8
                width: browseText.width + 20
                height: 24
                radius: 12
                color: Qt.rgba(0.95, 0.61, 0.07, 0.85)
                visible: !chartBridge.followLatest && !Backend.playingBack

                Text {
                    id: browseText
                    anchors.centerIn: parent
                    text: qsTr("历史浏览中 — 双击曲线区可回到实时")
                    color: "#141820"
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onDoubleClicked: Backend.resetView()
                }
            }
        }
    }
}
