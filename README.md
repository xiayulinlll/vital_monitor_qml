# vital_monitor —— 多通道生理信号趋势监测上位机

> 医疗监护趋势图上位机模块。技术栈：C++17 + Qt6 Quick（QML）+ QPainter + 多线程 + 环形缓冲区。

---

## 一、项目定位

多参数监护设备会持续采集心率、血氧、呼吸率、体温等生命体征数值，需要在 PC / 工控上位机
上绘制长时间趋势曲线，展示数小时内指标变化。如果直接在 UI 线程绘制大量历史采样点，
会造成界面卡顿、刷新延迟。

本项目基于 Qt6 Quick + QPainter 实现一套多通道生理趋势监测上位机：

- 多路体征趋势曲线同时绘制，不同指标独立颜色与量程；
- 实时数值面板，越限文字高亮；
- 体征越限自动在曲线上标记告警三角；
- 支持曲线横向缩放、鼠标拖拽平移、历史数据导入回放、画布截图导出；
- 后台子线程模拟持续采集，通过线程安全环形缓冲区管理历史数据，
  保证长时间连续运行 UI 不阻塞、内存占用恒定。

原型使用模拟生理数据，无需对接真实硬件。

---

## 二、目录结构

```
vital_monitor_qml/
├── CMakeLists.txt              # CMake 工程（Qt6 Core/Gui/Qml/Quick/Multimedia）
├── CMakePresets.json           # VS Code / 命令行构建预设（MinGW Debug/Release）
├── .vscode/                    # VS Code 配置：构建任务、调试、IntelliSense
├── qml/                        # ④ QML 界面层（控件基于 QtQuick 基础类型实现）
│   ├── Main.qml / Theme.qml / Backend.qml
│   ├── PatientBar.qml / ChartPanel.qml / ChartBridge.qml
│   ├── ChannelValueCard.qml / ChannelConfigPanel.qml
│   └── AlarmBanner.qml / AlarmListPanel.qml / LogPanel.qml …
└── src/
    ├── main.cpp
    ├── data/                   # ① 数据模拟采集层（后台子线程）
    │   ├── SampleData.h            #   采样帧 / 通道采样 / 告警事件数据结构
    │   └── VitalSignSimulator.h/.cpp   #   生理信号模拟器（QThread 内运行）
    ├── core/                   # ② 业务与数据层（主线程）
    │   ├── RingBuffer.h            #   线程安全定长环形缓冲区（模板）
    │   ├── ChannelConfig.h/.cpp    #   通道配置管理器（名称/颜色/量程/阈值/患者类型/报警优先级）
    │   ├── AlarmDetector.h/.cpp    #   告警判断模块（报警状态机 + 毫秒制延时 + SQI 门控 + 技术报警）
    │   ├── AlarmNotifier.h/.cpp    #   分级提示音 / 静音 / 暂停（单一计时真源）
    │   ├── CoordinateMapper.h/.cpp #   坐标变换计算器（数值→画布像素）
    │   ├── HistoryRecorder.h/.cpp  #   历史数据文件读写（回放/存档）
    │   └── AuditLogger.h/.cpp      #   审计追踪（配置变更 / 告警事件落盘 + CRC32 校验）
    ├── chart/                  # ③ QPainter 绘图内核层（主线程绘图）
    │   └── ChartBridge.h/.cpp      #   多通道趋势绘图内核（QQuickPaintedItem）
    ├── models/                 # ④ QML 数据模型层
    │   ├── ChannelModel.h/.cpp     #   通道数值（越限高亮 + SQI 指示）
    │   ├── AlarmModel.h/.cpp       #   活动告警列表（多优先级排序）
    │   └── LogModel.h/.cpp         #   状态 / 告警日志
    └── app/                    # ⑤ 业务宿主层（不含 UI 代码）
        └── MainWindowQml.h/.cpp    #   线程装配 + 帧分发 + 动作转发 + 审计落盘

tests/
└── alarm_state_test.cpp        # AlarmDetector 状态机自动化测试（13 个用例）

docs/
├── 医疗器材合规优化方向.md      # 合规差距分析（第一/二/三梯队需求来源）
└── 项目功能与代码文件说明.md    # 逐文件说明 + 功能清单 + 优化审核 + 项目质量评估
```

---

## 二点五、医疗器材合规增强（对应 `docs/医疗器材合规优化方向.md`）

