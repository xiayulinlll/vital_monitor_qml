# SOUP 清单与风险评估（IEC 62304 §8.1.2）

> 对应 `docs/医疗器材合规优化方向.md` **第五章「SOUP 管理」**、**第三梯队第 12 项**。
>
> **适用工程**：`vital_monitor_qml`。与 Widgets 版的差异见 §1.1
> （QML 版多一个 Qt Quick 栈与 QtMultimedia，少一个 Qt Widgets）。
>
> SOUP = Software Of Unknown Provenance，指**非自主开发、来源与质量不可完全掌控**
> 的软件组件（含框架、库、构建工具链）。IEC 62304 要求：识别、记录版本、
> 评估已知缺陷、制定升级策略。
>
> **版本信息采集日期**：2026-10-19。版本以本机开发环境实测为准（见 §4）。

---

## 1. SOUP 清单

| # | 组件 | 版本 | 用途 | 许可证 | 级别影响 | 已知缺陷 / 风险 | 缓解措施 |
|---|------|------|------|--------|---------|----------------|---------|
| S-1 | **Qt Core** | 6.2.4 (mingw_64) | 对象模型、信号槽、容器、文件 IO、CRC32 | LGPL-3.0 / 商业 | **C** | 线程亲和性误用导致崩溃；`QDateTime` 受系统时钟影响 | 严格 `moveToThread` + `QueuedConnection`；告警计时改用 `QElapsedTimer` 单调时钟（`SampleFrame::monotonicMs`） |
| S-2 | **Qt Gui** | 6.2.4 | `QPainter`、`QColor`、图像导出 | LGPL-3.0 / 商业 | **C** | 绘制在非 GUI 线程调用会崩溃 | 所有绘制限定在 GUI 线程（`ChartBridge::paint`） |
| S-3 | **Qt Qml / Qt Quick** | 6.2.4 | QML 引擎、场景图、`Canvas`、动画 | LGPL-3.0 / 商业 | **C** | 绑定循环导致卡死；`QQuickItem` 属性被覆盖会报 `FINAL property` 错误 | 组件属性命名避开 FINAL 属性（见 `ChannelModel::roleNames()` 的 `channelVisible`）；QML 侧不维护业务计时 |
| S-4 | **Qt Multimedia** | 6.2.4 | `QSoundEffect` 播放报警提示音 | LGPL-3.0 / 商业 | **C** | 无声卡 / 缺插件时 `status=Error`，报警变「哑巴」；Windows 音频服务被停用时同样失败 | `AlarmNotifier::audioAvailable` 检测并在界面显式提示「无音频输出，仅视觉报警」；提示音由代码合成 wav，不依赖外部资源 |
| S-5 | **MinGW-w64 (GCC)** | 11.2.0 | 编译器 / 运行时（libstdc++、libgcc） | GPL-3.0 with exception | B | 已知 ABI 兼容限制；异常 / RTTI 行为差异 | 固定工具链版本；`CMakePresets.json` 固化路径 |
| S-6 | **CMake** | 4.4.3（开发机实测） | 构建系统 | BSD-3-Clause | B | 生成器行为差异 | 锁定 preset；构建产物不进版本库 |
| S-7 | **Ninja** | 1.12.1（随 Qt 提供） | 构建执行器 | Apache-2.0 | B | — | 同上 |
| S-8 | **CTest** | 随 CMake | 自动化测试调度 | BSD-3-Clause | B | 本项目的测试为**自研断言**，仅用 CTest 做调度 | 测试目标不依赖 Qt Test（见 CMakeLists 注释） |
| S-9 | **Windows SDK / 系统 DLL** | 10.0.26200 | 平台 API（win32 后端）、音频设备 | 系统组件 | B | 系统更新可能改变行为 | 记录目标 OS 版本 |

### 1.1 与 Widgets 版 SOUP 清单的差异

| 组件 | Widgets 版 | QML 版 | 说明 |
|------|-----------|--------|------|
| Qt Widgets | 直接依赖 | **不依赖** | 界面全部由 QML 承载，顶层窗口是 `QtQuick.Window::Window` |
| Qt Qml / Qt Quick | 不依赖 | **直接依赖** | 场景图与动画带来额外的 GPU / 驱动相关风险 |
| Qt Multimedia | 不依赖（用 `QApplication::beep()`） | **直接依赖** | `QApplication::beep()` 属 Widgets，QML 版不可用 |

> **不属于 SOUP**：本项目自研的 `src/` 与 `qml/` 下全部代码（含 `AlarmDetector`、
> `AlarmNotifier`、`RingBuffer` 等），由本项目的变更控制流程管理
> （见 `docs/变更控制流程.md`）。

---

## 2. 关键风险评估

### R-SOUP-1：Qt 版本升级带来的行为变化（风险等级：中）

- **危害**：升级 Qt 后信号槽连接语义、QML 绑定刷新或 `QSoundEffect` 行为变化，
  可能导致告警横幅不刷新、提示音不响（→ 报警不可感知，C 级危害）。
