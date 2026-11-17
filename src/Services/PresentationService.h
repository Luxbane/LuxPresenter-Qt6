#pragma once

#include "Services/Presentation/OfficeConverter.h"

#include <QHash>
#include <QObject>
#include <QQueue>

namespace Lux {

// Opens presentation documents for the library, the panes and the projector (port of
// PresentationEngineService.LoadDocument): PDFs directly; PowerPoint/OpenDocument files by converting them to PDF
// with LibreOffice into the presentation cache (reused while newer than the source). Results are delivered
// asynchronously through opened()/failed(); a document is only reported once its PDF has been opened successfully.
class PresentationService : public QObject
{
    Q_OBJECT
public:
    struct Document
    {
        QString title;       // file name without extension
        QString filePath;    // the file the user imported
        QString pdfPath;     // PDF that is rendered (== filePath for PDFs)
        int pageCount = 0;
        bool isValid() const { return pageCount > 0 && !pdfPath.isEmpty(); }
    };

    explicit PresentationService(QObject* parent = nullptr);

    static bool isSupportedFile(const QString& path);                 // pdf + office extensions
    static QString supportedFilter();                                 // file dialog filter
    static bool isLegacyPowerPoint(const QString& path);              // .ppt / .pps
    static QString cacheDirectoryFor(const QString& filePath);        // <PresentationCache>/<md5 of lower-case path>
    static void pruneCache(const QList<QString>& activeSourceFilePaths);
    static void clearCacheFor(const QString& filePath);
    static void clearAllCache();

    void open(const QString& filePath);
    const Document* document(const QString& filePath) const;
    bool isBusy() const { return m_converting || !m_queue.isEmpty(); }
    void setSofficeOverride(const QString& exe) { m_soffice = exe; }  // tests
    void setConversionTimeoutMs(int ms) { m_timeout = ms; }

signals:
    void opened(const Lux::PresentationService::Document& doc);
    void failed(const QString& filePath, const QString& message);
    void busyChanged(bool busy);

private:
    void startNext();
    void finishOpen(const QString& filePath, const QString& pdfPath);
    void fail(const QString& filePath, const QString& msg);
    void setBusy();

    QHash<QString, Document> m_docs;      // by normalised original path
    QQueue<QString> m_queue;
    bool m_converting = false;
    bool m_lastBusy = false;
    QString m_soffice;
    int m_timeout = 180000;
    OfficeConverter m_converter;
};

} // namespace Lux

Q_DECLARE_METATYPE(Lux::PresentationService::Document)
