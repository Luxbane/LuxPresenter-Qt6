#include "Views/Components/SlidePaneView.h"

#include "Services/BackgroundThumbnails.h"
#include "Services/TextRepair.h"
#include "Services/ThemeService.h"
#include "Views/Components/PageView.h"
#include "Views/Components/ProportionalSplitter.h"
#include "Views/Components/SlideCanvas.h"
#include "Views/UiUtil.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace Lux {

namespace {

QString formatTime(double seconds)
{
    int s = std::max(0, static_cast<int>(seconds));
    int m = s / 60;
    s = s % 60;
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

enum Role { LabelRole = Qt::UserRole, ContentRole, TypeRole, GapRole };

constexpr int kBlankLineHeight = 2;   // tight spacing between slides ("mepet")

// Blank lines before a stanza, shown as empty (unselectable) space above its row.
int gapPixels(const QModelIndex& idx) { return qMax(0, idx.data(GapRole).toInt()) * kBlankLineHeight; }

QColor bannerColor(SlideType t)
{
    switch (t) {
    case SlideType::Chorus: return QColor(0x80, 0x18, 0x48);
    case SlideType::Bridge: return QColor(0x15, 0x80, 0x3D);
    default: return QColor(0x1E, 0x60, 0xD2);
    }
}

// Stanza row: coloured label banner (Verse blue / Chorus maroon / Bridge green) and the
// stanza text, with full-row selection highlight and hover highlight.
class StanzaDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const int w = opt.rect.width() > 0 ? opt.rect.width() : 300;
        int h = gapPixels(idx);
        if (!idx.data(LabelRole).toString().isEmpty())
            h += QFontMetrics(tahoma(11, true)).height() + 4 + 4; // banner height + spacing
        const QFontMetrics fm(tahoma(11));
        const QString text = Lux::displayText(idx.data(ContentRole).toString());
        const QRect br = fm.boundingRect(QRect(0, 0, w - 16, 10000), Qt::TextWordWrap, text);
        const int lines = qMax(1, br.height() / qMax(1, fm.lineSpacing()));
        h += lines * 22 + 4; // LineHeight 22, vertical padding gives ~26px comfortable row
        return {w, h};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        const Theme& t = ThemeService::instance().theme();
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = (opt.state & QStyle::State_MouseOver);
        const bool active = opt.widget && opt.widget->property("paneActive").toBool();
        p->save();

        const int gap = gapPixels(idx);
        int y = opt.rect.top() + gap;

        // 1. Draw Stanza Banner if present (e.g. "Verse 2", "Chorus", etc.)
        const QString label = Lux::displayText(idx.data(LabelRole).toString());
        if (!label.isEmpty()) {
            const QFont f = tahoma(11, true);
            const int bh = QFontMetrics(f).height() + 4;
            const QRect banner(opt.rect.left(), y + 1, opt.rect.width(), bh);
            p->fillRect(banner, bannerColor(static_cast<SlideType>(idx.data(TypeRole).toInt())));
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(banner.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
            y += bh + 3;
        }

        // 2. Lyric selection / hover box rectangle (leaves only 2px gap between adjacent items)
        const QRect lyricBox(opt.rect.left(), y + 1, opt.rect.width(), opt.rect.bottom() - y - 1);

        QColor textColor = t.listItemText;
        if (selected) {
            p->fillRect(lyricBox, active ? Theme::selectionBlue() : QColor(0x1E, 0x60, 0xD2));
            textColor = Qt::white;
        } else if (hovered) {
            // Gray selected hover box matching Image 2
            p->fillRect(lyricBox, QColor(255, 255, 255, 22)); // subtle translucent gray fill
            p->setPen(QColor(255, 255, 255, 55));            // subtle 1px border
            p->drawRect(lyricBox.adjusted(0, 0, -1, -1));
        }

        // 3. Draw Lyric Text vertically centered in the lyric box
        p->setFont(tahoma(11));
        p->setPen(textColor);
        const QFontMetrics fm(p->font());
        const QString text = Lux::displayText(idx.data(ContentRole).toString());
        const QRect textArea = lyricBox.adjusted(8, 2, -8, -2);

        int ly = textArea.top();
        for (const QString& para : text.split(QLatin1Char('\n'))) {
            QStringList wrapped;
            QString cur;
            for (const QString& word : para.split(QLatin1Char(' '))) {
                const QString trial = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
                if (fm.horizontalAdvance(trial) > textArea.width() && !cur.isEmpty()) {
                    wrapped << cur;
                    cur = word;
                } else {
                    cur = trial;
                }
            }
            wrapped << cur;
            for (const QString& l : std::as_const(wrapped)) {
                p->drawText(QRect(textArea.left(), ly, textArea.width(), 20), Qt::AlignVCenter | Qt::AlignLeft, l);
                ly += 20;
            }
        }
        p->restore();
    }
};

// The blank lines between stanzas are spacing only: presses/double-clicks on them do nothing (no selection,
// no Go Live), so only real lyric rows can be selected.
class GapGuard : public QObject
{
public:
    explicit GapGuard(QListWidget* list) : QObject(list), m_list(list) {}
    bool eventFilter(QObject*, QEvent* e) override
    {
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove: {
            const QPoint pos = static_cast<QMouseEvent*>(e)->position().toPoint();
            const QModelIndex idx = m_list->indexAt(pos);
            return idx.isValid() && pos.y() < m_list->visualRect(idx).top() + gapPixels(idx);
        }
        default: return false;
        }
    }

private:
    QListWidget* m_list;
};

