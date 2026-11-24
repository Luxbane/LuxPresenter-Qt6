#include "Views/MainWindow.h"

#include "Services/AppPaths.h"
#include "Services/DataService.h"
#include "Services/ProjectionService.h"
#include "Services/SongImportExport.h"
#include "Services/SongSearch.h"
#include "Services/TextRepair.h"
#include "Services/StageServer.h"
#include "Services/ThemeService.h"
#include "Services/UpdateService.h"
#include "Views/AboutWindow.h"
#include "Views/StageWindow.h"
#include "Views/Components/ClassicButton.h"
#include "Views/Components/LibraryView.h"
#include "Views/Components/MediaTab.h"
#include "Views/Components/OnlineBrowserView.h"
#include "Views/Components/PresentationTab.h"
#include "Views/Components/ProportionalSplitter.h"
#include "Views/Components/ScheduleView.h"
#include "Views/Components/SlideCanvas.h"
#include "Views/Components/SlidePaneView.h"
#include "Views/Components/SongsTab.h"
#include "Views/SongEditorWindow.h"
#include "Views/MessageAlertWindow.h"
#include "Views/Projection/ProjectionOutput.h"
#include "Views/UiUtil.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QSignalBlocker>
#include <memory>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include "Services/AdBlockService.h"
#include "Views/Components/WebView2Host.h"
#include "Services/BackgroundUtil.h"
#include "Views/BackgroundDialog.h"
#include "Views/Components/ScripturesTab.h"
#include "Views/OptionsDialog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace Lux {

namespace {
const char* const kKofiUrl = "https://ko-fi.com/luxbane";
const char* const kScheduleFilter = "LuxPresenter Schedule (*.lps *.fws);;JSON Schedule (*.json);;All Files (*.*)";
const char* const kSongsFilter = "LuxPresenter Songs Backup (*.json);;JSON Files (*.json);;All Files (*.*)";
} // namespace

