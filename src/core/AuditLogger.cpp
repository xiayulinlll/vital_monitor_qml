#include "AuditLogger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

// ---------------------------------------------------------------------------
// CRC32（IEEE 802.3 多项式 0xEDB88320）—— 用于行完整性校验
// ---------------------------------------------------------------------------
namespace {

quint32 crc32Table(int index)
{
    quint32 crc = static_cast<quint32>(index);
    for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    return crc;
}

quint32 crc32Of(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data) {
        const quint8 byte = static_cast<quint8>(ch);
        crc = crc32Table(static_cast<int>((crc ^ byte) & 0xFFu)) ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

} // namespace

QString AuditLogger::crc32Hex(const QString &text)
{
    return QStringLiteral("%1").arg(crc32Of(text.toUtf8()), 8, 16, QLatin1Char('0')).toUpper();
}

// ---------------------------------------------------------------------------

AuditLogger &AuditLogger::instance()
{
    static AuditLogger logger;
    return logger;
}

AuditLogger::AuditLogger(QObject *parent)
    : QObject(parent)
{
}

AuditLogger::~AuditLogger()
{
    close();
}

QString AuditLogger::iso8601Now()
{
    // ISO 8601 带时区偏移，便于与外部系统对齐时间基线。
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

QString AuditLogger::categoryName(Category c)
{
    switch (c) {
    case System:    return QStringLiteral("SYSTEM");
    case Config:    return QStringLiteral("CONFIG");
    case Alarm:     return QStringLiteral("ALARM");
    case TechAlarm: return QStringLiteral("TECH_ALARM");
    case Data:      return QStringLiteral("DATA");
    }
    return QStringLiteral("UNKNOWN");
}

QString AuditLogger::severityName(Severity s)
{
    switch (s) {
    case Info:     return QStringLiteral("INFO");
    case Warning:  return QStringLiteral("WARNING");
    case Critical: return QStringLiteral("CRITICAL");
    }
    return QStringLiteral("INFO");
}

bool AuditLogger::openFile(const QString &filePath)
{
    close();

    QString path = filePath;
    if (path.isEmpty()) {
        // 默认落在可执行文件同级的 audit 目录下。
        const QString baseDir = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/audit");
        QDir().mkpath(baseDir);
        path = QStringLiteral("%1/audit_%2.log")
                   .arg(baseDir,
                        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd")));
    }

    QDir().mkpath(QFileInfo(path).absolutePath());

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    m_filePath = path;
    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);

    // 写入表头（仅当文件为空时）。
    if (m_file.size() == 0) {
        m_stream << QStringLiteral(
            "# Vital Monitor 审计日志 / Audit Trail (IEC 62304 / YY/T 0664 参考)\n"
            "# 格式: ISO8601时间\\t类别\\t严重度\\t操作者\\t动作\\t明细\\tCRC32\n");
        m_stream.flush();
    }

    log(System, Info, QStringLiteral("审计日志已启用"),
        QStringLiteral("path=%1").arg(path));
    return true;
}

void AuditLogger::close()
{
    if (m_file.isOpen()) {
        log(System, Info, QStringLiteral("审计日志已关闭"));
        m_stream.flush();
        m_file.close();
    }
    m_filePath.clear();
}

void AuditLogger::log(Category category,
                      Severity severity,
                      const QString &action,
                      const QString &detail,
                      const QString &actor)
{
    const QString ts  = iso8601Now();
    const QString cat = categoryName(category);
    const QString sev = severityName(severity);

    // 明细中的制表符 / 换行会破坏列结构，统一转义。
    QString safeDetail = detail;
    safeDetail.replace(QLatin1Char('\t'), QLatin1Char(' '));
    safeDetail.replace(QLatin1Char('\n'), QLatin1Char(' '));
    safeDetail.replace(QLatin1Char('\r'), QLatin1Char(' '));

    // 校验和覆盖「时间 + 类别 + 严重度 + 操作者 + 动作 + 明细」，
    // 任何字段被事后篡改都会导致校验失败。
    const QString payload = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6")
                                .arg(ts, cat, sev, actor, action, safeDetail);

    if (!m_file.isOpen()) {
        // 未开启落盘时仍然通过标准输出留痕，避免事件彻底丢失。
        qInfo().noquote() << QStringLiteral("[AUDIT] %1").arg(payload);
        return;
    }

    m_stream << payload << QLatin1Char('\t') << crc32Hex(payload) << QLatin1Char('\n');
    m_stream.flush();
}

void AuditLogger::logConfigChange(const QString &channelName,
                                  const QString &field,
                                  const QString &oldValue,
                                  const QString &newValue)
{
    if (oldValue == newValue)
        return;   // 无变化不记录，避免日志噪声

    log(Config,
        Info,
        QStringLiteral("修改通道配置"),
        QStringLiteral("channel=%1; %2: %3 -> %4")
            .arg(channelName, field, oldValue, newValue));
}
