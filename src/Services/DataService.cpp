#include "Services/DataService.h"
#include "Services/BibleLibrary.h"

#include "Services/AppPaths.h"
#include "Services/JsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace Lux {
DataService::~DataService() = default;
BibleLibrary& DataService::bibles()
{
    if (!m_bibleLibrary)
        m_bibleLibrary = std::make_unique<BibleLibrary>(m_verses, QDir(m_dataDir).filePath(QStringLiteral("Bibles")));
    return *m_bibleLibrary;
}

namespace {

template <class T>
QJsonDocument toDoc(const QList<T>& list)
{
    QJsonArray a;
    for (const T& t : list)
        a.append(t.toJson());
    return QJsonDocument(a);
}

// Reads a JSON array of objects. Returns false on parse failure (list untouched).
template <class T>
bool readList(const QString& path, QList<T>& out, QString* error)
{
    QJsonDocument doc;
    if (!JsonFile::read(path, doc, error))
        return false;
    if (!doc.isArray()) {
        if (error)
            *error = QStringLiteral("%1 does not contain a JSON array.").arg(path);
        return false;
    }
    QList<T> result;
    const QJsonArray arr = doc.array();
    result.reserve(arr.size());
    for (const QJsonValue& v : arr)
        if (v.isObject())
            result.append(T::fromJson(v.toObject()));
    out = std::move(result);
    return true;
}

} // namespace

DataService::DataService()
{
    m_dataDir = AppPaths::dataDirectory();
    QDir().mkpath(m_dataDir);

    const QDir d(m_dataDir);
    m_mediaFile = d.filePath(QStringLiteral("media.json"));
    m_presentationsFile = d.filePath(QStringLiteral("presentations.json"));
    m_biblesFile = d.filePath(QStringLiteral("bibles.json"));
    m_backgroundsFile = d.filePath(QStringLiteral("backgrounds.json"));
    m_settingsFile = d.filePath(QStringLiteral("settings.json"));

    loadSettings();

    if (!m_settings.customSongDatabaseDirectory.trimmed().isEmpty()
        && QDir(m_settings.customSongDatabaseDirectory).exists())
        AppPaths::setSongsDirectory(m_settings.customSongDatabaseDirectory);

    m_songsFile = QDir(AppPaths::songsDirectory()).filePath(QStringLiteral("songs.json"));

    loadSongs();
    loadMedia();
    loadPresentationDocs();
    loadBackgrounds();
    loadBibles();
}

bool DataService::fail(const QString& error)
{
    m_lastError = error;
    return false;
}

// ---- Settings ---------------------------------------------------------------

void DataService::loadSettings()
{
    QJsonDocument doc;
    QString err;
    if (QFile::exists(m_settingsFile)) {
        if (JsonFile::read(m_settingsFile, doc, &err) && doc.isObject()) {
            m_settings = AppSettings::fromJson(doc.object());
            const QString bg = m_settings.lastUsedBackground;
            if (!bg.trimmed().isEmpty() && !bg.startsWith(QLatin1Char('#')) && !QFile::exists(bg)) {
                m_settings.lastUsedBackground.clear();
                saveSettingsFile();
            }
        } else {
            m_lastError = err;
            m_settings = AppSettings();
        }
    } else {
        m_settings = AppSettings();
        saveSettingsFile();
    }
}

bool DataService::saveSettings(const AppSettings& settings)
{
    m_settings = settings;
    return saveSettingsFile();
}

bool DataService::saveSettingsFile()
{
    QString err;
    if (!JsonFile::write(m_settingsFile, QJsonDocument(m_settings.toJson()), &err))
        return fail(err);
    return true;
}

// ---- Songs ------------------------------------------------------------------

void DataService::loadSongs()
{
    if (QFile::exists(m_songsFile)) {
        QString err;
        if (readList(m_songsFile, m_songs, &err)) {
            bool updated = false;
            for (Song& s : m_songs) {
                if (s.fontSize <= 0 || s.fontSize == 10) {
                    s.fontSize = 13;
                    updated = true;
                }
            }
            if (updated)
                saveSongsFile();
        } else {
            m_lastError = err;
            m_songs.clear();
        }
        return;
    }

    // Legacy location: <Data>/songs.json
    const QString legacy = QDir(m_dataDir).filePath(QStringLiteral("songs.json"));
    if (QFile::exists(legacy)) {
        QList<Song> old;
        if (readList(legacy, old, nullptr) && !old.isEmpty()) {
            m_songs = old;
            saveSongsFile();
            return;
        }
    }
    // Clean start: the song list is empty so users bring their own database.
    m_songs.clear();
    saveSongsFile();
}

QList<Song> DataService::songs() const
{
    QList<Song> sorted = m_songs;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Song& a, const Song& b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });
    return sorted;
}

