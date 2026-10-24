#include "chart/ChartBridge.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace {
// 缂傚倷鐒﹂敋婵炲瓨锕㈠畷鐘诲传閸曨厼骞嶉柡澶婄墢椤㈠﹦绮ｅ☉銏℃櫖闁割偅绻€缁狀垰銆掑鈧崨顔界暚闂?Y 闁哄鍋炲娆撳春閵忊剝鍎熼柨鏃囧亹缁€澶愬箹鐎涙ɑ绀嬮柛搴㈡尦閹儳鈻庤箛鎾寸様 X 闁哄鍋涢悺銊╂偉閿濆洨椹抽柟鎯ф噽缁€鍡涙煏?
// 婵?Widgets 闂?TrendChartWidget 闁诲海鎳撻懟顖炲矗韫囨梻鈻旈柍褜鍓熼幊娑氣偓闈涙啞閻ｉ亶鏌℃担瑙勭凡闁逞屽墰閵嗗妲愬┑鍥┾攳婵犻潧锕﹀Σ鍫濃槈閹惧崬鈧锝為幒鏂哄亾閸︻厼浠ф鐐叉喘閹?
// 闁荤喐鐟ュΛ婊堬綖鎼淬劌鏋侀柛顐ゅ枔娴滎垶鏌涘▎妯虹仯婵炴彃娼￠獮鎺楀Ψ閿旂偓啸闁诲海鏁稿▍銏㈡濞嗘垶鍠?docs/QML闁哄鏅欓懗鏈电昂闁荤姴娲ら悺銊ノ?md闂佹寧绋戦ˇ顓㈠焵?
constexpr int kMarginLeft   = 64;
constexpr int kMarginRight  = 16;
constexpr int kMarginTop    = 16;
constexpr int kMarginBottom = 34;

const QColor kBgColor      (0x14, 0x18, 0x20);
const QColor kPlotBgColor  (0x0D, 0x10, 0x16);
const QColor kGridColor    (0x2A, 0x32, 0x40);
const QColor kGridMajorCol (0x3A, 0x46, 0x58);
const QColor kAxisColor    (0x6B, 0x7A, 0x90);
const QColor kTextColor    (0xB8, 0xC4, 0xD4);

/// 闂佸憡顨嗗ú鐔煎焵椤掍礁鍝哄ù婊堢畺閹瑧鎲撮崟顒侇啀婵炴垶鎸搁敃顏勵瀶濞差亝鏅柛顐犲灮鐢盯鏌涢妷銉モ挃闁轰降鍊濋幆鍐礋椤愩垼顔夐柣鐔哥懁閼冲爼鎮伴妷鈺佺煑闁挎繂鎳庨ˉ宀勬煛閸偄澧剧紒杈ㄧ箖缁嬪绱掑Ο缁橆啀闂佺顕栭崰姘跺极鏉堛劎鈻旀慨姗嗗墮椤倝鎮橀悙鎻掆偓濠氱嵁閸℃哎浜归柕蹇嬪灮婢跺嫰鏌涢幇顖氳敿缂佲偓鐎ｎ剚瀚婚柨婵嗙箳缁€鍡涙煏?
constexpr int kMaxPointsPerSeries = 20000;
/// 闂佸憡绋戦敃銊╊敆閻旂厧鍐€闁搞儺浜堕崬璺衡槈閹剧韬俊鐐そ婵?
constexpr int kMaxAlarms = 2000;
/// SQI 婵炶揪绲界花鑲╄姳椤掑倹瀚氶柕澶堝劗閸嬫捇宕楅崗纰辨Н闂佸搫娲╃亸娆撳吹鎼淬劌缁╅柛锔诲幗閺嗕即鏌ｈ箛鏂款€滃鐟帮躬閹肩懓顫濋崡鐐寸枃闂?
constexpr double kLowQuality = 0.35;
} // namespace

ChartBridge::ChartBridge(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // QQuickPaintedItem 婵帗绋掗…鍫ヮ敇缂佹鈻旂€广儱妫楁径宥夋煛閳ь剟宕奸弴鐘靛€為梺鍝勭Т濞诧絿鑺遍妸锔绢浄閻犱礁婀辩粈澶愮叓閸パ勫殗闁靛棗绻樺浼村箚瑜忕涵鈧梺鐟扮仛閹稿摜妲?
    // 闂佹寧绋戦悧蹇涙偤閹寸偟顩?Widgets 闂佺粯顨呴悧濠囨倵椤栫偞鐒婚柣妯款嚙濞咃繝鏌?setMouseTracking(true)闂佹寧绋戦ˇ顓㈠焵?
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(false);   // 闂佺厧鍟块張顒€鈻嶅▎鎴犵＞闁瑰鍋熸竟鎰槈閹惧磭效婵炴挸澧庨幉鐗堟媴閻熼偊娼曢梻浣诡儥閸ㄨ埖绻涢崶顒佹櫖婵炴垶鐟﹂柨銈囩磼閹呬虎婵犫偓椤忓棛纾兼俊顖氭惈閻撴垿鏌￠崘顓熺【鐎规洜鍠栭幃顏堫敆閳ь剛妲愰幋锕€瑙?

    // 闂備焦褰冪粔鍓佸垝椤栫偛瑙﹂柛顐ｇ箚鐎氭瑩鏌ㄥ☉娆戠叝缂佹柨顕弫顔款槻闁哥偞鎸冲畷锝夘敍濠靛棗骞嬮梺琛″亾閻犲搫鎼悡鍌氼熆閼哥數顣叉い?update 闁荤姴娲弨閬嶆儑娴煎瓨鏅柛顐ゅ枑濞堝爼鏌熺拠鈥虫灕闁归鍏樺畷?+ 闂佸憡绋戦敃銊╊敆閻旂厧鍐€闁搞儺浜堕崬鍫曟煥濞戞﹩妾х紒?
    // 闂?0 ms 闂佸憡顨嗗ú姗€顢曢懖鈹惧亾鐟欏嫮顣叉俊鐐插€垮畷鎶藉Ω閿曗偓鎯熼梺鍛婅壘婵傛梻绮╃€涙顩查悗锝傛櫆椤愮晫鈧敻鍋婃禍鐐虹嵁閸℃稑绀冮柛娑卞灡閻ｉ亶鎮归崶顒佹暠闁活亙鍗冲畷銉╁醇濠靛﹤鐝梺鐟扮摠閸旀帞绮╅弶鎴旀瀻?paint()闂?
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(0);
    connect(m_repaintTimer, &QTimer::timeout, this, &ChartBridge::flushRepaint);
}