已按优化文档的**第一梯队 / 第二梯队 / 第三梯队（自动化测试部分）**落地：

| 梯队 / 序号 | 能力 | 实现位置 |
|------------|------|---------|
| 一 · 1 | **报警声光指示**：分级提示音 + 横幅闪烁 + 状态栏常驻；颜色 / 闪烁 / 声节奏按严重度分级 | `AlarmBanner`、`alarmIndicatorColor()`（`SampleData.h`） |
| 一 · 2 | **修复「只报一次」缺陷**：改为**持续报警 + 确认（acknowledge）**，确认后条件仍成立会按 `repeatMs` 重报 | `AlarmDetector::detect/acknowledge` |
| 一 · 3 | **审计日志**：配置变更（旧值→新值）与告警事件落盘，行尾 CRC32 校验 | `AuditLogger`、`MainWindow::auditAlarm/auditConfigDiff` |
| 一 · 4 | **报警延时以毫秒计**：`alarmDelayMs` 取代帧数，采样率变化行为一致 | `ChannelConfig`、`AlarmDetector::detect` |
| 二 · 5 | **报警状态机**：`Active → Acknowledged / Cleared / Escalated / LatchedSilent`，超时未确认自动**升级** | `AlarmEvent::State`、`AlarmDetector` |
| 二 · 6 | **患者类型预设**（成人 / 小儿 / 新生儿）+ 报警限剖面，二级危险阈值随基线平移 | `ChannelConfigManager::loadProfile` |
| 二 · 7 | **信号质量（SQI）与伪迹抑制**：低 SQI 抑制生理报警、曲线灰色虚线、数值显示 `--` | `VitalSignSimulator`、`AlarmDetector`、`ChartBridge`、`ChannelModel` |
| 二 · 8 | **数据完整性**：丢帧累计检测、单调采样序号、时间戳规范 | `SampleData`、`AlarmDetector`、`VitalSignSimulator` |
| 二 · 9 | **技术报警通道**：传感器异常 / 导联脱落 / 信号不足 / 数据不连续，与生理报警分离 | `AlarmEvent::Category`、`AlarmDetector` |
| 二 · 10 | **每通道独立 Y 轴刻度**：左右分列、按通道配色、轴顶标注通道缩写 | `ChartBridge::drawChannelScales` |
| 二 · 6 | **报警声光指示**：分级提示音（危急 3 连响 / 900 ms）+ 横幅闪烁 + 状态栏常驻报警状态 | `AlarmNotifier`、`AlarmBanner.qml`、`Main.qml`（v1.2.0 新增） |
| 二 · 7 | **静音 / 暂停 / 确认**：静音只关声音；暂停声光全停但有倒计时、到期自动恢复并立即重报（危急 ≤ 30 s） | `AlarmNotifier`、`Backend.qml`、`AlarmBanner.qml`（v1.2.0 新增） |
| 二 · 1 | **报警优先级语义**：`AlarmPriority`（高/中/低）+ 二级阈值，**去掉「超出半个量程 = 严重」的临床无意义折算** | `AlarmPriority`、`AlarmDetector::levelForPriority` |
| 六 · 1 | **报警色码对齐 IEC 60601-1-8**：全控件共用同一色值，高优先级额外强调 | `SampleData.h`、`Theme.qml` |
| 三 · 15 | **自动化测试**：`alarm_state_test`（14 用例）+ `alarm_pause_test`（10 用例），退出码 = 失败数 | `tests/*.cpp`、`CMakeLists.txt` |
| 九 · 3 | **单位与量纲校验**：已知单位 / 量纲族 / 物理区间三重校验，配置面板提交前列出问题 | `ChannelConfig::validateUnit`（v1.2.1 新增） |
| 九 · 4 | **患者信息区**：床号 / 患者ID / 姓名（内联编辑）+ 患者类型，改动写日志与审计 | `qml/PatientBar.qml`、`MainWindowQml::recordPatientInfo`（v1.2.1 新增） |
| 九 · 5 | **演示模式标识**：常驻高对比橙色色块「⚠ 演示模式 · 模拟数据」 | `qml/PatientBar.qml`（v1.2.1 新增） |
| 六 · 3 | **配置二次确认**：改量程 / 报警限时列出「旧值 → 新值」，点「确认下发」才生效 | `qml/ChannelConfigPanel.qml::requestApply`（v1.2.1 新增） |
| 三 · 5 | **越界 / 伪迹角标**：不裁剪数值，以 `↑` / `↓` / `?` 提示 | `ChannelModel`、`qml/ChannelValueCard.qml`（v1.2.1 新增） |

