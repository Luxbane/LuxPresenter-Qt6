#include "Views/UpdateDialog.h"

#include "Services/ThemeService.h"
#include "Views/UiUtil.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSizeGrip>
#include <QSvgRenderer>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace Lux {

namespace {

QPixmap renderRocketIcon(int size, bool isDark)
{
    // SVG rocket icon matching the diagonal flight pose with thruster fins
    const QString strokeColor = isDark ? QStringLiteral("#38BDF8") : QStringLiteral("#0284C7");
    const QString fillColor = isDark ? QStringLiteral("#0F2847") : QStringLiteral("#E0F2FE");

    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' width='%1' height='%1' fill='none' "
        "stroke='%2' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
        "<path d='M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.7-2.13-.09-2.91a2.18 2.18 0 0 0-2.91-.09z' fill='%3'/>"
        "<path d='M12 15l-3-3a22 22 0 0 1 2-3.95A12.88 12.88 0 0 1 22 2c0 2.72-.78 7.5-6 11a22.35 22.35 0 0 1-4 2z' fill='%3'/>"
        "<path d='M9 12H4s.55-3.03 2-4c1.62-1.08 5 0 5 0'/>"
        "<path d='M12 15v5s3.03-.55 4-2c1.08-1.62 0-5 0-5'/>"
        "</svg>")
        .arg(QString::number(size), strokeColor, fillColor);

    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QSvgRenderer renderer(svg.toUtf8());
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p);
    return px;
}

} // namespace

UpdateDialog::UpdateDialog(const UpdateInfo& info, QWidget* parent)
    : QDialog(parent)
    , m_info(info)
{
    setWindowTitle(QStringLiteral("Software Update"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(true);
    resize(520, 430);
    setMinimumSize(460, 360);

    setupUi();
    applyTheme();

    connect(&ThemeService::instance(), &ThemeService::themeChanged, this, [this](bool) {
        applyTheme();
    });
}

void UpdateDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(14);

    // 1. Header (Icon + Title + Version info)
    auto* headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(14);

    const bool dark = ThemeService::instance().isDark();
    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(renderRocketIcon(38, dark));
    iconLabel->setFixedSize(40, 40);
    headerLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* headerTextLayout = new QVBoxLayout;
    headerTextLayout->setSpacing(3);

    m_titleLabel = new QLabel(QStringLiteral("New Version Available!"), this);
    QFont titleFont = tahoma(16, true);
    titleFont.setFamily(QStringLiteral("Segoe UI"));
    m_titleLabel->setFont(titleFont);

    const QString curText = m_info.currentVersion.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)
                                ? m_info.currentVersion
                                : (QLatin1Char('v') + m_info.currentVersion);
    const QString newText = m_info.newVersion.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)
                                ? m_info.newVersion
                                : (QLatin1Char('v') + m_info.newVersion);

    m_versionLabel = new QLabel(QStringLiteral("Current: %1 \u2192 New: %2").arg(curText, newText), this);
    QFont verFont = tahoma(12);
    verFont.setFamily(QStringLiteral("Segoe UI"));
    m_versionLabel->setFont(verFont);

    headerTextLayout->addWidget(m_titleLabel);
    headerTextLayout->addWidget(m_versionLabel);
    headerLayout->addLayout(headerTextLayout, 1);

    root->addLayout(headerLayout);

    // 2. Middle Card with Release Notes (Markdown / RichText)
    auto* cardFrame = new QFrame(this);
    cardFrame->setObjectName(QStringLiteral("UpdateCard"));
    auto* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(0);

    m_browser = new QTextBrowser(cardFrame);
    m_browser->setOpenExternalLinks(true);
    m_browser->setFrameShape(QFrame::NoFrame);

    QString notes = m_info.releaseNotes.trimmed();
    if (notes.isEmpty()) {
        const QString title = !m_info.releaseTitle.isEmpty()
                                  ? m_info.releaseTitle
                                  : QStringLiteral("LuxPresenter %1").arg(newText);
        notes = QStringLiteral(
            "### \U0001F9E9 %1\n\n"
            "A new version of LuxPresenter is available on GitHub.\n\n"
            "---\n\n"
            "### \U0001F4E6 How to Update:\n"
            "1. Click **Download Update** to open the GitHub Release page in your browser.\n"
            "2. Download **LuxPresenter-Patch.zip** (or the installer).\n"
            "3. Extract the patch and run **Update.bat** to update your existing installation.")
            .arg(title);
    }

    m_browser->setMarkdown(notes);
    cardLayout->addWidget(m_browser);
    root->addWidget(cardFrame, 1);

    // 3. Action Buttons
    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 4, 0, 0);

    m_laterBtn = new QPushButton(QStringLiteral("Later"), this);
    m_laterBtn->setObjectName(QStringLiteral("LaterBtn"));
    m_laterBtn->setCursor(Qt::PointingHandCursor);
    m_laterBtn->setMinimumWidth(85);
    m_laterBtn->setFixedHeight(32);
    connect(m_laterBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_laterBtn);

    buttonLayout->addStretch(1);

    m_downloadBtn = new QPushButton(QStringLiteral("Download Update"), this);
    m_downloadBtn->setObjectName(QStringLiteral("DownloadBtn"));
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setMinimumWidth(140);
    m_downloadBtn->setFixedHeight(32);
    m_downloadBtn->setDefault(true);

    connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
        const QString targetUrl = !m_info.releaseUrl.isEmpty()
                                      ? m_info.releaseUrl
                                      : QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6/releases/latest");
        QDesktopServices::openUrl(QUrl(targetUrl));
        accept();
    });
    buttonLayout->addWidget(m_downloadBtn);

    root->addLayout(buttonLayout);
}