ChartBridge::~ChartBridge() = default;

// ---------------------------------------------------------------------------
// 闁诲繒鍋熼崑鐐哄焵椤戭剙妫崯蹇涙⒒?
// ---------------------------------------------------------------------------

void ChartBridge::setVisibleIndexSpan(quint64 span)
{
    const quint64 bounded = qBound<quint64>(20, span, 100000);
    if (bounded == m_visibleSpan)
        return;
    m_visibleSpan = bounded;
    updateMapper();
    emit visibleIndexSpanChanged();
    emit viewportInfoChanged();
    requestRepaint();
}

void ChartBridge::setFollowLatest(bool on)
{
    if (m_followLatest == on)
        return;
    m_followLatest = on;
    if (on)
        updateMapper();
    emit followLatestChanged();
    emit viewportInfoChanged();
    requestRepaint();
}

void ChartBridge::setMultiAxis(bool on)
{
    if (m_multiAxis == on)
        return;
    m_multiAxis = on;
    m_backgroundDirty = true;
    emit multiAxisChanged();
    requestRepaint();
}

QString ChartBridge::viewportInfo() const
{
    return QStringLiteral("Viewport [%1 ~ %2], span %3 pts, %4")
        .arg(m_mapper.viewStartIndex())
        .arg(m_mapper.viewEndIndex())
        .arg(m_mapper.indexSpan())
        .arg(m_followLatest ? QStringLiteral("LIVE") : QStringLiteral("BROWSING"));
}

// ---------------------------------------------------------------------------
// 闂佽桨鑳舵晶妤€鐣垫笟鈧畷妤呭Ψ閵夈儳绋夐梺鎸庣☉濠€?+ 婵炴垶鎸婚懝鐐叏閻旇桨娌柛灞剧矋缁绢垶鏌熼幁鎺戝姤闁汇劎鍠栭幃浠嬧€﹂幒鏃傤槷婵炴垶鎸哥粔鍓佸垝閳╁啯浜?QML 閻庢鍠楀ú姗€骞栭幖浣规櫖?
// ---------------------------------------------------------------------------

void ChartBridge::setChannels(const QVector<ChannelConfig> &channels)
{
    m_channels = channels;

    // 闂佸湱顭堥ˇ鐢稿焵椤掍礁鍝哄ù婊堢畺閺屽苯顓奸崨顖滃闁瑰吋娼欑换鎰板垂椤忓牊鏅悘鐐跺Г閺呭ジ姊洪幓鎺旂缂佺粯宀搁幃鎯р枎韫囨挸娈ラ梺鍝勭墕椤︿即寮抽悢鐓庣妞ゆ洖妫涚粈鍕煕?Widgets 闂佺粯顨呴悧鐐垫濮樿泛违?
    QVector<ChannelSeries> newSeries;
    newSeries.reserve(channels.size());
    for (const ChannelConfig &c : channels) {
        ChannelSeries s;
        s.id = c.id;
        for (const ChannelSeries &old : m_series) {
            if (old.id == c.id) {
                s.points  = old.points;
                s.quality = old.quality;
                break;
            }
        }
        newSeries.append(s);
    }
    m_series = newSeries;

    m_backgroundDirty = true;
    updateMapper();
    emit viewportInfoChanged();
    requestRepaint();
}

