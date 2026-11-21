// Milestone 2 tests: main window structure, navigation, theme, panes and library tabs.
// Run with QT_QPA_PLATFORM=offscreen (set by ctest) so no windows appear on screen.

#include "FakeOutput.h"
#include "Services/AppPaths.h"
#include "Services/DataService.h"
#include "Services/ProjectionService.h"
#include "Services/SongSearch.h"
#include "Services/TextRepair.h"
#include "Services/ThemeService.h"
#include "Views/AboutWindow.h"
#include "Views/Components/ClassicButton.h"
#include "Views/Components/LibraryView.h"
#include "Views/Components/MediaTab.h"
#include "Views/Components/OnlineBrowserView.h"
#include "Views/Components/PresentationTab.h"
#include "Views/Components/ScheduleView.h"
#include "Views/Components/ScripturesTab.h"
#include "Views/Components/SlideCanvas.h"
#include "Views/Components/SlidePaneView.h"
#include "Views/Components/SongsTab.h"
#include "Views/Components/TabStrip.h"
#include "Views/IconFactory.h"
#include "Views/MainWindow.h"
#include "Views/SongEditorWindow.h"
#include "Services/StageServer.h"
#include "Services/UpdateService.h"
#include "Views/StageWindow.h"
#include "Views/UpdateDialog.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace Lux;

