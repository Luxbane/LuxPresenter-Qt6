#include "Views/Projection/ProjectionWindow.h"

#include "Services/AlertService.h"
#include "Services/PageImageCache.h"
#include "Views/Components/TextRender.h"

#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace Lux {

namespace {
constexpr qreal kW = 1920;
constexpr qreal kH = 1080;

Qt::Alignment hAlignOf(const QString& a)
{
    if (a.compare(QStringLiteral("Left"), Qt::CaseInsensitive) == 0)
        return Qt::AlignLeft;
    if (a.compare(QStringLiteral("Right"), Qt::CaseInsensitive) == 0)
        return Qt::AlignRight;
    return Qt::AlignHCenter;
}

QFont fontOf(const SlideLayerData& d)
{
    QFont f(d.fontFamily.section(QLatin1Char(','), 0, 0).trimmed());
    f.setBold(d.bold);
    f.setItalic(d.italic);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}
// Footer (verse label / scripture reference) band and the text area left over, in design coordinates.
QRectF footerBand(const SlideLayerData& d, double left, double top, double bottom, double contentW)
{
    const LabelPlacement lp = labelPlacement(d.labelLocation);
    const qreal h = labelBandHeight(labelPixelSize(d.labelSize));
    const qreal indent = (d.labelIndent && lp.h != Qt::AlignHCenter) ? 60 : 0;
    const qreal x = left + (lp.h == Qt::AlignLeft ? indent : 0);
    const qreal w = contentW - indent - 10;
    return QRectF(x, lp.top ? top : kH - bottom - h, w, h);
}
QRectF textArea(const SlideLayerData& d, double left, double top, double bottom, double contentW)
{
    QRectF a(left, top, contentW, kH - top - bottom);
    if (d.footerVisible) {
        const qreal h = labelBandHeight(labelPixelSize(d.labelSize));
        if (labelPlacement(d.labelLocation).top)
            a.setTop(a.top() + h);
        else
            a.setBottom(a.bottom() - h);
    }
    return a;
}} // namespace

ProjectionWindow::ProjectionWindow(const ProjectionComposer* composer, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus), m_composer(composer)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("LuxPresenter Projection"));

    connect(&AlertService::instance(), &AlertService::alertStateChanged, this, [this] {
        if (AlertService::instance().isAlertVisible()) {
            if (!m_alertAnimTimer.isActive()) {
                m_alertPassStartTime = QDateTime::currentMSecsSinceEpoch();
                m_alertAnimTimer.start(16);
            }
        } else {
            m_alertAnimTimer.stop();
            m_alertPassStartTime = 0;
        }
        update();
    });
    connect(&m_alertAnimTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
}

void ProjectionWindow::preloadImage(const QString& path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('#')))
        return;
    if (!m_images.contains(path) && QFile::exists(path)) {
        const QPixmap pm(path);
        if (!pm.isNull())
            m_images.insert(path, pm);
    }
}

QPixmap ProjectionWindow::image(const QString& path)
{
    auto it = m_images.find(path);
    if (it == m_images.end())
        it = m_images.insert(path, QPixmap(path));
    return it.value();
}

qreal ProjectionWindow::layerFontSize(int layer)
{
    const SlideLayerData& d = m_composer->slideLayer(layer);
    if (!d.valid)
        return 0;
    if (m_fontSerial[layer] != d.serial) {
        const qreal contentW = m_composer->contentWidth();
        const auto m = m_composer->margins();
        const qreal maxH = textArea(d, m.left, m.top, m.bottom, contentW).height();
        const qreal maxTarget = d.basePt > 0 ? d.basePt * 6.5 : 84.5;
        m_fontSize[layer] = optimalFontSize(d.text, fontOf(d), contentW, maxH, maxTarget, 34.0, d.wrapText);
        m_fontSerial[layer] = d.serial;
    }
    return m_fontSize[layer];
}

