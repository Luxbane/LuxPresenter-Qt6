#include "Services/ProjectionComposer.h"

#include "Services/LyricsFormatter.h"
#include "Services/TextRepair.h"

#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>

#include <algorithm>

namespace Lux {

namespace {

bool isHexColor(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^#([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$"));
    return re.match(s).hasMatch();
}

bool isBlend(const QString& v) { return v.compare(QStringLiteral("Blend"), Qt::CaseInsensitive) == 0; }

bool isVideoFile(const QString& path)
{
    static const QStringList ext{"mp4", "wmv", "avi", "mov", "mkv", "m4v", "mpg", "mpeg", "webm", "ts", "flv", "mts", "m2ts"};
    return !path.trimmed().isEmpty() && ext.contains(QFileInfo(path).suffix().toLower());
}

QString layoutKey(const ProjectionState& s)
{
    return s.labelLocation + QLatin1Char('|') + s.labelSize + (s.indentLabel ? "|i" : "|-") + (s.autoWrapWords ? "|w" : "|-");
}
AppSettings formattingSettings(const ProjectionState& s)
{
    AppSettings a;
    a.capitalizeAllWords = s.capitalizeAllWords;
    a.capitalizeFirstCharEachLine = s.capitalizeFirstCharEachLine;
    a.autoCapitalizeKeywords = s.autoCapitalizeKeywords;
    a.capitalizeKeywords = s.capitalizeKeywords;
    return a;
}

} // namespace

ProjectionComposer::ProjectionComposer()
{
    m_timer.start();
}

double ProjectionComposer::contentWidth() const
{
    const double w = 1920 - m_margins.left - m_margins.right;
    return w > 200 ? w : 1720;
}

void ProjectionComposer::later(qint64 at, std::function<void()> fn)
{
    m_pending.append({at, std::move(fn)});
}

void ProjectionComposer::settle(qint64 t)
{
    for (int i = 0; i < m_pending.size();) {
        if (m_pending[i].at <= t) {
            auto fn = std::move(m_pending[i].fn);
            m_pending.removeAt(i);
            fn();
        } else {
            ++i;
        }
    }
}

bool ProjectionComposer::isAnimating(qint64 t) const
{
    if (!m_pending.isEmpty())
        return true;
    if (m_black.running(t) || m_logo.running(t))
        return true;
    for (int i = 0; i < 2; ++i)
        if (m_slideFade[i].running(t) || m_bgImgFade[i].running(t))
            return true;
    return false;
}

void ProjectionComposer::emitVideo(VideoCommand::Kind k, const QString& path, int ms, bool playing, bool looping)
{
    VideoCommand c;
    c.kind = k;
    c.path = path;
    c.crossfadeMs = ms;
    c.playing = playing;
    c.looping = looping;
    m_commands.append(c);
}

QList<VideoCommand> ProjectionComposer::takeVideoCommands()
{
    QList<VideoCommand> out;
    out.swap(m_commands);
    return out;
}

void ProjectionComposer::reset()
{
    if (m_bgVideoActive || m_inVideoMode)
        emitVideo(VideoCommand::Kind::Stop);
    m_first = true;
    m_blackShown = m_logoShown = m_blackVisible = m_logoVisible = false;
    m_black.set(0);
    m_logo.set(0);
    m_activeLayer = 0;
    for (int i = 0; i < 2; ++i) {
        m_slide[i] = SlideLayerData();
        m_slideFade[i].set(0);
        m_bgImgPath[i].clear();
        m_bgImgFade[i].set(0);
    }
    m_viewVAlign = QStringLiteral("Center");
    m_hasLast = false;
    m_lastSlideId.clear();
    m_lastText.clear();
    m_clearShown = false;
    m_rootBackground = QStringLiteral("#000000");
    m_currentBg.clear();
    m_hasCurrentBg = false;
    m_activeBgLayer = 0;
    m_bgVideoActive = false;
    m_videoPaused = false;
    m_inVideoMode = false;
    m_mediaPath.clear();
    m_presActive = false;
    m_presPdf.clear();
    m_presPage = -1;
    m_pending.clear();
}

void ProjectionComposer::setMargins(int top, int bottom, int left, int right)
{
    m_margins.left = std::max(10, 50 + left * 3);
    m_margins.top = std::max(5, 10 + top * 2);
    m_margins.right = std::max(10, 50 + right * 3);
    m_margins.bottom = std::max(10, 30 + bottom * 3);
}

// ---- overlays ---------------------------------------------------------------------------

void ProjectionComposer::setBlackOverlay(bool show, const QString& transition, int durationMs, qint64 t)
{
    if (show == m_blackShown)
        return;
    m_blackShown = show;
    const int duration = durationMs > 0 ? durationMs : 250;
    if (show) {
        m_blackVisible = true;
        if (isBlend(transition))
            m_black.run(0.0, 1.0, duration, t);
        else
            m_black.set(1.0);
    } else if (isBlend(transition)) {
        m_black.run(m_black.value(t), 0.0, duration, t);
        later(t + duration, [this] { if (!m_blackShown) m_blackVisible = false; });
    } else {
        m_black.set(0.0);
        m_blackVisible = false;
    }
}

void ProjectionComposer::setLogoOverlay(bool show, const QString& transition, int durationMs, qint64 t)
{
    if (show == m_logoShown)
        return;
    m_logoShown = show;
    const int duration = durationMs > 0 ? durationMs : 250;
    if (show) {
        m_logoVisible = true;
        if (isBlend(transition))
            m_logo.run(0.0, 1.0, duration, t);
        else
            m_logo.set(1.0);
    } else if (isBlend(transition)) {
        m_logo.run(m_logo.value(t), 0.0, duration, t);
        later(t + duration, [this] { if (!m_logoShown) m_logoVisible = false; });
    } else {
        m_logo.set(0.0);
        m_logoVisible = false;
    }
}

// ---- background -------------------------------------------------------------------------

void ProjectionComposer::stopBackgroundVideo()
{
    if (m_bgVideoActive) {
        emitVideo(VideoCommand::Kind::Stop);
        m_bgVideoActive = false;
        m_videoPaused = false;
    }
}

void ProjectionComposer::fadeOutImages(int durationMs, qint64 t)
{
    for (int i = 0; i < 2; ++i) {
        if (m_bgImgPath[i].isEmpty() || m_bgImgFade[i].value(t) <= 0.01)
            continue;
        if (durationMs > 0) {
            m_bgImgFade[i].run(m_bgImgFade[i].value(t), 0.0, durationMs, t);
            later(t + durationMs, [this, i] {
                if (m_activeBgLayer != i)
                    m_bgImgPath[i].clear();
            });
        } else {
            m_bgImgFade[i].set(0.0);
            m_bgImgPath[i].clear();
        }
    }
}

void ProjectionComposer::applyBackground(const QString& bg, const QString& transition, int durationMs, qint64 t)
{
    auto clearImages = [&] {
        for (int i = 0; i < 2; ++i) {
            if (m_bgImgPath[i].isEmpty())
                continue;
            m_bgImgFade[i].run(m_bgImgFade[i].value(t), 0.0, durationMs, t);
            if (durationMs <= 0) {
                m_bgImgPath[i].clear();
            } else {
                later(t + durationMs, [this, i] { m_bgImgPath[i].clear(); });
            }
        }
    };

    if (bg.trimmed().isEmpty()) {
        if (m_hasCurrentBg) {
            m_hasCurrentBg = false;
            m_currentBg.clear();
            clearImages();
            stopBackgroundVideo();
        }
        m_rootBackground = QStringLiteral("#001833");
        return;
    }
    if (m_hasCurrentBg && m_currentBg == bg && !m_first)
        return;

    m_currentBg = bg;
    m_hasCurrentBg = true;

    if (bg.startsWith(QLatin1Char('#'))) {
        clearImages();
        stopBackgroundVideo();
        m_rootBackground = isHexColor(bg) ? bg : QStringLiteral("#001833");
    } else if (QFile::exists(bg)) {
        if (isVideoFile(bg)) {
            const bool useBlend = isBlend(transition) && !m_first;
            m_rootBackground = QStringLiteral("transparent");
            emitVideo(VideoCommand::Kind::PlayBackground, bg, useBlend && durationMs > 0 ? durationMs : 0, true, true);
            m_bgVideoActive = true;
            m_videoPaused = false;
            if (useBlend && durationMs > 0) {
                fadeOutImages(durationMs, t);
            } else {
                for (int i = 0; i < 2; ++i) {
                    m_bgImgFade[i].set(0.0);
                    m_bgImgPath[i].clear();
                }
            }
        } else {
            const int next = 1 - m_activeBgLayer;
            const int out = m_activeBgLayer;
            m_bgImgPath[next] = bg;
            const bool useBlend = isBlend(transition) && !m_first;
            if (useBlend && durationMs > 0) {
                m_bgImgFade[next].run(0.0, 1.0, durationMs, t);
                later(t + durationMs, [this] {
                    stopBackgroundVideo();
                    m_rootBackground = QStringLiteral("#001833");
                });
                if (!m_bgImgPath[out].isEmpty() && m_bgImgFade[out].value(t) > 0.01) {
                    m_bgImgFade[out].run(m_bgImgFade[out].value(t), 0.0, durationMs, t);
                    later(t + durationMs, [this, out] {
                        if (m_activeBgLayer != out) {
                            m_bgImgPath[out].clear();
                            m_bgImgFade[out].set(0.0);
                        }
                    });
                }
            } else {
                m_bgImgFade[next].set(1.0);
                m_bgImgFade[out].set(0.0);
                m_bgImgPath[out].clear();
                stopBackgroundVideo();
                m_rootBackground = QStringLiteral("#001833");
            }
            m_activeBgLayer = next;
        }
    } else {
        m_rootBackground = QStringLiteral("#001833");
        clearImages();
        stopBackgroundVideo();
    }
}

void ProjectionComposer::fadeSlides(double toOpacity, int durationMs, bool blend, qint64 t)
{
    for (int i = 0; i < 2; ++i) {
        if (blend)
            m_slideFade[i].run(m_slideFade[i].value(t), toOpacity, durationMs, t);
        else
            m_slideFade[i].set(toOpacity);
    }
}

// ---- main entry (ProjectionWindow.UpdateState) -------------------------------------------

void ProjectionComposer::applyState(const ProjectionState& st)
{
    const qint64 t = now();
    settle(t);
    const int duration = st.transitionDurationMs > 0 ? st.transitionDurationMs : 250;
    // The first state after the window is (re)shown appears at once.
    const QString screenTr = m_first ? QStringLiteral("Cut") : st.screenTransition;

    // 1. Black overlay (also used for "output off")
    const bool shouldBlack = !st.isLive || st.isBlack;
    setBlackOverlay(shouldBlack, screenTr, duration, t);

    // 2. Logo overlay (shown when active and screen is not blacked out)
    setLogoOverlay(!shouldBlack && st.isLogo, screenTr, duration, t);

    // Silent Instant Transition: while the screen is blacked out or showing logo,
    // all underlying slide, text, and background changes must happen instantaneously (0 ms cut)
    // behind the curtain so that no stale background flashes when black/logo is toggled off.
    const bool isCovered = shouldBlack || st.isLogo;
    const int effectiveDuration = isCovered ? 0 : duration;
    const QString effectiveSongTr = isCovered ? QStringLiteral("Cut") : st.songTransition;
    const QString effectiveSlideTr = isCovered ? QStringLiteral("Cut") : st.slideTransition;
    const QString effectiveMediaTr = isCovered ? QStringLiteral("Cut") : st.mediaTransition;

    if (isCovered) {
        if ((m_bgVideoActive || m_inVideoMode) && !m_videoPaused) {
            emitVideo(VideoCommand::Kind::Pause);
            m_videoPaused = true;
        }
    } else {
        if (m_videoPaused && ((m_inVideoMode && st.isVideoPlaying) || (!m_inVideoMode && m_bgVideoActive))) {
            emitVideo(VideoCommand::Kind::Resume);
            m_videoPaused = false;
        } else if (m_inVideoMode && !st.isVideoPlaying && !m_videoPaused) {
            emitVideo(VideoCommand::Kind::Pause);
            m_videoPaused = true;
        }
    }

    // Presentation document page: black backdrop, the page fitted into the output (no slide text/background).
    if (st.isPresentationMode && !st.presentationPdfPath.isEmpty()) {
        if (!m_presActive) {
            m_presActive = true;
            if (m_inVideoMode || m_bgVideoActive) {
                emitVideo(VideoCommand::Kind::Stop);
                m_videoPaused = false;
            }
            m_inVideoMode = false;
            m_mediaPath.clear();
            m_bgVideoActive = false;
            m_hasCurrentBg = false;
            m_currentBg.clear();
            for (int i = 0; i < 2; ++i) {
                m_bgImgFade[i].set(0.0);
                m_bgImgPath[i].clear();
                m_slideFade[i].set(0.0);
            }
            m_hasLast = false;
            m_lastSlideId.clear();
            m_lastText.clear();
            m_clearShown = false;
            m_rootBackground = QStringLiteral("#000000");
        }
        if (m_presPdf != st.presentationPdfPath || m_presPage != st.presentationPage) {
            m_presPdf = st.presentationPdfPath;
            m_presPage = st.presentationPage;
            ++m_presSerial;
            if (m_preparePresentation)
                m_preparePresentation();
        }
        setLogoOverlay(st.isLogo, screenTr, duration, t);
        m_first = false;
        return;
    }
    if (m_presActive) {      // back to slides: re-apply the background and the current slide
        m_presActive = false;
        m_presPdf.clear();
        m_presPage = -1;
        ++m_presSerial;
        m_hasCurrentBg = false;
        m_currentBg.clear();
        m_hasLast = false;
    }

    // Standalone video mode (Media tab video)
    if (st.isVideoMode && !st.mediaFilePath.isEmpty() && QFile::exists(st.mediaFilePath)) {
        if (!m_inVideoMode) {
            m_inVideoMode = true;
            m_hasCurrentBg = false;
            m_currentBg.clear();
            setLogoOverlay(false, screenTr, duration, t);
            const bool useMediaBlend = isBlend(st.mediaTransition) || isBlend(st.songTransition) || isBlend(st.screenTransition);
            m_rootBackground = QStringLiteral("transparent");
            m_mediaPath = st.mediaFilePath;
            m_bgVideoActive = false;
            if (useMediaBlend && duration > 0 && !m_first) {
                emitVideo(VideoCommand::Kind::PlayMedia, st.mediaFilePath, duration, st.isVideoPlaying, st.isVideoLooping);
                fadeSlides(0.0, duration, true, t);
                fadeOutImages(duration, t);
            } else {
                emitVideo(VideoCommand::Kind::PlayMedia, st.mediaFilePath, 0, st.isVideoPlaying, st.isVideoLooping);
                fadeSlides(0.0, 0, false, t);
                for (int i = 0; i < 2; ++i) {
                    m_bgImgFade[i].set(0.0);
                    m_bgImgPath[i].clear();
                }
            }
            m_videoPaused = !st.isVideoPlaying;
        } else if (m_mediaPath != st.mediaFilePath) {
            m_mediaPath = st.mediaFilePath;
            emitVideo(VideoCommand::Kind::PlayMedia, st.mediaFilePath, duration, st.isVideoPlaying, st.isVideoLooping);
            m_videoPaused = !st.isVideoPlaying;
        }
        m_first = false;
        return;
    }
    if (m_inVideoMode) {
        m_inVideoMode = false;
        m_mediaPath.clear();
        m_hasCurrentBg = false; // force the song background to be re-applied
        m_currentBg.clear();
        m_videoPaused = false;
        emitVideo(VideoCommand::Kind::Stop);
        m_rootBackground = QStringLiteral("#001833");
    }

    // 3. Background (colour / image / video, with blend or instant cut if covered)
    applyBackground(st.activeBackground, effectiveSongTr, effectiveDuration, t);
    if (isCovered && m_bgVideoActive && !m_videoPaused) {
        emitVideo(VideoCommand::Kind::Pause);
        m_videoPaused = true;
    }

    // 4. Clear text
    bool wasCleared = m_clearShown;
    if (st.isClear) {
        if (!m_clearShown || m_slideFade[0].value(t) > 0.01 || m_slideFade[1].value(t) > 0.01) {
            m_clearShown = true;
            fadeSlides(0.0, effectiveDuration, isBlend(st.screenTransition) && effectiveDuration > 0, t);
        }
        m_first = false;
        return;
    }
    if (m_clearShown) {
        m_clearShown = false;
        wasCleared = true;
    }

    // 5. Slide display with double-buffer crossfade
    if (!st.activeSlide) {
        fadeSlides(0.0, effectiveDuration, isBlend(effectiveSlideTr) && effectiveDuration > 0, t);
        m_hasLast = false;
        m_lastSlideId.clear();
        m_lastText.clear();
        m_first = false;
        return;
    }
    const Slide& slide = *st.activeSlide;
    const AppSettings fmt = formattingSettings(st);
    const QString formatted = LyricsFormatter::format(displayText(slide.content), &fmt);

    const bool changed = wasCleared || !m_hasLast || m_lastSlideId != slide.id || m_lastText != formatted ||
                         m_lastV != st.songVerticalAlignment || m_lastH != st.songTextAlignment ||
                         m_lastShowLabels != st.showVerseLabels || m_lastLayoutKey != layoutKey(st) || m_lastFontSize != slide.fontSize ||
                         m_lastFontFamily != slide.fontFamily;
    if (!changed) {
        m_first = false;
        return;
    }
    m_hasLast = true;
    m_lastSlideId = slide.id;
    m_lastText = formatted;
    m_lastV = st.songVerticalAlignment;
    m_lastH = st.songTextAlignment;
    m_lastShowLabels = st.showVerseLabels;
    m_lastLayoutKey = layoutKey(st);
    m_lastFontSize = slide.fontSize;
    m_lastFontFamily = slide.fontFamily;

    const int next = 1 - m_activeLayer;
    const int out = m_activeLayer;
    SlideLayerData layer;
    layer.valid = true;
    layer.serial = ++m_serial;
    layer.slide = slide;
    if (slide.type == SlideType::Scripture) {
        const QString suffix = slide.subtitle.trimmed().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(slide.subtitle);
        layer.text = LyricsFormatter::format(displayText(slide.content) + suffix, &fmt);
        layer.footer = displayText(slide.title);
        layer.footerVisible = true;
    } else {
        layer.text = formatted;
        if (st.showVerseLabels && !slide.label.trimmed().isEmpty()) {
            layer.footer = displayText(slide.label);
            layer.footerVisible = true;
            layer.labelLocation = st.labelLocation;
            layer.labelSize = st.labelSize;
            layer.labelIndent = st.indentLabel;
        }
    }
    layer.wrapText = st.autoWrapWords;
    // Options > Font applies to songs when drawn; a scripture slide keeps its own font.
    const bool ownFont = slide.type == SlideType::Scripture && !slide.fontFamily.trimmed().isEmpty();
    layer.fontFamily = ownFont ? slide.fontFamily
                               : (st.songFontFamily.isEmpty() ? (slide.fontFamily.isEmpty() ? QStringLiteral("Arial") : slide.fontFamily)
                                                              : st.songFontFamily);
    layer.bold = st.songFontBold || slide.isBold;
    layer.italic = slide.isItalic;
    layer.outline = slide.hasOutline;
    layer.shadow = slide.hasShadow;
    layer.hAlign = st.songTextAlignment;
    layer.vAlign = st.songVerticalAlignment;
    layer.basePt = st.songFontSize > 0 ? st.songFontSize : (slide.fontSize > 0 ? slide.fontSize : 13);
    m_slide[next] = layer;
    m_viewVAlign = st.songVerticalAlignment;
    if (m_prepare)
        m_prepare(next);
    const qint64 tf = now();   // the transition starts after the preparation work, not before it

    const bool useBlend = isBlend(effectiveSlideTr) && !m_first && effectiveDuration > 0;
    if (useBlend) {
        // If a transition was already running (rapid navigation / key presses),
        // fast-forward the incoming slide so text doesn't stay washed out or laggy.
        const bool wasAnimating = m_slideFade[out].running(tf);
        const int actualDuration = wasAnimating ? std::min(150, effectiveDuration / 2) : effectiveDuration;
        m_slideFade[next].run(wasAnimating ? 0.4 : 0.0, 1.0, actualDuration, tf);
        const double curOut = wasCleared ? 0.0 : (m_slideFade[out].value(tf) > 0.05 ? m_slideFade[out].value(tf) : 1.0);
        if (curOut > 0.01)
            m_slideFade[out].run(curOut, 0.0, actualDuration, tf);
        else
            m_slideFade[out].set(0.0);
    } else {
        m_slideFade[next].set(1.0);
        m_slideFade[out].set(0.0);
    }
    m_activeLayer = next;
    m_first = false;
}

} // namespace Lux