### ⚠️ 告警键 / 计时缺陷修复（v1.1.1，与 Widgets 版同步）

以下三个缺陷**单元测试全绿时依然存在**，只能靠实机运行审计日志发现：

| # | 缺陷 | 现象 | 修复 |
|---|------|------|------|
| 1 | 告警持续时长丢失 | 审计日志出现 `持续时间=?s`，报警列表时长列显示「—」 | `Track::key` + `AlarmEvent::trackKey` 作为唯一定位手段，**禁止**用 `conditionKey()` 反查 |
| 2 | 条件键顺序写反 | 日志出现 `条件=1/0/LO`（通道在前） | `detect()` 的 `baseKey` 统一为 `类别/通道` |
| 3 | 确认非幂等 | 连点「确认」把审计日志淹没在重复记录里 | `acknowledge()` 跳过已确认的告警 |

> **键是跨模块契约**：条件键形如 `类别/通道/方向`（`0/2/HI`）；
> 技术报警另有一套键（`1/2/SQI`），只能通过 `AlarmEvent::trackKey` 定位。
> 「已持续 N 秒」的排序与显示全部走 `trackKey` → `activeDurationsMs()`。

实机验证（`build/vscode-mingw-debug/audit/audit_20261019.log`）：
条件键全部为 `0/<通道>/HI|LO`、技术报警为 `1/<通道>/SQI`，
4 条「告警确认」记录均带真实持续时长（2.3–4.1 s），`?s` 计数为 0，
同通道无重复确认记录。

> 第三梯队其余项已作为**过程文档**移植到本工程 `docs/`：
> `软件安全分级声明.md`、`SOUP清单.md`、`需求追溯矩阵.md`、`危害分析与FMEA.md`、
> `SBOM与网络安全.md`、`可用性工程文件.md`、`变更控制流程.md`。
> 这些文件按 **QML 版的真实代码**重写（不是从 Widgets 版照抄），
> 并如实标注了 QML 版尚未移植的能力（权限体系、采集看门狗、文件完整性校验）。

---

## 三、分层架构

```
┌──────────────────────────────────────────────────────────────┐
│  ⑤ 业务宿主层（main thread）                                  │
│     MainWindowQml：线程装配、帧分发、动作转发、审计落盘         │
├──────────────────────────────────────────────────────────────┤
│  ④ QML 界面层（qml/，独立控件自实现）                          │
│     Main / PatientBar / ChartPanel / ChannelValueCard /       │
│     AlarmBanner / AlarmListPanel / LogPanel / ChannelConfig…  │
├──────────────────────────────────────────────────────────────┤
│  ③ QML 数据模型层（main thread）                               │
│     ChannelModel / AlarmModel / LogModel                     │
├──────────────────────────────────────────────────────────────┤
│  ② 绘图内核层（ChartBridge : QQuickPaintedItem，主线程绘图）    │
│     曲线 / 网格 / 每通道刻度 / 告警标记 / 缩放平移              │
├──────────────────────────────────────────────────────────────┤
│  ① 业务与数据层（main thread）+ 采集模拟（worker thread）       │
│     RingBuffer / CoordinateMapper / AlarmDetector /           │
│     AlarmNotifier / HistoryRecorder / AuditLogger /           │
│     VitalSignSimulator                                        │
└──────────────────────────────────────────────────────────────┘
        ↑ Qt 信号槽 QueuedConnection（跨线程，自动排队）
```

**线程划分**：后台数据采集线程 + UI 主线程。绘图操作全部在 UI 主线程执行，
数据通过线程安全缓存隔离，采集线程绝不触碰任何 UI 对象。

---

## 四、核心业务流程

### 流程 1：实时趋势采集与绘制

```
后台线程生成模拟体征采样数据
   → 发信号 frameReady(SampleFrame)（QueuedConnection 跨线程）
   → 主线程槽 onFrameReady()：写入环形缓冲区 + 告警判断
   → 批量提交绘图内核 appendFrames()
   → 触发局部重绘，QPainter 绘制新增曲线段与告警标记
```

### 流程 2：曲线缩放平移交互

