#pragma once

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QMenu;
class QStackedWidget;
class QAction;

namespace Lux {

class AdBlockService;
class WebView2Host;

// Full-width Online browser page (OnlineBrowserView.xaml): navigation toolbar + Microsoft WebView2 viewport.
// The browser runs with full uBlock Origin 1.76.2 extension enabled.
class OnlineBrowserView : public QWidget
{
    Q_OBJECT
public:
    explicit OnlineBrowserView(QWidget* parent = nullptr);

    QPushButton* closeButton() const { return m_close; }
    QLineEdit* urlBox() const { return m_url; }
    QWidget* viewport() const;
    WebView2Host* host() const { return m_host; }
    // home, back, forward, refresh, stop, go, YouTube
    QList<QPushButton*> navigationButtons() const { return m_nav; }
    QLabel* unavailableLabel() const { return m_unavailable; }
    QProgressBar* loadingBar() const { return m_loading; }
    bool isUnavailable() const;
    void teardown();

    // Ad blocker / uBlock Origin control
    void setAdBlock(AdBlockService* service);
    QPushButton* adBlockButton() const { return m_ads; }
    QMenu* adBlockMenu() const { return m_adsMenu; }

signals:
    void adBlockEdited();   // on/off was changed
    void closeRequested();

protected:
    void showEvent(QShowEvent*) override;

private:
    void navigateFromBox();
    void updateButtons();
    void setNavigationEnabled(bool on);
    void updateAdBlockUi();

    QLineEdit* m_url;
    QPushButton* m_close;
    QStackedWidget* m_stack;
    WebView2Host* m_host;
    QLabel* m_unavailable;
    QProgressBar* m_loading;
    QList<QPushButton*> m_nav;
    QPushButton *m_back, *m_forward;
    QPushButton* m_ads = nullptr;
    QMenu* m_adsMenu = nullptr;
    QAction *m_adsOn = nullptr, *m_adsDashboard = nullptr, *m_adsStatus = nullptr;
    AdBlockService* m_service = nullptr;
};

} // namespace Lux
