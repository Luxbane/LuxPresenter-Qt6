#include "Services/PresentationService.h"

#include "Services/AppPaths.h"
#include "Services/PageImageCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

namespace Lux {

namespace {
QString normalised(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower(); }
bool isPdf(const QString& path) { return QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0; }
} // namespace

PresentationService::PresentationService(QObject* parent) : QObject(parent), m_converter(this)
{
    qRegisterMetaType<Lux::PresentationService::Document>();
}

bool PresentationService::isSupportedFile(const QString& path)
{
    return isPdf(path) || OfficeConverter::isOfficeFile(path);
}

bool PresentationService::isLegacyPowerPoint(const QString& path)
{
    const QString e = QFileInfo(path).suffix().toLower();
    return e == QLatin1String("ppt") || e == QLatin1String("pps");
}

QString PresentationService::supportedFilter()
{
    return QStringLiteral("Presentation Documents (*.pptx *.ppsx *.odp *.pdf);;PowerPoint Presentations (*.pptx *.ppsx);;"
                          "OpenDocument Presentations (*.odp);;PDF Documents (*.pdf);;Legacy PowerPoint (*.ppt *.pps);;All Files (*.*)");
}

QString PresentationService::cacheDirectoryFor(const QString& filePath)
{
    return AppPaths::presentationCacheDirectoryFor(filePath);
}

void PresentationService::clearCacheFor(const QString& filePath)
{
    if (filePath.trimmed().isEmpty())
        return;
    const QString dir = cacheDirectoryFor(filePath);
    if (QDir(dir).exists())
        QDir(dir).removeRecursively();
}

void PresentationService::clearAllCache()
{
    const QString cacheRoot = AppPaths::presentationCacheDirectory();
    QDir rootDir(cacheRoot);
    if (!rootDir.exists())
        return;
    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            QDir(fi.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(fi.absoluteFilePath());
        }
    }
}

void PresentationService::pruneCache(const QList<QString>& activeSourceFilePaths)
{
    const QString cacheRoot = AppPaths::presentationCacheDirectory();
    QDir rootDir(cacheRoot);
    if (!rootDir.exists())
        return;

    QSet<QString> activeHashes;
    for (const QString& path : activeSourceFilePaths) {
        if (!path.trimmed().isEmpty()) {
            const QByteArray hash = QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toLower().toUtf8(), QCryptographicHash::Md5).toHex();
            activeHashes.insert(QString::fromLatin1(hash).toLower());
        }
    }

    const QFileInfoList subDirs = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : subDirs) {
        const QString name = dirInfo.fileName().toLower();
        if (name == QStringLiteral("_user_profile"))
            continue; // Keep LibreOffice installation profile
        if (!activeHashes.contains(name)) {
            QDir(dirInfo.absoluteFilePath()).removeRecursively();
        }
    }
}

const PresentationService::Document* PresentationService::document(const QString& filePath) const
{
    const auto it = m_docs.constFind(normalised(filePath));
    return it == m_docs.cend() ? nullptr : &it.value();
}

void PresentationService::setBusy()
{
    const bool b = isBusy();
    if (b != m_lastBusy) {
        m_lastBusy = b;
        emit busyChanged(b);
    }
}

void PresentationService::fail(const QString& filePath, const QString& msg)
{
    emit failed(filePath, msg);
}

void PresentationService::open(const QString& filePath)
{
    // Always answered asynchronously (even for cached documents) so callers have a single code path.
    QTimer::singleShot(0, this, [this, filePath] {
        const QFileInfo fi(filePath);
        if (filePath.trimmed().isEmpty() || !fi.exists()) {
            fail(filePath, QStringLiteral("The file was not found: %1").arg(QDir::toNativeSeparators(filePath)));
            return;
        }
        if (!isSupportedFile(filePath)) {
            fail(filePath, QStringLiteral("Format '.%1' is not supported. Please import PPTX, PPSX, ODP, or PDF.").arg(fi.suffix().toLower()));
            return;
        }
        if (const Document* d = document(filePath)) {
            // a document that changed on disk is re-opened
            const QFileInfo pdf(d->pdfPath);
            if (pdf.exists() && (!OfficeConverter::isOfficeFile(filePath) || pdf.lastModified() >= fi.lastModified())) {
                emit opened(*d);
                return;
            }
        }
        if (isPdf(filePath)) {
            finishOpen(filePath, filePath);
            return;
        }
        // Office document: reuse a fresh cached conversion, otherwise convert.
        const QString cached = QDir(cacheDirectoryFor(filePath)).filePath(fi.completeBaseName() + QStringLiteral(".pdf"));
        const QFileInfo cf(cached);
        if (cf.exists() && cf.size() > 0 && cf.lastModified() >= fi.lastModified()) {
            QString err;
            if (PageImageCache::instance().pageCount(cached, &err) > 0) {
                finishOpen(filePath, cached);
                return;
            }
        }
        m_queue.enqueue(filePath);
        setBusy();
        startNext();
    });
}

void PresentationService::startNext()
{
    if (m_converting || m_queue.isEmpty())
        return;
    const QString file = m_queue.dequeue();
    m_converting = true;
    setBusy();
    m_converter.setSofficeOverride(m_soffice);
    m_converter.setTimeoutMs(m_timeout);
    const QString outDir = cacheDirectoryFor(file);
    const QString profile = QDir(AppPaths::presentationCacheDirectory()).filePath(QStringLiteral("_user_profile"));
    auto* conn = new QMetaObject::Connection;
    *conn = connect(&m_converter, &OfficeConverter::finished, this, [this, file, conn](bool ok, const QString& pdf, const QString& err) {
        disconnect(*conn);
        delete conn;
        m_converting = false;
        if (ok) {
            PageImageCache::instance().invalidate(pdf);   // rewritten file
            finishOpen(file, pdf);
        } else {
            fail(file, err);
        }
        setBusy();
        startNext();
    });
    m_converter.convert(file, outDir, profile);
}

void PresentationService::finishOpen(const QString& filePath, const QString& pdfPath)
{
    QString err;
    const int pages = PageImageCache::instance().pageCount(pdfPath, &err);
    if (pages <= 0) {
        fail(filePath, err.isEmpty() ? QStringLiteral("The document contains no pages.") : err);
        return;
    }
    Document d;
    d.title = QFileInfo(filePath).completeBaseName();
    d.filePath = filePath;
    d.pdfPath = pdfPath;
    d.pageCount = pages;
    m_docs.insert(normalised(filePath), d);
    emit opened(d);
}

} // namespace Lux
