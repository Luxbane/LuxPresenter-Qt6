#include "Views/Projection/ProjectionOutput.h"

#include "Views/Projection/DisplayHelper.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QResizeEvent>

namespace Lux {

ProjectionBackgroundWindow::ProjectionBackgroundWindow()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral("background-color: black;"));
    setWindowTitle(QStringLiteral("LuxPresenter Projection Background"));
    m_decks = new DualDeckVideo(this);
}

void ProjectionBackgroundWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_decks)
        m_decks->resizeToHost();
}

ProjectionOutput::ProjectionOutput(ScreenProvider screenProvider, std::function<void()> activateMain)
    : m_screens(std::move(screenProvider)), m_activateMain(std::move(activateMain))
{
    m_anim.setInterval(16);
    connect(&m_anim, &QTimer::timeout, this, [this] { tick(); });
    // Resolution / scaling / arrangement changes of any monitor re-fit the output to its (new) monitor.
    auto queueReassert = [this] {
        if (m_shown && !m_reassertQueued) {
            m_reassertQueued = true;
            QMetaObject::invokeMethod(this, [this] { reassertPlacement(); }, Qt::QueuedConnection);
        }
    };
    auto watch = [this, queueReassert](QScreen* s) {
        connect(s, &QScreen::geometryChanged, this, queueReassert);
        connect(s, &QScreen::logicalDotsPerInchChanged, this, queueReassert);
    };
    for (QScreen* s : QGuiApplication::screens())
        watch(s);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [watch, queueReassert](QScreen* s) { watch(s); queueReassert(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, queueReassert);
}

ProjectionOutput::~ProjectionOutput()
{
    delete m_fg;
    delete m_bg;
}

QList<ScreenInfo> ProjectionOutput::screens() const
{
    return m_screens ? m_screens() : DisplayHelper::enumerateScreens();
}

void ProjectionOutput::ensureWindows()
{
    if (m_bg)
        return;
    m_bg = new ProjectionBackgroundWindow;
    m_fg = new ProjectionWindow(&m_composer);
    m_composer.setPresentationPreparer([this] { if (m_fg) m_fg->preparePresentation(); });
    m_composer.setLayerPreparer([this](int layer) { if (m_fg) m_fg->prepareLayer(layer); });
    m_composer.setBackgroundPreparer([this](const QString& path) { if (m_fg) m_fg->preloadImage(path); });
    connect(m_fg, &ProjectionWindow::clicked, this, [this] { if (m_activateMain) m_activateMain(); });
    // Native handles + styles before the first show.
    DisplayHelper::makeNonActivating(m_bg->winId());
    DisplayHelper::makeNonActivating(m_fg->winId());
    m_bg->installEventFilter(this);
    m_fg->installEventFilter(this);
}

// While the output is shown its rectangle is the selected monitor. Qt can still move/resize a window after
// our SetWindowPos (per-monitor DPI change, screen change); any such change is corrected once the current
// event has been handled.
bool ProjectionOutput::eventFilter(QObject* watched, QEvent* e)
{
    if (m_shown && (watched == m_bg || watched == m_fg)) {
        switch (e->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
            if (!m_reassertQueued) {
                m_reassertQueued = true;
                QMetaObject::invokeMethod(this, [this] { reassertPlacement(); }, Qt::QueuedConnection);
            }
            break;
        default: break;
        }
    }
    return QObject::eventFilter(watched, e);
}

void ProjectionOutput::reassertPlacement()
{
    m_reassertQueued = false;
    if (!m_shown)
        return;
    // The monitor itself may have changed (resolution, scaling, position): follow its current description.
    for (const ScreenInfo& s : screens()) {
        const bool same = !m_screen.deviceName.isEmpty() ? s.deviceName == m_screen.deviceName : s.index == m_screen.index;
        if (same) {
            m_screen = s;
            break;
        }
    }
    const QRect target(m_screen.physicalLeft, m_screen.physicalTop, m_screen.physicalWidth, m_screen.physicalHeight);
    if (DisplayHelper::windowRect(m_bg->winId()) != target || DisplayHelper::windowRect(m_fg->winId()) != target)
        DisplayHelper::placePair(m_bg->winId(), m_fg->winId(), m_screen);
}

void ProjectionOutput::setMargins(int top, int bottom, int left, int right)
{
    m_mTop = top; m_mBottom = bottom; m_mLeft = left; m_mRight = right;
    m_marginsSet = true;
    m_composer.setMargins(top, bottom, left, right);
    if (m_fg)
        m_fg->update();
}

bool ProjectionOutput::show(const ScreenInfo& screen)
{
    ensureWindows();
    if (!m_shown) {
        m_composer.reset();   // first state after (re)showing is applied without transitions
        if (m_marginsSet)
            m_composer.setMargins(m_mTop, m_mBottom, m_mLeft, m_mRight);
    }
    m_screen = screen;
    // Make sure the Qt windows exist at the right size, then position them with physical pixels.
    m_bg->show();
    m_fg->show();
    // Qt applies its own pending geometry / per-monitor DPI handling for a freshly shown window after our
    // native SetWindowPos, which left the window at Qt's default size on the first show. Place, let Qt
    // digest the resulting window messages, and repeat until the native rectangle and Qt's own idea of the
    // widget size both agree with the target.
    // Place natively, then let Qt digest the window messages this produced (move/size/DPI). Qt delivers them as
    // queued window-system events, so run the event loop (without user input) until Qt agrees. A timer may call
    // back into the service meanwhile; ProjectionService::showWindow is re-entrancy safe for that.
    auto settled = [&] {
        const QRect target(screen.physicalLeft, screen.physicalTop, screen.physicalWidth, screen.physicalHeight);
        auto sizeOk = [&](QWidget* w) {
            const qreal dpr = w->devicePixelRatio();
            return qAbs(qRound(w->width() * dpr) - target.width()) <= 1 && qAbs(qRound(w->height() * dpr) - target.height()) <= 1;
        };
        return DisplayHelper::windowRect(m_bg->winId()) == target && DisplayHelper::windowRect(m_fg->winId()) == target &&
               sizeOk(m_bg) && sizeOk(m_fg);
    };
    for (int attempt = 0; attempt < 6; ++attempt) {
        DisplayHelper::placePair(m_bg->winId(), m_fg->winId(), screen);
        QCoreApplication::sendPostedEvents(nullptr, 0);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (settled())
            break;
    }
    m_bg->decks()->resizeToHost();
    m_shown = true;
    m_fg->update();
    return true;
}

void ProjectionOutput::hide()
{
    if (!m_bg)
        return;
    m_anim.stop();
    m_composer.reset();
    for (const VideoCommand& c : m_composer.takeVideoCommands())
        m_bg->decks()->execute(c);
    m_fg->hide();
    m_bg->hide();
    m_shown = false;
}

void ProjectionOutput::update(const ProjectionState& state)
{
    if (!m_shown)
        return;
    m_composer.applyState(state);
    for (const VideoCommand& c : m_composer.takeVideoCommands())
        m_bg->decks()->execute(c);
    if (m_composer.inVideoMode()) {
        m_bg->decks()->setVolume(state.videoVolume);
        m_bg->decks()->setLoop(state.isVideoLooping);
    }
    m_fg->update();
    if (!m_anim.isActive())
        m_anim.start();
}

void ProjectionOutput::tick()
{
    const qint64 t = m_composer.now();
    m_composer.advance(t);
    for (const VideoCommand& c : m_composer.takeVideoCommands())
        m_bg->decks()->execute(c);
    m_fg->update();
    if (!m_composer.isAnimating(t))
        m_anim.stop();
}

void ProjectionOutput::seekVideo(double seconds)
{
    if (m_bg)
        m_bg->decks()->seek(seconds);
}

double ProjectionOutput::videoPosition() const
{
    return m_bg ? m_bg->decks()->position() : 0;
}

double ProjectionOutput::videoDuration() const
{
    return m_bg ? m_bg->decks()->duration() : 0;
}

void ProjectionOutput::preWarm()
{
    ensureWindows();
    if (m_fg) {
        QImage dummy(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        dummy.fill(Qt::black);
        QPainter p(&dummy);
        m_fg->paintScene(&p, QRectF(0, 0, 1920, 1080), 0);
        p.end();
    }
    if (m_bg && m_bg->decks()) {
        m_bg->decks()->preWarm();
    }
}

void ProjectionOutput::preloadBackground(const QString& bg)
{
    ensureWindows();
    if (m_fg)
        m_fg->preloadImage(bg);
}

} // namespace Lux
