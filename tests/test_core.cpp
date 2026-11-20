#include "Models/ScheduleFileData.h"
#include "Models/Song.h"
#include "Services/AppPaths.h"
#include "Services/DataService.h"
#include "Services/JsonFile.h"
#include "Services/LyricsFormatter.h"
#include "Services/AlertService.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

using namespace Lux;

namespace {

// Contents of the settings.json shipped with the reference application.
const char kReferenceSettingsJson[] = R"({
  "LastUsedBackground": "",
  "IsVideoLooping": true,
  "ActiveLibraryTabIndex": 0,
  "SongTextAlignment": "Center",
  "SongVerticalAlignment": "Top",
  "SongFontSize": 13,
  "SongFontHeightPercent": 14,
  "SongFontFamily": "Tahoma",
  "SongFontBold": true,
  "OverrideSongFontGlobally": false,
  "ShowVerseLabels": false,
  "LabelLocation": "Top Center",
  "LabelSize": "Normal",
  "IndentLabel": false,
  "AutoWrapWords": true,
  "CapitalizeAllWords": true,
  "CapitalizeFirstCharEachLine": false,
  "AutoCapitalizeKeywords": false,
  "CapitalizeKeywords": "CHRIST\r\nFATHER\r\nGOD\r\nHE\r\nHIS\r\nHIM\r\nHIMSELF",
  "SongTransition": "Blend",
  "SlideTransition": "Blend",
  "MediaTransition": "Blend",
  "ScreenTransition": "Blend",
  "TransitionDurationMs": 500,
  "VideoBackgroundEngine": "libmpv"
})";

AppSettings plainSettings()
{
    AppSettings s;
    s.capitalizeAllWords = false;
    return s;
}

} // namespace

class CoreTests : public QObject
{
    Q_OBJECT

private:
    // Fresh isolated "application directory" inside the build tree (never outside the workspace).
    struct Sandbox
    {
        Sandbox()
        {
            QDir().mkpath(QStringLiteral(LUX_TEST_TMP));
            dir = std::make_unique<QTemporaryDir>(QStringLiteral(LUX_TEST_TMP "/case-XXXXXX"));
            AppPaths::setBaseDirectoryOverride(dir->path());
        }
        ~Sandbox()
        {
            AppPaths::setForceInstalledModeForTesting(false);
            AppPaths::setBaseDirectoryOverride(QString());
        }
        QString path(const QString& rel = QString()) const { return QDir(dir->path()).filePath(rel); }
        std::unique_ptr<QTemporaryDir> dir;
    };

    static void installKjv(const Sandbox& sb)
    {
        QDir().mkpath(sb.path(QStringLiteral("Data")));
        QVERIFY(QFile::copy(QStringLiteral(LUX_DATA_SRC "/bibles.json"), sb.path(QStringLiteral("Data/bibles.json"))));
    }

private slots:
    void settingsDefaultsMatchReferenceFile()
    {
        QJsonDocument doc = QJsonDocument::fromJson(kReferenceSettingsJson);
        QVERIFY(doc.isObject());
        const AppSettings fromRef = AppSettings::fromJson(doc.object());
        QCOMPARE(fromRef.toJson(), AppSettings().toJson());
        QCOMPARE(AppSettings().songFontFamily, QStringLiteral("Tahoma"));
        QCOMPARE(AppSettings().transitionDurationMs, 500);
        QVERIFY(AppSettings().customSongDatabaseDirectory.isNull());
    }

    void settingsRoundTrip()
    {
        AppSettings s;
        s.songFontSize = 40;
        s.isDarkMode = true;
        s.customSongDatabaseDirectory = QStringLiteral("D:/Songs");
        s.presetAlertMessages = {QStringLiteral("Please silence phones"), QStringLiteral("Welcome")};
        s.songTransition = QStringLiteral("Cut");
        const AppSettings r = AppSettings::fromJson(s.toJson());
        QCOMPARE(r.toJson(), s.toJson());
        QCOMPARE(r.presetAlertMessages.size(), 2);
        QVERIFY(r.isDarkMode);
    }

