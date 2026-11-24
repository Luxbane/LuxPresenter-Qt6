#include "Services/UpdateService.h"

#include "Views/UpdateDialog.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QWidget>

#ifndef LUX_VERSION
#define LUX_VERSION "1.0.2"
#endif

namespace Lux {

namespace {

struct SemVer
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
};

SemVer parseSemVer(QString s)
{
    s = s.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        s.remove(0, 1);

    const int dash = s.indexOf(QLatin1Char('-'));
    if (dash >= 0)
        s = s.left(dash);

    const int plus = s.indexOf(QLatin1Char('+'));
    if (plus >= 0)
        s = s.left(plus);

    const QStringList parts = s.split(QLatin1Char('.'));
    SemVer v;
    if (!parts.isEmpty())
        v.major = parts.value(0).toInt();
    if (parts.size() > 1)
        v.minor = parts.value(1).toInt();
    if (parts.size() > 2)
        v.patch = parts.value(2).toInt();
    if (parts.size() > 3)
        v.build = parts.value(3).toInt();
    return v;
}

} // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

bool UpdateService::isVersionNewer(const QString& remoteVer, const QString& currentVer)
{
    const SemVer r = parseSemVer(remoteVer);
    const SemVer c = parseSemVer(currentVer);
    if (r.major != c.major)
        return r.major > c.major;
    if (r.minor != c.minor)
        return r.minor > c.minor;
    if (r.patch != c.patch)
        return r.patch > c.patch;
    return r.build > c.build;
}

void UpdateService::checkForUpdates(QWidget* parentWidget, bool interactive, bool forceShowUpdate)
{
    m_parentWidget = parentWidget;
    m_interactive = interactive;
    m_forceShowUpdate = forceShowUpdate;

    emit checkingStarted();

    const QUrl url(QStringLiteral("https://api.github.com/repos/Luxbane/LuxPresenter-Qt6/releases/latest"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LuxPresenter-App/%1").arg(QStringLiteral(LUX_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onReplyFinished(reply);
    });
}

void UpdateService::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        emit updateChecked({}, false, err);

        if (m_interactive) {
            QMessageBox::warning(
                m_parentWidget,
                QStringLiteral("Software Update"),
                QStringLiteral("Unable to check for updates.\n\nPlease check your internet connection and try again.\n\n(%1)")
                    .arg(err));
        }
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        const QString err = QStringLiteral("Invalid response from update server.");
        emit updateChecked({}, false, err);
        if (m_interactive) {
            QMessageBox::warning(m_parentWidget, QStringLiteral("Software Update"), err);
        }
        return;
    }

    const QJsonObject obj = doc.object();
    const QString remoteTag = obj.value(QStringLiteral("tag_name")).toString();
    const QString releaseName = obj.value(QStringLiteral("name")).toString();
    const QString releaseBody = obj.value(QStringLiteral("body")).toString();
    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();

    QString patchUrl;
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const auto& a : assets) {
        const QJsonObject item = a.toObject();
        const QString aName = item.value(QStringLiteral("name")).toString();
        if (aName.contains(QStringLiteral("Patch"), Qt::CaseInsensitive) && aName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            patchUrl = item.value(QStringLiteral("browser_download_url")).toString();
            break;
        }
    }

    const QString currentVer = QStringLiteral(LUX_VERSION);
    const bool newer = isVersionNewer(remoteTag, currentVer) || m_forceShowUpdate;

    UpdateInfo info;
    info.currentVersion = currentVer;
    info.newVersion = remoteTag.isEmpty() ? currentVer : remoteTag;
    if (m_forceShowUpdate && info.newVersion == currentVer) {
        info.newVersion = QStringLiteral("%1 (Test Preview)").arg(currentVer);
    }
    info.releaseTitle = releaseName;
    info.releaseNotes = releaseBody;
    info.releaseUrl = !htmlUrl.isEmpty() ? htmlUrl : QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6/releases/latest");
    info.patchDownloadUrl = patchUrl;
    info.isUpdateAvailable = newer;

    emit updateChecked(info, true, {});

    if (newer) {
        auto* dlg = new UpdateDialog(info, m_parentWidget);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->exec();
    } else if (m_interactive) {
        QMessageBox::information(
            m_parentWidget,
            QStringLiteral("Software Update"),
            QStringLiteral("You're up to date!\n\nLuxPresenter %1 is currently the latest version.")
                .arg(currentVer.startsWith(QLatin1Char('v')) ? currentVer : (QLatin1Char('v') + currentVer)));
    }
}

void UpdateService::showPreviewDialog(QWidget* parentWidget)
{
    UpdateInfo sample;
    sample.currentVersion = QStringLiteral(LUX_VERSION);
    sample.newVersion = QStringLiteral("v1.1.2");
    sample.releaseTitle = QStringLiteral("LuxPresenter v1.1.2");
    sample.releaseNotes = QStringLiteral(
        "### \U0001F9E9 LuxPresenter v1.1.2\n\n"
        "A quick maintenance and polish update addressing background switching "
        "behavior while Black Screen is engaged, ensuring smooth presentation "
        "transitions during live services.\n\n"
        "---\n\n"
        "### \U0001F527 Bug Fixes & Improvements\n"
        "* **Seamless Black Screen Switching (`Ctrl+B`)**:\n"
        "  * Fixed an issue where changing songs or backgrounds while Black Screen was "
        "active caused the previous background to momentarily flash on screen before "
        "transitioning when Black Screen was disabled.\n"
        "* **Performance Enhancements**:\n"
        "  * Downscaled video thumbnail cache to lightweight 128x72 for low-end hardware.\n");
    sample.releaseUrl = QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6/releases/latest");
    sample.isUpdateAvailable = true;

    auto* dlg = new UpdateDialog(sample, parentWidget);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

} // namespace Lux
