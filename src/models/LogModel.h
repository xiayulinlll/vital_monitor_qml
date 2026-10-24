#ifndef LOGMODEL_H
#define LOGMODEL_H

#include <QAbstractListModel>
#include <QStringList>

/**
 * @brief 状态 / 告警日志模型（对应 C++ 版 MainWindow 里的 QPlainTextEdit）。
 *
 * QML 相对 Widgets 的第二个增益点：日志不再是一个「黑框文本」，
 * 而是可着色、可过滤、可复制的结构化列表：
 *  - 按 `keyword` 属性做大小写不敏感的即时过滤（QML 侧用 DelegateModel 或
 *    `LogPanel` 内联过滤实现）；
 *  - 按级别（INFO / WARN / ERROR）显示不同颜色圆点；
 *  - 上限 `maxRows` 自动淘汰最旧行，避免长时间运行内存增长
 *    （等价 C++ 的 `setMaximumBlockCount(500)`）。
 *
 * 内部用**环形下标**存储：插入 O(1)，不需要 `removeFirst()` 的 O(n) 搬移，
 * 顺带解决了 Widgets 版审核报告中的 R-2 隐患。
 */
class LogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Level {
        Info = 0,
        Warn,
        Error
    };
    Q_ENUM(Level)

    enum Roles {
        TextRole = Qt::UserRole + 1,
        TimeTextRole,
        LevelRole,
        LevelColorRole
    };
    Q_ENUM(Roles)

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 追加一行（自动带时间戳：HH:mm:ss.zzz）。
    Q_INVOKABLE void append(const QString &text, int level = Info);

    /// 清空全部日志。
    Q_INVOKABLE void clear();

    /// 把全部日志拼成纯文本（用于「复制日志」按钮）。
    Q_INVOKABLE QString toPlainText() const;

    /// 最大行数（超出后淘汰最旧行）。默认 500，与 Widgets 版一致。
    Q_PROPERTY(int maxRows READ maxRows WRITE setMaxRows NOTIFY maxRowsChanged)
    int maxRows() const { return m_maxRows; }
    void setMaxRows(int rows);

signals:
    void maxRowsChanged();

private:
    struct Entry {
        QString text;
        QString timeText;
        int     level = Info;
    };

    /// 逻辑下标 i（0 = 最旧）→ 物理下标。
    int physicalIndex(int i) const;

    QVector<Entry> m_entries;   ///< 固定容量，物理环形
    int m_head  = 0;            ///< 最旧元素物理下标
    int m_size  = 0;
    int m_maxRows = 500;
};

#endif // LOGMODEL_H
