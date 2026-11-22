#include "Views/Components/WebView2Host.h"

#include "Services/AppPaths.h"
#include "Services/UrlUtil.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QLibrary>
#include <QPointer>
#include <QVersionNumber>

#include <algorithm>

#ifdef LUX_HAS_WEBVIEW2
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif
#include "WebView2EnvironmentOptions.h"
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

namespace Lux {

namespace {
QString homeHtml()
{
    QFile f(QStringLiteral(":/online_home.html"));
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("<html><body style=\"background:#202124;color:#9AA0A6\">Online Browser</body></html>");
    QString s = QString::fromUtf8(f.readAll());
    if (s.startsWith(QChar(0xFEFF)))
        s.remove(0, 1);
    return s;
}

QString g_appDirOverride;
QString appDirectory() { return g_appDirOverride.isEmpty() ? QCoreApplication::applicationDirPath() : g_appDirOverride; }

QString findUBlockExtensionPath()
{
    const QString appDir = appDirectory();
    const QString baseDir = AppPaths::baseDirectory();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("Extensions/uBlockOrigin")),
        QDir(appDir).filePath(QStringLiteral("Portable/Extensions/uBlockOrigin")),
        QDir(baseDir).filePath(QStringLiteral("Portable/Extensions/uBlockOrigin")),
        QDir(baseDir).filePath(QStringLiteral("Extensions/uBlockOrigin")),
        QStringLiteral("E:/LuxPresenter-QT6/Portable/Extensions/uBlockOrigin")
    };
    for (const QString& path : candidates) {
        if (QFile::exists(QDir(path).filePath(QStringLiteral("manifest.json")))) {
            return QDir::toNativeSeparators(QDir::cleanPath(path));
        }
    }
    return {};
}

} // namespace

void WebView2Host::setApplicationDirectoryOverride(const QString& dir) { g_appDirOverride = dir; }

QString WebView2Host::newestRuntimeIn(const QString& folder)
{
    const QDir d(folder);
    if (!d.exists())
        return {};
    if (d.exists(QStringLiteral("msedgewebview2.exe")))
        return d.absolutePath();
    QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    std::sort(subs.begin(), subs.end(), [](const QString& a, const QString& b) { return QVersionNumber::fromString(a) > QVersionNumber::fromString(b); });
    for (const QString& sub : std::as_const(subs))
        if (QDir(d.filePath(sub)).exists(QStringLiteral("msedgewebview2.exe")))
            return d.filePath(sub);
    return {};
}

QStringList WebView2Host::runtimeCandidates(const QString& appDir)
{
    if (!qEnvironmentVariable("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER").isEmpty())
        return {QString()};   // an explicit override is honoured exclusively
    QStringList list;
    const QDir app(appDir);
    for (const char* rel : {"Engines/WebView2", "WebView2", "runtimes/WebView2"}) {
        const QString f = newestRuntimeIn(app.filePath(QLatin1String(rel)));
        if (!f.isEmpty() && !list.contains(f))
            list << f;
    }
    list << QString();   // registered Evergreen runtime
    // Like the reference: a runtime that is installed under Program Files but not registered is still used.
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"}) {
        const QString pf = qEnvironmentVariable(var);
        if (pf.isEmpty())
            continue;
        const QString f = newestRuntimeIn(QDir(pf).filePath(QStringLiteral("Microsoft/EdgeWebView/Application")));
        if (!f.isEmpty() && !list.contains(f))
            list << f;
    }
    return list;
}

#ifdef LUX_HAS_WEBVIEW2

struct WebView2Host::Impl
{
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    ComPtr<ICoreWebView2Profile7> profile7;
    ComPtr<ICoreWebView2BrowserExtension> uboExtension;
    EventRegistrationToken tokStart{}, tokDone{}, tokHistory{}, tokNewWindow{};
    QString uboId;
    bool uboEnabled = false;
    bool cleanedUp = false;
};

bool WebView2Host::builtWithSdk() { return true; }

static QString takeString(LPWSTR s)
{
    if (!s)
        return {};
    const QString q = QString::fromWCharArray(s);
    CoTaskMemFree(s);
    return q;
}

WebView2Host::WebView2Host(QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(100, 60);
}

WebView2Host::~WebView2Host()
{
    shutdown();
}

