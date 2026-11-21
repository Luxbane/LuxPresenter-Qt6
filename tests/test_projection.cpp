// Milestone 4 tests: display selection, projection state/output synchronisation, the transition
// pipeline (composer), deck-fade arithmetic and the rendered projector scene.
// Runs on the offscreen platform: no windows are shown and no video engine is started.

#include "FakeOutput.h"
#include "Services/DeckFade.h"
#include "Services/Displays.h"
#include "Services/ProjectionComposer.h"
#include "Services/ProjectionService.h"
#include "Views/Projection/ProjectionWindow.h"

#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace Lux;
using LuxTest::FakeOutput;

namespace {

ScreenInfo screen(int index, int w, int h, bool primary, int left = 0)
{
    ScreenInfo s;
    s.index = index;
    s.physicalLeft = left;
    s.physicalWidth = w;
    s.physicalHeight = h;
    s.isPrimary = primary;
    return s;
}

Slide slide(const QString& label, const QString& text)
{
    Slide s;
    s.label = label;
    s.content = text;
    return s;
}

ProjectionState baseState()
{
    ProjectionState st;
    st.activeBackground = QStringLiteral("#001833");
    st.transitionDurationMs = 500;
    st.songTransition = st.slideTransition = st.screenTransition = st.mediaTransition = QStringLiteral("Blend");
    return st;
}

struct Clocked
{
    qint64 now = 0;
    ProjectionComposer c;
    Clocked() { c.setClock([this] { return now; }); }
};

bool approx(double a, double b, double eps = 0.02) { return qAbs(a - b) <= eps; }

QString touch(const QTemporaryDir& d, const QString& name)
{
    const QString p = d.filePath(name);
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    return p;
}

} // namespace

class ProjectionTests : public QObject
{
    Q_OBJECT
private slots:
    // ---- display selection -------------------------------------------------------------
    void displaySelection()
    {
        const QList<ScreenInfo> one{screen(0, 1920, 1080, true)};
        const QList<ScreenInfo> two{screen(0, 1920, 1080, true), screen(1, 1280, 720, false, 1920)};
        const QList<ScreenInfo> secondFirst{screen(0, 800, 600, false), screen(1, 1920, 1080, true)};

        QVERIFY(DisplaySelection::choose({}, -1) == nullptr);
        QCOMPARE(DisplaySelection::choose(one, -1)->index, 0);          // single screen test mode
        QCOMPARE(DisplaySelection::choose(two, -1)->index, 1);          // automatic prefers the secondary display
        QCOMPARE(DisplaySelection::choose(secondFirst, -1)->index, 0);  // first non-primary, whatever its position
        QCOMPARE(DisplaySelection::choose(two, 0)->index, 0);           // explicit choice wins
        QCOMPARE(DisplaySelection::choose(two, 1)->index, 1);
        QCOMPARE(DisplaySelection::choose(two, 7)->index, 1);           // out of range -> automatic
        QVERIFY(!DisplaySelection::isProjectorConnected(one));
        QVERIFY(DisplaySelection::isProjectorConnected(two));

        QCOMPARE(DisplaySelection::statusText(1, 0), QStringLiteral("1 Display (Single Screen Test Mode)"));
        QCOMPARE(DisplaySelection::statusText(1, 1), QStringLiteral("1 Display (Single Screen Test Mode)"));
        QCOMPARE(DisplaySelection::statusText(2, 0), QStringLiteral("2 Displays Detected"));
        QCOMPARE(DisplaySelection::statusText(3, 2), QStringLiteral("3 Displays (Projector Output Active on Screen 2)"));
        QCOMPARE(two[1].displayName(), QStringLiteral("Secondary Monitor (1280x720)"));
        QCOMPARE(two[0].displayName(), QStringLiteral("Primary Monitor (1920x1080)"));
    }

