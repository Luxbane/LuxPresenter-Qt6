#pragma once

#include "Models/AppSettings.h"
#include "Models/MediaItem.h"
#include "Models/PresentationItem.h"
#include "Models/Song.h"
#include "Services/BibleLibrary.h"
#include "Services/PresentationService.h"
#include "Services/ScheduleService.h"

#include <QMainWindow>
#include <QTimer>

class QActionGroup;
class QKeyEvent;
class QLabel;
class QMenu;
class QSplitter;
class QStackedWidget;

namespace Lux {
class AdBlockService;

class ClassicButton;
class DataService;
class LibraryView;
class OnlineBrowserView;
class ProjectionService;
class ScheduleView;
class SlidePaneView;
class SongsTab;
class ScripturesTab;
class MessageAlertWindow;
class UpdateService;
class StageServer;
class StageWindow;

// Main application window (MainWindow.xaml + the Songs/Schedule parts of MainViewModel):
// menu bar, upper toolbar, work area (left: Schedule + Library, centre: Preview, right: Live;
// full-width Online page) and the status bar. Behaviour belonging to later milestones is
// present but disabled.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum class PreviewSource { None, SongList, Schedule, Presentation, Scripture, Media };

    explicit MainWindow(DataService& data, QWidget* parent = nullptr);
    ~MainWindow() override;

    // --- state ---
    bool isOnlineMode() const { return m_onlineMode; }
    int lastLibraryTab() const { return m_lastLibraryTab; }
    PreviewSource previewSource() const { return m_previewSource; }
    QString statusText() const;
    QString displayStatusText() const;
    void setStatusText(const QString& text);
    QString activeGlobalBackground() const;

    // --- parts (used by tests) ---
    LibraryView* library() const { return m_library; }
    SongsTab* songsTab() const;
    ScheduleView* schedule() const { return m_schedule; }
    ScheduleService& scheduleService() { return *m_scheduleService; }
    SlidePaneView* previewPane() const { return m_preview; }
    SlidePaneView* livePane() const { return m_live; }
    StageServer* stageServer() const { return m_stageServer; }
    StageWindow* stageWindow() const { return m_stageWindow; }
    OnlineBrowserView* online() const { return m_online; }
    AdBlockService* adBlock() const { return m_adBlock; }
    QStackedWidget* workStack() const { return m_workStack; }
    QSplitter* mainSplitter() const { return m_mainSplitter; }
    QSplitter* leftSplitter() const { return m_leftSplitter; }
    QWidget* toolbar() const { return m_toolbar; }
    ClassicButton* globeButton() const { return m_globe; }
    ClassicButton* themeButton() const { return m_themeButton; }
    ClassicButton* donateButton() const { return m_donate; }
    ClassicButton* newSongButton() const { return m_newSongButton; }
    ClassicButton* editSongButton() const { return m_editSongButton; }
    QList<ClassicButton*> toolbarButtons() const { return m_toolbarButtons; }
    QAction* themeAction() const { return m_themeAction; }
    QMenu* menu(const QString& title) const;
    QAction* action(const QString& menuTitle, const QString& text) const;

    // --- projection (Milestone 4) ---
    ProjectionService* projection() const { return m_projection; }
    PresentationService* presentations() const { return m_presentations; }
    ClassicButton* goLiveButton() const { return m_goLiveButton; }
    ClassicButton* logoButton() const { return m_logoButton; }
    ClassicButton* blackButton() const { return m_blackButton; }
    ClassicButton* clearButton() const { return m_clearButton; }
    ClassicButton* liveButton() const { return m_liveButton; }
    ClassicButton* alertButton() const { return m_alertButton; }
    QMenu* displayMenu() const { return m_displayMenu; }
    bool isLiveOutputActive() const;
    // PreviewKeyDown of the reference: returns true when the key was a projection shortcut.
    bool handleProjectionKey(QKeyEvent* e);

    // --- song / schedule operations (the interactive slots below wrap these) ---
    // Stores an edited/new song, refreshes the list, the preview and (when `linked`) matching
    // schedule items. `original` is null for a new song.
    void applyEditedSong(const Song* original, const Song& edited, bool linked);
    bool deleteSongById(const QString& id, bool askConfirmation);
    void addSongToSchedule(const QString& songId);
    void addPreviewToSchedule(); // the "+" button of the schedule
    // Options: validates, saves and applies the settings everywhere (theme, projector, panes, preview).
    void applySettings(const AppSettings& settings);
    // Background: makes ackground (colour or image/video file) the active one. It is shown in Preview and used for
    // everything staged afterwards; the projector / Live pane only change when updateLive is set.
    void setActiveBackground(const QString& background, bool updateLive);
    void openBackgroundDialog();
    // Bible: stages the passage in Preview (and goes live when asked); the schedule keeps it as a Scripture item.
    void showPassage(const BiblePassage& passage, bool goLiveNow = false);
    void addPassageToSchedule(const BiblePassage& passage);
    ScripturesTab* scripturesTab() const;
    // Presentation documents (M5): staged in Preview, shown live page by page, kept in the schedule as an item of
    // category Presentation whose slides record the source file (Slide.subtitle) and page number (Slide.content).
    void showDocumentInPreview(const PresentationService::Document& doc, int page, PreviewSource source = PreviewSource::Presentation);
    void showMediaInPreview(const MediaItem& media);
    void addDocumentToSchedule(const PresentationService::Document& doc);
    static PresentationItem documentToScheduleItem(const PresentationService::Document& doc);
    bool newScheduleNoPrompt();
    bool openScheduleFile(const QString& path, QString* error = nullptr);
    bool saveScheduleFile(const QString& path, QString* error = nullptr);
    bool promptSaveIfModified();

signals:
    void settingsApplied(const Lux::AppSettings& settings);
public slots:
    // Projection
    void goLive(int slideIndex = -1);   // Preview -> Live -> projector (slideIndex -1: preview's current slide)
    void toggleBlack();
    void toggleClear();
    void toggleLogo();
    void toggleLiveOutput();
    void liveNext();
    void livePrevious();
    void liveFirst();
    void liveLast();
    void selectOutputDisplay(int index);   // -1 = automatic; the choice is remembered (settings.outputDisplay)
    void openOptions();                    // File > Options...
    void setDialogsSuppressed(bool on) { m_suppressDialogs = on; }

