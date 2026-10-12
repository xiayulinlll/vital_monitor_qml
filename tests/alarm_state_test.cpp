// 单元测试：用确定性数据验证 AlarmDetector 的状态机行为。
//
// 由 CMake 目标 alarm_state_test 构建（对应优化文档第三梯队第 15 项：
// 自动化测试与 V&V）；不依赖 GUI，退出码 = 失败用例数（0 表示全部通过）。
//
// 运行：
//   cmake --build --preset mingw-debug --target alarm_state_test
//   build\vscode-mingw-debug\alarm_state_test.exe
#include "core/AlarmDetector.h"
#include "core/ChannelConfig.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

SampleFrame makeFrame(quint64 idx, qint64 tsMs, int ch, double v,
                      double quality = 1.0, quint64 dropped = 0)
{
    SampleFrame f;
    f.sampleIndex = idx;
    f.timestampMs = tsMs;
    f.droppedFrames = dropped;
    ChannelSample s;
    s.channelId = ch;
    s.value = v;
    s.quality = quality;
    f.channels.append(s);
    return f;
}

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

} // namespace

QVector<ChannelConfig> testCfgs()
{
    ChannelConfigManager mgr;
    QVector<ChannelConfig> cfgs = mgr.channels();
    for (ChannelConfig &c : cfgs) {
        if (c.id == 0) {
            c.alarmLow = 60.0;
            c.alarmHigh = 110.0;
            c.hasLowAlarm = true;
            c.hasHighAlarm = true;
            c.alarmDelayMs = 1000;
            c.alarmRepeatMs = 2000;
            c.alarmEscalateMs = 3000;
            // 明确优先级与二级阈值（取代旧的「半量程折算」）。
            c.alarmPriority    = AlarmPriority::Medium;
            c.alarmLow2        = 50.0;
            c.alarmHigh2       = 150.0;
            c.hasSecondaryLow  = true;
            c.hasSecondaryHigh = true;
        }
    }
    return cfgs;
}

static void case1_persistentRepeat()
{
    qInfo().noquote() << "== 用例 1：持续越限必须反复上报（修复“只报一次”）==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    int reports = 0;
    for (int i = 0; i < 100; ++i) {
        const auto evts = det.detect(makeFrame(i, t, 0, 150.0), cfgs);
        for (const AlarmEvent &e : evts)
            if (e.channelId == 0 && e.highLimit && e.category == AlarmEvent::Physiological
                && (e.state == AlarmEvent::Active || e.state == AlarmEvent::LatchedSilent))
                ++reports;
        t += 100;
    }
    qInfo().noquote() << "    持续 10s 超限，共上报" << reports << "次";
    check(reports >= 4, "持续越限出现多次上报（>=4）");
    check(det.activeCount() == 1, "活动告警数为 1（未重复新增）");
    check(det.hasPendingAlarm(), "存在待处理告警");
}

static void case2_ackThenRepeat()
{
    qInfo().noquote() << "== 用例 2：确认后条件仍成立 → 按 repeatMs 重报 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    for (int i = 0; i < 15; ++i) { det.detect(makeFrame(i, t, 0, 150.0), cfgs); t += 100; }
    const int before = det.activeCount();
    // 传入与 detect() 相同的时基（t），否则确认时间与帧时间戳不同源，
    // 会让后续「重复提示」的计时全部失效。
    const auto acked = det.acknowledge(QString(), t);
    check(before == 1 && acked.size() == 1, "确认前有 1 条活动告警且被确认");
    check(!det.hasPendingAlarm(), "确认后无待处理告警（声音停止）");

    int reReports = 0;
    // 确认后推进 5 秒：即使已确认，只要条件仍成立就必须按 repeatMs 再次提示。
    // t 已指向第 16 帧，故循环内每次自增即可覆盖到 t==6500（确认点 +5000）。
    for (int i = 15; i < 65; ++i) {
        const auto evts = det.detect(makeFrame(i, t, 0, 150.0), cfgs);
        for (const AlarmEvent &e : evts)
            if (e.state == AlarmEvent::LatchedSilent) ++reReports;
        t += 100;
    }
    qInfo().noquote() << "    确认后 5s 内重报" << reReports << "次";
    check(reReports >= 1, "确认后条件仍成立会重新提示（非永久静默）");
    check(det.activeCount() == 1, "重报不新增告警条数");
}

static void case3_escalation()
{
    qInfo().noquote() << "== 用例 3：超时未确认 → 升级 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    bool escalated = false;
    for (int i = 0; i < 100; ++i) {
        const auto evts = det.detect(makeFrame(i, t, 0, 150.0), cfgs);
        for (const AlarmEvent &e : evts)
            if (e.level == AlarmEvent::Escalated) escalated = true;
        t += 100;
    }
    check(escalated, "超时未确认会升级严重度");
}

static void case4_debounce()
{
    qInfo().noquote() << "== 用例 4：延时去抖（毫秒制）抑制瞬时毛刺 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    int reports = 0;
    for (int i = 0; i < 5; ++i) {
        reports += det.detect(makeFrame(i, t, 0, 150.0), cfgs).size();
        t += 100;
    }
    check(reports == 0, "500ms 毛刺被延时去抖抑制");
    check(det.activeCount() == 0, "毛刺后无活动告警");
}

