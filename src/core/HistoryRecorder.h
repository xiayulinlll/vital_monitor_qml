#ifndef HISTORYRECORDER_H
#define HISTORYRECORDER_H

#include <QObject>
#include <QString>
#include <QVector>

#include "data/SampleData.h"

/**
 * @brief 历史数据存取模块（支撑流程 3 历史回放、功能清单第 6 条）。
 *
 * 文件格式：自定义轻量文本格式（CSV 变体），首行为列头，
 * 每行格式： sampleIndex,timestampMs,ch0=value,ch1=value,...
 *
 * 之所以不用二进制，是为了便于人工查看与调试；
 * 对医疗回放场景数据量有限，文本格式足够。
 */
class HistoryRecorder
{
public:
    /// 把采样帧序列保存到文件。返回是否成功。
    static bool saveFrames(const QString &filePath,
                           const QVector<SampleFrame> &frames,
                           QString *errorMsg = nullptr);

    /// 从文件加载采样帧序列。返回是否成功。
    static bool loadFrames(const QString &filePath,
                           QVector<SampleFrame> &outFrames,
                           QString *errorMsg = nullptr);
};

#endif // HISTORYRECORDER_H
