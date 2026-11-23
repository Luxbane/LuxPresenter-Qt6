#pragma once

#include <QString>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

class QLibrary;

namespace Lux {

// Minimal dynamic binding to libmpv-2.dll (LGPL build, bundled next to the executable). The functions are
// resolved at run time with QLibrary, so the application starts and works (video disabled) when the DLL
// or the Vulkan loader it imports is missing. Signatures follow the public client API (mpv/client.h).
struct MpvApi
{
    using Handle = void;

    Handle* (*create)() = nullptr;
    int (*initialize)(Handle*) = nullptr;
    void (*terminateDestroy)(Handle*) = nullptr;
    int (*setOptionString)(Handle*, const char*, const char*) = nullptr;
    int (*setPropertyString)(Handle*, const char*, const char*) = nullptr;
    char* (*getPropertyString)(Handle*, const char*) = nullptr;
    int (*getProperty)(Handle*, const char*, int format, void* data) = nullptr;
    void (*freeMem)(void*) = nullptr;
    int (*command)(Handle*, const char** args) = nullptr;
    const char* (*errorString)(int) = nullptr;
    unsigned long (*clientApiVersion)() = nullptr;

    // mpv render API (OpenGL / FBO direct GPU rendering)
    int (*renderContextCreate)(mpv_render_context**, Handle*, mpv_render_param*) = nullptr;
    void (*renderContextFree)(mpv_render_context*) = nullptr;
    int (*renderContextRender)(mpv_render_context*, mpv_render_param*) = nullptr;
    void (*renderContextSetUpdateCallback)(mpv_render_context*, void (*)(void*), void*) = nullptr;
    uint64_t (*renderContextUpdate)(mpv_render_context*) = nullptr;

    static constexpr int kFormatFlag = 3;
    static constexpr int kFormatDouble = 5;

    // Loads the DLL (once). `dllPath` may be empty: then <application dir>/libmpv-2.dll is used.
    static MpvApi& instance();
    bool load(const QString& dllPath = {});
    bool isAvailable() const { return m_loaded; }
    QString lastError() const { return m_error; }
    QString dllPath() const { return m_path; }

private:
    bool m_loaded = false;
    QString m_error;
    QString m_path;
    QLibrary* m_lib = nullptr;
};

} // namespace Lux
