import QtQuick

/**
 * ChartBridge 的 QML 外观包装（薄壳）。
 *
 * ## 为什么不直接 `ChartBridge { }` 放在 ChartPanel 里
 *
 * C++ 里 `ChartBridge` 是 `QQuickPaintedItem` 子类，但它**不是**通过
 * `qmlRegisterType` 注册的类型，而是由 `MainWindowQml` 创建后经
 * `QQmlContext::setContextProperty("chartBridge", ...)` 暴露的**实例**。
 * 所以 QML 无法 `new` 它，只能把已有实例挂进场景图。
 *
 * 本组件负责这件「挂载」的活儿：
 *  - 声明 `signalBridge` 属性接收那个实例；
 *  - 通过 `Loader.sourceComponent` 无法用于实例挂载，因此改用
 *    `QQuickItem::setParentItem()` 语义等价的 `parent` 重设 +
 *    `anchors` —— 即直接把外部 item 的 parent 指向本组件。
 *
 * @note 这个模式（C++ 建实例 → QML 挂载）是本项目「数据热路径不过 QML」
 *       目标下的必然选择：若改成 `qmlRegisterType` 让 QML 来 new，
 *       那 C++ 业务层就拿不到指针，也就无法在 `onFrameReady()` 里
 *       直连 `appendFrames()` 而必须绕一圈信号。
 *       `QML_FOREIGN` + `QML_NAMED_ELEMENT` 需要 Qt 6.3+，
 *       本项目锁定 6.2，因此采用挂载模式。
 */
Item {
    id: wrapper

    /// C++ 侧注入的 ChartBridge 实例。
    property var signalBridge: null

    onSignalBridgeChanged: reparent()

    Component.onCompleted: reparent()

    /// 把外部 item 的父子关系挂到本组件下，并铺满整个区域。
    function reparent() {
        if (!signalBridge)
            return;

        signalBridge.parent = wrapper;
        signalBridge.anchors.fill = wrapper;
        signalBridge.visible = true;
    }
}
