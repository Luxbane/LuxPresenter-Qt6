#include "Views/Components/TextRender.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainterPath>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace Lux {

namespace {

struct Line
{
    QString text;
    qreal width = 0;
    qreal ascent = 0;
    qreal height = 0;
};

QList<Line> layoutLines(const QString& text, const QFont& font, qreal maxWidth, bool wrap)
{
    QList<Line> out;
    const QFontMetricsF fm(font);
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString& para : paragraphs) {
        if (para.trimmed().isEmpty()) {
            out.append({QString(), 0, fm.ascent(), fm.height()});
            continue;
        }
        QTextLayout layout(para, font);
        QTextOption opt;
        opt.setWrapMode(wrap ? QTextOption::WordWrap : QTextOption::NoWrap);
        layout.setTextOption(opt);
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(wrap ? maxWidth : 1.0e6);
            out.append({para.mid(line.textStart(), line.textLength()).trimmed(), line.naturalTextWidth(),
                        line.ascent(), line.height()});
        }
        layout.endLayout();
    }
    return out;
}

// Thread-local or static LRU cache for vector glyph paths to avoid re-generating
// hundreds of Bezier curves on every paint frame.
QString pathCacheKey(const QString& text, const TextBlockStyle& style, qreal w)
{
    return QString::number(style.font.pixelSize()) + QLatin1Char('|') +
           style.font.family() + (style.font.bold() ? QLatin1String("|b") : QLatin1String("|-")) +
           QLatin1Char('|') + QString::number(static_cast<int>(style.hAlign)) +
           QLatin1Char('|') + QString::number(w, 'f', 1) +
           QLatin1Char('|') + (style.wrap ? QLatin1Char('1') : QLatin1Char('0')) +
           QLatin1Char('|') + text;
}

QPainterPath getOrCreateTextPath(const QString& text, const TextBlockStyle& style, qreal w)
{
    static QHash<QString, QPainterPath> s_pathCache;
    const QString key = pathCacheKey(text, style, w);
    auto it = s_pathCache.constFind(key);
    if (it != s_pathCache.constEnd())
        return it.value();

    const QList<Line> lines = layoutLines(text, style.font, w, style.wrap);
    QPainterPath all;
    qreal y = style.strokeThickness;
    for (const Line& l : lines) {
        if (!l.text.isEmpty()) {
            qreal x = style.strokeThickness;
            if (style.hAlign & Qt::AlignHCenter)
                x = (style.width - l.width) / 2;
            else if (style.hAlign & Qt::AlignRight)
                x = style.width - style.strokeThickness - l.width;
            all.addText(QPointF(x, y + l.ascent), style.font, l.text);
        }
        y += l.height;
    }

    if (s_pathCache.size() > 128)
        s_pathCache.clear();
    s_pathCache.insert(key, all);
    return all;
}

} // namespace

TextBlockMetrics measureTextBlock(const QString& text, const TextBlockStyle& style)
{
    TextBlockMetrics m;
    const qreal w = std::max<qreal>(50, style.width - style.strokeThickness * 2);
    for (const Line& l : layoutLines(text, style.font, w, style.wrap)) {
        m.width = std::max(m.width, l.width);
        m.height += l.height;
    }
    return m;
}

void drawTextBlock(QPainter* p, const QString& text, const TextBlockStyle& style, const QPointF& origin)
{
    const qreal w = std::max<qreal>(50, style.width - style.strokeThickness * 2);
    QPainterPath all = getOrCreateTextPath(text, style, w);
    all.translate(origin);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    if (style.shadow) {
        // Soft drop shadow toward the lower right
        const qreal d = style.shadowDepth * 0.70710678;
        const QPainterPath shifted = all.translated(d, d);
        // 2 clean passes with soft alpha gives identical drop shadow with 1/2 the stroke operations
        for (int i = 0; i < 2; ++i) {
            const qreal grow = i * 1.8 * (style.shadowDepth / 3.5);
            p->setPen(QPen(QColor(0, 0, 0, 95), style.strokeThickness + grow * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(QColor(0, 0, 0, 95));
            p->drawPath(shifted);
        }
    }
    if (style.outline && style.strokeThickness > 0) {
        p->setPen(QPen(Qt::black, style.strokeThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->drawPath(all);
    }
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    p->drawPath(all);
    p->restore();
}

qreal optimalFontSize(const QString& text, QFont font, qreal maxW, qreal maxH, qreal targetMaxFs, qreal minFs, bool wrap)
{
    if (text.trimmed().isEmpty())
        return targetMaxFs;
    qreal low = minFs, high = targetMaxFs, best = minFs;
    for (int i = 0; i < 7; ++i) {
        const qreal mid = (low + high) / 2.0;
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(mid))));
        TextBlockStyle st;
        st.font = font;
        st.width = std::max<qreal>(100, maxW);
        st.strokeThickness = 0;
        st.wrap = wrap;
        const TextBlockMetrics m = measureTextBlock(text, st);
        if (m.height <= maxH && m.width <= maxW) {
            best = mid;
            low = mid + 0.5;
        } else {
            high = mid - 0.5;
        }
    }
    return std::round(best * 10.0) / 10.0;
}

LabelPlacement labelPlacement(const QString& location)
{
    LabelPlacement p;
    p.top = location.startsWith(QStringLiteral("Top"), Qt::CaseInsensitive);
    p.h = location.endsWith(QStringLiteral("Left"), Qt::CaseInsensitive)    ? Qt::AlignLeft
          : location.endsWith(QStringLiteral("Right"), Qt::CaseInsensitive) ? Qt::AlignRight
                                                                             : Qt::AlignHCenter;
    return p;
}
qreal labelPixelSize(const QString& size)
{
    if (size.compare(QStringLiteral("Small"), Qt::CaseInsensitive) == 0)
        return 28;
    if (size.compare(QStringLiteral("Large"), Qt::CaseInsensitive) == 0)
        return 48;
    return 36;
}
} // namespace Lux

