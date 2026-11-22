#include "Views/Components/SlideCanvas.h"

#include "Services/AlertService.h"
#include "Services/BackgroundThumbnails.h"
#include "Services/BackgroundUtil.h"
#include "Services/LyricsFormatter.h"
#include "Services/TextRepair.h"
#include "Views/Components/TextRender.h"
#include "Views/UiUtil.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QPainter>
#include <QPixmap>

#include <algorithm>

namespace Lux {

namespace {
constexpr qreal kW = 640;
constexpr qreal kH = 360;
constexpr qreal kFull = 3.0; // 1920 / 640

void drawShadowedLabel(QPainter* p, const QRectF& r, int flags, const QString& text, qreal px = 16)
{
    p->setFont(tahoma(1, true));
    QFont lf = p->font();
    lf.setPixelSize(std::max(1, int(std::lround(px))));
    p->setFont(lf);
    p->setPen(QColor(0, 0, 0, 242));
    p->drawText(r.translated(1.4, 1.4), flags, text);
    p->setPen(Qt::white);
    p->drawText(r, flags, text);
}
} // namespace

SlideCanvas::SlideCanvas(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(&BackgroundThumbnails::instance(), &BackgroundThumbnails::ready, this, [this](const QString& path) {
        if (path == m_background)
            update();
    });

    connect(&m_alertTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    connect(&AlertService::instance(), &AlertService::alertStateChanged, this, [this] {
        if (m_isLive && AlertService::instance().isAlertVisible()) {
            if (!m_alertTimer.isActive()) {
                m_alertStartTime = QDateTime::currentMSecsSinceEpoch();
                m_alertTimer.start(16);
            }
        } else {
            m_alertTimer.stop();
            m_alertStartTime = 0;
        }
        update();
    });
}

void SlideCanvas::setLive(bool live)
{
    m_isLive = live;
    if (m_isLive && AlertService::instance().isAlertVisible()) {
        if (!m_alertTimer.isActive()) {
            m_alertStartTime = QDateTime::currentMSecsSinceEpoch();
            m_alertTimer.start(16);
        }
    } else {
        m_alertTimer.stop();
        m_alertStartTime = 0;
    }
    update();
}

void SlideCanvas::setSlide(const Slide& slide)
{
    m_slide = slide;
    m_hasSlide = true;
    update();
}

void SlideCanvas::clearSlide()
{
    m_hasSlide = false;
    update();
}

void SlideCanvas::setBackground(const QString& hexOrImagePath)
{
    m_background = hexOrImagePath;
    update();
}

void SlideCanvas::setSettings(const AppSettings& settings)
{
    m_settings = settings;
    update();
}

QString SlideCanvas::formattedContent() const
{
    return LyricsFormatter::format(displayText(m_slide.content), &m_settings);
}

QFont SlideCanvas::slideFont() const
{
    const bool ownFont = m_slide.type == SlideType::Scripture && !m_slide.fontFamily.trimmed().isEmpty();
    const QString family = ownFont ? m_slide.fontFamily
                                   : (m_settings.songFontFamily.isEmpty() ? (m_slide.fontFamily.isEmpty() ? QStringLiteral("Arial") : m_slide.fontFamily)
                                                                          : m_settings.songFontFamily);
    QFont f(family);
    f.setBold(m_settings.songFontBold || m_slide.isBold);
    f.setItalic(m_slide.isItalic);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

qreal SlideCanvas::fullScaleFontSize() const
{
    if (!m_hasSlide)
        return 0;
    const int basePt = m_settings.songFontSize > 0 ? m_settings.songFontSize : (m_slide.fontSize > 0 ? m_slide.fontSize : 13);
    const qreal maxTarget = basePt > 0 ? basePt * 6.5 : 84.5;
    const QString text = formattedContent();
    const QFont f = slideFont();
    const QString key = text + QChar(0x1F) + f.family() + (f.bold() ? "b" : "-") + (f.italic() ? "i" : "-") + QString::number(basePt) + (m_settings.autoWrapWords ? "w" : "n");
    if (key != m_fsKey) {
        m_fsValue = optimalFontSize(text, f, 1700, 840, maxTarget, 34.0, m_settings.autoWrapWords);
        m_fsKey = key;
    }
    return m_fsValue;
}

void SlideCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Letterbox: fit 16:9 inside the widget (Viewbox Uniform).
    const qreal scale = std::min(width() / kW, height() / kH);
    const QRectF target((width() - kW * scale) / 2, (height() - kH * scale) / 2, kW * scale, kH * scale);
    p.fillRect(rect(), QColor(0x2B, 0x2B, 0x2B));
    p.save();
    p.translate(target.topLeft());
    p.scale(scale, scale);
    p.setClipRect(QRectF(0, 0, kW, kH));
    p.fillRect(QRectF(0, 0, kW, kH), Qt::black);

    // The pane shows the active/global background (slide-level backgrounds are applied by the
    // owner through setBackground, as MainViewModel does).
    const QString bg = m_background;
    if (bg.startsWith(QLatin1Char('#')) && QColor::isValidColorName(bg)) {
        p.fillRect(QRectF(0, 0, kW, kH), QColor(bg));
    } else if (!bg.isEmpty()) {
        // Image file, or a frame of a video background (the projector plays the video itself).
        p.fillRect(QRectF(0, 0, kW, kH), QColor(Backgrounds::kDefaultColor));
        const bool video = Backgrounds::isVideoPath(bg);
        const QImage img = BackgroundThumbnails::instance().image(bg, qMax(640, int(width() * devicePixelRatioF())));
        if (!img.isNull()) {
            // Scale to fit canvas without cropping
            p.drawImage(QRectF(0, 0, kW, kH), img, img.rect());
        }
        if (video) {
            p.setFont(tahoma(11, true));
            p.setPen(QColor(255, 255, 255, 200));
            p.drawText(QRectF(6, 4, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("\u25B6 VIDEO"));
        }
    }
    p.fillRect(QRectF(0, 0, kW, kH), QColor(0, 0, 0, 0x1A)); // contrast tint

    if (m_hasSlide && !m_cleared) {
        const QRectF area(36, 8, kW - 72, kH - 32); // margins 36,8,36,24
        const bool showTitle = m_slide.type == SlideType::Scripture && !m_slide.title.isEmpty();
        const bool showLabel = m_slide.type != SlideType::Scripture && m_settings.showVerseLabels && !m_slide.label.isEmpty();
        const LabelPlacement lp = labelPlacement(m_settings.labelLocation);
        const qreal labelPx = labelPixelSize(m_settings.labelSize) / kFull;
        const qreal labelH = labelBandHeight(labelPx);
        const qreal footerH = showLabel && !lp.top ? labelH : 0;
        const qreal headerLabelH = showLabel && lp.top ? labelH : 0;
        const qreal titleH = showTitle ? 20 : headerLabelH;

        if (showTitle)
            drawShadowedLabel(&p, QRectF(area.left(), area.top(), area.width(), titleH),
                              Qt::AlignHCenter | Qt::AlignVCenter, displayText(m_slide.title));

        const QString text = formattedContent();
        if (!text.isEmpty()) {
            const qreal full = fullScaleFontSize();
            const QString align = m_settings.songTextAlignment;
            const QString v = m_settings.songVerticalAlignment;
            const QString cacheKey = text + QChar(0x1F) + QString::number(full, 'f', 1) + QChar(0x1F) +
                                     align + QChar(0x1F) + v + QChar(0x1F) +
                                     (m_slide.hasOutline ? 'o' : '-') + (m_slide.hasShadow ? 's' : '-') +
                                     (m_settings.autoWrapWords ? 'w' : '-') +
                                     QString::number(titleH, 'f', 1) + QChar(0x1F) + QString::number(footerH, 'f', 1);

            if (cacheKey != m_cachedTextKey || m_cachedTextPixmap.isNull()) {
                QPixmap pm(qRound(kW), qRound(kH));
                pm.fill(Qt::transparent);
                QPainter tp(&pm);
                tp.setRenderHint(QPainter::Antialiasing);
                tp.setRenderHint(QPainter::TextAntialiasing);

                QFont f = slideFont();
                f.setPixelSize(std::max(1, static_cast<int>(std::lround(full))));

                TextBlockStyle st;
                st.font = f;
                st.width = 568 * kFull;
                st.strokeThickness = 3.0 * kFull;
                st.outline = m_slide.hasOutline;
                st.shadow = m_slide.hasShadow;
                st.shadowDepth = 3.5 * kFull;
                st.wrap = m_settings.autoWrapWords;
                st.hAlign = align.compare(QStringLiteral("Left"), Qt::CaseInsensitive) == 0   ? Qt::AlignLeft
                            : align.compare(QStringLiteral("Right"), Qt::CaseInsensitive) == 0 ? Qt::AlignRight
                                                                                               : Qt::AlignHCenter;
                TextBlockMetrics m = measureTextBlock(text, st);
                if (!st.wrap && m.width + 2 * st.strokeThickness > st.width) {
                    st.width = m.width + 2 * st.strokeThickness;
                    m = measureTextBlock(text, st);
                }
                const qreal blockW = st.width / kFull, blockH = (m.height + 2 * st.strokeThickness) / kFull;

                // Viewbox (Uniform, DownOnly): shrink if it does not fit the row.
                const QRectF row(area.left(), area.top() + titleH, area.width(), area.height() - titleH - footerH);
                const qreal s = std::min({1.0, row.height() / blockH, row.width() / blockW});
                const qreal drawW = blockW * s, drawH = blockH * s;
                qreal y = row.top();
                if (v.compare(QStringLiteral("Bottom"), Qt::CaseInsensitive) == 0)
                    y = row.bottom() - drawH;
                else if (v.compare(QStringLiteral("Top"), Qt::CaseInsensitive) != 0)
                    y = row.top() + (row.height() - drawH) / 2;
                const qreal x = row.left() + (row.width() - drawW) / 2;

                tp.save();
                tp.translate(x, y);
                tp.scale(s / kFull, s / kFull);
                drawTextBlock(&tp, text, st, QPointF(0, 0));
                tp.restore();
                tp.end();

                m_cachedTextPixmap = pm;
                m_cachedTextKey = cacheKey;
            }

            p.drawPixmap(0, 0, m_cachedTextPixmap);
        }

        if (showLabel) {
            const qreal indent = (m_settings.indentLabel && lp.h != Qt::AlignHCenter) ? 60 / kFull : 0;
            const QRectF lr(area.left() + (lp.h == Qt::AlignLeft ? indent : 0), lp.top ? area.top() : area.bottom() - labelH,
                            area.width() - indent, labelH);
            drawShadowedLabel(&p, lr, int(lp.h) | (lp.top ? Qt::AlignTop : Qt::AlignBottom), displayText(m_slide.label), labelPx);
        }
    }
    if (m_isLive && AlertService::instance().isAlertVisible()) {
        const QString alertText = AlertService::instance().currentActiveMessage().text;
        if (!alertText.isEmpty()) {
            const qreal bannerH = 25.3;
            const QRectF bannerRect(0, kH - bannerH, kW, bannerH);
            p.save();
            p.setOpacity(1.0);
            p.fillRect(bannerRect, QColor(0xFF, 0x00, 0x00));
            p.setClipRect(bannerRect);

            QFont f = tahoma(10, true);
            f.setPixelSize(15);
            p.setFont(f);
            QFontMetricsF fm(f);
            const qreal textW = fm.horizontalAdvance(alertText);
            const qreal totalDist = kW + textW;
            const qreal speed = 100.0;
            const qreal durationSec = qMax(3.0, totalDist / speed);

            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_alertStartTime <= 0)
                m_alertStartTime = now;

            qreal elapsed = (now - m_alertStartTime) / 1000.0;
            if (elapsed >= durationSec) {
                m_alertStartTime = now;
                elapsed = 0;
            }

            const qreal x = kW - (elapsed / durationSec) * totalDist;
            const qreal y = bannerRect.top() + (bannerH - fm.height()) / 2.0 + fm.ascent();

            p.setPen(QColor(0, 0, 0, 220));
            p.drawText(QPointF(x + 1.5, y + 1.5), alertText);

            p.setPen(Qt::white);
            p.drawText(QPointF(x, y), alertText);
            p.restore();
        }
    }
    if (m_black)
        p.fillRect(QRectF(0, 0, kW, kH), Qt::black);
    p.restore();
}

} // namespace Lux