QPushButton* transportButton(const QString& text, const QString& style, const QString& tip, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setStyleSheet(QStringLiteral("QPushButton{%1} QPushButton:disabled{color:#94A3B8;}").arg(style));
    b->setToolTip(tip);
    b->setEnabled(false);
    return b;
}

class ClickJumpSlider : public QSlider
{
public:
    using QSlider::QSlider;

    std::function<void(double frac)> onScrub;
    std::function<void(double frac)> onSeek;
    bool isDragging() const { return m_dragging; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            jumpTo(event);
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QSlider::mouseMoveEvent(event);
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            jumpTo(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QSlider::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            const double frac = std::clamp(static_cast<double>(value() - minimum()) / std::max(1, maximum() - minimum()), 0.0, 1.0);
            if (onSeek)
                onSeek(frac);
        }
    }

private:
    void jumpTo(QMouseEvent* event)
    {
        const int pos = orientation() == Qt::Horizontal ? event->position().toPoint().x() : event->position().toPoint().y();
        const int len = orientation() == Qt::Horizontal ? width() : height();
        if (len > 0) {
            const double frac = orientation() == Qt::Horizontal
                ? std::clamp(static_cast<double>(pos) / len, 0.0, 1.0)
                : std::clamp(1.0 - static_cast<double>(pos) / len, 0.0, 1.0);
            const int val = minimum() + std::lround(frac * (maximum() - minimum()));
            setValue(val);
            if (onScrub)
                onScrub(frac);
        }
    }

    bool m_dragging = false;
};

} // namespace

SlidePaneView::SlidePaneView(const QString& paneTitle, bool isLive, QWidget* parent)
    : QWidget(parent), m_paneTitle(paneTitle), m_isLive(isLive)
{
    setObjectName(QStringLiteral("MainRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(260);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("PaneHeader"));
    header->setFixedHeight(26);
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(8, 0, 8, 0);
    m_header = new QLabel(header);
    m_header->setFont(tahoma(11));
    m_header->setTextFormat(Qt::RichText);
    auto* options = new QLabel(QStringLiteral("\u2699 \u25BE"), header);
    options->setFont(segoeSymbol(10));
    options->setToolTip(QStringLiteral("Pane Options"));
    options->setStyleSheet(QStringLiteral("color:palette(mid);"));
    hl->addWidget(m_header, 1);
    hl->addWidget(options);
    root->addWidget(header);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildSlidePage());
    m_pages->addWidget(buildMediaPage());
    m_pages->addWidget(buildDocPage());
    root->addWidget(m_pages, 1);

    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        if (!m_isLive && m_previewPlayer.isValid()) {
            if (!m_isDraggingSlider) {
                const double pos = m_previewPlayer.position();
                const double dur = m_previewPlayer.duration();
                if (dur > 0) {
                    const double frac = std::clamp(pos / dur, 0.0, 1.0);
                    if (m_mediaSlider) {
                        QSignalBlocker b(m_mediaSlider);
                        m_mediaSlider->setValue(static_cast<int>(frac * 1000.0));
                    }
                    if (m_timeLabel) {
                        m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(pos), formatTime(dur)));
                    }
                }
            }
            if (m_previewPlayer.atEnd() && !m_isLooping) {
                setPlaying(false);
                if (m_mediaSlider) {
                    QSignalBlocker b(m_mediaSlider);
                    m_mediaSlider->setValue(0);
                }
            }
        }
    });

    updateHeader();
    updateDocStatus();
}

SlidePaneView::~SlidePaneView()
{
    stopPlayback();
}