    void slideJsonRoundTripAndClone()
    {
        Slide s;
        s.label = QStringLiteral("Chorus");
        s.type = SlideType::Chorus;
        s.customBackgroundPath = QStringLiteral("#001833");
        s.containerMargin = {1, 1, 1, 20};
        const Slide r = Slide::fromJson(s.toJson());
        QCOMPARE(r.id, s.id);
        QCOMPARE(r.type, SlideType::Chorus);
        QCOMPARE(r.containerMargin, s.containerMargin);
        QCOMPARE(r.customBackgroundPath, s.customBackgroundPath);

        const Slide c = s.clone();
        QVERIFY(c.id != s.id);
        QCOMPARE(c.label, s.label);

        // Unknown enum values fall back to the default instead of corrupting data.
        QJsonObject bad = s.toJson();
        bad[QStringLiteral("Type")] = 99;
        QCOMPARE(Slide::fromJson(bad).type, SlideType::Verse);
    }

    void lyricsFormatter()
    {
        AppSettings s = plainSettings();
        QCOMPARE(LyricsFormatter::format(QString(), &s), QString());
        QCOMPARE(LyricsFormatter::format(QStringLiteral("abc"), nullptr), QStringLiteral("abc"));
        QCOMPARE(LyricsFormatter::format(QStringLiteral("abc"), &s), QStringLiteral("abc"));

        s.capitalizeAllWords = true;
        QCOMPARE(LyricsFormatter::format(QStringLiteral("hello"), &s), QStringLiteral("HELLO"));

        s.capitalizeAllWords = false;
        s.capitalizeFirstCharEachLine = true;
        QCOMPARE(LyricsFormatter::format(QStringLiteral("\"one\nthree"), &s), QStringLiteral("\"One\nThree"));

        s.capitalizeFirstCharEachLine = false;
        s.autoCapitalizeKeywords = true;
        s.capitalizeKeywords = QStringLiteral("god\r\nhis");
        QCOMPARE(LyricsFormatter::format(QStringLiteral("praise god, his name; godly"), &s),
                 QStringLiteral("praise God, His name; godly"));
    }

    void songParsingLabelsAndStanzaMargins()
    {
        Song song;
        song.title = QStringLiteral("Test Song");
        song.author = QStringLiteral("Someone");
        song.lyricsRaw = QStringLiteral(
            "[Verse 1]\nLine A\nLine B\n\nLine C\nLine D\n\n\n[Chorus]\nChorus line\n\nBridge:\nBridge line");
        const AppSettings st = plainSettings();
        const PresentationItem p = song.toPresentationItem(&st);

        QCOMPARE(p.title, QStringLiteral("Test Song"));
        QCOMPARE(p.subtitle, QStringLiteral("By Someone"));
        QCOMPARE(p.category, ItemCategory::Song);
        QCOMPARE(p.slides.size(), 4);

        QCOMPARE(p.slides[0].label, QStringLiteral("Verse 1"));
        QCOMPARE(p.slides[0].content, QStringLiteral("Line A\nLine B"));
        QCOMPARE(p.slides[0].type, SlideType::Verse);
        QCOMPARE(p.slides[0].containerMargin.bottom, 2.0); // plain break

        QCOMPARE(p.slides[1].label, QString(""));
        QCOMPARE(p.slides[1].content, QStringLiteral("Line C\nLine D"));
        QCOMPARE(p.slides[1].containerMargin.bottom, 20.0); // 3+ newlines => stanza end

        QCOMPARE(p.slides[2].label, QStringLiteral("Chorus"));
        QCOMPARE(p.slides[2].type, SlideType::Chorus);
        QCOMPARE(p.slides[2].containerMargin.bottom, 20.0); // next block is a header

        QCOMPARE(p.slides[3].label, QStringLiteral("Bridge"));
        QCOMPARE(p.slides[3].type, SlideType::Bridge);
        QCOMPARE(p.slides[3].content, QStringLiteral("Bridge line"));
        QCOMPARE(p.slides[3].fontFamily, QStringLiteral("Tahoma"));
    }