void ProjectionWindow::paintLayer(QPainter* p, int layer)
{
    const SlideLayerData& d = m_composer->slideLayer(layer);
    if (!d.valid)
        return;
    const auto m = m_composer->margins();
    const qreal contentW = m_composer->contentWidth();
    const QRectF area = textArea(d, m.left, m.top, m.bottom, contentW);

    if (!d.text.isEmpty()) {
        QFont f = fontOf(d);
        f.setPixelSize(std::max(1, int(std::lround(layerFontSize(layer)))));
        TextBlockStyle st;
        st.font = f;
        st.width = contentW;
        st.strokeThickness = 5.0;
        st.outline = d.outline;
        st.shadow = d.shadow;
        st.shadowDepth = 5.0;
        st.hAlign = hAlignOf(d.hAlign);
        st.wrap = d.wrapText;
        const TextBlockMetrics tm = measureTextBlock(d.text, st);
        const qreal blockH = tm.height + 2 * st.strokeThickness;
        const qreal s = std::min({1.0, area.height() / blockH, contentW / (tm.width + 2 * st.strokeThickness)});
        const qreal drawH = blockH * s;
        qreal y = area.top();
        if (d.vAlign.compare(QStringLiteral("Bottom"), Qt::CaseInsensitive) == 0)
            y = area.bottom() - drawH;
        else if (d.vAlign.compare(QStringLiteral("Top"), Qt::CaseInsensitive) != 0)
            y = area.top() + (area.height() - drawH) / 2;
        p->save();
        p->translate(area.left() + contentW * (1 - s) / 2, y);
        p->scale(s, s);
        drawTextBlock(p, d.text, st, QPointF(0, 0));
        p->restore();
    }
    if (d.footerVisible && !d.footer.isEmpty()) {
        QFont f(QStringLiteral("Tahoma"));
        f.setBold(true);
        f.setPixelSize(int(labelPixelSize(d.labelSize)));
        p->setFont(f);
        const QRectF r = footerBand(d, m.left, m.top, m.bottom, contentW);
        const LabelPlacement lp = labelPlacement(d.labelLocation);
        const int flags = int(lp.h) | (lp.top ? Qt::AlignTop : Qt::AlignBottom);
        p->setPen(QColor(0, 0, 0, 220));
        p->drawText(r.translated(2, 2), flags, d.footer);
        p->setPen(Qt::white);
        p->drawText(r, flags, d.footer);
    }
}

QRectF ProjectionWindow::layerBounds(const SlideLayerData& d) const
{
    const auto m = m_composer->margins();
    const qreal pad = 40;   // outline + shadow bleed
    const QRectF r(m.left - pad, m.top - pad, m_composer->contentWidth() + 2 * pad, kH - m.top - m.bottom + 2 * pad);
    Q_UNUSED(d)
    return r.intersected(QRectF(0, 0, kW, kH));
}

// Rasterises a text layer once per (content, output size, scale factor). The layer is rendered exactly as it
// would be painted directly (same transform, same pixel sampling: the image origin is rounded to a device
// pixel and the sub-pixel remainder is folded into the render transform), so the result is identical but a
// frame of a crossfade costs one image blend instead of re-laying-out and re-stroking all glyph outlines.
const ProjectionWindow::LayerCache& ProjectionWindow::cacheFor(int layer, const QRectF& box, qreal s, qreal dpr)
{
    const SlideLayerData& d = m_composer->slideLayer(layer);
    LayerCache& c = m_cache[layer];
    const QRectF bounds = layerBounds(d);
    const qreal sd = s * dpr;
    const QPointF boxDev(box.left() * dpr, box.top() * dpr);
    if (!c.img.isNull() && c.serial == d.serial && c.bounds == bounds && qFuzzyCompare(c.sd, sd) &&
        qFuzzyCompare(c.dpr, dpr) && c.boxDev == boxDev)
        return c;

    const QPointF exact = boxDev + bounds.topLeft() * sd;
    const QPoint rounded(qRound(exact.x()), qRound(exact.y()));
    QImage img(QSize(int(std::ceil(bounds.width() * sd)) + 2, int(std::ceil(bounds.height() * sd)) + 2),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter ip(&img);
        ip.setRenderHint(QPainter::Antialiasing);
        ip.setRenderHint(QPainter::TextAntialiasing);
        ip.setRenderHint(QPainter::SmoothPixmapTransform);
        ip.translate(exact - QPointF(rounded));
        ip.scale(sd, sd);
        ip.translate(-bounds.topLeft());
        paintLayer(&ip, layer);
    }
    img.setDevicePixelRatio(dpr);
    c.img = img;
    c.originDev = rounded;
    c.serial = d.serial;
    c.bounds = bounds;
    c.sd = sd;
    c.dpr = dpr;
    c.boxDev = boxDev;
    ++m_cacheRenders;
    return c;
}