void ChartBridge::appendFrames(const QVector<SampleFrame> &frames)
{
    if (frames.isEmpty())
        return;

    for (const SampleFrame &f : frames) {
        if (m_dataStart == 0 && m_dataEnd == 0) {
            m_dataStart = f.sampleIndex;
            m_dataEnd   = f.sampleIndex;
        } else {
            m_dataEnd   = qMax(m_dataEnd, f.sampleIndex);
            m_dataStart = qMin(m_dataStart, f.sampleIndex);
        }

        for (const ChannelSample &s : f.channels) {
            for (ChannelSeries &series : m_series) {
                if (series.id == s.channelId) {
                    series.points.append(QPointF(f.sampleIndex, s.value));
                    series.quality.append(s.quality);
                    break;
                }
            }
        }
    }

    // 闂傚倸瀚崝鏇㈠春濡ゅ懎纭€闁哄洠鎳囬崑鎾愁潩閺堥潧顥氶梺缁樺姌椤寮抽悢鍏兼櫖鐎光偓閸曨亞绱氶梺绋跨箰缁夊綊寮抽柆宥呯睄闁割偅娲橀敍鐔煎级閳哄倸濮屾い銏″灴瀹曟﹢宕ㄩ鐘虫喖闂佸搫鍟版慨宕囩箔閸岀偞鈷旈柟閭﹀墻閺夊姊婚埀顒傛啑閵堝倸浜?
    for (ChannelSeries &series : m_series) {
        if (series.points.size() > kMaxPointsPerSeries) {
            const int drop = series.points.size() - kMaxPointsPerSeries;
            series.points.remove(0, drop);
            if (series.quality.size() > kMaxPointsPerSeries)
                series.quality.remove(0, series.quality.size() - kMaxPointsPerSeries);
        }
    }

    if (m_followLatest) {
        updateMapper();
        emit viewportInfoChanged();
        // 闁诲骸婀遍崑鐐差渻閸屾鐔煎灳瀹曞洨顢呮繛鎴炴尭椤戝懓銇愯閺屽苯顓奸崶鈺佺闂佽桨鑳舵晶妤€鐣垫笟鈧畷鐘绘偖鐎靛摜顦╅梻鍌氭噹婢т粙宕?1 / 闂傚倸鎳庢晶浠嬪磻?5闂佹寧绋戦ˇ顓㈠焵?
        requestRepaint(computePlotRect().adjusted(-1, -1, 1, 1));
    } else {
        requestRepaint();
    }
}

void ChartBridge::appendAlarms(const QVector<AlarmEvent> &alarms)
{
    if (alarms.isEmpty())
        return;
    m_alarms.append(alarms);
    if (m_alarms.size() > kMaxAlarms)
        m_alarms.remove(0, m_alarms.size() - kMaxAlarms);
    requestRepaint();
}

void ChartBridge::clearData()
{
    for (ChannelSeries &series : m_series) {
        series.points.clear();
        series.quality.clear();
    }
    m_alarms.clear();
    m_dataStart = 0;
    m_dataEnd   = 0;
    m_followLatest = true;
    updateMapper();
    emit followLatestChanged();
    emit viewportInfoChanged();
    requestRepaint();
}

void ChartBridge::resetViewToLatest()
{
    setFollowLatest(true);
}