    // ---- service <-> output synchronisation ----------------------------------------------
    void serviceKeepsOutputInSync()
    {
        const QList<ScreenInfo> two{screen(0, 1920, 1080, true), screen(1, 1280, 720, false, 1920)};
        ProjectionService svc;
        svc.setOutputFactory(LuxTest::fakeFactory(two));
        QSignalSpy vis(&svc, &ProjectionService::windowVisibilityChanged);

        QCOMPARE(svc.displays().size(), 2);
        QCOMPARE(svc.displayStatusText(), QStringLiteral("2 Displays Detected"));

        AppSettings settings;
        settings.marginTop = 3;
        settings.marginBottom = 4;
        settings.marginLeft = 5;
        settings.marginRight = 6;
        settings.songTransition = QStringLiteral("Cut");
        settings.transitionDurationMs = 321;
        settings.songTextAlignment = QStringLiteral("Left");
        svc.setSettings(settings);
        QCOMPARE(svc.state().songTransition, QStringLiteral("Cut"));
        QCOMPARE(svc.state().transitionDurationMs, 321);
        QCOMPARE(svc.state().songTextAlignment, QStringLiteral("Left"));

        // State set before the window exists is delivered with the first show (no flash of a stale scene).
        const Slide s = slide(QStringLiteral("Verse 1"), QStringLiteral("hello"));
        svc.setLiveSlide(s, QStringLiteral("#112233"));
        QVERIFY(svc.output() == nullptr);

        svc.showWindow();
        auto* fo = dynamic_cast<FakeOutput*>(svc.output());
        QVERIFY(fo);
        QVERIFY(fo->shown);
        QCOMPARE(fo->shownOn.index, 1);                 // automatic -> secondary monitor
        QCOMPARE(svc.activeScreenNumber(), 2);
        QCOMPARE(svc.displayStatusText(), QStringLiteral("2 Displays (Projector Output Active on Screen 2)"));
        QCOMPARE(fo->mTop, 3);
        QCOMPARE(fo->mRight, 6);
        QCOMPARE(fo->updates.size(), 1);
        QVERIFY(fo->updates.last().activeSlide.has_value());
        QCOMPARE(fo->updates.last().activeSlide->id, s.id);
        QCOMPARE(fo->updates.last().activeBackground, QStringLiteral("#112233"));
        QVERIFY(fo->updates.last().isLive && !fo->updates.last().isBlack);
        QCOMPARE(vis.size(), 1);
        QVERIFY(vis.last().first().toBool());

        svc.toggleBlack(true);
        QVERIFY(fo->updates.last().isBlack);
        svc.toggleClear(true);
        QVERIFY(fo->updates.last().isClear);
        svc.toggleLogo(true);
        QVERIFY(fo->updates.last().isLogo);
        svc.toggleBlack(false);
        svc.toggleClear(false);
        svc.toggleLogo(false);
        QVERIFY(!fo->updates.last().isBlack && !fo->updates.last().isClear && !fo->updates.last().isLogo);
        svc.setBackground(QStringLiteral("#ABCDEF"));
        QCOMPARE(fo->updates.last().activeBackground, QStringLiteral("#ABCDEF"));
        svc.toggleLive(false);
        QVERIFY(!fo->updates.last().isLive);
        svc.toggleLive(true);

        // Re-targeting the output moves it without hiding.
        const int showsBefore = fo->showCalls;
        svc.setSelectedMonitorIndex(0);
        QCOMPARE(fo->showCalls, showsBefore + 1);
        QCOMPARE(fo->shownOn.index, 0);
        QCOMPARE(svc.activeScreenNumber(), 1);
        QCOMPARE(vis.size(), 1);                        // still shown: no visibility change

        // Media (engine-level API)
        svc.playMedia(QStringLiteral("C:/x/video.mp4"), false, true);
        QVERIFY(fo->updates.last().isVideoMode && fo->updates.last().isVideoPlaying && !fo->updates.last().isVideoLooping);
        QCOMPARE(fo->updates.last().mediaFilePath, QStringLiteral("C:/x/video.mp4"));
        svc.setMediaPlaying(false);
        QVERIFY(!fo->updates.last().isVideoPlaying);
        svc.seekMedia(12.5);
        QCOMPARE(fo->lastSeek, 12.5);
        svc.stopMedia();
        QVERIFY(!fo->updates.last().isVideoMode);

        // Hidden: nothing is pushed, but the state is kept for the next show.
        svc.hideWindow();
        QVERIFY(!fo->shown);
        QCOMPARE(svc.activeScreenNumber(), 0);
        QCOMPARE(vis.size(), 2);
        QVERIFY(!vis.last().first().toBool());
        const int pushes = fo->updates.size();
        svc.toggleBlack(true);
        QCOMPARE(fo->updates.size(), pushes);
        QVERIFY(svc.state().isBlack);
        svc.toggleBlack(false);
        svc.showWindow();
        QCOMPARE(fo->updates.size(), pushes + 1);
        QCOMPARE(fo->updates.last().activeSlide->id, s.id);
    }

    void singleScreenUsesPrimary()
    {
        ProjectionService svc;
        svc.setOutputFactory(LuxTest::fakeFactory());
        QCOMPARE(svc.displayStatusText(), QStringLiteral("1 Display (Single Screen Test Mode)"));
        svc.showWindow();
        auto* fo = dynamic_cast<FakeOutput*>(svc.output());
        QVERIFY(fo && fo->shown);
        QVERIFY(fo->shownOn.isPrimary);
        QCOMPARE(svc.activeScreenNumber(), 1);
    }

