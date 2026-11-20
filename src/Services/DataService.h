#pragma once
#include <memory>

#include "Models/AppSettings.h"
#include "Models/BackgroundItem.h"
#include "Models/MediaItem.h"
#include "Models/PresentationDocModels.h"
#include "Models/Scripture.h"
#include "Models/Song.h"

#include <QList>
#include <QString>

namespace Lux {
class BibleLibrary;

// JSON persistence layer, ported from Services/DataService.cs (IDataService).
//
// Files: <Data>/settings.json, media.json, presentations.json, backgrounds.json,
// bibles.json and <Songs>/songs.json. Scripture search, chapter lookup and
// Bible import are added with the Scripture milestone.
//
// Unlike the reference, failures are not silently swallowed: write methods
// return false and lastError() describes the most recent problem. Scripture
// data is never fabricated: if bibles.json is missing or invalid the verse list
// stays empty.
class DataService
{
public:
    DataService();
    ~DataService();
    DataService(const DataService&) = delete;
    DataService& operator=(const DataService&) = delete;

    // Settings
    const AppSettings& settings() const { return m_settings; }
    bool saveSettings(const AppSettings& settings);

    // Songs (sorted by title, case-insensitive)
    QList<Song> songs() const;
    bool saveSong(const Song& song);
    bool deleteSong(const QString& songId);
    bool addSongs(const QList<Song>& songs, bool replaceAll = false);
    bool applyGlobalFontToAllSongs(const QString& fontFamily, int fontSize, bool isBold);
    bool switchSongsDirectory(const QString& newDirectory);
    // Adds the bundled public-domain sample hymns (user-triggered only, never automatic).
    // Returns the number of sample songs processed, or -1 on failure.
    int loadSampleSongs();
    bool songById(const QString& id, Song* out = nullptr) const;

    // Media
    const QList<MediaItem>& mediaItems() const { return m_media; }
    bool addMediaItem(const MediaItem& media);
    bool deleteMediaItem(const QString& mediaId);

    // Presentation library (newest first)
    QList<PresentationDocItem> presentationDocs() const;
    bool addPresentationDoc(const PresentationDocItem& doc);
    bool deletePresentationDoc(const QString& docId);

    // Backgrounds
    const QList<BackgroundItem>& backgrounds() const { return m_backgrounds; }
    bool addBackground(const BackgroundItem& item);
    bool addBackgrounds(const QList<BackgroundItem>& items);
    bool deleteBackground(const QString& id);
    bool saveBackgrounds(const QList<BackgroundItem>& items);

    // Scripture data
    const QList<BibleVerse>& bibleVerses() const { return m_verses; }
    // The installed Bible translations (bundled KJV + imports in <Data>/Bibles).
    BibleLibrary& bibles();
    bool hasVersion(const QString& version) const;
    int verseCount(const QString& version) const;

    QString dataDirectory() const { return m_dataDir; }
    QString songsFilePath() const { return m_songsFile; }
    QString lastError() const { return m_lastError; }

    // Prunes missing items from media.json, backgrounds.json, and presentations.json,
    // and deletes orphaned thumbnails and presentation cache directories on disk.
    void cleanupOrphanedData();

private:
    void loadSettings();
    void loadSongs();
    void loadMedia();
    void loadPresentationDocs();
    void loadBackgrounds();
    void loadBibles();

    bool saveSongsFile();
    bool saveMediaFile();
    bool savePresentationDocsFile();
    bool saveBackgroundsFile();
    bool saveSettingsFile();
    bool fail(const QString& error);

    QString m_dataDir;
    QString m_settingsFile;
    QString m_mediaFile;
    QString m_presentationsFile;
    QString m_backgroundsFile;
    QString m_biblesFile;
    QString m_songsFile;
    QString m_lastError;

    AppSettings m_settings;
    QList<Song> m_songs;
    QList<MediaItem> m_media;
    QList<PresentationDocItem> m_presentationDocs;
    QList<BackgroundItem> m_backgrounds;
    QList<BibleVerse> m_verses;
    std::unique_ptr<BibleLibrary> m_bibleLibrary;
};

} // namespace Lux
