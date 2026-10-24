# 变更日志（CHANGELOG）

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循语义化版本。

> **与 `vital_monitor`（Widgets 版）的关系**：两个工程共用同一套
> `src/data` 与 `src/core`（逐字复用），因此**告警核心的缺陷与修复必须同步**。
> 本文件只记录 QML 版自身的变更；核心层的缺陷背景见 Widgets 版
> `docs/CHANGELOG.md` v1.1.1。

---

## [1.2.1] — 2026-10-19

**主题**：补齐优化文档**第九章「界面显示与信息呈现」**中三处"文档标 ✅、代码却没有"
的缺口。本次改动全部来自**逐条比对文档与源码**后的发现 ——
此前 `医疗器材合规优化方向.md` 把 `PatientBarWidget`（Widgets 版类）、
`validateUnit()`、`confirmCriticalChanges()` 等**只存在于 Widgets 版**的实现
误标为 QML 版已完成。

### 新增 — 患者信息区与演示模式标识（9.1 第 4、5 项）

- `qml/PatientBar.qml`：床号 / 患者ID / 姓名（内联点击编辑，回车提交、Esc 取消）
  + 患者类型（跟随配置面板，单向绑定避免双真源）
  + 常驻橙色色块「⚠ 演示模式 · 模拟数据」（点击可在日志/审计中再声明一次）
  + 「活动报警 N / 已确认 M」计数
- `MainWindowQml::recordPatientInfo()`：患者上下文变更写日志 + 写审计
  （`Config / Info`，「这条报警属于谁」必须可追溯）
- `MainWindowQml::logNotice()`：QML 请求记一条提示日志（日志模型在 C++，
  QML 不直接拼装内部结构）

### 新增 — 配置二次确认（6.1 第 3 项）

- `ChannelConfigPanel.qml::requestApply()`：第一次点击只生成
  「旧值 → 新值」清单（量程 / 报警上下限），点「确认下发」才真正下发；
  无关键变更时直接提交，不增加无谓点击
- 面板高度随确认区自适应（否则差异清单被裁掉，等于没有确认）

### 新增 — 单位与量纲校验（9.1 第 3 项）

- `ChannelConfig::validateUnit()` + `UnitIssue` / `unitIssueName()` /
  `isKnownUnit()` / `isValuePlausibleInUnit()`：三重校验
  （已知单位 / 量纲族与通道名是否一致 / 量程是否物理可能）
- 配置面板在二次确认区列出单位问题（不阻塞提交，但不允许静默）
- `alarm_state_test` **用例 14** 覆盖 12 项断言

### 修复 — 越界与伪迹角标缺失（3.1 第 5 项）

文档声称"越限以 `↑`/`↓` 角标呈现"，但 QML 版 `ChannelValueCard` 并无该逻辑。
- `ChannelModel` 新增 `outOfRange` / `artifact` 角色并在 `applyFrame()` 计算
- `ChannelValueCard.qml`：越界显示 `↑`/`↓`，伪迹显示 `?` 并转橙色
  （用符号而非仅颜色，色觉障碍者同样可读）

### 修复 — 首屏声光状态为 undefined

`Backend.qml::syncFromHost()` 会读 `host.alarmMuted` / `alarmPaused` / …，
但 `MainWindowQml` **没有**这些 `Q_PROPERTY`，导致启动日志出现
`Cannot assign [undefined] to bool`，且首屏按钮状态与实际不一致。
- `MainWindowQml` 补齐 7 个 `Q_PROPERTY`（静音 / 暂停 / 剩余 / 上限 / 声卡 / 计数 / 待提示等级）

### 修复 — 运行时 QML 警告

- `PatientBar.qml` 误用 `Theme.panel`（不存在）→ 改为 `Theme.panelBg`；
  该错误会打印 `Unable to assign [undefined] to QColor`

### 验证

- 构建：`BUILD_EXIT=0`
- 测试：`ctest` **2/2 通过**（`alarm_state_test` 14 用例 + `alarm_pause_test` 10 用例）
- 运行时：`vital_monitor_qml.exe --selftest` → `SELFTEST PASS: frames=24 history=24 uptime=2s alarms=2`，
  **stdout/stderr 无任何 QML 警告**
