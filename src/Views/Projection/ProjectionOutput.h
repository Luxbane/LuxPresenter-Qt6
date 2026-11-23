#pragma once

#include "Services/ProjectionComposer.h"
#include "Services/ProjectionService.h"
#include "Views/Projection/DualDeckVideo.h"
#include "Views/Projection/ProjectionWindow.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

namespace Lux {

// Black window behind the projection window; hosts the two video decks (ProjectionBackgroundWindow.xaml).
class ProjectionBackgroundWindow : public QWidget
{
    Q_OBJECT
public:
    ProjectionBackgroundWindow();
    DualDeckVideo* decks() const { return m_decks; }

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    DualDeckVideo* m_decks = nullptr;
};

// The real projector output: the window pair + video decks, driven by a ProjectionComposer.
class ProjectionOutput : public QObject, public IProjectionOutput
{
    Q_OBJECT
public:
    using ScreenProvider = std::function<QList<ScreenInfo>()>;

    // `screenProvider` defaults to the Win32 monitor enumeration; tests inject small fake rectangles.
    explicit ProjectionOutput(ScreenProvider screenProvider = {}, std::function<void()> activateMain = {});
    ~ProjectionOutput() override;

    QList<ScreenInfo> screens() const override;
    bool show(const ScreenInfo& screen) override;
    void hide() override;
    bool isShown() const override { return m_shown; }
    void setMargins(int top, int bottom, int left, int right) override;
    void update(const ProjectionState& state) override;
    void seekVideo(double seconds) override;
    double videoPosition() const override;
    double videoDuration() const override;
    void preWarm() override;
    void preloadBackground(const QString& bg) override;

    // Re-reads the current monitor description (resolution/scaling/position) and re-fits the windows.
    void refreshPlacement() { reassertPlacement(); }

    ProjectionComposer& composer() { return m_composer; }
    ProjectionWindow* foreground() const { return m_fg; }
    ProjectionBackgroundWindow* background() const { return m_bg; }
    DualDeckVideo* decks() const { return m_bg ? m_bg->decks() : nullptr; }
    ScreenInfo currentScreen() const { return m_screen; }

private:
    bool eventFilter(QObject* watched, QEvent* e) override;
    void ensureWindows();
    void reassertPlacement();
    void tick();

    ScreenProvider m_screens;
    std::function<void()> m_activateMain;
    ProjectionComposer m_composer;
    ProjectionBackgroundWindow* m_bg = nullptr;
    ProjectionWindow* m_fg = nullptr;
    QTimer m_anim;
    ScreenInfo m_screen;
    bool m_reassertQueued = false;
    bool m_inShow = false;
    bool m_shown = false;
    bool m_marginsSet = false;
    int m_mTop = 0, m_mBottom = 0, m_mLeft = 0, m_mRight = 0;
};

} // namespace Lux