bool ChartBridge::exportImage(const QString &filePath) const
{
    if (filePath.isEmpty() || width() <= 0 || height() <= 0)
        return false;

    QImage image(QSize(qRound(width()), qRound(height())),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(kBgColor);

    QPainter p(&image);
    const_cast<ChartBridge *>(this)->paint(&p);
    p.end();

    return image.save(filePath);
}

// ---------------------------------------------------------------------------
// 闂備焦褰冪粔鍓佸垝椤栫偛瑙﹂柛顐ｇ箚鐎?
// ---------------------------------------------------------------------------

void ChartBridge::requestRepaint(const QRectF &rect)
{
    if (rect.isValid())
        m_pendingDirty = m_pendingDirty.isValid() ? m_pendingDirty.united(rect) : rect;

    if (m_repaintPending)
        return;
    m_repaintPending = true;
    m_repaintTimer->start();
}

void ChartBridge::flushRepaint()
{
    m_repaintPending = false;

    if (m_pendingDirty.isValid() && m_pendingDirty.width() < width() * 0.5) {
        // 缂備焦鍔曠€氼厼顭囬娑欐珷闁绘劕鎼▍銈夋煕閹烘鏁遍柡灞斤躬閺佸秴顫濋鈧☉褔鏌熺紒妯哄婵﹫绠撳畷锝呂熼崫鍕靛殭闂佸憡鐗曢幖顐︽偂濞嗘挸违?
        // @note QQuickItem::update() 闂佸憡鐟禍婵堟暜閹绢喖鐭楁俊銈勭劍濞堝鏌?QRect闂佹寧绋戦顪榚ctF 闂婎偄娲ら幊姗€濡磋箛娑樼闁割偅绻傜徊鍧楁煛娴ｈ姤褰х紒?
        //       闂?toAlignedRect() 闂佸吋婢樻總鏃傜箔婢舵劕鍙?toRect()闂佹寧绋掗懝楣冨箖濠婂嫬绶為柡宥冨妼閳锋牜鈧鍠氭慨宕囩博閹绢喖鍐€缁绢參顥撶粈?
        //       闂備緡鍓欓悘婵嬪储閵堝鐐婇柣妯诲絻缁插潡鏌℃担鍝ユ憼濠碘剝鎮傚顒勬偋閸喐顔嶉梺纭咁嚃閸犳艾鈻撻幋锕€瀚夐柍褜鍓熷畷?1px 闂佸憡甯掑ú锔炬暜閳ь剟鏌ｉ敐鍡欐噥缂佹鎳樺顔款槻闁稿孩姘ㄥΣ鎰版偐瀹曞洦娈梺浼欑稻閻燂綁鍩€?
        update(m_pendingDirty.toAlignedRect());
    } else {
        update();
    }
    m_pendingDirty = QRectF();
}

// ---------------------------------------------------------------------------
// 闂佸憡鍨规慨宕囩礊?/ 闁荤喐鐟ュΛ妤勩亹?
// ---------------------------------------------------------------------------

QRectF ChartBridge::computePlotRect() const
{
    // 婵犮垼鍩栨穱娲煀閸楃倣鐔煎灳瀹曞洨顢呮繛鎴炴尭椤戝懓銇愰崨濠勭懝鐟滄柨銆掗懜纰夌矗闁告洦鍘藉▓宀勬煕閹烘洦鍤欓悗鐟扮－缁艾煤椤忓拑绱甸梺鎸庣☉閼活垶骞嗘笟鈧畷姘枎韫囨挾顦﹂柡澶屽仜閻°劑寮搁崘顏佸亾濞戞瑣浠︾紒杈ㄥ閹奸箖宕ㄩ鐐寸様闂佺鐭囬崒娑卞仺闁荤偞鍑归崑澶愃夐崱娑樼濠㈣泛鐗冮崑?
    int rightMargin = kMarginRight;
    if (m_multiAxis) {
        int visibleCount = 0;
        for (const ChannelConfig &c : m_channels)
            if (c.visible)
                ++visibleCount;
        if (visibleCount / 2 > 0)
            rightMargin = qMax(kMarginRight, 58);
    }

    return QRectF(kMarginLeft,
                  kMarginTop,
                  qMax(1.0, width()  - kMarginLeft - rightMargin),
                  qMax(1.0, height() - kMarginTop  - kMarginBottom));
}

void ChartBridge::updateMapper()
{
    m_mapper.setPlotRect(computePlotRect());

    quint64 start = 0;
    quint64 end   = 0;
    const quint64 span = qMax<quint64>(1, m_visibleSpan);

    if (m_dataEnd == 0 && m_dataStart == 0) {
        end = 0;
        start = 0;
    } else if (m_followLatest) {
        end = m_dataEnd;
        start = (end >= span) ? (end - span + 1) : 0;
    } else {
        end = m_dragStartIndex;
        start = (end >= span) ? (end - span + 1) : 0;
    }

    m_mapper.setIndexRange(start, end);

    for (const ChannelConfig &c : m_channels)
        m_mapper.setValueRange(c.id, c.minValue, c.maxValue);
}

void ChartBridge::clampView()
{
    if (m_dragStartIndex < m_dataStart)
        m_dragStartIndex = m_dataStart;
    if (m_dataEnd != 0 && m_dragStartIndex > m_dataEnd)
        m_dragStartIndex = m_dataEnd;
}

// ---------------------------------------------------------------------------
// 缂傚倷鐒﹂敋闁?
// ---------------------------------------------------------------------------

void ChartBridge::paint(QPainter *painter)
{
    if (m_backgroundDirty
        || m_background.size() != QSize(qRound(width()), qRound(height())))
        rebuildStaticBackground();

    // 1) 闂佺厧鍟块張顒€鈻嶅▎鎾存櫖婵椴哥痪顖炴煙閹帒鍔ラ柛鎺撶⊕閿涙劙宕熼鍐炬船闂佸搫鏈幐鍓ф閿旈敮鍋撳☉娆欏叕缂佽鲸鐟︾粙澶婎吋婢跺﹥顏熸繝銏ｆ硾缁夊爼寮?40+ 闂佸搫顧€缁辨洜绱炴径鎰唨闁兼祴鏅涘▓鐘绘煥濞戞﹩妲堕柍?
    painter->drawImage(0, 0, m_background);

    const QRectF plot = computePlotRect();

    // 2) 闂佸搫娲╃亸娆撳吹鎼淬垻鈻旈幖绮瑰墲閸熺偤鎮归埀顒勬晜閻ｅ苯鐏遍柣鐘辨祰閸╁洭鍩€椤掆偓閸婃悂路閸愵喗鐓傞煫鍥ㄦ尭閻撴洟鏌￠崒婊冾暭婵?QQuickPaintedItem 閻庡湱顭堝鑸典繆閹间焦鍋ㄩ柤濮愬€楅幊澶愭偡娴ｉ顦﹀鍏夊亾闂?
    //    update() 婵炵鍋愭慨鎾矗閸℃稒鍎嶉柛鏇ㄥ幘閸欐劗鎲搁柇锔界窙缂佽鲸绻冨璇测枎閹捐櫕顏為梺鍛婂姇缁夊磭绮畝鈧槐鎺懳熼悜妯碱洯闂佸憡鐗滈崕銈夋儑閻楀牏顩查柕鍫濈箳缁€澶愭煕濞嗗繒鐒搁柛锝呮啞缁屽崬鈹戞繝鍕潔婵炴垶鎸哥粔椋庣矓濞差亝鍋╅悘鐐靛劦閸?
    const QRegion clip = painter->clipRegion();
    const QRectF dirty = clip.isEmpty()
                             ? plot
                             : plot.intersected(QRectF(clip.boundingRect()));

    drawCurves(*painter, dirty);
    drawAlarmMarkers(*painter, plot);
    drawLegend(*painter);

    // 3) 闂佸憡锚濡銇愮捄濂界绠涘鍏肩秾濠碘槅鍨埀顒€纾涵鈧梺鍦帛閸旀濡靛鑸垫櫖闁割偁鍨婚幖鐓幟?Widgets 闂?paintEvent 闂佸搫鐗滈崑鍕偄椤掑嫭鍎嶉柛鏇ㄥ墮缁插湱绱掗埀顒勬倻濡崵鈧噣鎮楀☉娆樻缂佽鲸宀告俊?
    if (!m_followLatest) {
        painter->setPen(QColor(0xF3, 0x9C, 0x12));
        painter->drawText(QRectF(plot.left(), plot.top() + 4, plot.width() - 8, 16),
                          Qt::AlignRight | Qt::AlignTop,
                          QStringLiteral("BROWSING - double click to resume live"));
    }
}

void ChartBridge::rebuildStaticBackground()
{
    const int w = qRound(width());
    const int h = qRound(height());
    if (w <= 0 || h <= 0)
        return;

    m_background = QImage(QSize(w, h), QImage::Format_ARGB32_Premultiplied);
    m_background.fill(kBgColor);

    QPainter p(&m_background);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRectF plot = computePlotRect();
    p.fillRect(plot, kPlotBgColor);

    drawGrid(p);
    drawAxes(p);
    p.end();

    m_backgroundDirty = false;
}



void ChartBridge::drawGrid(QPainter &p) const
{
    const QRectF plot = computePlotRect();

    QPen minorPen(kGridColor);
    minorPen.setWidth(1);

    constexpr int kXDivisions = 10;
    for (int i = 0; i <= kXDivisions; ++i) {
        const double x = plot.left() + plot.width() * i / kXDivisions;
        p.setPen(minorPen);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    constexpr int kYDivisions = 8;
    for (int i = 0; i <= kYDivisions; ++i) {
        const double y = plot.top() + plot.height() * i / kYDivisions;
        p.setPen((i == 0 || i == kYDivisions) ? QPen(kGridMajorCol) : minorPen);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
}

void ChartBridge::drawAxes(QPainter &p) const
{
    const QRectF plot = computePlotRect();

    QPen axisPen(kAxisColor);
    axisPen.setWidth(1);
    p.setPen(axisPen);
    p.drawLine(plot.topLeft(), plot.bottomLeft());
    p.drawLine(plot.bottomLeft(), plot.bottomRight());

    p.setPen(kTextColor);
    QFont f = p.font();
    f.setPointSizeF(8.5);
    p.setFont(f);

    constexpr int kDivisions = 5;
    for (int i = 0; i <= kDivisions; ++i) {
        const double x = plot.left() + plot.width() * i / kDivisions;
        const quint64 idx = m_mapper.viewStartIndex()
                          + m_mapper.indexSpan() * static_cast<quint64>(i) / kDivisions;
        const QString label = QStringLiteral("t+%1s").arg(idx / 10);
        p.drawText(QRectF(x - 40, plot.bottom() + 4, 80, 16),
                   Qt::AlignHCenter | Qt::AlignTop, label);
    }

    if (m_multiAxis)
        drawChannelScales(p);
}

void ChartBridge::drawChannelScales(QPainter &p) const
{
    // 濠殿噯绲界换姗€鍩€椤掍礁鍝哄ù婊堢畺閹亪顢楅崒婊冪秳 Y 闁哄鍋炲娆撳春閵忊剝鍎熼柨鏃囧亹缁愭顭块懜闈涘摵闁逞屽墯瀹€鍛婄妤ｅ啯鐓傞煫鍥ㄦ磵閺屻倗鈧懓澹婇崰鏍閹捐鍑犲ù锝嗘偠娴滐綁鏌ㄥ☉妯荤R 40~180 / SpO2 85~100 /
    // Temp 34~42闂佹寧绋戦¨鈧紒杈ㄧ箞瀹曟娼忛埡鍌涙婵炴垶鎸撮崑鎾绘煛瀹ュ浂鏀伴梺鎷岄哺鐎电厧顫濋鐐嶁晠鎮跺☉鎺戜壕濠殿喗顭嗛崘銊ь伝闂佺懓鐡ㄩ崝鎺旂博閹绢喖绾ч柨娑樺缁绢垳绱掗幆褍鐨＄紒杈ㄧ箘閳ь剛鎳撻懟顖炲矗韫囨稒鍎戦悗锝傛櫇閻熸繈鏌涢幋婵囨儓鐟滄澘顦靛畷鐘诲冀閵夈垹浜?
    const QRectF plot = computePlotRect();

    QVector<const ChannelConfig *> vis;
    for (const ChannelConfig &c : m_channels)
        if (c.visible)
            vis.append(&c);
    if (vis.isEmpty())
        return;

    constexpr int kRightAxisWidth = 52;
    const int nLeft  = (vis.size() + 1) / 2;
    const int nRight = vis.size() / 2;

    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);

    constexpr int kDivisions = 4;

    for (int ci = 0; ci < vis.size(); ++ci) {
        const ChannelConfig *c = vis.at(ci);
        const bool onLeft = (ci < nLeft);
        const int slot = onLeft ? ci : (ci - nLeft);
        const int slotsOnSide = onLeft ? nLeft : qMax(1, nRight);
        const int slotWidth = onLeft
                                  ? qMax(18, (kMarginLeft - 6) / qMax(1, slotsOnSide))
                                  : qMax(18, kRightAxisWidth / qMax(1, slotsOnSide));

        p.setPen(c->color);

        for (int i = 0; i <= kDivisions; ++i) {
            const double ratio = static_cast<double>(i) / kDivisions;
            const double y = plot.bottom() - ratio * plot.height();
            const double v = c->minValue + ratio * (c->maxValue - c->minValue);

            if (onLeft) {
                const double x = 2 + slot * slotWidth;
                p.drawText(QRectF(x, y - 8, slotWidth - 2, 16),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(v, 'f', 1));
            } else {
                const double x = plot.right() + 2 + slot * slotWidth;
                p.drawText(QRectF(x, y - 8, slotWidth - 2, 16),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(v, 'f', 1));
            }
        }

        // 闁哄鍋為幐鎶藉Υ婵犲嫮鍗氭い鏍ㄧ〒閸ㄥ吋绻涙径鍫濆闁逞屽墯瀹€鍛婄閸撗呯＝闁宠桨绀侀弲鎼佹煥濞戞ɑ婀版俊鍙夊灩濞戠敻顢欏▎鎯ф畽闂佸憡甯楅〃鍛村春閵忊剝鍎熼柨鏇楀亾闁活偅蓱缁傚秵鎯旈埄鍐╁枦婵炴垶鎼╂禍顏堝焵椤掍礁鍝哄ù婊堢畺婵?
        const QString abbr = c->name.section(QLatin1Char(' '), -1);
        p.setPen(c->color);
        if (onLeft) {
            const double x = 2 + slot * slotWidth;
            p.drawText(QRectF(x, plot.top() - 14, slotWidth - 2, 12),
                       Qt::AlignRight | Qt::AlignVCenter, abbr);
        } else {
            const double x = plot.right() + 2 + slot * slotWidth;
            p.drawText(QRectF(x, plot.top() - 14, slotWidth - 2, 12),
                       Qt::AlignLeft | Qt::AlignVCenter, abbr);
        }
    }
}

void ChartBridge::drawCurves(QPainter &p, const QRectF &clipRect) const
{
    p.save();
    p.setClipRect(clipRect);
    p.setRenderHint(QPainter::Antialiasing, true);

    const quint64 vStart = m_mapper.viewStartIndex();
    const quint64 vEnd   = m_mapper.viewEndIndex();

    for (const ChannelConfig &cfg : m_channels) {
        if (!cfg.visible)
            continue;

        const ChannelSeries *series = nullptr;
        for (const ChannelSeries &s : m_series) {
            if (s.id == cfg.id) { series = &s; break; }
        }
        if (!series || series->points.size() < 2)
            continue;

        QPen pen(cfg.color);
        pen.setWidthF(1.6);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        // 闂佸憡鐟禍鐐哄极閵堝洦鍠嗛柛鈩冾殔缂嶆捇鏌ら悡搴℃殭婵炴彃娼″畷姗€宕ㄩ娑氭殸闂佽桨鑳舵晶妤€鐣垫笟鈧鎼佸礋椤愩倗澶勯梺纭呭煐椤洭宕垫惔锝嗗磯妞ゆ牗姘ㄧ粣鐐烘煥濞戞澧曢柣顏嶅墴閺屽牓濡搁埡浣诡仧缂傚倷鐒﹂敋鐎殿喖鎲＄缓浠嬪棘閵堝洨顦梺?
        // 濡ょ姷鍋犻崺鏍偓鍨矋缁岄亶鍨鹃崘鎻掆枏闁荤姵鍔戦崝鎴﹀闯濞差亜绀嗛柛鈩冿供閸炰粙鏌ㄥ☉娆戞憘QI 闁哄鏅涘ú锝囩礊閸℃稑缁╅柛锔诲幗閺嗕即鏌ｈ箛鏂款€滃鐟帮躬閹肩懓顫濋崡鐐寸枃闂佹寧绋戦張顒冦亹娴ｈ櫣鐭嗛柟顓熷坊閸嬫挸鐣濋崘鎯ф畽濠电偛鐗呯徊濠氼敋娴兼潙鏋佸Λ棰佽兌閻熸繈鏌涘▎妯虹仸婵炵⒈鍓熸俊鏉戭吋閸愶絽浜?
        QPainterPath goodPath;
        QPainterPath poorPath;
        bool startedGood = false;
        bool startedPoor = false;

        QPointF lastPoint;
        bool haveLast = false;
        bool lastWasPoor = false;

        for (int i = 0; i < series->points.size(); ++i) {
            const QPointF &pt = series->points.at(i);
            const quint64 idx = static_cast<quint64>(pt.x());
            if (idx < vStart || idx > vEnd)
                continue;

            const QPointF px = m_mapper.pointFor(cfg.id, idx, pt.y());
            const double q = (i < series->quality.size()) ? series->quality.at(i) : 1.0;
            const bool poor = (q < kLowQuality);

            // 闁荤姵鍔戦崝鎴﹀闯濞差亝鍋愰柤鍝ヮ暯閸嬫挻鎷呯粙璺ㄢ偓濂告煙楠炲灝鐏俊鐐插€垮畷姘跺炊閵婏妇鍔烽柣鐘辫閸ㄦ壆娆㈤悙鐑樻櫖鐎光偓閸曨亞绱氶梺绋跨箰缁夊灚淇婇幖浣哥煑妞ゆ牗鐟ょ换鍡椻槈閹惧磭鍑圭紒妤€顦靛畷锝夘敍濞嗗海绠氬┑鐐茬墔缁茶崵鎹㈠☉銏犵闁归偊浜為閬嶆煛婢舵稓纾块柛銈呴叄婵?
            if (haveLast && poor != lastWasPoor) {
                if (poor) {
                    poorPath.moveTo(lastPoint);
                    poorPath.lineTo(px);
                    startedPoor = true;
                    startedGood = false;
                } else {
                    goodPath.moveTo(lastPoint);
                    goodPath.lineTo(px);
                    startedGood = true;
                    startedPoor = false;
                }
            } else if (poor) {
                if (!startedPoor) { poorPath.moveTo(px); startedPoor = true; }
                else poorPath.lineTo(px);
            } else {
                if (!startedGood) { goodPath.moveTo(px); startedGood = true; }
                else goodPath.lineTo(px);
            }

            lastPoint   = px;
            haveLast    = true;
            lastWasPoor = poor;
        }

        if (!goodPath.isEmpty()) {
            p.setPen(pen);
            p.drawPath(goodPath);
        }
        if (!poorPath.isEmpty()) {
            QPen dashPen(QColor(0x88, 0x90, 0x9C));
            dashPen.setWidthF(1.2);
            dashPen.setStyle(Qt::DashLine);
            p.setPen(dashPen);
            p.drawPath(poorPath);
        }
    }

    p.restore();
}

void ChartBridge::drawAlarmMarkers(QPainter &p, const QRectF &clipRect) const
{
    if (m_alarms.isEmpty())
        return;

    p.save();
    p.setClipRect(clipRect);
    p.setRenderHint(QPainter::Antialiasing, true);

    const quint64 vStart = m_mapper.viewStartIndex();
    const quint64 vEnd   = m_mapper.viewEndIndex();

    quint64 lastDrawnIndex = std::numeric_limits<quint64>::max();

    for (const AlarmEvent &a : m_alarms) {
        if (a.sampleIndex < vStart || a.sampleIndex > vEnd)
            continue;

        bool visible = false;
        for (const ChannelConfig &cc : m_channels) {
            if (cc.id == a.channelId) { visible = cc.visible; break; }
        }
        if (!visible)
            continue;

        const double x = m_mapper.xForIndex(a.sampleIndex);

        // 婵＄偑鍊曢悥濂稿磿閹绢喖宸濆┑鐘叉媼閸斿懎鈽夐幘鎰佹Ъ妞ゎ偅顨婇弫宥呪槈閵忊晜婢掗梺鑲╊攰瀵挸顕ｅΔ鍛唨闁绘挸瀵掗崵鐘差潡?IEC 60601-1-8闂佹寧绋戦懟顖炴嚐閻斿摜鈻旈幖娣灩琚氶柣鐘亾闁挎棁濮ら拏?/ 闂佽桨鐒﹀姗€鍩€椤掑倸甯剁€规洟浜堕幃?
        // 闂佺绻楁禍顒勫极?alarmIndicatorColor()闂佹寧绋戦惌鍌涘閳哄懎绀傜€广儱鎳忛崐鍗炩槈閹绢垰浜鹃梺鍛婄☉閿曘劑顢楅悢鐓庡窛闁割偁鍨圭粻娑樷槈閹惧磭孝闁诡喗鎸婚敍鎰煥閸℃妫岄梺?
        const double ty = clipRect.top() + 6;
        QPolygonF tri;
        tri << QPointF(x, ty)
            << QPointF(x - 5, ty - 8)
            << QPointF(x + 5, ty - 8);

        const QColor markColor = alarmIndicatorColor(a.level);
        p.setBrush(markColor);
        p.setPen(QPen(markColor.darker(140), 1));
        p.drawPolygon(tri);

        // 婵°倕鍊圭湁缂侀硸鍙冨畷妤呭醇閵忋倗宕滈梺鍛婂姇缁夊瓨鎱ㄩ悙瀵糕枖闁逞屽墴瀹曠兘宕奸姀鈽嗕槐濠碘剝顨呭Λ瀵告濠靛棭鍤曢柛婵嗗椤曆囨偡濞嗗繑顥℃い顐㈤叄瀵即骞戦幇顓炴毐闂佽鍎告担鎻掍壕?
        if (a.level == AlarmEvent::Critical || a.level == AlarmEvent::Escalated) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1));
            QPolygonF outer;
            outer << QPointF(x, ty + 1.5)
                  << QPointF(x - 6.5, ty - 9.5)
                  << QPointF(x + 6.5, ty - 9.5);
            p.drawPolygon(outer);
        }

        // 闂佸搫娲╃亸娆撳吹鎼淬垻鈻斿┑鐘插閻ｉ亶鎮楅崷顓炰槐婵＄虎鍨抽幖鐐節閸ヮ煈鈧瑩鏌ｉ幇顒佽础闁轰胶鍋撶粙澶愬焵椤掍胶鈻旀い蹇撳濮ｅ牓鏌涢敃鈧Λ娑㈠磻閿濆违?
        if (a.sampleIndex != lastDrawnIndex) {
            const QPointF dot = m_mapper.pointFor(a.channelId, a.sampleIndex, a.value);
            p.setBrush(markColor);
            p.drawEllipse(dot, 2.5, 2.5);
            lastDrawnIndex = a.sampleIndex;
        }
    }

    p.restore();
}

