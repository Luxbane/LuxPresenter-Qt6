#pragma once

#include "Models/AppSettings.h"
#include "Services/Displays.h"
#include "Services/ProjectionState.h"

#include <QObject>

#include <functional>
#include <memory>

namespace Lux {

// The physical projector output (windows + video decks). The service talks to it only through this
// interface so state/synchronisation logic can be tested with a fake.
class IProjectionOutput
{
public:
    virtual ~IProjectionOutput() = default;
    virtual QList<ScreenInfo> screens() const = 0;
    // Shows the output on `screen`; the first state pushed afterwards is applied without transitions.
    virtual bool show(const ScreenInfo& screen) = 0;
    virtual void hide() = 0;
    virtual bool isShown() const = 0;
    virtual void setMargins(int top, int bottom, int left, int right) = 0;
    virtual void update(const ProjectionState& state) = 0;
    virtual void seekVideo(double /*seconds*/) {}
    virtual double videoPosition() const { return 0; }
    virtual double videoDuration() const { return 0; }
    virtual void preWarm() {}
    virtual void preloadBackground(const QString& /*bg*/) {}
};

// Port of ProjectionService: owns the projection state and keeps the output in sync with it.
class ProjectionService : public QObject
{
    Q_OBJECT
public:
    using OutputFactory = std::function<std::unique_ptr<IProjectionOutput>()>;

    explicit ProjectionService(QObject* parent = nullptr);
    ~ProjectionService() override;

    void setOutputFactory(OutputFactory factory);

    const ProjectionState& state() const { return m_state; }
    void setSettings(const AppSettings& settings);

    // Displays
    QList<ScreenInfo> displays() const;
    int selectedMonitorIndex() const { return m_selectedMonitor; }   // -1 = automatic
    void setSelectedMonitorIndex(int index);
    // 1-based number of the screen currently showing the output, 0 if the output is hidden.
    int activeScreenNumber() const { return m_activeScreen; }
    QString displayStatusText() const;

    // Output window
    void showWindow();
    void hideWindow();
    bool isWindowShown() const;
    void preWarm();
    void preloadBackground(const QString& bg);
    // Monitors were added/removed: re-choose the target monitor for a visible output.
    void refreshOutput() { if (isWindowShown()) showWindow(); }

    // Live state
    void setLiveSlide(const std::optional<Slide>& slide, const QString& background);
    void setBackground(const QString& background);
    void toggleLive(bool live);
    void toggleBlack(bool black);
    void toggleClear(bool clear);
    void toggleLogo(bool logo);
    void setLogoPath(const QString& path);

    // Presentation documents: the page (0-based) of a PDF is shown on the output, aspect ratio preserved.
    void setLivePresentation(const QString& pdfPath, int page);
    void navigatePresentation(int page);
    void closePresentation();

    // Standalone video (engine-level; the Media tab UI arrives in a later milestone)
    void playMedia(const QString& path, bool looping, bool playing = true);
    void setMediaPlaying(bool playing);
    void stopMedia();
    void seekMedia(double seconds);
    double mediaPosition() const;
    double mediaDuration() const;
    void setMediaVolume(double volume01);
    void setMediaLooping(bool looping);
    bool isMediaPlaying() const { return m_state.isVideoPlaying; }
    bool isMediaLooping() const { return m_state.isVideoLooping; }

    IProjectionOutput* output() const { return m_output.get(); }

signals:
    void stateChanged();
    void windowVisibilityChanged(bool shown);

private:
    void showWindowOnce();
    void push();
    IProjectionOutput* ensureOutput();

    ProjectionState m_state;
    AppSettings m_settings;
    OutputFactory m_factory;
    std::unique_ptr<IProjectionOutput> m_output;
    int m_selectedMonitor = -1;
    int m_activeScreen = 0;
    bool m_inShow = false;
    bool m_showAgain = false;
};

} // namespace Lux
