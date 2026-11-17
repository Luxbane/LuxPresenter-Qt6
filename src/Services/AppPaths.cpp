#include "Services/AppPaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

namespace Lux {

namespace {

QString g_baseOverride;
QString g_dataDir;
QString g_songsDir;

QString clean(const QString& p) { return QDir::cleanPath(p); }

bool isDirectoryWritable(const QString& dirPath)
{
    if (!QDir(dirPath).exists())
        return false;
    const QString probe = QDir(dirPath).filePath(
        QStringLiteral(".write_test_%1.tmp").arg(QUuid::createUuid().toString(QUuid::Id128)));
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("test");
    f.close();
    return QFile::remove(probe);
}

void copyDirectoryIfMissing(const QString& src, const QString& dst)
{
    const QDir srcDir(src);
    QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString dest = QDir(dst).filePath(srcDir.relativeFilePath(file));
        if (QFile::exists(dest))
            continue;
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile::copy(file, dest);
    }
}

bool g_forceInstalledMode = false;

bool usePortableMode(const QString& base)
{
    return !g_forceInstalledMode && isDirectoryWritable(base);
}

QString resolveDataDirectory()
{
    const QString base = AppPaths::baseDirectory();
    const QString localData = QDir(base).filePath(QStringLiteral("Data"));

    if (usePortableMode(base) && QDir().mkpath(localData))
        return clean(localData);

    // Installed mode: %APPDATA%\LuxPresenterQt6\Data (never the C# app's folder)
    const QString appData = AppPaths::installedDataDirectory();
    QDir().mkpath(appData);
    if (QDir(localData).exists())
        copyDirectoryIfMissing(localData, appData); // seed bundled data (KJV)
    return appData;
}

QString resolveSongsDirectory()
{
    const QString base = AppPaths::baseDirectory();
    const QString localDb = QDir(base).filePath(QStringLiteral("Database"));

    if (usePortableMode(base) && QDir().mkpath(localDb))
        return clean(localDb);

    const QString docsDb = AppPaths::installedSongsDirectory();
    QDir().mkpath(docsDb);
    return docsDb;
}

} // namespace

QString AppPaths::installedDataDirectory()
{
    QString roaming = qEnvironmentVariable("APPDATA");
    if (roaming.isEmpty())
        roaming = QDir::home().filePath(QStringLiteral("AppData/Roaming"));
    return clean(QDir(roaming).filePath(QString::fromLatin1(kAppIdentity) + QStringLiteral("/Data")));
}

QString AppPaths::installedSongsDirectory()
{
    QString profile = qEnvironmentVariable("USERPROFILE");
    if (profile.isEmpty())
        profile = QDir::homePath();
    return clean(QDir(profile).filePath(
        QStringLiteral("Documents/") + QString::fromLatin1(kAppIdentity) + QStringLiteral("/Database")));
}

void AppPaths::setForceInstalledModeForTesting(bool force)
{
    g_forceInstalledMode = force;
    resetCache();
}

QString AppPaths::baseDirectory()
{
    return g_baseOverride.isEmpty() ? clean(QCoreApplication::applicationDirPath()) : g_baseOverride;
}

QString AppPaths::dataDirectory()
{
    if (g_dataDir.isEmpty())
        g_dataDir = resolveDataDirectory();
    return g_dataDir;
}

QString AppPaths::thumbnailCacheDirectory()
{
    return clean(QDir(dataDirectory()).filePath(QStringLiteral("Thumbnails")));
}

QString AppPaths::presentationCacheDirectory()
{
    const QString dir = clean(QDir(dataDirectory()).filePath(QStringLiteral("PresentationCache")));
    QDir().mkpath(dir);
    return dir;
}

QString AppPaths::presentationCacheDirectoryFor(const QString& filePath)
{
    const QByteArray hash = QCryptographicHash::hash(QFileInfo(filePath).absoluteFilePath().toLower().toUtf8(), QCryptographicHash::Md5).toHex();
    return QDir(presentationCacheDirectory()).filePath(QString::fromLatin1(hash));
}

QString AppPaths::mediaDirectory()
{
    return clean(QDir(dataDirectory()).filePath(QStringLiteral("Media")));
}

QString AppPaths::songsDirectory()
{
    if (g_songsDir.isEmpty())
        g_songsDir = resolveSongsDirectory();
    return g_songsDir;
}

void AppPaths::setSongsDirectory(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        g_songsDir = resolveSongsDirectory();
        return;
    }
    g_songsDir = clean(path);
    QDir().mkpath(g_songsDir);
}

QString AppPaths::resolveLibreOfficeDirectory()
{
    const QString localEngine = clean(QDir(baseDirectory()).filePath(QStringLiteral("Engines/LibreOffice")));
    if (QDir(localEngine).exists())
        return localEngine;

    const QString pf = qEnvironmentVariable("ProgramFiles");
    if (!pf.isEmpty()) {
        const QString standard = clean(QDir(pf).filePath(QStringLiteral("LibreOffice")));
        if (QDir(standard).exists())
            return standard;
    }
    return localEngine;
}

void AppPaths::setBaseDirectoryOverride(const QString& path)
{
    g_baseOverride = path.isEmpty() ? QString() : clean(path);
    resetCache();
}

void AppPaths::resetCache()
{
    g_dataDir.clear();
    g_songsDir.clear();
}

} // namespace Lux