bool DataService::saveSong(const Song& song)
{
    auto it = std::find_if(m_songs.begin(), m_songs.end(), [&](const Song& s) { return s.id == song.id; });
    if (it != m_songs.end())
        *it = song;
    else
        m_songs.append(song);
    return saveSongsFile();
}

bool DataService::deleteSong(const QString& songId)
{
    m_songs.removeIf([&](const Song& s) { return s.id == songId; });
    return saveSongsFile();
}

bool DataService::addSongs(const QList<Song>& songs, bool replaceAll)
{
    if (replaceAll)
        m_songs.clear();
    for (const Song& song : songs) {
        auto it = std::find_if(m_songs.begin(), m_songs.end(), [&](const Song& s) {
            return s.title.compare(song.title, Qt::CaseInsensitive) == 0;
        });
        if (it != m_songs.end()) {
            it->lyricsRaw = song.lyricsRaw;
            it->author = song.author;
            it->copyright = song.copyright;
        } else {
            m_songs.append(song);
        }
    }
    return saveSongsFile();
}

bool DataService::applyGlobalFontToAllSongs(const QString& fontFamily, int fontSize, bool isBold)
{
    Q_UNUSED(isBold); // the reference accepts but ignores this parameter
    for (Song& s : m_songs) {
        if (fontSize > 0)
            s.fontSize = fontSize;
        if (!fontFamily.trimmed().isEmpty())
            s.fontFamily = fontFamily;
    }
    return saveSongsFile();
}

int DataService::loadSampleSongs()
{
    QJsonDocument doc;
    QString err;
    QFile f(QStringLiteral(":/sample_songs.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Sample songs resource is missing."));
        return -1;
    }
    doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        fail(QStringLiteral("Sample songs resource is invalid."));
        return -1;
    }
    QList<Song> samples;
    for (const QJsonValue& v : doc.array())
        if (v.isObject())
            samples.append(Song::fromJson(v.toObject()));
    if (!addSongs(samples, /*replaceAll=*/false))
        return -1;
    return static_cast<int>(samples.size());
}

bool DataService::songById(const QString& id, Song* out) const
{
    for (const Song& s : m_songs) {
        if (s.id == id) {
            if (out)
                *out = s;
            return true;
        }
    }
    return false;
}

bool DataService::switchSongsDirectory(const QString& newDirectory)
{
    AppPaths::setSongsDirectory(newDirectory);
    m_songsFile = QDir(AppPaths::songsDirectory()).filePath(QStringLiteral("songs.json"));
    m_settings.customSongDatabaseDirectory = newDirectory;
    saveSettingsFile();

    if (QFile::exists(m_songsFile)) {
        QString err;
        if (!readList(m_songsFile, m_songs, &err)) {
            m_songs.clear();
            return fail(err);
        }
        return true;
    }
    return saveSongsFile(); // keep the current songs in the new location
}

bool DataService::saveSongsFile()
{
    QString err;
    if (!JsonFile::write(m_songsFile, toDoc(m_songs), &err))
        return fail(err);
    return true;
}

// ---- Media ------------------------------------------------------------------

void DataService::loadMedia()
{
    if (!QFile::exists(m_mediaFile)) {
        m_media.clear();
        saveMediaFile();
        return;
    }
    QList<MediaItem> loaded;
    QString err;
    if (!readList(m_mediaFile, loaded, &err)) {
        m_lastError = err;
        m_media.clear();
        return;
    }
    m_media.clear();
    for (const MediaItem& m : std::as_const(loaded))
        if (!m.filePath.trimmed().isEmpty() && QFile::exists(m.filePath))
            m_media.append(m);
    if (m_media.size() != loaded.size())
        saveMediaFile();
}

bool DataService::addMediaItem(const MediaItem& media)
{
    m_media.append(media);
    return saveMediaFile();
}

bool DataService::deleteMediaItem(const QString& mediaId)
{
    m_media.removeIf([&](const MediaItem& m) { return m.id == mediaId; });
    return saveMediaFile();
}

bool DataService::saveMediaFile()
{
    QString err;
    if (!JsonFile::write(m_mediaFile, toDoc(m_media), &err))
        return fail(err);
    return true;
}

// ---- Presentation library ---------------------------------------------------

void DataService::loadPresentationDocs()
{
    if (!QFile::exists(m_presentationsFile))
        return;
    QList<PresentationDocItem> loaded;
    QString err;
    if (!readList(m_presentationsFile, loaded, &err)) {
        m_lastError = err;
        m_presentationDocs.clear();
        return;
    }
    m_presentationDocs.clear();
    for (const PresentationDocItem& p : std::as_const(loaded)) {
        if (!p.filePath.trimmed().isEmpty() && (QFile::exists(p.filePath) || QDir(AppPaths::presentationCacheDirectoryFor(p.filePath)).exists()))
            m_presentationDocs.append(p);
    }
    if (m_presentationDocs.size() != loaded.size())
        savePresentationDocsFile();
}

