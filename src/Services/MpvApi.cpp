#include "Services/MpvApi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Lux {

MpvApi& MpvApi::instance()
{
    static MpvApi api;
    return api;
}

bool MpvApi::load(const QString& dllPath)
{
    static QMutex s_loadMutex;
    QMutexLocker locker(&s_loadMutex);

    if (m_loaded)
        return true;
    QString path = dllPath;
    if (path.isEmpty())
        path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("libmpv-2.dll"));
    if (!QFileInfo::exists(path)) {
        m_error = QStringLiteral("libmpv-2.dll not found: %1").arg(QDir::toNativeSeparators(path));
        qWarning() << m_error;
        return false;
    }

#ifdef Q_OS_WIN
    // Ensure Windows dynamic linker searches the application directory for dependencies like vulkan-1.dll
    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(appDir.utf16()));
#endif

    if (!m_lib)
        m_lib = new QLibrary(path); // intentionally kept for the life of the process
    else
        m_lib->setFileName(path);
    if (!m_lib->load()) {
        m_error = m_lib->errorString();
        qWarning() << "MpvApi::load failed to load" << path << ":" << m_error;
        return false;
    }
    auto sym = [this](const char* n) { return m_lib->resolve(n); };
    create = reinterpret_cast<decltype(create)>(sym("mpv_create"));
    initialize = reinterpret_cast<decltype(initialize)>(sym("mpv_initialize"));
    terminateDestroy = reinterpret_cast<decltype(terminateDestroy)>(sym("mpv_terminate_destroy"));
    setOptionString = reinterpret_cast<decltype(setOptionString)>(sym("mpv_set_option_string"));
    setPropertyString = reinterpret_cast<decltype(setPropertyString)>(sym("mpv_set_property_string"));
    getPropertyString = reinterpret_cast<decltype(getPropertyString)>(sym("mpv_get_property_string"));
    getProperty = reinterpret_cast<decltype(getProperty)>(sym("mpv_get_property"));
    freeMem = reinterpret_cast<decltype(freeMem)>(sym("mpv_free"));
    command = reinterpret_cast<decltype(command)>(sym("mpv_command"));
    errorString = reinterpret_cast<decltype(errorString)>(sym("mpv_error_string"));
    clientApiVersion = reinterpret_cast<decltype(clientApiVersion)>(sym("mpv_client_api_version"));

    renderContextCreate = reinterpret_cast<decltype(renderContextCreate)>(sym("mpv_render_context_create"));
    renderContextFree = reinterpret_cast<decltype(renderContextFree)>(sym("mpv_render_context_free"));
    renderContextRender = reinterpret_cast<decltype(renderContextRender)>(sym("mpv_render_context_render"));
    renderContextSetUpdateCallback = reinterpret_cast<decltype(renderContextSetUpdateCallback)>(sym("mpv_render_context_set_update_callback"));
    renderContextUpdate = reinterpret_cast<decltype(renderContextUpdate)>(sym("mpv_render_context_update"));
    if (!create || !initialize || !terminateDestroy || !setOptionString || !setPropertyString || !getPropertyString ||
        !getProperty || !freeMem || !command) {
        m_error = QStringLiteral("libmpv-2.dll is missing required exports");
        m_lib->unload();
        return false;
    }
    m_path = path;
    m_loaded = true;
    m_error.clear();
    return true;
}

} // namespace Lux
