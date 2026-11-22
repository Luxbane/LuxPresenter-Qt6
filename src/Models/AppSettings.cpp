#include <QRegularExpression>
#include "Models/AppSettings.h"

#include "Services/JsonUtil.h"

namespace Lux {

QStringList AppSettings::fontFamilies()
{
    return {QStringLiteral("Arial"), QStringLiteral("Tahoma"), QStringLiteral("Segoe UI"), QStringLiteral("Calibri"),
            QStringLiteral("Trebuchet MS"), QStringLiteral("Verdana"), QStringLiteral("Arial Black"),
            QStringLiteral("Times New Roman"), QStringLiteral("Impact")};
}

const QStringList& AppSettings::labelLocations()
{
    static const QStringList l{QStringLiteral("Top Left"), QStringLiteral("Top Center"), QStringLiteral("Top Right"),
                               QStringLiteral("Bottom Left"), QStringLiteral("Bottom Center"), QStringLiteral("Bottom Right")};
    return l;
}

const QStringList& AppSettings::labelSizes()
{
    static const QStringList l{QStringLiteral("Small"), QStringLiteral("Normal"), QStringLiteral("Large")};
    return l;
}

namespace {
QString oneOf(const QString& v, const QStringList& allowed, const QString& fallback)
{
    for (const QString& a : allowed)
        if (a.compare(v.trimmed(), Qt::CaseInsensitive) == 0)
            return a;
    return fallback;
}
} // namespace

void AppSettings::sanitize()
{
    songTextAlignment = oneOf(songTextAlignment, {QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")}, QStringLiteral("Center"));
    songVerticalAlignment = oneOf(songVerticalAlignment, {QStringLiteral("Top"), QStringLiteral("Center"), QStringLiteral("Bottom")}, QStringLiteral("Top"));
    if (songFontFamily.trimmed().isEmpty())
        songFontFamily = QStringLiteral("Tahoma");
    songFontFamily = songFontFamily.trimmed();
    songFontSize = qBound(6, songFontSize, 96);
    songFontHeightPercent = qBound(1, songFontHeightPercent, 100);
    marginTop = qBound(-20, marginTop, 40);
    marginBottom = qBound(-20, marginBottom, 40);
    marginLeft = qBound(-20, marginLeft, 40);
    marginRight = qBound(-20, marginRight, 40);
    labelLocation = oneOf(labelLocation, labelLocations(), QStringLiteral("Top Center"));
    labelSize = oneOf(labelSize, labelSizes(), QStringLiteral("Normal"));
    const QStringList tr{QStringLiteral("Blend"), QStringLiteral("Cut")};
    songTransition = oneOf(songTransition, tr, QStringLiteral("Blend"));
    slideTransition = oneOf(slideTransition, tr, QStringLiteral("Blend"));
    mediaTransition = oneOf(mediaTransition, tr, QStringLiteral("Blend"));
    screenTransition = oneOf(screenTransition, tr, QStringLiteral("Blend"));
    // the four durations offered by Options (Fast / Normal / Smooth / Slow); anything else snaps to the nearest
    const int steps[] = {250, 500, 750, 1000};
    int best = steps[1];
    for (int s : steps)
        if (qAbs(s - transitionDurationMs) < qAbs(best - transitionDurationMs))
            best = s;
    transitionDurationMs = best;
    adBlockWhitelist.removeAll(QString());
    for (QString& h : adBlockWhitelist) {
        h = h.trimmed().toLower();
        const int scheme = h.indexOf(QStringLiteral("://"));
        if (scheme >= 0)
            h.remove(0, scheme + 3);
        const int end = h.indexOf(QRegularExpression(QStringLiteral("[/?#:]")));
        if (end >= 0)
            h.truncate(end);
        while (h.startsWith(QLatin1Char('*')) || h.startsWith(QLatin1Char('.')))
            h.remove(0, 1);
    }
    adBlockWhitelist.removeAll(QString());
    adBlockWhitelist.removeDuplicates();
    selectedBibleVersion = selectedBibleVersion.trimmed();
}

QJsonObject AppSettings::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("LastUsedBackground")] = lastUsedBackground;
    o[QStringLiteral("IsVideoLooping")] = isVideoLooping;
    o[QStringLiteral("ActiveLibraryTabIndex")] = activeLibraryTabIndex;
    o[QStringLiteral("CustomSongDatabaseDirectory")] = Json::fromNullable(customSongDatabaseDirectory);
    o[QStringLiteral("IsDarkMode")] = isDarkMode;
    o[QStringLiteral("PresetAlertMessages")] = Json::fromStringList(presetAlertMessages);
    o[QStringLiteral("SongTextAlignment")] = songTextAlignment;
    o[QStringLiteral("SongVerticalAlignment")] = songVerticalAlignment;
    o[QStringLiteral("SongFontSize")] = songFontSize;
    o[QStringLiteral("SongFontHeightPercent")] = songFontHeightPercent;
    o[QStringLiteral("SongFontFamily")] = songFontFamily;
    o[QStringLiteral("SongFontBold")] = songFontBold;
    o[QStringLiteral("OverrideSongFontGlobally")] = overrideSongFontGlobally;
    o[QStringLiteral("MarginTop")] = marginTop;
    o[QStringLiteral("MarginBottom")] = marginBottom;
    o[QStringLiteral("MarginLeft")] = marginLeft;
    o[QStringLiteral("MarginRight")] = marginRight;
    o[QStringLiteral("ShowVerseLabels")] = showVerseLabels;
    o[QStringLiteral("LabelLocation")] = labelLocation;
    o[QStringLiteral("LabelSize")] = labelSize;
    o[QStringLiteral("IndentLabel")] = indentLabel;
    o[QStringLiteral("AutoWrapWords")] = autoWrapWords;
    o[QStringLiteral("CapitalizeAllWords")] = capitalizeAllWords;
    o[QStringLiteral("CapitalizeFirstCharEachLine")] = capitalizeFirstCharEachLine;
    o[QStringLiteral("AutoCapitalizeKeywords")] = autoCapitalizeKeywords;
    o[QStringLiteral("CapitalizeKeywords")] = capitalizeKeywords;
    o[QStringLiteral("SongTransition")] = songTransition;
    o[QStringLiteral("SlideTransition")] = slideTransition;
    o[QStringLiteral("MediaTransition")] = mediaTransition;
    o[QStringLiteral("ScreenTransition")] = screenTransition;
    o[QStringLiteral("TransitionDurationMs")] = transitionDurationMs;
    o[QStringLiteral("VideoBackgroundEngine")] = videoBackgroundEngine;
    o[QStringLiteral("OutputDisplay")] = outputDisplay;
    o[QStringLiteral("SelectedBibleVersion")] = selectedBibleVersion;
    o[QStringLiteral("AdBlockEnabled")] = adBlockEnabled;
    o[QStringLiteral("AdBlockWhitelist")] = Json::fromStringList(adBlockWhitelist);
    o[QStringLiteral("DeletedBuiltinBackgrounds")] = Json::fromStringList(deletedBuiltinBackgrounds);
    o[QStringLiteral("IsMediaGridView")] = isMediaGridView;
    o[QStringLiteral("StageServerEnabled")] = stageServerEnabled;
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject& o)
{
    const AppSettings d; // defaults
    AppSettings s;
    s.lastUsedBackground = Json::str(o, QStringLiteral("LastUsedBackground"), d.lastUsedBackground);
    s.isVideoLooping = Json::boolean(o, QStringLiteral("IsVideoLooping"), d.isVideoLooping);
    s.activeLibraryTabIndex = Json::integer(o, QStringLiteral("ActiveLibraryTabIndex"), d.activeLibraryTabIndex);
    s.customSongDatabaseDirectory = Json::nullableStr(o, QStringLiteral("CustomSongDatabaseDirectory"));
    s.isDarkMode = Json::boolean(o, QStringLiteral("IsDarkMode"), d.isDarkMode);
    s.presetAlertMessages = Json::stringList(o, QStringLiteral("PresetAlertMessages"));
    s.songTextAlignment = Json::str(o, QStringLiteral("SongTextAlignment"), d.songTextAlignment);
    s.songVerticalAlignment = Json::str(o, QStringLiteral("SongVerticalAlignment"), d.songVerticalAlignment);
    s.songFontSize = Json::integer(o, QStringLiteral("SongFontSize"), d.songFontSize);
    s.songFontHeightPercent = Json::integer(o, QStringLiteral("SongFontHeightPercent"), d.songFontHeightPercent);
    s.songFontFamily = Json::str(o, QStringLiteral("SongFontFamily"), d.songFontFamily);
    s.songFontBold = Json::boolean(o, QStringLiteral("SongFontBold"), d.songFontBold);
    s.overrideSongFontGlobally = Json::boolean(o, QStringLiteral("OverrideSongFontGlobally"), d.overrideSongFontGlobally);
    s.marginTop = Json::integer(o, QStringLiteral("MarginTop"), d.marginTop);
    s.marginBottom = Json::integer(o, QStringLiteral("MarginBottom"), d.marginBottom);
    s.marginLeft = Json::integer(o, QStringLiteral("MarginLeft"), d.marginLeft);
    s.marginRight = Json::integer(o, QStringLiteral("MarginRight"), d.marginRight);
    s.showVerseLabels = Json::boolean(o, QStringLiteral("ShowVerseLabels"), d.showVerseLabels);
    s.labelLocation = Json::str(o, QStringLiteral("LabelLocation"), d.labelLocation);
    s.labelSize = Json::str(o, QStringLiteral("LabelSize"), d.labelSize);
    s.indentLabel = Json::boolean(o, QStringLiteral("IndentLabel"), d.indentLabel);
    s.autoWrapWords = Json::boolean(o, QStringLiteral("AutoWrapWords"), d.autoWrapWords);
    s.capitalizeAllWords = Json::boolean(o, QStringLiteral("CapitalizeAllWords"), d.capitalizeAllWords);
    s.capitalizeFirstCharEachLine = Json::boolean(o, QStringLiteral("CapitalizeFirstCharEachLine"), d.capitalizeFirstCharEachLine);
    s.autoCapitalizeKeywords = Json::boolean(o, QStringLiteral("AutoCapitalizeKeywords"), d.autoCapitalizeKeywords);
    s.capitalizeKeywords = Json::str(o, QStringLiteral("CapitalizeKeywords"), d.capitalizeKeywords);
    s.songTransition = Json::str(o, QStringLiteral("SongTransition"), d.songTransition);
    s.slideTransition = Json::str(o, QStringLiteral("SlideTransition"), d.slideTransition);
    s.mediaTransition = Json::str(o, QStringLiteral("MediaTransition"), d.mediaTransition);
    s.screenTransition = Json::str(o, QStringLiteral("ScreenTransition"), d.screenTransition);
    s.transitionDurationMs = Json::integer(o, QStringLiteral("TransitionDurationMs"), d.transitionDurationMs);
    s.videoBackgroundEngine = Json::str(o, QStringLiteral("VideoBackgroundEngine"), d.videoBackgroundEngine);
    s.outputDisplay = Json::str(o, QStringLiteral("OutputDisplay"), d.outputDisplay);
    s.selectedBibleVersion = Json::str(o, QStringLiteral("SelectedBibleVersion"), d.selectedBibleVersion);
    s.adBlockEnabled = Json::boolean(o, QStringLiteral("AdBlockEnabled"), d.adBlockEnabled);
    s.adBlockWhitelist = Json::stringList(o, QStringLiteral("AdBlockWhitelist"));
    s.deletedBuiltinBackgrounds = Json::stringList(o, QStringLiteral("DeletedBuiltinBackgrounds"));
    s.isMediaGridView = Json::boolean(o, QStringLiteral("IsMediaGridView"), d.isMediaGridView);
    s.stageServerEnabled = Json::boolean(o, QStringLiteral("StageServerEnabled"), d.stageServerEnabled);
    s.sanitize();
    return s;
}

} // namespace Lux
