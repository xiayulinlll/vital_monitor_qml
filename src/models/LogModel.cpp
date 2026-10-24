#include "models/LogModel.h"

#include <QColor>
#include <QDateTime>

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_entries.resize(m_maxRows);
}

int LogModel::physicalIndex(int i) const
{
    // 逻辑下标 0 = 最旧元素，物理位置从 m_head 开始环形推进。
    return (m_head + i) % m_maxRows;
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_size;
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_size)
        return QVariant();

    const Entry &e = m_entries.at(physicalIndex(index.row()));

    switch (role) {
    case TextRole:     return e.text;
    case TimeTextRole: return e.timeText;
    case LevelRole:    return e.level;
    case LevelColorRole:
        switch (e.level) {
        case Warn:  return QColor(0xE0, 0xA0, 0x30);
        case Error: return QColor(0xFF, 0x52, 0x52);
        default:    return QColor(0x6E, 0x7A, 0x8A);
        }
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        { TextRole,       "text"       },
        { TimeTextRole,   "timeText"   },
        { LevelRole,      "level"      },
        { LevelColorRole, "levelColor" }
    };
}

void LogModel::append(const QString &text, int level)
{
    Entry e;
    e.text     = text;
    e.timeText = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    e.level    = level;

    if (m_size < m_maxRows) {
        // 仍有空位：追加到末尾。
        const int row = m_size;
        beginInsertRows(QModelIndex(), row, row);
        m_entries[physicalIndex(row)] = e;
        ++m_size;
        endInsertRows();
    } else {
        // 已满：覆写最旧槽位并前移 head —— O(1)，没有 removeFirst() 的搬移。
        m_entries[m_head] = e;
        m_head = (m_head + 1) % m_maxRows;
        // 整体内容位移了一格，用 reset 通知视图（列表短，代价可忽略）。
        beginResetModel();
        endResetModel();
    }
}

void LogModel::clear()
{
    beginResetModel();
    m_head = 0;
    m_size = 0;
    endResetModel();
}

QString LogModel::toPlainText() const
{
    QString out;
    out.reserve(m_size * 32);
    for (int i = 0; i < m_size; ++i) {
        const Entry &e = m_entries.at(physicalIndex(i));
        out += e.timeText;
        out += QStringLiteral("  ");
        out += e.text;
        out += QLatin1Char('\n');
    }
    return out;
}

void LogModel::setMaxRows(int rows)
{
    rows = qMax(1, rows);
    if (rows == m_maxRows)
        return;

    // 缩小容量时丢弃最旧的行。
    QVector<Entry> kept;
    if (rows < m_size) {
        for (int i = m_size - rows; i < m_size; ++i)
            kept.append(m_entries.at(physicalIndex(i)));
    } else {
        for (int i = 0; i < m_size; ++i)
            kept.append(m_entries.at(physicalIndex(i)));
    }

    beginResetModel();
    m_maxRows = rows;
    m_entries.clear();
    m_entries.resize(m_maxRows);
    for (int i = 0; i < kept.size(); ++i)
        m_entries[i] = kept.at(i);
    m_head = 0;
    m_size = kept.size();
    endResetModel();

    emit maxRowsChanged();
}
