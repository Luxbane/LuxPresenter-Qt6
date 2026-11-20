#include "Services/BackgroundThumbnails.h"
#include "Services/AppPaths.h"
#include "Services/BackgroundUtil.h"
#include "Services/VideoPoster.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QThreadPool>
namespace Lux {
BackgroundThumbnails& BackgroundThumbnails::instance()
{
    static BackgroundThumbnails* inst = new BackgroundThumbnails;   // lives for the whole run
    return *inst;
}
QString BackgroundThumbnails::key(const QString& path, int w) const
{
    const QFileInfo fi(path);
    return QStringLiteral("%1|%2|%3|%4").arg(path).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size()).arg(w);
}
QString BackgroundThumbnails::posterFileFor(const QString& videoPath) const
{
    return AppPaths::thumbnailPathFor(videoPath);
}
void BackgroundThumbnails::clearMemoryCache()
{
    m_cache.clear();
    m_failed.clear();
}
BackgroundThumbnails::State BackgroundThumbnails::state(const QString& path, int maxWidth) const
{
    if (!Backgrounds::isImagePath(path) && !Backgrounds::isVideoPath(path))
        return State::NotApplicable;
    const QString k = key(path, maxWidth);
    if (m_cache.contains(k))
        return State::Ready;
    if (m_pending.contains(k))
        return State::Pending;
    if (m_failed.contains(k))
        return State::Failed;
    return QFileInfo::exists(path) ? State::Pending : State::Failed;
}
QImage BackgroundThumbnails::image(const QString& path, int maxWidth)
{
    const bool video = Backgrounds::isVideoPath(path);
    if ((!video && !Backgrounds::isImagePath(path)) || !QFileInfo(path).isFile())
        return {};
    const QString k = key(path, maxWidth);
    auto it = m_cache.constFind(k);
    if (it != m_cache.constEnd())
        return it.value();
    if (m_failed.contains(k) || m_pending.contains(k))
        return {};
    auto scaled = [maxWidth](const QString& file) {
        QImageReader r(file);
        r.setAutoTransform(true);
        const QSize sz = r.size();
        if (sz.isValid() && sz.width() > maxWidth)
            r.setScaledSize(QSize(maxWidth, qMax(1, sz.height() * maxWidth / sz.width())));
        return r.read();
    };
    if (!video) {
        const QImage img = scaled(path);
        if (img.isNull()) {
            m_failed.insert(k);
            return {};
        }
        m_cache.insert(k, img);
        return img;
    }
    const QString poster = posterFileFor(path);
    if (QFileInfo(poster).size() > 0) {
        const QImage img = scaled(poster);
        if (!img.isNull()) {
            m_cache.insert(k, img);
            return img;
        }
    }
    m_pending.insert(k);
    QPointer<BackgroundThumbnails> self(this);
    QThreadPool::globalInstance()->start([self, path, poster, k, scaled] {
        const bool ok = VideoPoster::generate(path, poster);
        const QImage img = ok ? scaled(poster) : QImage();
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, path, k, img] {
            if (!self)
                return;
            self->m_pending.remove(k);
            if (img.isNull())
                self->m_failed.insert(k);
            else
                self->m_cache.insert(k, img);
            emit self->ready(path);
        }, Qt::QueuedConnection);
    });
    return {};
}
} // namespace Lux
