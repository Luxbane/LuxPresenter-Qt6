#include "Services/ProjectionService.h"

namespace Lux {

ProjectionService::ProjectionService(QObject* parent) : QObject(parent) {}

ProjectionService::~ProjectionService() = default;

void ProjectionService::setOutputFactory(OutputFactory factory)
{
    m_output.reset();
    m_activeScreen = 0;
    m_factory = std::move(factory);
}

IProjectionOutput* ProjectionService::ensureOutput()
{
    if (!m_output && m_factory)
        m_output = m_factory();
    return m_output.get();
}

void ProjectionService::setSettings(const AppSettings& settings)
{
    m_settings = settings;
    m_state.applySettings(settings);
    if (m_output)
        m_output->setMargins(settings.marginTop, settings.marginBottom, settings.marginLeft, settings.marginRight);
    push();
}

QList<ScreenInfo> ProjectionService::displays() const
{
    if (m_output)
        return m_output->screens();
    if (m_factory) {
        // Enumerating needs an output object; use a throw-away one for the query (cheap, windows are lazy).
        const auto probe = m_factory();
        return probe ? probe->screens() : QList<ScreenInfo>();
    }
    return {};
}

QString ProjectionService::displayStatusText() const
{
    return DisplaySelection::statusText(int(displays().size()), m_activeScreen);
}

void ProjectionService::setSelectedMonitorIndex(int index)
{
    if (index == m_selectedMonitor)
        return;
    m_selectedMonitor = index;
    if (m_inShow || isWindowShown()) // move the live output to the newly chosen monitor
        showWindow();
}

void ProjectionService::showWindow()
{
    // Showing runs the event loop briefly (the output waits for Qt to adopt the new geometry), so a timer or
    // signal can call back in, e.g. a display switch. Nested requests are folded into one more pass.
    if (m_inShow) {
        m_showAgain = true;
        return;
    }
    m_inShow = true;
    do {
        m_showAgain = false;
        showWindowOnce();
    } while (m_showAgain);
    m_inShow = false;
}

void ProjectionService::showWindowOnce()
{
    IProjectionOutput* out = ensureOutput();
    if (!out)
        return;
    const QList<ScreenInfo> screens = out->screens();
    const ScreenInfo* target = DisplaySelection::choose(screens, m_selectedMonitor);
    if (!target)
        return;
    const bool wasShown = out->isShown();
    out->setMargins(m_settings.marginTop, m_settings.marginBottom, m_settings.marginLeft, m_settings.marginRight);
    if (!out->show(*target))
        return;
    m_activeScreen = target->index + 1;
    push();
    if (!wasShown)
        emit windowVisibilityChanged(true);
}

void ProjectionService::hideWindow()
{
    if (!m_output || !m_output->isShown()) {
        m_activeScreen = 0;
        return;
    }
    m_output->hide();
    m_activeScreen = 0;
    emit windowVisibilityChanged(false);
}

bool ProjectionService::isWindowShown() const
{
    return m_output && m_output->isShown();
}

void ProjectionService::preWarm()
{
    IProjectionOutput* out = ensureOutput();
    if (out)
        out->preWarm();
}

void ProjectionService::push()
{
    if (m_output && m_output->isShown())
        m_output->update(m_state);
    emit stateChanged();
}

void ProjectionService::setLivePresentation(const QString& pdfPath, int page)
{
    m_state.isVideoMode = false;
    m_state.isVideoPlaying = false;
    m_state.isPresentationMode = true;
    m_state.presentationPdfPath = pdfPath;
    m_state.presentationPage = page;
    push();
}

void ProjectionService::navigatePresentation(int page)
{
    if (!m_state.isPresentationMode || m_state.presentationPage == page)
        return;
    m_state.presentationPage = page;
    push();
}

void ProjectionService::closePresentation()
{
    if (!m_state.isPresentationMode)
        return;
    m_state.isPresentationMode = false;
    m_state.presentationPdfPath.clear();
    push();
}

void ProjectionService::setLiveSlide(const std::optional<Slide>& slide, const QString& background)
{
    m_state.isPresentationMode = false;
    m_state.presentationPdfPath.clear();
    m_state.isVideoMode = false;
    m_state.isVideoPlaying = false;
    m_state.mediaFilePath.clear();
    m_state.activeSlide = slide;
    m_state.activeBackground = background;
    push();
}

void ProjectionService::setBackground(const QString& background)
{
    m_state.isVideoMode = false;
    m_state.isVideoPlaying = false;
    m_state.mediaFilePath.clear();
    m_state.activeBackground = background;
    push();
}

void ProjectionService::toggleLive(bool live)
{
    m_state.isLive = live;
    push();
}

void ProjectionService::toggleBlack(bool black)
{
    m_state.isBlack = black;
    push();
}

void ProjectionService::toggleClear(bool clear)
{
    m_state.isClear = clear;
    push();
}

void ProjectionService::toggleLogo(bool logo)
{
    m_state.isLogo = logo;
    push();
}

void ProjectionService::setLogoPath(const QString& path)
{
    m_state.logoPath = path;
    push();
}

void ProjectionService::playMedia(const QString& path, bool looping, bool playing)
{
    m_state.isPresentationMode = false;
    m_state.presentationPdfPath.clear();
    m_state.isVideoMode = true;
    m_state.mediaFilePath = path;
    m_state.isVideoLooping = looping;
    m_state.isVideoPlaying = playing;
    push();
}

void ProjectionService::setMediaPlaying(bool playing)
{
    m_state.isVideoPlaying = playing;
    push();
}

void ProjectionService::stopMedia()
{
    m_state.isVideoMode = false;
    m_state.isVideoPlaying = false;
    m_state.mediaFilePath.clear();
    push();
}

void ProjectionService::seekMedia(double seconds)
{
    if (m_output)
        m_output->seekVideo(seconds);
}

double ProjectionService::mediaPosition() const
{
    return m_output ? m_output->videoPosition() : 0;
}

double ProjectionService::mediaDuration() const
{
    return m_output ? m_output->videoDuration() : 0;
}

void ProjectionService::setMediaVolume(double volume01)
{
    m_state.videoVolume = std::clamp(volume01, 0.0, 1.0);
    push();
}

void ProjectionService::setMediaLooping(bool looping)
{
    m_state.isVideoLooping = looping;
    push();
}

} // namespace Lux