MainWindow::MainWindow(DataService& data, QWidget* parent) : QMainWindow(parent), m_data(data)
{
    setWindowTitle(QStringLiteral("LuxPresenter - Default Profile"));
    setWindowIcon(QIcon(QStringLiteral(":/app_icon.png")));
    m_scheduleService = new ScheduleService(this);
    m_projection = new ProjectionService(this);
    m_presentations = new PresentationService(this);
    m_adBlock = new AdBlockService(QDir(AppPaths::dataDirectory()).filePath(QStringLiteral("AdBlock")),
                                   QDir(AppPaths::baseDirectory()).filePath(QStringLiteral("Data/AdBlock")), this);
    m_adBlock->setEnabled(m_data.settings().adBlockEnabled);
    m_adBlock->setWhitelist(m_data.settings().adBlockWhitelist);
    m_adBlock->reload();
    m_stageServer = new StageServer(8080, this);
    if (!m_data.settings().stageServerEnabled)
        m_stageServer->stop();
    m_stageWindow = new StageWindow(this);
    m_stageTimer.setInterval(250);
    connect(&m_stageTimer, &QTimer::timeout, this, &MainWindow::updateStageDisplay);
    m_stageTimer.start();
    resize(1024, 620);
    setMinimumSize(750, 460);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("MainRoot"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    buildMenus();
    buildToolbar();
    root->addWidget(m_toolbar);
    buildWorkArea();
    root->addWidget(m_workStack, 1);
    buildStatusBar();
    wireSongsAndSchedule();
    setupProjection();
    setupPresentations();
    setupMedia();

    updateThemeUi();
    updateSongActions();
    connect(&ThemeService::instance(), &ThemeService::themeChanged, this, [this](bool) { updateThemeUi(); });

    // Auto-check for updates silently on startup (non-interactive, shows dialog only if a newer version exists)
    QTimer::singleShot(2500, this, [this] {
        if (!m_suppressDialogs) {
            if (!m_updateService)
                m_updateService = new UpdateService(this);
            m_updateService->checkForUpdates(this, /*interactive=*/false, /*forceShowUpdate=*/false);
        }
    });
}

SongsTab* MainWindow::songsTab() const { return m_library->songsTab(); }
ScripturesTab* MainWindow::scripturesTab() const { return m_library->scripturesTab(); }

QAction* MainWindow::plannedAction(QMenu* menu, const QString& text, int milestone, const QKeySequence& shortcut)
{
    QAction* a = menu->addAction(text);
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    a->setEnabled(false);
    a->setStatusTip(plannedNote(milestone));
    return a;
}

ClassicButton* MainWindow::plannedButton(ClassicButton* b, const QString& tip, int milestone)
{
    b->setToolTip(tip + QStringLiteral(" - ") + plannedNote(milestone));
    b->setEnabled(false);
    return b;
}

QMenu* MainWindow::menu(const QString& title) const
{
    for (QAction* a : menuBar()->actions())
        if (QString(a->text()).remove(QLatin1Char('&')) == title)
            return a->menu();
    return nullptr;
}

QAction* MainWindow::action(const QString& menuTitle, const QString& text) const
{
    if (QMenu* m = menu(menuTitle))
        for (QAction* a : m->actions())
            if (a->text() == text)
                return a;
    return nullptr;
}

// ---------------------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------------------

void MainWindow::buildMenus()
{
    QMenuBar* bar = menuBar();
    bar->setNativeMenuBar(false);

    QMenu* file = bar->addMenu(QStringLiteral("&File"));
    auto* newSched = file->addAction(QStringLiteral("New Schedule"));
    newSched->setShortcut(QKeySequence::New);
    connect(newSched, &QAction::triggered, this, &MainWindow::newSchedule);
    auto* openSched = file->addAction(QStringLiteral("Open Schedule..."));
    openSched->setShortcut(QKeySequence::Open);
    connect(openSched, &QAction::triggered, this, &MainWindow::openSchedule);
    auto* saveSched = file->addAction(QStringLiteral("Save Schedule"));
    saveSched->setShortcut(QKeySequence::Save);
    connect(saveSched, &QAction::triggered, this, [this] { saveSchedule(false); });
    auto* saveSchedAs = file->addAction(QStringLiteral("Save Schedule As..."));
    saveSchedAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveSchedAs, &QAction::triggered, this, [this] { saveSchedule(true); });
    file->addSeparator();
    connect(file->addAction(QStringLiteral("Open Songs Database Folder...")), &QAction::triggered, this,
            &MainWindow::openSongsFolder);
    file->addSeparator();
    connect(file->addAction(QStringLiteral("Options...")), &QAction::triggered, this, &MainWindow::openOptions);
    file->addSeparator();
    connect(file->addAction(QStringLiteral("Exit")), &QAction::triggered, this, &QWidget::close);

    QMenu* schedule = bar->addMenu(QStringLiteral("&Schedule"));
    connect(schedule->addAction(QStringLiteral("New Schedule")), &QAction::triggered, this, &MainWindow::newSchedule);
    connect(schedule->addAction(QStringLiteral("Open Schedule...")), &QAction::triggered, this, &MainWindow::openSchedule);
    connect(schedule->addAction(QStringLiteral("Save Schedule")), &QAction::triggered, this, [this] { saveSchedule(false); });
    connect(schedule->addAction(QStringLiteral("Save Schedule As...")), &QAction::triggered, this, [this] { saveSchedule(true); });
    schedule->addSeparator();
    connect(schedule->addAction(QStringLiteral("Add Item...")), &QAction::triggered, this, &MainWindow::addPreviewToSchedule);
    connect(schedule->addAction(QStringLiteral("Clear Schedule")), &QAction::triggered, this, &MainWindow::clearSchedule);

    QMenu* songs = bar->addMenu(QStringLiteral("&Songs"));
    connect(songs->addAction(QStringLiteral("New Song...")), &QAction::triggered, this, &MainWindow::newSong);
    m_editSelectedAction = songs->addAction(QStringLiteral("Edit Selected Song"));
    connect(m_editSelectedAction, &QAction::triggered, this, &MainWindow::editActiveSong);
    m_deleteSelectedAction = songs->addAction(QStringLiteral("Delete Selected Song"));
    connect(m_deleteSelectedAction, &QAction::triggered, this, &MainWindow::deleteSelectedSong);
    songs->addSeparator();
    connect(songs->addAction(QStringLiteral("Open Songs Database Folder...")), &QAction::triggered, this,
            &MainWindow::openSongsFolder);
    connect(songs->addAction(QStringLiteral("Change Database Folder...")), &QAction::triggered, this,
            &MainWindow::changeSongsFolder);
    songs->addSeparator();
    connect(songs->addAction(QStringLiteral("Import Song Library Backup (.json)...")), &QAction::triggered, this,
            &MainWindow::importSongsBackup);
    connect(songs->addAction(QStringLiteral("Export Song Library Backup (.json)...")), &QAction::triggered, this,
            &MainWindow::exportSongsBackup);
    songs->addSeparator();
    connect(songs->addAction(QStringLiteral("Load Default Sample Songs")), &QAction::triggered, this,
            &MainWindow::loadSampleSongs);

    QMenu* scriptures = bar->addMenu(QStringLiteral("Scri&ptures"));
    connect(scriptures->addAction(QStringLiteral("Import Bible Translation (.json)...")), &QAction::triggered, this, [this] {
        m_library->selectTab(1);
        scripturesTab()->promptImport();
    });

    QMenu* live = bar->addMenu(QStringLiteral("&Live"));
    // Shortcuts are handled by the window-level key handler (PreviewKeyDown), not QAction shortcuts, so
    // that Space / Ctrl+C keep working the way the reference does; the menu only shows the gesture text.
    m_goLiveAction = live->addAction(QStringLiteral("Go Live\tF5"));
    connect(m_goLiveAction, &QAction::triggered, this, [this] { goLive(); });
    m_blackAction = live->addAction(QStringLiteral("Black\tCtrl+B"));
    m_blackAction->setCheckable(true);
    connect(m_blackAction, &QAction::triggered, this, &MainWindow::toggleBlack);
    m_clearAction = live->addAction(QStringLiteral("Clear\tCtrl+C"));
    m_clearAction->setCheckable(true);
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::toggleClear);
    m_logoAction = live->addAction(QStringLiteral("Logo\tF8"));
    m_logoAction->setCheckable(true);
    connect(m_logoAction, &QAction::triggered, this, &MainWindow::toggleLogo);
    m_liveOutputAction = live->addAction(QStringLiteral("Live Output"));
    m_liveOutputAction->setCheckable(true);
    connect(m_liveOutputAction, &QAction::triggered, this, &MainWindow::toggleLiveOutput);
    live->addSeparator();
    m_displayMenu = live->addMenu(QStringLiteral("Output Display"));
    connect(m_displayMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildDisplayMenu);
    live->addSeparator();
    QAction* stageAct = live->addAction(QStringLiteral("Stage Display (Confidence Monitor)\tCtrl+Shift+D"));
    stageAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(stageAct, &QAction::triggered, this, &MainWindow::toggleStageWindow);

    QMenu* view = bar->addMenu(QStringLiteral("&View"));
    m_themeAction = view->addAction(QString());
    m_themeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::toggleThemeMode);

    QMenu* profiles = bar->addMenu(QStringLiteral("&Profiles"));
    QAction* def = profiles->addAction(QStringLiteral("Default Profile"));
    def->setCheckable(true);
    def->setChecked(true);
    connect(def, &QAction::toggled, def, [def](bool on) { if (!on) def->setChecked(true); }); // only profile
    profiles->addSeparator();
    connect(profiles->addAction(QStringLiteral("Export Songs Profile...")), &QAction::triggered, this,
            &MainWindow::exportSongsBackup);
    connect(profiles->addAction(QStringLiteral("Import Songs Profile...")), &QAction::triggered, this,
            &MainWindow::importSongsBackup);

    QMenu* help = bar->addMenu(QStringLiteral("&Help"));
    QAction* updateAct = help->addAction(QStringLiteral("Check for Updates..."));
    connect(updateAct, &QAction::triggered, this, &MainWindow::checkForUpdates);
    help->addSeparator();
    QAction* donate = help->addAction(QStringLiteral("\u2615 Support && Donate (Ko-fi)..."));
    connect(donate, &QAction::triggered, this, &MainWindow::openKofi);
    help->addSeparator();
    QAction* about = help->addAction(QStringLiteral("About LuxPresenter"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    // Hidden debug shortcut (Ctrl+Shift+U) to preview the Software Update dialog
    auto* previewUpdateAct = new QAction(this);
    previewUpdateAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
    connect(previewUpdateAct, &QAction::triggered, this, [this] {
        UpdateService::showPreviewDialog(this);
    });
    addAction(previewUpdateAct);
}

void MainWindow::buildToolbar()
{
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("Toolbar"));
    m_toolbar = frame;
    auto* lay = new QHBoxLayout(frame);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(0);

    auto track = [this](ClassicButton* b) { m_toolbarButtons.append(b); return b; };

    // ---- left group ----
    m_newSongButton = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    m_newSongButton->addIcon(Icon::Add, 15, 15);
    m_newSongButton->setToolTip(QStringLiteral("New Song"));
    connect(m_newSongButton, &QAbstractButton::clicked, this, &MainWindow::newSong);

    m_editSongButton = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    m_editSongButton->addIcon(Icon::Edit, 15, 15);
    m_editSongButton->setToolTip(QStringLiteral("Edit Active Song"));
    connect(m_editSongButton, &QAbstractButton::clicked, this, &MainWindow::editActiveSong);

    auto* display = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    display->addIcon(Icon::Display, 16, 15);
    display->setToolTip(QStringLiteral("Options (projector output, text, transitions, online)"));
    connect(display, &QAbstractButton::clicked, this, &MainWindow::openOptions);

    m_globe = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    m_globe->addIcon(Icon::Globe, 15, 15);
    m_globe->setToolTip(QStringLiteral("Online Web Integration"));
    connect(m_globe, &QAbstractButton::clicked, this, &MainWindow::toggleOnline);

    m_donate = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    m_donate->addIcon(Icon::Donate, 14, 14, 4);
    m_donate->addText(QStringLiteral("Donate"), tahoma(11, true), ClassicButton::ColorRole::Donate);
    m_donate->setToolTip(QStringLiteral("Support LuxPresenter on Ko-fi (%1)").arg(QString::fromLatin1(kKofiUrl)));
    connect(m_donate, &QAbstractButton::clicked, this, &MainWindow::openKofi);

    m_themeButton = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    connect(m_themeButton, &QAbstractButton::clicked, this, &MainWindow::toggleThemeMode);

    for (QWidget* w : {static_cast<QWidget*>(m_newSongButton), static_cast<QWidget*>(m_editSongButton),
                       static_cast<QWidget*>(display), static_cast<QWidget*>(m_globe),
                       static_cast<QWidget*>(m_donate), static_cast<QWidget*>(m_themeButton)})
        lay->addWidget(w);
    lay->addStretch(1);

    // ---- right group ----
    auto* background = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    background->addIcon(Icon::Background, 14, 14, 4);
    background->addText(QStringLiteral("Background"), tahoma(11));
    background->addText(QStringLiteral(" \u25BE"), tahoma(9), ClassicButton::ColorRole::Secondary);
    background->setToolTip(QStringLiteral("Background Options"));
    connect(background, &QAbstractButton::clicked, this, &MainWindow::openBackgroundDialog);

    auto* goLive = track(new ClassicButton(ClassicButton::Kind::Framed, frame));
    goLive->addText(QStringLiteral("\u25B6"), segoeSymbol(12), ClassicButton::ColorRole::Fixed, 4, QColor(0x16, 0xA3, 0x4A));
    goLive->addText(QStringLiteral("Go Live"), tahoma(11, true));
    goLive->setToolTip(QStringLiteral("Go Live (F5)"));
    m_goLiveButton = goLive;
    connect(goLive, &QAbstractButton::clicked, this, [this] { this->goLive(); });

    auto* alert = track(new ClassicButton(ClassicButton::Kind::Icon, frame));
    alert->addIcon(Icon::Alert, 14, 14, 4);
    alert->addText(QStringLiteral("\u25BE"), tahoma(9), ClassicButton::ColorRole::Secondary);
    alert->setToolTip(QStringLiteral("Alert Message"));
    m_alertButton = alert;
    connect(alert, &ClassicButton::clicked, this, [this] {
        if (!m_alertWindow) {
            m_alertWindow = new MessageAlertWindow(m_data, this);
        }
        m_alertWindow->show();
        m_alertWindow->raise();
        m_alertWindow->activateWindow();
    });

    auto* logo = track(new ClassicButton(ClassicButton::Kind::Toggle, frame));
    logo->addIcon(Icon::Logo, 13, 13, 4);
    logo->addText(QStringLiteral("Logo"), tahoma(11));
    logo->setToolTip(QStringLiteral("Logo (F8)"));
    m_logoButton = logo;
    connect(logo, &QAbstractButton::toggled, this, [this](bool on) {
        m_projection->toggleLogo(on);
        syncLiveAction(m_logoAction, on);
        updateStageDisplay();
    });

    auto* black = track(new ClassicButton(ClassicButton::Kind::Toggle, frame));
    black->addIcon(Icon::BlackSquare, 11, 11, 4);
    black->addText(QStringLiteral("Black"), tahoma(11));
    black->setToolTip(QStringLiteral("Black (Ctrl+B, F6)"));
    m_blackButton = black;
    connect(black, &QAbstractButton::toggled, this, [this](bool on) {
        m_projection->toggleBlack(on);
        m_live->canvas()->setBlack(on);
        syncLiveAction(m_blackAction, on);
        updateStageDisplay();
    });

    auto* clear = track(new ClassicButton(ClassicButton::Kind::Toggle, frame));
    clear->addIcon(Icon::Clear, 12, 12, 4);
    clear->addText(QStringLiteral("Clear"), tahoma(11));
    clear->setToolTip(QStringLiteral("Clear (Ctrl+C, F7)"));
    m_clearButton = clear;
    connect(clear, &QAbstractButton::toggled, this, [this](bool on) {
        m_projection->toggleClear(on);
        m_live->canvas()->setTextCleared(on);
        syncLiveAction(m_clearAction, on);
        updateStageDisplay();
    });

    auto* liveToggle = track(new ClassicButton(ClassicButton::Kind::Live, frame));
    liveToggle->addText(QStringLiteral("Live"), tahoma(11, true));
    liveToggle->setToolTip(QStringLiteral("Toggle Live Output"));
    m_liveButton = liveToggle;
    connect(liveToggle, &QAbstractButton::toggled, this, [this](bool on) { setLiveOutput(on); });

    lay->addWidget(background);
    lay->addSpacing(7); // Background button has a 0,0,8,0 margin in the reference
    lay->addWidget(goLive);
    lay->addWidget(alert);
    lay->addWidget(logo);
    lay->addWidget(black);
    lay->addWidget(clear);
    lay->addWidget(liveToggle);
}

