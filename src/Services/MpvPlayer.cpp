#include "Services/MpvPlayer.h"

#include "Services/MpvApi.h"

#include <clocale>

namespace Lux {

MpvPlayer::~MpvPlayer()
{
    destroy();
}

bool MpvPlayer::setOption(const char* name, const char* value)
{
    return MpvApi::instance().setOptionString(m_handle, name, value) >= 0;
}

bool MpvPlayer::setProp(const char* name, const char* value)
{
    return MpvApi::instance().setPropertyString(m_handle, name, value) >= 0;
}

bool MpvPlayer::init(quintptr wid)
{
    MpvApi& api = MpvApi::instance();
    if (!api.isAvailable() && !api.load()) {
        m_error = api.lastError();
        return false;
    }
    destroy();
    // mpv requires the C numeric locale (reference: MpvNative does the same).
    std::setlocale(LC_NUMERIC, "C");

    for (int attempt = 0; attempt < 2; ++attempt) {
        m_handle = api.create();
        if (!m_handle) {
            m_error = QStringLiteral("mpv_create failed");
            return false;
        }
        const QByteArray w = QByteArray::number(qulonglong(wid));
        setOption("wid", w.constData());
        setOption("vo", "gpu");
        if (attempt == 0)
            setOption("gpu-context", "d3d11"); // second attempt lets mpv choose
        setOption("hwdec", "auto-safe");
        // Ultra-low GPU and CPU overhead settings for potato laptop iGPUs (e.g. Intel HD 5500)
        setOption("profile", "fast");
        setOption("scale", "bilinear");
        setOption("cscale", "bilinear");
        setOption("dscale", "bilinear");
        setOption("d3d11-flip", "no");
        setOption("d3d11-exclusive-fs", "no");
        setOption("vd-lavc-threads", "2");
        setOption("keep-open", "yes");
        setOption("idle", "yes");
        setOption("force-window", "yes");
        setOption("osc", "no");
        setOption("osd-level", "0");
        setOption("config", "no");
        setOption("load-scripts", "no");
        setOption("ytdl", "no");
        setOption("input-default-bindings", "no");
        setOption("input-vo-keyboard", "no");
        setOption("terminal", "no");
        const int rc = api.initialize(m_handle);
        if (rc >= 0)
            return true;
        m_error = QStringLiteral("mpv_initialize failed (%1)").arg(api.errorString ? QString::fromUtf8(api.errorString(rc)) : QString::number(rc));
        api.terminateDestroy(m_handle);
        m_handle = nullptr;
    }
    return false;
}

void MpvPlayer::destroy()
{
    if (m_handle) {
        MpvApi::instance().terminateDestroy(m_handle);
        m_handle = nullptr;
    }
}

bool MpvPlayer::load(const QString& path, Role role, bool loop, bool startPaused, double volume)
{
    if (!m_handle)
        return false;
    if (role == Role::Background) {
        setProp("mute", "yes");
        setProp("aid", "no"); // disables audio decoding completely, saving CPU on dual-core laptops
        // Background video scales to monitor without letterboxing or cropping
        setProp("keepaspect", "no");
        setProp("panscan", "0.0");
        setProp("loop-file", "inf");
    } else {
        setProp("mute", "no");
        setProp("aid", "auto");
        // Standalone media video keeps its file aspect ratio with letterboxing
        setProp("keepaspect", "yes");
        setProp("panscan", "0.0");
        setProp("loop-file", loop ? "inf" : "no");
        setVolume(volume);
    }
    setProp("pause", startPaused ? "yes" : "no");
    const QByteArray p = path.toUtf8();
    const char* args[] = {"loadfile", p.constData(), nullptr};
    const int rc = MpvApi::instance().command(m_handle, args);
    if (rc < 0) {
        const MpvApi& api = MpvApi::instance();
        m_error = QStringLiteral("loadfile failed (%1)").arg(api.errorString ? QString::fromUtf8(api.errorString(rc)) : QString::number(rc));
        return false;
    }
    return true;
}

void MpvPlayer::stop()
{
    if (!m_handle)
        return;
    const char* args[] = {"stop", nullptr};
    MpvApi::instance().command(m_handle, args);
}

void MpvPlayer::setPaused(bool paused)
{
    if (m_handle)
        setProp("pause", paused ? "yes" : "no");
}

bool MpvPlayer::isPaused() const
{
    return property("pause") == QLatin1String("yes");
}

void MpvPlayer::seek(double seconds)
{
    if (!m_handle)
        return;
    const QByteArray s = QByteArray::number(seconds, 'f', 3);
    const char* args[] = {"seek", s.constData(), "absolute", nullptr};
    MpvApi::instance().command(m_handle, args);
}

void MpvPlayer::setVolume(double volume01)
{
    if (!m_handle)
        return;
    const QByteArray v = QByteArray::number(volume01 * 100.0, 'f', 1);
    setProp("volume", v.constData());
}

void MpvPlayer::setLoop(bool loop)
{
    if (m_handle)
        setProp("loop-file", loop ? "inf" : "no");
}

double MpvPlayer::duration() const
{
    double v = 0;
    if (m_handle && MpvApi::instance().getProperty(m_handle, "duration", MpvApi::kFormatDouble, &v) >= 0)
        return v;
    return 0;
}

double MpvPlayer::position() const
{
    double v = 0;
    if (m_handle && MpvApi::instance().getProperty(m_handle, "time-pos", MpvApi::kFormatDouble, &v) >= 0)
        return v;
    return 0;
}

bool MpvPlayer::atEnd() const
{
    return property("eof-reached") == QLatin1String("yes");
}

bool MpvPlayer::hasVideo() const
{
    return !property("video-params/w").isEmpty();
}

bool MpvPlayer::isFirstFrameReady() const
{
    if (!m_handle)
        return false;
    MpvApi& api = MpvApi::instance();
    // 1. Check if at least one video frame has physically been decoded and displayed
    int64_t frames = 0;
    if (api.getProperty && api.getProperty(m_handle, "video-frame-count", 4 /* MPV_FORMAT_INT64 */, &frames) >= 0) {
        if (frames >= 1)
            return true;
    }
    // 2. Check if playback position is available and >= 0
    double pos = 0;
    if (api.getProperty && api.getProperty(m_handle, "time-pos", MpvApi::kFormatDouble, &pos) >= 0) {
        return true;
    }
    return false;
}

QString MpvPlayer::property(const char* name) const
{
    if (!m_handle)
        return {};
    MpvApi& api = MpvApi::instance();
    char* s = api.getPropertyString(m_handle, name);
    if (!s)
        return {};
    const QString r = QString::fromUtf8(s);
    api.freeMem(s);
    return r;
}

} // namespace Lux
