#include "AlarmBanner.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

namespace {

/// 严重度对应的底色（视觉报警级别：Critical 红 / Warning 黄 / Info 青）。
/// 复用 SampleData.h 中的统一定义，保证与曲线标记 / 数值面板完全一致。
QColor levelColor(AlarmEvent::Level lv)
{
    return alarmIndicatorColor(lv);
}

/// 严重度对应的中文名（用于横幅文案）。
QString levelName(AlarmEvent::Level lv)
{
    switch (lv) {
    case AlarmEvent::Critical:  return QStringLiteral("严重");
    case AlarmEvent::Escalated: return QStringLiteral("升级");
    case AlarmEvent::Warning:   return QStringLiteral("一般");
    case AlarmEvent::Info:
    default:                    return QStringLiteral("提示");
    }
}

/// 闪烁周期：严重度越高闪得越快（符合 IEC 60601-1-8 分级提示思路）。
int blinkIntervalMs(AlarmEvent::Level lv)
{
    switch (lv) {
    case AlarmEvent::Critical:
    case AlarmEvent::Escalated: return 300;
    case AlarmEvent::Warning:   return 550;
    default:                    return 900;
    }
}

/// 提示音重复周期：严重度越高越急促。
int soundIntervalMs(AlarmEvent::Level lv)
{
    switch (lv) {
    case AlarmEvent::Critical:
    case AlarmEvent::Escalated: return 900;
    case AlarmEvent::Warning:   return 1800;
    default:                    return 3000;
    }
}

} // namespace

AlarmBanner::AlarmBanner(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("AlarmBanner"));
    setMinimumHeight(34);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 10, 4);
    lay->setSpacing(8);

    m_textLabel = new QLabel(this);
    QFont tf = m_textLabel->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 0.5);
    m_textLabel->setFont(tf);
    m_textLabel->setStyleSheet(QStringLiteral("color:#FFFFFF; background:transparent;"));
    lay->addWidget(m_textLabel, 1);

    m_ackBtn = new QPushButton(QStringLiteral("确认"), this);
    m_ackBtn->setFixedWidth(70);
    m_ackBtn->setToolTip(QStringLiteral("确认并静音当前报警（告警条件仍持续时会按周期再次提醒）"));
    lay->addWidget(m_ackBtn);

    m_muteBtn = new QPushButton(QStringLiteral("静音: 关"), this);
    m_muteBtn->setCheckable(true);
    m_muteBtn->setFixedWidth(84);
    m_muteBtn->setToolTip(QStringLiteral("关闭/开启声音提示（不影响视觉报警）"));
    lay->addWidget(m_muteBtn);

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, &AlarmBanner::onBlinkTick);

    m_soundTimer = new QTimer(this);
    connect(m_soundTimer, &QTimer::timeout, this, &AlarmBanner::onSoundTick);

    connect(m_ackBtn, &QPushButton::clicked,
            this, &AlarmBanner::acknowledgeRequested);
    connect(m_muteBtn, &QPushButton::toggled, this, [this](bool on) {
        m_muteBtn->setText(on ? QStringLiteral("静音: 开") : QStringLiteral("静音: 关"));
        setMuted(on);
    });

    setVisible(false);
}

AlarmBanner::~AlarmBanner() = default;

void AlarmBanner::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    if (m_muted)
        m_soundTimer->stop();
    else if (hasAlarm())
        m_soundTimer->start(soundIntervalMs(topLevel()));
    emit mutedChanged(m_muted);
}

AlarmEvent::Level AlarmBanner::topLevel() const
{
    AlarmEvent::Level best = AlarmEvent::Info;
    bool found = false;
    for (const AlarmEvent &a : m_alarms) {
        if (!found || a.level > best) {
            best  = a.level;
            found = true;
        }
    }
    return found ? best : AlarmEvent::Info;
}

void AlarmBanner::setActiveAlarms(const QVector<AlarmEvent> &alarms)
{
    m_alarms = alarms;
    refresh();
}

void AlarmBanner::refresh()
{
    if (m_alarms.isEmpty()) {
        m_blinkTimer->stop();
        m_soundTimer->stop();
        m_blinkOn = true;
        setVisible(false);
        update();
        return;
    }

    const AlarmEvent::Level lv = topLevel();

    // 文案：最高严重度 + 活动告警条数 + 首条摘要。
    const QString first = m_alarms.first().text;
    m_textLabel->setText(QStringLiteral("[%1报警] 活动 %2 条 — %3")
                             .arg(levelName(lv))
                             .arg(m_alarms.size())
                             .arg(first));

    // 视觉：按严重度设定闪烁节奏。
    m_blinkTimer->start(blinkIntervalMs(lv));

    // 听觉：未静音时按严重度节奏发声。
    if (!m_muted && !m_soundTimer->isActive())
        m_soundTimer->start(soundIntervalMs(lv));

    setVisible(true);
    update();
}

void AlarmBanner::onBlinkTick()
{
    m_blinkOn = !m_blinkOn;
    update();
}

void AlarmBanner::onSoundTick()
{
    if (m_muted || m_alarms.isEmpty())
        return;

    // 严重度越高，连响次数越多（简易分级声提示）。
    const int beeps = (topLevel() >= AlarmEvent::Critical) ? 3 : 1;
    for (int i = 0; i < beeps; ++i)
        QApplication::beep();
}

void AlarmBanner::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    const AlarmEvent::Level lv = topLevel();
    QColor c = levelColor(lv);

    // 闪烁：暗相时降低亮度，形成明显视觉冲击。
    if (!m_blinkOn)
        c = c.darker(180);

    p.fillRect(rect(), c);

    // 左侧 4px 高亮条，强化边界识别。
    p.fillRect(QRect(0, 0, 4, height()), QColor(0xFF, 0xFF, 0xFF, m_blinkOn ? 220 : 90));

    // 底部细描边，与绘图区区分。
    p.setPen(QColor(0, 0, 0, 120));
    p.drawLine(0, height() - 1, width(), height() - 1);
}