void MainWindow::buildWorkArea()
{
    m_workStack = new QStackedWidget(this);

    // Page 0: three columns (left 340 [240..520], preview 1*, live 1*) with 5px splitters.
    m_schedule = new ScheduleView(*m_scheduleService, this);
    m_library = new LibraryView(m_data, *m_presentations, this);

    // Schedule 45% / Library 55% of the column height (and kept while resizing until the user drags the handle).
    auto* leftSplit = new ProportionalSplitter(Qt::Vertical, this);
    leftSplit->setDefaultRatios({45, 55});
    m_leftSplitter = leftSplit;
    m_leftSplitter->setChildrenCollapsible(false);
    m_leftSplitter->setHandleWidth(5);
    m_leftSplitter->addWidget(m_schedule);
    m_leftSplitter->addWidget(m_library);
    m_schedule->setMinimumHeight(140);
    m_library->setMinimumHeight(180);
    m_leftSplitter->setStretchFactor(0, 0);
    m_leftSplitter->setStretchFactor(1, 1);
    // Left column: 340px initially, 240..520 (MinWidth/MaxWidth of the reference column).
    auto* leftColumn = new QWidget(this);
    leftColumn->setMinimumWidth(240);
    leftColumn->setMaximumWidth(520);
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_leftSplitter);

    m_preview = new SlidePaneView(QStringLiteral("Preview"), /*isLive=*/false, this);
    m_live = new SlidePaneView(QStringLiteral("Live"), /*isLive=*/true, this);
    m_preview->setSettings(m_data.settings());
    m_live->setSettings(m_data.settings());
    m_liveBackground = activeGlobalBackground();
    m_preview->canvas()->setBackground(m_liveBackground);
    m_live->canvas()->setBackground(m_liveBackground);
    m_projection->setBackground(m_liveBackground);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setHandleWidth(5);
    m_mainSplitter->addWidget(leftColumn);
    m_mainSplitter->addWidget(m_preview);
    m_mainSplitter->addWidget(m_live);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 1);
    m_workStack->addWidget(m_mainSplitter);

    // Page 1: Online browser across the whole work area.
    m_online = new OnlineBrowserView(this);
    m_workStack->addWidget(m_online);

    connect(m_library, &LibraryView::currentTabChanged, this, &MainWindow::onLibraryTabChanged);
    connect(m_online, &OnlineBrowserView::closeRequested, this, [this] { m_library->selectTab(m_lastLibraryTab); });
}

void MainWindow::buildStatusBar()
{
    QStatusBar* sb = statusBar();
    sb->setSizeGripEnabled(false);
    sb->setFixedHeight(22);
    m_statusLabel = new QLabel(QStringLiteral("Ready"), sb);
    m_statusLabel->setFont(tahoma(11));
    m_statusLabel->setContentsMargins(4, 0, 0, 0);
    sb->addWidget(m_statusLabel, 1);
    m_displayLabel = new QLabel(sb);
    m_displayLabel->setFont(tahoma(11));
    m_displayLabel->setStyleSheet(QStringLiteral("color:#047857;"));
    m_displayLabel->setContentsMargins(0, 0, 8, 0);
    sb->addPermanentWidget(m_displayLabel);
    updateDisplayStatus();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { updateDisplayStatus(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { updateDisplayStatus(); });
}

// ---------------------------------------------------------------------------------------
// Songs & schedule wiring (MainViewModel event hooks)
// ---------------------------------------------------------------------------------------

void MainWindow::wireSongsAndSchedule()
{
    m_online->setAdBlock(m_adBlock);
    connect(m_online, &OnlineBrowserView::adBlockEdited, this, [this] {
        AppSettings s = m_data.settings();
        s.adBlockEnabled = m_adBlock->enabled();
        s.adBlockWhitelist = m_adBlock->whitelist();
        m_data.saveSettings(s);
    });
    connect(scripturesTab(), &ScripturesTab::passageSelected, this, [this](const BiblePassage& p) { showPassage(p, false); });
    connect(scripturesTab(), &ScripturesTab::passageActivated, this, [this](const BiblePassage& p) { showPassage(p, true); });
    connect(scripturesTab(), &ScripturesTab::addToScheduleRequested, this, &MainWindow::addPassageToSchedule);
    SongsTab* st = songsTab();
    connect(st, &SongsTab::songSelectedForPreview, this, &MainWindow::onSongSelectedForPreview);
    connect(st, &SongsTab::songDoubleClicked, this, [this](const QString& id) {
        onSongSelectedForPreview(id);
        goLive();
    });
    connect(st, &SongsTab::goLiveRequested, this, [this](const QString& id) {
        onSongSelectedForPreview(id);
        goLive();
    });
    connect(st, &SongsTab::addToScheduleRequested, this, &MainWindow::addSongToSchedule);
    connect(st, &SongsTab::newSongRequested, this, &MainWindow::newSong);
    connect(st, &SongsTab::editSongRequested, this, &MainWindow::editSong);
    connect(st, &SongsTab::deleteSongRequested, this, [this](const QString& id) { deleteSongById(id, true); });
    connect(st, &SongsTab::importBackupRequested, this, &MainWindow::importSongsBackup);
    connect(st, &SongsTab::openSongsFolderRequested, this, &MainWindow::openSongsFolder);
    connect(st, &SongsTab::loadSamplesRequested, this, &MainWindow::loadSampleSongs);
    connect(st, &SongsTab::selectionStateChanged, this, [this](bool) { updateSongActions(); });

    connect(m_scheduleService, &ScheduleService::itemSelectedForPreview, this, &MainWindow::onScheduleItemSelected);
    connect(m_schedule, &ScheduleView::addRequested, this, &MainWindow::addPreviewToSchedule);
    connect(m_schedule, &ScheduleView::newRequested, this, &MainWindow::newSchedule);
    connect(m_schedule, &ScheduleView::openRequested, this, &MainWindow::openSchedule);
    connect(m_schedule, &ScheduleView::saveRequested, this, [this] { saveSchedule(false); });
    connect(m_schedule, &ScheduleView::saveAsRequested, this, [this] { saveSchedule(true); });
    connect(m_schedule, &ScheduleView::clearRequested, this, &MainWindow::clearSchedule);
    connect(m_schedule, &ScheduleView::itemActivated, this, [this](int i) {
        m_scheduleService->userSelect(i);
        goLiveAfterSelect();
    });
    connect(m_schedule, &ScheduleView::goLiveRequested, this, [this](int i) {
        m_scheduleService->userSelect(i);
        goLiveAfterSelect();
    });
    connect(m_schedule, &ScheduleView::songDropped, this, [this](const QString& songId, int index) {
        Song s;
        if (!m_data.songById(songId, &s))
            return;
        m_scheduleService->insertItem(index, ScheduleItem::fromPresentation(s.toPresentationItem(&m_data.settings())));
    });
}

QString MainWindow::activeGlobalBackground() const
{
    return Backgrounds::usable(m_data.settings().lastUsedBackground);
}
void MainWindow::openBackgroundDialog()
{
    BackgroundDialog dlg(m_data, activeGlobalBackground(), this);
    dlg.setApplyToLive(false); // Default is UNCHECKED per user requirement
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedValue().isEmpty())
        setActiveBackground(dlg.selectedValue(), dlg.applyToLive());
}
void MainWindow::setActiveBackground(const QString& background, bool updateLive)
{
    const QString bg = Backgrounds::usable(background);
    const bool missing = bg != background.trimmed();
    AppSettings s = m_data.settings();
    s.lastUsedBackground = bg;
    m_data.saveSettings(s);
    m_preview->canvas()->setBackground(bg);
    if (m_projection)
        m_projection->preloadBackground(bg);
    if (updateLive) {
        m_liveBackground = bg;
        m_live->canvas()->setBackground(bg);
        m_projection->setBackground(bg);
    }
    setStatusText(missing ? QStringLiteral("Background not available, using the default instead.")
                          : QStringLiteral("Background: %1%2").arg(bg.startsWith(QLatin1Char('#')) ? bg : QFileInfo(bg).fileName(),
                                                                    updateLive ? QStringLiteral(" (Preview and Live)") : QStringLiteral(" (Preview)")));
}

void MainWindow::showInPreview(const PresentationItem& item, const QString& background, int slideIndex)
{
    m_preview->loadItem(item, background, slideIndex);
    if (m_projection)
        m_projection->preloadBackground(background);
}