QImage ProjectionWindow::presentationImage(const QSize& box)
{
    if (!m_composer->presentationActive() || box.isEmpty())
        return {};
    if (m_presKeySerial != m_composer->presentationSerial() || m_presKeyBox != box || m_presImage.isNull()) {
        m_presImage = PageImageCache::instance().page(m_composer->presentationPdf(), m_composer->presentationPage(), box);
        m_presKeySerial = m_composer->presentationSerial();
        m_presKeyBox = box;
    }
    return m_presImage;
}

void ProjectionWindow::preparePresentation()
{
    const qreal dpr = devicePixelRatioF();
    presentationImage(QSize(qRound(width() * dpr), qRound(height() * dpr)));
}

void ProjectionWindow::prepareLayer(int layer)
{
    if (layer < 0 || layer > 1 || !m_composer->slideLayer(layer).valid || width() <= 0 || height() <= 0)
        return;
    layerFontSize(layer);
    if (!m_cacheEnabled)
        return;
    const QRectF target(rect());
    const qreal s = std::min(target.width() / kW, target.height() / kH);
    const QRectF box(target.left() + (target.width() - kW * s) / 2, target.top() + (target.height() - kH * s) / 2, kW * s, kH * s);
    cacheFor(layer, box, s, devicePixelRatioF());
}
void ProjectionWindow::paintLogo(QPainter* p)
{
    // (the colour field behind the logo is painted by paintScene over the whole output)
    p->save();
    p->translate(kW / 2 - 100, kH / 2 - 170);
    p->scale(2.0, 2.0);
    QPen pen(QColor(0xD4, 0xAF, 0x37), 10);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    QPainterPath cross;
    cross.moveTo(50, 0);
    cross.lineTo(50, 140);
    cross.moveTo(10, 40);
    cross.lineTo(90, 40);
    p->drawPath(cross);
    p->restore();
    QFont f(QStringLiteral("Tahoma"));
    f.setBold(true);
    f.setPixelSize(40);
    p->setFont(f);
    const QRectF r(0, kH / 2 + 150, kW, 60);
    p->setPen(QColor(0, 0, 0, 180));
    p->drawText(r.translated(2, 2), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("LUXPRESENTER"));
    p->setPen(Qt::white);
    p->drawText(r, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("LUXPRESENTER"));
}

// The scene is authored on a 1920x1080 design canvas. The output (whatever the monitor's resolution or aspect
// ratio) is filled as follows: colours, background images, the black overlay and the logo field cover the
// whole target rectangle; the text/logo content is laid out on the design canvas scaled uniformly (no
// stretching) and centred, so it follows the target's size proportionally.
void ProjectionWindow::paintScene(QPainter* p, const QRectF& target, qint64 t)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->save();
    // At fractional scale factors the logical window size is rounded, which can leave the last device pixel
    // row/column outside `target`; fields are painted 1 logical pixel beyond it (clipped by the window).
    const QRectF fill = target.adjusted(-1, -1, 1, 1);
    p->setClipRect(fill);
    const qreal s = std::min(target.width() / kW, target.height() / kH);
    const QRectF box(target.left() + (target.width() - kW * s) / 2, target.top() + (target.height() - kH * s) / 2, kW * s, kH * s);

    // Root background: transparent lets the video window show through.
    if (m_composer->videoShowsThrough()) {
        p->setCompositionMode(QPainter::CompositionMode_Source);
        p->fillRect(fill, Qt::transparent);
        p->setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        p->fillRect(fill, QColor(m_composer->rootBackground()));
    }

    for (int i = 0; i < 2; ++i) {
        const QString path = m_composer->bgImagePath(i);
        const double o = m_composer->bgImageOpacity(i, t);
        if (path.isEmpty() || o <= 0.001)
            continue;
        const QPixmap pm = image(path);
        if (pm.isNull())
            continue;
        p->setOpacity(o);
        // Background image scales to fit the monitor without cropping (Stretch to fill target)
        p->drawPixmap(fill, pm, pm.rect());
    }
    if (m_composer->presentationActive()) {
        // The page is rendered for this output's pixel size and centred without scaling (aspect ratio kept).
        const qreal dpr = p->device()->devicePixelRatioF();
        QImage img = presentationImage(QSize(qRound(target.width() * dpr), qRound(target.height() * dpr)));
        if (!img.isNull()) {
            const QPoint origin(qRound((target.left() + target.width() / 2) * dpr - img.width() / 2.0),
                                qRound((target.top() + target.height() / 2) * dpr - img.height() / 2.0));
            img.setDevicePixelRatio(dpr);
            p->setOpacity(1.0);
            p->drawImage(QPointF(origin.x() / dpr, origin.y() / dpr), img);
        }
    }
    for (int i = 0; i < 2; ++i) {
        const double o = m_composer->slideOpacity(i, t);
        if (o <= 0.001)
            continue;
        p->setOpacity(o);
        if (m_cacheEnabled && m_composer->slideLayer(i).valid) {
            // Blit the pre-rendered layer (pixel-exact with direct painting; see cacheFor).
            const qreal dpr = p->device()->devicePixelRatioF();
            const LayerCache& c = cacheFor(i, box, s, dpr);
            p->drawImage(QPointF(c.originDev.x() / dpr, c.originDev.y() / dpr), c.img);
        } else {
            p->save();
            p->translate(box.topLeft());
            p->scale(s, s);
            paintLayer(p, i);
            p->restore();
        }
    }
    p->setOpacity(1.0);
    const double b = m_composer->blackOpacity(t);
    if (b > 0.001) {
        p->setOpacity(b);
        p->fillRect(fill, Qt::black);
    }
    const double l = m_composer->logoOpacity(t);
    if (l > 0.001) {
        p->setOpacity(l);
        p->fillRect(fill, QColor(0x00, 0x18, 0x33));
        p->save();
        p->translate(box.topLeft());
        p->scale(s, s);
        paintLogo(p);
        p->restore();
    }
    if (AlertService::instance().isAlertVisible()) {
        paintAlert(p, target);
    }
    p->restore();
}