void ChartBridge::drawLegend(QPainter &p) const
{
    p.save();
    QFont f = p.font();
    f.setPointSizeF(8.5);
    p.setFont(f);

    int x = kMarginLeft + 8;
    const int y = kMarginTop + 6;

    for (const ChannelConfig &c : m_channels) {
        p.setBrush(c.visible ? c.color : QColor(0x50, 0x58, 0x64));
        p.setPen(Qt::NoPen);
        p.drawRect(x, y, 10, 10);

        p.setPen(c.visible ? kTextColor : QColor(0x60, 0x68, 0x74));
        const QString txt = QStringLiteral("%1(%2)").arg(c.name, c.unit);
        const QRectF tr(x + 14, y - 3, 150, 16);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, txt);
        x += 22 + qRound(QFontMetricsF(p.font()).horizontalAdvance(txt)) + 14;
    }

    p.restore();
}


// ---------------------------------------------------------------------------
// 婵炲瓨鍤庨崐鎾惰姳娴煎瓨鏅慨姗嗗幘濞夊﹪寮堕悜鍡楁灓缂傚倸顦靛?/ 闂佸綊鏀遍悧妤冣偓姘健閻涱喖鐣烽崶鍡忔櫊閺佸秹宕煎┑鍡橀敪闂佽　鍋撶痪顓炴媼濞兼帡鏌涘▎娆戠Ф缂佽鲸绻冪粙澶婎吋閸涱厜妤呮煕濡厧鏋庢い顐ｅ姍瀵偊鎮ч崼婵堛偊闂?
// ---------------------------------------------------------------------------