    // ---- transition pipeline -------------------------------------------------------------
    void firstStateAppliesWithoutTransition()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("Verse 1"), QStringLiteral("first"));
        k.c.applyState(st);
        const int layer = k.c.activeSlideLayer();
        QVERIFY(approx(k.c.slideOpacity(layer, 0), 1.0));
        QVERIFY(approx(k.c.slideOpacity(1 - layer, 0), 0.0));
        QVERIFY(k.c.slideLayer(layer).valid);
        QCOMPARE(k.c.slideLayer(layer).text, QStringLiteral("FIRST"));   // capitalise-all default
        QCOMPARE(k.c.rootBackground(), QStringLiteral("#001833"));
        QVERIFY(!k.c.isAnimating(0));
    }

    void slideBlendCrossfadesTwoLayers()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        const int first = k.c.activeSlideLayer();

        k.now = 1000;
        st.activeSlide = slide(QStringLiteral("B"), QStringLiteral("beta"));
        k.c.applyState(st);
        const int second = k.c.activeSlideLayer();
        QVERIFY(second != first);
        QCOMPARE(k.c.slideLayer(second).text, QStringLiteral("BETA"));
        QCOMPARE(k.c.slideLayer(first).text, QStringLiteral("ALPHA")); // old text stays while it fades out
        QVERIFY(approx(k.c.slideOpacity(second, 1000), 0.0));
        QVERIFY(approx(k.c.slideOpacity(first, 1000), 1.0));
        QVERIFY(approx(k.c.slideOpacity(second, 1250), 0.5));
        QVERIFY(approx(k.c.slideOpacity(first, 1250), 0.5));
        QVERIFY(approx(k.c.slideOpacity(second, 1500), 1.0));
        QVERIFY(approx(k.c.slideOpacity(first, 1500), 0.0));
        QVERIFY(k.c.isAnimating(1100));
        QVERIFY(!k.c.isAnimating(1600));

        // Rapid advance: the incoming layer starts from where the outgoing one is, never above 1.
        k.now = 2000;
        st.activeSlide = slide(QStringLiteral("C"), QStringLiteral("gamma"));
        k.c.applyState(st);
        k.now = 2100;
        st.activeSlide = slide(QStringLiteral("D"), QStringLiteral("delta"));
        k.c.applyState(st);
        for (qint64 t = 2100; t <= 2700; t += 50) {
            QVERIFY(k.c.slideOpacity(0, t) >= -0.001 && k.c.slideOpacity(0, t) <= 1.001);
            QVERIFY(k.c.slideOpacity(1, t) >= -0.001 && k.c.slideOpacity(1, t) <= 1.001);
        }
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), 2700), 1.0));
    }

    void slideCutIsInstant()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.slideTransition = st.songTransition = QStringLiteral("Cut");
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        k.now = 100;
        st.activeSlide = slide(QStringLiteral("B"), QStringLiteral("beta"));
        k.c.applyState(st);
        const int l = k.c.activeSlideLayer();
        QVERIFY(approx(k.c.slideOpacity(l, 100), 1.0));
        QVERIFY(approx(k.c.slideOpacity(1 - l, 100), 0.0));
        QVERIFY(!k.c.isAnimating(100));
    }

    void unchangedSlideDoesNotRestartTransition()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        const int l = k.c.activeSlideLayer();
        const int serial = k.c.slideLayer(l).serial;
        k.now = 500;
        k.c.applyState(st);       // same slide, same options (e.g. a toggle elsewhere)
        QCOMPARE(k.c.activeSlideLayer(), l);
        QCOMPARE(k.c.slideLayer(l).serial, serial);
        QVERIFY(!k.c.isAnimating(500));
    }

    void blackOverlayFadesInAndOut()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        QVERIFY(!k.c.blackVisible());

        k.now = 2000;
        st.isBlack = true;
        k.c.applyState(st);
        QVERIFY(k.c.blackVisible());
        QVERIFY(approx(k.c.blackOpacity(2000), 0.0));
        QVERIFY(approx(k.c.blackOpacity(2250), 0.5));
        QVERIFY(approx(k.c.blackOpacity(2500), 1.0));

        k.now = 3000;
        st.isBlack = false;
        k.c.applyState(st);
        QVERIFY(approx(k.c.blackOpacity(3000), 1.0));
        QVERIFY(approx(k.c.blackOpacity(3250), 0.5));
        QVERIFY(approx(k.c.blackOpacity(3500), 0.0));
        QVERIFY(k.c.blackVisible());          // collapsed only after the fade finished
        k.c.advance(3600);
        QVERIFY(!k.c.blackVisible());
        QVERIFY(!k.c.isAnimating(3600));
    }

    void blackCutIsInstantAndOutputOffBlanks()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.screenTransition = QStringLiteral("Cut");
        st.isBlack = true;
        k.c.applyState(st);
        QVERIFY(approx(k.c.blackOpacity(0), 1.0));
        st.isBlack = false;
        k.now = 10;
        k.c.applyState(st);
        QVERIFY(approx(k.c.blackOpacity(10), 0.0));
        QVERIFY(!k.c.blackVisible());

        // "Live" switched off = black output
        st.isLive = false;
        k.now = 20;
        k.c.applyState(st);
        QVERIFY(approx(k.c.blackOpacity(20), 1.0));
    }

    void clearFadesTextOnlyAndRestores()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        const int l = k.c.activeSlideLayer();

        k.now = 1000;
        st.isClear = true;
        k.c.applyState(st);
        QVERIFY(approx(k.c.slideOpacity(l, 1250), 0.5));
        QVERIFY(approx(k.c.slideOpacity(l, 1500), 0.0));
        QVERIFY(!k.c.blackVisible());                          // background stays
        QCOMPARE(k.c.rootBackground(), QStringLiteral("#001833"));

        k.now = 2000;
        st.isClear = false;
        k.c.applyState(st);
        const int back = k.c.activeSlideLayer();
        QVERIFY(approx(k.c.slideOpacity(back, 2000), 0.0));
        QVERIFY(approx(k.c.slideOpacity(back, 2500), 1.0));
        QCOMPARE(k.c.slideLayer(back).text, QStringLiteral("ALPHA"));
    }

    void logoOverlayAndPrecedence()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        k.now = 1000;
        st.isLogo = true;
        k.c.applyState(st);
        QVERIFY(k.c.logoVisible());
        QVERIFY(approx(k.c.logoOpacity(1250), 0.5));
        QVERIFY(approx(k.c.logoOpacity(1500), 1.0));

        // Black wins over the logo (logo hides while black is shown).
        k.now = 2000;
        st.isBlack = true;
        k.c.applyState(st);
        QVERIFY(approx(k.c.logoOpacity(2500), 0.0));
        QVERIFY(approx(k.c.blackOpacity(2500), 1.0));

        k.now = 3000;
        st.isBlack = false;
        st.isLogo = false;
        k.c.applyState(st);
        k.c.advance(4000);
        QVERIFY(!k.c.logoVisible() && !k.c.blackVisible());
    }

    void backgroundColourImageAndVideo()
    {
        QTemporaryDir dir;
        const QString img1 = touch(dir, QStringLiteral("a.png"));
        const QString img2 = touch(dir, QStringLiteral("b.jpg"));
        const QString vid1 = touch(dir, QStringLiteral("a.mp4"));
        const QString vid2 = touch(dir, QStringLiteral("b.mkv"));

        Clocked k;
        ProjectionState st = baseState();

        // colour: instant, no video commands
        st.activeBackground = QStringLiteral("#FF0000");
        k.c.applyState(st);
        QCOMPARE(k.c.rootBackground(), QStringLiteral("#FF0000"));
        QVERIFY(k.c.takeVideoCommands().isEmpty());

        // image (first image of a session is applied directly; the next one crossfades)
        k.now = 1000;
        st.activeBackground = img1;
        k.c.applyState(st);
        int a = -1;
        for (int i = 0; i < 2; ++i)
            if (k.c.bgImagePath(i) == img1)
                a = i;
        QVERIFY(a >= 0);
        k.now = 3000;
        st.activeBackground = img2;
        k.c.applyState(st);
        int b = 1 - a;
        QCOMPARE(k.c.bgImagePath(b), img2);
        QVERIFY(approx(k.c.bgImageOpacity(b, 3000), 0.0));
        QVERIFY(approx(k.c.bgImageOpacity(b, 3250), 0.5));
        QVERIFY(approx(k.c.bgImageOpacity(a, 3250), 0.5));
        QVERIFY(approx(k.c.bgImageOpacity(b, 3500), 1.0));
        k.c.advance(3600);
        QVERIFY(k.c.bgImagePath(a).isEmpty());   // outgoing image released

        // video: root becomes transparent (the video window shows through), commands carry the crossfade
        k.now = 5000;
        st.activeBackground = vid1;
        k.c.applyState(st);
        QVERIFY(k.c.videoShowsThrough());
        QList<VideoCommand> cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::PlayBackground);
        QCOMPARE(cmds[0].path, vid1);
        QCOMPARE(cmds[0].crossfadeMs, 500);          // blend between the image and the video

        k.now = 6000;
        st.activeBackground = vid2;
        k.c.applyState(st);
        cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QCOMPARE(cmds[0].path, vid2);
        QCOMPARE(cmds[0].crossfadeMs, 500);          // video -> video crossfade between decks

        // same background again: nothing to do
        k.now = 6500;
        k.c.applyState(st);
        QVERIFY(k.c.takeVideoCommands().isEmpty());

        // black pauses the video, un-black resumes it
        k.now = 7000;
        st.isBlack = true;
        k.c.applyState(st);
        cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::Pause);
        k.now = 8000;
        st.isBlack = false;
        k.c.applyState(st);
        cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::Resume);

        // back to a colour: video stops
        k.now = 9000;
        st.activeBackground = QStringLiteral("#123456");
        k.c.applyState(st);
        cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::Stop);
        QCOMPARE(k.c.rootBackground(), QStringLiteral("#123456"));
        QVERIFY(!k.c.videoShowsThrough());
    }

    void videoBackgroundOnFirstStateIsNotBlended()
    {
        QTemporaryDir dir;
        const QString vid = touch(dir, QStringLiteral("a.mp4"));
        Clocked k;
        ProjectionState st = baseState();
        st.activeBackground = vid;
        k.c.applyState(st);
        const QList<VideoCommand> cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QCOMPARE(cmds[0].crossfadeMs, 0);
    }

    void missingBackgroundFileFallsBack()
    {
        Clocked k;
        ProjectionState st = baseState();
        st.activeBackground = QStringLiteral("C:/definitely/not/here.png");
        k.c.applyState(st);
        QCOMPARE(k.c.rootBackground(), QStringLiteral("#001833"));
        QVERIFY(k.c.takeVideoCommands().isEmpty());
    }

    void resetStopsVideoAndRestartsFirstRender()
    {
        QTemporaryDir dir;
        const QString vid = touch(dir, QStringLiteral("a.mp4"));
        Clocked k;
        ProjectionState st = baseState();
        st.activeBackground = vid;
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        k.c.takeVideoCommands();
        k.c.reset();
        const QList<VideoCommand> cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::Stop);
        QVERIFY(!k.c.slideLayer(0).valid && !k.c.slideLayer(1).valid);
        // the next state after a re-show appears at once
        st.activeBackground = QStringLiteral("#001833");
        k.now = 100;
        k.c.applyState(st);
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), 100), 1.0));
    }

    void standaloneMediaVideoCommands()
    {
        QTemporaryDir dir;
        const QString vid = touch(dir, QStringLiteral("clip.mp4"));
        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);
        k.now = 1000;
        st.isVideoMode = true;
        st.mediaFilePath = vid;
        st.isVideoPlaying = true;
        st.isVideoLooping = false;
        k.c.applyState(st);
        QVERIFY(k.c.inVideoMode());
        QVERIFY(k.c.videoShowsThrough());
        QList<VideoCommand> cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::PlayMedia);
        QCOMPARE(cmds[0].crossfadeMs, 500);
        QVERIFY(cmds[0].playing && !cmds[0].looping);
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), 1500), 0.0)); // text fades out for the video

        k.now = 2000;
        st.isVideoPlaying = false;
        k.c.applyState(st);
        cmds = k.c.takeVideoCommands();
        QCOMPARE(cmds.size(), 1);
        QVERIFY(cmds[0].kind == VideoCommand::Kind::Pause);

        // Returning from standalone media video to slides: stops video and restores root background
        k.now = 3000;
        st.isVideoMode = false;
        st.mediaFilePath.clear();
        st.activeSlide = slide(QStringLiteral("B"), QStringLiteral("beta"));
        k.c.applyState(st);
        QVERIFY(!k.c.inVideoMode());
        QVERIFY(!k.c.videoShowsThrough());
        cmds = k.c.takeVideoCommands();
        QVERIFY(!cmds.isEmpty());
        QVERIFY(cmds.last().kind == VideoCommand::Kind::Stop);
    }

    void marginsFollowReferenceFormula()
    {
        ProjectionComposer c;
        QCOMPARE(c.margins().left, 100.0);   // composer defaults (no settings applied yet)
        c.setMargins(0, 0, 0, 0);
        QCOMPARE(c.margins().left, 50.0);
        QCOMPARE(c.margins().top, 10.0);
        QCOMPARE(c.margins().right, 50.0);
        QCOMPARE(c.margins().bottom, 30.0);
        QCOMPARE(c.contentWidth(), 1820.0);
        c.setMargins(10, 10, 10, 10);
        QCOMPARE(c.margins().left, 80.0);
        QCOMPARE(c.margins().top, 30.0);
        QCOMPARE(c.margins().bottom, 60.0);
        c.setMargins(-50, -50, -50, -50);    // clamped minimums
        QCOMPARE(c.margins().left, 10.0);
        QCOMPARE(c.margins().top, 5.0);
        QCOMPARE(c.margins().bottom, 10.0);
    }

    // ---- video deck crossfade arithmetic --------------------------------------------------
    void deckFadeSequence()
    {
        DeckFade f;
        QCOMPARE(f.active(), -1);
        QCOMPARE(f.begin(0, 0), 0);                 // first video: deck 0, immediately opaque
        QCOMPARE(f.alpha(0), 255);
        QCOMPARE(f.alpha(1), 0);
        QCOMPARE(f.tick(0), -1);
        QVERIFY(!f.fading());

        QCOMPARE(f.begin(500, 1000), 1);            // crossfade onto the idle deck
        QVERIFY(f.fading());
        QCOMPARE(f.alpha(0), 255);
        QCOMPARE(f.alpha(1), 0);
        QCOMPARE(f.tick(1000), -1);
        QCOMPARE(f.tick(1250), -1);
        QCOMPARE(f.alpha(1), 127);
        QCOMPARE(f.alpha(0), 255);                  // outgoing stays opaque underneath
        int prev = -1;
        for (qint64 t = 1000; t < 1500; t += 20) {  // monotonic ramp
            f.tick(t);
            QVERIFY(f.alpha(1) >= prev);
            prev = f.alpha(1);
        }
        QCOMPARE(f.tick(1500), 0);                  // deck 0 finished fading out: caller stops it
        QCOMPARE(f.alpha(1), 255);
        QCOMPARE(f.alpha(0), 0);
        QCOMPARE(f.active(), 1);
        QVERIFY(!f.fading());
        QCOMPARE(f.tick(1600), -1);

        QCOMPARE(f.begin(0, 2000), 0);              // cut: new deck opaque, old one reported for stopping
        QCOMPARE(f.alpha(0), 255);
        QCOMPARE(f.alpha(1), 0);
        QCOMPARE(f.tick(2000), 1);

        f.begin(400, 3000);
        f.stopAll();
        QCOMPARE(f.active(), -1);
        QCOMPARE(f.alpha(0) + f.alpha(1), 0);
        QVERIFY(!f.fading());
    }

    // ---- scaling to the target display ------------------------------------------------------
    // The projector scene must follow the size/aspect ratio of the output it is painted for, never that of the
    // Preview/Live panes: colours fill the whole target, text scales uniformly (no stretching) and stays centred.
    void sceneScalesToTargetResolution()
    {
        Clocked k;
        ProjectionWindow win(&k.c);
        ProjectionState st = baseState();
        st.activeBackground = QStringLiteral("#FF0000");
        st.songVerticalAlignment = QStringLiteral("Center");
        st.activeSlide = slide(QStringLiteral("Verse 1"), QStringLiteral("Amazing grace how sweet the sound that saved a wretch like me"));
        k.c.applyState(st);
        const auto m = k.c.margins();
        const double designCenterY = m.top + (1080 - m.top - m.bottom) / 2.0;

        struct Case { int w, h; };
        const QList<Case> cases{{960, 540}, {1920, 1080}, {1600, 1000}, {1280, 720}, {1920, 800}, {1024, 768}, {3840, 2160}};
        QList<double> designWidths, designHeights;
        for (const Case& c : cases) {
            QImage img(c.w, c.h, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            win.paintScene(&p, QRectF(0, 0, c.w, c.h), 0);
            p.end();
            const QString tag = QStringLiteral("%1x%2").arg(c.w).arg(c.h);
            for (QPoint pt : {QPoint(0, 0), QPoint(c.w - 1, 0), QPoint(0, c.h - 1), QPoint(c.w - 1, c.h - 1)})
                QVERIFY2(img.pixel(pt) == qRgb(255, 0, 0), qPrintable(tag + " corner not filled (letterbox?)"));
            int x0 = c.w, y0 = c.h, x1 = -1, y1 = -1;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    const QRgb px = img.pixel(x, y);
                    if (qRed(px) < 200 || qGreen(px) > 60) {
                        x0 = qMin(x0, x); x1 = qMax(x1, x); y0 = qMin(y0, y); y1 = qMax(y1, y);
                    }
                }
            QVERIFY2(x1 > x0 && y1 > y0, qPrintable(tag + " no text"));
            const double s = qMin(c.w / 1920.0, c.h / 1080.0);
            designWidths << (x1 - x0 + 1) / s;
            designHeights << (y1 - y0 + 1) / s;
            QVERIFY2(qAbs((x0 + x1) / 2.0 - c.w / 2.0) <= 0.01 * c.w + 2, qPrintable(tag + " not centred horizontally"));
            const double expectY = (c.h - 1080 * s) / 2 + designCenterY * s;
            QVERIFY2(qAbs((y0 + y1) / 2.0 - expectY) <= 30 * s + 2, qPrintable(tag + " vertical position"));
        }
        // Same design-space size at every resolution/aspect ratio => proportional scaling, no stretching.
        for (int i = 1; i < designWidths.size(); ++i) {
            QVERIFY2(qAbs(designWidths[i] / designWidths[0] - 1) < 0.03, qPrintable(QString::number(designWidths[i])));
            QVERIFY2(qAbs(designHeights[i] / designHeights[0] - 1) < 0.03, qPrintable(QString::number(designHeights[i])));
        }
    }

    void backgroundImageFitsTargetWithoutDistortion()
    {
        QTemporaryDir dir;
        QImage src(400, 400, QImage::Format_RGB32);
        src.fill(Qt::black);
        QPainter sp(&src);
        sp.fillRect(150, 150, 100, 100, Qt::white);      // centred square marker
        sp.end();
        const QString path = dir.filePath(QStringLiteral("bg.png"));
        QVERIFY(src.save(path));

        Clocked k;
        ProjectionWindow win(&k.c);
        ProjectionState st = baseState();
        st.activeBackground = path;
        k.c.applyState(st);
        for (QSize sz : {QSize(1600, 1000), QSize(1920, 800), QSize(1280, 720), QSize(1024, 768)}) {
            QImage img(sz, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            win.paintScene(&p, QRectF(0, 0, sz.width(), sz.height()), 0);
            p.end();
            for (QPoint pt : {QPoint(0, 0), QPoint(sz.width() - 1, sz.height() - 1)})
                QCOMPARE(qAlpha(img.pixel(pt)), 255);    // fully covered
            int x0 = sz.width(), y0 = sz.height(), x1 = -1, y1 = -1;
            for (int y = 0; y < sz.height(); ++y)
                for (int x = 0; x < sz.width(); ++x)
                    if (qRed(img.pixel(x, y)) > 200) {
                        x0 = qMin(x0, x); x1 = qMax(x1, x); y0 = qMin(y0, y); y1 = qMax(y1, y);
                    }
            const double kw = sz.width() / 400.0;
            const double kh = sz.height() / 400.0;
            const int w = x1 - x0 + 1, h = y1 - y0 + 1;
            QVERIFY2(qAbs(w - 100 * kw) <= 4, qPrintable(QString::number(w)));
            QVERIFY2(qAbs(h - 100 * kh) <= 4, qPrintable(QString::number(h)));
            QVERIFY(qAbs((x0 + x1) / 2.0 - sz.width() / 2.0) <= 2 && qAbs((y0 + y1) / 2.0 - sz.height() / 2.0) <= 2);
        }
    }

    void logoAndBlackFillWholeTarget()
    {
        Clocked k;
        ProjectionWindow win(&k.c);
        ProjectionState st = baseState();
        st.isLogo = true;
        k.c.applyState(st);
        QImage img(1600, 1000, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        win.paintScene(&p, QRectF(0, 0, 1600, 1000), 0);
        p.end();
        QCOMPARE(img.pixel(0, 0), qRgb(0x00, 0x18, 0x33));
        QCOMPARE(img.pixel(1599, 999), qRgb(0x00, 0x18, 0x33));
        QCOMPARE(img.pixel(800, 5), qRgb(0x00, 0x18, 0x33));
    }
    // ---- slide-change pipeline (stutter regression) -------------------------------------------
    // The transition must not do text layout / outline stroking per frame, and the preparation must happen
    // before the fade starts so that no frame of the transition waits for it.
    void preparationHappensBeforeTheFadeStarts()
    {
        Clocked k;
        QList<int> prepared;
        k.c.setLayerPreparer([&](int layer) {
            prepared << layer;
            k.now += 80;          // the expensive work takes a while
        });
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("alpha"));
        k.c.applyState(st);       // first state: prepared, applied at once
        QCOMPARE(prepared.size(), 1);
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), k.now), 1.0));

        k.now = 1000;
        st.activeSlide = slide(QStringLiteral("B"), QStringLiteral("beta"));
        k.c.applyState(st);
        QCOMPARE(prepared.size(), 2);
        QCOMPARE(prepared.last(), k.c.activeSlideLayer());
        // clock is now 1080: the fade starts there, so the first frame is still the old slide (opacity 0 / 1)
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), k.now), 0.0));
        QVERIFY(approx(k.c.slideOpacity(1 - k.c.activeSlideLayer(), k.now), 1.0));
        QVERIFY(approx(k.c.slideOpacity(k.c.activeSlideLayer(), k.now + 250), 0.5));

        // an unchanged state prepares nothing
        k.c.applyState(st);
        QCOMPARE(prepared.size(), 2);
    }

    void textLayersAreRasterisedOncePerSlideNotPerFrame()
    {
        Clocked k;
        ProjectionWindow win(&k.c);
        win.resize(480, 270);
        k.c.setLayerPreparer([&](int layer) { win.prepareLayer(layer); });
        ProjectionState st = baseState();
        st.activeBackground = QStringLiteral("#001833");
        st.activeSlide = slide(QStringLiteral("A"), QStringLiteral("Amazing grace how sweet the sound"));
        k.c.applyState(st);
        QCOMPARE(win.cacheRenderCount(), 1);               // prepared up front

        QImage img(480, 270, QImage::Format_ARGB32_Premultiplied);
        auto frame = [&](qint64 t) {
            img.fill(Qt::transparent);
            QPainter p(&img);
            win.paintScene(&p, QRectF(0, 0, 480, 270), t);
        };
        for (qint64 t = 0; t < 300; t += 16)
            frame(t);
        QCOMPARE(win.cacheRenderCount(), 1);               // painting alone never re-rasterises

        k.now = 1000;
        st.activeSlide = slide(QStringLiteral("B"), QStringLiteral("That saved a wretch like me"));
        k.c.applyState(st);
        QCOMPARE(win.cacheRenderCount(), 2);               // only the new layer was rendered
        for (qint64 t = 1000; t <= 1520; t += 16)          // the whole crossfade: two layers, 33 frames
            frame(t);
        QCOMPARE(win.cacheRenderCount(), 2);

        // repeated changes alternate the two layers: exactly one rasterisation per change
        for (int i = 0; i < 6; ++i) {
            k.now = 2000 + i * 1000;
            st.activeSlide = slide(QStringLiteral("S%1").arg(i), QStringLiteral("Stanza number %1 of the song").arg(i));
            k.c.applyState(st);
            frame(k.now + 100);
            frame(k.now + 600);
        }
        QCOMPARE(win.cacheRenderCount(), 8);

        // a different output size re-rasterises (target-aware), once
        win.resize(640, 360);
        QImage big(640, 360, QImage::Format_ARGB32_Premultiplied);
        for (qint64 t = 9000; t < 9100; t += 16) {
            big.fill(Qt::transparent);
            QPainter p(&big);
            win.paintScene(&p, QRectF(0, 0, 640, 360), t);
        }
        QVERIFY(win.cacheRenderCount() <= 10);
    }

    void cachedLayerMatchesDirectPaintingPixelForPixel()
    {
        for (qreal dpr : {1.0, 1.25, 1.5}) {
            Clocked k;
            ProjectionWindow win(&k.c);
            ProjectionState st = baseState();
            st.activeBackground = QStringLiteral("#001833");
            st.songVerticalAlignment = QStringLiteral("Bottom");
            st.activeSlide = slide(QStringLiteral("Verse 1"), QStringLiteral("Amazing grace how sweet the sound\nThat saved a wretch like me"));
            k.c.applyState(st);
            const QSize px(int(1024 * dpr / 1.0), int(768 * dpr / 1.0));   // 4:3 output: offsets are fractional
            auto render = [&](bool cached) {
                win.setLayerCacheEnabled(cached);
                QImage img(px, QImage::Format_ARGB32_Premultiplied);
                img.setDevicePixelRatio(dpr);
                img.fill(Qt::transparent);
                QPainter p(&img);
                win.paintScene(&p, QRectF(0, 0, px.width() / dpr, px.height() / dpr), 0);
                p.end();
                return img;
            };
            const QImage a = render(true);
            const QImage b = render(false);
            QCOMPARE(a.size(), b.size());
            int maxDiff = 0;
            qint64 differing = 0;
            for (int y = 0; y < a.height(); ++y) {
                const QRgb* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const QRgb* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    if (ra[x] == rb[x])
                        continue;
                    ++differing;
                    maxDiff = std::max({maxDiff, qAbs(qRed(ra[x]) - qRed(rb[x])), qAbs(qGreen(ra[x]) - qGreen(rb[x])),
                                        qAbs(qBlue(ra[x]) - qBlue(rb[x]))});
                }
            }
            // anti-aliasing edge pixels may differ by float rounding of the sub-pixel offset (a few levels of 255)
            QVERIFY2(maxDiff <= 6, qPrintable(QStringLiteral("dpr %1: max channel diff %2 (%3 px)").arg(dpr).arg(maxDiff).arg(differing)));
        }
    }
    // ---- rendered projector scene ---------------------------------------------------------
    void sceneRendersState()
    {
        QTemporaryDir dir;
        const QString vid = touch(dir, QStringLiteral("v.mp4"));
        Clocked k;
        ProjectionWindow win(&k.c);
        auto render = [&](qint64 t) {
            QImage img(480, 270, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            win.paintScene(&p, QRectF(0, 0, 480, 270), t);
            p.end();
            return img;
        };
        auto isRed = [](QRgb c) { return qRed(c) > 240 && qGreen(c) < 15 && qBlue(c) < 15; };

        ProjectionState st = baseState();
        st.activeBackground = QStringLiteral("#FF0000");
        st.activeSlide = slide(QStringLiteral("Verse 1"), QStringLiteral("Amazing grace how sweet the sound"));
        k.c.applyState(st);
        QImage img = render(0);
        QVERIFY(isRed(img.pixel(5, 5)));
        bool textDrawn = false;                      // outlined white text somewhere in the middle band
        for (int y = 80; y < 190 && !textDrawn; y += 2)
            for (int x = 40; x < 440; x += 2)
                if (!isRed(img.pixel(x, y))) { textDrawn = true; break; }
        QVERIFY(textDrawn);
        const qreal fs = win.layerFontSize(k.c.activeSlideLayer());
        QVERIFY2(fs >= 34 && fs <= 13 * 6.5 + 1.0, qPrintable(QString::number(fs)));

        // black overlay half-way: red halves
        k.now = 1000;
        st.isBlack = true;
        k.c.applyState(st);
        img = render(1250);
        const int r = qRed(img.pixel(5, 5));
        QVERIFY2(r > 110 && r < 145, qPrintable(QString::number(r)));
        img = render(1500);
        QCOMPARE(img.pixel(5, 5), qRgb(0, 0, 0));

        // clear + un-black + logo
        k.now = 3000;
        st.isBlack = false;
        st.isLogo = true;
        k.c.applyState(st);
        img = render(3600);
        QCOMPARE(img.pixel(5, 5), qRgb(0x00, 0x18, 0x33));   // logo background colour
        bool gold = false;                                   // cross stroke
        for (int y = 40; y < 230 && !gold; y += 2)
            for (int x = 200; x < 280; x += 2) {
                const QRgb c = img.pixel(x, y);
                if (qRed(c) > 180 && qGreen(c) > 140 && qBlue(c) < 110) { gold = true; break; }
            }
        QVERIFY(gold);

        // background video: window stays transparent so the video window shows through
        Clocked k2;
        ProjectionWindow win2(&k2.c);
        ProjectionState vs = baseState();
        vs.activeBackground = vid;
        k2.c.applyState(vs);
        QImage vimg(480, 270, QImage::Format_ARGB32_Premultiplied);
        vimg.fill(Qt::transparent);
        QPainter vp(&vimg);
        win2.paintScene(&vp, QRectF(0, 0, 480, 270), 0);
        vp.end();
        QCOMPARE(qAlpha(vimg.pixel(5, 5)), 0);
    }

    void silentInstantTransitionBehindBlackScreen()
    {
        QTemporaryDir dir;
        const QString imgA = touch(dir, QStringLiteral("a.png"));
        const QString imgB = touch(dir, QStringLiteral("b.png"));

        Clocked k;
        ProjectionState st = baseState();
        st.activeSlide = slide(QStringLiteral("S1"), QStringLiteral("Song 1 lyrics"));
        st.activeBackground = imgA;
        st.transitionDurationMs = 500;
        k.c.applyState(st);

        // Background A is active
        QCOMPARE(k.c.currentBackground(), imgA);
        QVERIFY(!k.c.blackVisible());

        // 1. Black Screen engaged (Ctrl+B)
        k.now = 1000;
        st.isBlack = true;
        k.c.applyState(st);
        QVERIFY(k.c.blackVisible());

        // 2. Operator selects Song 2 with Background B and goes live while screen is blacked out
        k.now = 2000;
        st.activeSlide = slide(QStringLiteral("S2"), QStringLiteral("Song 2 lyrics"));
        st.activeBackground = imgB;
        k.c.applyState(st);

        // Behind the curtain: Background B must be applied immediately (0 ms cut), not skipped!
        QCOMPARE(k.c.currentBackground(), imgB);
        QCOMPARE(k.c.slideLayer(k.c.activeSlideLayer()).slide.label, QStringLiteral("S2"));

        // 3. Black Screen disengaged (Ctrl+B turned off)
        k.now = 3000;
        st.isBlack = false;
        k.c.applyState(st);

        // When black is removed, Background B is already fully present underneath without any stale Background A leak!
        QCOMPARE(k.c.currentBackground(), imgB);
        QCOMPARE(k.c.slideLayer(k.c.activeSlideLayer()).slide.label, QStringLiteral("S2"));
    }
};

QTEST_MAIN(ProjectionTests)
#include "test_projection.moc"
