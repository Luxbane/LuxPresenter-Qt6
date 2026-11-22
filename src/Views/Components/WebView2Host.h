#pragma once

#include <QString>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>

namespace Lux {
class AdBlockService;

// Microsoft Edge WebView2 embedded in a native child window (the Online browser's page).
// The browser runs with full uBlock Origin extension (Manifest V2) enabled in a persistent profile.
// On application exit, website browsing data (cookies, cache, history) is automatically cleared
// while strictly preserving extension settings, filter lists, and storage.
// The WebView2 Runtime is an external dependency (Evergreen runtime, or a fixed runtime in <app>\Engines\WebView2);
// when it is missing the host reports it through unavailable() instead of failing.
class WebView2Host : public QWidget
{
    Q_OBJECT
public:
    enum class State { NotStarted, Starting, Ready, Unavailable };

    explicit WebView2Host(QWidget* parent = nullptr);
    ~WebView2Host() override;

    static bool builtWithSdk();
    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    QString unavailableReason() const { return m_reason; }

    void initialize();                       // idempotent; asynchronous
    void retry();                            // resets and re-attempts initialization if unavailable
    void shutdown();                         // closes controller, terminates msedgewebview2 processes, frees RAM
    void navigate(const QString& userInput); // URL or search text (see normalizeBrowserInput)
    void loadUrl(const QString& url);        // navigates to the URL exactly as given (no address-bar rules)
    void goHome();
    void goBack();
    void goForward();
    void reload();
    void stop();
    bool canGoBack() const;
    bool canGoForward() const;
    QString currentUrl() const;              // "" for the home page
    bool isPrivate() const;                  // asks the browser profile (false for persistent profile)
    QString userDataFolder() const { return m_userData; }
    // Where the browser engine comes from once ready: the runtime folder, or "Evergreen" for the registered system runtime.
    QString runtimeSource() const { return m_source; }
    QString browserVersion() const { return m_version; }
    // Folders tried in order ("" = the registered Evergreen runtime): bundled fixed runtime (Engines\WebView2, WebView2,
    // runtimes\WebView2), Evergreen, then an unregistered runtime under Program Files\Microsoft\EdgeWebView\Application.
    // The environment variable WEBVIEW2_BROWSER_EXECUTABLE_FOLDER, when set, is the only candidate.
    static QStringList runtimeCandidates(const QString& appDir);
    static QString newestRuntimeIn(const QString& folder);
    static void setApplicationDirectoryOverride(const QString& dir);   // tests: where Engines\WebView2 etc. are looked for
    // Runs a script in the page and returns the JSON-encoded result (test aid).
    void evaluate(const QString& script, std::function<void(const QString&)> callback);

    // uBlock Origin extension management
    bool isExtensionEnabled() const;
    void setExtensionEnabled(bool on);
    QString extensionId() const;
    void openExtensionDashboard();

    // Data cleanup on exit: clears cookies, cache, history while preserving extension storage
    void cleanupBrowsingDataSync(int timeoutMs = 2500);

    // Deprecated compatibility stub (ad blocking is handled by uBlock Origin extension)
    void setAdBlock(AdBlockService* = nullptr) {}

signals:
    void ready();
    void unavailable(const QString& reason);
    void urlChanged(const QString& url);
    void loadingChanged(bool loading);
    void historyChanged();
    void extensionStatusChanged(bool enabled);

protected:
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void moveEvent(QMoveEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void focusInEvent(QFocusEvent*) override;

private:
    struct Impl;
    void fail(const QString& reason);
    void controllerCreated();
    void startEnvironment(int index);
    void initializeExtension();
    void completeStartup();
    void syncBounds();

    std::unique_ptr<Impl> d;
    State m_state = State::NotStarted;
    QString m_reason;
    QString m_userData;
    QString m_source, m_version;
    QStringList m_candidates;
    QStringList m_tried;
    QString m_current;
    QString m_nav;
    bool m_loading = false;
};

} // namespace Lux