void ChartBridge::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const double factor = (delta > 0) ? 0.8 : 1.25;
    quint64 newSpan = static_cast<quint64>(m_visibleSpan * factor);
    newSpan = qBound<quint64>(20, newSpan, 100000);

    if (newSpan != m_visibleSpan) {
        m_visibleSpan = newSpan;
        updateMapper();
        emit visibleIndexSpanChanged();
        emit viewportChanged(m_mapper.viewStartIndex(), m_mapper.viewEndIndex());
        emit viewportInfoChanged();
        requestRepaint();
    }
    event->accept();
}

void ChartBridge::mousePressEvent(QMouseEvent *event)
{
    // QQuickPaintedItem 婵炴垶鎸哥粔瀛樼濞戙垺鐒婚柟閭﹀墮閸撹偐绱掓潏鈺佇㈤柟鏂ュ墲缁嬪顢橀悢鎭掑仦闁荤姴娲﹀ú鈺冩娴兼潙缁╅悹鍝勬惈閻?press 闂佸憡顨呴崢鏍船椤掑倹鍋橀柕濞炬櫇閻愬﹪鏌″鍛悙闁告埊绱曢幃顕€顢曢敐鍡樺婵＄偑鍊涢～澶嬫叏閸曨垰鐭楅柡宥冨劗閸?
    m_dragging = true;
    m_dragLastPos = event->position();
    // 闁哄鏅滅粙鎴﹀矗閸℃稑绠柡宥冨妼椤忓ジ鏌涘Δ鈧崯鍧楀垂韫囨稑绀嗛柡澶婄仢閸у﹪鏌涘▎蹇撶骇缂佷浇宕甸幉鎾醇閻斿奔绀侀锝堢疀閵壯咁槷闂佸憡鍔樺畷鐢靛垝閵娧勫枂闁糕剝顨呯紞鎾绘偣閾忕懓鏋涙繛鍫涘灲婵?
    if (m_followLatest) {
        m_followLatest = false;
        m_dragStartIndex = m_dataEnd;
        emit followLatestChanged();
        emit viewportInfoChanged();
    }
    event->accept();
}