void MainWindow::onSongSelectedForPreview(const QString& songId)
{
    Song song;
    if (!m_data.songById(songId, &song))
        return;
    m_previewSource = PreviewSource::SongList;
    m_activeSongId = songId;
    songsTab()->setPaneActive(true);
    m_schedule->setPaneActive(false);

    const PresentationItem pres = song.toPresentationItem(&m_data.settings());
    // Only an existing image file overrides the global background (colour values do not).
    const QString bg = (!pres.backgroundPath.trimmed().isEmpty() && QFile::exists(pres.backgroundPath))
                           ? pres.backgroundPath
                           : activeGlobalBackground();
    showInPreview(pres, bg);
    // A lyrics search selects the stanza it found (Preview only: nothing is sent to Live or the projector).
    const QString lyricsQuery = songsTab()->lyricsQuery().trimmed();
    if (!lyricsQuery.isEmpty()) {
        QStringList stanzas;
        for (const Slide& s : pres.slides)
            stanzas << s.content;
        const int match = SongSearch::matchingStanza(stanzas, lyricsQuery);
        if (match >= 0)
            m_preview->setCurrentSlideIndex(match);
    }
    setStatusText(QStringLiteral("Preview: %1").arg(displayText(song.title)));
}

void MainWindow::onScheduleItemSelected(int index)
{
    if (index < 0 || index >= m_scheduleService->count())
        return;
    const ScheduleItem& item = m_scheduleService->items().at(index);
    m_previewSource = PreviewSource::Schedule;
    m_schedule->setPaneActive(true);
    songsTab()->setPaneActive(false);

    if (item.category == ItemCategory::Presentation) {
        openScheduleDocument(item);
        return;
    }
    m_pendingDoc = PendingDoc();   // a different kind of item replaces a document that was still loading

    // The item's own background (colour or existing file) wins; a missing file falls back to the global one.
    const QString bg = Backgrounds::usable(item.presentation.backgroundPath, activeGlobalBackground());
    showInPreview(item.presentation, bg);
    setStatusText(QStringLiteral("Preview: %1").arg(displayText(item.title)));
}

void MainWindow::showPassage(const BiblePassage& p, bool goLiveNow)
{
    if (!p.isValid())
        return;
    m_previewSource = PreviewSource::Scripture;
    m_activePassage = p;
    songsTab()->setPaneActive(false);
    m_schedule->setPaneActive(false);
    showInPreview(p.toPresentationItem(&m_data.settings()), activeGlobalBackground());
    setStatusText(QStringLiteral("Preview: %1 (%2)").arg(p.reference, p.translationId));
    if (goLiveNow)
        goLive(0);
}
void MainWindow::addPassageToSchedule(const BiblePassage& p)
{
    if (!p.isValid())
        return;
    PresentationItem item = p.toPresentationItem(&m_data.settings());
    item.backgroundPath = activeGlobalBackground();
    for (Slide& s : item.slides)
        s.customBackgroundPath = item.backgroundPath;
    m_scheduleService->addToSchedule(item);
    setStatusText(QStringLiteral("Added to Schedule: %1 (%2)").arg(p.reference, p.translationId));
}
void MainWindow::onLibraryTabChanged(int index)
{
    const bool wasOnline = m_onlineMode;
    m_onlineMode = (index == LibraryView::kOnlineTab);
    if (!m_onlineMode)
        m_lastLibraryTab = index;
    m_workStack->setCurrentIndex(m_onlineMode ? 1 : 0);

    if (wasOnline && !m_onlineMode && m_online) {
        // Leaving Online browser: fully terminate WebView2 processes and release RAM/VRAM
        m_online->teardown();
    }

    if (m_onlineMode)
        return;

    switch (index) {
    case 0: { // Songs tab
        m_previewSource = PreviewSource::SongList;
        m_schedule->setPaneActive(false);
        songsTab()->setPaneActive(true);
        const QString songId = !songsTab()->selectedSongId().isEmpty()
                                   ? songsTab()->selectedSongId()
                                   : m_activeSongId;
        if (!songId.isEmpty()) {
            onSongSelectedForPreview(songId);
        } else if (!songsTab()->filteredSongs().isEmpty()) {
            songsTab()->userSelect(songsTab()->filteredSongs().first().id);
        }
        break;
    }
    case 1: { // Scriptures tab
        m_previewSource = PreviewSource::Scripture;
        m_schedule->setPaneActive(false);
        songsTab()->setPaneActive(false);
        ScripturesTab* scTab = scripturesTab();
        if (scTab->verseList()->selectedItems().isEmpty() && scTab->verseList()->count() > 0) {
            scTab->verseList()->setCurrentRow(0);
            scTab->verseList()->item(0)->setSelected(true);
        }
        const BiblePassage p = scTab->currentPassage();
        if (p.isValid())
            showPassage(p, false);
        break;
    }
    case 2: { // Media tab
        MediaTab* mediaTab = m_library->mediaTab();
        if (!mediaTab->selectedMedia().has_value() && mediaTab->grid()->count() > 0) {
            mediaTab->grid()->setCurrentRow(0);
            mediaTab->grid()->item(0)->setSelected(true);
        }
        if (mediaTab->selectedMedia().has_value()) {
            showMediaInPreview(*mediaTab->selectedMedia());
        } else {
            m_previewSource = PreviewSource::Media;
            m_activeMedia = MediaItem();
            m_schedule->setPaneActive(false);
            songsTab()->setPaneActive(false);
            m_preview->loadEmptyMedia();
            setStatusText(QStringLiteral("Preview: Media"));
        }
        break;
    }
    case 3: { // Presentation tab
        PresentationTab* presTab = m_library->presentationTab();
        if (!presTab->currentDocument() && presTab->documentCombo()->count() > 0) {
            if (presTab->documentCombo()->currentIndex() < 0) {
                presTab->documentCombo()->setCurrentIndex(0);
            }
        }
        if (presTab->currentDocument()) {
            if (presTab->slideGrid()->currentRow() < 0 && presTab->slideGrid()->count() > 0)
                presTab->slideGrid()->setCurrentRow(0);
            const int row = std::max(0, presTab->slideGrid()->currentRow());
            showDocumentInPreview(*presTab->currentDocument(), row);
        } else {
            m_previewSource = PreviewSource::Presentation;
            m_schedule->setPaneActive(false);
            songsTab()->setPaneActive(false);
            m_preview->loadEmptyPresentation();
            setStatusText(QStringLiteral("Preview: Presentation"));
        }
        break;
    }
    default:
        break;
    }
}

void MainWindow::updateSongActions()
{
    const bool has = !songsTab()->selectedSongId().isEmpty();
    songsTab()->actionAddToSchedule()->setEnabled(has);
    songsTab()->actionEdit()->setEnabled(has);
    songsTab()->actionDelete()->setEnabled(has);
    m_deleteSelectedAction->setEnabled(has);
}

// ---------------------------------------------------------------------------------------
// Songs
// ---------------------------------------------------------------------------------------

void MainWindow::addSongToSchedule(const QString& songId)
{
    Song s;
    if (!m_data.songById(songId, &s))
        return;
    m_scheduleService->addToSchedule(s.toPresentationItem(&m_data.settings()));
}

void MainWindow::addPreviewToSchedule()
{
    if (m_preview->hasDocument()) {
        addDocumentToSchedule(m_preview->document());
        return;
    }
    const QList<Slide> slides = m_preview->slides();
    if (!slides.isEmpty()) {
        PresentationItem pres;
        pres.title = m_preview->itemTitle();
        pres.subtitle = m_preview->itemSubtitle();
        pres.slides = slides;
        if (m_previewSource == PreviewSource::Scripture)
            pres.category = ItemCategory::Scripture;
        // the background shown with the item travels with it (saved in schedules, restored on selection)
        pres.backgroundPath = m_preview->canvas()->background();
        for (Slide& sl : pres.slides)
            if (sl.customBackgroundPath.trimmed().isEmpty())
                sl.customBackgroundPath = pres.backgroundPath;
        m_scheduleService->addToSchedule(pres);
        setStatusText(QStringLiteral("Added to Schedule: %1").arg(displayText(pres.title)));
    } else if (!songsTab()->selectedSongId().isEmpty()) {
        Song s;
        if (m_data.songById(songsTab()->selectedSongId(), &s)) {
            const PresentationItem pres = s.toPresentationItem(&m_data.settings());
            m_scheduleService->addToSchedule(pres);
            setStatusText(QStringLiteral("Added to Schedule: %1").arg(displayText(pres.title)));
        }
    }
}