- QML 模块：`qmldir` 已含 `PatientBar 1.0`

### 已知限制（未做，如实记录）

- 患者信息不做持久化，仅会话内有效；无账号体系，审计 actor 仍为空
- 单位校验的 QML 侧提示表与 C++ `kUnitRanges` 需手工保持一致
  （权威判定在 C++，QML 只负责提前提示）
- 配置锁定开关、`UserRole` 权限、`FileIntegrity` 哈希校验、采集看门狗、
  开机自检仍**未从 Widgets 版移植**（FMEA H-10 / H-11）
- 新增界面元素无 GUI 自动化测试（仅代码审查 + 运行时无警告）

---

## [1.2.0] — 2026-10-19

**主题**：落地优化文档**第二梯队第 6、7 项** —— QML 版此前**完全没有声音、
也没有静音 / 暂停能力**（只有横幅闪烁）。本次补齐「报警声光指示」与
「报警静音 / 暂停 / 确认」，并配以可见倒计时、自动重新激活策略与
两套自动化测试。

> **为什么必须做**：只有视觉提示的报警，在医护背对屏幕、环境光过强时
> 等于「没有报警」。而一旦引入静音 / 暂停，就同时引入了「永久抑制」的新风险，
> 因此两者必须同时落地并互相约束（见 `docs/危害分析与FMEA.md` H-6 / H-7 / H-8）。

### 新增 — 分级提示音（`AlarmNotifier`）

- **新文件**：`src/core/AlarmNotifier.h/.cpp`，负责「听觉 + 抑制状态」，是
  声音节奏与暂停计时的**唯一真源**（QML 侧不再维护任何计时器，避免与
  `AlarmDetector::alarmRepeatMs` 形成两套时钟）。
- **分级节奏**：危急 / 升级 → 900 ms **3 连响**；一般 → 1800 ms 1 响；
  提示 → 3000 ms 1 响。与 `Theme.blinkIntervalMs()` 的视觉节奏一一对应。
- **首声立即响**：不等第一个周期（「发现延迟」优先于「节奏整齐」）。
- **升级立即变节奏**：等级变化时立刻按新节奏补一声，不再等旧周期走完。
- **音源由代码合成**：运行时生成 16-bit PCM wav（800 Hz / 1000 Hz，两端 5 ms
  淡入淡出防「咔」声），避免部署漏拷音源。
- **为什么不用 `QApplication::beep()`**：那是 QtWidgets 接口，本工程是
  `QGuiApplication`（刻意不链接 Widgets），故改用 `QSoundEffect`（QtMultimedia）。

### 新增 — 静音（Mute）

- `AlarmNotifier::setMuted()` / `Backend.mute(on)`；横幅「🔊 静音」按钮（开启后
  变深底「🔇 已静音」），快捷键 `M`。
- **只关声音**，横幅继续按优先级闪烁 —— 静音 ≠ 暂停 ≠ 关闭报警。
- 静音 / 取消静音均写日志与审计。

### 新增 — 暂停（Pause）+ 倒计时 + 自动恢复

- `AlarmNotifier::startPause(seconds)` / `cancelPause()`；横幅「⏸ 暂停 ↔ ⏵ 恢复 N s」，
  快捷键 `P`。
- **时长上限强制收紧**：危急 / 升级 ≤ **30 s**，其余 ≤ **120 s**（由 C++ 校验，
  QML 传再大的值也无效 —— 界面不能成为绕过规范的入口）。
- **每秒倒计时**（`Q_PROPERTY pauseRemainingSeconds` → `Backend.alarmPauseRemaining`）。
- **到期自动恢复**：归零即解除暂停；若仍有未处理告警**立即重新发声**，
  若条件已消失则保持安静（恢复不制造声音）。
- **暂停外观**：横幅转中性灰 + 45° 斜纹 + 强制停止闪烁，**不隐藏** ——
  隐藏等于关掉报警，保持全彩又会被误读为「正在正常提示」。
