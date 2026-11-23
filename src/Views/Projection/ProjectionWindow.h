#pragma once

#include "Services/ProjectionComposer.h"

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include <functional>

namespace Lux {

// The projector's single unified canvas window: an opaque, frameless, non-activating window
// that paints the 1920x1080 scene (backgrounds, slide text layers, black and logo overlays)
// directly to hardware in a single canvas.
class ProjectionWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectionWindow(const ProjectionComposer* composer, QWidget* parent = nullptr);

    // Paints the scene for time `t` into `target` (any paint device; used by the window and by tests).
    void paintScene(QPainter* p, const QRectF& target, qint64 t);
    // Chosen pixel size of the slide text on a layer (cached per layer content). Test helper.
    qreal layerFontSize(int layer);
    // Does the expensive per-slide work now (font fitting + rasterising the text layer for the current output
    // size) so that painting a transition frame is only a blit. Safe to call at any time.
    void prepareLayer(int layer);
    // Renders the presentation page for the current output size ahead of the first paint (see ProjectionComposer).
    void preparePresentation();
    // Preloads and decodes an image background into memory ahead of transitions, preventing frame drops.
    void preloadImage(const QString& path);
    // Presentation page image for an output of `deviceBox` pixels (cached per page and size). Null when unavailable.
    QImage presentationImage(const QSize& deviceBox);
    // Dynamically switches between Opaque Direct-Flip mode (image/slide/color) and Layered transparent mode (video).
    void setLayered(bool layered);
    bool isLayered() const { return m_isLayered; }
    // Test hooks
    int cacheRenderCount() const { return m_cacheRenders; }
    void setLayerCacheEnabled(bool on) { m_cacheEnabled = on; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    struct LayerCache
    {
        QImage img;                 // device-pixel rendering of the text layer (premultiplied ARGB)
        QPoint originDev;           // device position of its top-left corner
        int serial = -1;
        QRectF bounds;              // design-space rectangle it covers
        qreal sd = 0, dpr = 0;      // design->device scale, device pixel ratio
        QPointF boxDev;             // device position of the design canvas
    };
    QRectF layerBounds(const SlideLayerData& d) const;
    const LayerCache& cacheFor(int layer, const QRectF& box, qreal s, qreal dpr);
    QPixmap image(const QString& path);
    void paintLayer(QPainter* p, int layer);
    void paintLogo(QPainter* p);
    void paintAlert(QPainter* p, const QRectF& target);

    const ProjectionComposer* m_composer;
    QHash<QString, QPixmap> m_images;
    LayerCache m_cache[2];
    QImage m_presImage;
    int m_presKeySerial = -1;
    QSize m_presKeyBox;
    int m_cacheRenders = 0;
    bool m_cacheEnabled = true;
    int m_fontSerial[2] = {-1, -1};
    qreal m_fontSize[2] = {0, 0};
    QTimer m_alertAnimTimer;
    qint64 m_alertPassStartTime = 0;
    bool m_isLayered = false;
};

} // namespace Lux
