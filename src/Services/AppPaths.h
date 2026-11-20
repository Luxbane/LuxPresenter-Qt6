#pragma once

#include <QString>

namespace Lux {

// Port of Services/AppPaths.cs.
//
//  * Portable mode: if the application directory is writable, user data lives in
//    <app>\Data and songs in <app>\Database.
//  * Installed mode: otherwise data lives in %APPDATA%\LuxPresenterQt6\Data (seeded
//    from <app>\Data) and songs in %USERPROFILE%\Documents\LuxPresenterQt6\Database.
//
// Data isolation: the Qt build uses its own identity ("LuxPresenterQt6") and never
// reads, migrates or writes the C# LuxPresenter's AppData/Documents/install folders.
// LibreOffice is an external dependency resolved from <app>\Engines\LibreOffice or
// %ProgramFiles%\LibreOffice; no drive-specific fallback paths are used.
// All paths use forward slashes (Qt style).
class AppPaths
{
public:
    static constexpr const char* kAppIdentity = "LuxPresenterQt6";

    // Installed-mode locations (pure path computation; nothing is created or read).
    static QString installedDataDirectory();
    static QString installedSongsDirectory();

    static QString baseDirectory();
    static QString dataDirectory();            // settings.json, media.json, bibles.json, ...
    static QString thumbnailCacheDirectory();  // <Data>/Thumbnails
    static QString thumbnailPathFor(const QString& filePath);
    static QString presentationCacheDirectory(); // <Data>/PresentationCache (created on demand)
    static QString presentationCacheDirectoryFor(const QString& filePath);
    static QString mediaDirectory();           // <Data>/Media
    static QString songsDirectory();           // folder holding songs.json
    static void setSongsDirectory(const QString& path); // empty = re-resolve default
    // <app>/Engines/LibreOffice if present, else %ProgramFiles%/LibreOffice if present,
    // else the (non-existent) local engine path.
    static QString resolveLibreOfficeDirectory();

    // Test hooks: override the application directory and clear all cached paths.
    static void setBaseDirectoryOverride(const QString& path);
    static void setForceInstalledModeForTesting(bool force);
    static void resetCache();
};

} // namespace Lux
