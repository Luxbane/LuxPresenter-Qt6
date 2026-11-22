// LuxPresenter (Qt 6) - application entry point.

#include "Services/DataService.h"
#include "Services/ThemeService.h"
#include "Views/Components/LibraryView.h"
#include "Services/ProjectionService.h"
#include "Views/Components/SongsTab.h"
#include "Views/MainWindow.h"
#include "Services/PresentationService.h"
#include "Views/Components/OnlineBrowserView.h"
#include "Views/Components/PresentationTab.h"
#include "Views/Components/SlidePaneView.h"
#include "Views/Components/WebView2Host.h"
#include "Views/Components/TextRender.h"
#include <QComboBox>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include "Views/Components/ClassicButton.h"
#include "Views/Projection/DisplayHelper.h"
#include "Views/Projection/ProjectionOutput.h"
#include "Services/ProjectionService.h"

#include <QApplication>
#include <QLineEdit>
#include <QSharedPointer>
#include <QUrl>
#include <QFile>
#include <QFont>
#include <QTextStream>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>
#include <QThread>
#include "Views/BackgroundDialog.h"
#include "Views/OptionsDialog.h"
#include "Views/SplashScreen.h"
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#include <ole2.h>
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    OleInitialize(nullptr);
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("LuxPresenterQt6"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUX_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont base(QStringLiteral("Segoe UI"));
    base.setPixelSize(12);
    QApplication::setFont(base);

    const QStringList args = app.arguments();
    const bool showSplash = !args.contains(QStringLiteral("--no-splash")) &&
                            !args.contains(QStringLiteral("--screenshot")) &&
                            !args.contains(QStringLiteral("--dialog")) &&
                            !args.contains(QStringLiteral("--projection-test")) &&
                            !args.contains(QStringLiteral("--present-test")) &&
                            !args.contains(QStringLiteral("--online-test"));

    std::unique_ptr<Lux::SplashScreen> splash;
    if (showSplash) {
        splash = std::make_unique<Lux::SplashScreen>();
        splash->show();
        splash->setProgress(10, QStringLiteral("Loading user preferences and theme..."));
    }

    Lux::DataService data;
    Lux::ThemeService::instance().apply(data.settings().isDarkMode);

    if (splash)
        splash->setProgress(25, QStringLiteral("Detecting display monitors and presentation screens..."));

    if (splash)
        splash->setProgress(40, QStringLiteral("Initializing hardware video and media engines..."));

    if (splash)
        splash->setProgress(60, QStringLiteral("Loading songs database, bibles, and media library..."));

    Lux::MainWindow window(data);

    if (splash)
        splash->setProgress(80, QStringLiteral("Preparing presentation workspace and stage display..."));

    if (splash)
        splash->setProgress(85, QStringLiteral("Pre-warming graphics pipeline and text engine..."));

    {
        // Pre-warm font engine and TextRender vector path cache
        QFont dummyFont(QStringLiteral("Tahoma"), 20, QFont::Bold);
        const QString dummyLyric = QStringLiteral("Amazing grace! How sweet the sound\nThat saved a wretch like me!");
        Lux::TextBlockStyle dummyStyle;
        dummyStyle.font = dummyFont;
        dummyStyle.width = 568 * 3.0;
        dummyStyle.strokeThickness = 9.0;
        dummyStyle.outline = true;
        dummyStyle.shadow = true;
        dummyStyle.shadowDepth = 10.5;
        dummyStyle.wrap = true;
        Lux::optimalFontSize(dummyLyric, dummyFont, 1700, 840, 130.0, 34.0, true);
        Lux::measureTextBlock(dummyLyric, dummyStyle);

        QImage dummySurface(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        dummySurface.fill(Qt::transparent);
        QPainter p(&dummySurface);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        Lux::drawTextBlock(&p, dummyLyric, dummyStyle, QPointF(100, 100));
        p.end();
    }

    if (window.projection()) {
        window.projection()->preWarm();
    }

    if (splash)
        splash->setProgress(95, QStringLiteral("Cleaning temporary presentation cache files..."));

    data.cleanupOrphanedData();

    if (splash) {
        splash->setProgress(100, QStringLiteral("Ready"));
        QThread::msleep(180);
        splash->close();
    }

    window.showMaximized();
    window.activateWindow();

    // Developer aids: "--tab N" selects a library tab, "--select-song N" previews the N-th listed song and adds
    // it to the schedule, "--screenshot out.png" saves the main window and exits.
    const int pick = static_cast<int>(args.indexOf(QStringLiteral("--select-song")));
    if (pick >= 0 && pick + 1 < args.size()) {
        const auto songs = window.songsTab()->filteredSongs();
        const int n = args.at(pick + 1).toInt();
        if (n >= 0 && n < songs.size()) {
            window.songsTab()->userSelect(songs.at(n).id);
            window.addSongToSchedule(songs.at(n).id);
            window.songsTab()->userSelect(songs.at(n).id);
        }
    }
    const int tab = static_cast<int>(args.indexOf(QStringLiteral("--tab")));
    if (tab >= 0 && tab + 1 < args.size())
        window.library()->selectTab(args.at(tab + 1).toInt());
    // "--dialog options|options-songs|options-online|background out.png": shows that window, saves a picture of it and exits.
    const int dlgFlag = static_cast<int>(args.indexOf(QStringLiteral("--dialog")));
    if (dlgFlag >= 0 && dlgFlag + 2 < args.size()) {
        const QString which = args.at(dlgFlag + 1), outFile = args.at(dlgFlag + 2);
        QTimer::singleShot(900, &app, [&window, &data, &app, which, outFile] {
            if (which.startsWith(QStringLiteral("options"))) {
                Lux::OptionsDialog dlg(data.settings(), {}, window.adBlock(), &window);
                dlg.selectCategory(which.endsWith(QStringLiteral("songs")) ? 1 : which.endsWith(QStringLiteral("online")) ? 2 : 0);
                dlg.resize(760, which.endsWith(QStringLiteral("songs")) ? 700 : 520);
                dlg.show();
                QApplication::processEvents();
                dlg.grab().save(outFile);
            } else {
                Lux::BackgroundDialog dlg(data, window.activeGlobalBackground(), &window);
                dlg.show();
                for (int i = 0; i < 30; ++i) { QApplication::processEvents(); QThread::msleep(60); }
                dlg.grab().save(outFile);
            }
            app.quit();
        });
    }    // "--projection-test base" goes live with the selected song, then saves the projector window as
    // base_live.png / base_black.png / base_logo.png plus base_windows.txt (window state) and exits.
    const int ptest = static_cast<int>(args.indexOf(QStringLiteral("--projection-test")));
    if (ptest >= 0 && ptest + 1 < args.size()) {
        const QString snapBase = args.at(ptest + 1);
        window.goLive();
        auto projOut = [&window] { return dynamic_cast<Lux::ProjectionOutput*>(window.projection()->output()); };
        auto snap = [&window, projOut, snapBase](const QString& name) {
            if (auto* o = projOut())
                o->foreground()->grab().save(snapBase + QStringLiteral("_") + name + QStringLiteral(".png"));
        };
        auto rects = [&window, projOut, snapBase](const QString& tag) {
            QFile f(snapBase + QStringLiteral("_windows.txt"));
            if (!f.open(QIODevice::Append | QIODevice::Text))
                return;
            QTextStream ts(&f);
            ts << tag << " mainActive=" << window.isActiveWindow();
            if (auto* o = projOut()) {
                const QRect fg = Lux::DisplayHelper::windowRect(o->foreground()->winId());
                const QRect bg = Lux::DisplayHelper::windowRect(o->background()->winId());
                const qreal d = o->foreground()->devicePixelRatio();
                ts << " fg=" << fg.x() << "," << fg.y() << " " << fg.width() << "x" << fg.height() << " bg=" << bg.width() << "x" << bg.height()
                   << " qtFg=" << qRound(o->foreground()->width() * d) << "x" << qRound(o->foreground()->height() * d);
                const QImage img = o->foreground()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
                const QRgb c0 = img.pixel(0, 0);
                const bool corners = img.pixel(img.width() - 1, 0) == c0 && img.pixel(0, img.height() - 1) == c0 &&
                                     img.pixel(img.width() - 1, img.height() - 1) == c0;
                int x0 = img.width(), x1 = -1;
                for (int y = 0; y < img.height(); y += 2)
                    for (int x = 0; x < img.width(); ++x) {
                        const QRgb px = img.pixel(x, y);
                        if (qRed(px) > 150 && qGreen(px) > 150) { x0 = qMin(x0, x); x1 = qMax(x1, x); }
                    }
                const double sc = qMin(img.width() / 1920.0, img.height() / 1080.0);
                ts << " grab=" << img.width() << "x" << img.height() << " dpr=" << d << " cornersFilled=" << corners
                   << " textWidthDesign=" << (x1 >= x0 ? (x1 - x0 + 1) / sc : -1.0);
            }
            ts << "\n";
        };
        rects(QStringLiteral("immediately-after-first-GoLive"));
        QTimer::singleShot(300, &app, [rects] { rects(QStringLiteral("300ms")); });
        QTimer::singleShot(500, &app, [&window] { window.goLive(); });
        QTimer::singleShot(800, &app, [rects] { rects(QStringLiteral("after-second-GoLive")); });
        QTimer::singleShot(900, &app, [snap] { snap(QStringLiteral("live")); });
        // rapid stanza changes (regression run for the slide-change pipeline); logs how often text was rasterised
        for (int i = 0; i < 6; ++i)
            QTimer::singleShot(880 + i * 10, &app, [&window] { window.liveNext(); });
        QTimer::singleShot(950, &app, [&window] { window.selectOutputDisplay(0); });
        QTimer::singleShot(980, &app, [rects] { rects(QStringLiteral("switched-to-display-1-primary")); });
        QTimer::singleShot(985, &app, [&window] { window.selectOutputDisplay(1); });
        QTimer::singleShot(995, &app, [rects] { rects(QStringLiteral("switched-to-display-2")); });
        QTimer::singleShot(1000, &app, [&window] { window.blackButton()->click(); });
        QTimer::singleShot(1900, &app, [snap, &window] {
            snap(QStringLiteral("black"));
            window.blackButton()->click();
            window.logoButton()->click();
        });
        QTimer::singleShot(2900, &app, [snap, &window, projOut, snapBase] {
            snap(QStringLiteral("logo"));
            QFile f(snapBase + QStringLiteral("_windows.txt"));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << "shown=" << window.projection()->isWindowShown() << "\n";
                if (auto* o2 = projOut())
                    ts << "textRasterisations=" << o2->foreground()->cacheRenderCount() << "\n";
                ts << "status=" << window.displayStatusText() << "\n";
                if (auto* o = projOut()) {
                    const QRect fg = Lux::DisplayHelper::windowRect(o->foreground()->winId());
                    const QRect bg = Lux::DisplayHelper::windowRect(o->background()->winId());
                    ts << "fg=" << fg.x() << "," << fg.y() << " " << fg.width() << "x" << fg.height() << "\n";
                    ts << "bg=" << bg.x() << "," << bg.y() << " " << bg.width() << "x" << bg.height() << "\n";
                    ts << "fgVisible=" << o->foreground()->isVisible() << " fgActive=" << o->foreground()->isActiveWindow() << "\n";
                }
            }
            window.newScheduleNoPrompt();
            QCoreApplication::quit();
        });
    }
    // "--present-test <document> <outbase>": imports a presentation document through the library tab, stages and
    // goes live with page 1, moves to page 2, and saves outbase_page1.png / _page2.png (projector window),
    // outbase_main.png (main window) and outbase_log.txt, then exits. "--online-test <outbase>" opens the Online page
    // and records the WebView2 state plus a screenshot of the main window.
    const int ptd = static_cast<int>(args.indexOf(QStringLiteral("--present-test")));
    if (ptd >= 0 && ptd + 2 < args.size()) {
        const QString doc = args.at(ptd + 1), base = args.at(ptd + 2);
        auto log = QSharedPointer<QStringList>::create();
        auto step = QSharedPointer<int>::create(0);
        window.library()->selectTab(3);
        auto* tab = window.library()->presentationTab();
        QObject::connect(tab, &Lux::PresentationTab::messageRequested, &app, [log](const QString& t, const QString& m, bool) { *log << QStringLiteral("message: %1 | %2").arg(t, m); });
        const bool accepted = tab->importPath(doc);
        *log << QStringLiteral("import accepted: %1").arg(accepted);
        auto* timer = new QTimer(&app);
        timer->setInterval(150);
        QObject::connect(timer, &QTimer::timeout, &app, [&window, tab, base, log, step, timer] {
            auto fgGrab = [&window](const QString& name) {
                if (auto* o = dynamic_cast<Lux::ProjectionOutput*>(window.projection()->output()))
                    o->foreground()->grab().save(name);
            };
            // phases: 0 wait for the document (a first LibreOffice conversion can take a minute), 1 go live,
            // 2 first page captured, 3 second page captured and finished
            static int phase = 0, ticks = 0;
            ++ticks;
            auto finish = [&] {
                QFile f(base + QStringLiteral("_log.txt"));
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    QTextStream(&f) << log->join(QLatin1Char('\n')) << "\n";
                window.newScheduleNoPrompt();
                timer->stop();
                QCoreApplication::quit();
            };
            if (phase == 0) {
                if (tab->currentDocument()) {
                    *log << QStringLiteral("opened after %1 ms").arg(ticks * 150);
                    *log << QStringLiteral("pages: %1  library entries: %2").arg(tab->currentDocument()->pageCount).arg(tab->documentCombo()->count());
                    *log << QStringLiteral("pdf: %1").arg(tab->currentDocument()->pdfPath);
                    emit tab->slideGrid()->itemPressed(tab->slideGrid()->item(0));
                    *log << QStringLiteral("preview mode doc: %1  status: %2").arg(window.previewPane()->hasDocument()).arg(window.statusText());
                    window.goLive();
                    phase = 1;
                    ticks = 0;
                } else if (ticks > 1600 || (log->size() > 1 && !tab->currentDocument() && ticks > 4 && log->last().startsWith(QLatin1String("message:")))) {
                    *log << QStringLiteral("no document opened");
                    finish();
                }
                return;
            }
            if (phase == 1 && ticks >= 8) {
                fgGrab(base + QStringLiteral("_page1.png"));
                window.liveNext();
                phase = 2;
                ticks = 0;
            } else if (phase == 2 && ticks >= 6) {
                fgGrab(base + QStringLiteral("_page2.png"));
                window.grab().save(base + QStringLiteral("_main.png"));
                *log << QStringLiteral("live status: %1").arg(window.statusText());
                *log << QStringLiteral("projection: presentation=%1 page=%2 shown=%3").arg(window.projection()->state().isPresentationMode)
                            .arg(window.projection()->state().presentationPage).arg(window.projection()->isWindowShown());
                finish();
            }
        });        timer->start();
    }
    const int otd = static_cast<int>(args.indexOf(QStringLiteral("--online-test")));
    if (otd >= 0 && otd + 1 < args.size()) {
        const QString base = args.at(otd + 1);
        window.library()->selectTab(Lux::LibraryView::kOnlineTab);
        auto step = QSharedPointer<int>::create(0);
        auto* timer = new QTimer(&app);
        timer->setInterval(250);
        QObject::connect(timer, &QTimer::timeout, &app, [&window, base, step, timer] {
            auto* host = window.online()->host();
            ++*step;
            if (*step < 80 && !host->isReady() && !window.online()->isUnavailable())
                return;
            if (*step > 1000)
                return;
            QStringList log;
            log << QStringLiteral("ready=%1 unavailable=%2 reason=%3 private=%4 userData=%5")
                       .arg(host->isReady()).arg(window.online()->isUnavailable()).arg(host->unavailableReason()).arg(host->isPrivate()).arg(host->userDataFolder());
            log << QStringLiteral("runtime=%1 version=%2").arg(host->runtimeSource(), host->browserVersion());
            log << QStringLiteral("nav enabled: %1").arg(window.online()->navigationButtons().first()->isEnabled());
            QFile a(base + QStringLiteral("_page.html"));
            if (a.open(QIODevice::WriteOnly))
                a.write("<html><head><title>LuxPresenter online test</title></head><body style=\"font:40px Segoe UI;background:#fff\">WebView2 page loaded inside LuxPresenter</body></html>");
            a.close();
            host->loadUrl(QUrl::fromLocalFile(a.fileName()).toString());
            *step = 2000;
            QTimer::singleShot(2500, qApp, [&window, base, log, host, timer]() mutable {
                auto title = QSharedPointer<QString>::create();
                host->evaluate(QStringLiteral("document.title"), [title](const QString& r) { *title = r; });
                QTimer::singleShot(600, qApp, [&window, base, log, title, host, timer]() mutable {
                    log << QStringLiteral("title=%1 url=%2 urlbox=%3").arg(*title, host->currentUrl(), window.online()->urlBox()->text());
                    window.screen()->grabWindow(0).save(base + QStringLiteral("_screen.png"));   // desktop pixels: includes the WebView2 surface
                    log << QStringLiteral("loadingBarVisible=%1").arg(window.online()->loadingBar()->isVisible());
                    QFile f(base + QStringLiteral("_log.txt"));
                    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                        QTextStream(&f) << log.join(QLatin1Char('\n')) << "\n";
                    timer->stop();
                    window.newScheduleNoPrompt();
                    QCoreApplication::quit();
                });
            });
        });
        timer->start();
    }    const int shot = static_cast<int>(args.indexOf(QStringLiteral("--screenshot")));
    if (shot >= 0 && shot + 1 < args.size()) {
        const QString out = args.at(shot + 1);
        QTimer::singleShot(800, &app, [&window, out] {
            window.grab().save(out);
            window.newScheduleNoPrompt(); // a dev screenshot must not end in the "save schedule?" prompt
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
