#include "models/AlarmModel.h"

#include <QDateTime>
#include <algorithm>

namespace {

/// 严重度排序权重：Escalated(3) > Critical(2) > Warning(1) > Info(0)。
int levelWeight(AlarmEvent::Level lv)
{
    switch (lv) {
    case AlarmEvent::Escalated: return 3;
    case AlarmEvent::Critical:  return 2;
    case AlarmEvent::Warning:   return 1;
    case AlarmEvent::Info:      return 0;
    }
    return 0;
}

/// 告警状态中文名（与 MainWindow::auditAlarm 使用的措辞保持一致）。
QString stateName(AlarmEvent::State st)
{
    switch (st) {
    case AlarmEvent::Active:        return QStringLiteral("激活");
    case AlarmEvent::Acknowledged:  return QStringLiteral("已确认");
    case AlarmEvent::Cleared:       return QStringLiteral("已解除");
    case AlarmEvent::LatchedSilent: return QStringLiteral("静音重报");
    }
    return QStringLiteral("激活");
}

} // namespace

AlarmModel::AlarmModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AlarmModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_alarms.size();
}

QVariant AlarmModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_alarms.size())
        return QVariant();

    const AlarmEvent &a = m_alarms.at(index.row());

    switch (role) {
    case ChannelIdRole:   return a.channelId;
    case LevelRole:       return static_cast<int>(a.level);
    case LevelColorRole:  return alarmIndicatorColor(a.level);
    case LevelNameRole:   return QStringLiteral("%1报警")
                                 .arg(a.level == AlarmEvent::Info     ? QStringLiteral("提示")
                                      : a.level == AlarmEvent::Warning ? QStringLiteral("一般")
                                                                       : QStringLiteral("严重"));
    case CategoryRole:    return static_cast<int>(a.category);
    case StateRole:       return static_cast<int>(a.state);
    case StateNameRole:   return stateName(a.state);
    case HighLimitRole:   return a.highLimit;
    case ValueRole:       return a.value;
    case LimitRole:       return a.limit;
    case TextRole:        return a.text;
    case TimeTextRole:
        return QDateTime::fromMSecsSinceEpoch(a.timestampMs)
            .toString(QStringLiteral("HH:mm:ss"));
    case SampleIndexRole: return QVariant::fromValue<qulonglong>(a.sampleIndex);
    case PulsingRole:
        // 高优先级 / 已升级必须比中低优先级更醒目（IEC 60601-1-8）。
        return a.level == AlarmEvent::Escalated || a.level == AlarmEvent::Critical;
    case IsTechnicalRole: return a.category == AlarmEvent::Technical;
    case TrackKeyRole:    return a.trackKey;
    case DurationMsRole:  return m_durations.value(a.trackKey, -1);
    case DurationTextRole: {
        // -1 表示未知（理论上不应出现）：显示「—」而不是伪造一个时长。
        const qint64 ms = m_durations.value(a.trackKey, -1);
        return ms < 0 ? QStringLiteral("—")
                      : QStringLiteral("持续 %1 s")
                            .arg(QString::number(ms / 1000.0, 'f', 1));
    }
    default:              return QVariant();
    }
}

QHash<int, QByteArray> AlarmModel::roleNames() const
{
    return {
        { ChannelIdRole,   "channelId"  },
        { LevelRole,       "level"      },
        { LevelColorRole,  "levelColor" },
        { LevelNameRole,   "levelName"  },
        { CategoryRole,    "category"   },
        { StateRole,       "state"      },
        { StateNameRole,   "stateName"  },
        { HighLimitRole,   "highLimit"  },
        { ValueRole,       "value"      },
        { LimitRole,       "limit"      },
        { TextRole,        "text"       },
        { TimeTextRole,    "timeText"   },
        { SampleIndexRole, "sampleIndex"},
        { PulsingRole,     "pulsing"    },
        { IsTechnicalRole, "isTechnical"},
        { TrackKeyRole,    "trackKey"   },
        { DurationMsRole,  "durationMs" },
        { DurationTextRole,"durationText"}
    };
}

bool AlarmModel::lessThan(const AlarmEvent &a, const AlarmEvent &b) const
{
    const int wa = levelWeight(a.level);
    const int wb = levelWeight(b.level);
    if (wa != wb)
        return wa > wb;                       // 严重度高的排前面

    // 未确认的排前面：已确认的告警已有人处置，不该继续占据最顶端。
    const bool aAcked = (a.state == AlarmEvent::Acknowledged
                         || a.state == AlarmEvent::LatchedSilent);
    const bool bAcked = (b.state == AlarmEvent::Acknowledged
                         || b.state == AlarmEvent::LatchedSilent);
    if (aAcked != bAcked)
        return !aAcked;

    // 同严重度：持续久的排前面。
    // 用 trackKey 查时长而非 conditionKey()：技术报警的键是 "1/2/SQI" 这类，
    // conditionKey() 只能生成生理告警键，查不到时长（会退化成全部 0）。
    return m_durations.value(a.trackKey, 0)
         > m_durations.value(b.trackKey, 0);
}

void AlarmModel::setAlarms(const QVector<AlarmEvent> &alarms,
                           const QHash<QString, qint64> &durations)
{
    // 时长快照必须在排序前落位：lessThan() 依赖 m_durations。
    m_durations = durations;

    QVector<AlarmEvent> sorted = alarms;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [this](const AlarmEvent &a, const AlarmEvent &b) {
                         return lessThan(a, b);
                     });

    // 数量与内容都可能变；告警条数通常 ≤ 十位数量级，
    // 用 reset 足够廉价，且能避免逐项 diff 的复杂度。
    beginResetModel();
    m_alarms = sorted;
    endResetModel();

    emit alarmsChanged();
}

int AlarmModel::topLevel() const
{
    int best = 0;
    for (const AlarmEvent &a : m_alarms)
        best = qMax(best, levelWeight(a.level));
    return best;
}