- **控制措施**：
  1. 版本号写入本清单，升级必须走变更控制流程（提交 PR + 回归测试）；
  2. `tests/alarm_state_test`（13 用例）覆盖告警状态机全部迁移路径，
     `tests/alarm_pause_test`（10 用例）覆盖静音 / 暂停 / 自动恢复；
  3. UI 相关改动需人工目视验证（见 `docs/可用性工程文件.md` §7 检查表）。

### R-SOUP-2：`QDateTime` 使用墙钟，受 NTP 校时影响（风险等级：中，**已控制**）

- **危害**：系统校时回拨 1 小时，导致告警去抖计时、重复提示、升级计时失效，
  可能使报警永不触发或疯狂重报。
- **控制措施（已实现）**：`SampleFrame::monotonicMs` 由 `QElapsedTimer` 提供，
  `AlarmDetector::frameTimeMs()` 优先取单调时钟；墙钟仅用于显示与存档。
  回归用例见 `alarm_state_test` 用例 10「单调时钟时间基准」。

### R-SOUP-3：线程亲和性误用（风险等级：高）

- **危害**：在采集线程触碰 QML 对象或模型 → 未定义行为 / 崩溃，监护中断。
- **控制措施**：`VitalSignSimulator` 仅通过信号槽投递 `SampleFrame`（值语义），
  采集线程内不持有任何 QObject 模型；`AlarmDetector` / `AlarmNotifier` /
  所有 `*Model` 只在 GUI 线程运行。QML 侧只读 `Backend.qml` 的属性
  （由 C++ 3 Hz 写入），不直接触碰采集线程。

### R-SOUP-4：Qt Quick 场景图与显卡驱动相关崩溃（风险等级：中，QML 版新增）

- **危害**：老旧 / 虚拟机显卡驱动下场景图初始化失败，界面白屏或直接崩溃。
- **控制措施**：`windeployqt` 已随构建部署 `opengl32sw.dll`（软件渲染回退）；
  必要时可用 `QT_QUICK_BACKEND=software` 强制软件渲染。
  **未做**：未在无 GPU 的纯净虚拟机上实测该回退路径。

### R-SOUP-5：`QSoundEffect` 无声（风险等级：高，QML 版新增）

- **危害**：音频后端不可用（无音频设备 / 播放服务被停用 / 缺 multimedia 插件）
  时程序仍「正常运行」，但**没有任何声音报警**；医护会误以为设备在监护且无异常。
- **控制措施**：
  1. `AlarmNotifier::audioAvailable` 反映真实加载状态（`QSoundEffect::status`）；
  2. 界面在报警条右下角显示「⚠ 无音频输出，仅视觉报警」，
     状态栏显示「N 级报警 · 无音频」；
  3. 日志与启动提示中记录该降级状态。
- **残余风险**：视觉报警本身仍有效，且提示明确；但在强光 / 远距离场景下
  仅有视觉提示的可靠性未经验证（需可用性测试）。

### R-SOUP-6：MinGW 运行时 DLL 部署缺失（风险等级：低）

- **危害**：目标机缺少 `libstdc++-6.dll` 等导致程序无法启动。
- **控制措施**：`CMakeLists.txt` 中 `windeployqt` POST_BUILD 自动拷贝运行时
  （含 `Qt6Multimedia.dll`，构建日志可见 "Updating Qt6Multimedia.dll"）。

---

## 3. 升级策略

1. **冻结原则**：发布分支上的 SOUP 版本冻结；升级只能通过变更请求。
2. **升级流程**：
   `提出变更请求 → 评估已知 CVE → 本地构建 → ctest 全绿（两套）→ 人工目视回归 → 更新本清单版本号 → 记录到 CHANGELOG`
3. **已知漏洞跟踪**：见 `docs/SBOM与网络安全.md` §3（NVD 检索入口与记录表）。
4. **禁止事项**：不得使用未记录版本的 Qt / 编译器；不得直接替换 bin 目录下的 DLL。
5. **QML 版特别注意**：升级 Qt 时须**同时**复核 `qml/Theme.qml` 的节奏常量与
   `AlarmNotifier::soundIntervalMs()` 是否仍然一致 —— 两处硬编码相同数值，
   若只改一处会出现「横幅闪得快、声音却慢」的撕裂感。

---

## 4. 版本采集方式（可重现）

```bat
:: Qt 版本
D:\qt6\6.2.4\mingw_64\bin\qmake.exe -query QT_VERSION

:: 编译器版本
D:\qt6\Tools\mingw1120_64\bin\g++.exe --version

:: CMake / Ninja
cmake --version
D:\qt6\Tools\Ninja\ninja.exe --version
```

**2026-10-19 实测结果**：

| 项 | 实测值 |
|----|--------|
| Qt | `6.2.4` |
| GCC | `g++.exe (x86_64-posix-seh-rev3, Built by MinGW-W64 project) 11.2.0` |
| CMake | `cmake version 4.4.3` |
| Ninja | `1.12.1` |

---

## 5. 变更记录

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0 | 2026-10-19 | 首次建立（从 Widgets 版移植并重写）：登记 9 项 SOUP、6 条风险；新增 Qt Quick / Qt Multimedia 相关风险（R-SOUP-4、R-SOUP-5）；版本号改为本机实测 |

*文档性质：工程自查，非法规符合性认定。*
