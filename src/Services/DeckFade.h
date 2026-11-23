#pragma once

#include <QtGlobal>

namespace Lux {

// Alpha bookkeeping for the two video decks (ProjectionBackgroundWindow crossfade): the incoming deck's
// alpha ramps 0 -> 255 over the crossfade while the outgoing deck stays opaque underneath, then the
// outgoing deck is stopped. Pure arithmetic so it can be tested without a window or a player.
class DeckFade
{
public:
    // Start a new item on the idle deck; returns that deck's index (0/1) so the caller can load it.
    int begin(int crossfadeMs, qint64 now);
    // Advance the fade. Returns the deck that has just finished fading out (stop it), or -1.
    int tick(qint64 now);
    // Restart crossfade clock so file load time does not truncate the transition.
    void restartClock(qint64 now) { m_start = now; }
    // Both decks transparent, nothing active.
    void stopAll();

    int alpha(int deck) const { return m_alpha[deck]; }
    int active() const { return m_active; }
    bool fading() const { return m_fading; }

private:
    int m_active = -1;
    int m_outgoing = -1;
    int m_alpha[2] = {0, 0};
    bool m_fading = false;
    qint64 m_start = 0;
    int m_duration = 0;
};

} // namespace Lux