static void case5_cleared()
{
    qInfo().noquote() << "== 用例 5：条件恢复 → 解除 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    for (int i = 0; i < 20; ++i) { det.detect(makeFrame(i, t, 0, 150.0), cfgs); t += 100; }
    check(det.activeCount() == 1, "越限期间有 1 条活动告警");
    bool cleared = false;
    for (int i = 20; i < 40; ++i) {
        const auto evts = det.detect(makeFrame(i, t, 0, 80.0), cfgs);
        for (const AlarmEvent &e : evts)
            if (e.state == AlarmEvent::Cleared) cleared = true;
        t += 100;
    }
    check(cleared, "回到正常范围产生解除事件");
    check(det.activeCount() == 0, "解除后活动告警清零");
}

static void case6_sqiGating()
{
    qInfo().noquote() << "== 用例 6：SQI 过低 → 抑制生理报警，改发技术报警 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    int phys = 0, tech = 0;
    for (int i = 0; i < 30; ++i) {
        const auto evts = det.detect(makeFrame(i, t, 0, 150.0, /*quality=*/0.10), cfgs);
        for (const AlarmEvent &e : evts) {
            if (e.category == AlarmEvent::Technical) ++tech;
            else if (e.channelId == 0 && e.highLimit) ++phys;
        }
        t += 100;
    }
    check(phys == 0, "低 SQI 下不产生生理越限报警");
    check(tech >= 1, "低 SQI 产生技术报警");
}

static void case7_droppedFrames()
{
    qInfo().noquote() << "== 用例 7：丢帧 → 技术报警 ==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();
    qint64 t = 1000;
    int dropReports = 0;
    for (int i = 0; i < 10; ++i) {
        const auto evts = det.detect(makeFrame(i, t, -1, 0.0, 1.0, /*dropped=*/1), cfgs);
        for (const AlarmEvent &e : evts)
            if (e.category == AlarmEvent::Technical && e.channelId == -1) ++dropReports;
        t += 100;
    }
    check(dropReports >= 1, "丢帧累计超阈值后产生技术报警");
}

static void case8_prioritySemantics()
{
    qInfo().noquote() << "== 用例 8：报警优先级来自配置，而非「超出量程一半」==";
    AlarmDetector det;
    det.setDefaultSampleIntervalMs(100);
    const QVector<ChannelConfig> cfgs = testCfgs();

    // 8a) 中优先级 + 未越过二级阈值 → 应为 Warning（不是 Critical）。
    {
        AlarmDetector d;
        d.setDefaultSampleIntervalMs(100);
        qint64 t = 1000;
        AlarmEvent::Level lvl = AlarmEvent::Info;
        for (int i = 0; i < 30; ++i) {
            const auto evts = d.detect(makeFrame(i, t, 0, 120.0), cfgs);
            for (const AlarmEvent &e : evts)
                if (e.state == AlarmEvent::Active && e.channelId == 0 && e.highLimit
                    && e.category == AlarmEvent::Physiological)
                    lvl = e.level;
            t += 100;
        }
        check(lvl == AlarmEvent::Warning, "120 bpm（一级越限）→ Warning 而非 Critical");
    }

    // 8b) 越过二级阈值(>150) → 应为 Critical。
    {
        AlarmDetector d;
        d.setDefaultSampleIntervalMs(100);
        qint64 t = 1000;
        AlarmEvent::Level lvl = AlarmEvent::Info;
        for (int i = 0; i < 30; ++i) {
            const auto evts = d.detect(makeFrame(i, t, 0, 170.0), cfgs);
            for (const AlarmEvent &e : evts)
                if (e.state == AlarmEvent::Active && e.channelId == 0 && e.highLimit
                    && e.category == AlarmEvent::Physiological)
                    lvl = e.level;
            t += 100;
        }
        check(lvl == AlarmEvent::Critical, "170 bpm（越过二级阈值）→ Critical");
    }

    // 8c) 低优先级通道即使超到量程极端，也不应变成 Critical。
    {
        QVector<ChannelConfig> c2 = testCfgs();
        for (ChannelConfig &c : c2)
            if (c.id == 0) {
                c.alarmPriority = AlarmPriority::Low;
                c.hasSecondaryHigh = false;   // 无二级阈值
            }
        AlarmDetector d;
        d.setDefaultSampleIntervalMs(100);
        qint64 t = 1000;
        AlarmEvent::Level lvl = AlarmEvent::Info;
        for (int i = 0; i < 30; ++i) {
            const auto evts = d.detect(makeFrame(i, t, 0, 179.0), c2);
            for (const AlarmEvent &e : evts)
                if (e.state == AlarmEvent::Active && e.channelId == 0 && e.highLimit
                    && e.category == AlarmEvent::Physiological)
                    lvl = e.level;
            t += 100;
        }
        check(lvl == AlarmEvent::Info, "低优先级通道超到量程极端仍为 Info（不折算成 Critical）");
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    case1_persistentRepeat();
    case2_ackThenRepeat();
    case3_escalation();
    case4_debounce();
    case5_cleared();
    case6_sqiGating();
    case7_droppedFrames();
    case8_prioritySemantics();

    qInfo().noquote() << (g_fail == 0
                              ? QStringLiteral("==== 全部通过 ====")
                              : QStringLiteral("==== 失败 %1 项 ====").arg(g_fail));
    return g_fail;
}
