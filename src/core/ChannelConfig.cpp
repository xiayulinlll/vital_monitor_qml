#include "ChannelConfig.h"

#include <QHash>

QString patientTypeName(PatientType t)
{
    switch (t) {
    case PatientType::Adult:     return QStringLiteral("成人");
    case PatientType::Pediatric: return QStringLiteral("小儿");
    case PatientType::Neonate:   return QStringLiteral("新生儿");
    case PatientType::Custom:    return QStringLiteral("自定义");
    }
    return QStringLiteral("未知");
}

QString alarmPriorityName(AlarmPriority p)
{
    switch (p) {
    case AlarmPriority::Low:    return QStringLiteral("低");
    case AlarmPriority::Medium: return QStringLiteral("中");
    case AlarmPriority::High:   return QStringLiteral("高");
    }
    return QStringLiteral("中");
}

ChannelConfigManager::ChannelConfigManager()
{
    loadDefaults();
}

void ChannelConfigManager::loadDefaults()
{
    m_channels.clear();

    // 1) 心率 HR：60~120 bpm 常见，上下限告警
    {
        ChannelConfig c;
        c.id = 0;
        c.name = QStringLiteral("心率 HR");
        c.unit = QStringLiteral("bpm");
        c.color = QColor(0xE7, 0x4C, 0x3C);   // 红
        c.minValue = 40.0;
        c.maxValue = 180.0;
        c.alarmLow = 60.0;
        c.alarmHigh = 110.0;
        c.hasLowAlarm = true;
        c.hasHighAlarm = true;
        // 一般越限为中优先级；低于 50 / 高于 150 危及生命 → 高优先级。
        c.alarmPriority   = AlarmPriority::Medium;
        c.alarmLow2       = 50.0;
        c.alarmHigh2      = 150.0;
        c.hasSecondaryLow  = true;
        c.hasSecondaryHigh = true;
        m_channels.append(c);
    }

    // 2) 血氧 SpO2：95~100%，通常只设下限告警
    {
        ChannelConfig c;
        c.id = 1;
        c.name = QStringLiteral("血氧 SpO2");
        c.unit = QStringLiteral("%");
        c.color = QColor(0x2E, 0x86, 0xDE);   // 蓝
        c.minValue = 85.0;
        c.maxValue = 100.0;
        c.alarmLow = 94.0;
        c.alarmHigh = 100.0;
        c.hasLowAlarm = true;
        c.hasHighAlarm = false;
        // 血氧是最关键的生命参数：一级即高优先级，< 88% 为危及生命。
        c.alarmPriority  = AlarmPriority::High;
        c.alarmLow2      = 88.0;
        c.hasSecondaryLow = true;
        // 报警延时更短。
        c.alarmDelayMs = 500;
        c.alarmEscalateMs = 5000;
        m_channels.append(c);
    }

    // 3) 呼吸率 RR：12~20 次/分
    {
        ChannelConfig c;
        c.id = 2;
        c.name = QStringLiteral("呼吸率 RR");
        c.unit = QStringLiteral("rpm");
        c.color = QColor(0x27, 0xAE, 0x60);   // 绿
        c.minValue = 0.0;
        c.maxValue = 40.0;
        c.alarmLow = 8.0;
        c.alarmHigh = 24.0;
        c.hasLowAlarm = true;
        c.hasHighAlarm = true;
        // 呼吸暂停（< 5）/ 严重急促（> 32）危及生命。
        c.alarmPriority   = AlarmPriority::Medium;
        c.alarmLow2       = 5.0;
        c.alarmHigh2      = 32.0;
        c.hasSecondaryLow  = true;
        c.hasSecondaryHigh = true;
        m_channels.append(c);
    }

    // 4) 体温 Temp：36~37.5 ℃
    {
        ChannelConfig c;
        c.id = 3;
        c.name = QStringLiteral("体温 Temp");
        c.unit = QStringLiteral("℃");
        c.color = QColor(0xF3, 0x9C, 0x12);   // 橙
        c.minValue = 34.0;
        c.maxValue = 42.0;
        c.alarmLow = 35.5;
        c.alarmHigh = 37.5;
        c.hasLowAlarm = true;
        c.hasHighAlarm = true;
        // 体温变化缓慢，一般越限为低优先级；< 35 / > 39 才危及生命。
        c.alarmPriority   = AlarmPriority::Low;
        c.alarmLow2       = 35.0;
        c.alarmHigh2      = 39.0;
        c.hasSecondaryLow  = true;
        c.hasSecondaryHigh = true;
        // 体温变化缓慢，延时放宽以抑制噪声误报。
        c.alarmDelayMs = 3000;
        c.alarmEscalateMs = 20000;
        m_channels.append(c);
    }
}

