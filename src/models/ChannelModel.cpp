#include "models/ChannelModel.h"

namespace {
/// 信号质量门控阈值，必须与 AlarmDetector 中的 kLowQualityThreshold 一致，
/// 否则会出现「数值面板说可信、报警模块说不可信」的不一致。
constexpr double kLowQualityThreshold = 0.35;
} // namespace

ChannelModel::ChannelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChannelModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const Row &r = m_rows.at(index.row());

    switch (role) {
    case ChannelIdRole:    return r.cfg.id;
    case NameRole:         return r.cfg.name;
    case UnitRole:         return r.cfg.unit;
    case ColorRole:        return r.cfg.color;
    case MinValueRole:     return r.cfg.minValue;
    case MaxValueRole:     return r.cfg.maxValue;
    case AlarmLowRole:     return r.cfg.alarmLow;
    case AlarmHighRole:    return r.cfg.alarmHigh;
    case HasLowAlarmRole:  return r.cfg.hasLowAlarm;
    case HasHighAlarmRole: return r.cfg.hasHighAlarm;
    case VisibleRole:      return r.cfg.visible;
    case ValueRole:        return r.value;
    case QualityRole:      return r.quality;
    case ValidRole:        return r.valid;
    case AlarmedRole:      return r.alarmed;
    case HasValueRole:     return r.hasValue;
    case AlarmBorderRole:
        // 无告警时返回空字符串，QML 侧据此回退到默认边框色。
        return r.alarmBorder.isValid() ? r.alarmBorder.name() : QString();
    case OutOfRangeRole:   return r.outOfRange;
    case ArtifactRole:     return r.artifact;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ChannelModel::roleNames() const
{
    return {
        { ChannelIdRole,    "channelId"    },
        { NameRole,         "name"         },
        { UnitRole,         "unit"         },
        { ColorRole,        "curveColor"   },
        { MinValueRole,     "minValue"     },
        { MaxValueRole,     "maxValue"     },
        { AlarmLowRole,     "alarmLow"     },
        { AlarmHighRole,    "alarmHigh"    },
        { HasLowAlarmRole,  "hasLowAlarm"  },
        { HasHighAlarmRole, "hasHighAlarm" },
        // @note 角色名刻意用 `channelVisible` 而非 `visible`：
        //       QML 委托若要安全取用该角色，需写成
        //       `required property bool channelVisible`；而
        //       `required property bool visible` 会因 QQuickItem::visible
        //       是 FINAL 属性而直接报 "Cannot override FINAL property"
        //       导致整个委托无法创建（实测于 Qt 6.2.4）。
        //       这与 ChannelValueCard::channelVisible 的命名保持一致。
        { VisibleRole,      "channelVisible" },
        { ValueRole,        "value"        },
        { QualityRole,      "quality"      },
        { ValidRole,        "valid"        },
        { AlarmedRole,      "alarmed"      },
        { HasValueRole,     "hasValue"     },
        { AlarmBorderRole,  "alarmBorder"  },
        { OutOfRangeRole,   "outOfRange"   },
        { ArtifactRole,     "artifact"     }
    };
}

void ChannelModel::resetChannels(const QVector<ChannelConfig> &channels)
{
    // 尽量保留同名通道的当前数值，避免改配置时数值瞬间跳回 "--"。
    QVector<Row> fresh;
    fresh.reserve(channels.size());
    for (const ChannelConfig &c : channels) {
        Row r;
        r.cfg = c;
        for (const Row &old : m_rows) {
            if (old.cfg.id == c.id) {
                r.value      = old.value;
                r.quality    = old.quality;
                r.valid      = old.valid;
                r.hasValue   = old.hasValue;
                r.outOfRange = old.outOfRange;
                r.artifact   = old.artifact;
                break;
            }
        }
        fresh.append(r);
    }

    beginResetModel();
    m_configs = channels;
    m_rows    = fresh;
    endResetModel();
}

void ChannelModel::applyFrame(const SampleFrame &frame)
{
    if (m_rows.isEmpty())
        return;

    // 只有真正变化的行才发 dataChanged：10 Hz × 4 通道的更新里，
    // 每帧实际变化的行数远小于总行数，避免整表重绘。
    QVector<int> pendingRows;
    QVector<QVector<int>> pendingRoles;

    for (const ChannelSample &s : frame.channels) {
        for (int i = 0; i < m_rows.size(); ++i) {
            // @note 用非 const 引用取出行以便就地更新；Row 内没有
            //       QObject，直接改成员不会触发信号，最后统一 emit。
            Row &r = m_rows[i];
            if (r.cfg.id != s.channelId)
                continue;

            const bool valid = !s.invalid && (s.quality >= kLowQualityThreshold);
            const bool lowAlarm  = r.cfg.hasLowAlarm  && (s.value < r.cfg.alarmLow);
            const bool highAlarm = r.cfg.hasHighAlarm && (s.value > r.cfg.alarmHigh);
            const bool alarmed = valid && (lowAlarm || highAlarm);
            // 不裁剪数值（优化文档 3.1 第 5 项）：越限原文保留，
            // 用该标记让界面显示 ↑ / ↓ 角标，避免把 HR 250 静默显示成 180。
            const bool outOfRange = (s.value < r.cfg.minValue)
                                    || (s.value > r.cfg.maxValue);

            const bool changed = !r.hasValue
                                 || !qFuzzyCompare(r.value + 1.0, s.value + 1.0)
                                 || !qFuzzyCompare(r.quality + 1.0, s.quality + 1.0)
                                 || r.valid != valid
                                 || r.alarmed != alarmed
                                 || r.outOfRange != outOfRange
                                 || r.artifact != s.artifact;
            r.value      = s.value;
            r.quality    = s.quality;
            r.valid      = valid;
            r.alarmed    = alarmed;
            r.outOfRange = outOfRange;
            r.artifact   = s.artifact;
            r.hasValue   = true;

            if (changed) {
                pendingRows.append(i);
                pendingRoles.append(QVector<int>{
                    ValueRole, QualityRole, ValidRole, AlarmedRole, HasValueRole,
                    OutOfRangeRole, ArtifactRole });
            }
            break;
        }
    }

    for (int k = 0; k < pendingRows.size(); ++k) {
        const QModelIndex idx = index(pendingRows.at(k), 0);
        emit dataChanged(idx, idx, pendingRoles.at(k));
    }
}

void ChannelModel::applyAlarmState(const QVector<AlarmEvent> &activeAlarms)
{
    QVector<QColor> borders(m_rows.size());

    for (int i = 0; i < m_rows.size(); ++i) {
        QColor border;   // 默认无效 = 无告警
        const int chId = m_rows.at(i).cfg.id;

        for (const AlarmEvent &a : activeAlarms) {
            if (a.channelId != chId)
                continue;

            // 技术报警用蓝色边框区分生理报警，便于快速定位设备问题
            // （与 VitalValuePanel::applyAlarmState 完全一致）。
            if (a.category == AlarmEvent::Technical) {
                border = QColor(0x2A, 0x6E, 0x8A);
                break;
            }

            const QColor c = alarmIndicatorColor(a.level);
            // 已确认的告警边框减淡，表示「已处理但仍未恢复」。
            border = (a.state == AlarmEvent::Acknowledged) ? c.darker(150) : c;
            break;
        }
        borders[i] = border;
    }

    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).alarmBorder == borders.at(i))
            continue;
        m_rows[i].alarmBorder = borders.at(i);
        const QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { AlarmBorderRole });
    }
}