void UpdateDialog::applyTheme()
{
    const bool dark = ThemeService::instance().isDark();

    if (dark) {
        setStyleSheet(QStringLiteral(
            "QDialog {"
            "    background-color: #1A1D24;"
            "    color: #F1F5F9;"
            "}"
            "QFrame#UpdateCard {"
            "    background-color: #12151B;"
            "    border: 1px solid #282F3D;"
            "    border-radius: 6px;"
            "}"
            "QTextBrowser {"
            "    background-color: transparent;"
            "    color: #CBD5E1;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 13px;"
            "    border: none;"
            "}"
            "QPushButton#LaterBtn {"
            "    background-color: #262C38;"
            "    color: #CBD5E1;"
            "    border: 1px solid #3B4455;"
            "    border-radius: 4px;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 12px;"
            "    font-weight: 500;"
            "    padding: 4px 14px;"
            "}"
            "QPushButton#LaterBtn:hover {"
            "    background-color: #323A4A;"
            "    color: #F8FAFC;"
            "    border-color: #4A5568;"
            "}"
            "QPushButton#LaterBtn:pressed {"
            "    background-color: #1E232D;"
            "}"
            "QPushButton#DownloadBtn {"
            "    background-color: #0078D7;"
            "    color: #FFFFFF;"
            "    border: 1px solid #005A9E;"
            "    border-radius: 4px;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 12px;"
            "    font-weight: 600;"
            "    padding: 4px 16px;"
            "}"
            "QPushButton#DownloadBtn:hover {"
            "    background-color: #1084E3;"
            "    border-color: #0078D7;"
            "}"
            "QPushButton#DownloadBtn:pressed {"
            "    background-color: #0067B8;"
            "}"));

        if (m_titleLabel)
            m_titleLabel->setStyleSheet(QStringLiteral("color: #F1F5F9; font-weight: bold;"));
        if (m_versionLabel)
            m_versionLabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    } else {
        setStyleSheet(QStringLiteral(
            "QDialog {"
            "    background-color: #F8FAFC;"
            "    color: #0F172A;"
            "}"
            "QFrame#UpdateCard {"
            "    background-color: #FFFFFF;"
            "    border: 1px solid #CBD5E1;"
            "    border-radius: 6px;"
            "}"
            "QTextBrowser {"
            "    background-color: transparent;"
            "    color: #334155;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 13px;"
            "    border: none;"
            "}"
            "QPushButton#LaterBtn {"
            "    background-color: #F1F5F9;"
            "    color: #334155;"
            "    border: 1px solid #CBD5E1;"
            "    border-radius: 4px;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 12px;"
            "    font-weight: 500;"
            "    padding: 4px 14px;"
            "}"
            "QPushButton#LaterBtn:hover {"
            "    background-color: #E2E8F0;"
            "    color: #0F172A;"
            "}"
            "QPushButton#LaterBtn:pressed {"
            "    background-color: #CBD5E1;"
            "}"
            "QPushButton#DownloadBtn {"
            "    background-color: #0078D7;"
            "    color: #FFFFFF;"
            "    border: 1px solid #005A9E;"
            "    border-radius: 4px;"
            "    font-family: 'Segoe UI', Tahoma, sans-serif;"
            "    font-size: 12px;"
            "    font-weight: 600;"
            "    padding: 4px 16px;"
            "}"
            "QPushButton#DownloadBtn:hover {"
            "    background-color: #1084E3;"
            "    border-color: #0078D7;"
            "}"
            "QPushButton#DownloadBtn:pressed {"
            "    background-color: #0067B8;"
            "}"));

        if (m_titleLabel)
            m_titleLabel->setStyleSheet(QStringLiteral("color: #0F172A; font-weight: bold;"));
        if (m_versionLabel)
            m_versionLabel->setStyleSheet(QStringLiteral("color: #64748B;"));
    }
}

} // namespace Lux