QWidget* SlidePaneView::buildSlidePage()
{
    // Stanza list 52% / rendered slide 48% by default, adjustable by dragging the handle.
    auto* split = new ProportionalSplitter(Qt::Vertical, this);
    split->setDefaultRatios({52, 48});
    m_splitter = split;
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(5);
    m_splitter->setStyleSheet(QStringLiteral("QSplitter::handle{background:#CBD5E1;}"));

    m_stanzas = new QListWidget(m_splitter);
    m_stanzas->setObjectName(QStringLiteral("StanzaList"));
    m_stanzas->setFrameShape(QFrame::NoFrame);
    m_stanzas->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stanzas->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_stanzas->setMouseTracking(true);
    m_stanzas->viewport()->setAttribute(Qt::WA_Hover, true);
    m_stanzas->viewport()->setMouseTracking(true);
    m_stanzas->setItemDelegate(new StanzaDelegate(m_stanzas));
    m_stanzas->setMinimumHeight(140);
    new ActiveSelectionTracker(m_stanzas);
    m_stanzas->viewport()->installEventFilter(new GapGuard(m_stanzas));
    connect(m_stanzas, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_slides.size())
            m_canvas->setSlide(m_slides.at(row));
        else
            m_canvas->clearSlide();
        emit currentSlideChanged(row);
    });
    connect(m_stanzas, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { emit slideDoubleClicked(m_stanzas->row(it)); });

    auto* viewport = new QWidget(m_splitter);
    viewport->setAttribute(Qt::WA_StyledBackground, true);
    viewport->setObjectName(QStringLiteral("SlideViewport"));
    viewport->setStyleSheet(QStringLiteral("QWidget#SlideViewport{background:#2B2B2B;}"));
    viewport->setMinimumHeight(150);
    auto* vl = new QVBoxLayout(viewport);
    vl->setContentsMargins(12, 8, 12, 8);
    m_canvas = new SlideCanvas(viewport);
    m_canvas->setLive(m_isLive);
    vl->addWidget(m_canvas);

    m_splitter->addWidget(m_stanzas);
    m_splitter->addWidget(viewport);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    return m_splitter;
}