void MainWindow::applyEditedSong(const Song* original, const Song& edited, bool linked)
{
    const QString oldTitle = original ? original->title : QString();
    if (!m_data.saveSong(edited)) {
        QMessageBox::critical(this, QStringLiteral("LuxPresenter"),
                              QStringLiteral("Failed to save the song:\n%1").arg(m_data.lastError()));
        return;
    }
    // Capture the preview state before the list reload possibly re-previews the song.
    const bool previewShowsSong =
        m_preview->itemTitle().compare(edited.title, Qt::CaseInsensitive) == 0 ||
        (original && m_preview->itemTitle().compare(oldTitle, Qt::CaseInsensitive) == 0);
    const int curIdx = m_preview->currentSlideIndex();

    songsTab()->reload(edited.id);
    const PresentationItem updated = edited.toPresentationItem(&m_data.settings());
    if (previewShowsSong)
        showInPreview(updated, activeGlobalBackground(), std::max(0, curIdx));
    if (linked)
        m_scheduleService->replaceByTitle(oldTitle, edited.title, updated);
}

void MainWindow::newSong()
{
    SongEditorWindow editor(nullptr, this);
    if (editor.exec() == QDialog::Accepted)
        applyEditedSong(nullptr, editor.resultSong(), editor.applyToSchedule());
}

void MainWindow::editSong(const QString& songId)
{
    Song s;
    if (!m_data.songById(songId, &s))
        return;
    SongEditorWindow editor(&s, this);
    if (editor.exec() == QDialog::Accepted)
        applyEditedSong(&s, editor.resultSong(), editor.applyToSchedule());
}

void MainWindow::editSelectedSong()
{
    if (!songsTab()->selectedSongId().isEmpty())
        editSong(songsTab()->selectedSongId());
}

void MainWindow::editActiveSong()
{
    const QString title = m_preview->itemTitle().trimmed();
    if (title.isEmpty()) {
        editSelectedSong();
        return;
    }
    const QList<Song> songs = m_data.songs();
    auto find = [&](auto pred) -> const Song* {
        for (const Song& s : songs)
            if (pred(s))
                return &s;
        return nullptr;
    };
    const Song* match = find([&](const Song& s) { return s.title.compare(title, Qt::CaseInsensitive) == 0; });
    if (!match)
        match = find([&](const Song& s) { return s.title.startsWith(title, Qt::CaseInsensitive); });
    if (!match)
        match = find([&](const Song& s) { return title.startsWith(s.title, Qt::CaseInsensitive); });

    Song working;
    if (match) {
        working = *match;
    } else if (!m_preview->slides().isEmpty()) {
        // The preview shows something that is not a stored song: offer it as a new song.
        QStringList blocks;
        for (const Slide& s : m_preview->slides())
            blocks << ((!s.label.trimmed().isEmpty() ? QStringLiteral("[%1]\n").arg(s.label) : QString()) + s.content);
        working.title = title;
        QString author = m_preview->itemSubtitle();
        author.replace(QStringLiteral("By "), QString());
        working.author = author.trimmed();
        working.lyricsRaw = blocks.join(QStringLiteral("\n\n"));
        working.defaultBackground = m_preview->canvas()->background();
    } else {
        return;
    }
    SongEditorWindow editor(&working, this);
    if (editor.exec() == QDialog::Accepted)
        applyEditedSong(&working, editor.resultSong(), editor.applyToSchedule());
}

bool MainWindow::deleteSongById(const QString& id, bool askConfirmation)
{
    Song s;
    if (!m_data.songById(id, &s))
        return false;
    if (askConfirmation) {
        const auto res = QMessageBox::question(this, QStringLiteral("Delete Song"),
                                               QStringLiteral("Are you sure you want to delete '%1'?").arg(s.title),
                                               QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes)
            return false;
    }
    if (!m_data.deleteSong(id))
        return false;
    songsTab()->reload();
    updateSongActions();
    return true;
}

void MainWindow::deleteSelectedSong()
{
    if (!songsTab()->selectedSongId().isEmpty())
        deleteSongById(songsTab()->selectedSongId(), true);
}

void MainWindow::changeSongsFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Songs Database Folder"),
                                                          AppPaths::songsDirectory());
    if (dir.isEmpty())
        return;
    if (!m_data.switchSongsDirectory(dir)) {
        QMessageBox::critical(this, QStringLiteral("Error"),
                              QStringLiteral("Failed to switch database folder: %1").arg(m_data.lastError()));
        return;
    }
    songsTab()->reload();
    QMessageBox::information(this, QStringLiteral("Songs Database Location"),
                             QStringLiteral("Songs database folder successfully changed to:\n%1\n\nLoaded %2 songs.")
                                 .arg(QDir::toNativeSeparators(dir))
                                 .arg(m_data.songs().size()));
}

void MainWindow::loadSampleSongs()
{
    const auto res = QMessageBox::question(
        this, QStringLiteral("Load Sample Songs"),
        QStringLiteral("Load default sample songs into your library?\n\nThis will add sample worship songs to your library."),
        QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes)
        return;
    if (m_data.loadSampleSongs() < 0) {
        QMessageBox::critical(this, QStringLiteral("LuxPresenter"),
                              QStringLiteral("Failed to load sample songs:\n%1").arg(m_data.lastError()));
        return;
    }
    songsTab()->reload();
    QMessageBox::information(this, QStringLiteral("LuxPresenter"), QStringLiteral("Sample songs loaded successfully!"));
}

void MainWindow::exportSongsBackup()
{
    const QList<Song> songs = m_data.songs();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Songs Profile (Backup)"),
                                                      QStringLiteral("LuxPresenter_Songs_Backup.json"),
                                                      QString::fromLatin1(kSongsFilter));
    if (path.isEmpty())
        return;
    QString err;
    if (!SongImportExport::exportSongs(path, songs, &err)) {
        QMessageBox::critical(this, QStringLiteral("Export Error"), QStringLiteral("Failed to export songs:\n%1").arg(err));
        return;
    }
    QMessageBox::information(this, QStringLiteral("Export Songs Backup"),
                             QStringLiteral("Successfully exported %1 songs to:\n%2").arg(songs.size()).arg(QDir::toNativeSeparators(path)));
}

void MainWindow::importSongsBackup()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import Songs Profile (Backup)"), QString(),
                                                      QString::fromLatin1(kSongsFilter));
    if (path.isEmpty())
        return;
    QList<Song> imported;
    QString err;
    if (!SongImportExport::importSongs(path, imported, &err)) {
        QMessageBox::critical(this, QStringLiteral("Import Error"),
                              QStringLiteral("Failed to import songs backup:\n%1").arg(err));
        return;
    }
    if (imported.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Import Error"), QStringLiteral("No songs found in the selected file."));
        return;
    }
    QMessageBox box(QMessageBox::Question, QStringLiteral("Import Songs Backup"),
                    QStringLiteral("Found %1 songs in this backup profile.\n\n"
                                   "\u2022 Click [Yes] to Merge with your existing songs.\n"
                                   "\u2022 Click [No] to Replace all current songs with this backup.\n"
                                   "\u2022 Click [Cancel] to abort.")
                        .arg(imported.size()),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this);
    const int res = box.exec();
    if (res == QMessageBox::Yes) {
        m_data.addSongs(imported, false);
        songsTab()->reload();
        QMessageBox::information(this, QStringLiteral("Import Complete"),
                                 QStringLiteral("Successfully merged %1 songs into your library!").arg(imported.size()));
    } else if (res == QMessageBox::No) {
        m_data.addSongs(imported, true);
        songsTab()->reload();
        QMessageBox::information(this, QStringLiteral("Import Complete"),
                                 QStringLiteral("Successfully replaced library with %1 songs from backup!").arg(imported.size()));
    }
}

// ---------------------------------------------------------------------------------------
// Schedule files
// ---------------------------------------------------------------------------------------

bool MainWindow::newScheduleNoPrompt()
{
    m_scheduleService->clear();
    return true;
}

bool MainWindow::newSchedule()
{
    if (!promptSaveIfModified())
        return false;
    return newScheduleNoPrompt();
}

bool MainWindow::openScheduleFile(const QString& path, QString* error)
{
    return m_scheduleService->loadFromFile(path, error);
}

bool MainWindow::saveScheduleFile(const QString& path, QString* error)
{
    return m_scheduleService->saveToFile(path, error);
}

bool MainWindow::openSchedule()
{
    if (!promptSaveIfModified())
        return false;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Schedule"), QString(),
                                                      QString::fromLatin1(kScheduleFilter));
    if (path.isEmpty())
        return false;
    QString err;
    if (!openScheduleFile(path, &err)) {
        QMessageBox::critical(this, QStringLiteral("LuxPresenter"), QStringLiteral("Failed to open schedule: %1").arg(err));
        return false;
    }
    return true;
}

bool MainWindow::saveSchedule(bool saveAs)
{
    QString path = m_scheduleService->currentFilePath();
    if (saveAs || path.trimmed().isEmpty()) {
        const QString suggested = path.trimmed().isEmpty()
                                      ? QStringLiteral("Schedule_%1.fws").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")))
                                      : QFileInfo(path).fileName();
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Schedule"), suggested,
                                            QString::fromLatin1(kScheduleFilter));
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += QStringLiteral(".fws");
    }
    QString err;
    if (!saveScheduleFile(path, &err)) {
        QMessageBox::critical(this, QStringLiteral("LuxPresenter"), QStringLiteral("Failed to save schedule: %1").arg(err));
        return false;
    }
    return true;
}