```
鼠标滚轮 / 拖拽 → ChartBridge 更新视口（采样序号范围）
   → 业务层坐标映射器重新计算 → 触发重绘，更新曲线显示
```

### 流程 3：历史数据回放

```
加载历史 CSV 文件 → HistoryRecorder::loadFrames()
   → 数据写入环形缓存 → 启动回放定时器（50ms）
   → 按时间顺序分批推送采样点，模拟实时采集效果
```

---

## 五、核心难点与工程解决方案

| # | 难点 | 工程解决方案 |
|---|------|-------------|
| 1 | 大量采样点绘制，整帧重绘 CPU 高 | **静态网格背景预渲染到 `QImage` 缓存**，`paint()` 直接贴图；实时模式下对绘图区做**局部重绘**（`update(rect)`），只刷新新增数据区域；主线程侧**批量提交**（每 2 帧合并一次）进一步降低重绘频率 |
| 2 | 持续采集数据，内存不断增长 | `RingBuffer<T>` **固定容量环形缓冲**，超容量自动淘汰最旧数据，内存占用上限恒定，适合 7×24 小时运行；日志模型同样限制最大行数 |
| 3 | 跨线程传递采样数据出现竞态崩溃 | 采集对象 `moveToThread()` 到工作线程，只生产数据；与主线程仅通过 **QueuedConnection 信号槽**通信；自定义类型通过 `qRegisterMetaType` 注册；环形缓冲使用 **QReadWriteLock** 读写保护（读可并发） |
| 4 | 多通道量程不同，坐标映射容易出错 | 每个通道在 `CoordinateMapper` 中**独立维护 Y 轴量程**，X 轴共享时间视口；绘图层只负责画点和线，**不参与业务计算** |
| 5 | 曲线缩放平移交互卡顿 | 交互时**只修改视口参数**（序号范围 / 跨度），不修改底层原始数据；拖拽过程中减少全量刷新，**鼠标释放后再做一次完整刷新** |

---

## 六、功能清单

1. 基于 QPainter 的趋势曲线绘制（`QQuickPaintedItem`），多通道生命体征同屏显示
2. 后台子线程模拟生理数据采集，线程安全环形缓存管理历史采样点，内存恒定
3. 支持多通道曲线独立配置颜色、量程、告警阈值，越限自动标记告警事件
4. 支持曲线横向时间缩放、纵向量程切换，鼠标拖拽平移查看历史趋势
5. 实时体征数值列表（`ChannelModel`），指标越限高亮提示
6. 支持历史数据文件导入回放，趋势画布截图导出保存
7. 绘制优化：背景图像缓存、局部区域重绘，降低 CPU 占用
8. 长时间稳定运行，适合医疗监护类上位机软件场景


---

## 七、环境要求

| 组件 | 版本 / 路径（本机实测） |
|------|------------------------|
| Qt | 6.2.4，MinGW 套件：`D:\qt6\6.2.4\mingw_64` |
| 编译器 | MinGW 11.2.0 x64：`D:\qt6\Tools\mingw1120_64\bin` |
| 构建工具 | CMake ≥ 3.19 + Ninja |
| C++ 标准 | C++17 |

> 若你的 Qt / 工具链安装路径不同，请同步修改 `CMakePresets.json` 中的
> `CMAKE_PREFIX_PATH`、`CMAKE_*_COMPILER`、`environment.PATH` 与
> `.vscode/settings.json`、`.vscode/launch.json` 中的对应路径。

---

## 八、构建与运行

### 方式 A：命令行（推荐先验证）

```bat
cd /d D:\qt6.2.4_project\vital_monitor

:: 需先把 MinGW 与 Qt 的 bin 加入 PATH（关键！否则 g++ 加载不了自身运行时 DLL）
set PATH=D:\qt6\Tools\mingw1120_64\bin;D:\qt6\6.2.4\mingw_64\bin;%PATH%

cmake --preset mingw-debug
cmake --build --preset mingw-debug
build\vscode-mingw-debug\vital_monitor.exe
```

Release 构建把 `mingw-debug` 换成 `mingw-release` 即可。

> `CMakeLists.txt` 中已配置 `windeployqt` 后置命令，构建后会自动把 Qt 运行时 DLL
> 拷贝到可执行文件目录，可直接双击运行。

### 方式 A-2：构建并运行自动化测试

