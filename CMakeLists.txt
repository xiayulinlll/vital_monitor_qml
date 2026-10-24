cmake_minimum_required(VERSION 3.16)

project(vital_monitor_qml VERSION 1.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let CMake run moc/uic/rcc automatically.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 模块：QML 侧使用 QtQuick（不依赖 QtQuick.Controls，
# 下拉框/开关/微调框等控件均在 qml/ 下实现），绘图内核使用 QPainter。
#
# @note 为什么没有 Qt6::Widgets：本程序是 QGuiApplication，
#       界面全部由 QML 承载，顶层窗口是 QtQuick.Window 的 Window。
#       QPainter 属于 Qt6::Gui，不需要 Widgets。少链接一个模块
#       可明显缩短启动时间与部署体积。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia)

qt_add_executable(vital_monitor_qml
    src/main.cpp

    # ---- Layer 1: data simulation / acquisition (worker thread) ----
    # 与 Widgets 版**逐字复用**，未做任何修改。
    src/data/SampleData.h
    src/data/VitalSignSimulator.h
    src/data/VitalSignSimulator.cpp

    # ---- Layer 2: business & data management (main thread) ----
    src/core/RingBuffer.h
    src/core/ChannelConfig.h
    src/core/ChannelConfig.cpp
    src/core/AlarmDetector.h
    src/core/AlarmDetector.cpp
    src/core/AlarmNotifier.h
    src/core/AlarmNotifier.cpp
    src/core/CoordinateMapper.h
    src/core/CoordinateMapper.cpp
    src/core/HistoryRecorder.h
    src/core/HistoryRecorder.cpp
    src/core/AuditLogger.h
    src/core/AuditLogger.cpp

    # ---- Layer 3: QML 数据模型层（替代 Widgets 版的 src/ui 面板）----
    # @note Widgets 版的 src/ui/ 与 src/chart/TrendChartWidget.* 已从本工程移除；
    #       它们完整保留在原 vital_monitor 工程中，此处不再重复存放，
    #       以免全局搜索出现两份功能重复的实现。
    src/models/ChannelModel.h
    src/models/ChannelModel.cpp
    src/models/AlarmModel.h
    src/models/AlarmModel.cpp
    src/models/LogModel.h
    src/models/LogModel.cpp

    # ---- Layer 4: QPainter 绘图内核（QQuickPaintedItem）----
    src/chart/ChartBridge.h
    src/chart/ChartBridge.cpp

    # ---- Layer 5: 业务宿主（不含 UI 代码）----
    src/app/MainWindowQml.h
    src/app/MainWindowQml.cpp
)

target_include_directories(vital_monitor_qml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# QML 模块注册
#
# URI = VitalMonitor，RESOURCE_PREFIX 用 "/qt/qml"（Qt 6.2 的默认约定，
# 也是 qmllint/部署工具期望的位置；写成其他前缀会导致
# QQmlApplicationEngine 找不到模块）。
# ---------------------------------------------------------------------------
# 单例声明（**必须在 qt_add_qml_module 之前**设置）
#
# @note Qt 6.2 **没有** `SINGLETON` 关键字（那是 Qt 6.5+ 的语法糖）。
#       6.2 必须用源文件属性 `QT_QML_SINGLETON_TYPE` 标记；
#       写成 `SINGLETON file.qml` 会被当作文件名处理并报
#       "Only .qml, .js or .mjs files should be added with QML_FILES"。
#
# @warning 这一步必须放在 qt_add_qml_module **之前**：
#          该属性是在调用时被读取并写进 qmldir 的 `singleton` 标记；
#          放在调用之后就只会影响后续目标，本模块的 qmldir 里不会有它。
#
#   - Theme.qml   → 全局配色真源（QML 侧不得硬编码色值）
#   - Backend.qml → 业务状态镜像 + 动作转发；C++ 侧无法用 singletonInstance()
#     检索文件式单例（Qt 6.2 只接受 C++ 注册的类型 id），
#     改由 Main.qml 在 Component.onCompleted 里把实例回传给 controller
# ---------------------------------------------------------------------------
set_source_files_properties(qml/Theme.qml   PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Backend.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vital_monitor_qml
    URI VitalMonitor
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Backend.qml
        qml/PatientBar.qml
        qml/ChartPanel.qml
        qml/ChartBridge.qml
        qml/ChannelValueCard.qml
        qml/AlarmListPanel.qml
        qml/AlarmListDelegate.qml
        qml/AlarmBanner.qml
        qml/LogPanel.qml
        qml/ChannelConfigPanel.qml
        qml/ToolButton.qml
        qml/ComboBox.qml
        qml/ConfigSpin.qml
        qml/ToggleSwitch.qml
)

target_link_libraries(vital_monitor_qml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    # 报警提示音：QGuiApplication 下不能调用 QApplication::beep()，
    # 改用 QSoundEffect 异步播放（详见 src/core/AlarmNotifier.cpp 头部注释）。
    Qt6::Multimedia
)

# Windows GUI application (no console window).
set_target_properties(vital_monitor_qml PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Deploy Qt runtime DLLs next to the executable so it can run directly.
# QML 应用必须带上 --qmldir 参数，否则 windeployqt 无法扫描出
# QML 插件依赖（例如 QtQuick 的 scenegraph 后端），运行时会白屏。
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET vital_monitor_qml POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --qmldir
                    "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:vital_monitor_qml>"
            COMMENT "Running windeployqt on vital_monitor_qml"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found; Qt DLLs will not be copied automatically.")
    endif()
endif()

# ---------------------------------------------------------------------------
# 自动化测试：直接复用 Widgets 版的报警状态机测试。
#
# 该测试不依赖任何 UI，因此 QML 版与 Widgets 版共用同一份断言，
# 保证「换壳不换行为」——这正是本次迁移最重要的验收条件。
# ---------------------------------------------------------------------------
qt_add_executable(alarm_state_test
    tests/alarm_state_test.cpp

    src/data/SampleData.h
    src/core/ChannelConfig.h
    src/core/ChannelConfig.cpp
    src/core/AlarmDetector.h
    src/core/AlarmDetector.cpp
)

target_include_directories(alarm_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(alarm_state_test PRIVATE
    Qt6::Core
    Qt6::Gui   # SampleData.h 的 alarmIndicatorColor() 使用 QColor
)

# 控制台程序：输出测试结果到标准输出，退出码即失败用例数。
# @note 关掉 AUTOMOC：本测试不含任何 Q_OBJECT 类，无需 moc。
set_target_properties(alarm_state_test PROPERTIES
    WIN32_EXECUTABLE FALSE
    AUTOMOC OFF
    AUTORCC OFF
)

enable_testing()
add_test(NAME alarm_state_test COMMAND alarm_state_test)

# ---------------------------------------------------------------------------
# 声光提示策略测试（第二梯队第 6、7 项）：静音 / 暂停 / 到期自动恢复。
#
# @note 需要 AUTOMOC：AlarmNotifier 含 Q_OBJECT（Q_PROPERTY + 信号）。
#       与 alarm_state_test 分开成独立目标，避免「状态机回归」与
#       「提示抑制策略」在同一个退出码里互相掩盖失败。
# ---------------------------------------------------------------------------
qt_add_executable(alarm_pause_test
    tests/alarm_pause_test.cpp

    src/data/SampleData.h
    src/core/AlarmNotifier.h
    src/core/AlarmNotifier.cpp
)

target_include_directories(alarm_pause_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 只链接 Core + Multimedia：QSoundEffect 需要 Qt6::Multimedia，
# 但测试会调用 setAudioEnabled(false)，因此无声卡环境也能跑。
target_link_libraries(alarm_pause_test PRIVATE
    Qt6::Core
    Qt6::Multimedia
)

set_target_properties(alarm_pause_test PROPERTIES
    WIN32_EXECUTABLE FALSE
    AUTOMOC ON
    AUTORCC OFF
)

add_test(NAME alarm_pause_test COMMAND alarm_pause_test)
