#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

namespace Lux {

struct UpdateInfo
{
    QString currentVersion;
    QString newVersion;
    QString releaseTitle;
    QString releaseNotes;
    QString releaseUrl;
    QString patchDownloadUrl;
    bool isUpdateAvailable = false;
};

class UpdateService : public QObject
{
    Q_OBJECT
public:
    explicit UpdateService(QObject* parent = nullptr);
    ~UpdateService() override = default;

    // Checks the latest GitHub release.
    // If interactive is true, shows feedback dialogs (update dialog, up-to-date message, or error).
    // If forceShowUpdate is true, opens the update dialog even if the version is not newer (for testing).
    void checkForUpdates(QWidget* parentWidget = nullptr, bool interactive = true, bool forceShowUpdate = false);

    // Semantic version comparison: returns true if remoteVer > currentVer
    static bool isVersionNewer(const QString& remoteVer, const QString& currentVer);

    // Displays the sample / preview update dialog (useful for visual testing)
    static void showPreviewDialog(QWidget* parentWidget = nullptr);

signals:
    void checkingStarted();
    void updateChecked(const UpdateInfo& info, bool ok, const QString& errorMessage);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QWidget* m_parentWidget = nullptr;
    bool m_interactive = true;
    bool m_forceShowUpdate = false;
};

} // namespace Lux