QWidget* SlidePaneView::buildMediaPage()
{
    auto* page = new QFrame(this);
    page->setFrameShape(QFrame::NoFrame);
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setAutoFillBackground(true);
    QPalette p = page->palette();
    p.setColor(QPalette::Window, QColor(0x33, 0x33, 0x33));
    page->setPalette(p);
    page->setObjectName(QStringLiteral("MediaPage"));
    page->setStyleSheet(QStringLiteral("QFrame#MediaPage{background:#333333;}"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(16, 16, 16, 12);
    v->setSpacing(0);

    auto* screen = new QFrame(page);
    screen->setObjectName(QStringLiteral("VideoScreen"));
    screen->setAutoFillBackground(true);
    QPalette sp = screen->palette();
    sp.setColor(QPalette::Window, Qt::black);
    screen->setPalette(sp);
    screen->setStyleSheet(QStringLiteral("QFrame#VideoScreen{background:#000000; border:2px solid #222222; border-radius:3px;}"));
    auto* sl = new QStackedLayout(screen);
    sl->setStackingMode(QStackedLayout::StackOne);

    m_previewPoster = new QLabel(screen);
    m_previewPoster->setAlignment(Qt::AlignCenter);
    m_previewPoster->setStyleSheet(QStringLiteral("background:#000000; border:none;"));
    m_previewPoster->setScaledContents(false);
    sl->addWidget(m_previewPoster);

    if (!m_isLive) {
        m_videoHost = new QWidget(screen);
        m_videoHost->setAttribute(Qt::WA_NativeWindow);
        m_videoHost->setAttribute(Qt::WA_NoSystemBackground);
        m_videoHost->setAttribute(Qt::WA_OpaquePaintEvent);
        m_videoHost->hide();
        sl->addWidget(m_videoHost);
    }

    m_videoNote = new QLabel(QStringLiteral("(video playing on live output)"), screen);
    m_videoNote->setAlignment(Qt::AlignCenter);
    m_videoNote->setFont(tahoma(14));
    m_videoNote->setStyleSheet(QStringLiteral("color:#888888; font-style:italic; border:none; background:#000000;"));
    sl->addWidget(m_videoNote);

    if (m_isLive) {
        sl->setCurrentWidget(m_videoNote);
        m_videoNote->show();
        m_previewPoster->hide();
    } else {
        sl->setCurrentWidget(m_previewPoster);
        m_previewPoster->show();
        m_videoNote->hide();
    }

    v->addWidget(screen, 1);
    v->addSpacing(8);

    auto* scrub = new QHBoxLayout;
    scrub->setContentsMargins(0, 4, 0, 8);
    auto* cslider = new ClickJumpSlider(Qt::Horizontal, page);
    cslider->setRange(0, 1000);
    cslider->setValue(0);
    cslider->setEnabled(false);
    cslider->setCursor(Qt::PointingHandCursor);
    m_mediaSlider = cslider;

    m_timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), page);
    m_timeLabel->setFont(tahoma(11));
    m_timeLabel->setStyleSheet(QStringLiteral("color:#E2E8F0; background:transparent;"));
    scrub->addWidget(m_mediaSlider, 1);
    scrub->addSpacing(12);
    scrub->addWidget(m_timeLabel);
    v->addLayout(scrub);

    auto* buttons = new QHBoxLayout;

    m_loopBtn = new QPushButton(QStringLiteral("\u21BB"), page);
    m_loopBtn->setFixedSize(32, 30);
    m_loopBtn->setCursor(Qt::PointingHandCursor);
    m_loopBtn->setEnabled(false);
    updateLoopButtonUi();

    m_rewindBtn = new QPushButton(QStringLiteral("\u25C0\u25C0"), page);
    m_rewindBtn->setFixedSize(36, 28);
    m_rewindBtn->setCursor(Qt::PointingHandCursor);
    m_rewindBtn->setToolTip(QStringLiteral("Rewind"));
    m_rewindBtn->setEnabled(false);
    m_rewindBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; background:#CBD5E1; border:1px solid #94A3B8; border-radius:3px; color:#1E293B; }"
        "QPushButton:hover { background:#E2E8F0; border-color:#64748B; }"
        "QPushButton:pressed { background:#94A3B8; }"
        "QPushButton:disabled { background:#334155; border-color:#475569; color:#64748B; }"
    ));

    m_playBtn = new QPushButton(QStringLiteral("\u25B6"), page);
    m_playBtn->setFixedSize(44, 44);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setToolTip(QStringLiteral("Play / Pause"));
    m_playBtn->setEnabled(false);
    m_playBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:18px; font-weight:bold; background:#E2E8F0; border:2px solid #64748B; border-radius:4px; color:#0284C7; }"
        "QPushButton:hover { background:#FFFFFF; border-color:#38BDF8; color:#0284C7; }"
        "QPushButton:pressed { background:#CBD5E1; }"
        "QPushButton:disabled { background:#334155; border-color:#475569; color:#64748B; }"
    ));

    m_forwardBtn = new QPushButton(QStringLiteral("\u25B6\u25B6"), page);
    m_forwardBtn->setFixedSize(36, 28);
    m_forwardBtn->setCursor(Qt::PointingHandCursor);
    m_forwardBtn->setToolTip(QStringLiteral("Forward"));
    m_forwardBtn->setEnabled(false);
    m_forwardBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:10px; background:#CBD5E1; border:1px solid #94A3B8; border-radius:3px; color:#1E293B; }"
        "QPushButton:hover { background:#E2E8F0; border-color:#64748B; }"
        "QPushButton:pressed { background:#94A3B8; }"
        "QPushButton:disabled { background:#334155; border-color:#475569; color:#64748B; }"
    ));

    m_transport = {m_loopBtn, m_rewindBtn, m_playBtn, m_forwardBtn};

    auto* speaker = new QLabel(QStringLiteral("\U0001F50A"), page);
    speaker->setFont(segoeEmoji(14));
    speaker->setStyleSheet(QStringLiteral("color:#94A3B8; background:transparent;"));

    m_volumeSlider = new QSlider(Qt::Horizontal, page);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(70);
    m_volumeSlider->setEnabled(false);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);

    buttons->addWidget(m_loopBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_rewindBtn);
    buttons->addSpacing(4);
    buttons->addWidget(m_playBtn);
    buttons->addSpacing(4);
    buttons->addWidget(m_forwardBtn);
    buttons->addStretch(1);
    buttons->addWidget(speaker);
    buttons->addWidget(m_volumeSlider);
    v->addLayout(buttons);

    connect(m_loopBtn, &QPushButton::clicked, this, [this] {
        setLooping(!m_isLooping);
        if (!m_isLive && m_previewPlayer.isValid())
            m_previewPlayer.setLoop(m_isLooping);
        emit mediaLoopToggled(m_isLooping);
    });

    connect(m_playBtn, &QPushButton::clicked, this, [this] {
        setPlaying(!m_isPlaying);
        if (!m_isLive) {
            if (m_previewPlayer.isValid())
                m_previewPlayer.setPaused(!m_isPlaying);
            if (m_isPlaying && !m_pollTimer.isActive())
                m_pollTimer.start(100);
        } else {
            emit mediaPlayPauseRequested(m_isPlaying);
        }
    });

    connect(m_rewindBtn, &QPushButton::clicked, this, [this] {
        if (!m_isLive) {
            if (m_previewPlayer.isValid())
                m_previewPlayer.seek(0);
        } else {
            emit mediaSeekRequested(0.0);
        }
        m_mediaSlider->setValue(0);
    });

    connect(m_forwardBtn, &QPushButton::clicked, this, [this] {
        if (!m_isLive) {
            if (m_previewPlayer.isValid())
                m_previewPlayer.seek(m_previewPlayer.duration());
        } else {
            emit mediaSeekRequested(1.0);
        }
        m_mediaSlider->setValue(1000);
    });

    cslider->onScrub = [this](double frac) {
        m_isDraggingSlider = true;
        const double dur = !m_isLive ? (m_previewPlayer.isValid() ? m_previewPlayer.duration() : 0.0) : m_lastDuration;
        if (dur > 0 && m_timeLabel)
            m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(frac * dur), formatTime(dur)));
    };

    cslider->onSeek = [this](double frac) {
        m_isDraggingSlider = false;
        if (!m_isLive) {
            if (m_previewPlayer.isValid()) {
                const double dur = m_previewPlayer.duration();
                m_previewPlayer.seek(frac * dur);
            }
        } else {
            emit mediaSeekRequested(frac);
        }
    };

    connect(m_mediaSlider, &QSlider::sliderPressed, this, [this] {
        m_isDraggingSlider = true;
    });

    connect(m_mediaSlider, &QSlider::valueChanged, this, [this](int val) {
        if (m_isDraggingSlider) {
            const double frac = val / 1000.0;
            const double dur = !m_isLive ? (m_previewPlayer.isValid() ? m_previewPlayer.duration() : 0.0) : m_lastDuration;
            if (dur > 0 && m_timeLabel)
                m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(frac * dur), formatTime(dur)));
        }
    });

    connect(m_mediaSlider, &QSlider::sliderReleased, this, [this] {
        m_isDraggingSlider = false;
        const double frac = m_mediaSlider->value() / 1000.0;
        if (!m_isLive) {
            if (m_previewPlayer.isValid()) {
                const double dur = m_previewPlayer.duration();
                m_previewPlayer.seek(frac * dur);
            }
        } else {
            emit mediaSeekRequested(frac);
        }
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        m_volume = val / 100.0;
        if (!m_isLive) {
            if (m_previewPlayer.isValid())
                m_previewPlayer.setVolume(m_volume);
        } else {
            emit mediaVolumeChanged(m_volume);
        }
    });

    return page;
}

