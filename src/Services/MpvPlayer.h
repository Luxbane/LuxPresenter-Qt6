#pragma once

#include <QString>

namespace Lux {

// One libmpv instance rendering into a native window (a "deck"). Port of MpvPlayer/MpvNative in the
// reference, restricted to what the projection engine needs.
class MpvPlayer
{
public:
    enum class Role { Background, Media };

    MpvPlayer() = default;
    ~MpvPlayer();
    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // `wid` is the HWND mpv draws into. Fails (returns false) if libmpv is unavailable.
    bool init(quintptr wid);
    void destroy();
    bool isValid() const { return m_handle != nullptr; }

    // Loads `path`. Background: muted, looping, stretched to fill. Media: sound on, aspect kept.
    bool load(const QString& path, Role role, bool loop, bool startPaused, double volume = 1.0);
    void stop();
    void setPaused(bool paused);
    bool isPaused() const;
    void seek(double seconds);
    void setVolume(double volume01);
    void setLoop(bool loop);

    double duration() const;      // seconds, 0 if unknown
    double position() const;      // seconds
    bool atEnd() const;           // eof-reached
    bool hasVideo() const;        // a video track has been decoded (video-params available)
    bool isFirstFrameReady() const; // first video frame is decoded and ready on the HWND
    QString property(const char* name) const;
    QString lastError() const { return m_error; }

private:
    bool setOption(const char* name, const char* value);
    bool setProp(const char* name, const char* value);
    void* m_handle = nullptr;
    QString m_error;
};

} // namespace Lux
