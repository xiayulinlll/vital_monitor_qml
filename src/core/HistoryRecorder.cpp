#include "HistoryRecorder.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

bool HistoryRecorder::saveFrames(const QString &filePath,
                                 const QVector<SampleFrame> &frames,
                                 QString *errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        return false;
    }

    QTextStream out(&file);
    out << "# vital_monitor history v1\n";
    out << "sampleIndex,timestampMs,channelId,value\n";

    for (const SampleFrame &f : frames) {
        for (const ChannelSample &s : f.channels) {
            out << f.sampleIndex << ','
                << f.timestampMs << ','
                << s.channelId << ','
                << QString::number(s.value, 'f', 3) << '\n';
        }
    }

    out.flush();
    file.close();
    return true;
}

bool HistoryRecorder::loadFrames(const QString &filePath,
                                 QVector<SampleFrame> &outFrames,
                                 QString *errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg)
            *errorMsg = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return false;
    }

    outFrames.clear();

    QTextStream in(&file);
    quint64 currentIndex = 0;
    bool hasCurrent = false;
    SampleFrame current;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith(QStringLiteral("sampleIndex"), Qt::CaseInsensitive))
            continue; // 表头

        const QStringList parts = line.split(',');
        if (parts.size() < 4)
            continue;

        bool okIdx = false, okTs = false, okCh = false, okVal = false;
        const quint64 idx = parts.at(0).toULongLong(&okIdx);
        const qint64  ts  = parts.at(1).toLongLong(&okTs);
        const int     ch  = parts.at(2).toInt(&okCh);
        const double  val = parts.at(3).toDouble(&okVal);
        if (!okIdx || !okTs || !okCh || !okVal)
            continue;

        // 同一 sampleIndex 的多行合并为一帧。
        if (!hasCurrent || idx != currentIndex) {
            if (hasCurrent)
                outFrames.append(current);
            current = SampleFrame();
            current.sampleIndex = idx;
            current.timestampMs = ts;
            currentIndex = idx;
            hasCurrent = true;
        }

        ChannelSample s;
        s.channelId = ch;
        s.value = val;
        current.channels.append(s);
    }

    if (hasCurrent)
        outFrames.append(current);

    file.close();

    if (outFrames.isEmpty()) {
        if (errorMsg)
            *errorMsg = QStringLiteral("文件中没有有效数据");
        return false;
    }
    return true;
}