```bat
cd /d D:\qt6.2.4_project\vital_monitor
set PATH=D:\qt6\Tools\mingw1120_64\bin;D:\qt6\6.2.4\mingw_64\bin;%PATH%

:: 构建测试目标（与主程序共用业务层代码，避免测试与实现漂移）
cmake --preset mingw-debug
cmake --build build\vscode-mingw-debug --target alarm_state_test

:: 运行：控制台打印每个用例结果，退出码 = 失败用例数（0 = 全部通过）
build\vscode-mingw-debug\alarm_state_test.exe

:: 也可通过 CTest 运行
ctest --test-dir build\vscode-mingw-debug
```

测试覆盖 8 组关键行为：

| 用例 | 验证内容 |
|------|---------|
| 1 | 持续越限必须**反复上报**（回归验证「只报一次」缺陷已修复） |
| 2 | 确认（acknowledge）后条件仍成立 → 按 `repeatMs` **重新提示**，不永久静默 |
| 3 | 超时未确认 → 严重度**升级**（Escalated） |
| 4 | 毫秒制延时**去抖**抑制瞬时毛刺 |
| 5 | 条件恢复 → 产生 **Cleared** 事件并清空活动告警 |
| 6 | **SQI 过低** → 抑制生理报警、改发技术报警 |
| 7 | **丢帧累计**超阈值 → 技术报警 |
| 8 | **报警优先级来自配置**（而非「超出量程一半」折算） |

### 方式 B：VS Code

1. 用 VS Code 打开文件夹 `D:\qt6.2.4_project\vital_monitor`；
2. 安装推荐扩展（弹出的推荐列表，或参考 `.vscode/extensions.json`）：
   `ms-vscode.cpptools`、`ms-vscode.cmake-tools`、`twxs.cmake`；
3. `Ctrl+Shift+P` → **CMake: Select Configure Preset** → 选择 `mingw-debug`；
4. `Ctrl+Shift+B` 构建（默认任务 `CMake: Build (mingw-debug)`）；
5. `F5` 启动调试（配置 `Debug vital_monitor (MinGW / gdb)`，gdb 断点可用）。

---

## 九、操作说明

| 操作 | 说明 |
|------|------|
| **▶ 开始采集** | 启动后台线程，10 Hz 生成模拟体征数据并实时绘制 |
| **■ 停止采集** | 停止数据产生，界面保留已绘制曲线 |
| **视口跨度** | 下拉框切换显示窗口：10 秒 / 30 秒 / 1 分钟 / 3 分钟 / 10 分钟 |
| **实时跟随** | 开启后视口右端始终对齐最新数据；关闭即进入历史浏览模式 |
| **鼠标滚轮** | 在绘图区滚动，横向缩放时间轴（只改视口，不动原始数据） |
| **鼠标左键拖拽** | 平移查看历史趋势，自动切换历史浏览模式，松手后完整刷新 |
| **清空** | 清空历史缓冲、告警记录与画布（声音与暂停状态同步复位） |
| **导入历史** | 载入 CSV 历史数据并自动按实时节奏回放（文件放 `<应用目录>/imports`） |
| **导出截图** | 把当前趋势画布保存为 PNG / JPG（写到 `<应用目录>/exports`） |
| **导出数据** | 把环形缓冲中的历史数据导出为 CSV |
| **通道参数配置** | 修改每路通道的显示开关、颜色、量程上下限、告警上下限，点「应用配置」生效 |
| **患者类型** | 下拉切换成人 / 小儿 / 新生儿，自动加载对应报警限剖面（第二梯队第 6 项） |
| **确认告警** | 报警条「确认」（或 `Ctrl+A`）：签收全部活动告警，声音停止；若条件仍成立会按 `repeatMs` 重新提示 |
| **静音** | 报警条「🔊 静音」（或 `M`）：**只关声音**，横幅继续闪烁（IEC 60601-1-8 要求视觉提示保留） |
| **暂停** | 报警条「⏸ 暂停」（或 `P`）：声音 + 闪烁**全部**停止，横幅变灰+斜纹并显示「恢复 N s」，**到期自动恢复** |
| **空格** | 开始 / 停止采集 |

### 报警声光提示（第二梯队第 6、7 项）