- **无告警时暂停不生效**：返回 0 并明确提示「当前没有未确认的告警」，
  避免出现「按了没反应」的假状态。
- 暂停开始 / 恢复写日志与审计（含暂停时长、活动告警数、最高等级）。

### 新增 — 状态栏常驻报警状态

- `qml/Main.qml` 状态栏新增徽标，按优先级显示：
  「无报警」/「N 级报警 M 条」/「N 级报警 · 已静音」/「N 级报警 · 无音频」/
  「报警已暂停 N s」（暂停优先级最高）。
- 满足 IEC 60601-1-8「报警系统状态须始终可见」。

### 新增 — 无音频降级提示

- `AlarmNotifier::audioAvailable` 反映 `QSoundEffect::status` 的真实结果。
- 不可用时：横幅右下角「⚠ 无音频输出，仅视觉报警」+ 状态栏「无音频」+ 日志记录。
  绝不假装「能响」。

### 新增 — 自动化测试 `alarm_pause_test`（10 个用例）

| 用例 | 内容 |
|------|------|
| 1 | 无告警时暂停不生效 |
| 2 | 一般告警可暂停 120 s |
| 3 | 危急 / 升级被收紧到 30 s |
| 4 | 暂停期间切换静音不影响暂停与倒计时 |
| 5 | 暂停 1 s 到期 → 自动恢复并重新发声 |
| 6 | 静音只关声音、跨周期不发声、取消后立即补声 |
| 7 | 提前结束暂停立即恢复 |
| 8 | 告警已消失时恢复不制造声音 |
| 9 | 首声立即响（危急 3 连响 / 一般 1 响） |
| 10 | 等级变化时节奏立即切换（升级后 2 s 内 ≥ 2 轮 × 3 连响） |

> 用例 10 **先失败后通过**：首版 `syncSoundTimer()` 在等级变化时只调了
> `start()` 而没有补一声，导致「升级后要等旧周期走完才变急促」。
> 这类缺陷单元测试原样写出来就能抓到，属于「先写失败用例」的实践。

### 变更 — 构建

- `CMakeLists.txt`：`find_package(... Multimedia)` + 链接 `Qt6::Multimedia`；
  新增 `alarm_pause_test` 目标与 `add_test`（`ctest` 现为 2 个测试）。
- `windeployqt` 已自动部署 `Qt6Multimedia.dll`（构建日志可见）。
- `project(... VERSION 1.1.1)` → **`1.2.0`**（新增功能，次版本递增）。

### 变更 — 文档

新增 7 份第三梯队过程文件（内容按 QML 版重写，非照抄 Widgets 版）：
`docs/软件安全分级声明.md`、`docs/SOUP清单.md`、`docs/危害分析与FMEA.md`、
`docs/SBOM与网络安全.md`、`docs/需求追溯矩阵.md`、`docs/可用性工程文件.md`、
`docs/变更控制流程.md`。

### 验证

```
cmake --build --preset mingw-debug        → BUILD_EXIT=0
ctest --test-dir build/vscode-mingw-debug → 100% tests passed, 2/2
  alarm_state_test   13 用例通过
  alarm_pause_test   10 用例通过
启动实机运行 8 s：进程存活，stderr 无 QML / ReferenceError 输出
```

### 已知限制（诚实记录）

1. **界面按钮点击无 GUI 自动化测试** —— 暂停 / 静音的策略层有单测，
   但按钮是否存在、颜色是否正确仍需目视回归（见 `docs/变更控制流程.md` §6）。
2. **未在真实无声卡环境验证**：无音频提示的 UI 分支靠代码审查与日志确认。
3. **以下能力仍未移植**：权限体系（FMEA H-10）与采集看门狗（H-11），
   在 `docs/危害分析与FMEA.md` 中仍标为不可接受风险。

---

## [1.1.1] — 2026-10-19

**主题**：同步 Widgets 版 v1.1.1 的告警键 / 计时缺陷修复，并补齐 QML 版
缺失的**数据契约**（单调时钟、伪迹标记），使「换壳不换行为」真正成立。

