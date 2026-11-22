#include "Views/Components/OnlineBrowserView.h"

#include "Services/AdBlockEngine.h"
#include "Services/AdBlockService.h"
#include "Views/Components/WebView2Host.h"
#include "Views/UiUtil.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QAction>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Lux {

namespace {

QPushButton* navButton(const QString& glyph, const QString& tip, int px, const QString& color, bool emoji, QWidget* parent)
{
    auto* b = new QPushButton(glyph, parent);
    b->setFixedSize(26, 26);
    b->setFont(emoji ? segoeEmoji(px) : segoeSymbol(px));
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral("QPushButton{background:transparent; border:none; padding:0; %1}"
                                    "QPushButton:disabled{background:transparent; border:none; color:#94A3B8;}")
                         .arg(color));
    b->setEnabled(false);
    return b;
}

} // namespace

OnlineBrowserView::OnlineBrowserView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("MainRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Navigation / URL toolbar (38px)
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("SurfaceBarBottom"));
    bar->setFixedHeight(38);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(6, 4, 6, 4);
    bl->setSpacing(0);

    auto* home = navButton(QStringLiteral("\U0001F3E0"), QStringLiteral("Home"), 13, QString(), true, bar);
    m_back = navButton(QStringLiteral("\u25C0"), QStringLiteral("Back"), 12, QString(), false, bar);
    m_forward = navButton(QStringLiteral("\u25B6"), QStringLiteral("Forward"), 12, QString(), false, bar);
    auto* refresh = navButton(QStringLiteral("\U0001F504"), QStringLiteral("Refresh"), 12, QString(), true, bar);
    auto* stop = navButton(QStringLiteral("\u2716"), QStringLiteral("Stop"), 11, QStringLiteral("color:#DC2626;"), false, bar);
    m_nav = {home, m_back, m_forward, refresh, stop};
    for (QPushButton* b : m_nav) {
        bl->addWidget(b);
        bl->addSpacing(2);
    }
    bl->addSpacing(6);

    m_url = new QLineEdit(bar);
    m_url->setPlaceholderText(QStringLiteral("Search with Google or enter web address..."));
    m_url->setFont(segoeSymbol(12));
    bl->addWidget(m_url, 1);
    auto* go = navButton(QStringLiteral("\u2794"), QStringLiteral("Go to URL"), 12, QStringLiteral("color:#1E60D2; font-weight:bold;"), false, bar);
    go->setFixedSize(24, 26);
    m_nav.append(go);
    bl->addWidget(go);
    bl->addSpacing(8);

    auto* youtube = new QPushButton(QStringLiteral("YouTube"), bar);
    youtube->setStyleSheet(QStringLiteral("QPushButton{font-size:11px; padding:2px 8px;}"));
    m_nav.append(youtube);
    bl->addWidget(youtube);
    bl->addSpacing(6);

    // uBlock Origin badge & menu
    m_ads = new QPushButton(QStringLiteral("\U0001F6E1 uBlock Origin: On"), bar);
    m_ads->setObjectName(QStringLiteral("adBlockButton"));
    m_ads->setCursor(Qt::PointingHandCursor);
    m_ads->setFont(segoeEmoji(11));
    m_adsMenu = new QMenu(m_ads);
    m_adsOn = m_adsMenu->addAction(QStringLiteral("Enable uBlock Origin"));
    m_adsOn->setCheckable(true);
    m_adsOn->setChecked(true);
    m_adsDashboard = m_adsMenu->addAction(QStringLiteral("Open uBlock Origin Dashboard"));
    m_adsMenu->addSeparator();
    m_adsStatus = m_adsMenu->addAction(QStringLiteral("uBlock Origin 1.76.2 (Active)"));
    m_adsStatus->setEnabled(false);
    m_ads->setMenu(m_adsMenu);
    bl->addWidget(m_ads);
    bl->addSpacing(6);

    m_close = new QPushButton(QStringLiteral("\u2715 Close Online"), bar);
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setToolTip(QStringLiteral("Close Online Browser and return to Presentation"));
    m_close->setStyleSheet(QStringLiteral("QPushButton{background:#FEE2E2; color:#DC2626; border:1px solid #FCA5A5; font-size:11px; font-weight:600; padding:2px 8px;}"
                                          "QPushButton:hover{background:#FECACA;}"));
    bl->addWidget(m_close);
    root->addWidget(bar);

    // 2. Viewport: the WebView2 control, or an explanation when it cannot run.
    m_stack = new QStackedWidget(this);
    m_host = new WebView2Host(m_stack);
    m_unavailable = new QLabel(m_stack);
    m_unavailable->setAlignment(Qt::AlignCenter);
    m_unavailable->setWordWrap(true);
    m_unavailable->setOpenExternalLinks(true);
    m_unavailable->setFont(tahoma(12));
    m_unavailable->setStyleSheet(QStringLiteral("color:palette(mid); background:transparent; padding:24px;"));
    m_stack->addWidget(m_host);
    m_stack->addWidget(m_unavailable);
    root->addWidget(m_stack, 1);

    m_loading = new QProgressBar(m_stack);
    m_loading->setRange(0, 0);
    m_loading->setTextVisible(false);
    m_loading->setFixedHeight(3);
    m_loading->hide();

    setNavigationEnabled(false);

    connect(m_close, &QPushButton::clicked, this, &OnlineBrowserView::closeRequested);
    connect(home, &QPushButton::clicked, this, [this] { m_host->goHome(); });
    connect(m_back, &QPushButton::clicked, this, [this] { m_host->goBack(); });
    connect(m_forward, &QPushButton::clicked, this, [this] { m_host->goForward(); });
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (m_url->text().trimmed().isEmpty())
            m_host->goHome();
        else
            m_host->reload();
    });
    connect(stop, &QPushButton::clicked, this, [this] { m_host->stop(); });
    connect(go, &QPushButton::clicked, this, [this] { navigateFromBox(); });
    connect(youtube, &QPushButton::clicked, this, [this] { m_host->navigate(QStringLiteral("https://www.youtube.com")); });
    connect(m_url, &QLineEdit::returnPressed, this, [this] { navigateFromBox(); });

    connect(m_host, &WebView2Host::ready, this, [this] {
        m_stack->setCurrentWidget(m_host);
        setNavigationEnabled(true);
        updateButtons();
        updateAdBlockUi();
    });
    connect(m_host, &WebView2Host::extensionStatusChanged, this, [this](bool) {
        updateAdBlockUi();
    });
    connect(m_adsOn, &QAction::triggered, this, [this](bool on) {
        if (m_host && m_host->isReady())
            m_host->setExtensionEnabled(on);
        if (m_service)
            m_service->setEnabled(on);
        updateAdBlockUi();
        emit adBlockEdited();
    });
    connect(m_adsDashboard, &QAction::triggered, this, [this] {
        if (m_host)
            m_host->openExtensionDashboard();
    });

    connect(m_host, &WebView2Host::unavailable, this, [this](const QString& reason) {
        m_unavailable->setText(QStringLiteral("The online browser is not available.\n\n%1\n\n"
                                              "The rest of LuxPresenter is not affected. WebView2 Runtime download: "
                                              "<a href=\"https://developer.microsoft.com/microsoft-edge/webview2/\">developer.microsoft.com/microsoft-edge/webview2</a>")
                                   .arg(reason.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
        m_unavailable->setTextFormat(Qt::RichText);
        m_stack->setCurrentWidget(m_unavailable);
        setNavigationEnabled(false);
    });
    connect(m_host, &WebView2Host::urlChanged, this, [this](const QString& u) { m_url->setText(u); });
    connect(m_host, &WebView2Host::loadingChanged, this, [this](bool on) {
        m_loading->setGeometry(0, 0, m_stack->width(), 3);
        m_loading->setVisible(on);
        m_loading->raise();
    });
    connect(m_host, &WebView2Host::historyChanged, this, [this] { updateButtons(); });
}

QWidget* OnlineBrowserView::viewport() const { return m_host; }

void OnlineBrowserView::setAdBlock(AdBlockService* service)
{
    m_service = service;
    if (m_host)
        m_host->setAdBlock(service);
    if (m_service)
        connect(m_service, &AdBlockService::changed, this, &OnlineBrowserView::updateAdBlockUi);
    updateAdBlockUi();
}

void OnlineBrowserView::updateAdBlockUi()
{
    const bool on = (m_host && m_host->isReady()) ? m_host->isExtensionEnabled()
                                                  : (m_service ? m_service->enabled() : true);
    if (m_adsOn)
        m_adsOn->setChecked(on);
    if (m_ads) {
        m_ads->setText(on ? QStringLiteral("\U0001F6E1 uBlock Origin: On") : QStringLiteral("\U0001F6E1 uBlock Origin: Off"));
        m_ads->setToolTip(on ? QStringLiteral("uBlock Origin 1.76.2 is active. Website ads and trackers are filtered.")
                             : QStringLiteral("uBlock Origin is paused. Click for options."));
        m_ads->setStyleSheet(on ? QStringLiteral("QPushButton{background:#DCFCE7; color:#166534; border:1px solid #86EFAC; font-size:11px; font-weight:600; padding:2px 8px;} QPushButton::menu-indicator{width:0;}")
                                : QStringLiteral("QPushButton{background:#F1F5F9; color:#64748B; border:1px solid #CBD5E1; font-size:11px; padding:2px 8px;} QPushButton::menu-indicator{width:0;}"));
    }
}

bool OnlineBrowserView::isUnavailable() const { return m_host->state() == WebView2Host::State::Unavailable; }

void OnlineBrowserView::teardown()
{
    if (m_host)
        m_host->shutdown();
    setNavigationEnabled(false);
    m_url->clear();
    m_stack->setCurrentWidget(m_host);
}

void OnlineBrowserView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_host->state() == WebView2Host::State::Unavailable)
        m_host->retry();
    else
        m_host->initialize();
    m_loading->setGeometry(0, 0, m_stack->width(), 3);
}

void OnlineBrowserView::navigateFromBox()
{
    if (m_host->isReady())
        m_host->navigate(m_url->text());
}

void OnlineBrowserView::setNavigationEnabled(bool on)
{
    for (QPushButton* b : m_nav)
        b->setEnabled(on);
    m_url->setEnabled(on);
    m_url->setToolTip(on ? QString() : QStringLiteral("The browser is starting or not available"));
    if (on)
        updateButtons();
}

void OnlineBrowserView::updateButtons()
{
    if (!m_host->isReady())
        return;
    m_back->setEnabled(m_host->canGoBack());
    m_forward->setEnabled(m_host->canGoForward());
}

} // namespace Lux