QList<PresentationDocItem> DataService::presentationDocs() const
{
    QList<PresentationDocItem> sorted = m_presentationDocs;
    std::stable_sort(sorted.begin(), sorted.end(), [](const PresentationDocItem& a, const PresentationDocItem& b) {
        return a.dateAdded > b.dateAdded;
    });
    return sorted;
}

bool DataService::addPresentationDoc(const PresentationDocItem& doc)
{
    m_presentationDocs.removeIf([&](const PresentationDocItem& p) {
        return p.filePath.compare(doc.filePath, Qt::CaseInsensitive) == 0;
    });
    m_presentationDocs.prepend(doc);
    return savePresentationDocsFile();
}

bool DataService::deletePresentationDoc(const QString& docId)
{
    m_presentationDocs.removeIf([&](const PresentationDocItem& p) { return p.id == docId; });
    return savePresentationDocsFile();
}

bool DataService::savePresentationDocsFile()
{
    QString err;
    if (!JsonFile::write(m_presentationsFile, toDoc(m_presentationDocs), &err))
        return fail(err);
    return true;
}

// ---- Backgrounds ------------------------------------------------------------

void DataService::loadBackgrounds()
{
    if (!QFile::exists(m_backgroundsFile)) {
        m_backgrounds.clear();
        saveBackgroundsFile();
        return;
    }
    QList<BackgroundItem> loaded;
    QString err;
    if (!readList(m_backgroundsFile, loaded, &err)) {
        m_lastError = err;
        m_backgrounds.clear();
        return;
    }
    m_backgrounds.clear();
    for (const BackgroundItem& b : std::as_const(loaded))
        if (b.filePath.trimmed().isEmpty() || b.filePath.startsWith(QLatin1Char('#')) || QFile::exists(b.filePath))
            m_backgrounds.append(b);
    if (m_backgrounds.size() != loaded.size())
        saveBackgroundsFile();
}

bool DataService::addBackground(const BackgroundItem& item)
{
    m_backgrounds.append(item);
    return saveBackgroundsFile();
}

bool DataService::addBackgrounds(const QList<BackgroundItem>& items)
{
    m_backgrounds.append(items);
    return saveBackgroundsFile();
}

bool DataService::deleteBackground(const QString& id)
{
    m_backgrounds.removeIf([&](const BackgroundItem& b) { return b.id == id; });
    return saveBackgroundsFile();
}

bool DataService::saveBackgrounds(const QList<BackgroundItem>& items)
{
    m_backgrounds = items;
    return saveBackgroundsFile();
}

bool DataService::saveBackgroundsFile()
{
    QString err;
    if (!JsonFile::write(m_backgroundsFile, toDoc(m_backgrounds), &err))
        return fail(err);
    return true;
}

// ---- Scripture --------------------------------------------------------------

void DataService::loadBibles()
{
    // Seed the user-data copy from the bundled <app>/Data/bibles.json when it is
    // missing or implausibly small (same rule as the reference).
    const QString bundled = QDir(AppPaths::baseDirectory()).filePath(QStringLiteral("Data/bibles.json"));
    constexpr qint64 kMinPlausibleSize = 1'000'000;
    const bool sameFile = QFileInfo(bundled).canonicalFilePath() == QFileInfo(m_biblesFile).canonicalFilePath()
                          && QFileInfo(m_biblesFile).exists();
    if (!sameFile && QFile::exists(bundled)) {
        const bool missing = !QFile::exists(m_biblesFile);
        const bool tooSmall = !missing && QFileInfo(m_biblesFile).size() < kMinPlausibleSize
                              && QFileInfo(bundled).size() > kMinPlausibleSize;
        if (missing || tooSmall) {
            QFile::remove(m_biblesFile);
            QFile::copy(bundled, m_biblesFile);
        }
    }

    m_verses.clear();
    if (!QFile::exists(m_biblesFile))
        return; // never fabricate Scripture text

    QString err;
    if (!readList(m_biblesFile, m_verses, &err)) {
        m_lastError = err;
        m_verses.clear();
    }
}

bool DataService::hasVersion(const QString& version) const
{
    return std::any_of(m_verses.cbegin(), m_verses.cend(), [&](const BibleVerse& v) {
        return v.version.compare(version, Qt::CaseInsensitive) == 0;
    });
}

int DataService::verseCount(const QString& version) const
{
    return static_cast<int>(std::count_if(m_verses.cbegin(), m_verses.cend(), [&](const BibleVerse& v) {
        return v.version.compare(version, Qt::CaseInsensitive) == 0;
    }));
}

} // namespace Lux