void ChartBridge::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        event->ignore();
        return;
    }

    const qreal dx = event->position().x() - m_dragLastPos.x();
    m_dragLastPos = event->position();

    const double plotWidth = qMax(1.0, computePlotRect().width());
    // 闂佺绉寸换鎺旂矆鐏炵偓濯寸€广儱娉?闂?闂備焦褰冨ú锕傛偋鏉堛劍鍎熼煫鍥ㄦ尭婵炲洤霉閿濆懐肖娴滄盯鏌ㄥ☉妯煎闁诡喗绮撳畷锝夋惞椤愩垻褰?= 闂佹椿浜滈鍡椕洪崸妤€绫嶉柍鍝勫暞閻ｉ亶鏌℃担鍝勵暭鐎规挷绶氶弫宥嗗緞閸艾浜?
    const double indexPerPixel = static_cast<double>(m_visibleSpan) / plotWidth;
    const qint64 delta = static_cast<qint64>(-dx * indexPerPixel);

    qint64 target = static_cast<qint64>(m_dragStartIndex) + delta;
    if (target < 0)
        target = 0;
    m_dragStartIndex = static_cast<quint64>(target);
    clampView();

    updateMapper();
    emit viewportChanged(m_mapper.viewStartIndex(), m_mapper.viewEndIndex());
    emit viewportInfoChanged();
    requestRepaint();
    event->accept();
}