QWidget* SlidePaneView::buildDocPage()
{
    auto* page = new QWidget(this);
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setObjectName(QStringLiteral("DocPage"));
    page->setStyleSheet(QStringLiteral("QWidget#DocPage{background:#0F172A;}"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* viewport = new QFrame(page);
    viewport->setObjectName(QStringLiteral("DocViewport"));
    viewport->setStyleSheet(QStringLiteral("QFrame#DocViewport{background:#000000; border:1px solid #1E293B; border-radius:3px;}"));
    auto* vpl = new QVBoxLayout(viewport);
    vpl->setContentsMargins(1, 1, 1, 1);
    m_pageView = new PageView(viewport);
    vpl->addWidget(m_pageView);
    auto* wrap = new QVBoxLayout;
    wrap->setContentsMargins(10, 10, 10, 4);
    wrap->addWidget(viewport);
    v->addLayout(wrap, 1);

    auto* bar = new QFrame(page);
    bar->setFixedHeight(46);
    bar->setObjectName(QStringLiteral("DocBar"));
    bar->setStyleSheet(QStringLiteral("QFrame#DocBar{background:#1E293B; border:none; border-top:1px solid #334155;}"));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 4, 8, 4);
    const QString btn = QStringLiteral("font-size:11px; padding:3px 8px; background:#334155; color:#F8FAFC; border:1px solid #475569;");
    auto* prev = transportButton(QStringLiteral("\u25C0 Prev Slide"), btn, QStringLiteral("Go to previous slide"), bar);
    auto* next = transportButton(QStringLiteral("Next Slide \u25B6"), btn, QStringLiteral("Go to next slide"), bar);
    m_docPrev = prev;
    m_docNext = next;
    connect(prev, &QPushButton::clicked, this, [this] { setDocumentPage(m_docPage - 1); });
    connect(next, &QPushButton::clicked, this, [this] { setDocumentPage(m_docPage + 1); });
    m_transport << prev << next;
    m_docStatus = new QLabel(bar);
    m_docStatus->setFont(tahoma(11));
    bl->addWidget(prev);
    bl->addSpacing(4);
    bl->addWidget(next);
    bl->addStretch(1);
    bl->addWidget(m_docStatus);
    v->addWidget(bar);
    return page;
}

void SlidePaneView::updateHeader()
{
    const QString shownTitle = displayText(m_itemTitle);
    m_headerPlain = m_paneTitle + QStringLiteral(" - ") + shownTitle;
    m_header->setText(QStringLiteral("<span style='font-weight:bold'>%1</span> - <span style='font-weight:600'>%2</span>")
                          .arg(m_paneTitle.toHtmlEscaped(), shownTitle.toHtmlEscaped()));
}

void SlidePaneView::updateDocStatus()
{
    if (m_isLive) {
        m_docStatus->setStyleSheet(QStringLiteral("color:#4ADE80; font-weight:600; background:transparent; border:none;"));
        m_docStatus->setText(QStringLiteral("LIVE \u25CF ") + displayText(m_itemSubtitle));
    } else {
        m_docStatus->setStyleSheet(QStringLiteral("color:#94A3B8; background:transparent; border:none;"));
        m_docStatus->setText(displayText(m_itemSubtitle));
    }
}

void SlidePaneView::setMode(Mode mode)
{
    if (mode != Mode::Media) {
        if (m_videoHost)
            m_videoHost->hide();
        if (m_previewPoster)
            m_previewPoster->hide();
    }
    m_pages->setCurrentIndex(static_cast<int>(mode));
    updateDocButtons();
}

void SlidePaneView::updateDocButtons()
{
    const bool doc = m_doc.isValid() && mode() == Mode::PresentationDoc;
    if (m_docPrev) {
        m_docPrev->setEnabled(doc && m_docPage > 0);
        m_docNext->setEnabled(doc && m_docPage + 1 < m_doc.pageCount);
    }
}

void SlidePaneView::loadDocument(const DocumentRef& doc, int page)
{
    stopPlayback();
    m_media = MediaItem();
    m_doc = doc;
    m_docPage = std::clamp(page, 0, std::max(0, doc.pageCount - 1));
    m_itemTitle = doc.title;
    m_itemSubtitle = QStringLiteral("Slide %1 of %2").arg(m_docPage + 1).arg(doc.pageCount);
    m_slides.clear();
    m_stanzas->clear();
    m_canvas->clearSlide();
    setMode(Mode::PresentationDoc);
    updateHeader();
    updateDocStatus();
    m_pageView->setPage(doc.pdfPath, m_docPage);
    updateDocButtons();
}

void SlidePaneView::setDocumentPage(int page)
{
    if (!m_doc.isValid())
        return;
    const int p = std::clamp(page, 0, m_doc.pageCount - 1);
    if (p == m_docPage)
        return;
    m_docPage = p;
    m_itemSubtitle = QStringLiteral("Slide %1 of %2").arg(p + 1).arg(m_doc.pageCount);
    updateDocStatus();
    m_pageView->setPage(m_doc.pdfPath, p);
    updateDocButtons();
    emit documentPageChanged(p);
}

void SlidePaneView::showDocumentError(const QString& title, const QString& message)
{
    clearDocument();
    m_itemTitle = title;
    m_itemSubtitle.clear();
    m_slides.clear();
    m_stanzas->clear();
    m_canvas->clearSlide();
    setMode(Mode::PresentationDoc);
    updateHeader();
    updateDocStatus();
    m_pageView->setMessage(message);
}

void SlidePaneView::clearDocument()
{
    m_doc = DocumentRef();
    m_docPage = 0;
    m_pageView->clear();
    updateDocButtons();
}

void SlidePaneView::loadEmptyPresentation()
{
    stopPlayback();
    m_media = MediaItem();
    m_slides.clear();
    m_stanzas->clear();
    m_canvas->clearSlide();
    clearDocument();
    setItemTitle(QStringLiteral("Presentation"));
    setItemSubtitle(QString());
    setMode(Mode::PresentationDoc);
    updateHeader();
    updateDocStatus();
}

void SlidePaneView::updateLoopButtonUi()
{
    if (!m_loopBtn)
        return;
    if (m_isLooping) {
        m_loopBtn->setStyleSheet(QStringLiteral(
            "QPushButton { font-size:15px; font-weight:bold; background:#1E293B; border:1px solid #38BDF8; border-radius:3px; color:#38BDF8; }"
            "QPushButton:hover { background:#24354D; color:#7DD3FC; }"
            "QPushButton:disabled { background:#334155; border-color:#475569; color:#64748B; }"
        ));
        m_loopBtn->setToolTip(QStringLiteral("Loop: On (Click to Disable)"));
    } else {
        m_loopBtn->setStyleSheet(QStringLiteral(
            "QPushButton { font-size:15px; font-weight:bold; background:transparent; border:1px solid transparent; border-radius:3px; color:#64748B; }"
            "QPushButton:hover { background:rgba(255,255,255,0.08); color:#94A3B8; }"
            "QPushButton:disabled { background:transparent; border-color:transparent; color:#475569; }"
        ));
        m_loopBtn->setToolTip(QStringLiteral("Loop: Off (Click to Enable)"));
    }
}

void SlidePaneView::setPlaying(bool play)
{
    m_isPlaying = play;
    if (m_playBtn) {
        m_playBtn->setText(play ? QStringLiteral("\u23F8") : QStringLiteral("\u25B6"));
        m_playBtn->setToolTip(play ? QStringLiteral("Pause") : QStringLiteral("Play"));
    }
}

void SlidePaneView::setLooping(bool loop)
{
    m_isLooping = loop;
    updateLoopButtonUi();
}

void SlidePaneView::setVolume(double vol)
{
    m_volume = std::clamp(vol, 0.0, 1.0);
    if (m_volumeSlider) {
        QSignalBlocker b(m_volumeSlider);
        m_volumeSlider->setValue(static_cast<int>(m_volume * 100.0));
    }
    if (!m_isLive && m_previewPlayer.isValid())
        m_previewPlayer.setVolume(m_volume);
}

void SlidePaneView::updateMediaProgress(double positionSec, double durationSec)
{
    m_lastDuration = durationSec;
    if (m_isLive && !m_isDraggingSlider && durationSec > 0) {
        const double frac = std::clamp(positionSec / durationSec, 0.0, 1.0);
        if (m_mediaSlider) {
            QSignalBlocker b(m_mediaSlider);
            m_mediaSlider->setValue(static_cast<int>(frac * 1000.0));
        }
        if (m_timeLabel) {
            m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(positionSec), formatTime(durationSec)));
        }
    }
}