> 这三个缺陷都是**跑起来才暴露**的：单元测试全绿，但审计日志里的
> `持续时间=?s` 和 `条件=1/0/LO` 一眼就看得出不对。

### 修复 — 告警持续时长丢失（`AlarmEvent::trackKey`）

- **现象**：审计日志「告警确认」记录出现 `持续时间=?s`；报警列表时长列显示「—」。
- **根因**：技术报警在 `m_tracks` 中的键是 `"1/<通道>/SQI"`，而下游用
  `AlarmEvent::conditionKey()`（生成 `"1/<通道>/HI"`）去查时长表。
  两者**属于不同键空间**，永远查不到。
- **修复**：`Track` 保存真实键 `key`，`makeEvent()` 回填 `AlarmEvent::trackKey`；
  `MainWindowQml`（审计）与 `AlarmModel`（排序 / 时长列）统一改用 `trackKey`。
  `activeDurationsMs()` 不再跳过 `onsetMs == 0` 的记录（返回 0 而非缺失）。
- **QML 侧新增**：`AlarmModel` 增加 `trackKey` / `durationMs` / `durationText`
  三个角色，`AlarmListDelegate.qml` 显示「持续 x.x s」。

### 修复 — 条件键顺序写反（`类别/通道` vs `通道/类别`）

- **现象**：日志中通道 1 的下限告警记为 `条件=1/0/LO`——形如「通道/类别」。
- **根因**：`detect()` 的 `baseKey` 写作 `.arg(cfg.id).arg(0)`，与
  `AlarmEvent::conditionKey()` 约定的「类别/通道」**顺序相反**。
- **修复**：`baseKey` 改为 `类别/通道`，并注释声明「键是跨模块契约」。

### 修复 — 告警确认非幂等（审计日志被连点淹没）

- **根因**：`AlarmDetector::acknowledge()` 对已确认的告警仍返回事件。
- **修复**：跳过 `t.acknowledged == true` 的记录。确认是**一次性事件**；
  条件仍成立时的重复提示由 `updateCondition()` 的 `repeatMs` 负责。

### 补齐 — QML 版缺失的数据契约（修复的前提）

Widgets 版早已具备、QML 版此前**完全没有**的字段，导致上面的修复在
QML 版无法落地（没有单调时钟就谈不上「同一时基」，没有伪迹标记就谈不上
「伪迹不参与生理报警」）：

| 新增 | 位置 | 作用 |
|------|------|------|
| `SampleFrame::monotonicMs` | `SampleData.h` | 告警计时用单调时钟，NTP 校时不影响去抖 / 升级 |
| `ChannelSample::artifact` | `SampleData.h` | 伪迹样本不参与生理报警（也不再误报为技术报警） |
| `AlarmDetector::frameTimeMs()` | `AlarmDetector.h/.cpp` | 统一取时基（单调优先、缺失退回墙钟） |
| `VitalSignSimulator` 的 `QElapsedTimer` | `VitalSignSimulator.h/.cpp` | 采集侧填充 `monotonicMs`，丢帧改按单调间隔推算 |
| `AlarmModel` 时长角色 | `AlarmModel.h/.cpp` | 报警列表显示并按时长排序（未确认优先） |

### 测试

- `alarm_state_test`：8 → **13 个用例**（补齐用例 9–13，与 Widgets 版断言一致）
- `ctest`：1/1 通过
- **运行时验证**（实机审计日志）：条件键均为 `0/<通道>/HI|LO`、
  技术报警为 `1/<通道>/SQI`；4 条「告警确认」记录均带真实持续时长，
  `持续时间=?s` 计数为 0；同通道无重复确认记录。

---

## [1.0.0] — 2026-10-12

**主题**：QML 版首版（Widgets 版迁移）。

- QML 界面（qt-quick 基础类型实现控件，无 QtQuick.Controls 依赖）+ QPainter 绘图内核
- 模型层：`ChannelModel` / `AlarmModel` / `LogModel`
- 业务宿主：`MainWindowQml`（数据面 / 控制面分离，高频数据不跨越 QML 引擎）
- 复用 Widgets 版 `src/data` 与 `src/core`，`alarm_state_test` 8 个用例