void MainWindow::clearSchedule()
{
    if (m_scheduleService->count() == 0)
        return;
    const auto res = QMessageBox::question(this, QStringLiteral("Clear Schedule"),
                                           QStringLiteral("Are you sure you want to clear the schedule?"),
                                           QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
        m_scheduleService->clear();
}

bool MainWindow::promptSaveIfModified()
{
    if (!m_scheduleService->hasUnsavedChanges() || m_scheduleService->count() == 0)
        return true;
    const auto res = QMessageBox::question(this, QStringLiteral("LuxPresenter"),
                                           QStringLiteral("Do you want to save changes to the current Schedule?"),
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (res == QMessageBox::Yes)
        return saveSchedule(false);
    return res == QMessageBox::No;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!promptSaveIfModified()) {
        e->ignore();
        return;
    }
    if (m_online && m_online->host())
        m_online->host()->cleanupBrowsingDataSync(2500);
    m_data.cleanupOrphanedData();
    m_projection->hideWindow(); // the projector windows must not keep the application alive
    QMainWindow::closeEvent(e);
}

// ---------------------------------------------------------------------------------------
// Online, theme, status, dialogs
// ---------------------------------------------------------------------------------------

void MainWindow::toggleOnline()
{
    m_library->selectTab(m_onlineMode ? m_lastLibraryTab : LibraryView::kOnlineTab);
}

void MainWindow::toggleThemeMode()
{
    AppSettings settings = m_data.settings();
    settings.isDarkMode = !settings.isDarkMode;
    m_data.saveSettings(settings);
    ThemeService::instance().apply(settings.isDarkMode);
}

void MainWindow::updateThemeUi()
{
    const bool dark = ThemeService::instance().isDark();
    m_themeAction->setText(dark ? QStringLiteral("\u2600 Light Mode") : QStringLiteral("\U0001F319 Dark Mode"));
    m_themeButton->clearParts();
    m_themeButton->addText(dark ? QStringLiteral("\u2600") : QStringLiteral("\U0001F319"), segoeSymbol(12),
                           ClassicButton::ColorRole::Fixed, 4,
                           dark ? QColor(0xFB, 0xBF, 0x24) : QColor(0x4F, 0x46, 0xE5));
    m_themeButton->addText(dark ? QStringLiteral("Light") : QStringLiteral("Dark"), tahoma(11, true));
    m_themeButton->setToolTip(dark ? QStringLiteral("Switch to Light Mode (Ctrl+T)")
                                   : QStringLiteral("Switch to Dark Mode (Ctrl+T)"));
}

void MainWindow::updateDisplayStatus()
{
    m_displayLabel->setText(m_projection->displayStatusText());
}

QString MainWindow::statusText() const { return m_statusLabel->text(); }
QString MainWindow::displayStatusText() const { return m_displayLabel->text(); }
void MainWindow::setStatusText(const QString& text) { m_statusLabel->setText(text); }

void MainWindow::showAbout()
{
    AboutWindow dlg(this);
    dlg.exec();
}

void MainWindow::checkForUpdates()
{
    if (m_suppressDialogs) {
        setStatusText(QStringLiteral("Checking for updates..."));
        return;
    }
    if (!m_updateService)
        m_updateService = new UpdateService(this);

    const bool shiftHeld = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
    setStatusText(shiftHeld ? QStringLiteral("Checking for updates (test mode)...") : QStringLiteral("Checking for updates..."));
    m_updateService->checkForUpdates(this, /*interactive=*/true, /*forceShowUpdate=*/shiftHeld);
}

void MainWindow::toggleStageWindow()
{
    if (!m_stageWindow)
        m_stageWindow = new StageWindow(this);

    if (m_stageWindow->isVisible()) {
        m_stageWindow->hide();
    } else {
        updateStageDisplay();
        m_stageWindow->show();
        m_stageWindow->raise();
        m_stageWindow->activateWindow();
    }
}

void MainWindow::updateStageDisplay()
{
    const bool serverRunning = m_stageServer && m_stageServer->isRunning();
    const bool windowVisible = m_stageWindow && m_stageWindow->isVisible();
    if (!serverRunning && !windowVisible)
        return;

    StageState s;
    s.time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    s.isBlack = m_projection ? m_projection->state().isBlack : false;
    s.isClear = m_projection ? m_projection->state().isClear : false;
    s.alertMessage = AlertService::instance().isAlertVisible() ? AlertService::instance().currentActiveMessage().text : QString();

    if (m_live) {
        s.title = m_live->itemTitle();
        if (m_live->hasDocument()) {
            s.currentSlide = QStringLiteral("Slide %1").arg(m_live->documentPage() + 1);
            s.nextSlide = QStringLiteral("Slide %1").arg(m_live->documentPage() + 2);
        } else {
            const auto slides = m_live->slides();
            const int idx = m_live->currentSlideIndex();
            if (idx >= 0 && idx < slides.size()) {
                s.currentSlide = slides[idx].content;
            }
            if (idx + 1 < slides.size()) {
                s.nextSlide = slides[idx + 1].content;
            } else if (!slides.isEmpty()) {
                s.nextSlide = QStringLiteral("(End of item)");
            }
        }
    }

    if (m_stageServer)
        m_stageServer->updateState(s);
    if (m_stageWindow && m_stageWindow->isVisible())
        m_stageWindow->updateState(s);
}

void MainWindow::openSongsFolder()
{
    QDir().mkpath(AppPaths::songsDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::songsDirectory()));
}

void MainWindow::openKofi()
{
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl)));
}

// ---------------------------------------------------------------------------------------
// Projection (Milestone 4)
// ---------------------------------------------------------------------------------------

MainWindow::~MainWindow()
{
    m_data.cleanupOrphanedData();
    if (qApp)
        qApp->removeEventFilter(this);
}

void MainWindow::setupProjection()
{
    m_projection->setSettings(m_data.settings());
    m_projection->setOutputFactory([this]() -> std::unique_ptr<IProjectionOutput> {
        return std::make_unique<ProjectionOutput>(ProjectionOutput::ScreenProvider{}, [this] {
            raise();
            activateWindow();
        });
    });
    connect(m_projection, &ProjectionService::windowVisibilityChanged, this, [this](bool) { updateDisplayStatus(); });
    connect(m_live, &SlidePaneView::currentSlideChanged, this, [this](int row) {
        if (!m_loadingLive && row >= 0)
            applyLiveSlide(row);
    });
    connect(&m_liveMediaTimer, &QTimer::timeout, this, [this] {
        if (m_live->hasMedia() && m_projection->isWindowShown()) {
            const double pos = m_projection->mediaPosition();
            const double dur = m_projection->mediaDuration();
            m_live->updateMediaProgress(pos, dur);
        }
    });
    connect(m_live, &SlidePaneView::mediaPlayPauseRequested, this, [this](bool play) {
        m_projection->setMediaPlaying(play);
    });
    connect(m_live, &SlidePaneView::mediaLoopToggled, this, [this](bool loop) {
        m_projection->setMediaLooping(loop);
    });
    connect(m_live, &SlidePaneView::mediaSeekRequested, this, [this](double fraction) {
        const double dur = m_projection->mediaDuration();
        if (dur > 0)
            m_projection->seekMedia(fraction * dur);
    });
    connect(m_live, &SlidePaneView::mediaVolumeChanged, this, [this](double vol) {
        m_projection->setMediaVolume(vol);
    });
    connect(m_preview, &SlidePaneView::slideDoubleClicked, this, [this](int row) { goLive(row); });
    auto monitorsChanged = [this] {
        if (!m_data.settings().outputDisplay.isEmpty())
            m_projection->setSelectedMonitorIndex(-1);   // re-resolved below by device name
        applyOutputDisplayByName(m_data.settings().outputDisplay);
        m_projection->refreshOutput();
        rebuildDisplayMenu();
        updateDisplayStatus();
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, monitorsChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, monitorsChanged);
    qApp->installEventFilter(this);
    applyOutputDisplayByName(m_data.settings().outputDisplay);
    rebuildDisplayMenu();
    updateDisplayStatus();
}

bool MainWindow::isLiveOutputActive() const
{
    return m_liveButton->isChecked();
}

void MainWindow::syncLiveAction(QAction* a, bool checked)
{
    if (a && a->isChecked() != checked) {
        QSignalBlocker b(a);
        a->setChecked(checked);
    }
}

void MainWindow::rebuildDisplayMenu()
{
    m_displayMenu->clear();
    delete m_displayGroup;
    m_displayGroup = new QActionGroup(this);
    m_displayGroup->setExclusive(true);
    const int selected = m_projection->selectedMonitorIndex();
    auto add = [&](const QString& text, int index) {
        QAction* a = m_displayMenu->addAction(text);
        a->setCheckable(true);
        a->setActionGroup(m_displayGroup);
        a->setChecked(index == selected);
        connect(a, &QAction::triggered, this, [this, index] { selectOutputDisplay(index); });
    };
    add(QStringLiteral("Automatic (prefer secondary display)"), -1);
    for (const ScreenInfo& s : m_projection->displays())
        add(QStringLiteral("%1: %2").arg(s.index + 1).arg(s.displayName()), s.index);
}