void WebView2Host::shutdown()
{
    if (m_state == State::NotStarted)
        return;
    cleanupBrowsingDataSync(500);
    if (d->view) {
        d->view->remove_NavigationStarting(d->tokStart);
        d->view->remove_NavigationCompleted(d->tokDone);
        d->view->remove_HistoryChanged(d->tokHistory);
        d->view->remove_NewWindowRequested(d->tokNewWindow);
    }
    if (d->controller)
        d->controller->Close();
    d->view.Reset();
    d->controller.Reset();
    d->profile7.Reset();
    d->uboExtension.Reset();
    d->env.Reset();
    m_state = State::NotStarted;
    m_tried.clear();
    m_reason.clear();
    m_current.clear();
    m_loading = false;
}

void WebView2Host::fail(const QString& reason)
{
    m_state = State::Unavailable;
    m_reason = reason;
    emit unavailable(reason);
}

void WebView2Host::retry()
{
    m_state = State::NotStarted;
    m_tried.clear();
    m_reason.clear();
    initialize();
}

void WebView2Host::initialize()
{
    if (m_state != State::NotStarted)
        return;
    m_state = State::Starting;

#ifdef LUX_HAS_WEBVIEW2
    OleInitialize(nullptr);
#endif

    static QLibrary loader(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WebView2Loader.dll")));
    if (!loader.isLoaded() && !loader.load()) {
        fail(QStringLiteral("WebView2Loader.dll could not be loaded (%1).").arg(loader.errorString()));
        return;
    }

    // Persistent profile: keep uBlock Origin extension settings, rules, and cache intact across sessions.
    // Website data (cookies, history) is cleanly cleared on application shutdown.
    m_userData = QDir(AppPaths::dataDirectory()).filePath(QStringLiteral("WebView2Data"));
    QDir().mkpath(m_userData);
    m_candidates = runtimeCandidates(appDirectory());
    startEnvironment(0);
}

void WebView2Host::startEnvironment(int index)
{
#ifdef LUX_HAS_WEBVIEW2
    OleInitialize(nullptr);
#endif
    using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    static QLibrary loader(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WebView2Loader.dll")));
    auto createEnv = reinterpret_cast<CreateEnvFn>(loader.resolve("CreateCoreWebView2EnvironmentWithOptions"));
    if (!createEnv) {
        fail(QStringLiteral("WebView2Loader.dll does not provide CreateCoreWebView2EnvironmentWithOptions."));
        return;
    }
    const QString folder = m_candidates.value(index);
    m_tried << (folder.isEmpty() ? QStringLiteral("registered WebView2 Runtime") : QDir::toNativeSeparators(folder));

    QPointer<WebView2Host> self(this);
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const std::wstring userData = QDir::toNativeSeparators(m_userData).toStdWString();
    const std::wstring folderW = QDir::toNativeSeparators(folder).toStdWString();

    auto next = [self, index](const QString& why) {
        if (!self)
            return;
        if (index + 1 < self->m_candidates.size()) {
            self->startEnvironment(index + 1);
            return;
        }
        self->fail(why + QStringLiteral("\nTried: ") + self->m_tried.join(QStringLiteral("; ")) +
                   QStringLiteral(".\nThe Portable package normally contains the runtime in Engines\\WebView2; otherwise install "
                                  "the Evergreen WebView2 Runtime."));
    };

    // Enable browser extensions (required for full uBlock Origin MV2 support) and disable bloat features
    auto envOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (envOptions) {
        envOptions->put_AreBrowserExtensionsEnabled(TRUE);
        envOptions->put_AdditionalBrowserArguments(
            L"--disable-features=msEdgeSidebarV2,msHub,msEdgeShopping,msEdgeFeed,msEdgeCopilot,Translate "
            L"--disable-background-networking --disable-sync --no-pings");
    }

    const HRESULT hr = createEnv(
        folder.isEmpty() ? nullptr : folderW.c_str(), userData.c_str(), envOptions.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [self, hwnd, next, folder](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (!self)
                    return S_OK;
                if (FAILED(result) || !env) {
                    next(QStringLiteral("The Microsoft Edge WebView2 Runtime could not be started (0x%1).").arg(quint32(result), 8, 16, QLatin1Char('0')));
                    return S_OK;
                }
                self->d->env = env;
                ComPtr<ICoreWebView2Environment10> env10;
                if (FAILED(self->d->env.As(&env10))) {
                    next(QStringLiteral("The WebView2 Runtime does not support the required environment features."));
                    return S_OK;
                }
                LPWSTR ver = nullptr;
                env->get_BrowserVersionString(&ver);
                self->m_version = takeString(ver);
                self->m_source = folder.isEmpty() ? QStringLiteral("Evergreen") : folder;
                ComPtr<ICoreWebView2ControllerOptions> options;
                if (FAILED(env10->CreateCoreWebView2ControllerOptions(&options)) || !options) {
                    self->fail(QStringLiteral("The WebView2 controller options could not be created."));
                    return S_OK;
                }
                // Persistent profile: do NOT enable InPrivate mode so extensions operate properly
                env10->CreateCoreWebView2ControllerWithOptions(
                    hwnd, options.Get(),
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [self](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!self)
                                return S_OK;
                            if (FAILED(r) || !controller) {
                                self->fail(QStringLiteral("The WebView2 browser window could not be created (0x%1).")
                                               .arg(quint32(r), 8, 16, QLatin1Char('0')));
                                return S_OK;
                            }
                            self->d->controller = controller;
                            self->controllerCreated();
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());
    if (FAILED(hr))
        next(QStringLiteral("The Microsoft Edge WebView2 Runtime was not found (0x%1).").arg(quint32(hr), 8, 16, QLatin1Char('0')));
}

void WebView2Host::controllerCreated()
{
    d->controller->get_CoreWebView2(&d->view);
    if (!d->view) {
        fail(QStringLiteral("The WebView2 control could not be initialised."));
        return;
    }
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(d->view->get_Settings(&settings)) && settings) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
    }
    QPointer<WebView2Host> self(this);
    d->view->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>([self](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            if (!self)
                return S_OK;
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const QString u = takeString(uri);
            self->m_nav = u;
            self->m_current = isHomeUrl(u) ? QString() : u;
            self->m_loading = true;
            emit self->loadingChanged(true);
            emit self->urlChanged(self->m_current);
            return S_OK;
        }).Get(), &d->tokStart);
    d->view->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>([self](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
            if (!self)
                return S_OK;
            LPWSTR src = nullptr;
            sender->get_Source(&src);
            const QString u = takeString(src);
            self->m_current = isHomeUrl(u) ? QString() : u;
            self->m_loading = false;
            emit self->loadingChanged(false);
            emit self->urlChanged(self->m_current);
            emit self->historyChanged();
            return S_OK;
        }).Get(), &d->tokDone);
    d->view->add_HistoryChanged(
        Callback<ICoreWebView2HistoryChangedEventHandler>([self](ICoreWebView2*, IUnknown*) -> HRESULT {
            if (self)
                emit self->historyChanged();
            return S_OK;
        }).Get(), &d->tokHistory);
    // Pop-ups and target=_blank links: unprompted pop-ups without a user gesture are suppressed.
    // User-initiated new windows navigate in the same view: the browser never spawns desktop windows.
    d->view->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>([self](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            args->put_Handled(TRUE);
            BOOL byUser = FALSE;
            args->get_IsUserInitiated(&byUser);
            if (!byUser) {
                // Suppress unprompted popup opened without user interaction
                return S_OK;
            }
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const QString u = takeString(uri);
            if (self && !u.isEmpty())
                sender->Navigate(reinterpret_cast<LPCWSTR>(u.utf16()));
            return S_OK;
        }).Get(), &d->tokNewWindow);

    // Suppress unsolicited window.open() spawned by page scripts without a user gesture
    const wchar_t* popupGuardScript =
        L"(function(){try{"
        L"var o=window.open;"
        L"window.open=function(){"
        L"if(navigator.userActivation&&navigator.userActivation.isActive){return o.apply(window,arguments);}"
        L"return null;};"
        L"}catch(e){}})();";
    d->view->AddScriptToExecuteOnDocumentCreated(popupGuardScript, nullptr);

    initializeExtension();
}

