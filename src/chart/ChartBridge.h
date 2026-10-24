#ifndef CHARTBRIDGE_H
#define CHARTBRIDGE_H

#include <QQuickPaintedItem>
#include <QImage>
#include <QVector>

#include "data/SampleData.h"
#include "core/ChannelConfig.h"
#include "core/CoordinateMapper.h"

class QTimer;

/**
 * @brief QML 趋势图桥接控件（`QQuickPaintedItem`）。
 *
 * ## 为什么 QtCharts 不采用
 *
 * 1. **QtCharts** 对 6 万个历史点的实时刷新缺乏细粒度控制，无法按需做局部重绘与背景预渲染；
 * 2. 自行绘制可把绘制成本拆分为「背景预渲染一次 + 新增区域局部刷新」，在 10 Hz 刷新下更可控。
 *
 * 因此本项目的策略是：**绘图内核保持 C++/QPainter，QML 只承担外壳**。
 * 本类内部几乎原封不动复用了 Widgets 版 `TrendChartWidget` 的算法：
 *  - 静态网格背景预渲染到 `QImage`，`paint()` 只贴图（不重复画 40+ 条线）；
 *  - `appendFrames()` 用 `update(rect)` 只刷新新增数据覆盖的矩形；
 *  - 缩放 / 平移只改 `CoordinateMapper` 的视口参数，**原始数据一个都不动**；
 *  - 每通道独立 Y 轴刻度（左右分列、按通道配色）。
 *
 * @note 与 Widgets 版的差异：本类通过 `Q_PROPERTY` 向 QML 暴露
 *       `visibleIndexSpan` / `followLatest` / `multiAxis`，
 *       使 QML 侧可直接绑定开关，无需 C++ 中转槽函数。
 */
class ChartBridge : public QQuickPaintedItem
{
    Q_OBJECT

    /// 视口宽度（显示的采样点数）。QML 侧滑块 / 下拉框可直接绑定。
    Q_PROPERTY(quint64 visibleIndexSpan READ visibleIndexSpan
               WRITE setVisibleIndexSpan NOTIFY visibleIndexSpanChanged)
    /// 是否自动跟随最新数据（实时模式）；关闭后为历史浏览模式。
    Q_PROPERTY(bool followLatest READ followLatest WRITE setFollowLatest
               NOTIFY followLatestChanged)
    /// 是否启用每通道独立 Y 轴刻度。
    Q_PROPERTY(bool multiAxis READ multiAxis WRITE setMultiAxis
               NOTIFY multiAxisChanged)
    /// 视口提示文本（供状态栏 / 副标题显示）。
    Q_PROPERTY(QString viewportInfo READ viewportInfo NOTIFY viewportInfoChanged)
    /// 是否处于「历史浏览」状态（QML 用于显示提示横幅）。
    Q_PROPERTY(bool browsing READ browsing NOTIFY followLatestChanged)

public:
    explicit ChartBridge(QQuickItem *parent = nullptr);
    ~ChartBridge() override;

    void paint(QPainter *painter) override;

    quint64 visibleIndexSpan() const { return m_visibleSpan; }
    void setVisibleIndexSpan(quint64 span);

    bool followLatest() const { return m_followLatest; }
    void setFollowLatest(bool on);

    bool multiAxis() const { return m_multiAxis; }
    void setMultiAxis(bool on);

    QString viewportInfo() const;
    bool browsing() const { return !m_followLatest; }

    // ---- 供 C++ 业务层直接调用（数据热路径，不经过 QML）----

    /// 通道配置（名称 / 颜色 / 量程 / 可见性）变化。
    void setChannels(const QVector<ChannelConfig> &channels);

    /**
     * @brief 追加一批新采样数据。
     *
     * 这是**唯一的数据入口**：由 `MainWindowQml::onFrameReady()` 直接调用，
     * 不经过 QML 引擎，避免每帧构造 `QVariantList` 的开销。
     */
    void appendFrames(const QVector<SampleFrame> &frames);

    /// 追加告警事件（在曲线上画三角标记）。
    void appendAlarms(const QVector<AlarmEvent> &alarms);

    /// 清空曲线与告警数据。
    void clearData();

    /// 把视口复位到最新数据。
    Q_INVOKABLE void resetViewToLatest();

    /// 导出当前画布为图片（QML 侧 `exportImage` 动作）。
    Q_INVOKABLE bool exportImage(const QString &filePath) const;

    /// 每通道曲线与质量数据（只读，供测试断言与调试）。
    struct ChannelSeries {
        int              id = -1;
        QVector<QPointF> points;   ///< (sampleIndex, value) —— 存原始值，非像素
        QVector<double>  quality;  ///< 一一对应的 SQI
    };

signals:
    void visibleIndexSpanChanged();
    void followLatestChanged();
    void multiAxisChanged();
    void viewportInfoChanged();
    /// 视口范围变化（缩放 / 平移），供状态栏同步。
    void viewportChanged(quint64 startIndex, quint64 endIndex);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    /// @note QQuickItem 的鼠标抓取释放回调是 `mouseUngrabEvent()`（无参），
    ///       不是 `QEvent*` 版本；后者是 QGraphicsItem 的 API，
    ///       写错签名会导致 override 编译失败。
    void mouseUngrabEvent() override;

private slots:
    /// 合并一帧内的多次重绘请求（批量提交）。
    void flushRepaint();

private:
    void rebuildStaticBackground();
    QRectF computePlotRect() const;
    void updateMapper();
    void clampView();

    void drawGrid(QPainter &p) const;
    void drawAxes(QPainter &p) const;
    void drawCurves(QPainter &p, const QRectF &clipRect) const;
    void drawAlarmMarkers(QPainter &p, const QRectF &clipRect) const;
    void drawChannelScales(QPainter &p) const;
    void drawLegend(QPainter &p) const;

    /// 增量重绘去重：一帧内只提交一次 update()。
    void requestRepaint(const QRectF &rect = QRectF());

    QVector<ChannelConfig>  m_channels;
    QVector<ChannelSeries>  m_series;
    QVector<AlarmEvent>     m_alarms;

    CoordinateMapper m_mapper;
    QImage           m_background;      ///< 静态网格背景缓存
    bool             m_backgroundDirty = true;

    bool             m_multiAxis    = true;
    bool             m_followLatest = true;
    quint64          m_visibleSpan  = 600;

    quint64 m_dataStart = 0;
    quint64 m_dataEnd   = 0;

    // 交互状态
    bool    m_dragging       = false;
    QPointF m_dragLastPos;
    quint64 m_dragStartIndex = 0;

    // 重绘合并
    QRectF m_pendingDirty;
    bool   m_repaintPending = false;
    QTimer *m_repaintTimer  = nullptr;
};

#endif // CHARTBRIDGE_H
