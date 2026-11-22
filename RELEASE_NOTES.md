# LuxPresenter Release Notes

## Version 1.0.1 (November 2026)

### Highlights & New Features

* **Stage Display (Confidence Monitor) — Native & Web-based**
  * **Embedded HTTP Server:** Built-in lightweight HTTP server listening on `0.0.0.0:8080/stage` (fallback to 8081–8089 if in use). Musician, vocalist, and pastor mobile devices, tablets, and stage Smart TVs can connect via Wi-Fi without any extra cabling or software.
  * **Responsive Dark-Mode SPA:** Modern high-contrast web stage display with real-time digital clock, active lyric slide, Next Slide preview container, alert banner, and screen blackout indicators with 300 ms polling.
  * **Native Stage Window:** Local confidence monitor window with dedicated shortcut (`Ctrl+Shift+D`) and fullscreen toggle (`F11`), routable to a 3rd HDMI / DisplayPort physical monitor.
  * **Network Helper in Options:** Automatic detection of private LAN/Wi-Fi IPv4 addresses displayed in **Tools -> Options -> General** with one-click **Copy Link** and **Open in Browser** buttons.

* **Startup Splash Loading Window & Smooth Initialization**
  * Modern frameless slate / dark blue gradient window with application branding, version tag, subtitle, and an animated progress bar (0–100%).
  * Stepwise initialization reporting preferences loading, display detection, video engine setup, database loading, and workspace setup.
  * Graceful exception handling ensuring clean error reporting without hanging background processes.

* **GPU Pipeline & Font Engine Pre-Warming**
  * Automated off-screen pre-warming pass during splash startup (~85%) compiling DirectX shaders, swapchain buffers, and DirectWrite font caches in advance.
  * Eliminates first-frame stutter on Intel UHD / Iris Xe / discrete GPUs, delivering silky-smooth 60 FPS from the very first slide or video transition.

* **On-Demand WebView2 Lifecycle (Zero Idle Overhead)**
  * **Lazy Initialization:** Zero `msedgewebview2.exe` background processes and 0 MB RAM allocated at application startup.
  * **Clean Teardown:** Completely closes the WebView2 controller and terminates background Chromium processes whenever the user leaves the Online tab, returning 150–350 MB of RAM, VRAM, and CPU threads to the worship presentation engine.

### Bug Fixes & Performance Improvements

* **Instant Lyrics Selection & Anti-Lag (0 ms Response):**
  * Fixed an issue where clicking or navigating lyrics caused an ~1-second delay and frozen selection highlight due to synchronous CPU-intensive vector glyph parsing and 4-pass shadow stroking on the UI thread.
  * Implemented an in-memory `QPainterPath` vector glyph cache (`getOrCreateTextPath`) and optimized 2-pass soft drop-shadow rendering, delivering an 8x rendering speedup.
  * Added `SlideCanvas` pixmap caching, turning subsequent slide redraws during video playback into near-instantaneous memory blits (< 0.05 ms).
  * Added responsive animation fast-forwarding when rapidly tapping arrow keys, preventing washed-out text and fade stacking.

* **Silent Instant Transitions behind Black Screen & Logo (`Ctrl+B` / `F8`):**
  * Eliminated background leakage / visual glitches when changing songs or backgrounds while Black Screen or Logo overlay is active.
  * When covered, slide and background transitions execute as silent 0 ms cuts behind the curtain, and video backgrounds are paused to save CPU/GPU cycles.
  * Upon disabling Black Screen (`Ctrl+B`), the black overlay smoothly fades out with the new background 100% ready underneath, cleanly resuming video playback without flicker.

* **Orphaned Data & Thumbnail Cleanup:**
  * Automated cleanup on startup and shutdown sweeping orphaned thumbnail files, presentation cache artifacts, and stale database records for deleted media and backgrounds.