void ProjectionWindow::paintAlert(QPainter* p, const QRectF& target)
{
    if (!AlertService::instance().isAlertVisible())
        return;

    const QString text = AlertService::instance().currentActiveMessage().text;
    if (text.isEmpty())
        return;

    // Height matches 76px on 1080p height
    const qreal bannerH = qMax(32.0, 76.0 * target.height() / 1080.0);
    const QRectF bannerRect(target.left(), target.bottom() - bannerH, target.width(), bannerH);

    p->save();
    p->setOpacity(1.0);
    p->fillRect(bannerRect, QColor(0xFF, 0x00, 0x00));
    p->setClipRect(bannerRect);

    QFont f(QStringLiteral("Tahoma"));
    f.setBold(true);
    f.setPixelSize(qMax(14, qRound(46.0 * target.height() / 1080.0)));
    p->setFont(f);

    QFontMetricsF fm(f);
    const qreal textW = fm.horizontalAdvance(text);
    const qreal totalDist = target.width() + textW;
    const qreal speed = qMax(50.0, 180.0 * (target.width() / 1920.0)); // 180 design-px/sec
    const qreal durationSec = qMax(4.0, totalDist / speed);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_alertPassStartTime <= 0)
        m_alertPassStartTime = now;

    qreal elapsed = (now - m_alertPassStartTime) / 1000.0;
    if (elapsed >= durationSec) {
        m_alertPassStartTime = now;
        AlertService::instance().notifyPassCompleted();
        if (!AlertService::instance().isAlertVisible()) {
            p->restore();
            return;
        }
        elapsed = 0;
    }

    const qreal x = target.right() - (elapsed / durationSec) * totalDist;
    const qreal y = bannerRect.top() + (bannerH - fm.height()) / 2.0 + fm.ascent();

    // Drop shadow
    p->setPen(QColor(0, 0, 0, 240));
    p->drawText(QPointF(x + 2.5, y + 2.5), text);

    // Foreground white text
    p->setPen(Qt::white);
    p->drawText(QPointF(x, y), text);

    p->restore();
}

void ProjectionWindow::paintEvent(QPaintEvent*)
{
    // The whole window is the output: its pixel size follows the monitor it is on.
    QPainter p(this);
    paintScene(&p, QRectF(rect()), m_composer->now());
}
void ProjectionWindow::mousePressEvent(QMouseEvent* e)
{
    emit clicked();
    e->accept();
}

} // namespace Lux