void MainWindow::selectOutputDisplay(int index)
{
    m_projection->setSelectedMonitorIndex(index);
    updateDisplayStatus();
    QString name;
    for (const ScreenInfo& s : m_projection->displays())
        if (s.index == index)
            name = s.deviceName;
    AppSettings s = m_data.settings();
    if (s.outputDisplay != name) {
        s.outputDisplay = name;
        m_data.saveSettings(s);
    }
}
// Selects the monitor with this device name (empty or not connected: automatic) without changing the saved choice.
void MainWindow::applyOutputDisplayByName(const QString& deviceName)
{
    int index = -1;
    if (!deviceName.isEmpty())
        for (const ScreenInfo& s : m_projection->displays())
            if (s.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0)
                index = s.index;
    m_projection->setSelectedMonitorIndex(index);
    rebuildDisplayMenu();
    updateDisplayStatus();
}
void MainWindow::openOptions()
{
    QList<OptionsDialog::Monitor> monitors;
    for (const ScreenInfo& s : m_projection->displays())
        monitors.append({s.deviceName, QStringLiteral("%1: %2").arg(s.index + 1).arg(s.displayName())});
    OptionsDialog dlg(m_data.settings(), monitors, m_adBlock, this);
    if (dlg.exec() == QDialog::Accepted)
        applySettings(dlg.settings());
}
void MainWindow::applySettings(const AppSettings& in)
{
    AppSettings s = in;
    s.sanitize();
    const AppSettings old = m_data.settings();
    if (!m_data.saveSettings(s)) {
        setStatusText(QStringLiteral("Could not save the settings: %1").arg(m_data.lastError()));
        if (!m_suppressDialogs)
            QMessageBox::warning(this, QStringLiteral("Options"), QStringLiteral("The settings could not be saved:\n%1").arg(m_data.lastError()));
    }
    if (s.isDarkMode != old.isDarkMode)
        ThemeService::instance().apply(s.isDarkMode);
    m_projection->setSettings(s);
    if (s.outputDisplay != old.outputDisplay)
        applyOutputDisplayByName(s.outputDisplay);
    m_adBlock->setEnabled(s.adBlockEnabled);
    m_adBlock->setWhitelist(s.adBlockWhitelist);
    if (m_stageServer && s.stageServerEnabled != old.stageServerEnabled) {
        if (s.stageServerEnabled)
            m_stageServer->start(8080);
        else
            m_stageServer->stop();
    }
    m_preview->setSettings(s);
    m_live->setSettings(s);
    // Songs are formatted when they are loaded into Preview: load the staged library song again.
    if (m_previewSource == PreviewSource::SongList && !m_activeSongId.isEmpty()) {
        const int row = m_preview->currentSlideIndex();
        onSongSelectedForPreview(m_activeSongId);
        m_preview->setCurrentSlideIndex(row);
    }
    if (m_previewSource == PreviewSource::Scripture && m_activePassage.isValid())
        showPassage(m_activePassage);   // verses are formatted when staged
    emit settingsApplied(s);
}

void MainWindow::setLiveOutput(bool on)
{
    if (m_liveButton->isChecked() != on) {
        QSignalBlocker b(m_liveButton);
        m_liveButton->setChecked(on);
    }
    syncLiveAction(m_liveOutputAction, on);
    m_projection->toggleLive(on);
    if (on)
        m_projection->showWindow();
    else
        m_projection->hideWindow();
    updateDisplayStatus();
}

void MainWindow::applyLiveSlide(int index)
{
    m_library->presentationTab()->markLive(-1);
    const QList<Slide> slides = m_live->slides();
    if (index < 0 || index >= slides.size())
        return;
    m_projection->setLiveSlide(slides.at(index), m_liveBackground);
    const QString label = displayText(slides.at(index).label);
    setStatusText(QStringLiteral("Live: %1 - %2").arg(displayText(m_live->itemTitle()), label));
    updateStageDisplay();
}

void MainWindow::goLive(int slideIndex)
{
    const QList<Slide> slides = m_preview->slides();
    const bool wasActive = isLiveOutputActive();
    if (!wasActive) {
        // Activate quietly; the window is shown below once the slide is in place (no flash of black).
        QSignalBlocker b(m_liveButton);
        m_liveButton->setChecked(true);
        syncLiveAction(m_liveOutputAction, true);
        m_projection->toggleLive(true);
    }
    if (m_preview->hasDocument()) {
        m_liveMediaTimer.stop();
        m_projection->stopMedia();
        // Presentation document: the live pane shows the same page and the projector renders it for its monitor.
        const PresentationService::Document doc = m_preview->document();
        const int page = std::clamp(slideIndex >= 0 ? slideIndex : m_preview->documentPage(), 0, doc.pageCount - 1);
        m_loadingLive = true;
        m_live->loadDocument(doc, page);
        m_loadingLive = false;
        applyLiveDocumentPage(page);
    } else if (m_preview->hasMedia()) {
        const MediaItem med = m_preview->media();
        m_loadingLive = true;
        m_live->loadMedia(med);
        m_loadingLive = false;
        m_liveBackground = med.filePath;
        m_preview->setPlaying(false);
        const bool loop = m_preview->isLooping();
        m_live->setLooping(loop);
        m_projection->playMedia(med.filePath, loop, true);
        m_live->setPlaying(true);
        m_liveMediaTimer.start(100);
        setStatusText(QStringLiteral("Live Media: %1").arg(displayText(med.name)));
    } else if (!slides.isEmpty()) {
        m_liveMediaTimer.stop();
        m_projection->stopMedia();
        PresentationItem pres;
        pres.title = m_preview->itemTitle();
        pres.subtitle = m_preview->itemSubtitle();
        pres.slides = slides;
        const int idx = std::clamp(slideIndex >= 0 ? slideIndex : m_preview->currentSlideIndex(), 0,
                                   static_cast<int>(slides.size()) - 1);
        m_liveBackground = m_preview->canvas()->background();
        m_loadingLive = true;
        m_live->loadItem(pres, m_liveBackground, idx);
        m_loadingLive = false;
        m_live->canvas()->setBlack(m_blackButton->isChecked());
        m_live->canvas()->setTextCleared(m_clearButton->isChecked());
        applyLiveSlide(idx);
    } else {
        m_liveMediaTimer.stop();
        m_projection->stopMedia();
        // No document and no slides in preview:
        // Update live background to match Preview, even when no song is active.
        m_liveBackground = m_preview->canvas()->background();
        m_live->canvas()->setBackground(m_liveBackground);
        const auto liveSlides = m_live->slides();
        const int liveIdx = m_live->currentSlideIndex();
        if (!liveSlides.isEmpty() && liveIdx >= 0 && liveIdx < liveSlides.size()) {
            m_projection->setLiveSlide(liveSlides.at(liveIdx), m_liveBackground);
        } else {
            m_projection->setBackground(m_liveBackground);
        }
        setStatusText(QStringLiteral("Live Background: %1").arg(
            m_liveBackground.startsWith(QLatin1Char('#')) ? m_liveBackground : QFileInfo(m_liveBackground).fileName()));
    }
    if (m_liveButton->isChecked() && !m_projection->isWindowShown())
        m_projection->showWindow();
    updateDisplayStatus();
    updateStageDisplay();
}

void MainWindow::toggleBlack() { m_blackButton->click(); }
void MainWindow::toggleClear() { m_clearButton->click(); }
void MainWindow::toggleLogo() { m_logoButton->click(); }
void MainWindow::toggleLiveOutput() { m_liveButton->click(); }

int MainWindow::liveCount() const
{
    return m_live->hasDocument() ? m_live->document().pageCount : static_cast<int>(m_live->slides().size());
}

int MainWindow::liveCurrent() const
{
    return m_live->hasDocument() ? m_live->documentPage() : m_live->currentSlideIndex();
}

void MainWindow::navigateLive(int target)
{
    const int n = liveCount();
    if (n == 0)
        return;
    const int t = std::clamp(target, 0, n - 1);
    if (m_live->hasDocument())
        m_live->setDocumentPage(t);   // documentPageChanged -> projector
    else
        m_live->setCurrentSlideIndex(t);
}

void MainWindow::liveNext() { navigateLive(liveCurrent() + 1); }
void MainWindow::livePrevious() { navigateLive(liveCurrent() - 1); }
void MainWindow::liveFirst() { navigateLive(0); }
void MainWindow::liveLast() { navigateLive(liveCount() - 1); }

