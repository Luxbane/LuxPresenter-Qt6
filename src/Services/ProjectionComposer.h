#pragma once

#include "Models/Slide.h"
#include "Services/ProjectionState.h"

#include <QElapsedTimer>
#include <QList>
#include <QString>

#include <functional>

namespace Lux {

// Linear opacity animation (WPF DoubleAnimation with HoldEnd).
struct Fade
{
    double from = 0, to = 0;
    qint64 start = 0;
    int duration = 0;
    bool active = false;

    void set(double v) { from = to = v; duration = 0; active = false; }
    void run(double f, double t, int ms, qint64 now)
    {
        if (ms <= 0) { set(t); return; }
        from = f; to = t; start = now; duration = ms; active = true;
    }
    double value(qint64 now) const
    {
        if (!active) return to;
        const double p = double(now - start) / duration;
        return p >= 1.0 ? to : (p <= 0.0 ? from : from + (to - from) * p);
    }
    bool running(qint64 now) const { return active && (now - start) < duration; }
};

// What a slide layer shows (one of the two crossfading text layers).
struct SlideLayerData
{
    bool valid = false;
    int serial = 0;          // bumped whenever the content changes (invalidates paint-time caches)
    Slide slide;
    QString text;            // formatted lyrics / scripture text
    QString footer;          // scripture reference or verse label
    bool footerVisible = false;
    QString fontFamily;
    bool bold = true, italic = false, outline = true, shadow = true;
    QString hAlign = QStringLiteral("Center");
    int basePt = 13;
    QString vAlign = QStringLiteral("Center");
    QString labelLocation = QStringLiteral("Bottom Right");   // where the footer (verse label / scripture reference) goes
    QString labelSize = QStringLiteral("Normal");
    bool labelIndent = false;
    bool wrapText = true;
};

// Instructions for the dual-deck video engine, produced by the composer.
struct VideoCommand
{
    enum class Kind { PlayBackground, PlayMedia, Stop, Pause, Resume };
    Kind kind = Kind::Stop;
    QString path;
    int crossfadeMs = 0;
    bool playing = true;   // PlayMedia: start playing (false = load paused)
    bool looping = true;
    bool operator==(const VideoCommand&) const = default;
};

// The projector transition pipeline (port of ProjectionWindow.UpdateState and its helpers) as a pure
// model: it decides which layers are visible and animates their opacities. Painting (ProjectionWindow)
// and video playback (dual-deck mpv) read from it. A clock can be injected for deterministic tests.
class ProjectionComposer
{
public:
    ProjectionComposer();

    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    qint64 now() const { return m_clock ? m_clock() : m_timer.elapsed(); }

    // Forget everything shown so far; the next state is applied without transitions (window re-shown).
    void reset();
    void applyState(const ProjectionState& state);
    // ProjectionWindow.UpdateMargins (percent-like margins from the options).
    void setMargins(int top, int bottom, int left, int right);

    // Called with the layer index right after a slide layer was filled and before its fade starts, so the
    // renderer can do the expensive work (font fitting, text rasterisation) up front; the fade then starts
    // after it, and no frame of the transition has to wait for it.
    void setLayerPreparer(std::function<void(int)> fn) { m_prepare = std::move(fn); }
    // Same for the presentation page (called when the shown page changes).
    void setPresentationPreparer(std::function<void()> fn) { m_preparePresentation = std::move(fn); }
    // Same for background images: preloads and decodes the image ahead of starting the fade.
    void setBackgroundPreparer(std::function<void(const QString&)> fn) { m_prepareBackground = std::move(fn); }

    // ---- scene description (valid at time `t`) ----
    struct Margins { double left = 100, top = 20, right = 100, bottom = 70; };
    Margins margins() const { return m_margins; }
    double contentWidth() const;