    void songWithoutLyricsGetsTitleSlide()
    {
        Song song;
        song.title = QStringLiteral("Only Title");
        const AppSettings st = plainSettings();
        const PresentationItem p = song.toPresentationItem(&st);
        QCOMPARE(p.slides.size(), 1);
        QCOMPARE(p.slides[0].label, QStringLiteral("Slide 1"));
        QCOMPARE(p.slides[0].content, QStringLiteral("Only Title"));
        QCOMPARE(p.subtitle, QString(""));
    }

    void songJsonRoundTrip()
    {
        Song s;
        s.title = QStringLiteral("Judul Lagu \u2014 \u00c9");
        s.lyricsRaw = QStringLiteral("a\r\nb");
        s.defaultBackground = QStringLiteral("#0B132B");
        s.isFavorite = true;
        const Song r = Song::fromJson(s.toJson());
        QCOMPARE(r.toJson(), s.toJson());
        QCOMPARE(r.title, s.title);
    }

    void scheduleFileRoundTrip()
    {
        Sandbox sb;
        Song song;
        song.title = QStringLiteral("S");
        song.lyricsRaw = QStringLiteral("[Verse 1]\nx\ny\n\n\n[Chorus]\nz");
        const AppSettings st = plainSettings();
        QList<ScheduleItem> items{ScheduleItem::fromPresentation(song.toPresentationItem(&st))};
        QCOMPARE(items[0].typeLabel, QStringLiteral("Song"));

        const QString file = sb.path(QStringLiteral("test.schedule.json"));
        QString err;
        QVERIFY2(ScheduleFile::save(file, items, &err), qPrintable(err));

        QList<ScheduleItem> loaded;
        QVERIFY2(ScheduleFile::load(file, loaded, &err), qPrintable(err));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].id, items[0].id);
        QCOMPARE(loaded[0].presentation.slides.size(), items[0].presentation.slides.size());
        QCOMPARE(loaded[0].presentation.slides[0].content, items[0].presentation.slides[0].content);
        // ContainerMargin is not part of the schedule file: loads as the default.
        QCOMPARE(loaded[0].presentation.slides[0].containerMargin, (Thickness{1, 1, 1, 2}));

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("Version")).toString(), QStringLiteral("1.0"));
        QVERIFY(root.contains(QStringLiteral("SavedAt")));
    }

    void jsonFileHandlesBomAndErrors()
    {
        Sandbox sb;
        const QString p = sb.path(QStringLiteral("bom.json"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\xEF\xBB\xBF{\"a\":1}");
        f.close();
        QJsonDocument doc;
        QString err;
        QVERIFY2(JsonFile::read(p, doc, &err), qPrintable(err));
        QCOMPARE(doc.object().value(QStringLiteral("a")).toInt(), 1);

        QVERIFY(!JsonFile::read(sb.path(QStringLiteral("missing.json")), doc, &err));
        QVERIFY(!err.isEmpty());

        QFile g(p);
        QVERIFY(g.open(QIODevice::WriteOnly | QIODevice::Truncate));
        g.write("{not json");
        g.close();
        err.clear();
        QVERIFY(!JsonFile::read(p, doc, &err));
        QVERIFY(err.contains(QStringLiteral("Invalid JSON")));
    }

    void appPathsPortableMode()
    {
        Sandbox sb;
        const QString base = AppPaths::baseDirectory();
        QCOMPARE(AppPaths::dataDirectory(), QDir::cleanPath(base + QStringLiteral("/Data")));
        QCOMPARE(AppPaths::songsDirectory(), QDir::cleanPath(base + QStringLiteral("/Database")));
        QVERIFY(QDir(AppPaths::dataDirectory()).exists());
        QVERIFY(QDir(AppPaths::songsDirectory()).exists());
        QCOMPARE(AppPaths::mediaDirectory(), AppPaths::dataDirectory() + QStringLiteral("/Media"));
        QCOMPARE(AppPaths::thumbnailCacheDirectory(), AppPaths::dataDirectory() + QStringLiteral("/Thumbnails"));
        QVERIFY(QDir(AppPaths::presentationCacheDirectory()).exists());

        // No drive-specific fallback: result is either a local engine path or a Program Files path.
        const QString lo = AppPaths::resolveLibreOfficeDirectory();
        QVERIFY(!lo.isEmpty());
        QVERIFY(!lo.startsWith(QStringLiteral("E:"), Qt::CaseInsensitive)
                || lo.startsWith(QDir::cleanPath(base), Qt::CaseInsensitive));

        const QString custom = sb.path(QStringLiteral("MySongs"));
        AppPaths::setSongsDirectory(custom);
        QCOMPARE(AppPaths::songsDirectory(), QDir::cleanPath(custom));
        QVERIFY(QDir(custom).exists());
    }

    void appPathsInstalledModeUsesSeparateIdentity()
    {
        Sandbox sb;
        // Redirect the user-profile locations into the sandbox so the real
        // %APPDATA% / Documents (and the C# app's data) are never touched.
        const QByteArray oldAppData = qgetenv("APPDATA");
        const QByteArray oldProfile = qgetenv("USERPROFILE");
        const QString fakeRoaming = sb.path(QStringLiteral("profile/AppData/Roaming"));
        const QString fakeProfile = sb.path(QStringLiteral("profile"));
        qputenv("APPDATA", QDir::toNativeSeparators(fakeRoaming).toUtf8());
        qputenv("USERPROFILE", QDir::toNativeSeparators(fakeProfile).toUtf8());
        auto restore = qScopeGuard([&] {
            qputenv("APPDATA", oldAppData);
            qputenv("USERPROFILE", oldProfile);
        });

        QCOMPARE(AppPaths::installedDataDirectory(), QDir::cleanPath(fakeRoaming + QStringLiteral("/LuxPresenterQt6/Data")));
        QCOMPARE(AppPaths::installedSongsDirectory(),
                 QDir::cleanPath(fakeProfile + QStringLiteral("/Documents/LuxPresenterQt6/Database")));
        QVERIFY(!QDir(fakeRoaming).exists()); // computing the path creates nothing

        // Bundled data next to the executable gets seeded into the installed-mode folder.
        QDir().mkpath(sb.path(QStringLiteral("Data")));
        QFile marker(sb.path(QStringLiteral("Data/marker.txt")));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("seed");
        marker.close();

        AppPaths::setForceInstalledModeForTesting(true);
        QCOMPARE(AppPaths::dataDirectory(), AppPaths::installedDataDirectory());
        QCOMPARE(AppPaths::songsDirectory(), AppPaths::installedSongsDirectory());
        QVERIFY(QFile::exists(AppPaths::dataDirectory() + QStringLiteral("/marker.txt")));
        QVERIFY(!QFile::exists(sb.path(QStringLiteral("Database")))); // portable folder not used

        // Identity is "LuxPresenterQt6" - never the C# app's "LuxPresenter" folders.
        QVERIFY(AppPaths::dataDirectory().contains(QStringLiteral("/LuxPresenterQt6/Data")));
        QVERIFY(!AppPaths::dataDirectory().contains(QStringLiteral("/LuxPresenter/")));
        QVERIFY(!AppPaths::songsDirectory().contains(QStringLiteral("/LuxPresenter/")));

        DataService ds;
        QVERIFY2(ds.lastError().isEmpty(), qPrintable(ds.lastError()));
        QVERIFY(QFile::exists(AppPaths::dataDirectory() + QStringLiteral("/settings.json")));
        QVERIFY(QFile::exists(AppPaths::songsDirectory() + QStringLiteral("/songs.json")));
        QVERIFY(!QFile::exists(sb.path(QStringLiteral("Data/settings.json")))); // nothing written next to the exe
    }

    void dataServiceCreatesDefaultsAndPersists()
    {
        Sandbox sb;
        {
            DataService ds;
            QVERIFY2(ds.lastError().isEmpty(), qPrintable(ds.lastError()));
            QVERIFY(QFile::exists(sb.path(QStringLiteral("Data/settings.json"))));
            QVERIFY(QFile::exists(sb.path(QStringLiteral("Data/media.json"))));
            QVERIFY(QFile::exists(sb.path(QStringLiteral("Data/backgrounds.json"))));
            QVERIFY(QFile::exists(sb.path(QStringLiteral("Database/songs.json"))));
            QCOMPARE(ds.songs().size(), 0);
            QCOMPARE(ds.verseCount(QStringLiteral("KJV")), 0); // nothing fabricated without a dataset
            QVERIFY(!ds.hasVersion(QStringLiteral("TB")));

            Song b; b.title = QStringLiteral("beta");  b.lyricsRaw = QStringLiteral("b");
            Song a; a.title = QStringLiteral("Alpha"); a.lyricsRaw = QStringLiteral("a");
            QVERIFY(ds.saveSong(b));
            QVERIFY(ds.saveSong(a));
            QCOMPARE(ds.songs().at(0).title, QStringLiteral("Alpha")); // case-insensitive order

            // Same title: lyrics/author/copyright are updated in place, no duplicate.
            Song a2; a2.title = QStringLiteral("alpha"); a2.lyricsRaw = QStringLiteral("a2"); a2.author = QStringLiteral("X");
            QVERIFY(ds.addSongs({a2}));
            QCOMPARE(ds.songs().size(), 2);
            QCOMPARE(ds.songs().at(0).lyricsRaw, QStringLiteral("a2"));

            QVERIFY(ds.applyGlobalFontToAllSongs(QStringLiteral("Arial"), 30, true));

            AppSettings st = ds.settings();
            st.songFontSize = 44;
            st.presetAlertMessages << QStringLiteral("Hello");
            QVERIFY(ds.saveSettings(st));

            BackgroundItem colour;
            colour.name = QStringLiteral("Deep Blue Majesty");
            colour.type = BackgroundType::Color;
            colour.hexColor = QStringLiteral("#001833");
            colour.filePath = QStringLiteral("#001833");
            QVERIFY(ds.addBackground(colour));
            BackgroundItem missing; // points at a file that does not exist: dropped at next load
            missing.name = QStringLiteral("gone");
            missing.filePath = sb.path(QStringLiteral("does-not-exist.png"));
            QVERIFY(ds.addBackground(missing));

            PresentationDocItem pd1; pd1.name = QStringLiteral("old"); pd1.filePath = sb.path(QStringLiteral("Data/settings.json"));
            pd1.dateAdded = QDateTime(QDate(2026, 1, 1), QTime(8, 0));
            PresentationDocItem pd2; pd2.name = QStringLiteral("new"); pd2.filePath = sb.path(QStringLiteral("Data/media.json"));
            pd2.dateAdded = QDateTime(QDate(2026, 6, 1), QTime(8, 0));
            QVERIFY(ds.addPresentationDoc(pd1));
            QVERIFY(ds.addPresentationDoc(pd2));
            QCOMPARE(ds.presentationDocs().at(0).name, QStringLiteral("new"));
        }
        {
            DataService ds; // reload from disk
            QVERIFY2(ds.lastError().isEmpty(), qPrintable(ds.lastError()));
            QCOMPARE(ds.songs().size(), 2);
            QCOMPARE(ds.songs().at(0).fontFamily, QStringLiteral("Arial"));
            QCOMPARE(ds.songs().at(0).fontSize, 30);
            QCOMPARE(ds.settings().songFontSize, 44);
            QCOMPARE(ds.settings().presetAlertMessages, QStringList{QStringLiteral("Hello")});
            QCOMPARE(ds.backgrounds().size(), 1);
            QCOMPARE(ds.backgrounds().at(0).hexColor, QStringLiteral("#001833"));
            QCOMPARE(ds.presentationDocs().size(), 2);
            QCOMPARE(ds.presentationDocs().at(0).name, QStringLiteral("new"));
            QVERIFY(ds.deleteSong(ds.songs().at(0).id));
            QCOMPARE(ds.songs().size(), 1);
        }
    }

    void dataServiceSwitchSongsDirectory()
    {
        Sandbox sb;
        DataService ds;
        Song s; s.title = QStringLiteral("Keep"); s.lyricsRaw = QStringLiteral("x");
        QVERIFY(ds.saveSong(s));
        const QString other = sb.path(QStringLiteral("OtherSongs"));
        QVERIFY(ds.switchSongsDirectory(other));
        QVERIFY(QFile::exists(other + QStringLiteral("/songs.json")));
        QCOMPARE(ds.songs().size(), 1);
        QCOMPARE(ds.settings().customSongDatabaseDirectory, other);

        DataService again; // the custom directory is picked up from settings.json
        QCOMPARE(QDir::cleanPath(again.songsFilePath()), QDir::cleanPath(other + QStringLiteral("/songs.json")));
        QCOMPARE(again.songs().size(), 1);
    }

    void dataServiceRecoversFromCorruptFiles()
    {
        Sandbox sb;
        QDir().mkpath(sb.path(QStringLiteral("Data")));
        QFile f(sb.path(QStringLiteral("Data/settings.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ broken");
        f.close();
        DataService ds;
        QVERIFY(!ds.lastError().isEmpty());                       // reported, not hidden
        QCOMPARE(ds.settings().toJson(), AppSettings().toJson()); // defaults used
    }

    void bundledKjvDataset()
    {
        Sandbox sb;
        installKjv(sb);
        DataService ds;
        QVERIFY2(ds.lastError().isEmpty(), qPrintable(ds.lastError()));
        QCOMPARE(ds.bibleVerses().size(), 31102);
        QCOMPARE(ds.verseCount(QStringLiteral("KJV")), 31102);
        QVERIFY(ds.hasVersion(QStringLiteral("kjv")));
        QVERIFY(!ds.hasVersion(QStringLiteral("TB"))); // TB is never bundled

        const BibleVerse& first = ds.bibleVerses().first();
        QCOMPARE(first.version, QStringLiteral("KJV"));
        QCOMPARE(first.reference(), QStringLiteral("%1 1:1").arg(first.book));
        QVERIFY(first.text.startsWith(QStringLiteral("In the beginning")));

        int minBook = 1000, maxBook = 0;
        for (const BibleVerse& v : ds.bibleVerses()) {
            QVERIFY(v.version == QStringLiteral("KJV"));
            QVERIFY(v.chapter > 0 && v.verse > 0 && !v.text.isEmpty());
            minBook = qMin(minBook, v.bookId);
            maxBook = qMax(maxBook, v.bookId);
        }
        QCOMPARE(minBook, 1);
        QCOMPARE(maxBook, 66);

        const Slide s = first.toSlide();
        QCOMPARE(s.type, SlideType::Scripture);
        QCOMPARE(s.subtitle, QStringLiteral("KJV"));
        QCOMPARE(s.content, first.text.toUpper()); // default settings capitalise all words
    }

    void alertService()
    {
        AlertService& svc = AlertService::instance();
        svc.clear();
        QVERIFY(!svc.isAlertVisible());
        QCOMPARE(svc.runningMessages().size(), 0);

        svc.addMessage(QStringLiteral("Test Alert"), 2);
        QVERIFY(svc.isAlertVisible());
        QCOMPARE(svc.runningMessages().size(), 1);
        QCOMPARE(svc.currentActiveMessage().text, QStringLiteral("Test Alert"));
        QCOMPARE(svc.currentActiveMessage().count, 2);

        svc.notifyPassCompleted();
        QVERIFY(svc.isAlertVisible());
        QCOMPARE(svc.currentActiveMessage().count, 1);

        svc.notifyPassCompleted();
        QVERIFY(!svc.isAlertVisible());
        QCOMPARE(svc.runningMessages().size(), 0);
    }

    void dataServiceCleanupOrphanedData()
    {
        Sandbox sb;
        DataService ds;

        // 1. Create a real media file and an orphan media item
        const QString validMediaPath = sb.path(QStringLiteral("video1.mp4"));
        {
            QFile vf(validMediaPath);
            QVERIFY(vf.open(QIODevice::WriteOnly));
            vf.write("dummy video data");
            vf.close();
        }
        MediaItem validMedia;
        validMedia.name = QStringLiteral("Valid Video");
        validMedia.filePath = validMediaPath;
        QVERIFY(ds.addMediaItem(validMedia));

        MediaItem missingMedia;
        missingMedia.name = QStringLiteral("Missing Video");
        missingMedia.filePath = sb.path(QStringLiteral("does_not_exist.mp4"));
        QVERIFY(ds.addMediaItem(missingMedia));

        // 2. Create valid thumbnail and orphan thumbnail in Data/Thumbnails
        const QString validThumb = AppPaths::thumbnailPathFor(validMediaPath);
        {
            QDir().mkpath(QFileInfo(validThumb).absolutePath());
            QFile tf(validThumb);
            QVERIFY(tf.open(QIODevice::WriteOnly));
            tf.write("png data");
            tf.close();
        }
        const QString orphanThumb = QDir(AppPaths::thumbnailCacheDirectory()).filePath(QStringLiteral("video_orphan_12345.png"));
        {
            QFile tf(orphanThumb);
            QVERIFY(tf.open(QIODevice::WriteOnly));
            tf.write("orphan data");
            tf.close();
        }

        // 3. Create valid presentation and orphan presentation
        const QString validPresPath = sb.path(QStringLiteral("pres1.pdf"));
        {
            QFile pf(validPresPath);
            QVERIFY(pf.open(QIODevice::WriteOnly));
            pf.write("%PDF-1.4 dummy");
            pf.close();
        }
        PresentationDocItem validPres;
        validPres.name = QStringLiteral("Valid Pres");
        validPres.filePath = validPresPath;
        QVERIFY(ds.addPresentationDoc(validPres));

        PresentationDocItem missingPres;
        missingPres.name = QStringLiteral("Missing Pres");
        missingPres.filePath = sb.path(QStringLiteral("missing.pdf"));
        QVERIFY(ds.addPresentationDoc(missingPres));

        const QString validPresCache = AppPaths::presentationCacheDirectoryFor(validPresPath);
        QDir().mkpath(validPresCache);
        {
            QFile cf(QDir(validPresCache).filePath(QStringLiteral("slide_1.png")));
            cf.open(QIODevice::WriteOnly);
            cf.write("slide");
            cf.close();
        }

        const QString orphanPresCache = QDir(AppPaths::presentationCacheDirectory()).filePath(QStringLiteral("orphan_cache_hash_9999"));
        QDir().mkpath(orphanPresCache);
        {
            QFile cf(QDir(orphanPresCache).filePath(QStringLiteral("slide_1.png")));
            cf.open(QIODevice::WriteOnly);
            cf.write("orphan slide");
            cf.close();
        }

        // 4. Run cleanup
        ds.cleanupOrphanedData();

        // 5. Verify results:
        // Missing media pruned, valid media kept
        QCOMPARE(ds.mediaItems().size(), 1);
        QCOMPARE(ds.mediaItems().first().name, QStringLiteral("Valid Video"));

        // Missing presentation pruned, valid presentation kept
        QCOMPARE(ds.presentationDocs().size(), 1);
        QCOMPARE(ds.presentationDocs().first().name, QStringLiteral("Valid Pres"));

        // Valid thumbnail preserved, orphan thumbnail removed
        QVERIFY(QFile::exists(validThumb));
        QVERIFY(!QFile::exists(orphanThumb));

        // Valid presentation cache preserved, orphan presentation cache directory removed
        QVERIFY(QDir(validPresCache).exists());
        QVERIFY(!QDir(orphanPresCache).exists());
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
