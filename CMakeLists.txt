cmake_minimum_required(VERSION 3.21)
project(LuxPresenter VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT MSVC)
    message(FATAL_ERROR "LuxPresenter requires MSVC 2022 (see Rules.md section 7).")
endif()
add_compile_options(/utf-8 /W4 /permissive-)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
qt_standard_project_setup()

set(LUX_PORTABLE_DIR "${CMAKE_SOURCE_DIR}/Portable" CACHE PATH "Output directory for the portable test build")

# ---- Core library (models + services; no GUI dependency) -------------------
add_library(luxcore STATIC
    src/Models/AppSettings.cpp
    src/Models/Slide.cpp
    src/Models/PresentationItem.cpp
    src/Models/Song.cpp
    src/Models/Scripture.cpp
    src/Models/MediaItem.cpp
    src/Models/BackgroundItem.cpp
    src/Models/ScheduleItem.cpp
    src/Models/ScheduleFileData.cpp
    src/Models/PresentationDocModels.cpp
    src/Services/AppPaths.cpp
    src/Services/JsonFile.cpp
    src/Services/LyricsFormatter.cpp
    src/Services/DataService.cpp
    src/Services/TextRepair.cpp
    src/Services/SongSearch.cpp
    src/Services/SongImportExport.cpp
    src/Services/ScheduleService.cpp
    src/Services/Displays.cpp
    src/Services/ProjectionComposer.cpp
    src/Services/ProjectionService.cpp
    src/Services/DeckFade.cpp
    src/Services/MpvApi.cpp
    src/Services/MpvPlayer.cpp
    src/Services/PdfDocument.cpp
    src/Services/UrlUtil.cpp
    src/Services/AdBlockEngine.cpp
    src/Services/BibleLibrary.cpp
    src/Services/BackgroundUtil.cpp
    src/Services/VideoPoster.cpp
    src/Services/AlertService.cpp
    src/Services/Presentation/OfficeConverter.cpp
)
target_include_directories(luxcore PUBLIC src)
target_link_libraries(luxcore PUBLIC Qt6::Core)
target_compile_definitions(luxcore PUBLIC LUX_VERSION="${PROJECT_VERSION}")
# Public-domain sample hymns (loaded only when the user asks for them).
qt_add_resources(luxcore "luxcore_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES resources/sample_songs.json)

# ---- GUI library (Qt Widgets) ------------------------------------------------
add_library(luxui STATIC
    src/Services/ThemeService.cpp
    src/Views/IconFactory.cpp
    src/Views/AboutWindow.cpp
    src/Views/SongEditorWindow.cpp
    src/Views/OptionsDialog.cpp
    src/Views/BackgroundDialog.cpp
    src/Views/MessageAlertWindow.cpp
    src/Views/MainWindow.cpp
    src/Views/Components/ClassicButton.cpp
    src/Views/Components/TabStrip.cpp
    src/Views/Components/ScheduleView.cpp
    src/Views/Components/SongsTab.cpp
    src/Views/Components/ScripturesTab.cpp
    src/Views/Components/MediaTab.cpp
    src/Views/Components/PresentationTab.cpp
    src/Views/Components/OnlineBrowserView.cpp
    src/Views/Components/LibraryView.cpp
    src/Views/Components/SlideCanvas.cpp
    src/Views/Components/TextRender.cpp
    src/Views/Components/SlidePaneView.cpp
    src/Views/Components/ProportionalSplitter.cpp
    src/Views/Projection/DisplayHelper.cpp
    src/Views/Projection/DualDeckVideo.cpp
    src/Views/Projection/ProjectionWindow.cpp
    src/Views/Projection/ProjectionOutput.cpp
    src/Services/PageImageCache.cpp
    src/Services/BackgroundThumbnails.cpp
    src/Services/AdBlockService.cpp
    src/Services/PresentationService.cpp
    src/Services/UpdateService.cpp
    src/Views/UpdateDialog.cpp
    src/Views/Components/PageView.cpp
    src/Views/Components/WebView2Host.cpp
)
target_link_libraries(luxui PUBLIC luxcore Qt6::Widgets Qt6::Svg Qt6::Network user32 shcore)
# Application manifest (Windows 10 support) for every executable that links the GUI library.
target_sources(luxui INTERFACE "${CMAKE_SOURCE_DIR}/resources/app.manifest")
qt_add_resources(luxui "luxui_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES resources/app_icon.png resources/online_home.html)

# Microsoft WebView2 SDK headers (downloaded separately into third_party/, see README). Without them the Online
# browser is built as "unavailable"; WebView2Loader.dll is loaded at run time, never linked.
set(LUX_WEBVIEW2_INC "${CMAKE_SOURCE_DIR}/third_party/webview2/build/native/include")
if(EXISTS "${LUX_WEBVIEW2_INC}/WebView2.h")
    target_include_directories(luxui PRIVATE "${LUX_WEBVIEW2_INC}")
    target_compile_definitions(luxui PRIVATE LUX_HAS_WEBVIEW2)
endif()
set(LUX_PDFIUM_DLL "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/pdfium.dll")
set(LUX_WEBVIEW2_LOADER_DLL "${CMAKE_SOURCE_DIR}/third_party/webview2/build/native/x64/WebView2Loader.dll")

# ---- Application -----------------------------------------------------------
add_executable(LuxPresenter WIN32 src/main.cpp resources/app.rc)
target_link_libraries(LuxPresenter PRIVATE luxui)
set_target_properties(LuxPresenter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${LUX_PORTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${LUX_PORTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${LUX_PORTABLE_DIR}")

# Bundled KJV dataset (the only Bible that ships with the application).
add_custom_command(TARGET LuxPresenter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LUX_PORTABLE_DIR}/Data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/data/bibles.json" "${LUX_PORTABLE_DIR}/Data/bibles.json")
# Ad blocker filter-list snapshot (optional fallback; uBlock Origin is used primarily)
if(EXISTS "${CMAKE_SOURCE_DIR}/data/adblock")
    add_custom_command(TARGET LuxPresenter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/adblock" "${LUX_PORTABLE_DIR}/Data/AdBlock")
endif()
# uBlock Origin extension bundle (1.76.2 unpacked, MV2)
add_custom_command(TARGET LuxPresenter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Portable/Extensions/uBlockOrigin"
            "${LUX_PORTABLE_DIR}/Extensions/uBlockOrigin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Portable/Extensions/uBlockOrigin"
            "$<TARGET_FILE_DIR:LuxPresenter>/Extensions/uBlockOrigin")

# Video engine: LGPL libmpv build (downloaded separately into third_party/, see README). Bundled only when
# present; without it the application runs with video disabled.
set(LUX_MPV_DLL "${CMAKE_SOURCE_DIR}/third_party/mpv-lgpl/libmpv-2.dll")
if(EXISTS "${LUX_MPV_DLL}")
    add_custom_command(TARGET LuxPresenter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUX_MPV_DLL}" "${LUX_PORTABLE_DIR}")
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/mpv-lgpl/vulkan-1.dll")
        add_custom_command(TARGET LuxPresenter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/mpv-lgpl/vulkan-1.dll" "${LUX_PORTABLE_DIR}")
    endif()
endif()
foreach(dll "${LUX_PDFIUM_DLL}" "${LUX_WEBVIEW2_LOADER_DLL}")
    if(EXISTS "${dll}")
        add_custom_command(TARGET LuxPresenter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "${LUX_PORTABLE_DIR}")
    endif()
endforeach()
add_custom_command(TARGET LuxPresenter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LUX_PORTABLE_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" "${LUX_PORTABLE_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/docs" "${LUX_PORTABLE_DIR}/docs")

# Presentation engine: an unmodified official LibreOffice (admin-extracted MSI, see README) is bundled under
# Engines\LibreOffice, with the app-local Visual C++ runtime it needs next to its program files. The WebView2
# Fixed Version Runtime (official Microsoft CAB) goes to Engines\WebView2. Both are copied once per version.
set(LUX_LIBREOFFICE_SRC "${CMAKE_SOURCE_DIR}/third_party/libreoffice")
set(LUX_WEBVIEW2_RUNTIME_SRC "${CMAKE_SOURCE_DIR}/third_party/webview2-runtime")
set(LUX_LO_EXTRA "")
if(DEFINED ENV{VCToolsRedistDir})
    foreach(name vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll)
        set(dll "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT/${name}")
        if(EXISTS "${dll}")
            list(APPEND LUX_LO_EXTRA "${dll}")
        endif()
    endforeach()
endif()
if(EXISTS "${LUX_LIBREOFFICE_SRC}/program/soffice.bin")
    add_custom_command(TARGET LuxPresenter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DSRC=${LUX_LIBREOFFICE_SRC} -DDST=${LUX_PORTABLE_DIR}/Engines/LibreOffice
                -DSTAMP=LibreOffice-26.8.3-x64 "-DEXTRA_FILES=${LUX_LO_EXTRA}" "-DREMOVE_GLOBS=*.msi"
                -P "${CMAKE_SOURCE_DIR}/cmake/bundle_dependency.cmake")
endif()
file(GLOB LUX_WV2_RUNTIME_DIRS LIST_DIRECTORIES true "${LUX_WEBVIEW2_RUNTIME_SRC}/Microsoft.WebView2.FixedVersionRuntime.*")
foreach(rt IN LISTS LUX_WV2_RUNTIME_DIRS)
    if(EXISTS "${rt}/msedgewebview2.exe")
        string(REGEX REPLACE ".*FixedVersionRuntime\\.([0-9.]+)\\.x64$" "\\1" rt_version "${rt}")
        add_custom_command(TARGET LuxPresenter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DSRC=${rt} -DDST=${LUX_PORTABLE_DIR}/Engines/WebView2/${rt_version}
                    -DSTAMP=WebView2-Fixed-${rt_version}-x64 -P "${CMAKE_SOURCE_DIR}/cmake/bundle_dependency.cmake")
    endif()
endforeach()

# Qt runtime for a self-contained portable folder.
get_filename_component(LUX_QT_BIN "${Qt6_DIR}/../../../bin" ABSOLUTE)
find_program(LUX_WINDEPLOYQT windeployqt HINTS "${LUX_QT_BIN}" NO_DEFAULT_PATH)
if(LUX_WINDEPLOYQT)
    add_custom_command(TARGET LuxPresenter POST_BUILD
        COMMAND "${LUX_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                --no-opengl-sw --no-system-d3d-compiler
                $<IF:$<CONFIG:Debug>,--debug,--release>
                "$<TARGET_FILE:LuxPresenter>")
endif()

# App-local MSVC runtime (redistributable DLLs from the Visual Studio installation)
# so the portable folder runs on machines without the VC++ redistributable.
if(DEFINED ENV{VCToolsRedistDir} AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(name vcruntime140.dll vcruntime140_1.dll msvcp140.dll)
        set(dll "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT/${name}")
        if(EXISTS "${dll}")
            add_custom_command(TARGET LuxPresenter POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "${LUX_PORTABLE_DIR}")
        endif()
    endforeach()
endif()

# ---- Tests -----------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