    void toggleThemeMode();
    void toggleOnline();
    void toggleStageWindow();
    void updateStageDisplay();
    void showAbout();
    void checkForUpdates();
    void openSongsFolder();
    void openKofi();

    // Songs
    void newSong();
    void editSong(const QString& songId);
    void editSelectedSong();
    void editActiveSong();
    void deleteSelectedSong();
    void changeSongsFolder();
    void loadSampleSongs();
    void exportSongsBackup();
    void importSongsBackup();

    // Schedule
    bool newSchedule();
    bool openSchedule();
    bool saveSchedule(bool saveAs = false);
    void clearSchedule();

protected:
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void setLiveOutput(bool on);
    void applyLiveSlide(int index);
    void navigateLive(int target);
    void rebuildDisplayMenu();
    void applyOutputDisplayByName(const QString& deviceName);
    void syncLiveAction(QAction* a, bool checked);
    void setupProjection();
    void setupPresentations();
    void setupMedia();
    void openScheduleDocument(const ScheduleItem& item);
    void goLiveAfterSelect();
    int liveCurrent() const;
    int liveCount() const;
    void applyLiveDocumentPage(int page);
    void buildMenus();
    void buildToolbar();
    void buildWorkArea();
    void buildStatusBar();
    void wireSongsAndSchedule();
    void onLibraryTabChanged(int index);
    void onSongSelectedForPreview(const QString& songId);
    void onScheduleItemSelected(int index);
    void showInPreview(const PresentationItem& item, const QString& background, int slideIndex = 0);
    void updateThemeUi();
    void updateDisplayStatus();
    void updateSongActions();
    QAction* plannedAction(QMenu* menu, const QString& text, int milestone, const QKeySequence& shortcut = {});
    ClassicButton* plannedButton(ClassicButton* b, const QString& tip, int milestone);

    DataService& m_data;
    ScheduleService* m_scheduleService = nullptr; // child QObject: outlives the child widgets that observe it
    QWidget* m_toolbar = nullptr;
    QStackedWidget* m_workStack = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_leftSplitter = nullptr;
    ScheduleView* m_schedule = nullptr;
    LibraryView* m_library = nullptr;
    SlidePaneView* m_preview = nullptr;
    SlidePaneView* m_live = nullptr;
    OnlineBrowserView* m_online = nullptr;
    AdBlockService* m_adBlock = nullptr;   // child QObject
    QLabel* m_statusLabel = nullptr;
    QLabel* m_displayLabel = nullptr;
    ClassicButton* m_globe = nullptr;
    ClassicButton* m_themeButton = nullptr;
    ClassicButton* m_donate = nullptr;
    ClassicButton* m_newSongButton = nullptr;
    ClassicButton* m_editSongButton = nullptr;
    QList<ClassicButton*> m_toolbarButtons;
    QAction* m_themeAction = nullptr;
    QAction* m_editSelectedAction = nullptr;
    QAction* m_deleteSelectedAction = nullptr;
    ProjectionService* m_projection = nullptr;
    PresentationService* m_presentations = nullptr;
    UpdateService* m_updateService = nullptr;
    StageServer* m_stageServer = nullptr;
    StageWindow* m_stageWindow = nullptr;
    QTimer m_stageTimer;
    struct PendingDoc { QString path; int page = 0; bool goLive = false; bool active = false; } m_pendingDoc;
    ClassicButton *m_goLiveButton = nullptr, *m_logoButton = nullptr, *m_blackButton = nullptr,
                  *m_clearButton = nullptr, *m_liveButton = nullptr, *m_alertButton = nullptr;
    MessageAlertWindow* m_alertWindow = nullptr;
    QAction *m_goLiveAction = nullptr, *m_blackAction = nullptr, *m_clearAction = nullptr, *m_logoAction = nullptr,
            *m_liveOutputAction = nullptr;
    QMenu* m_displayMenu = nullptr;
    QActionGroup* m_displayGroup = nullptr;
    QString m_liveBackground;
    bool m_suppressDialogs = false;   // tests: report messages through the status line only
    bool m_loadingLive = false;
    bool m_onlineMode = false;
    int m_lastLibraryTab = 0;
    bool m_sizesInitialised = false;
    PreviewSource m_previewSource = PreviewSource::None;
    QString m_activeSongId;
    BiblePassage m_activePassage;
    MediaItem m_activeMedia;
    QTimer m_liveMediaTimer;
};

} // namespace Lux