void WebView2Host::initializeExtension()
{
    ComPtr<ICoreWebView2_13> view13;
    if (FAILED(d->view.As(&view13)) || !view13) {
        completeStartup();
        return;
    }
    ComPtr<ICoreWebView2Profile> profile;
    if (FAILED(view13->get_Profile(&profile)) || !profile) {
        completeStartup();
        return;
    }
    if (FAILED(profile.As(&d->profile7)) || !d->profile7) {
        completeStartup();
        return;
    }

    QPointer<WebView2Host> self(this);
    d->profile7->GetBrowserExtensions(
        Callback<ICoreWebView2ProfileGetBrowserExtensionsCompletedHandler>(
            [self](HRESULT hr, ICoreWebView2BrowserExtensionList* list) -> HRESULT {
                if (!self)
                    return S_OK;
                if (SUCCEEDED(hr) && list) {
                    UINT32 count = 0;
                    list->get_Count(&count);
                    for (UINT32 i = 0; i < count; ++i) {
                        ComPtr<ICoreWebView2BrowserExtension> ext;
                        list->GetValueAtIndex(i, &ext);
                        if (!ext)
                            continue;
                        LPWSTR id = nullptr;
                        LPWSTR name = nullptr;
                        ext->get_Id(&id);
                        ext->get_Name(&name);
                        const QString extId = takeString(id);
                        const QString extName = takeString(name);
                        if (extId.compare(QStringLiteral("klcpjomcfmclkggljbohjomhjclfbclp"), Qt::CaseInsensitive) == 0
                            || extName.contains(QStringLiteral("uBlock"), Qt::CaseInsensitive)) {
                            self->d->uboExtension = ext;
                            self->d->uboId = extId;
                            BOOL enabled = FALSE;
                            ext->get_IsEnabled(&enabled);
                            if (!enabled)
                                ext->Enable(TRUE, nullptr);
                            self->d->uboEnabled = true;
                            self->completeStartup();
                            return S_OK;
                        }
                    }
                }

                // Not yet installed: find extension directory and install it
                const QString extPath = findUBlockExtensionPath();
                if (extPath.isEmpty()) {
                    self->completeStartup();
                    return S_OK;
                }

                const std::wstring extPathW = extPath.toStdWString();
                self->d->profile7->AddBrowserExtension(
                    extPathW.c_str(),
                    Callback<ICoreWebView2ProfileAddBrowserExtensionCompletedHandler>(
                        [self](HRESULT hrAdd, ICoreWebView2BrowserExtension* ext) -> HRESULT {
                            if (!self)
                                return S_OK;
                            if (SUCCEEDED(hrAdd) && ext) {
                                self->d->uboExtension = ext;
                                LPWSTR id = nullptr;
                                ext->get_Id(&id);
                                self->d->uboId = takeString(id);
                                BOOL enabled = FALSE;
                                ext->get_IsEnabled(&enabled);
                                if (!enabled)
                                    ext->Enable(TRUE, nullptr);
                                self->d->uboEnabled = true;
                            }
                            self->completeStartup();
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());
}

void WebView2Host::completeStartup()
{
    m_state = State::Ready;
    syncBounds();
    d->controller->put_IsVisible(isVisible() ? TRUE : FALSE);
    goHome();
    emit ready();
    emit extensionStatusChanged(d->uboEnabled);
}

void WebView2Host::syncBounds()
{
    if (!d->controller)
        return;
    RECT r{};
    GetClientRect(reinterpret_cast<HWND>(winId()), &r);   // physical pixels
    d->controller->put_Bounds(r);
    d->controller->NotifyParentWindowPositionChanged();
}

void WebView2Host::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    syncBounds();
}

void WebView2Host::moveEvent(QMoveEvent* e)
{
    QWidget::moveEvent(e);
    syncBounds();
}

void WebView2Host::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    window()->installEventFilter(this);   // follow moves of the top-level window
    if (d->controller) {
        d->controller->put_IsVisible(TRUE);
        syncBounds();
    }
}

void WebView2Host::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if (d->controller)
        d->controller->put_IsVisible(FALSE);   // a hidden page does not render
}

bool WebView2Host::eventFilter(QObject* o, QEvent* e)
{
    if (o == window() && (e->type() == QEvent::Move || e->type() == QEvent::Resize) && d->controller)
        d->controller->NotifyParentWindowPositionChanged();
    return QWidget::eventFilter(o, e);
}

void WebView2Host::focusInEvent(QFocusEvent* e)
{
    QWidget::focusInEvent(e);
    if (d->controller)
        d->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void WebView2Host::navigate(const QString& userInput)
{
    const QString url = normalizeBrowserInput(userInput);
    if (url.isEmpty()) {
        goHome();
        return;
    }
    if (d->view)
        d->view->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
}

void WebView2Host::loadUrl(const QString& url)
{
    if (d->view)
        d->view->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
}

void WebView2Host::goHome()
{
    if (!d->view)
        return;
    m_current.clear();
    emit urlChanged(QString());
    const QString html = homeHtml();
    d->view->NavigateToString(reinterpret_cast<LPCWSTR>(html.utf16()));
}

void WebView2Host::goBack() { if (d->view) d->view->GoBack(); }
void WebView2Host::goForward() { if (d->view) d->view->GoForward(); }
void WebView2Host::reload() { if (d->view) d->view->Reload(); }
void WebView2Host::stop() { if (d->view) d->view->Stop(); }

bool WebView2Host::canGoBack() const
{
    BOOL b = FALSE;
    return d->view && SUCCEEDED(d->view->get_CanGoBack(&b)) && b;
}

bool WebView2Host::canGoForward() const
{
    BOOL b = FALSE;
    return d->view && SUCCEEDED(d->view->get_CanGoForward(&b)) && b;
}

QString WebView2Host::currentUrl() const { return m_current; }

bool WebView2Host::isPrivate() const
{
    // The profile is persistent to support extensions.
    ComPtr<ICoreWebView2_13> v13;
    if (!d->view || FAILED(d->view.As(&v13)))
        return false;
    ComPtr<ICoreWebView2Profile> profile;
    BOOL priv = FALSE;
    return SUCCEEDED(v13->get_Profile(&profile)) && profile && SUCCEEDED(profile->get_IsInPrivateModeEnabled(&priv)) && priv;
}

bool WebView2Host::isExtensionEnabled() const
{
    return d ? d->uboEnabled : false;
}

void WebView2Host::setExtensionEnabled(bool on)
{
    if (d && d->uboExtension) {
        d->uboEnabled = on;
        QPointer<WebView2Host> self(this);
        d->uboExtension->Enable(
            on ? TRUE : FALSE,
            Callback<ICoreWebView2BrowserExtensionEnableCompletedHandler>(
                [self, on](HRESULT) -> HRESULT {
                    if (self) {
                        self->d->uboEnabled = on;
                        emit self->extensionStatusChanged(on);
                    }
                    return S_OK;
                })
                .Get());
        emit extensionStatusChanged(on);
    }
}

QString WebView2Host::extensionId() const
{
    return d->uboId;
}

void WebView2Host::openExtensionDashboard()
{
    if (!d->uboId.isEmpty() && d->view) {
        const QString uboUrl = QStringLiteral("chrome-extension://%1/dashboard.html").arg(d->uboId);
        loadUrl(uboUrl);
    }
}

void WebView2Host::cleanupBrowsingDataSync(int timeoutMs)
{
    if (d->cleanedUp || !d->view)
        return;
    d->cleanedUp = true;

    ComPtr<ICoreWebView2_13> view13;
    if (FAILED(d->view.As(&view13)))
        return;
    ComPtr<ICoreWebView2Profile> profile;
    if (FAILED(view13->get_Profile(&profile)) || !profile)
        return;
    ComPtr<ICoreWebView2Profile2> prof2;
    if (FAILED(profile.As(&prof2)) || !prof2)
        return;

    // Safely clear website browsing data while preserving uBlock Origin extension state:
    // 1. COOKIES, DISK_CACHE, BROWSING_HISTORY, DOWNLOAD_HISTORY, GENERAL_AUTOFILL, PASSWORD_AUTOSAVE:
    //    Standard website browsing artifacts.
    // 2. LOCAL_STORAGE, CACHE_STORAGE, SERVICE_WORKERS, WEB_SQL:
    //    Safely clears website DOM storage, PWA cache storage, and website service workers.
    //    Chromium stores extension settings separately in 'Local Extension Settings' (leveldb),
    //    which is untouched by LOCAL_STORAGE. uBO MV2 does not use Service Workers or CacheStorage.
    // 3. INDEXED_DB, ALL_DOM_STORAGE, ALL_SITE, SETTINGS, ALL_PROFILE are INTENTIONALLY EXCLUDED:
    //    WebView2 lacks origin-scoped ClearBrowsingData. Calling ClearBrowsingData with INDEXED_DB
    //    wipes uBlock Origin's compiled rule cache (chrome-extension_..._0.indexeddb), forcing uBO
    //    to re-parse all filter assets from scratch on restart and causing initial ad request leaks.
    COREWEBVIEW2_BROWSING_DATA_KINDS kinds =
        COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES |
        COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE |
        COREWEBVIEW2_BROWSING_DATA_KINDS_BROWSING_HISTORY |
        COREWEBVIEW2_BROWSING_DATA_KINDS_DOWNLOAD_HISTORY |
        COREWEBVIEW2_BROWSING_DATA_KINDS_GENERAL_AUTOFILL |
        COREWEBVIEW2_BROWSING_DATA_KINDS_PASSWORD_AUTOSAVE |
        COREWEBVIEW2_BROWSING_DATA_KINDS_LOCAL_STORAGE |
        COREWEBVIEW2_BROWSING_DATA_KINDS_CACHE_STORAGE |
        COREWEBVIEW2_BROWSING_DATA_KINDS_SERVICE_WORKERS |
        COREWEBVIEW2_BROWSING_DATA_KINDS_WEB_SQL;

    bool done = false;
    HRESULT hr = prof2->ClearBrowsingData(
        kinds,
        Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
            [&done](HRESULT) -> HRESULT {
                done = true;
                return S_OK;
            })
            .Get());
    if (FAILED(hr))
        return;

    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        Sleep(10);
    }
}

void WebView2Host::evaluate(const QString& script, std::function<void(const QString&)> callback)
{
    if (!d->view) {
        callback(QString());
        return;
    }
    d->view->ExecuteScript(reinterpret_cast<LPCWSTR>(script.utf16()),
                           Callback<ICoreWebView2ExecuteScriptCompletedHandler>([callback](HRESULT, LPCWSTR result) -> HRESULT {
                               callback(result ? QString::fromWCharArray(result) : QString());
                               return S_OK;
                            }).Get());
}

#else // built without the WebView2 SDK

struct WebView2Host::Impl {};

bool WebView2Host::builtWithSdk() { return false; }
WebView2Host::WebView2Host(QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>()) {}
WebView2Host::~WebView2Host() = default;
void WebView2Host::shutdown() { m_state = State::NotStarted; }
void WebView2Host::fail(const QString& reason) { m_state = State::Unavailable; m_reason = reason; emit unavailable(reason); }
void WebView2Host::initialize()
{
    if (m_state == State::NotStarted)
        fail(QStringLiteral("This build of LuxPresenter does not include the WebView2 SDK, so the Online browser is unavailable."));
}
void WebView2Host::controllerCreated() {}
void WebView2Host::startEnvironment(int) {}
void WebView2Host::initializeExtension() {}
void WebView2Host::completeStartup() {}
void WebView2Host::syncBounds() {}
void WebView2Host::resizeEvent(QResizeEvent* e) { QWidget::resizeEvent(e); }
void WebView2Host::showEvent(QShowEvent* e) { QWidget::showEvent(e); }
void WebView2Host::hideEvent(QHideEvent* e) { QWidget::hideEvent(e); }
void WebView2Host::moveEvent(QMoveEvent* e) { QWidget::moveEvent(e); }
bool WebView2Host::eventFilter(QObject* o, QEvent* e) { return QWidget::eventFilter(o, e); }
void WebView2Host::focusInEvent(QFocusEvent* e) { QWidget::focusInEvent(e); }
void WebView2Host::navigate(const QString&) {}
void WebView2Host::loadUrl(const QString&) {}
void WebView2Host::goHome() {}
void WebView2Host::goBack() {}
void WebView2Host::goForward() {}
void WebView2Host::reload() {}
void WebView2Host::stop() {}
bool WebView2Host::canGoBack() const { return false; }
bool WebView2Host::canGoForward() const { return false; }
QString WebView2Host::currentUrl() const { return m_current; }
bool WebView2Host::isPrivate() const { return false; }
bool WebView2Host::isExtensionEnabled() const { return false; }
void WebView2Host::setExtensionEnabled(bool) {}
QString WebView2Host::extensionId() const { return {}; }
void WebView2Host::openExtensionDashboard() {}
void WebView2Host::cleanupBrowsingDataSync(int) {}
void WebView2Host::evaluate(const QString&, std::function<void(const QString&)> callback) { callback(QString()); }

#endif

} // namespace Lux