void SlidePaneView::stopPlayback()
{
    m_pollTimer.stop();
    if (m_previewPlayer.isValid()) {
        m_previewPlayer.stop();
        m_previewPlayer.destroy();
    }
    if (m_videoHost)
        m_videoHost->hide();
    if (m_previewPoster)
        m_previewPoster->hide();
    setPlaying(false);
}

void SlidePaneView::loadMedia(const MediaItem& media)
{
    clearDocument();
    stopPlayback();

    m_media = media;
    m_slides.clear();
    m_stanzas->clear();
    m_canvas->clearSlide();
    setItemTitle(media.name);
    setItemSubtitle(media.type == MediaType::Video ? QStringLiteral("Video") : QStringLiteral("Media"));
    setMode(Mode::Media);

    m_isPlaying = false;
    m_isLooping = false;
    m_volume = 1.0;
    updateLoopButtonUi();
    setPlaying(false);
    if (m_mediaSlider) {
        QSignalBlocker b(m_mediaSlider);
        m_mediaSlider->setValue(0);
        m_mediaSlider->setEnabled(true);
    }
    if (m_timeLabel)
        m_timeLabel->setText(QStringLiteral("0:00 / 0:00"));

    if (m_loopBtn) m_loopBtn->setEnabled(true);
    if (m_rewindBtn) m_rewindBtn->setEnabled(true);
    if (m_playBtn) m_playBtn->setEnabled(true);
    if (m_forwardBtn) m_forwardBtn->setEnabled(true);
    if (m_volumeSlider) {
        m_volumeSlider->setValue(100);
        m_volumeSlider->setEnabled(true);
    }

    auto* screen = m_pages->widget(1)->findChild<QFrame*>(QStringLiteral("VideoScreen"));
    auto* sl = screen ? qobject_cast<QStackedLayout*>(screen->layout()) : nullptr;

    if (!m_isLive) {
        m_videoNote->hide();
        const QImage poster = BackgroundThumbnails::instance().image(media.filePath, 800);
        if (!poster.isNull() && m_previewPoster) {
            m_previewPoster->setPixmap(QPixmap::fromImage(poster).scaled(
                m_previewPoster->size().isEmpty() ? QSize(400, 250) : m_previewPoster->size(),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_previewPoster->show();
            if (sl) sl->setCurrentWidget(m_previewPoster);
        }

        if (m_videoHost) {
            m_videoHost->show();
            if (sl) sl->setCurrentWidget(m_videoHost);
            if (m_previewPlayer.init(quintptr(m_videoHost->winId()))) {
                m_previewPlayer.load(media.filePath, MpvPlayer::Role::Media, m_isLooping, /*startPaused=*/true, m_volume);
            }
        }
        m_pollTimer.start(100);
    } else {
        if (m_videoHost) m_videoHost->hide();
        if (m_previewPoster) m_previewPoster->hide();
        if (sl) sl->setCurrentWidget(m_videoNote);
        m_videoNote->show();
    }
}

void SlidePaneView::loadEmptyMedia()
{
    clearDocument();
    stopPlayback();

    m_media = MediaItem();
    m_slides.clear();
    m_stanzas->clear();
    m_canvas->clearSlide();
    setItemTitle(QStringLiteral("Media"));
    setItemSubtitle(QString());
    setMode(Mode::Media);

    m_isPlaying = false;
    m_isLooping = false;
    m_volume = 1.0;
    updateLoopButtonUi();
    setPlaying(false);
    if (m_mediaSlider) {
        QSignalBlocker b(m_mediaSlider);
        m_mediaSlider->setValue(0);
        m_mediaSlider->setEnabled(false);
    }
    if (m_timeLabel)
        m_timeLabel->setText(QStringLiteral("0:00 / 0:00"));

    if (m_loopBtn) m_loopBtn->setEnabled(false);
    if (m_rewindBtn) m_rewindBtn->setEnabled(false);
    if (m_playBtn) m_playBtn->setEnabled(false);
    if (m_forwardBtn) m_forwardBtn->setEnabled(false);
    if (m_volumeSlider) m_volumeSlider->setEnabled(false);

    auto* screen = m_pages->widget(1)->findChild<QFrame*>(QStringLiteral("VideoScreen"));
    auto* sl = screen ? qobject_cast<QStackedLayout*>(screen->layout()) : nullptr;

    if (!m_isLive) {
        if (m_videoHost) m_videoHost->hide();
        if (m_previewPoster) {
            m_previewPoster->clear();
            m_previewPoster->show();
            if (sl) sl->setCurrentWidget(m_previewPoster);
        }
        if (m_videoNote) m_videoNote->hide();
    } else {
        if (m_videoHost) m_videoHost->hide();
        if (m_previewPoster) m_previewPoster->hide();
        if (sl) sl->setCurrentWidget(m_videoNote);
        if (m_videoNote) m_videoNote->show();
    }
}

SlidePaneView::Mode SlidePaneView::mode() const { return static_cast<Mode>(m_pages->currentIndex()); }

void SlidePaneView::setItemTitle(const QString& title)
{
    m_itemTitle = title;
    updateHeader();
}

void SlidePaneView::setItemSubtitle(const QString& subtitle)
{
    m_itemSubtitle = subtitle;
    updateDocStatus();
}

void SlidePaneView::setSlides(const QList<Slide>& slides)
{
    m_slides = slides;
    m_stanzas->clear();
    for (const Slide& s : slides) {
        auto* it = new QListWidgetItem(m_stanzas);
        it->setData(LabelRole, s.label);
        it->setData(ContentRole, s.content);
        it->setData(TypeRole, static_cast<int>(s.type));
        // Unknown spacing (slides not made from song text, e.g. loaded from a schedule file) = one blank line.
        const int row = m_stanzas->count() - 1;
        it->setData(GapRole, row == 0 ? 0 : (s.blankLinesBefore >= 0 ? s.blankLinesBefore : 1));
    }
    if (slides.isEmpty())
        m_canvas->clearSlide();
}

void SlidePaneView::loadItem(const PresentationItem& item, const QString& background, int slideIndex)
{
    clearDocument();
    stopPlayback();
    m_media = MediaItem();
    setMode(Mode::Slides);
    m_itemTitle = item.title;
    m_itemSubtitle = item.subtitle;
    updateHeader();
    updateDocStatus();
    m_canvas->setBackground(background);
    setSlides(item.slides);
    if (!item.slides.isEmpty())
        setCurrentSlideIndex(std::clamp(slideIndex, 0, static_cast<int>(item.slides.size()) - 1));
}

void SlidePaneView::setSettings(const AppSettings& settings)
{
    m_canvas->setSettings(settings);
}

void SlidePaneView::setCurrentSlideIndex(int index) { m_stanzas->setCurrentRow(index); }

int SlidePaneView::currentSlideIndex() const { return m_stanzas->currentRow(); }

} // namespace Lux