| 能力 | 实现 | 说明 |
|------|------|------|
| 分级提示音 | `AlarmNotifier`（`QSoundEffect`） | 危急 / 升级：900 ms **3 连响**；一般：1800 ms 1 响；提示：3000 ms 1 响 |
| 横幅闪烁 | `AlarmBanner.qml` + `Theme.blinkIntervalMs()` | 与声音节奏一一对应，暂停时强制停止 |
| 状态栏常驻 | `Main.qml` | 「无报警」/「N 级报警 M 条」/「已静音」/「无音频」/「已暂停 N s」 |
| 静音 Mute | `AlarmNotifier::setMuted()` | 只关声音，视觉提示保留 |
| 暂停 Pause | `AlarmNotifier::startPause()` | 危急 ≤ **30 s**、其余 ≤ **120 s**（C++ 强制收紧）；倒计时每秒可见；到期自动恢复并立即重报 |
| 无音频降级 | `AlarmNotifier::audioAvailable` | 横幅显示「⚠ 无音频输出，仅视觉报警」，不假装能响 |
| 可追溯 | `AuditLogger` | 暂停 / 恢复 / 静音均落盘（含时长与活动告警数） |

> **为什么提示音在 C++ 而不在 QML**：本工程是 `QGuiApplication`，不能调用
> `QApplication::beep()`；更关键的是，若在 QML 里用 `Timer` 控制节奏，就会与
> `AlarmDetector` 的 `alarmRepeatMs` / `alarmEscalateMs` 形成两套计时真源，
> 最终表现为「横幅在闪、声音却没跟上」。
>
> **未做**：界面按钮点击无 GUI 自动化测试，暂停 / 静音的实际观感需目视回归
> （清单见 `docs/变更控制流程.md` §6）。

### 报警状态机（第二梯队第 5 项）

```
                     条件持续成立（越过 alarmDelayMs）
        ┌──────────────────────────┐
        ▼                          │
   ConditionActive ──► Active ─────┤
                        │  │       │
          用户确认       │  └───────┘ 按 repeatMs 重复提示
                        ▼
                  Acknowledged ────► 条件仍成立 → LatchedSilent（周期重报）
                        │
        超过 alarmEscalateMs 未确认
                        ▼
                    Escalated（最高严重度，不可静默）
                        │
        数值回到正常范围 ▼
                    Cleared（撤下报警条）
```

### 报警优先级（对应优化文档 2.1 第 1 项）

报警等级由通道**显式配置**的 `AlarmPriority` 与二级（危及生命）阈值决定，
不再使用「超出阈值半个量程即严重」的折算方式（该方式与临床无关）。

| 优先级 / 条件 | 报警等级 | 颜色（IEC 60601-1-8） |
|--------------|---------|---------------------|
| 低优先级 | `Info` | 青 `#2A9EB0` |
| 中优先级 | `Warning` | 琥珀 `#D69A09` |
| 高优先级 | `Critical` | 红 `#D32F2F` |
| 突破二级阈值（危及生命） | `Critical` | 红 + 白框强调 |
| 超时未确认 | `Escalated` | 红 + 白框强调 |

### 默认通道

| 通道 | 单位 | 量程 | 告警下限 | 告警上限 | 优先级 | 二级（危及生命） |
|------|------|------|---------|---------|--------|----------------|
| 心率 HR | bpm | 40 ~ 180 | 60 | 110 | 中 | < 50 或 > 150 |
| 血氧 SpO2 | % | 85 ~ 100 | 94 | —（不设上限） | 高 | < 88 |
| 呼吸率 RR | rpm | 0 ~ 40 | 8 | 24 | 中 | < 5 或 > 32 |
| 体温 Temp | ℃ | 34 ~ 42 | 35.5 | 37.5 | 低 | < 35 或 > 39 |

> 切换患者类型（成人 / 小儿 / 新生儿）会按预设剖面覆盖量程与报警限，
> 二级阈值随各类型生理基线等比平移，避免把成人危险界值套用到新生儿。

### 历史数据文件格式

CSV 变体（`HistoryRecorder`）：

```
# vital_monitor history v1
sampleIndex,timestampMs,channelId,value
0,1730000000000,0,78.5
0,1730000000000,1,98.2
0,1730000000000,2,16.0
0,1730000000000,3,36.6
1,1730000000100,0,79.1
...
```

同一 `sampleIndex` 的多行会被合并为一帧。

---

## 十、代码要点索引

