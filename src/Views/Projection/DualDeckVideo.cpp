#include "Views/Projection/DualDeckVideo.h"

#include "Services/MpvApi.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Lux {

DualDeckVideo::DualDeckVideo(QWidget* host) : QObject(host), m_host(host)
{
    for (int i = 0; i < 2; ++i) {
        auto* w = new QWidget(host);
        w->setAttribute(Qt::WA_NativeWindow);
        w->setAttribute(Qt::WA_NoSystemBackground);
        w->setAttribute(Qt::WA_OpaquePaintEvent);
        w->setGeometry(host->rect());
        w->show();
        HWND h = reinterpret_cast<HWND>(w->winId());
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
        m_deck[i] = w;
    }
    m_clock.start();
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &DualDeckVideo::onTick);
}

DualDeckVideo::~DualDeckVideo()
{
    m_timer.stop();
    for (auto& p : m_player)
        p.destroy();
}

void DualDeckVideo::resizeToHost()
{
    for (QWidget* w : m_deck)
        w->setGeometry(m_host->rect());
}

void DualDeckVideo::preWarm()
{
    MpvApi& api = MpvApi::instance();
    if (!api.isAvailable() && !api.load())
        return;
    resizeToHost();
    for (int d = 0; d < 2; ++d) {
        if (!m_ready[d] && m_deck[d]) {
            m_ready[d] = m_player[d].init(quintptr(m_deck[d]->winId()));
            if (m_ready[d])
                applyAlpha(d);
        }
    }
}

int DualDeckVideo::appliedAlpha(int deck) const
{
    BYTE a = 0;
    DWORD flags = 0;
    COLORREF key = 0;
    if (GetLayeredWindowAttributes(reinterpret_cast<HWND>(m_deck[deck]->winId()), &key, &a, &flags))
        return a;
    return -1;
}

void DualDeckVideo::applyAlpha(int deck)
{
    const int a = m_fade.alpha(deck);
    if (a == m_lastAlpha[deck])
        return;
    m_lastAlpha[deck] = a;
    SetLayeredWindowAttributes(reinterpret_cast<HWND>(m_deck[deck]->winId()), 0, BYTE(a), LWA_ALPHA);
}

void DualDeckVideo::stopDeck(int deck)
{
    if (m_player[deck].isValid())
        m_player[deck].stop();
    applyAlpha(deck);
}

bool DualDeckVideo::startOn(const VideoCommand& cmd, MpvPlayer::Role role)
{
    MpvApi& api = MpvApi::instance();
    if (!api.isAvailable() && !api.load()) {
        m_error = api.lastError();
        return false;
    }
    const int deck = m_fade.begin(cmd.crossfadeMs, m_clock.elapsed());
    if (!m_ready[deck]) {
        resizeToHost();
        m_ready[deck] = m_player[deck].init(quintptr(m_deck[deck]->winId()));
        if (!m_ready[deck]) {
            m_error = m_player[deck].lastError();
            m_fade.stopAll();
            return false;
        }
    }
    applyAlpha(deck);
    m_deck[deck]->raise();
    const bool loop = role == MpvPlayer::Role::Background ? true : cmd.looping;
    const bool ok = m_player[deck].load(cmd.path, role, loop, !cmd.playing, m_volume);
    if (!ok)
        m_error = m_player[deck].lastError();

    if (m_fade.fading() && cmd.crossfadeMs > 0) {
        // Crossfading from an existing deck: hold incoming at alpha 0 until frame 1 is decoded & displayed
        m_cuedDeck = deck;
        m_cueStartTime = m_clock.elapsed();
        m_fade.restartClock(m_cueStartTime);
        m_timer.start();
    } else {
        m_cuedDeck = -1;
        const int out = m_fade.tick(m_clock.elapsed()); // immediate cuts report their outgoing deck right away
        if (out >= 0)
            stopDeck(out);
    }
    for (int d = 0; d < 2; ++d)
        applyAlpha(d);
    return ok;
}

bool DualDeckVideo::execute(const VideoCommand& cmd)
{
    switch (cmd.kind) {
    case VideoCommand::Kind::PlayBackground:
        return startOn(cmd, MpvPlayer::Role::Background);
    case VideoCommand::Kind::PlayMedia:
        return startOn(cmd, MpvPlayer::Role::Media);
    case VideoCommand::Kind::Stop:
        m_timer.stop();
        m_cuedDeck = -1;
        for (int d = 0; d < 2; ++d)
            if (m_player[d].isValid())
                m_player[d].stop();
        m_fade.stopAll();
        m_lastAlpha[0] = m_lastAlpha[1] = -1;
        for (int d = 0; d < 2; ++d)
            applyAlpha(d);
        return true;
    case VideoCommand::Kind::Pause:
    case VideoCommand::Kind::Resume:
        for (int d = 0; d < 2; ++d)
            if (m_player[d].isValid())
                m_player[d].setPaused(cmd.kind == VideoCommand::Kind::Pause);
        return true;
    }
    return false;
}

void DualDeckVideo::onTick()
{
    const qint64 now = m_clock.elapsed();
    if (m_cuedDeck >= 0) {
        const bool ready = m_player[m_cuedDeck].isFirstFrameReady();
        const bool timedOut = (now - m_cueStartTime) >= 1500;
        if (ready || timedOut) {
            // First frame is rendered on the target deck (or timeout reached)!
            // Begin the smooth visible crossfade clock now.
            m_cuedDeck = -1;
            m_fade.restartClock(now);
        } else {
            // Still loading/decoding the first frame: hold the clock so alpha stays 0
            m_fade.restartClock(now);
        }
    }

    const int out = m_fade.tick(m_clock.elapsed());
    for (int d = 0; d < 2; ++d)
        applyAlpha(d);
    if (out >= 0)
        stopDeck(out);
    if (!m_fade.fading() && m_cuedDeck < 0)
        m_timer.stop();
}

void DualDeckVideo::setVolume(double volume01)
{
    m_volume = volume01;
    const int a = m_fade.active();
    if (a >= 0 && m_player[a].isValid())
        m_player[a].setVolume(volume01);
}

void DualDeckVideo::setLoop(bool loop)
{
    const int a = m_fade.active();
    if (a >= 0 && m_player[a].isValid())
        m_player[a].setLoop(loop);
}

void DualDeckVideo::seek(double seconds)
{
    const int a = m_fade.active();
    if (a >= 0 && m_player[a].isValid())
        m_player[a].seek(seconds);
}

double DualDeckVideo::position() const
{
    const int a = m_fade.active();
    return a >= 0 ? m_player[a].position() : 0;
}

double DualDeckVideo::duration() const
{
    const int a = m_fade.active();
    return a >= 0 ? m_player[a].duration() : 0;
}

} // namespace Lux
