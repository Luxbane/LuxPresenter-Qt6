#pragma once

#include "Models/AppSettings.h"
#include "Models/Slide.h"

#include <QTimer>
#include <QWidget>

namespace Lux {

// The rendered 16:9 slide of SlidePaneView.xaml: 640x360 logical canvas scaled to fit
// (letterboxed), black base, background colour/image, dark tint, outlined text and the
// Scripture reference header / bottom-right label. Text sizing follows
// SlidePaneView.UpdateSlideContent: the largest size (<= SongFontSize*6.5 at 1920x1080,
// min 34) that fits 1700x840, then scaled by 1/3 to the 640-wide canvas.
class SlideCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit SlideCanvas(QWidget* parent = nullptr);

    void setSlide(const Slide& slide);
    void clearSlide();
    void setBackground(const QString& hexOrImagePath); // "#RRGGBB" or an image file path
    void setSettings(const AppSettings& settings);     // alignment, font, capitalisation
    // Live pane mirrors of the projector toggles: Black covers the slide, Clear hides its text.
    void setBlack(bool on) { m_black = on; update(); }
    void setTextCleared(bool on) { m_cleared = on; update(); }
    void setLive(bool live);
    bool isLive() const { return m_isLive; }
    bool isBlack() const { return m_black; }
    bool isTextCleared() const { return m_cleared; }
    bool hasSlide() const { return m_hasSlide; }
    const Slide& slide() const { return m_slide; }
    QString background() const { return m_background; }
    // Pixel size (at 1920x1080) chosen for the current slide; 0 when empty. Test helper.
    qreal fullScaleFontSize() const;

    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString formattedContent() const;
    QFont slideFont() const;

    Slide m_slide;
    bool m_hasSlide = false;
    bool m_black = false;
    bool m_cleared = false;
    bool m_isLive = false;
    QTimer m_alertTimer;
    qint64 m_alertStartTime = 0;
    QString m_background = QStringLiteral("#001833"); // MainViewModel.ActiveGlobalBackground default
    AppSettings m_settings;
    mutable QString m_fsKey;      // memo of fullScaleFontSize (the search lays the text out 7 times)
    mutable qreal m_fsValue = 0;
    mutable QPixmap m_cachedTextPixmap;
    mutable QString m_cachedTextKey;
};

} // namespace Lux
