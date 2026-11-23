#pragma once

#include "Services/DeckFade.h"
#include "Services/MpvPlayer.h"
#include "Services/ProjectionComposer.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace Lux {

// Two native child windows ("decks"), each rendered by its own libmpv instance. A new video starts on the
// idle deck and its window alpha ramps up over the old one (crossfade), after which the old deck stops.
class DualDeckVideo : public QObject
{
    Q_OBJECT
public:
    explicit DualDeckVideo(QWidget* host);
    ~DualDeckVideo() override;

    // Execute an instruction from the composer. Returns false if the video engine is unavailable.
    bool execute(const VideoCommand& cmd);
    void setVolume(double volume01);
    void setLoop(bool loop);
    void seek(double seconds);
    void resizeToHost();
    void preWarm();

    int activeDeck() const { return m_fade.active(); }
    int deckAlpha(int deck) const { return m_fade.alpha(deck); }   // value the fade has computed
    int appliedAlpha(int deck) const;                               // value read back from the window (Win32)
    bool isFading() const { return m_fade.fading(); }
    MpvPlayer* player(int deck) { return &m_player[deck]; }
    QWidget* deckWidget(int deck) { return m_deck[deck]; }
    double position() const;
    double duration() const;
    QString lastError() const { return m_error; }

    // Shortens the poll interval used by tests/diagnostics (default 16 ms).
    void setTickInterval(int ms) { m_timer.setInterval(ms); }
    void tickNow() { onTick(); }

private slots:
    void onTick();

private:
    bool startOn(const VideoCommand& cmd, MpvPlayer::Role role);
    void applyAlpha(int deck);
    void stopDeck(int deck);

    QWidget* m_host;
    QWidget* m_deck[2] = {nullptr, nullptr};
    MpvPlayer m_player[2];
    bool m_ready[2] = {false, false};
    DeckFade m_fade;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_volume = 1.0;
    QString m_error;
    int m_lastAlpha[2] = {-1, -1};
    int m_cuedDeck = -1;
    qint64 m_cueStartTime = 0;
};

} // namespace Lux
