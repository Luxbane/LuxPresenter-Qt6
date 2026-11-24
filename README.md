# LuxPresenter

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Qt 6.12](https://img.shields.io/badge/Qt-6.12-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20(x64)-0078D6.svg)](https://www.microsoft.com/windows)
[![GitHub release](https://img.shields.io/github/v/release/Luxbane/LuxPresenter-Qt6?color=blue)](https://github.com/Luxbane/LuxPresenter-Qt6/releases)

> *"We really got a free, high-performance church presenter app one day after GTA 6."* 🎮🕊️

**LuxPresenter** is a high-performance, modern church presentation and worship projection software engineered in **C++20** and **Qt 6**, built and pair-programmed with **Claude Sonnet 5.5** and **Gemini 3.8 Flash**. Designed for churches, ministries, conferences, and live events, LuxPresenter delivers rock-solid stability, zero-latency switching, and hardware-accelerated dual-display output.

* **GitHub Repository:** [https://github.com/Luxbane/LuxPresenter-Qt6](https://github.com/Luxbane/LuxPresenter-Qt6)

---

## Key Features

* **Dual-Screen Projection Engine**
  * Hardware-accelerated output using native Win32 layered windows and Direct3D 11 / Vulkan.
  * Dual-deck video background crossfade powered by **libmpv**.
  * Instant blackout (`F6`), stage clear (`F7`), logo overlay (`F8`), and live transitions (`F5`).
  * Single-screen test mode for rehearsal and multi-monitor auto-detection for live venues.

* **Song & Lyrics Presenter**
  * Instant full-text search across titles and lyric stanzas.
  * Smart stanza navigation with keyboard shortcuts (`Space`, `PgDn`, `PgUp`, arrow keys).
  * Auto line wrapping, customizable Tahoma/system typography, margins, and custom background media.
  * Schedule manager for organizing services, liturgies, and setlists.

* **Scripture Viewer**
  * Multi-version Bible support (bundled King James Version + user-importable Indonesian Terjemahan Baru / TB).
  * Rapid reference search supporting flexible inputs (e.g. `Genesis 1 1`, `John 3:16`, `Wahyu 6 6`, chapter ranges, and keyword searches).
  * Visual books & chapters drawer with quick verse preview.

* **Media & Video Player**
  * High-performance playback engine for MP4, MKV, MOV, WebM, MP3, WAV, and high-resolution images.
  * Interactive click-to-seek progress timeline, volume slider, and loop playback toggle.
  * Customizable library display: switch between Filmstrip Grid Cards and compact List View.

* **Presentation Documents**
  * Crisp, page-accurate PDF rendering via embedded **Google PDFium**.
  * PowerPoint & Impress presentation support (`.pptx`, `.ppsx`, `.odp`) via headless background conversion.

* **Integrated Online Browser**
  * Native **Microsoft Edge WebView2** integration with InPrivate isolation.
  * Built-in **uBlock Origin** ad-blocker for clean, uninterrupted live streaming and media display.

* **Modern Dark & Light Themes**
  * Thoughtfully styled interface built for low-light church projection booths.

---

## Download & Installation

Visit the [GitHub Releases](https://github.com/Luxbane/LuxPresenter-Qt6/releases) page to download:

1. **LuxPresenter Setup (`LuxPresenter_Setup.exe`)**
   * Standard Windows installer.
   * Installs by default for current user (`%LocalAppData%\Programs\LuxPresenter`) without requiring administrator elevation, with optional all-users mode.
   * Creates Start Menu and Desktop shortcuts, plus an official uninstaller.

2. **Quick Patch (`LuxPresenter-Patch.zip`)**
   * Lightweight update package (~8 MB) containing updated binaries and `Update.bat`.
   * Automatically closes running instances, detects installation directory, and replaces updated program files in seconds.

3. **Portable Edition**
   * Standalone package ready to run off a USB drive without installation.

---

## System Requirements

* **OS:** Windows 10 (1809+) or Windows 11, 64-bit.
* **Processor:** Intel Core i3 / AMD Ryzen 3 or higher.
* **RAM:** 4 GB minimum (8 GB recommended for video & presentations).
* **Graphics:** DirectX 11 / Vulkan compatible GPU.
* **Display:** 1920x1080 primary display; secondary output/projector recommended.

---

## Building from Source

### Prerequisites
* **Visual Studio 2022** (v143 toolset, MSVC C++20 compiler)
* **Qt 6.12+** (Components: `Core`, `Gui`, `Widgets`, `Svg`, `Network`, `Test`)
* **CMake 3.21+** and **Ninja**
* **Inno Setup 6** (optional, for compiling the installer)

### Quick Build
```cmd
git clone https://github.com/Luxbane/LuxPresenter-Qt6.git
cd LuxPresenter
build.bat Release
```

The build script will:
1. Configure and generate Ninja build files in `build\`.
2. Compile the core library and GUI executable.
3. Run all automated unit test suites (`luxcore_tests`, `luxsongs_tests`, `luxprojection_tests`, etc.).
4. Package the portable distribution into `Portable\`.

### Compiling the Installer
```cmd
"C:\Users\%USERNAME%\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\setup.iss
```
The compiled installer will be written to `Release\LuxPresenter_Setup.exe`.

---

## Third-Party Credits & Libraries

LuxPresenter incorporates open-source libraries under their respective licenses:

| Component | License | Role |
|---|---|---|
| **Qt Framework 6.12** | LGPL-3.0 | Application framework and UI widgets |
| **libmpv** | LGPL-2.1+ | Hardware-accelerated video & audio engine |
| **Google PDFium** | BSD-3-Clause / Apache-2.0 | PDF presentation rendering |
| **Microsoft Edge WebView2** | BSD-3-Clause | Embedded browser runtime |
| **uBlock Origin** | GPL-3.0 | Web content and ad blocker extension |
| **King James Version (KJV)** | Public Domain | Default bundled Scripture database |

For full third-party notices and complete license texts, see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## License & Author

LuxPresenter is created and maintained by **Luxbane**, co-developed with **Claude Sonnet 5.5** and **Gemini 3.8 Flash**.

* **License:** Licensed under the [GNU General Public License v3.0](LICENSE) (GPL-3.0). Free and open-source forever.
* **GitHub:** [@Luxbane](https://github.com/Luxbane)
* **Releases:** [https://github.com/Luxbane/LuxPresenter-Qt6/releases](https://github.com/Luxbane/LuxPresenter-Qt6/releases)