    double blackOpacity(qint64 t) const { return m_blackVisible ? m_black.value(t) : 0.0; }
    double logoOpacity(qint64 t) const { return m_logoVisible ? m_logo.value(t) : 0.0; }
    bool blackVisible() const { return m_blackVisible; }
    bool logoVisible() const { return m_logoVisible; }
    double slideOpacity(int layer, qint64 t) const { return m_slideFade[layer].value(t); }
    const SlideLayerData& slideLayer(int layer) const { return m_slide[layer]; }
    int activeSlideLayer() const { return m_activeLayer; }
    QString viewboxVerticalAlignment() const { return m_viewVAlign; }
    double bgImageOpacity(int layer, qint64 t) const { return m_bgImgFade[layer].value(t); }
    QString bgImagePath(int layer) const { return m_bgImgPath[layer]; }
    int activeBgLayer() const { return m_activeBgLayer; }
    // Colour behind everything ("transparent" while a video shows through from the video window).
    QString rootBackground() const { return m_rootBackground; }
    bool videoShowsThrough() const { return m_rootBackground.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) == 0; }
    bool inVideoMode() const { return m_inVideoMode; }
    QString currentBackground() const { return m_currentBg; }
    // Presentation document page being shown (replaces slides and backgrounds, on black).
    bool presentationActive() const { return m_presActive; }
    QString presentationPdf() const { return m_presPdf; }
    int presentationPage() const { return m_presPage; }
    int presentationSerial() const { return m_presSerial; }
    bool isAnimating(qint64 t) const;
    // Runs completed fade-out clean-ups (call from the repaint timer).
    void advance(qint64 t) { settle(t); }

    // Pending video-engine instructions (cleared by the call).
    QList<VideoCommand> takeVideoCommands();

private:
    void settle(qint64 t);                    // run completed fade-out clean-ups
    void later(qint64 at, std::function<void()> fn);
    void setBlackOverlay(bool show, const QString& transition, int durationMs, qint64 t);
    void setLogoOverlay(bool show, const QString& transition, int durationMs, qint64 t);
    void applyBackground(const QString& bg, const QString& transition, int durationMs, qint64 t);
    void fadeOutImages(int durationMs, qint64 t);
    void stopBackgroundVideo();
    void fadeSlides(double toOpacity, int durationMs, bool blend, qint64 t);
    void emitVideo(VideoCommand::Kind k, const QString& path = {}, int ms = 0, bool playing = true, bool looping = true);

    std::function<qint64()> m_clock;
    std::function<void(int)> m_prepare;
    std::function<void()> m_preparePresentation;
    std::function<void(const QString&)> m_prepareBackground;
    bool m_presActive = false;
    QString m_presPdf;
    int m_presPage = -1;
    int m_presSerial = 0;
    QElapsedTimer m_timer;
    bool m_first = true;

    // overlays
    bool m_blackShown = false, m_logoShown = false;     // _isCurrentlyBlack / _isCurrentlyLogo
    bool m_blackVisible = false, m_logoVisible = false; // Visibility != Collapsed
    Fade m_black, m_logo;

    // slides (double-buffered)
    int m_activeLayer = 0;
    SlideLayerData m_slide[2];
    Fade m_slideFade[2];
    QString m_viewVAlign = QStringLiteral("Center");
    QString m_lastSlideId, m_lastText, m_lastV, m_lastH;
    bool m_hasLast = false;
    bool m_lastShowLabels = false;
    QString m_lastLayoutKey;
    int m_lastFontSize = -1;
    QString m_lastFontFamily;
    bool m_clearShown = false;
    int m_serial = 0;

    // background
    QString m_rootBackground = QStringLiteral("#000000");
    QString m_currentBg;
    bool m_hasCurrentBg = false;
    int m_activeBgLayer = 0;
    QString m_bgImgPath[2];
    Fade m_bgImgFade[2];
    bool m_bgVideoActive = false;   // a background video is playing in the deck window
    bool m_videoPaused = false;
    bool m_inVideoMode = false;     // standalone media video
    QString m_mediaPath;

    Margins m_margins;
    QList<VideoCommand> m_commands;
    struct Pending { qint64 at; std::function<void()> fn; };
    QList<Pending> m_pending;
};

} // namespace Lux