namespace {

struct Env
{
    explicit Env(bool withBible = false)
    {
        QDir().mkpath(QStringLiteral(LUX_TEST_TMP));
        dir = std::make_unique<QTemporaryDir>(QStringLiteral(LUX_TEST_TMP "/ui-XXXXXX"));
        AppPaths::setBaseDirectoryOverride(dir->path());
        if (withBible) {
            QDir().mkpath(dir->filePath(QStringLiteral("Data")));
            QFile::copy(QStringLiteral(LUX_DATA_SRC "/bibles.json"), dir->filePath(QStringLiteral("Data/bibles.json")));
        }
        data = std::make_unique<DataService>();
        ThemeService::instance().apply(false);
    }
    ~Env()
    {
        window.reset();
        data.reset();
        AppPaths::setBaseDirectoryOverride(QString());
    }
    MainWindow& mainWindow()
    {
        if (!window) {
            window = std::make_unique<MainWindow>(*data);
            // never open real projector windows from the offscreen tests
            window->projection()->setOutputFactory(LuxTest::fakeFactory());
        }
        return *window;
    }
    QString path(const QString& rel) const { return QDir(dir->path()).filePath(rel); }

    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<DataService> data;
    std::unique_ptr<MainWindow> window;
};

QStringList menuTitles(QMenuBar* bar)
{
    QStringList out;
    for (QAction* a : bar->actions())
        out << QString(a->text()).remove(QLatin1Char('&'));
    return out;
}

QStringList actionTexts(QMenu* m)
{
    QStringList out;
    for (QAction* a : m->actions())
        out << (a->isSeparator() ? QStringLiteral("---") : a->text());
    return out;
}

QAction* findAction(QMenu* m, const QString& text)
{
    for (QAction* a : m->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

Song addSong(Env& env, const QString& title, const QString& author, const QString& lyrics, bool fav = false)
{
    Song s;
    s.title = title;
    s.author = author;
    s.lyricsRaw = lyrics;
    s.isFavorite = fav;
    env.data->saveSong(s);
    return s;
}

const QString kGraceLyrics = QStringLiteral("[Verse 1]\nAmazing grace\nhow sweet the sound\n\n[Chorus]\nPraise Him");
bool hasNonBackgroundPixels(const QImage& img, const QRect& r, QRgb bg)
{
    for (int y = r.top(); y <= r.bottom(); y += 2)
        for (int x = r.left(); x <= r.right(); x += 2)
            if (img.pixel(x, y) != bg)
                return true;
    return false;
}

} // namespace

class UiTests : public QObject
{
    Q_OBJECT

private slots:
    void windowBasics()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        QCOMPARE(w.windowTitle(), QStringLiteral("LuxPresenter - Default Profile"));
        QVERIFY(w.minimumSize().width() >= 750 && w.minimumSize().height() >= 460);
        QCOMPARE(menuTitles(w.menuBar()),
                 (QStringList{"File", "Schedule", "Songs", "Scriptures", "Live", "View", "Profiles", "Help"}));
        QCOMPARE(w.statusText(), QStringLiteral("Ready"));
        QVERIFY(!w.displayStatusText().isEmpty());
        QVERIFY(!w.windowIcon().isNull());
    }

    void menusFollowReference()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        const auto menus = w.menuBar()->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(menus.size(), 8);
        QMenu* file = menus[0];
        QCOMPARE(actionTexts(file),
                 (QStringList{"New Schedule", "Open Schedule...", "Save Schedule", "Save Schedule As...", "---",
                              "Open Songs Database Folder...", "---", "Options...", "---", "Exit"}));
        QVERIFY(findAction(file, QStringLiteral("Exit"))->isEnabled());
        QVERIFY(findAction(file, QStringLiteral("New Schedule"))->isEnabled());
        QVERIFY(findAction(file, QStringLiteral("Options..."))->isEnabled()); // Milestone 6
        QCOMPARE(findAction(file, QStringLiteral("New Schedule"))->shortcut(), QKeySequence(QKeySequence::New));
        QCOMPARE(actionTexts(menus[3]), (QStringList{"Import Bible Translation (.json)..."}));
        QCOMPARE(actionTexts(menus[4]),
                 (QStringList{"Go Live\tF5", "Black\tCtrl+B", "Clear\tCtrl+C", "Logo\tF8", "Live Output", "---", "Output Display", "---", "Stage Display (Confidence Monitor)\tCtrl+Shift+D"}));
        QVERIFY(findAction(menus[4], QStringLiteral("Go Live\tF5"))->isEnabled()); // Milestone 4
        QVERIFY(findAction(menus[4], QStringLiteral("Black\tCtrl+B"))->isCheckable());
        QVERIFY(findAction(menus[4], QStringLiteral("Stage Display (Confidence Monitor)\tCtrl+Shift+D"))->isEnabled());
        QCOMPARE(actionTexts(menus[6]).first(), QStringLiteral("Default Profile"));
        QVERIFY(findAction(menus[6], QStringLiteral("Default Profile"))->isChecked());
        QVERIFY(findAction(menus[7], QStringLiteral("About LuxPresenter"))->isEnabled());
        QVERIFY(findAction(menus[7], QStringLiteral("Check for Updates..."))->isEnabled());
        QCOMPARE(w.themeAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_T));
    }

    void toolbarButtons()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        QCOMPARE(w.toolbarButtons().size(), 13);
        int enabled = 0;
        for (ClassicButton* b : w.toolbarButtons()) {
            QVERIFY(!b->toolTip().isEmpty());
            enabled += b->isEnabled() ? 1 : 0;
        }
        // new/edit song, globe, donate, theme toggle + Go Live, Logo, Black, Clear, Live (Milestone 4);
        // + Options (M6); Background (M6); Alert Message
        QCOMPARE(enabled, 13);
        QVERIFY(w.alertButton() && w.alertButton()->isEnabled());
        QVERIFY(w.globeButton()->isEnabled() && w.donateButton()->isEnabled() && w.themeButton()->isEnabled());
        QVERIFY(w.newSongButton()->isEnabled() && w.editSongButton()->isEnabled());
        QVERIFY(w.themeButton()->toolTip().contains(QStringLiteral("Dark Mode")));
        QVERIFY(w.themeButton()->sizeHint().height() >= 28);
    }

    void libraryTabsAndNavigation()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        LibraryView* lib = w.library();
        QCOMPARE(lib->tabCount(), 5);
        QStringList names;
        for (int i = 0; i < lib->tabCount(); ++i)
            names << lib->tabText(i);
        QCOMPARE(names, (QStringList{"Songs", "Scriptures", "Media", "Presentation", "Online"}));
        QCOMPARE(lib->currentTab(), 0);
        QCOMPARE(lib->pages()->currentWidget(), static_cast<QWidget*>(lib->songsTab()));
        lib->selectTab(1);
        QCOMPARE(lib->pages()->currentWidget(), static_cast<QWidget*>(lib->scripturesTab()));
        lib->selectTab(2);
        QCOMPARE(lib->pages()->currentWidget(), static_cast<QWidget*>(lib->mediaTab()));
        lib->selectTab(3);
        QCOMPARE(lib->pages()->currentWidget(), static_cast<QWidget*>(lib->presentationTab()));
        QVERIFY(!w.isOnlineMode());
        QCOMPARE(w.workStack()->currentIndex(), 0);
        lib->selectTab(99); // ignored
        QCOMPARE(lib->currentTab(), 3);
    }

    void onlineModeSwitchesWorkArea()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        w.library()->selectTab(2); // Media
        QCOMPARE(w.lastLibraryTab(), 2);
        w.library()->selectTab(LibraryView::kOnlineTab);
        QVERIFY(w.isOnlineMode());
        QCOMPARE(w.workStack()->currentWidget(), static_cast<QWidget*>(w.online()));
        QCOMPARE(w.lastLibraryTab(), 2); // online does not overwrite the last real tab

        // Globe leaves online mode back to the previous tab
        w.globeButton()->click();
        QVERIFY(!w.isOnlineMode());
        QCOMPARE(w.library()->currentTab(), 2);
        QCOMPARE(w.workStack()->currentWidget(), static_cast<QWidget*>(w.mainSplitter()));
        // ... and enters it again
        w.globeButton()->click();
        QVERIFY(w.isOnlineMode());
        // "Close Online" returns as well
        w.online()->closeButton()->click();
        QVERIFY(!w.isOnlineMode());
        QCOMPARE(w.library()->currentTab(), 2);

        // Online browser toolbar: only Close is active until the WebView2 browser is ready (it starts when the page is shown)
        for (QPushButton* b : w.online()->navigationButtons())
            QVERIFY(!b->isEnabled());
        QVERIFY(w.online()->closeButton()->isEnabled());
        QVERIFY(!w.online()->urlBox()->isEnabled());
    }

    void workAreaProportions()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        w.resize(1200, 760);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(50);

        const QList<int> cols = w.mainSplitter()->sizes();
        QCOMPARE(cols.size(), 3);
        QVERIFY2(qAbs(cols[0] - 340) <= 2, qPrintable(QString::number(cols[0])));
        QVERIFY2(qAbs(cols[1] - cols[2]) <= 2, qPrintable(QStringLiteral("%1 vs %2").arg(cols[1]).arg(cols[2])));
        QCOMPARE(w.mainSplitter()->handleWidth(), 5);

        const QList<int> rows = w.leftSplitter()->sizes();
        const double scheduleShare = double(rows[0]) / (rows[0] + rows[1]);
        QVERIFY2(qAbs(scheduleShare - 0.45) <= 0.02, qPrintable(QString::number(scheduleShare)));
        QCOMPARE(w.leftSplitter()->handleWidth(), 5);
        QVERIFY(w.previewPane()->width() >= 260 && w.livePane()->width() >= 260);
        QCOMPARE(w.leftSplitter()->parentWidget()->minimumWidth(), 240);
        QCOMPARE(w.leftSplitter()->parentWidget()->maximumWidth(), 520);
        QCOMPARE(w.statusBar()->height(), 22);

        // growing the window keeps the left column at 340 (fixed GridLength) and splits the rest
        w.resize(1500, 760);
        QTest::qWait(50);
        const QList<int> wide = w.mainSplitter()->sizes();
        QVERIFY2(qAbs(wide[0] - 340) <= 2, qPrintable(QString::number(wide[0])));
        QVERIFY(qAbs(wide[1] - wide[2]) <= 2);
    }

    void themeToggleAndPersistence()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        QVERIFY(!ThemeService::instance().isDark());
        QCOMPARE(w.themeAction()->text(), QString::fromUtf8("\U0001F319 Dark Mode"));
        w.themeButton()->click();
        QVERIFY(ThemeService::instance().isDark());
        QVERIFY(env.data->settings().isDarkMode);
        QCOMPARE(w.themeAction()->text(), QString::fromUtf8("\u2600 Light Mode"));
        QVERIFY(w.themeButton()->toolTip().contains(QStringLiteral("Light Mode")));
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor(0x24, 0x26, 0x2C));

        { // persisted in settings.json
            DataService reloaded;
            QVERIFY(reloaded.settings().isDarkMode);
        }
        w.themeAction()->trigger();
        QVERIFY(!ThemeService::instance().isDark());
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor(0xEC, 0xE9, 0xD8));
        QVERIFY(!env.data->settings().isDarkMode);
    }

    void themePalettesMatchReference()
    {
        const Theme l = Theme::makeLight();
        const Theme d = Theme::makeDark();
        QCOMPARE(l.panelBg, QColor("#FFFFFF"));
        QCOMPARE(l.border, QColor("#B2BAC2"));
        QCOMPARE(l.toolbarBottom, QColor("#E6EBF0"));
        QCOMPARE(d.windowBg, QColor("#24262C"));
        QCOMPARE(d.donateText, QColor("#FBBF24"));
        QCOMPARE(d.headerTop, QColor("#32353E"));
        const QString qss = ThemeService::buildStyleSheet(d);
        QVERIFY(!qss.contains(QLatin1Char('@'))); // every token substituted
        QVERIFY(qss.contains(QStringLiteral("#24262c")));
    }

    void iconsRender()
    {
        for (const Theme& t : {Theme::makeLight(), Theme::makeDark()}) {
            for (Icon i : {Icon::Add, Icon::Edit, Icon::Display, Icon::Globe, Icon::Donate, Icon::Background,
                           Icon::Alert, Icon::Logo, Icon::BlackSquare, Icon::Clear}) {
                const QImage img = IconFactory::pixmap(i, 16, 16, 2.0, t).toImage();
                QCOMPARE(img.size(), QSize(32, 32));
                bool any = false;
                for (int y = 0; y < img.height() && !any; ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (qAlpha(img.pixel(x, y)) > 0) { any = true; break; }
                QVERIFY2(any, qPrintable(QStringLiteral("icon %1 is blank").arg(static_cast<int>(i))));
            }
        }
    }

    void panesAndModes()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        SlidePaneView* pv = w.previewPane();
        SlidePaneView* lv = w.livePane();
        QCOMPARE(pv->headerText(), QStringLiteral("Preview - "));
        QCOMPARE(lv->headerText(), QStringLiteral("Live - "));
        QVERIFY(!pv->isLive() && lv->isLive());
        QCOMPARE(pv->mode(), SlidePaneView::Mode::Slides);
        pv->setMode(SlidePaneView::Mode::Media);
        QCOMPARE(pv->pages()->currentIndex(), 1);
        pv->setMode(SlidePaneView::Mode::PresentationDoc);
        QCOMPARE(pv->pages()->currentIndex(), 2);
        pv->setMode(SlidePaneView::Mode::Slides);
        QCOMPARE(pv->pages()->currentIndex(), 0);

        // media transport belongs to Milestone 6; the document Prev/Next buttons stay disabled until a document is shown
        QCOMPARE(pv->transportButtons().size(), 6);
        for (QPushButton* b : pv->transportButtons())
            QVERIFY(!b->isEnabled());
        QVERIFY(pv->videoNote()->isHidden()); // preview pane: no "playing on live output" note
        QVERIFY(!lv->videoNote()->isHidden());

        pv->setItemTitle(QStringLiteral("Amazing Grace"));
        QCOMPARE(pv->headerText(), QStringLiteral("Preview - Amazing Grace"));
        lv->setItemSubtitle(QStringLiteral("Slide 2"));
        QVERIFY(lv->docStatus()->text().startsWith(QStringLiteral("LIVE")));
        pv->setItemSubtitle(QStringLiteral("Slide 2"));
        QCOMPARE(pv->docStatus()->text(), QStringLiteral("Slide 2"));
    }

    void stanzaListDrivesCanvas()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        SlidePaneView* pv = w.previewPane();
        pv->resize(420, 640);
        pv->show();
        QList<Slide> slides;
        Slide a; a.label = QStringLiteral("Verse 1"); a.content = QStringLiteral("LINE ONE\nLINE TWO"); a.type = SlideType::Verse;
        Slide b; b.label = QStringLiteral("Chorus"); b.content = QStringLiteral("CHORUS TEXT"); b.type = SlideType::Chorus;
        Slide c; c.label = QStringLiteral("John 3:16"); c.title = QStringLiteral("John 3:16"); c.content = QStringLiteral("FOR GOD SO LOVED");
        c.type = SlideType::Scripture;
        slides << a << b << c;
        pv->setSlides(slides);
        QCOMPARE(pv->stanzaList()->count(), 3);
        QVERIFY(!pv->canvas()->hasSlide());
        pv->setCurrentSlideIndex(1);
        QCOMPARE(pv->currentSlideIndex(), 1);
        QVERIFY(pv->canvas()->hasSlide());
        QCOMPARE(pv->canvas()->slide().label, QStringLiteral("Chorus"));

        // A standalone canvas actually paints text (white fill) over the dark-blue default background.
        SlideCanvas canvas;
        canvas.resize(640, 360);
        canvas.setSlide(b);
        const QImage img = canvas.grab().toImage();
        bool white = false;
        for (int y = 10; y < 340 && !white; y += 2)
            for (int x = 100; x < 540; x += 2)
                if (qRed(img.pixel(x, y)) > 240 && qGreen(img.pixel(x, y)) > 240 && qBlue(img.pixel(x, y)) > 240) { white = true; break; }
        QVERIFY(white);
        QVERIFY(qBlue(img.pixel(5, 5)) > qRed(img.pixel(5, 5))); // dark-blue default background
        canvas.clearSlide();
        const QImage blank = canvas.grab().toImage();
        bool anyWhite = false;
        for (int y = 10; y < 340 && !anyWhite; y += 2)
            for (int x = 100; x < 540; x += 2)
                if (qRed(blank.pixel(x, y)) > 240) { anyWhite = true; break; }
        QVERIFY(!anyWhite);
        pv->setSlides({});
        QVERIFY(!pv->canvas()->hasSlide());
    }

    void songsTabShowsStoredSongs()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        SongsTab* tab = w.songsTab();
        QCOMPARE(tab->list()->topLevelItemCount(), 0);
        QVERIFY(!tab->emptyCard()->isHidden());
        QCOMPARE(tab->list()->headerItem()->text(0), QStringLiteral("Title"));
        QCOMPARE(tab->list()->headerItem()->text(1), QStringLiteral("Author"));
        QVERIFY(tab->openFolderButton()->isEnabled() && tab->importButton()->isEnabled());
        QVERIFY(tab->sampleButton()->isEnabled() && tab->createButton()->isEnabled());
        QVERIFY(tab->titleSearch()->isEnabled() && tab->lyricsSearch()->isEnabled() && tab->favoritesButton()->isEnabled());

        Song s1; s1.title = QStringLiteral("Zebra"); s1.author = QStringLiteral("Z. Author"); s1.lyricsRaw = QStringLiteral("z");
        Song s2; s2.title = QStringLiteral("alpha"); s2.author = QStringLiteral("A. Author"); s2.lyricsRaw = QStringLiteral("a");
        QVERIFY(env.data->saveSong(s1));
        QVERIFY(env.data->saveSong(s2));
        tab->reload();
        QCOMPARE(tab->list()->topLevelItemCount(), 2);
        QCOMPARE(tab->list()->topLevelItem(0)->text(0), QStringLiteral("alpha")); // title order
        QCOMPARE(tab->list()->topLevelItem(1)->text(1), QStringLiteral("Z. Author"));
        QVERIFY(tab->emptyCard()->isHidden());
    }
    void scripturesTabStructure()
    {
        Env env(true);
        MainWindow& w = env.mainWindow();
        ScripturesTab* tab = w.library()->scripturesTab();
        QCOMPARE(tab->versionTabs()->count(), 1);   // only the bundled KJV until the user imports a translation
        QCOMPARE(tab->versionTabs()->tabText(0), QStringLiteral("KJV"));
        QCOMPARE(tab->selectedVersion(), QStringLiteral("KJV"));
        QVERIFY(tab->booksButton()->isEnabled() && tab->importButton()->isEnabled());
        QVERIFY(tab->bookCombo()->isEnabled() && tab->searchBox()->isEnabled());

        // Books / Import labels must be fully visible at the normal 340px left column.
        w.resize(1200, 760);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.library()->selectTab(1);
        QTest::qWait(50);
        QVERIFY2(qAbs(w.mainSplitter()->sizes()[0] - 340) <= 2, "left column must stay at 340");
        for (QPushButton* b : {tab->booksButton(), tab->importButton()}) {
            const auto labels = b->findChildren<QLabel*>();
            QCOMPARE(labels.size(), 2);
            for (QLabel* l : labels)
                QVERIFY2(l->width() >= l->sizeHint().width(),
                         qPrintable(QStringLiteral("clipped '%1': %2 < %3").arg(l->text()).arg(l->width()).arg(l->sizeHint().width())));
            QVERIFY(b->width() >= b->layout()->sizeHint().width());
            QVERIFY(b->geometry().right() <= tab->width());
        }
        QCOMPARE(tab->chapterCombo()->width(), 54);
    }

    void mediaAndPresentationTabs()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        MediaTab* media = w.library()->mediaTab();
        QStringList folders;
        for (int i = 0; i < media->folderCombo()->count(); ++i)
            folders << media->folderCombo()->itemText(i);
        QCOMPARE(folders, (QStringList{"Videos", "Images", "Audio"}));
        QStringList sub;
        for (int i = 0; i < media->subTabs()->count(); ++i)
            sub << media->subTabs()->tabText(i);
        QCOMPARE(sub, (QStringList{"Videos", "Images", "Audio"}));
        QVERIFY(media->addButton()->isEnabled());
        QVERIFY(!media->deleteButton()->isEnabled());
        QCOMPARE(media->grid()->count(), 0);

        QFile f(env.path(QStringLiteral("clip.mp4")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        MediaItem m; m.name = QStringLiteral("Clip"); m.filePath = f.fileName(); m.type = MediaType::Video;
        QVERIFY(env.data->addMediaItem(m));
        media->refresh();
        QCOMPARE(media->grid()->count(), 1);
        QCOMPARE(media->grid()->item(0)->text(), QStringLiteral("Clip"));
        media->grid()->item(0)->setSelected(true);
        QVERIFY(media->deleteButton()->isEnabled());
        emit media->deleteButton()->clicked();
        QCOMPARE(media->grid()->count(), 0);
        QVERIFY(!media->deleteButton()->isEnabled());

        PresentationTab* pres = w.library()->presentationTab();
        QCOMPARE(pres->documentCombo()->count(), 0);
        PresentationDocItem d; d.name = QStringLiteral("Sunday Deck"); d.filePath = f.fileName();
        QVERIFY(env.data->addPresentationDoc(d));
        pres->refresh();
        QCOMPARE(pres->documentCombo()->count(), 1);
        QCOMPARE(pres->documentCombo()->itemText(0), QStringLiteral("Sunday Deck"));
        QVERIFY(pres->importButton()->isEnabled());          // Milestone 5: import works
        QVERIFY(pres->filterBox()->isEnabled());
        QVERIFY(!pres->deleteButton()->isEnabled());         // nothing selected
        QCOMPARE(pres->importButton()->text(), QString::fromUtf8("\u2795 Import Presentation"));
    }

    void scheduleViewStructure()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        ScheduleView* sv = w.schedule();
        QCOMPARE(sv->itemCount(), 0);
        QCOMPARE(actionTexts(sv->headerMenu()).size(), 6);
        QVERIFY(sv->actionNew()->isEnabled() && sv->actionOpen()->isEnabled() && sv->actionSave()->isEnabled());
        QVERIFY(!sv->actionClear()->isEnabled()); // nothing to clear yet
        for (QToolButton* tb : {sv->addButton(), sv->removeButton(), sv->moveUpButton(), sv->moveDownButton(), sv->optionsButton()})
            QCOMPARE(tb->width(), 22);
        QVERIFY(sv->addButton()->isEnabled());
        QVERIFY(!sv->removeButton()->isEnabled()); // RemoveItemCommand needs a selection
    }
    void aboutDialog()
    {
        Env env;
        AboutWindow dlg;
        QCOMPARE(dlg.windowTitle(), QStringLiteral("About LuxPresenter"));
        QCOMPARE(dlg.size(), QSize(460, 330));
        bool foundVersion = false, foundCreator = false;
        for (QLabel* l : dlg.findChildren<QLabel*>()) {
            foundVersion |= l->text() == QStringLiteral("Version %1").arg(QStringLiteral(LUX_VERSION));
            foundCreator |= l->text().contains(QStringLiteral("Luxbane"));
        }
        QVERIFY(foundVersion && foundCreator);
    }

    // ===================== Milestone 3 =====================

    void songSearchFlowAndPreview()
    {
        Env env;
        const Song grace = addSong(env, QStringLiteral("Amazing Grace"), QStringLiteral("John Newton"), kGraceLyrics);
        const Song blessed = addSong(env, QStringLiteral("Blessed Assurance"), QStringLiteral("Fanny Crosby"), QStringLiteral("[Verse 1]\nThis is my story"), true);
        addSong(env, QStringLiteral("Karena Kasih"), QStringLiteral("X"), QStringLiteral("karena kasih-Nya"));
        MainWindow& w = env.mainWindow();
        SongsTab* tab = w.songsTab();
        tab->reload();
        QCOMPARE(tab->list()->topLevelItemCount(), 3);
        QCOMPARE(tab->list()->topLevelItem(0)->text(0), QStringLiteral("Amazing Grace"));
        QVERIFY(tab->emptyCard()->isHidden());
        QVERIFY(w.previewPane()->headerText() == QStringLiteral("Preview - ")); // no preview at start-up

        QSignalSpy preview(tab, &SongsTab::songSelectedForPreview);
        tab->setTitleQuery(QStringLiteral("blessed"));
        tab->applyFilterImmediate();
        QCOMPARE(tab->filteredSongs().size(), 1);
        QCOMPARE(tab->selectedSongId(), blessed.id);       // first result is selected ...
        QCOMPARE(preview.count(), 1);                      // ... and previewed
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Blessed Assurance"));
        QCOMPARE(w.statusText(), QStringLiteral("Preview: Blessed Assurance"));
        QCOMPARE(w.previewSource(), MainWindow::PreviewSource::SongList);
        QVERIFY(tab->paneActive());
        QVERIFY(!w.schedule()->paneActive());

        // Title and lyrics queries are mutually exclusive
        tab->setLyricsQuery(QStringLiteral("sweet sound"));
        QVERIFY(tab->titleQuery().isEmpty());
        tab->applyFilterImmediate();
        QCOMPARE(tab->filteredSongs().size(), 1);
        QCOMPARE(tab->selectedSongId(), grace.id);
        tab->setTitleQuery(QStringLiteral("karna")); // Indonesian contraction synonym
        QVERIFY(tab->lyricsQuery().isEmpty());
        tab->applyFilterImmediate();
        QCOMPARE(tab->filteredSongs().first().title, QStringLiteral("Karena Kasih"));

        // Debounced typing: the filter applies ~180 ms after the last keystroke
        tab->setTitleQuery(QStringLiteral("amaz"));
        QCOMPARE(tab->filteredSongs().first().title, QStringLiteral("Karena Kasih")); // not yet
        QTRY_COMPARE_WITH_TIMEOUT(tab->filteredSongs().first().title, QStringLiteral("Amazing Grace"), 1500);

        // Favourites + clearing the search restores the full list
        tab->setTitleQuery(QString());
        tab->setFavoritesOnly(true);
        QCOMPARE(tab->filteredSongs().size(), 1);
        QCOMPARE(tab->filteredSongs().first().id, blessed.id);
        tab->setFavoritesOnly(false);
        QCOMPARE(tab->list()->topLevelItemCount(), 3);
    }

    void selectingSongLoadsStanzasIntoPreview()
    {
        Env env;
        const Song grace = addSong(env, QStringLiteral("Amazing Grace"), QStringLiteral("John Newton"), kGraceLyrics);
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        w.songsTab()->userSelect(grace.id);
        SlidePaneView* pv = w.previewPane();
        QCOMPARE(pv->headerText(), QStringLiteral("Preview - Amazing Grace"));
        QCOMPARE(pv->itemSubtitle(), QStringLiteral("By John Newton"));
        QCOMPARE(pv->slides().size(), 2);
        QCOMPARE(pv->stanzaList()->count(), 2);
        QCOMPARE(pv->currentSlideIndex(), 0);
        QVERIFY(pv->canvas()->hasSlide());
        QCOMPARE(pv->slides()[0].label, QStringLiteral("Verse 1"));
        QCOMPARE(pv->slides()[0].content, QStringLiteral("AMAZING GRACE\nHOW SWEET THE SOUND")); // default: capitalise all words
        QCOMPARE(pv->slides()[1].type, SlideType::Chorus);
        QCOMPARE(pv->canvas()->background(), QStringLiteral("#001833"));
        QVERIFY2(pv->canvas()->fullScaleFontSize() >= 34 && pv->canvas()->fullScaleFontSize() <= 13 * 6.5 + 1.0, // the reference search can overshoot the cap slightly
                 qPrintable(QString::number(pv->canvas()->fullScaleFontSize())));

        pv->setCurrentSlideIndex(1);
        QCOMPARE(pv->canvas()->slide().label, QStringLiteral("Chorus"));
        // Selecting only previews: the live pane stays empty until Go Live
        QVERIFY(w.livePane()->slides().isEmpty());
        // Double-click goes live (Milestone 4)
        emit w.songsTab()->songDoubleClicked(grace.id);
        QCOMPARE(w.livePane()->slides().size(), 2);
    }

    // ---- Milestone 4: Preview -> Live -> projector ------------------------------------------------
    void goLiveFlowAndToggles()
    {
        Env env;
        const Song grace = addSong(env, QStringLiteral("Amazing Grace"), QStringLiteral("John Newton"), kGraceLyrics);
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        w.songsTab()->userSelect(grace.id);
        ProjectionService* svc = w.projection();
        QVERIFY(w.goLiveButton()->isEnabled() && w.logoButton()->isEnabled() && w.blackButton()->isEnabled() &&
                w.clearButton()->isEnabled() && w.liveButton()->isEnabled());
        QVERIFY(!w.isLiveOutputActive());
        QVERIFY(!svc->isWindowShown());

        w.previewPane()->setCurrentSlideIndex(1);
        w.goLiveButton()->click();
        QVERIFY(w.isLiveOutputActive() && w.liveButton()->isChecked());
        QCOMPARE(w.livePane()->slides().size(), 2);
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
        QCOMPARE(w.livePane()->headerText(), QStringLiteral("Live - Amazing Grace"));
        QCOMPARE(w.livePane()->canvas()->slide().label, QStringLiteral("Chorus"));
        QCOMPARE(w.statusText(), QStringLiteral("Live: Amazing Grace - Chorus"));
        auto* fo = dynamic_cast<LuxTest::FakeOutput*>(svc->output());
        QVERIFY(fo && fo->shown);
        QCOMPARE(fo->updates.last().activeSlide->label, QStringLiteral("Chorus"));
        QCOMPARE(fo->updates.last().activeBackground, QStringLiteral("#001833"));
        QVERIFY(fo->updates.last().isLive);
        QCOMPARE(w.action(QStringLiteral("Live"), QStringLiteral("Live Output"))->isChecked(), true);

        // Clicking/keying a Live stanza projects it; the Preview is untouched.
        w.livePane()->setCurrentSlideIndex(0);
        QCOMPARE(fo->updates.last().activeSlide->label, QStringLiteral("Verse 1"));
        QCOMPARE(w.statusText(), QStringLiteral("Live: Amazing Grace - Verse 1"));
        QCOMPARE(w.previewPane()->currentSlideIndex(), 1);

        // Black / Clear / Logo: buttons, menu actions, service state and the Live pane stay in step.
        w.blackButton()->click();
        QVERIFY(svc->state().isBlack && fo->updates.last().isBlack);
        QVERIFY(w.livePane()->canvas()->isBlack());
        QVERIFY(w.action(QStringLiteral("Live"), QStringLiteral("Black\tCtrl+B"))->isChecked());
        w.action(QStringLiteral("Live"), QStringLiteral("Black\tCtrl+B"))->trigger();
        QVERIFY(!svc->state().isBlack && !w.blackButton()->isChecked() && !w.livePane()->canvas()->isBlack());
        w.clearButton()->click();
        QVERIFY(svc->state().isClear && w.livePane()->canvas()->isTextCleared());
        QVERIFY(w.action(QStringLiteral("Live"), QStringLiteral("Clear\tCtrl+C"))->isChecked());
        w.clearButton()->click();
        QVERIFY(!svc->state().isClear && !w.livePane()->canvas()->isTextCleared());
        w.logoButton()->click();
        QVERIFY(svc->state().isLogo && fo->updates.last().isLogo);
        w.action(QStringLiteral("Live"), QStringLiteral("Logo\tF8"))->trigger();
        QVERIFY(!svc->state().isLogo && !w.logoButton()->isChecked());

        // Live toggle: off hides the output (state kept), on shows it again.
        w.liveButton()->click();
        QVERIFY(!w.isLiveOutputActive() && !fo->shown && !svc->state().isLive);
        w.action(QStringLiteral("Live"), QStringLiteral("Live Output"))->trigger();
        QVERIFY(w.isLiveOutputActive() && fo->shown && svc->state().isLive);
        QCOMPARE(fo->updates.last().activeSlide->label, QStringLiteral("Verse 1"));

        // Go Live again from the Preview (slide 1) replaces what the output shows.
        w.goLive();
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
        QCOMPARE(fo->updates.last().activeSlide->label, QStringLiteral("Chorus"));
    }

    void goLiveFromDoubleClickAndContextMenus()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), QStringLiteral("[Verse 1]\nalpha one\n\n[Verse 2]\nalpha two"));
        const Song b = addSong(env, QStringLiteral("Beta"), QString(), QStringLiteral("beta only"));
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();

        emit w.songsTab()->songDoubleClicked(b.id);
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Beta"));
        QCOMPARE(w.statusText(), QStringLiteral("Live: Beta - "));
        QVERIFY(w.projection()->isWindowShown());

        w.songsTab()->userSelect(a.id);
        w.songsTab()->actionGoLive()->trigger();
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Alpha"));
        QCOMPARE(w.livePane()->slides().size(), 2);

        // Preview stanza double-click goes live at that stanza.
        emit w.previewPane()->slideDoubleClicked(1);
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
        QCOMPARE(w.projection()->state().activeSlide->content, QStringLiteral("ALPHA TWO"));

        // Schedule items: double-click / context-menu Go Live
        w.addSongToSchedule(b.id);
        emit w.schedule()->itemActivated(0);
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Beta"));
        w.songsTab()->userSelect(a.id);
        w.goLive();
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Alpha"));
        emit w.schedule()->goLiveRequested(0);
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Beta"));
    }

    void liveKeyboardShortcuts()
    {
        Env env;
        const Song s = addSong(env, QStringLiteral("Four"), QString(), QStringLiteral("[Verse 1]\none\n\n[Verse 2]\ntwo\n\n[Verse 3]\nthree\n\n[Verse 4]\nfour"));
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        w.songsTab()->userSelect(s.id);
        auto key = [&](int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QKeyEvent e(QEvent::KeyPress, k, m);
            return w.handleProjectionKey(&e);
        };
        QVERIFY(key(Qt::Key_F5));                              // Go Live
        QVERIFY(w.isLiveOutputActive());
        QCOMPARE(w.livePane()->currentSlideIndex(), 0);
        QVERIFY(key(Qt::Key_Space));
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
        QVERIFY(key(Qt::Key_PageDown));
        QCOMPARE(w.livePane()->currentSlideIndex(), 2);
        QVERIFY(key(Qt::Key_Right));
        QVERIFY(key(Qt::Key_Down));                            // clamps at the last stanza
        QCOMPARE(w.livePane()->currentSlideIndex(), 3);
        QCOMPARE(w.projection()->state().activeSlide->label, QStringLiteral("Verse 4"));
        QVERIFY(key(Qt::Key_Up));
        QVERIFY(key(Qt::Key_PageUp));
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
        QVERIFY(key(Qt::Key_Left));
        QVERIFY(key(Qt::Key_Left));                            // clamps at the first
        QCOMPARE(w.livePane()->currentSlideIndex(), 0);
        QVERIFY(key(Qt::Key_End));
        QCOMPARE(w.livePane()->currentSlideIndex(), 3);
        QVERIFY(key(Qt::Key_Home));
        QCOMPARE(w.livePane()->currentSlideIndex(), 0);

        QVERIFY(key(Qt::Key_B, Qt::ControlModifier));
        QVERIFY(w.projection()->state().isBlack);
        QVERIFY(key(Qt::Key_F6));
        QVERIFY(!w.projection()->state().isBlack);
        QVERIFY(key(Qt::Key_Period));
        QVERIFY(w.projection()->state().isBlack);
        QVERIFY(key(Qt::Key_B, Qt::ControlModifier));
        QVERIFY(key(Qt::Key_F7));
        QVERIFY(w.projection()->state().isClear);
        QVERIFY(key(Qt::Key_C, Qt::ControlModifier));
        QVERIFY(!w.projection()->state().isClear);
        QVERIFY(key(Qt::Key_F8));
        QVERIFY(w.projection()->state().isLogo);
        QVERIFY(key(Qt::Key_F8));

        QVERIFY(!key(Qt::Key_A));                              // other keys are left alone
        QVERIFY(!key(Qt::Key_Space, Qt::AltModifier));
        QVERIFY(!key(Qt::Key_S, Qt::ControlModifier));
    }

    void outputDisplayMenuAndStatus()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        auto screen = [](int i, bool primary) {
            Lux::ScreenInfo s;
            s.index = i;
            s.physicalWidth = primary ? 1920 : 1280;
            s.physicalHeight = primary ? 1080 : 720;
            s.isPrimary = primary;
            return s;
        };
        w.projection()->setOutputFactory(LuxTest::fakeFactory({screen(0, true), screen(1, false)}));
        emit w.displayMenu()->aboutToShow();
        QStringList items;
        for (QAction* a : w.displayMenu()->actions())
            items << a->text();
        QCOMPARE(items, (QStringList{"Automatic (prefer secondary display)", "1: Primary Monitor (1920x1080)",
                                     "2: Secondary Monitor (1280x720)"}));
        QVERIFY(w.displayMenu()->actions()[0]->isChecked());

        w.goLive();
        QCOMPARE(w.projection()->activeScreenNumber(), 2);
        QCOMPARE(w.displayStatusText(), QStringLiteral("2 Displays (Projector Output Active on Screen 2)"));
        w.displayMenu()->actions()[1]->trigger();            // choose the primary monitor
        QCOMPARE(w.projection()->selectedMonitorIndex(), 0);
        QCOMPARE(w.projection()->activeScreenNumber(), 1);
        QCOMPARE(w.displayStatusText(), QStringLiteral("2 Displays (Projector Output Active on Screen 1)"));
        emit w.displayMenu()->aboutToShow();
        QVERIFY(w.displayMenu()->actions()[1]->isChecked());
        w.liveButton()->click();                              // output off
        QCOMPARE(w.displayStatusText(), QStringLiteral("2 Displays Detected"));
    }

    // ---- stanza list: blank-line spacing and unselectable separators --------------------------
    void songBlankLinesBecomeStanzaSpacing()
    {
        // KTP4-style lyrics: one blank line between lines, two before the last group.
        Song s;
        s.title = QStringLiteral("KTP4");
        s.lyricsRaw = QStringLiteral("ONE\n\nTWO\n\nTHREE\n\n\nFOUR\n\n[Chorus]\nFIVE\nFIVE B\n\r\n\r\n\r\n\r\nSIX");
        const PresentationItem item = s.toPresentationItem(nullptr);
        QCOMPARE(item.slides.size(), 6);
        QList<int> gaps;
        for (const Slide& sl : item.slides)
            gaps << sl.blankLinesBefore;
        QCOMPARE(gaps, (QList<int>{0, 1, 1, 2, 1, 4}));
        QCOMPARE(item.slides[0].content, QStringLiteral("ONE"));       // lyrics text itself is untouched
        QCOMPARE(item.slides[4].content, QStringLiteral("FIVE\nFIVE B"));
        // the spacing hint is not part of the stored/serialised formats
        QVERIFY(!item.slides[3].toJson().contains(QStringLiteral("BlankLinesBefore")));
        QVERIFY(!item.slides[3].toJson(false).contains(QStringLiteral("BlankLinesBefore")));
        QCOMPARE(Slide::fromJson(item.slides[3].toJson()).blankLinesBefore, -1);
        // identical for songs coming back from JSON (database / backup import)
        const Song again = Song::fromJson(s.toJson());
        QCOMPARE(again.toPresentationItem(nullptr).slides[3].blankLinesBefore, 2);
    }

    void stanzaListShowsSpacingAndSeparatorsAreNotSelectable()
    {
        Song s;
        s.title = QStringLiteral("KTP4");
        s.lyricsRaw = QStringLiteral("LINE ONE\n\nLINE TWO\n\nLINE THREE\n\n\nLINE FOUR\n\nLINE FIVE");
        SlidePaneView pane(QStringLiteral("Preview"), false);
        pane.resize(520, 700);
        pane.splitter()->setSizes({450, 150});
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        pane.loadItem(s.toPresentationItem(nullptr), QStringLiteral("#001833"), 0);
        QListWidget* list = pane.stanzaList();
        QCOMPARE(list->count(), 5);

        // Row heights: separators are visible space (17 px per blank line); the first row has none.
        const int base = list->sizeHintForRow(0);
        QCOMPARE(list->sizeHintForRow(1) - base, 17);
        QCOMPARE(list->sizeHintForRow(3) - base, 34);       // two blank lines before LINE FOUR
        QCOMPARE(list->sizeHintForRow(4) - base, 17);

        QSignalSpy changed(&pane, &SlidePaneView::currentSlideChanged);
        QSignalSpy dbl(&pane, &SlidePaneView::slideDoubleClicked);
        QWidget* vp = list->viewport();
        auto contentPoint = [&](int row) { return list->visualRect(list->model()->index(row, 0)).bottomLeft() + QPoint(40, -6); };
        auto gapPoint = [&](int row) { return list->visualRect(list->model()->index(row, 0)).topLeft() + QPoint(40, 6); };

        // QTest::mouseDClick releases before the double-click event, which the view treats as a fresh press;
        // send press + double-click the way a real double click arrives.
        auto doubleClick = [&](QPoint pt) {
            QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, pt);
            QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(pt), vp->mapToGlobal(QPointF(pt)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &e);
            QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, pt);
        };

        // clicking a separator selects nothing and changes nothing
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, gapPoint(3));
        QCOMPARE(list->currentRow(), 0);
        doubleClick(gapPoint(3));
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(dbl.size(), 0);
        QCOMPARE(changed.size(), 0);

        // clicking real lyric text selects the stanza and double-click still goes live
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, contentPoint(3));
        QCOMPARE(list->currentRow(), 3);
        doubleClick(contentPoint(2));
        QCOMPARE(dbl.size(), 1);
        QCOMPARE(dbl.last().first().toInt(), 2);
        list->setCurrentRow(3);

        // the selection highlight covers the lyric row only, never the blank-line space above it
        const QImage img = vp->grab().toImage();
        const qreal dpr = img.devicePixelRatio();
        auto px = [&](QPoint p) { return img.pixel(int(p.x() * dpr), int(p.y() * dpr)); };
        const QRgb gapColour = px(gapPoint(3));
        const QRgb rowColour = px(contentPoint(3));
        QVERIFY2(gapColour != rowColour, "selection highlight must not extend into the separator");
        QCOMPARE(px(gapPoint(3) + QPoint(0, 12)), gapColour);      // whole separator is un-highlighted
    }

    void newAndEditedSongsGetTheSameSpacing()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        const Song created = addSong(env, QStringLiteral("Fresh"), QString(), QStringLiteral("a\n\nb\n\n\n\nc"));
        w.songsTab()->reload();
        w.songsTab()->userSelect(created.id);
        QListWidget* list = w.previewPane()->stanzaList();
        QCOMPARE(list->count(), 3);
        const int base = list->sizeHintForRow(0);
        QCOMPARE(list->sizeHintForRow(1) - base, 17);
        QCOMPARE(list->sizeHintForRow(2) - base, 51);              // three blank lines

        // edit: remove the long gap, add a stanza
        Song edited = created;
        edited.lyricsRaw = QStringLiteral("a\n\nb\n\nc\n\n\nd");
        w.applyEditedSong(&created, edited, false);
        list = w.previewPane()->stanzaList();
        QCOMPARE(list->count(), 4);
        QCOMPARE(list->sizeHintForRow(2) - list->sizeHintForRow(0), 17);
        QCOMPARE(list->sizeHintForRow(3) - list->sizeHintForRow(0), 34);

        // the same through the schedule and the live pane (Go Live keeps stanza navigation by index)
        w.goLive(3);
        QCOMPARE(w.livePane()->slides().size(), 4);
        QCOMPARE(w.livePane()->currentSlideIndex(), 3);
        QCOMPARE(w.livePane()->stanzaList()->sizeHintForRow(3) - w.livePane()->stanzaList()->sizeHintForRow(0), 34);
        w.liveFirst();
        QCOMPARE(w.projection()->state().activeSlide->content, QStringLiteral("A"));
    }

    void slidesWithoutSpacingInfoUseOneBlankLine()
    {
        // e.g. a schedule file: spacing hints are not stored, so stanzas are separated by a single blank line
        PresentationItem item;
        item.title = QStringLiteral("From file");
        for (const char* c : {"x", "y", "z"}) {
            Slide s;
            s.content = QString::fromLatin1(c);
            item.slides.append(s);
        }
        SlidePaneView pane(QStringLiteral("Preview"), false);
        pane.resize(400, 600);
        pane.show();
        QVERIFY(QTest::qWaitForWindowExposed(&pane));
        pane.loadItem(item, QStringLiteral("#001833"), 0);
        QListWidget* list = pane.stanzaList();
        QCOMPARE(list->sizeHintForRow(1) - list->sizeHintForRow(0), 17);
        QCOMPARE(list->sizeHintForRow(2), list->sizeHintForRow(1));
    }
    // ---- Song search: the matching stanza is selected in Preview ---------------------------------------------
    void lyricSearchSelectsTheMatchingStanzaInPreview()
    {
        Env env;
        const Song walau = addSong(env, QStringLiteral("Walau Gunung Tak Berpindah"), QStringLiteral("Anon"),
            QStringLiteral("JIWAKU BERTANYA APAKAH MASIH ADA HARAPAN?\n\nMELEWATI TANGISAN RASA SESAK DI DADA\n\n\nIMAN KU BERKATA TUHANKU YANG PEGANG KUASA\n\n"
                           "WALAUPUN GUNUNG ITU TAK BERPINDAH\nTETAP PERCAYA KAU YANG PUNYA KUASA\n\nSEKALIPUN KU DALAM LEMBAH KELAM\n\n"
                           "WALAUPUN GUNUNG ITU TAK BERPINDAH LAGI\n\nWALAUPUN MASALAHKU MASIH ADA"));
        const Song kasih = addSong(env, QStringLiteral("Kasih Setia"), QString(), QStringLiteral("Bapa yang baik\n\nAku percaya\n\nSelamanya"));
        const Song other = addSong(env, QStringLiteral("Gunung Tinggi"), QString(), QStringLiteral("Aku mendaki\n\nDi atas gunung itu\n\nAku bernyanyi"));
        MainWindow& w = env.mainWindow();
        SongsTab* tab = w.songsTab();
        tab->reload();
        SlidePaneView* pv = w.previewPane();

        // Live content that must never change while searching
        w.songsTab()->userSelect(kasih.id);
        w.goLive();
        auto* fo = dynamic_cast<LuxTest::FakeOutput*>(w.projection()->output());
        QVERIFY(fo);
        const QString liveTitle = w.livePane()->itemTitle();
        const int pushes = fo->updates.size();
        const QString liveLabel = w.projection()->state().activeSlide->label;
        auto liveUnchanged = [&] {
            QCOMPARE(w.livePane()->itemTitle(), liveTitle);
            QCOMPARE(fo->updates.size(), pushes);
            QCOMPARE(w.projection()->state().activeSlide->label, liveLabel);
        };

        auto search = [&](const QString& q) {
            tab->setLyricsQuery(q);
            QTRY_COMPARE_WITH_TIMEOUT(tab->lyricsQuery(), q, 2000);
            QTest::qWait(300);   // debounce
        };
        auto selected = [&] { return pv->currentSlideIndex(); };

        search(QStringLiteral("walaupun gunung itu tak berpindah"));          // exact phrase: first stanza containing it
        QCOMPARE(pv->itemTitle(), QStringLiteral("Walau Gunung Tak Berpindah"));
        QCOMPARE(selected(), 3);
        QVERIFY(pv->canvas()->slide().content.contains(QStringLiteral("WALAUPUN GUNUNG ITU TAK BERPINDAH")));
        liveUnchanged();

        search(QStringLiteral("gunung itu"));                                  // partial
        QCOMPARE(pv->itemTitle(), QStringLiteral("Walau Gunung Tak Berpindah"));
        QCOMPARE(selected(), 3);
        search(QStringLiteral("WaLauPun GuNung"));                             // case-insensitive, same stanza (first of several)
        QCOMPARE(selected(), 3);
        search(QStringLiteral("masalahku masih"));                             // a later stanza
        QCOMPARE(selected(), 6);
        search(QStringLiteral("sekalipun, ku dalam lembah-kelam!"));           // punctuation-insensitive
        QCOMPARE(selected(), 4);
        search(QStringLiteral("tuhanku kuasa"));                               // words spread over a stanza: best stanza
        QCOMPARE(selected(), 2);
        liveUnchanged();

        // title-only match: no lyric is invented, normal preview (first stanza)
        search(QStringLiteral("kasih setia"));
        QCOMPARE(pv->itemTitle(), QStringLiteral("Kasih Setia"));
        QCOMPARE(selected(), 0);
        // the Title box never selects lyrics
        tab->setLyricsQuery(QString());
        tab->setTitleQuery(QStringLiteral("walau gunung"));
        QTest::qWait(400);
        QCOMPARE(pv->itemTitle(), QStringLiteral("Walau Gunung Tak Berpindah"));
        QCOMPARE(selected(), 0);
        tab->setTitleQuery(QString());
        QTest::qWait(400);

        // another song while the search is active: its own matching stanza
        search(QStringLiteral("gunung itu"));
        QVERIFY(!tab->filteredSongs().isEmpty());
        tab->userSelect(other.id);
        QCOMPARE(pv->itemTitle(), QStringLiteral("Gunung Tinggi"));
        QCOMPARE(selected(), 1);
        tab->userSelect(walau.id);
        QCOMPARE(selected(), 3);

        // clearing the search does not leave the matched stanza highlighted
        tab->setLyricsQuery(QString());
        QTest::qWait(400);
        QCOMPARE(pv->itemTitle(), QStringLiteral("Walau Gunung Tak Berpindah"));
        QCOMPARE(selected(), 0);
        tab->userSelect(other.id);                                              // and a song selected afterwards starts at its top
        QCOMPARE(selected(), 0);
        liveUnchanged();
        QCOMPARE(w.statusText().left(8), QStringLiteral("Preview:"));
    }

    void matchingStanzaRulesAreDeterministic()
    {
        const QStringList s{QStringLiteral("Satu dua"), QStringLiteral("Tiga empat dua"), QStringLiteral("Lima enam"), QStringLiteral("dua empat")};
        QCOMPARE(SongSearch::matchingStanza(s, QStringLiteral("dua")), 0);                // first occurrence
        QCOMPARE(SongSearch::matchingStanza(s, QStringLiteral("EMPAT DUA")), 1);          // whole phrase wins over later words
        QCOMPARE(SongSearch::matchingStanza(s, QStringLiteral("dua empat")), 3);          // the phrase is only in the last stanza
        QCOMPARE(SongSearch::matchingStanza(s, QStringLiteral("tiga enam")), 1);          // no phrase: most words, first on ties
        QCOMPARE(SongSearch::matchingStanza(s, QStringLiteral("zzz")), -1);
        QCOMPARE(SongSearch::matchingStanza(s, QString()), -1);
        QCOMPARE(SongSearch::matchingStanza({}, QStringLiteral("dua")), -1);
        QCOMPARE(SongSearch::matchingStanza({QStringLiteral("Karena kasih")}, QStringLiteral("karna")), 0);   // reference synonyms
        QCOMPARE(SongSearch::matchingStanza({QStringLiteral("it''s fine")}, QStringLiteral("its")), 0);          // apostrophes ignored
    }

    // ---- Song list: Enter = Go Live ----------------------------------------------------------------------------
    void enterInTheSongListGoesLiveOnce()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), QStringLiteral("alpha one\n\nalpha two"));
        const Song b = addSong(env, QStringLiteral("Beta"), QString(), QStringLiteral("beta one\n\nbeta two"));
        MainWindow& w = env.mainWindow();
        SongsTab* tab = w.songsTab();
        tab->reload();
        QSignalSpy goLive(tab, &SongsTab::goLiveRequested);
        auto fo = [&] { return dynamic_cast<LuxTest::FakeOutput*>(w.projection()->output()); };

        // mouse-selected song + Enter
        tab->userSelect(a.id);
        QVERIFY(!w.isLiveOutputActive());
        QTest::keyClick(tab->list(), Qt::Key_Return);
        QCOMPARE(goLive.size(), 1);                                   // one request, not two (event is consumed)
        QCOMPARE(goLive.last().first().toString(), a.id);
        QVERIFY(w.isLiveOutputActive());
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Alpha"));
        QVERIFY(fo() && fo()->shown);
        QCOMPARE(fo()->updates.last().activeSlide->content, QStringLiteral("ALPHA ONE"));
        QCOMPARE(w.statusText(), QStringLiteral("Live: Alpha - "));

        // keyboard navigation then Enter at once: the song under the cursor goes live
        QTest::keyClick(tab->list(), Qt::Key_Down);
        QTest::keyClick(tab->list(), Qt::Key_Enter);                  // keypad Enter behaves the same
        QCOMPARE(goLive.size(), 2);
        QCOMPARE(goLive.last().first().toString(), b.id);
        QCOMPARE(w.livePane()->itemTitle(), QStringLiteral("Beta"));
        QCOMPARE(fo()->updates.last().activeSlide->content, QStringLiteral("BETA ONE"));
        QCOMPARE(w.previewPane()->itemTitle(), QStringLiteral("Beta"));

        // key repeat and modified Enter do nothing
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), /*autorep=*/true);
        QApplication::sendEvent(tab->list(), &repeat);
        QTest::keyClick(tab->list(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(goLive.size(), 2);

        // Enter in the search boxes searches (and moves to the list) but never goes live
        tab->titleSearch()->setFocus();
        tab->titleSearch()->setText(QStringLiteral("alp"));
        QTest::keyClick(tab->titleSearch(), Qt::Key_Return);
        tab->lyricsSearch()->setText(QStringLiteral("beta"));
        QTest::keyClick(tab->lyricsSearch(), Qt::Key_Return);
        QCOMPARE(goLive.size(), 2);

        // the main window's own shortcuts do not treat Enter as a command, and Enter without a selection does nothing
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QVERIFY(!w.handleProjectionKey(&enter));
        tab->setTitleQuery(QStringLiteral("zzzzzz"));
        tab->setLyricsQuery(QString());
        QTest::qWait(400);
        QCOMPARE(tab->list()->topLevelItemCount(), 0);
        QTest::keyClick(tab->list(), Qt::Key_Return);
        QCOMPARE(goLive.size(), 2);
        // existing list keys still work: Space = next Live stanza
        tab->setTitleQuery(QString());
        QTest::qWait(400);
        tab->userSelect(a.id);
        w.goLive();
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QVERIFY(w.handleProjectionKey(&space));
        QCOMPARE(w.livePane()->currentSlideIndex(), 1);
    }
    void songListContextMenuKeyboardAndDrag()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), QStringLiteral("a"));
        MainWindow& w = env.mainWindow();
        SongsTab* tab = w.songsTab();
        tab->reload();
        QVERIFY(tab->actionAddToSchedule()->isEnabled() && tab->actionEdit()->isEnabled() && tab->actionDelete()->isEnabled());
        QVERIFY(w.action(QStringLiteral("Songs"), QStringLiteral("Delete Selected Song"))->isEnabled());

        // Keyboard / context-menu requests are checked on a stand-alone tab (the window would
        // answer them with modal dialogs).
        {
            SongsTab lone(*env.data);
            QSignalSpy del(&lone, &SongsTab::deleteSongRequested);
            QSignalSpy edit(&lone, &SongsTab::editSongRequested);
            QSignalSpy add(&lone, &SongsTab::addToScheduleRequested);
            lone.list()->setFocus();
            QTest::keyClick(lone.list(), Qt::Key_Delete);
            QCOMPARE(del.count(), 1);
            QCOMPARE(del.first().first().toString(), a.id);
            lone.actionEdit()->trigger();
            QCOMPARE(edit.count(), 1);
            lone.actionAddToSchedule()->trigger();
            QCOMPARE(add.count(), 1);
        }
        tab->actionAddToSchedule()->trigger(); // wired through MainWindow (not modal)
        QCOMPARE(w.scheduleService().count(), 1);
        // Drag source: the list offers the song id as MIME data
        QMimeData* mime = tab->list()->model() ? tab->list()->model()->mimeData({tab->list()->model()->index(0, 0)}) : nullptr;
        QVERIFY(mime);
        QCOMPARE(QString::fromUtf8(mime->data(QString::fromLatin1(ScheduleListWidget::kSongMime))), a.id);
        delete mime;

        // deleting really removes the song and re-selects nothing
        QVERIFY(w.deleteSongById(a.id, /*askConfirmation=*/false));
        QCOMPARE(tab->list()->topLevelItemCount(), 0);
        QVERIFY(!tab->emptyCard()->isHidden());
        QVERIFY(!w.action(QStringLiteral("Songs"), QStringLiteral("Delete Selected Song"))->isEnabled());
        QVERIFY(!w.deleteSongById(QStringLiteral("missing"), false));
    }

    void scheduleCrudFromTheWindow()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), kGraceLyrics);
        const Song b = addSong(env, QStringLiteral("Beta"), QString(), QStringLiteral("[Verse 1]\nbeta text"));
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        ScheduleView* sv = w.schedule();
        ScheduleService& svc = w.scheduleService();

        // "Add to Schedule" for the selected song; the new item is selected and previewed
        w.addSongToSchedule(a.id);
        QCOMPARE(sv->itemCount(), 1);
        QCOMPARE(svc.selectedIndex(), 0);
        QCOMPARE(w.previewSource(), MainWindow::PreviewSource::Schedule);
        QVERIFY(sv->paneActive());
        QVERIFY(!w.songsTab()->paneActive());
        QCOMPARE(w.statusText(), QStringLiteral("Preview: Alpha"));
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Alpha"));

        // The "+" button adds what the preview currently shows
        w.songsTab()->userSelect(b.id);
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Beta"));
        sv->addButton()->click();
        QCOMPARE(sv->itemCount(), 2);
        QCOMPARE(w.statusText(), QStringLiteral("Added to Schedule: Beta")); // set after the new item was previewed
        QCOMPARE(svc.items()[1].title, QStringLiteral("Beta"));

        // Move / remove through the tool column
        svc.userSelect(1);
        QVERIFY(sv->removeButton()->isEnabled());
        sv->moveUpButton()->click();
        QCOMPARE(svc.items()[0].title, QStringLiteral("Beta"));
        QCOMPARE(sv->listWidget()->item(0)->text(), QStringLiteral("Beta"));
        QCOMPARE(sv->listWidget()->currentRow(), 0);
        sv->moveDownButton()->click();
        QCOMPARE(svc.items()[1].title, QStringLiteral("Beta"));
        sv->removeButton()->click();
        QCOMPARE(sv->itemCount(), 1);
        QCOMPARE(svc.items()[0].title, QStringLiteral("Alpha"));
        QCOMPARE(svc.selectedIndex(), 0);

        // Clicking a row previews it again even if it is already selected
        QSignalSpy prev(&svc, &ScheduleService::itemSelectedForPreview);
        QTest::mouseClick(sv->listWidget()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          sv->listWidget()->visualItemRect(sv->listWidget()->item(0)).center());
        QCOMPARE(prev.count(), 1);

        // Delete key removes the selected item
        sv->listWidget()->setFocus();
        QTest::keyClick(sv->listWidget(), Qt::Key_Delete);
        QCOMPARE(sv->itemCount(), 0);
        QVERIFY(!sv->removeButton()->isEnabled());
    }

    void scheduleDragAndDropInsertsAndReorders()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), QStringLiteral("[Verse 1]\na"));
        const Song b = addSong(env, QStringLiteral("Beta"), QString(), QStringLiteral("[Verse 1]\nb"));
        const Song c = addSong(env, QStringLiteral("Gamma"), QString(), QStringLiteral("[Verse 1]\nc"));
        MainWindow& w = env.mainWindow();
        w.resize(900, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.songsTab()->reload();
        ScheduleListWidget* list = w.schedule()->listWidget();
        ScheduleService& svc = w.scheduleService();

        // Qt only delivers a Drop to the widget that accepted the preceding DragEnter/DragMove.
        auto sendDrop = [&](QMimeData& mime, const QPoint& pos, Qt::DropAction action) {
            QDragEnterEvent enter(pos, action, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(list->viewport(), &enter);
            QDragMoveEvent move(pos, action, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(list->viewport(), &move);
            QDropEvent drop(QPointF(pos), action, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(list->viewport(), &drop);
        };
        auto dropSong = [&](const QString& id, const QPoint& pos) {
            QMimeData mime;
            mime.setData(QString::fromLatin1(ScheduleListWidget::kSongMime), id.toUtf8());
            sendDrop(mime, pos, Qt::CopyAction);
        };
        QSignalSpy dropped(list, &ScheduleListWidget::songDropped);
        dropSong(a.id, QPoint(5, 5));                      // empty list => index 0
        QVERIFY2(dropped.count() == 1, "drop event did not reach the list");
        QCOMPARE(svc.count(), 1);
        dropSong(b.id, QPoint(5, list->viewport()->height() - 2)); // below the last row => append
        QCOMPARE(svc.items()[1].title, QStringLiteral("Beta"));
        const QRect firstRow = list->visualItemRect(list->item(0));
        dropSong(c.id, QPoint(5, firstRow.top() + 2));      // upper half of row 0 => insert before it
        QCOMPARE(svc.items()[0].title, QStringLiteral("Gamma"));
        QCOMPARE(svc.selectedIndex(), 0);                  // inserted item is selected
        QCOMPARE(list->dropIndexAt(QPoint(5, list->visualItemRect(list->item(1)).bottom() - 1)), 2);

        // Reorder: drag row 0 below the last row
        QMimeData rowMime;
        rowMime.setData(QString::fromLatin1(ScheduleListWidget::kRowMime), QByteArray::number(0));
        sendDrop(rowMime, QPoint(5, list->viewport()->height() - 2), Qt::MoveAction);
        QCOMPARE(svc.items()[2].title, QStringLiteral("Gamma"));
        QCOMPARE(svc.selectedIndex(), 2);                  // dragged item ends up selected
        QCOMPARE(list->count(), 3);

        // Unknown MIME types are ignored
        QMimeData junk;
        junk.setText(QStringLiteral("hello"));
        sendDrop(junk, QPoint(5, 5), Qt::CopyAction);
        QCOMPARE(svc.count(), 3);
    }

    void schedulePersistsThroughTheWindow()
    {
        Env env;
        const Song a = addSong(env, QStringLiteral("Alpha"), QString(), kGraceLyrics);
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        w.addSongToSchedule(a.id);
        w.addPreviewToSchedule();
        QCOMPARE(w.scheduleService().count(), 2);
        QVERIFY(w.scheduleService().hasUnsavedChanges());

        const QString file = env.path(QStringLiteral("sunday.fws"));
        QString err;
        QVERIFY2(w.saveScheduleFile(file, &err), qPrintable(err));
        QVERIFY(!w.scheduleService().hasUnsavedChanges());
        QVERIFY(w.promptSaveIfModified()); // nothing to ask when saved

        QVERIFY(w.newScheduleNoPrompt());
        QCOMPARE(w.schedule()->itemCount(), 0);
        QVERIFY(w.scheduleService().currentFilePath().isNull());

        QVERIFY2(w.openScheduleFile(file, &err), qPrintable(err));
        QCOMPARE(w.schedule()->itemCount(), 2);
        QCOMPARE(w.scheduleService().selectedIndex(), 0);
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Alpha")); // first item previewed after opening
        QCOMPARE(w.previewPane()->slides().size(), 2);
        QVERIFY(!w.scheduleService().hasUnsavedChanges());

        QVERIFY(!w.openScheduleFile(env.path(QStringLiteral("nope.fws")), &err));
        QCOMPARE(w.schedule()->itemCount(), 2); // failed open leaves the schedule alone
    }

    void songEditorNewSong()
    {
        Env env;
        SongEditorWindow dlg(nullptr);
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Editor"));
        QCOMPARE(dlg.titleEdit()->text(), QStringLiteral("New Song"));
        QCOMPARE(dlg.lyricsEdit()->toPlainText(), QStringLiteral("[Verse 1]\nEnter lyrics here..."));
        QCOMPARE(dlg.fontCombo()->currentData().toString(), QStringLiteral("Tahoma"));
        QCOMPARE(dlg.sizeEdit()->text(), QStringLiteral("13"));
        QVERIFY(dlg.boldButton()->isChecked() && dlg.shadowButton()->isChecked() && dlg.outlineButton()->isChecked());
        QVERIFY(!dlg.italicButton()->isChecked() && !dlg.underlineButton()->isChecked());
        QCOMPARE(dlg.currentBackground(), QStringLiteral("#001833"));
        QVERIFY(dlg.linkedCheck()->isChecked());
        QCOMPARE(dlg.backgroundCombo()->count(), 5);
        QCOMPARE(dlg.fontCombo()->count(), 6);
        QCOMPARE(dlg.size(), QSize(820, 580));
        QVERIFY(!dlg.iconButtons().first()->isEnabled()); // Song Information: no behaviour in the reference

        dlg.titleEdit()->setText(QStringLiteral("  My New Song  "));
        dlg.lyricsEdit()->setPlainText(QStringLiteral("[Verse 1]\nHello\nWorld"));
        dlg.fontCombo()->setCurrentIndex(dlg.fontCombo()->findData(QStringLiteral("Verdana")));
        dlg.sizeEdit()->setText(QStringLiteral("20"));
        dlg.italicButton()->click();
        dlg.underlineButton()->click();
        dlg.shadowButton()->click();
        dlg.backgroundCombo()->setCurrentIndex(2);
        dlg.okButton()->click();
        QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));

        const Song r = dlg.resultSong();
        QCOMPARE(r.title, QStringLiteral("My New Song")); // trimmed
        QCOMPARE(r.lyricsRaw, QStringLiteral("[Verse 1]\nHello\nWorld"));
        QCOMPARE(r.fontFamily, QStringLiteral("Verdana"));
        QCOMPARE(r.fontSize, 20);
        QVERIFY(r.isBold && r.isItalic && r.isUnderline && !r.hasShadow && r.hasOutline);
        QCOMPARE(r.defaultBackground, QStringLiteral("#1A1B41"));
        QVERIFY(!r.id.isEmpty());

        // invalid size falls back to 13; Reset Attributes restores the defaults
        dlg.sizeEdit()->setText(QStringLiteral("abc"));
        QCOMPARE(dlg.resultSong().fontSize, 13);
        auto resetButtons = dlg.findChildren<QPushButton*>();
        for (QPushButton* b : resetButtons)
            if (b->text() == QStringLiteral("Reset Attributes"))
                b->click();
        QCOMPARE(dlg.fontCombo()->currentData().toString(), QStringLiteral("Tahoma"));
        QCOMPARE(dlg.sizeEdit()->text(), QStringLiteral("13"));
        QVERIFY(dlg.shadowButton()->isChecked() && !dlg.italicButton()->isChecked());

        SongEditorWindow cancelled(nullptr);
        cancelled.cancelButton()->click();
        QCOMPARE(cancelled.result(), static_cast<int>(QDialog::Rejected));
    }

    void songEditorEditsExistingSongKeepingIdentity()
    {
        Env env;
        Song orig;
        orig.title = QStringLiteral("Old Title");
        orig.author = QStringLiteral("Kept Author");
        orig.copyright = QStringLiteral("Kept (c)");
        orig.key = QStringLiteral("D");
        orig.isFavorite = true;
        orig.fontColor = QStringLiteral("#FFEE00");
        orig.lyricsRaw = kGraceLyrics;
        orig.defaultBackground = QStringLiteral("C:/some/custom.png"); // not one of the presets
        orig.fontFamily = QStringLiteral("Times New Roman");
        orig.fontSize = 18;
        orig.isBold = false;
        orig.hasOutline = false;

        SongEditorWindow dlg(&orig);
        QCOMPARE(dlg.titleEdit()->text(), QStringLiteral("Old Title"));
        QCOMPARE(dlg.fontCombo()->currentData().toString(), QStringLiteral("Times New Roman"));
        QCOMPARE(dlg.sizeEdit()->text(), QStringLiteral("18"));
        QVERIFY(!dlg.boldButton()->isChecked() && !dlg.outlineButton()->isChecked());
        // The combo shows a preset, but the song's own background is kept until the user picks one
        QCOMPARE(dlg.currentBackground(), QStringLiteral("C:/some/custom.png"));
        dlg.titleEdit()->setText(QStringLiteral("New Title"));
        const Song r = dlg.resultSong();
        QCOMPARE(r.id, orig.id);
        QCOMPARE(r.title, QStringLiteral("New Title"));
        QCOMPARE(r.author, QStringLiteral("Kept Author"));
        QCOMPARE(r.copyright, QStringLiteral("Kept (c)"));
        QCOMPARE(r.key, QStringLiteral("D"));
        QVERIFY(r.isFavorite);
        QCOMPARE(r.fontColor, QStringLiteral("#FFEE00"));
        QCOMPARE(r.defaultBackground, QStringLiteral("C:/some/custom.png"));
        QCOMPARE(orig.title, QStringLiteral("Old Title")); // the original object is never mutated

        dlg.backgroundCombo()->setCurrentIndex(3);
        QCOMPARE(dlg.resultSong().defaultBackground, QStringLiteral("#1E293B"));

        // A preset background is shown selected
        Song preset = orig;
        preset.defaultBackground = QStringLiteral("#064E3B");
        SongEditorWindow dlg2(&preset);
        QCOMPARE(dlg2.backgroundCombo()->currentIndex(), 4);
    }

    void songEditorPreviewFollowsTheCaret()
    {
        const QString lyrics = QStringLiteral("[Verse 1]\nLine A\nLine B\n\n[Chorus]\nSing out\n\nBridge:\nBridge text");
        const int atVerse = 12;                                  // inside "Line A"
        const int atChorus = lyrics.indexOf(QStringLiteral("Sing")) + 2;
        const int atBridge = lyrics.indexOf(QStringLiteral("Bridge text")) + 1;
        QCOMPARE(SongEditorWindow::previewTextFor(lyrics, atVerse, QStringLiteral("T")), QStringLiteral("Line A\nLine B"));
        QCOMPARE(SongEditorWindow::previewTextFor(lyrics, atChorus, QStringLiteral("T")), QStringLiteral("Sing out"));
        QCOMPARE(SongEditorWindow::previewTextFor(lyrics, atBridge, QStringLiteral("T")), QStringLiteral("Bridge text"));
        // caret on the blank line between stanzas: the line itself is empty => falls back to the title
        const int blank = lyrics.indexOf(QStringLiteral("\n\n")) + 1;
        QVERIFY(!SongEditorWindow::previewTextFor(lyrics, blank, QStringLiteral("Title")).isNull());
        QCOMPARE(SongEditorWindow::previewTextFor(QStringLiteral("   "), 0, QStringLiteral("Title")), QStringLiteral("Title"));
        QCOMPARE(SongEditorWindow::previewTextFor(QStringLiteral("a\r\nb\r\n\r\nc"), 5, QStringLiteral("T")), QStringLiteral("c")); // CRLF input

        Env env;
        SongEditorWindow dlg(nullptr);
        dlg.titleEdit()->setText(QStringLiteral("Hymn"));
        dlg.lyricsEdit()->setPlainText(lyrics);
        QTextCursor cur = dlg.lyricsEdit()->textCursor();
        cur.setPosition(atChorus);
        dlg.lyricsEdit()->setTextCursor(cur);
        QCOMPARE(dlg.preview()->state().text, QStringLiteral("Sing out"));
        QCOMPARE(dlg.preview()->state().footer, QStringLiteral("Hymn (Preview)"));
        dlg.sizeEdit()->setText(QStringLiteral("10"));
        QCOMPARE(dlg.preview()->state().fontPx, 40);              // size * 4
        dlg.outlineButton()->click();
        QVERIFY(!dlg.preview()->state().outline);
        dlg.lyricsEdit()->clear();
        QCOMPARE(dlg.preview()->state().text, QStringLiteral("Hymn")); // empty lyrics show the title

        // the preview paints the text (white pixels over the dark background)
        dlg.lyricsEdit()->setPlainText(QStringLiteral("[Verse 1]\nHELLO WORLD"));
        dlg.preview()->resize(640, 360);
        const QImage img = dlg.preview()->grab().toImage();
        bool white = false;
        for (int y = 20; y < 340 && !white; y += 2)
            for (int x = 40; x < 600; x += 2)
                if (qRed(img.pixel(x, y)) > 240 && qGreen(img.pixel(x, y)) > 240 && qBlue(img.pixel(x, y)) > 240) { white = true; break; }
        QVERIFY(white);
    }

    void editedSongUpdatesLibraryPreviewAndSchedule()
    {
        Env env;
        Song orig = addSong(env, QStringLiteral("Amazing Grace"), QStringLiteral("John Newton"), kGraceLyrics);
        const Song other = addSong(env, QStringLiteral("Other"), QString(), QStringLiteral("[Verse 1]\nother"));
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        w.addSongToSchedule(orig.id);
        w.addSongToSchedule(other.id);
        w.songsTab()->userSelect(orig.id);
        w.previewPane()->setCurrentSlideIndex(1);

        Song edited = orig;
        edited.title = QStringLiteral("Amazing Grace (edited)");
        edited.lyricsRaw = QStringLiteral("[Verse 1]\nNew line one\n\n[Chorus]\nNew chorus\n\n[Bridge]\nNew bridge");
        w.applyEditedSong(&orig, edited, /*linked=*/true);

        Song stored;
        QVERIFY(env.data->songById(orig.id, &stored));
        QCOMPARE(stored.title, QStringLiteral("Amazing Grace (edited)"));
        QCOMPARE(env.data->songs().size(), 2); // edited in place, not duplicated
        QCOMPARE(w.songsTab()->selectedSongId(), orig.id);
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Amazing Grace (edited)"));
        QCOMPARE(w.previewPane()->slides().size(), 3);
        QCOMPARE(w.previewPane()->currentSlideIndex(), 1);   // the slide index is preserved
        QCOMPARE(w.scheduleService().items()[0].title, QStringLiteral("Amazing Grace (edited)"));
        QCOMPARE(w.scheduleService().items()[0].presentation.slides.size(), 3);
        QCOMPARE(w.scheduleService().items()[1].title, QStringLiteral("Other"));

        // "linked" unchecked: the schedule keeps its copy
        Song again = edited;
        again.title = QStringLiteral("Third Title");
        w.applyEditedSong(&edited, again, /*linked=*/false);
        QCOMPARE(w.scheduleService().items()[0].title, QStringLiteral("Amazing Grace (edited)"));
        QCOMPARE(w.songsTab()->list()->topLevelItemCount(), 2);

        // A brand-new song is stored, listed, selected and previewed
        Song fresh;
        fresh.title = QStringLiteral("Brand New");
        fresh.lyricsRaw = QStringLiteral("[Verse 1]\nfresh");
        w.applyEditedSong(nullptr, fresh, true);
        QCOMPARE(env.data->songs().size(), 3);
        QCOMPARE(w.songsTab()->selectedSongId(), fresh.id);
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Brand New"));
        QCOMPARE(w.statusText(), QStringLiteral("Preview: Brand New"));

        // and everything is on disk
        DataService reloaded;
        QCOMPARE(reloaded.songs().size(), 3);
    }

    void songMenusAndToolbarAreWired()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        for (const char* t : {"New Song...", "Edit Selected Song", "Open Songs Database Folder...", "Change Database Folder...",
                              "Import Song Library Backup (.json)...", "Export Song Library Backup (.json)...", "Load Default Sample Songs"})
            QVERIFY2(w.action(QStringLiteral("Songs"), QString::fromLatin1(t))->isEnabled(), t);
        QVERIFY(!w.action(QStringLiteral("Songs"), QStringLiteral("Delete Selected Song"))->isEnabled()); // nothing selected
        for (const char* t : {"New Schedule", "Open Schedule...", "Save Schedule", "Save Schedule As...", "Add Item...", "Clear Schedule"})
            QVERIFY2(w.action(QStringLiteral("Schedule"), QString::fromLatin1(t))->isEnabled(), t);
        QVERIFY(w.action(QStringLiteral("Profiles"), QStringLiteral("Export Songs Profile..."))->isEnabled());
        QVERIFY(w.action(QStringLiteral("Profiles"), QStringLiteral("Import Songs Profile..."))->isEnabled());
        // Live / Scriptures / Options remain later milestones
        QVERIFY(w.action(QStringLiteral("Live"), QStringLiteral("Go Live\tF5"))->isEnabled()); // Milestone 4
        QVERIFY(w.action(QStringLiteral("Scriptures"), QStringLiteral("Import Bible Translation (.json)..."))->isEnabled());
        QVERIFY(w.newSongButton()->isEnabled() && w.editSongButton()->isEnabled());

        // Sample songs only appear when the user triggers the action's backend
        QCOMPARE(w.songsTab()->list()->topLevelItemCount(), 0);
        QCOMPARE(env.data->loadSampleSongs(), 5);
        w.songsTab()->reload();
        QCOMPARE(w.songsTab()->list()->topLevelItemCount(), 5);
        QVERIFY(w.songsTab()->emptyCard()->isHidden());
        QVERIFY(w.action(QStringLiteral("Songs"), QStringLiteral("Delete Selected Song"))->isEnabled()); // first song auto-selected
    }
    // ===================== Layout refinement =====================

    void defaultSplitterProportions()
    {
        Env env;
        MainWindow& w = env.mainWindow();
        w.resize(1100, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(50);

        // Left column: Schedule 45% / Library 55% (the Library no longer dominates the column)
        auto share = [&] { const auto r = w.leftSplitter()->sizes(); return double(r[0]) / (r[0] + r[1]); };
        QVERIFY2(qAbs(share() - 0.45) <= 0.02, qPrintable(QString::number(share())));
        QVERIFY(w.leftSplitter()->sizes()[0] >= 140 && w.leftSplitter()->sizes()[1] >= 180); // minimum sizes hold

        // Preview and Live: stanza list 52% / slide 48%, identical in both panes
        for (SlidePaneView* pane : {w.previewPane(), w.livePane()}) {
            const auto s = pane->splitter()->sizes();
            const double listShare = double(s[0]) / (s[0] + s[1]);
            QVERIFY2(qAbs(listShare - 0.52) <= 0.03, qPrintable(QString::number(listShare)));
            // the 16:9 slide gets room for a reasonably large canvas
            QVERIFY(pane->canvas()->height() >= 150);
        }

        // The proportions follow the window size until the user drags a handle ...
        w.resize(1500, 1000);
        QTest::qWait(50);
        QVERIFY2(qAbs(share() - 0.45) <= 0.02, qPrintable(QString::number(share())));
        w.resize(900, 560);
        QTest::qWait(50);
        QVERIFY2(qAbs(share() - 0.45) <= 0.03, qPrintable(QString::number(share())));
        QVERIFY(w.leftSplitter()->sizes()[0] >= 140 && w.leftSplitter()->sizes()[1] >= 180);

        // ... after which the manual layout is respected.
        w.resize(1100, 800);
        QTest::qWait(50);
        QSplitterHandle* handle = w.leftSplitter()->handle(1);
        const QPoint c = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(handle, c + QPoint(0, 60));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, c + QPoint(0, 60));
        QTest::qWait(30);
        const int manualSchedule = w.leftSplitter()->sizes()[0];
        QVERIFY(manualSchedule > 0.45 * (w.leftSplitter()->sizes()[0] + w.leftSplitter()->sizes()[1]) + 30);
        w.resize(1300, 900);
        QTest::qWait(50);
        QCOMPARE(w.leftSplitter()->sizes()[0], manualSchedule); // not reset to 45%
        // the horizontal layout is unchanged: 340 px left column, equal preview/live
        const auto cols = w.mainSplitter()->sizes();
        QVERIFY(qAbs(cols[0] - 340) <= 2 && qAbs(cols[1] - cols[2]) <= 2);
    }

    // ===================== Legacy character repair (U+0080..U+009F) =====================

    void legacyControlCharactersRenderLikeRealPunctuation()
    {
        // Cp1252 punctuation that was stored as C1 control characters (apostrophe, en dash, quotes, ellipsis)
        const QString stored = QStringLiteral("DON\u0092T STOP \u0096 \u0091GO\u0092 \u0093HI\u0094 WAIT\u0085");
        const QString real = QStringLiteral("DON\u2019T STOP \u2013 \u2018GO\u2019 \u201CHI\u201D WAIT\u2026");
        QCOMPARE(displayText(stored), real);

        // Slide canvas: identical pixels for the stored and the repaired text (no boxes)
        auto render = [](const QString& content) {
            Slide s;
            s.label = QStringLiteral("Verse \u0096 1");
            s.content = content;
            AppSettings st;
            st.capitalizeAllWords = false;
            SlideCanvas c;
            c.setSettings(st);
            c.resize(640, 360);
            c.setSlide(s);
            return c.grab().toImage();
        };
        const QImage a = render(stored), b = render(real);
        QCOMPARE(a.size(), b.size());
        QVERIFY(a == b);
        // and the text really is drawn (not an empty slide)
        QVERIFY(a != render(QString()));

        // Stanza list (delegate) and preview header
        Env env;
        MainWindow& w = env.mainWindow();
        Slide sl;
        sl.label = QStringLiteral("Verse\u0096A");
        sl.content = stored;
        SlidePaneView* pv = w.previewPane();
        pv->resize(420, 600);
        pv->show();
        pv->setSlides({sl});
        const QImage listStored = pv->stanzaList()->grab().toImage();
        sl.label = QStringLiteral("Verse\u2013A");
        sl.content = real;
        pv->setSlides({sl});
        QVERIFY(pv->stanzaList()->grab().toImage() == listStored);

        pv->setItemTitle(QStringLiteral("Apapun Terjadi \u0096 Welyar"));
        QCOMPARE(pv->headerText(), QStringLiteral("Preview - Apapun Terjadi \u2013 Welyar"));
    }

    void songListScheduleAndStatusShowRepairedText()
    {
        Env env;
        Song s;
        s.title = QStringLiteral("Bapa\u0092s Love \u0096 Live");
        s.author = QStringLiteral("O\u0092Neil");
        s.lyricsRaw = QStringLiteral("[Verse 1]\nKau sgalanya bagi\u0092ku");
        QVERIFY(env.data->saveSong(s));
        MainWindow& w = env.mainWindow();
        w.songsTab()->reload();
        QCOMPARE(w.songsTab()->list()->topLevelItem(0)->text(0), QStringLiteral("Bapa\u2019s Love \u2013 Live"));
        QCOMPARE(w.songsTab()->list()->topLevelItem(0)->text(1), QStringLiteral("O\u2019Neil"));
        w.songsTab()->userSelect(s.id);
        QCOMPARE(w.statusText(), QStringLiteral("Preview: Bapa\u2019s Love \u2013 Live"));
        QCOMPARE(w.previewPane()->headerText(), QStringLiteral("Preview - Bapa\u2019s Love \u2013 Live"));
        w.addSongToSchedule(s.id);
        QCOMPARE(w.schedule()->listWidget()->item(0)->text(), QStringLiteral("Bapa\u2019s Love \u2013 Live"));

        // The stored text is untouched everywhere it is kept
        Song stored;
        QVERIFY(env.data->songById(s.id, &stored));
        QCOMPARE(stored.title, s.title);
        QCOMPARE(stored.lyricsRaw, s.lyricsRaw);
        QCOMPARE(w.scheduleService().items()[0].title, s.title);
        QVERIFY(w.scheduleService().items()[0].presentation.slides[0].content.contains(QChar(0x92)));
    }

    void editorShowsRepairedTextButKeepsStoredTextUnlessEdited()
    {
        Env env;
        Song s;
        s.title = QStringLiteral("Bapa\u0092s Song");
        s.lyricsRaw = QStringLiteral("[Verse 1]\r\nKau sgalanya bagi\u0092ku\r\n\r\n[Chorus]\r\nHalle\u0096lujah");
        SongEditorWindow dlg(&s);
        QCOMPARE(dlg.titleEdit()->text(), QStringLiteral("Bapa\u2019s Song"));
        QVERIFY(dlg.lyricsEdit()->toPlainText().contains(QChar(0x2019)));
        QVERIFY(!dlg.lyricsEdit()->toPlainText().contains(QChar(0x92)));
        QVERIFY(!dlg.preview()->state().text.contains(QChar(0x92)));

        // Untouched: the stored text is returned byte for byte (CRLF and C1 characters included)
        const Song same = dlg.resultSong();
        QCOMPARE(same.title, s.title);
        QCOMPARE(same.lyricsRaw, s.lyricsRaw);

        // Edited: the new text is saved as shown (real punctuation)
        dlg.lyricsEdit()->setPlainText(dlg.lyricsEdit()->toPlainText() + QStringLiteral("\nnew line"));
        const Song edited = dlg.resultSong();
        QVERIFY(edited.lyricsRaw.contains(QChar(0x2019)));
        QVERIFY(!edited.lyricsRaw.contains(QChar(0x92)));
        QVERIFY(edited.lyricsRaw.endsWith(QStringLiteral("new line")));
        QCOMPARE(edited.title, s.title); // title was not touched
        dlg.titleEdit()->setText(QStringLiteral("Changed"));
        QCOMPARE(dlg.resultSong().title, QStringLiteral("Changed"));
    }

    void fontsHaveGlyphsForRepairedCharactersButNotForControls()
    {
        // Root cause evidence: U+0080..U+009F are control characters (no glyphs in any font, so Qt draws
        // the "missing glyph" box), while the Windows-1252 punctuation they stand for is in every UI font.
        for (const char* family : {"Tahoma", "Arial", "Segoe UI"}) {
            QFont f{QString::fromLatin1(family)};
            f.setPixelSize(20);
            if (QFontInfo(f).family().compare(QString::fromLatin1(family), Qt::CaseInsensitive) != 0)
                continue; // font not installed on this machine
            const QFontMetrics fm(f);
            for (char32_t c : {0x91u, 0x92u, 0x96u, 0x85u})
                QVERIFY2(!fm.inFontUcs4(c), qPrintable(QString::fromLatin1(family) + QStringLiteral(" U+") + QString::number(static_cast<uint>(c), 16)));
            for (char32_t c : {0x2018u, 0x2019u, 0x2013u, 0x2026u, 0x201Cu, 0x201Du, 0xB4u})
                QVERIFY2(fm.inFontUcs4(c), qPrintable(QString::fromLatin1(family) + QStringLiteral(" U+") + QString::number(static_cast<uint>(c), 16)));
        }
    }

    void renderScreenshots()
    {
        // Saves light/dark screenshots next to the test data for manual inspection.
        Env env;
        MainWindow& w = env.mainWindow();
        w.resize(1280, 720);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(80);
        const QString out = QStringLiteral(LUX_TEST_TMP);
        QVERIFY(w.grab().save(out + QStringLiteral("/ui_light.png")));
        w.library()->selectTab(1);
        QTest::qWait(30);
        QVERIFY(w.grab().save(out + QStringLiteral("/ui_light_scriptures.png")));
        w.library()->selectTab(LibraryView::kOnlineTab);
        QTest::qWait(30);
        QVERIFY(w.grab().save(out + QStringLiteral("/ui_light_online.png")));
        w.library()->selectTab(0);
        w.themeAction()->trigger();
        QTest::qWait(80);
        QVERIFY(w.grab().save(out + QStringLiteral("/ui_dark.png")));
        w.themeAction()->trigger();
    }

    void softwareUpdateSemVer()
    {
        // Tests semantic version comparison rules
        QVERIFY(UpdateService::isVersionNewer(QStringLiteral("v1.1.2"), QStringLiteral("1.0.0")));
        QVERIFY(UpdateService::isVersionNewer(QStringLiteral("v1.0.1"), QStringLiteral("1.0.0")));
        QVERIFY(UpdateService::isVersionNewer(QStringLiteral("2.0.0"), QStringLiteral("1.9.9")));
        QVERIFY(!UpdateService::isVersionNewer(QStringLiteral("v1.0.0"), QStringLiteral("1.0.0")));
        QVERIFY(!UpdateService::isVersionNewer(QStringLiteral("v0.9.9"), QStringLiteral("1.0.0")));
        QVERIFY(!UpdateService::isVersionNewer(QStringLiteral("1.0.0-beta"), QStringLiteral("1.0.0")));
    }

    void softwareUpdateDialog()
    {
        UpdateInfo info;
        info.currentVersion = QStringLiteral("1.0.0");
        info.newVersion = QStringLiteral("v1.1.2");
        info.releaseTitle = QStringLiteral("LuxPresenter v1.1.2");
        info.releaseNotes = QStringLiteral("### LuxPresenter v1.1.2\n- Bug fixes and improvements");
        info.releaseUrl = QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6/releases/tag/v1.1.2");
        info.patchDownloadUrl = QStringLiteral("https://github.com/Luxbane/LuxPresenter-Qt6/releases/download/v1.1.2/LuxPresenter-Patch.zip");
        info.isUpdateAvailable = true;

        UpdateDialog dlg(info);
        QCOMPARE(dlg.windowTitle(), QStringLiteral("Software Update"));
        QCOMPARE(dlg.releaseUrl(), info.releaseUrl);
        QVERIFY(!dlg.windowIcon().isNull());
    }

    void stageDisplayAndServerBasics()
    {
        StageServer server(8990);
        QVERIFY(server.isRunning());
        QVERIFY(server.serverPort() >= 8990);

        const auto ips = StageServer::getLocalIpAddresses();
        QVERIFY(!ips.isEmpty());
        QVERIFY(!StageServer::primaryLocalIpAddress().isEmpty());
        QVERIFY(server.stageUrl().contains(QStringLiteral("/stage")));

        StageState state;
        state.title = QStringLiteral("Amazing Grace");
        state.currentSlide = QStringLiteral("Amazing grace how sweet the sound");
        state.nextSlide = QStringLiteral("That saved a wretch like me");
        state.time = QStringLiteral("10:30:00");
        state.alertMessage = QStringLiteral("Car B 1234 CD");
        state.isBlack = false;
        state.isClear = false;

        server.updateState(state);
        const StageState fetched = server.currentState();
        QCOMPARE(fetched.title, QStringLiteral("Amazing Grace"));
        QCOMPARE(fetched.currentSlide, QStringLiteral("Amazing grace how sweet the sound"));
        QCOMPARE(fetched.nextSlide, QStringLiteral("That saved a wretch like me"));
        QCOMPARE(fetched.alertMessage, QStringLiteral("Car B 1234 CD"));

        StageWindow stageWin;
        QCOMPARE(stageWin.windowTitle(), QStringLiteral("LuxPresenter - Stage Display (Confidence Monitor)"));
        stageWin.updateState(state);
        QVERIFY(!stageWin.windowIcon().isNull());
    }
};

QTEST_MAIN(UiTests)
#include "test_ui.moc"
