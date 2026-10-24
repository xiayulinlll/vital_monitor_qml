// 单元测试：验证报警声光提示的「静音 / 暂停 / 自动恢复」策略。
//
// 对应优化文档第二梯队第 6、7 项，以及 IEC 60601-1-8 的三条硬约束：
//   1. 暂停必须有明确时长，不得无限期静默；
//   2. 到期自动恢复，恢复时若仍有未处理告警立即重新提示；
//   3. 暂停状态必须可见（剩余秒数可查询 → QML 据此渲染倒计时）。
//
// 为什么单独成目标：`alarm_state_test` 只关心「报警是否成立」，
// 本测试只关心「提示是否被正确抑制 / 恢复」。两者互不依赖，
// 任一改动都不会让另一套断言出现假阳性。
//
// 用例：
//   1  无告警时暂停不生效（返回 0）        2  一般告警暂停 120 s
//   3  危急报警暂停上限收紧到 30 s          4  暂停期间静音切换不改变暂停
//   5  到期自动恢复并重新起声               6  静音只关声音、不影响状态
//   7  取消暂停立即恢复                     8  告警消失后恢复不发声
//   9  首声立即响（不等第一个周期）        10  严重度变化时节奏随之改变
//
// 运行：
//   cmake --build --preset mingw-debug --target alarm_pause_test
//   build\vscode-mingw-debug\alarm_pause_test.exe
#include "core/AlarmNotifier.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