void ChartBridge::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        // 闂佸綊鏀遍悧妤冣偓姘虫硶缁辨帡骞樼€涙ê鐨㈤梺鍛婅壘妤犲憡绂掑☉娆戔枖闁逞屽墮閳诲酣鍨惧畷鍥ㄦ闂佽桨鐒﹀娆撳春濞戙垹妫橀悽顖ｅ枤缁€澶愭⒑椤掆偓閻忔繈宕㈤妶鍡樹氦闁搞儮鏂侀弻銈呪槈閹垮啫寮い锝呭閹广垺鎷呯粙鍨紟闂備焦褰冪换姗€宕抽崫銉х＜婵☆垳纭堕崑?
        requestRepaint();
    }
    event->accept();
}

void ChartBridge::mouseUngrabEvent()
{
    // 缂備焦鍔栭〃鍛般亹濞戞瑥绶為弶鍫氭櫅婵?/ 婵崿鍛ｉ柣鏍电悼閹噣顢曢敐鍜佹降闁荤姍鍛暭婵＄偛鍊荤槐鎺楀箻鐎涙ê鐨㈤梺褰掓敱閻楁鈧艾娼￠弫宥呯暆閸曨亞绱氶梺绋跨箰缁夋潙鐣烽柆宥呮嵍闁靛绠戦悵娑㈡煙妞嬪孩娅曞┑顔芥倐楠炩偓濞达絿顣介崑?
    m_dragging = false;
    QQuickPaintedItem::mouseUngrabEvent();
}

