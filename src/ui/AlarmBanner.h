#ifndef ALARMBANNER_H
#define ALARMBANNER_H

#include <QWidget>
#include <QVector>

#include "data/SampleData.h"

class QLabel;
class QPushButton;
class QTimer;

/**
 * @brief 报警横幅控件（对应优化文档第一梯队第 1 项：报警声光指示）。
 *
 * 医疗器械要求「报警必须同时提供视觉与听觉指示」，因此本控件同时承担：
 *
 *  - **视觉**：顶部高对比横幅，按严重度切换底色并闪烁（闪烁频率随严重度提高）；
 *  - **听觉**：按严重度以不同节奏发出提示音，并通过静音开关控制；
 *  - **交互**：提供「确认」按钮，对应报警状态机中的 Acknowledged 状态。
 *
 * 设计要点：
 *  - 只负责「呈现 + 提示」，不参与任何告警判定逻辑；
 *  - 通过 setActiveAlarms() 接收当前活动告警集合，自行去重与排序；
 *  - 使用 QApplication::beep() 发声，避免引入多媒体依赖。
 */
class AlarmBanner : public QWidget
{
    Q_OBJECT

public:
    explicit AlarmBanner(QWidget *parent = nullptr);
    ~AlarmBanner() override;

    /**
     * @brief 更新当前活动告警集合（每次收到新告警/解除时调用）。
     *
     * 控件会依据严重度决定闪烁频率与提示音节奏；
     * 传入空列表表示当前无活动告警，控件自动隐藏。
     */
    void setActiveAlarms(const QVector<AlarmEvent> &alarms);

    /// 当前是否有需要提示的告警。
    bool hasAlarm() const { return !m_alarms.isEmpty(); }

    /// 声音是否处于静音状态。
    bool isMuted() const { return m_muted; }

    /// 设置静音（不影响视觉提示，仅关闭声音）。
    void setMuted(bool muted);

signals:
    /// 用户点击「确认」按钮，请求确认当前全部活动告警。
    void acknowledgeRequested();

    /// 静音状态变化。
    void mutedChanged(bool muted);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onBlinkTick();
    void onSoundTick();

private:
    /// 依据当前告警集合刷新文案与定时器参数。
    void refresh();

    /// 取当前最高严重度（无告警时返回 Info）。
    AlarmEvent::Level topLevel() const;

    QVector<AlarmEvent> m_alarms;

    QLabel      *m_textLabel = nullptr;
    QPushButton *m_ackBtn    = nullptr;
    QPushButton *m_muteBtn   = nullptr;

    QTimer *m_blinkTimer = nullptr;   ///< 视觉闪烁
    QTimer *m_soundTimer = nullptr;   ///< 声音节奏

    bool m_muted     = false;
    bool m_blinkOn   = true;
    bool m_soundOn   = true;
};

#endif // ALARMBANNER_H
