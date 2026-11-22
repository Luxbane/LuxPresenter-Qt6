#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Lux {

// Port of Models/AppSettings.cs. Defaults match the settings.json shipped with
// the reference application (e.g. Tahoma rather than the class-level "Arial").
struct AppSettings
{
    QString lastUsedBackground;
    bool isVideoLooping = true;
    int activeLibraryTabIndex = 0;
    QString customSongDatabaseDirectory; // null = not set
    bool isDarkMode = false;
    QStringList presetAlertMessages;

    // Song options
    QString songTextAlignment = QStringLiteral("Center");   // Left, Center, Right
    QString songVerticalAlignment = QStringLiteral("Top");  // Top, Center, Bottom
    int songFontSize = 13;
    int songFontHeightPercent = 14;
    QString songFontFamily = QStringLiteral("Tahoma");
    bool songFontBold = true;
    bool overrideSongFontGlobally = false;

    // Projection margins
    int marginTop = 0;
    int marginBottom = 0;
    int marginLeft = 0;
    int marginRight = 0;

    // Verse/Chorus labels
    bool showVerseLabels = false;
    QString labelLocation = QStringLiteral("Top Center");
    QString labelSize = QStringLiteral("Normal");
    bool indentLabel = false;

    // Word formatting
    bool autoWrapWords = true;
    bool capitalizeAllWords = true;
    bool capitalizeFirstCharEachLine = false;
    bool autoCapitalizeKeywords = false;
    QString capitalizeKeywords = QStringLiteral("CHRIST\r\nFATHER\r\nGOD\r\nHE\r\nHIS\r\nHIM\r\nHIMSELF");

    // Transitions: "Blend" or "Cut"
    QString songTransition = QStringLiteral("Blend");
    QString slideTransition = QStringLiteral("Blend");
    QString mediaTransition = QStringLiteral("Blend");
    QString screenTransition = QStringLiteral("Blend");
    int transitionDurationMs = 500;

    // "libmpv" (default) or the legacy fallback name used by the reference.
    QString videoBackgroundEngine = QStringLiteral("libmpv");

    // Added by the Qt version (extra keys; the reference ignores unknown keys)
    QString outputDisplay;                       // Windows device name of the projector monitor; empty = automatic
    QString selectedBibleVersion;                // id of the active Bible translation; empty = built-in default
    bool adBlockEnabled = true;                  // Online tab ad blocker
    QStringList adBlockWhitelist;                // host names for which blocking is off
    QStringList deletedBuiltinBackgrounds;       // built-in background IDs that the user removed
    bool isMediaGridView = true;                 // Media library view: true = grid (thumbnails), false = list
    bool stageServerEnabled = true;              // Web stage display HTTP server

    // Brings every value into its valid range / allowed set (a hand-edited or damaged settings.json cannot break
    // the application). Called whenever settings are read or applied.
    void sanitize();
    static QStringList fontFamilies();           // the font choices offered in Options
    static const QStringList& labelLocations();
    static const QStringList& labelSizes();

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject& o);
};

} // namespace Lux
