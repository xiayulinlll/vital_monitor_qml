#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "app/MainWindowQml.h"
#include "data/SampleData.h"

namespace {

/// 诊断日志文件（见 messageHandler 的说明）。
QFile g_logFile;

/// 把 Qt 诊断输出同时写入文件。
///
/// @note 之所以需要：Windows GUI 子系统程序（WIN32_EXECUTABLE TRUE）
///       没有控制台，stderr 重定向经常捕获不到 QML 引擎的运行时错误
///       （属性绑定失败、组件创建失败等），导致「程序启动了但界面空白」
///       这类问题极难定位。落盘后可直接查看 QML 究竟报了什么。
void messageHandler(QtMsgType type, const QMessageLogContext &ctx,
                    const QString &msg)
{
    Q_UNUSED(ctx);

    static const char *kLevelNames[] = { "DEBUG", "WARNING", "CRITICAL",
                                         "FATAL", "INFO" };
    const char *level = kLevelNames[qMin(int(type), 4)];

    const QString line =
        QStringLiteral("%1 [%2] %3")
            .arg(QDateTime::currentDateTime()
                     .toString(QStringLiteral("HH:mm:ss.zzz")),
                 QLatin1String(level), msg);

    if (g_logFile.isOpen()) {
        QTextStream ts(&g_logFile);
        ts << line << '\n';
        ts.flush();
    }
    fprintf(stderr, "%s\n", qPrintable(line));
}

} // namespace

/**
 * @brief QML 版监护上位机入口。
 *
 * 与 Widgets 版的差异（`vital_monitor/src/main.cpp`）：
 *  - 由 `QApplication` 改为 `QGuiApplication`：QML 场景图不需要 QtWidgets，
 *    少链接一个模块可明显缩短启动时间与包体；
 *  - 不再 `new MainWindow` 作为顶层窗口，改为加载 `qml/Main.qml`；
 *  - 但**绘图内核仍然是 QPainter**（`ChartBridge` 继承 `QQuickPaintedItem`），
 *    这是刻意的取舍：QML 只替换外壳，热路径绘制算法完全复用。
 *
 * 启动顺序有严格要求：
 *   1. 创建业务宿主 `MainWindowQml`（构造时按默认剖面建好模型）；
 *   2. `setupThreading()` 先起采集线程，保证首帧到达时线程已就绪；
 *   3. 注册 QML 单例与上下文属性；
 *   4. 最后才 `load()` QML —— 此时 `Backend.qml` 单例已存在，
 *      `exposeToQml()` 才能查到并注入初始状态。
 */
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VitalMonitorQml"));
    app.setOrganizationName(QStringLiteral("QtMedicalDemo"));

    // 诊断日志落盘（见 messageHandler 的说明）。
    g_logFile.setFileName(QStringLiteral("vital_monitor_qml.log"));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    // 注册自定义类型，使跨线程信号槽（QueuedConnection）可以传递它们。
    // 注意：QVector<ChannelConfig> 必须显式注册，否则 requestStart /
    // requestUpdateConfigs 这类带参跨线程调用会在运行时报
    // "Cannot queue arguments of type" 并静默丢消息。
    qRegisterMetaType<ChannelSample>("ChannelSample");
    qRegisterMetaType<SampleFrame>("SampleFrame");
    qRegisterMetaType<AlarmEvent>("AlarmEvent");
    qRegisterMetaType<QVector<ChannelConfig>>("QVector<ChannelConfig>");

    // 本项目 QML 侧控件均在 qml/ 下实现，不依赖 Qt Quick Controls。
    // 因此**不设置** QQuickStyle（那需要额外链接 Qt6::QuickControls2）。

    MainWindowQml controller;

    // 采集线程必须在 QML 加载前启动：Main.qml 的 Component.onCompleted
    // 会立即读取 Backend 状态并可能触发 start()。
    controller.setupThreading();

    QQmlApplicationEngine engine;

    // 业务宿主以上下文属性暴露，QML 侧可直接调用其 public slots。
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),
                                             &controller);

    // 打开审计日志（失败不阻断启动，仅降级为仅控制台输出）。
    controller.openAuditLog();

    // 注入模型与初始状态到 QML 上下文 + Backend 单例。
    controller.exposeToQml(engine.rootContext());

    // QML 加载失败时必须显式退出：否则进程会静默挂着不显示任何窗口，
    // 使用者只会看到「程序启动了但没界面」这种极难排查的现象。
    //
    // @note Qt 6.2 没有 `QQmlApplicationEngine::objectCreationFailed` 信号
    //       与 `loadFromModule()`（二者均为 6.4 / 6.5 引入）。
    //       因此这里用 6.2 可用的组合：
    //         - 连接 engine 的 objectCreated 检查 root 对象是否为空；
    //         - 用 `load()` 加载 qrc 路径（与 RESOURCE_PREFIX /qt/qml 对应）。
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [](QObject *obj, const QUrl &url) {
            if (!obj) {
                qCritical("QML 加载失败：%s", qPrintable(url.toString()));
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    // 用模块名加载而非路径：让 qt_add_qml_module 生成的资源前缀生效，
    // 从而支持 qrc 内置资源与外部文件两种部署方式。
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/VitalMonitor/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    // 采集默认不自动开始（与 Widgets 版一致：由用户点「开始采集」）。
    qInfo("QML 界面已加载，等待用户操作");

    // -----------------------------------------------------------------------
    // --selftest：无人值守的端到端自检。
    //
    // 验证链路（全自动，退出码即结论）：
    //   QML 加载 → Backend 回传 → 启动采集 → 工作线程产帧
    //   → onFrameReady 写环形缓冲 / 判告警 / 喂模型 → 状态推回 QML
    //
    // 之所以做成自检而不是「启动后看一眼」：GUI 程序若只靠人眼看，
    // 线上一旦某处静默失败（例如跨线程 signal 参数类型没注册，
    // Qt 只会打一条 warning 然后丢消息），界面依旧显示，数据却永远是 0。
    // 自检让这种失败直接体现为退出码。
    // -----------------------------------------------------------------------
    if (app.arguments().contains(QStringLiteral("--selftest"))) {
        constexpr int kRunMs = 2500;

        controller.startAcquisition(100);

        QTimer::singleShot(kRunMs, &app, [&controller, &app]() {
            controller.stopAcquisition();

            const int frames  = controller.frameCount();
            const int hist    = controller.historySize();
            const int uptime  = controller.uptimeSec();

            bool ok = true;
            if (frames < 15) {          // 2.5 s @ 10 Hz ≈ 25 帧，留足余量
                qCritical("SELFTEST FAIL: 采集帧数不足 (%d)", frames);
                ok = false;
            }
            if (hist != frames) {
                qCritical("SELFTEST FAIL: 历史缓冲与帧数不一致 (%d vs %d)",
                          hist, frames);
                ok = false;
            }
            if (uptime < 1) {
                qCritical("SELFTEST FAIL: 运行时长未累计 (%d s)", uptime);
                ok = false;
            }

            qInfo("SELFTEST %s: frames=%d history=%d uptime=%ds alarms=%d",
                  ok ? "PASS" : "FAIL", frames, hist, uptime,
                  controller.totalAlarmCount());

            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}