namespace {

int g_fail = 0;

void check(bool cond, const QString &what)
{
    if (!cond) {
        ++g_fail;
        qWarning().noquote() << "  [FAIL]" << what;
    } else {
        qInfo().noquote() << "  [ OK ]" << what;
    }
}

/// 驱动事件循环 ms 毫秒（让 QTimer 真正触发）。
void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// 创建一个关闭真实音频输出的提示器（CI / 无声卡环境）。
///
/// @note `beepCount` 仍会累加：它统计「策略上要求发声的次数」，
///       因此无需声卡即可验证节奏与抑制逻辑。
AlarmNotifier *makeNotifier()
{
    auto *n = new AlarmNotifier();
    n->setAudioEnabled(false);
    return n;
}

// ---------------------------------------------------------------------------

void case1_noAlarmNoPause()
{
    qInfo().noquote() << "== 用例 1：无告警时暂停不生效 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Info, /*hasPending=*/false);
    const int secs = n->startPause(120);

    check(secs == 0, "无告警时 startPause() 返回 0");
    check(!n->isPaused(), "未进入暂停状态");
    check(n->pauseRemainingSeconds() == 0, "剩余秒数为 0");

    delete n;
}

void case2_pauseBaseRate()
{
    qInfo().noquote() << "== 用例 2：一般告警暂停 120 s 上限 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Warning, true);
    const int secs = n->startPause(120);

    check(secs == 120, "一般告警可暂停 120 s");
    check(n->isPaused(), "处于暂停状态");
    check(n->pauseRemainingSeconds() == 120, "剩余秒数=120");
    check(!n->shouldBeep(), "暂停期间不应发声");

    delete n;
}

void case3_criticalPauseClamped()
{
    qInfo().noquote() << "== 用例 3：危急报警暂停被收紧到 30 s ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Critical, true);
    const int secs = n->startPause(120);

    check(secs == AlarmNotifier::kCriticalPauseMaxSeconds,
          "请求 120 s 被收紧为 30 s（危急不可长时间静默）");
    check(n->pauseMaxSeconds() == 30, "危急下 pauseMaxSeconds()=30");

    // 升级后的告警同样按危急处理。
    AlarmNotifier *n2 = makeNotifier();
    n2->updateTopLevel(AlarmEvent::Escalated, true);
    check(n2->startPause(600) == 30, "Escalated 同样收紧到 30 s");
    check(n2->pauseMaxSeconds() == 30, "Escalated 下 pauseMaxSeconds()=30");

    delete n;
    delete n2;
}

void case4_muteDuringPause()
{
    qInfo().noquote() << "== 用例 4：暂停期间切换静音不改变暂停 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Warning, true);
    n->startPause(10);

    n->setMuted(true);
    check(n->isMuted(), "静音已开启");
    check(n->isPaused(), "暂停状态未被静音影响");
    check(n->pauseRemainingSeconds() == 10, "倒计时未被重置");

    n->setMuted(false);
    check(!n->shouldBeep(), "取消静音后仍因暂停而不发声");

    delete n;
}

void case5_autoResume()
{
    qInfo().noquote() << "== 用例 5：暂停到期自动恢复并重新起声 ==";
    AlarmNotifier *n = makeNotifier();

    QElapsedTimer clock;
    clock.start();

    n->updateTopLevel(AlarmEvent::Warning, true);
    const int before = n->beepCount();     // 首声已立即响过

    // 暂停 1 秒（允许的最小正时长），等待其到期。
    const int secs = n->startPause(1);
    check(secs == 1, "暂停 1 s 生效");
    const int duringPause = n->beepCount();
    check(duringPause == before, "暂停开始后不再新增发声");

    // 等待 1.4 s：覆盖 1 s 倒计时 + 恢复时的立即发声。
    spin(1400);

    check(!n->isPaused(), "暂停到期后自动解除");
    check(n->pauseRemainingSeconds() == 0, "剩余秒数归零");
    check(n->beepCount() > duringPause,
          "恢复时仍有未处理告警 → 立即重新发声（不得静默等待下个周期）");
    qInfo().noquote() << "    暂停前" << before << "次 / 暂停中" << duringPause
                      << "次 / 恢复后" << n->beepCount() << "次，耗时"
                      << clock.elapsed() << "ms";

    delete n;
}

void case6_muteOnlyAudio()
{
    qInfo().noquote() << "== 用例 6：静音只关声音，不影响报警状态 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Critical, true);
    const int afterFirst = n->beepCount();
    check(afterFirst > 0, "有未处理告警时立即发声");

    n->setMuted(true);
    check(!n->shouldBeep(), "静音后不应发声");

    const int mutedCount = n->beepCount();
    spin(1200);   // 跨越一个危急周期（900 ms）
    check(n->beepCount() == mutedCount, "静音期间跨周期仍不发声");

    n->setMuted(false);
    check(n->shouldBeep(), "取消静音后恢复发声资格");
    check(n->beepCount() > mutedCount, "取消静音后立即补一声（不等整周期）");

    delete n;
}

void case7_cancelPause()
{
    qInfo().noquote() << "== 用例 7：提前结束暂停立即恢复 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Warning, true);
    n->startPause(60);
    const int during = n->beepCount();

    n->cancelPause();
    check(!n->isPaused(), "暂停已解除");
    check(n->beepCount() > during, "恢复后立即发声");

    delete n;
}

void case8_resumeWithoutAlarm()
{
    qInfo().noquote() << "== 用例 8：告警已消失时恢复不发声 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Warning, true);
    n->startPause(60);
    const int during = n->beepCount();

    // 条件恢复：告警被清除。
    n->updateTopLevel(AlarmEvent::Info, /*hasPending=*/false);
    n->cancelPause();

    check(!n->isPaused(), "暂停已解除");
    check(!n->shouldBeep(), "无告警时不应发声");
    check(n->beepCount() == during, "恢复动作本身不制造声音");

    delete n;
}

void case9_immediateFirstBeep()
{
    qInfo().noquote() << "== 用例 9：首声立即响（发现延迟优先）==";
    AlarmNotifier *n = makeNotifier();

    check(n->beepCount() == 0, "初始未发声");
    n->updateTopLevel(AlarmEvent::Critical, true);
    // 危急为 3 连响：一次 updateTopLevel() 后应立即计 3 次。
    check(n->beepCount() == 3, "危急报警首次即 3 连响，无需等待 900 ms");

    AlarmNotifier *n2 = makeNotifier();
    n2->updateTopLevel(AlarmEvent::Warning, true);
    check(n2->beepCount() == 1, "一般报警首次 1 响");

    delete n;
    delete n2;
}

void case10_rhythmFollowsLevel()
{
    qInfo().noquote() << "== 用例 10：严重度变化时节奏随之改变 ==";
    AlarmNotifier *n = makeNotifier();

    n->updateTopLevel(AlarmEvent::Warning, true);
    const int warnStart = n->beepCount();   // 1 响

    // 升级为危急：节奏 1800 → 900 ms，连响 1 → 3。
    n->updateTopLevel(AlarmEvent::Escalated, true);
    check(n->beepCount() == warnStart + 3,
          "升级为 Escalated 后立即 3 连响（不等待旧节奏走完）");

    // 连续跑 2 秒：危急周期 900 ms，应至少再响 2 轮。
    const int beforeSpin = n->beepCount();
    spin(2000);
    const int delta = n->beepCount() - beforeSpin;
    qInfo().noquote() << "    2 s 内新增" << delta << "次发声";
    check(delta >= 6, "2 s 内至少 2 轮 × 3 连响");

    delete n;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo().noquote() << "==== 报警静音 / 暂停策略测试 ====";
    case1_noAlarmNoPause();
    case2_pauseBaseRate();
    case3_criticalPauseClamped();
    case4_muteDuringPause();
    case5_autoResume();
    case6_muteOnlyAudio();
    case7_cancelPause();
    case8_resumeWithoutAlarm();
    case9_immediateFirstBeep();
    case10_rhythmFollowsLevel();

    qInfo().noquote() << "==== 失败项：" << g_fail << " ====";
    return g_fail;
}