bool MainWindow::handleProjectionKey(QKeyEvent* e)
{
    QWidget* f = QApplication::focusWidget();
    const bool typing = qobject_cast<QLineEdit*>(f) || qobject_cast<QTextEdit*>(f) || qobject_cast<QPlainTextEdit*>(f) ||
                        qobject_cast<QAbstractSpinBox*>(f) ||
                        (qobject_cast<QComboBox*>(f) && qobject_cast<QComboBox*>(f)->isEditable());
    if (typing)
        return false;
    // Arrow/Home/End keep driving list, tree and combo-box navigation when one of them has the focus.
    const bool navigatingView = qobject_cast<QAbstractItemView*>(f) || qobject_cast<QComboBox*>(f);

    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const int key = e->key();
    if (mods == Qt::ControlModifier) {
        if (key == Qt::Key_B) { toggleBlack(); return true; }
        if (key == Qt::Key_C) { toggleClear(); return true; }
        return false;
    }
    if (mods != Qt::NoModifier)
        return false;
    switch (key) {
    case Qt::Key_F5: goLive(); return true;
    case Qt::Key_F6:
    case Qt::Key_Period: toggleBlack(); return true;
    case Qt::Key_F7: toggleClear(); return true;
    case Qt::Key_F8: toggleLogo(); return true;
    case Qt::Key_Space:
    case Qt::Key_PageDown: liveNext(); return true;
    case Qt::Key_PageUp: livePrevious(); return true;
    case Qt::Key_Right: liveNext(); return true;
    case Qt::Key_Left: livePrevious(); return true;
    case Qt::Key_Down: if (navigatingView) return false; liveNext(); return true;
    case Qt::Key_Up: if (navigatingView) return false; livePrevious(); return true;
    case Qt::Key_Home: if (navigatingView) return false; liveFirst(); return true;
    case Qt::Key_End: if (navigatingView) return false; liveLast(); return true;
    default: return false;
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e)
{
    if (e->type() == QEvent::KeyPress && QApplication::activeWindow() == this && !QApplication::activePopupWidget() &&
        !QApplication::activeModalWidget() && m_workStack->currentIndex() == 0) {
        if (handleProjectionKey(static_cast<QKeyEvent*>(e)))
            return true;
    }
    return QMainWindow::eventFilter(watched, e);
}

// ---------------------------------------------------------------------------------------
// Presentation documents (Milestone 5)
// ---------------------------------------------------------------------------------------

void MainWindow::setupPresentations()
{
    m_data.cleanupOrphanedData();

    PresentationTab* tab = m_library->presentationTab();
    connect(tab, &PresentationTab::slideStaged, this, [this](const PresentationService::Document& d, int page) {
        showDocumentInPreview(d, page);
    });
    connect(m_presentations, &PresentationService::opened, this, [this](const PresentationService::Document& doc) {
        if (m_library->currentTab() == 3) {
            PresentationTab* tab = m_library->presentationTab();
            if (tab->slideGrid()->currentRow() < 0 && tab->slideGrid()->count() > 0)
                tab->slideGrid()->setCurrentRow(0);
            const int page = std::max(0, tab->slideGrid()->currentRow());
            showDocumentInPreview(doc, page);
        }
    });
    connect(tab, &PresentationTab::slideActivated, this, [this](const PresentationService::Document& d, int page) {
        showDocumentInPreview(d, page);
        goLive(page);
    });
    connect(tab, &PresentationTab::addToScheduleRequested, this, &MainWindow::addDocumentToSchedule);
    connect(tab, &PresentationTab::messageRequested, this, [this](const QString& title, const QString& text, bool isError) {
        setStatusText(text.section(QLatin1Char('\n'), 0, 0));
        if (m_suppressDialogs)
            return;
        if (isError)
            QMessageBox::warning(this, title, text);
        else
            QMessageBox::information(this, title, text);
    });
    // Preview pane navigation (its Prev/Next buttons) keeps the library grid and the status line in step.
    connect(m_preview, &SlidePaneView::documentPageChanged, this, [this, tab](int page) {
        tab->setSelectedPage(page);
        setStatusText(QStringLiteral("Preview Presentation: %1 - Slide %2").arg(displayText(m_preview->document().title)).arg(page + 1));
    });
    // Live pane navigation drives the projector.
    connect(m_live, &SlidePaneView::documentPageChanged, this, [this](int page) {
        if (!m_loadingLive)
            applyLiveDocumentPage(page);
    });
    // Documents requested for a schedule item arrive here.
    connect(m_presentations, &PresentationService::opened, this, [this](const PresentationService::Document& d) {
        if (!m_pendingDoc.active || QFileInfo(d.filePath) != QFileInfo(m_pendingDoc.path))
            return;
        const PendingDoc p = m_pendingDoc;
        m_pendingDoc = PendingDoc();
        showDocumentInPreview(d, p.page, PreviewSource::Schedule);
        if (p.goLive)
            goLive(p.page);
    });
    connect(m_presentations, &PresentationService::failed, this, [this](const QString& path, const QString& message) {
        if (!m_pendingDoc.active || QFileInfo(path) != QFileInfo(m_pendingDoc.path))
            return;
        m_pendingDoc = PendingDoc();
        m_preview->showDocumentError(QFileInfo(path).completeBaseName(), message);
        setStatusText(QStringLiteral("Presentation unavailable: ") + message.section(QLatin1Char('\n'), 0, 0));
    });
}

void MainWindow::showDocumentInPreview(const PresentationService::Document& doc, int page, PreviewSource source)
{
    m_previewSource = source;
    m_schedule->setPaneActive(source == PreviewSource::Schedule);
    songsTab()->setPaneActive(false);
    m_preview->loadDocument(doc, page);
    m_library->presentationTab()->setSelectedPage(m_preview->documentPage());
    setStatusText(QStringLiteral("Preview Presentation: %1 - Slide %2").arg(displayText(doc.title)).arg(m_preview->documentPage() + 1));
}

void MainWindow::applyLiveDocumentPage(int page)
{
    const PresentationService::Document& doc = m_live->document();
    if (!doc.isValid())
        return;
    m_projection->setLivePresentation(doc.pdfPath, page);   // first call enters presentation mode, later ones navigate
    m_library->presentationTab()->markLive(page);
    setStatusText(QStringLiteral("Live Presentation: %1 - Slide %2").arg(displayText(doc.title)).arg(page + 1));
}

PresentationItem MainWindow::documentToScheduleItem(const PresentationService::Document& doc)
{
    PresentationItem item;
    item.title = doc.title;
    item.subtitle = QStringLiteral("%1 slides").arg(doc.pageCount);
    item.category = ItemCategory::Presentation;
    for (int i = 0; i < doc.pageCount; ++i) {
        Slide s;
        s.label = QStringLiteral("Slide %1").arg(i + 1);
        s.content = QString::number(i + 1);     // 1-based page
        s.title = doc.title;
        s.subtitle = doc.filePath;               // the source file (the schedule JSON format is unchanged)
        s.type = SlideType::General;
        item.slides.append(s);
    }
    return item;
}

void MainWindow::addDocumentToSchedule(const PresentationService::Document& doc)
{
    if (!doc.isValid())
        return;
    m_scheduleService->addToSchedule(documentToScheduleItem(doc));
    setStatusText(QStringLiteral("Added to Schedule: %1").arg(displayText(doc.title)));
}

void MainWindow::openScheduleDocument(const ScheduleItem& item)
{
    const QList<Slide>& slides = item.presentation.slides;
    const QString path = slides.isEmpty() ? QString() : slides.first().subtitle;
    if (path.isEmpty()) {
        m_preview->showDocumentError(displayText(item.title), QStringLiteral("This schedule item does not refer to a presentation file."));
        return;
    }
    m_pendingDoc.path = path;
    m_pendingDoc.page = 0;
    m_pendingDoc.goLive = false;
    m_pendingDoc.active = true;
    m_preview->showDocumentError(displayText(item.title), QStringLiteral("Loading presentation..."));
    setStatusText(QStringLiteral("Preview: %1").arg(displayText(item.title)));
    m_presentations->open(path);
}

void MainWindow::goLiveAfterSelect()
{
    if (m_pendingDoc.active)
        m_pendingDoc.goLive = true;   // the document is still opening: go live as soon as it is in Preview
    else
        goLive();
}

void MainWindow::setupMedia()
{
    MediaTab* tab = m_library->mediaTab();
    connect(tab, &MediaTab::mediaSelected, this, [this](const MediaItem& m) {
        showMediaInPreview(m);
    });
    connect(tab, &MediaTab::mediaActivated, this, [this](const MediaItem& m) {
        showMediaInPreview(m);
        goLive();
    });
    connect(tab, &MediaTab::mediaCleared, this, [this] {
        if (m_library->currentTab() == 2) {
            m_previewSource = PreviewSource::Media;
            m_activeMedia = MediaItem();
            m_schedule->setPaneActive(false);
            songsTab()->setPaneActive(false);
            m_preview->loadEmptyMedia();
            setStatusText(QStringLiteral("Preview: Media"));
        }
    });
}

void MainWindow::showMediaInPreview(const MediaItem& media)
{
    m_previewSource = PreviewSource::Media;
    m_activeMedia = media;
    m_schedule->setPaneActive(false);
    songsTab()->setPaneActive(false);
    m_preview->loadMedia(media);
    setStatusText(QStringLiteral("Preview Media: %1").arg(displayText(media.name)));
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (m_sizesInitialised)
        return;
    m_sizesInitialised = true;
    // Initial proportions: left column 340px, preview/live share the rest equally.
    const int total = m_mainSplitter->width() - 2 * m_mainSplitter->handleWidth();
    const int rest = qMax(520, total - 340);
    m_mainSplitter->setSizes({340, rest / 2, rest - rest / 2});
}

} // namespace Lux