| 关注点 | 文件 | 关键设计 |
|--------|------|---------|
| 线程安全环形缓冲 | `src/core/RingBuffer.h` | 模板 + `QReadWriteLock`，`rangeByKey` 二分定位支持局部取数 |
| 跨线程生产者 | `src/data/VitalSignSimulator.cpp` | `moveToThread` + `QTimer` 在子线程触发，只 `emit` 不碰 UI |
| 线程装配 | `src/app/MainWindowQml.cpp` → `setupThreading()` | `connect` 自动排队；`QThread::finished` → `deleteLater` 安全析构 |
| 绘制优化 | `src/chart/ChartBridge.cpp` | `QQuickPaintedItem` 内 QPainter 绘制；高频数据 C++→C++ 直连，不经 QML 引擎 |
| 局部重绘 | `src/chart/ChartBridge.cpp` → `appendFrames()` | 每 2 帧批量提交一次，降低重绘频率 |
| 声光提示单一真源 | `src/core/AlarmNotifier.cpp` | 声音节奏 / 静音 / 暂停计时全在 C++，QML 只读状态（避免两套时钟） |
| 无音频降级 | `src/core/AlarmNotifier.cpp` → `audioAvailable` | `QSoundEffect::status` 失败时显式提示「仅视觉报警」 |
| 坐标映射 | `src/core/CoordinateMapper.cpp` | X 用采样序号视口，Y 按通道独立归一化并翻转 |
| 报警状态机 | `src/core/AlarmDetector.cpp` | Active / Acknowledged / Escalated / Cleared 四态迁移；毫秒制延时；升级与重复提示使用**独立计时锚点** |
| 报警时基一致性 | `src/core/AlarmDetector.cpp` → `acknowledge(key, nowMs)` | 确认操作必须传入与 `detect()` 相同的 `timestampMs` 时基，否则重报 / 升级计时错乱 |
| 报警优先级 | `src/core/AlarmDetector.cpp` → `levelForPriority()` | 等级由 `AlarmPriority` + 二级阈值决定，不用「半量程」折算 |
| 统一报警色码 | `src/data/SampleData.h` → `alarmIndicatorColor()` + `qml/Theme.qml` | 两处取值完全一致；曲线 / 横幅 / 数值卡片共用（IEC 60601-1-8） |
| 交互不扰动数据 | `src/chart/ChartBridge.cpp` → 滚轮 / 拖拽处理 | 只改视口起点与跨度，不动环形缓冲 |

---

## 十一、可扩展优化方向

1. **数据持久化**：定时把环形缓冲中的历史采样追加写入本地文件，实现断电续存。
2. **光标探针**：控件已预留 `cursorIndexChanged(quint64, bool)` 信号，可在
   `mouseMoveEvent` 中获取悬停采样序号，据此绘制垂直游标并显示该时刻各通道数值。
3. **降采样绘制**：当视口跨度远大于画布像素宽度时（例如 10 分钟窗口），
   可按像素列做 min/max 聚合降采样，进一步降低曲线路径构建开销。
4. **报警条高级特性**：报警列表已按优先级 / 持续时长排序并显示持续时长，
   静音 / 暂停 / 确认与分级提示音已落地（v1.2.0）；
   仍缺「报警历史归档」与「暂停超时未恢复的升级提醒」。
5. **用户身份与权限**：查看 / 配置 / 管理分级，关键操作（改报警限）需授权
   （IEC 81001-5-1）。**QML 版尚未移植**，见 `docs/危害分析与FMEA.md` H-10；
   目前改报警限已有**二次确认**（v1.2.1），但无角色阻断。
6. **文件完整性与开机自检**：导出文件的 SHA-256 伴生清单校验、
   启动自检结论显示、采集线程看门狗 —— 均只在 Widgets 版实现，
   QML 版未移植（FMEA H-11）。

> 已完成项（声光报警与分级提示音、静音 / 暂停、报警状态机、审计日志、SQI、
> 技术报警、患者类型预设、每通道 Y 轴、IEC 色码、报警优先级语义、自动化测试、
> 患者信息区与演示模式标识、配置二次确认、单位量纲校验、越界 / 伪迹角标）见
> [二点五、医疗器材合规增强](#二点五医疗器材合规增强对应-docs医疗器材合规优化方向md)。

> **从医疗器械角度的完整优化清单**（功能、规范性、安全性、合规性，含优先级路线图）
> 见 [`docs/医疗器材合规优化方向.md`](docs/医疗器材合规优化方向.md)。

---

## 十二、许可

示例工程项目，仅用于学习与技术演示。