// ---------------------------------------------------------------------------
// 患者类型报警限剖面
// ---------------------------------------------------------------------------
namespace {

/// 单通道在某患者类型下的量程与报警限。
struct ProfileEntry {
    double minValue;
    double maxValue;
    double alarmLow;
    double alarmHigh;
    bool   hasLow;
    bool   hasHigh;
    /// 报警优先级（IEC 60601-1-8 高 / 中 / 低）。
    AlarmPriority priority;
};

/// (patientType, channelId) -> ProfileEntry
const QHash<QString, ProfileEntry> &profileTable()
{
    // 数据来源参考常见监护仪出厂默认报警限（成人 / 小儿 / 新生儿）。
    // 二级（危及生命）阈值由 loadProfile() 按「一级阈值 + 量程比例」派生，
    // 保证不同患者类型的危险界值随其基线一起平移。
    static const QHash<QString, ProfileEntry> table = {
        // ---- 成人 Adult ----
        { QStringLiteral("0/0"), {  40, 180,  60, 110, true,  true,  AlarmPriority::Medium } }, // HR
        { QStringLiteral("0/1"), {  85, 100,  94, 100, true,  false, AlarmPriority::High   } }, // SpO2
        { QStringLiteral("0/2"), {   0,  40,   8,  24, true,  true,  AlarmPriority::Medium } }, // RR
        { QStringLiteral("0/3"), {  34,  42, 35.5, 37.5, true, true,  AlarmPriority::Low    } }, // Temp

        // ---- 小儿 Pediatric ----
        { QStringLiteral("1/0"), {  40, 200,  70, 150, true,  true,  AlarmPriority::Medium } }, // HR 更快
        { QStringLiteral("1/1"), {  85, 100,  94, 100, true,  false, AlarmPriority::High   } },
        { QStringLiteral("1/2"), {   0,  50,  10,  30, true,  true,  AlarmPriority::Medium } }, // RR 更快
        { QStringLiteral("1/3"), {  34,  42, 35.5, 37.8, true, true,  AlarmPriority::Low    } },

        // ---- 新生儿 Neonate ----
        { QStringLiteral("2/0"), {  40, 240, 100, 180, true,  true,  AlarmPriority::Medium } }, // HR 100~180
        { QStringLiteral("2/1"), {  85, 100,  90, 100, true,  false, AlarmPriority::High   } }, // SpO2 下限 90
        { QStringLiteral("2/2"), {   0,  80,  30,  60, true,  true,  AlarmPriority::Medium } }, // RR 30~60
        { QStringLiteral("2/3"), {  34,  42, 36.0, 37.5, true, true,  AlarmPriority::Low    } },
    };
    return table;
}

} // namespace

void ChannelConfigManager::loadProfile(PatientType type, bool overwriteCustom)
{
    if (type == PatientType::Custom) {
        // 切到「自定义」只更新标记，不改数值。
        m_patientType = type;
        return;
    }

    const QHash<QString, ProfileEntry> &table = profileTable();
    const bool overwrite = overwriteCustom || (m_patientType != PatientType::Custom);

    for (ChannelConfig &c : m_channels) {
        // 用户已手工设定为自定义范围时，默认不覆盖，避免抹掉医生设置。
        if (!overwrite && c.visible == false)
            continue;

        const QString key = QStringLiteral("%1/%2")
                                .arg(static_cast<int>(type))
                                .arg(c.id);
        const auto it = table.constFind(key);
        if (it == table.constEnd())
            continue;

        const ProfileEntry &e = it.value();
        c.minValue    = e.minValue;
        c.maxValue    = e.maxValue;
        c.alarmLow    = e.alarmLow;
        c.alarmHigh   = e.alarmHigh;
        c.hasLowAlarm = e.hasLow;
        c.hasHighAlarm= e.hasHigh;
        c.alarmPriority = e.priority;

        // 二级（危及生命）阈值随患者类型基线一起平移：
        // 取「一级阈值到量程端点距离」的一定比例，保证新生儿与成人的
        // 危险界值随其生理基线同步变化，而不是套用同一绝对数值。
        const double span = qMax(1.0, e.maxValue - e.minValue);
        if (e.hasLow) {
            c.alarmLow2       = qMax(e.minValue, e.alarmLow - span * 0.10);
            c.hasSecondaryLow = true;
        } else {
            c.hasSecondaryLow = false;
        }
        if (e.hasHigh) {
            c.alarmHigh2       = qMin(e.maxValue, e.alarmHigh + span * 0.20);
            c.hasSecondaryHigh = true;
        } else {
            c.hasSecondaryHigh = false;
        }
    }

    m_patientType = type;
}

ChannelConfig ChannelConfigManager::channelAt(int index) const
{
    if (index < 0 || index >= m_channels.size())
        return ChannelConfig();
    return m_channels.at(index);
}

bool ChannelConfigManager::channelById(int id, ChannelConfig &out) const
{
    for (const ChannelConfig &c : m_channels) {
        if (c.id == id) {
            out = c;
            return true;
        }
    }
    return false;
}

void ChannelConfigManager::updateChannel(const ChannelConfig &cfg)
{
    for (ChannelConfig &c : m_channels) {
        if (c.id == cfg.id) {
            c = cfg;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 进程级患者类型单点真源
// ---------------------------------------------------------------------------
namespace {
/// 独一无二的静态存储，供两个静态访问器共享读写。
PatientType &globalTypeStorage()
{
    static PatientType s_type = PatientType::Adult;
    return s_type;
}
} // namespace

PatientType ChannelConfigManager::globalPatientType()
{
    return globalTypeStorage();
}

void ChannelConfigManager::setGlobalPatientType(PatientType type)
{
    globalTypeStorage() = type;
}

