#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "prism/analysis.hpp"
#include "prism/control.hpp"
#include "prism/frame_windows.hpp"
#include "prism/macro.hpp"
#include "prism/pathfinder.hpp"
#include "prism/timing.hpp"
#include "toy_sim.hpp"

using namespace prism;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fails;                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static void section(const char* name) { std::printf("[%s]\n", name); }

static Macro mk(std::vector<InputEvent> ev, uint8_t init = 0) {
    Macro m;
    m.events = std::move(ev);
    m.initialMask = init;
    m.normalize();
    return m;
}

// ---------------------------------------------------------------- macro
static void testMacro() {
    section("macro");
    Macro m = mk({{10, true, 0}, {10, true, 0}, {20, false, 0}, {25, false, 0}, {30, true, 1}});
    CHECK(m.events.size() == 3);  // duplicate press + stray release dropped
    CHECK(m.stateAt(9).mask() == 0);
    CHECK(m.stateAt(10).mask() == 1);
    CHECK(m.stateAt(19).mask() == 1);
    CHECK(m.stateAt(20).mask() == 0);
    CHECK(m.stateAt(30).mask() == 2);
    CHECK(m.pressCount() == 2 && m.pressCount(0) == 1 && m.pressCount(1) == 1);
    CHECK(m.lastFrame() == 30);

    // zero-length click is not representable
    Macro z = mk({{5, true, 0}, {5, false, 0}});
    CHECK(z.events.size() <= 1);

    // serialize / parse
    auto p = Macro::parse(m.serialize());
    CHECK(p.has_value() && *p == m);
    Macro init = mk({{3, false, 0}, {8, true, 0}}, 1);  // starts held, release at 3
    CHECK(init.events.size() == 2);
    auto pi = Macro::parse(init.serialize());
    CHECK(pi.has_value() && *pi == init && pi->initialMask == 1);
    CHECK(!Macro::parse("garbage").has_value());
    CHECK(!Macro::parse("PRISMMACRO 2\n").has_value());
    CHECK(!Macro::parse("PRISMMACRO 1\n5 D 0\n3 U 0\n").has_value());   // out of order
    CHECK(!Macro::parse("PRISMMACRO 1\n-5 D 0\n").has_value());         // negative
    CHECK(!Macro::parse("PRISMMACRO 1\n5 X 0\n").has_value());          // bad token
    CHECK(!Macro::parse("PRISMMACRO 1\n5 D 2\n").has_value());          // bad player
    CHECK(!Macro::parse("PRISMMACRO 1\n5 D 0\n6 D 0\n").has_value());   // non-alternating
    CHECK(!Macro::parse("PRISMMACRO 1\n5 D\n").has_value());            // truncated
    CHECK(Macro::parse("PRISMMACRO 1\n").has_value());                  // empty is valid

    // cursor matches stateAt
    MacroCursor cur(m);
    bool same = true;
    for (uint32_t f = 0; f < 40; ++f) same = same && cur.at(f).mask() == m.stateAt(f).mask();
    CHECK(same);
    MacroCursor ci(init);
    CHECK(ci.at(0).mask() == 1 && ci.at(3).mask() == 0 && ci.at(8).mask() == 1);
}

static void testEditor() {
    section("editor");
    MacroEditor ed(mk({{10, true, 0}, {20, false, 0}, {40, true, 0}, {50, false, 0}}));
    CHECK(!ed.canUndo());

    CHECK(ed.moveEvent(0, 12));
    CHECK(ed.macro().events[0].frame == 12);
    CHECK(!ed.moveEvent(0, 20));  // would collide with its release
    CHECK(!ed.moveEvent(1, 12));  // would cross the press
    CHECK(!ed.moveEvent(0, 12));  // no-op
    CHECK(!ed.moveEvent(99, 1));

    CHECK(ed.addEvent({60, true, 0}));
    CHECK(!ed.addEvent({61, true, 0}));  // already held, redundant
    CHECK(ed.addEvent({70, false, 0}));
    CHECK(ed.macro().pressCount() == 3);

    CHECK(ed.removeClick(4));  // the 60/70 click
    CHECK(ed.macro().pressCount() == 2);
    CHECK(!ed.removeClick(1));  // index 1 is a release
    CHECK(!ed.removeClick(99));

    CHECK(ed.undo());
    CHECK(ed.macro().pressCount() == 3);
    CHECK(ed.redo());
    CHECK(ed.macro().pressCount() == 2);
    while (ed.undo()) {}
    CHECK(ed.macro().events[0].frame == 10 && ed.macro().pressCount() == 2);
    CHECK(!ed.canUndo() && ed.canRedo());

    // new edit clears redo
    CHECK(ed.moveEvent(0, 11));
    CHECK(!ed.canRedo());

    // trim: hold across the start boundary gets a synthetic press at 0
    MacroEditor t(mk({{10, true, 0}, {30, false, 0}, {50, true, 0}, {70, false, 0}}));
    CHECK(t.trim(20, 60));
    // at frame 20 player is held -> press@0; release@10 (30-20); press@30 (50-20); held at end -> release@40
    CHECK(t.macro().events.size() == 4);
    CHECK(t.macro().events[0] == (InputEvent{0, true, 0}));
    CHECK(t.macro().events[1] == (InputEvent{10, false, 0}));
    CHECK(t.macro().events[2] == (InputEvent{30, true, 0}));
    CHECK(t.macro().events[3] == (InputEvent{40, false, 0}));
    CHECK(!t.trim(5, 5));

    // stitch
    MacroEditor a(mk({{5, true, 0}, {50, false, 0}}));
    Macro tail = mk({{2, true, 0}, {4, false, 0}});
    CHECK(a.stitch(tail, 100));
    CHECK(a.macro().events.size() == 4);
    CHECK(a.macro().events[2] == (InputEvent{102, true, 0}));
    // head still held at the stitch point gets released there
    MacroEditor h(mk({{5, true, 0}}));
    CHECK(h.stitch(tail, 20));
    CHECK(h.macro().events[1] == (InputEvent{20, false, 0}));
    CHECK(h.macro().events[2] == (InputEvent{22, true, 0}));
}

// -------------------------------------------------------- frame windows
struct FakeRunner : IRunner {
    std::vector<uint32_t> target;
    int early = 3, late = 5;
    int calls = 0;
    bool succeeds(const Macro& m) override {
        ++calls;
        size_t k = 0;
        for (const auto& e : m.events) {
            if (!e.down) continue;
            if (k >= target.size()) return false;
            const int d = static_cast<int>(e.frame) - static_cast<int>(target[k]);
            if (d < -early || d > late) return false;
            ++k;
        }
        return k == target.size();
    }
};

static void testWindows() {
    section("frame windows");
    Macro m = mk({{100, true, 0}, {120, false, 0}, {200, true, 0}, {203, false, 0}});
    FakeRunner r;
    r.target = {100, 200};
    WindowReport rep = computeWindows(m, r);
    CHECK(rep.baselineOk && !rep.cancelled);
    CHECK(rep.windows.size() == 4);
    CHECK(rep.windows[0].earlyBy == 3 && rep.windows[0].lateBy == 5 && rep.windows[0].width() == 9);
    // release@120 doesn't affect success in the fake runner, so it spans the whole radius
    // but is bounded by neighbours: press@100 below, press@200 above.
    CHECK(rep.windows[1].earlyBy == 19 && rep.windows[1].lateBy == 30);
    // release@203 is bounded below by press@200 (can go to 201) -> early 2
    CHECK(rep.windows[3].earlyBy == 2 && rep.windows[3].lateBy == 30);

    // failing baseline -> no windows
    FakeRunner bad;
    bad.target = {500};
    WindowReport br = computeWindows(m, bad);
    CHECK(!br.baselineOk && br.windows.empty());

    // cancel
    WindowOptions o;
    int polls = 0;
    o.cancel = [&] { return ++polls > 2; };
    WindowReport cr = computeWindows(m, r, o);
    CHECK(cr.cancelled && cr.windows.size() == 2);

    // radius cap + progress callback
    WindowOptions o2;
    o2.maxRadius = 2;
    size_t lastDone = 0;
    o2.progress = [&](size_t d, size_t) { lastDone = d; };
    WindowReport r2 = computeWindows(m, r, o2);
    CHECK(r2.windows[0].earlyBy == 2 && r2.windows[0].lateBy == 2);
    CHECK(lastDone == 4);

    // press at frame 0 can't move earlier than 0
    Macro z = mk({{1, true, 0}, {10, false, 0}});
    FakeRunner zr;
    zr.target = {1};
    zr.early = 10;
    WindowReport zrep = computeWindows(z, zr);
    CHECK(zrep.windows[0].earlyBy == 1);

    // export formats
    const std::string csv = windowsToCsv(rep);
    CHECK(csv.rfind("index,frame,type,player,early_by,late_by,width\n", 0) == 0);
    CHECK(csv.find("0,100,press,1,3,5,9") != std::string::npos);
    const std::string js = windowsToJson(rep);
    CHECK(js.find("\"baselineOk\":true") != std::string::npos && js.find("\"width\":9") != std::string::npos);

    // tightness + colors + buckets
    CHECK(tightness(1) == 1.f && tightness(0) == 1.f);
    CHECK(tightness(16) == 0.f && tightness(1000) == 0.f);
    CHECK(tightness(4) > tightness(8));
    CHECK(tightnessColor(0.f) == (Rgb{60, 220, 60}));
    CHECK(tightnessColor(1.f).r > 200 && tightnessColor(1.f).g < 80);
    CHECK(tightnessColor(-5.f) == tightnessColor(0.f) && tightnessColor(5.f) == tightnessColor(1.f));
    WindowReport cm;
    cm.windows = {{0, 5, true, 0, 0, 0}, {1, 55, true, 0, 8, 7}, {2, 56, false, 0, 0, 0}};
    auto buckets = consistencyBuckets(cm, 100, 4);
    CHECK(buckets.size() == 4 && buckets[0] == 1.f && buckets[1] == 0.f && buckets[2] == tightness(16));
    CHECK(consistencyBuckets(cm, 0, 4) == std::vector<float>(4, 0.f));
    CHECK(consistencyBuckets(cm, 100, 0).empty());
    cm.windows = {{0, 100, true, 0, 0, 0}};  // frame == levelFrames clamps into last bucket
    CHECK(consistencyBuckets(cm, 100, 4)[3] == 1.f);
}

// --------------------------------------------------------------- timing
static void testTiming() {
    section("timing");
    CHECK(observedPressGapBucket(0) == 0 && observedPressGapBucket(1) == 0);
    CHECK(observedPressGapBucket(2) == 1 && observedPressGapBucket(3) == 2);
    CHECK(observedPressGapBucket(4) == 3 && observedPressGapBucket(5) == 4);
    CHECK(observedPressGapBucket(6) == 5 && observedPressGapBucket(7) == 6);
    CHECK(observedPressGapBucket(8) == 6 && observedPressGapBucket(9) == 7);
    CHECK(observedPressGapBucket(10) == 7 && observedPressGapBucket(11) == 8);
    ClickTimingTracker clickTiming;
    clickTiming.recordPress(100, 0);
    clickTiming.recordPress(104, 0);
    clickTiming.recordPress(106, 1);
    CHECK(clickTiming.pressCount() == 3);
    CHECK(clickTiming.recent().size() == 3);
    CHECK(!clickTiming.recent()[0].hasGap && clickTiming.recent()[1].gapFrames == 4);
    CHECK(clickTiming.recent()[2].player == 1 && clickTiming.recent()[2].gapFrames == 2);
    CHECK(clickTiming.gapCounts()[3] == 1 && clickTiming.gapCounts()[1] == 1);
    for (uint32_t frame = 107; frame < 114; ++frame) clickTiming.recordPress(frame, 0);
    CHECK(clickTiming.pressCount() == 10 && clickTiming.recent().size() == ClickTimingTracker::kRecentLimit);
    CHECK(clickTiming.recent().front().sequence == 6 && clickTiming.recent().back().sequence == 10);
    clickTiming.reset();
    CHECK(clickTiming.pressCount() == 0 && clickTiming.recent().empty());
    for (auto [tps, fps] : std::vector<std::pair<uint32_t, uint32_t>>{{240, 60}, {240, 144}, {1000, 360}, {480, 60}, {60, 240}, {7, 3}}) {
        StepScheduler s(tps, fps);
        uint64_t total = 0;
        for (uint32_t i = 0; i < fps; ++i) total += s.nextFrameSteps();
        CHECK(total == tps);
    }
    StepScheduler even(240, 60);
    CHECK(even.nextFrameSteps() == 4 && even.nextFrameSteps() == 4);
    StepScheduler uneven(240, 144);
    uint32_t mn = 99, mx = 0;
    for (int i = 0; i < 144; ++i) {
        const uint32_t n = uneven.nextFrameSteps();
        mn = std::min(mn, n);
        mx = std::max(mx, n);
    }
    CHECK(mn == 1 && mx == 2);  // uneven: exactly why snapping to a divisor matters
    CHECK(!even.configure(0, 60) && !even.configure(60, 0) && !even.configure(kMaxTps + 1, 60));
    CHECK(even.tps() == 240);  // failed configure leaves state alone

    CHECK(nearestDivisorFps(240, 144) == 120);
    CHECK(nearestDivisorFps(240, 60) == 60);
    CHECK(nearestDivisorFps(240, 1000) == 240);
    CHECK(nearestDivisorFps(240, 1) == 1);
    CHECK(nearestDivisorFps(0, 60) == 0);
    CHECK(240 % nearestDivisorFps(240, 100) == 0);

    // click resolution. 1s = 1,000,000 us.
    CHECK(resolveInputStep(0, 240, 60, true) == 0);
    CHECK(resolveInputStep(1'000'000, 240, 60, true) == 240);
    CHECK(resolveInputStep(4166, 240, 60, true) == 1);   // just after step 0 started -> step 1
    CHECK(resolveInputStep(4167, 240, 60, true) == 2);   // step 1 starts at 4166.67us
    CHECK(resolveInputStep(5000, 240, 60, false) == 4);  // next frame boundary is 16667us -> frame 1 -> step 4
    CHECK(resolveInputStep(16666, 240, 60, false) == 4); // frame 1 starts at 16666.67us, so 16666us still maps to it
    CHECK(resolveInputStep(16667, 240, 60, false) == 8); // 16667us just missed it -> next frame boundary
    CHECK(resolveInputStep(5000, 240, 60, true) < resolveInputStep(5000, 240, 60, false) + 1);
    CHECK(resolveInputStep(10'000'000'000ull, 1000, 360, true) == 10'000'000ull);

    // latency
    LatencyCalibrator lc;
    CHECK(!lc.offsetMs().has_value());
    for (double v : {10., 11., 9., 10., 10., 12., 8., 10.}) lc.addSample(v);
    lc.addSample(500.0);                // outlier
    lc.addSample(std::nan(""));         // ignored
    CHECK(lc.count() == 9);
    auto ms = lc.offsetMs();
    CHECK(ms.has_value() && *ms > 9.0 && *ms < 11.5);
    auto fr = lc.offsetFrames(240);
    CHECK(fr.has_value() && std::abs(*fr - *ms * 0.24) < 1e-9);
    CHECK(!lc.offsetFrames(0).has_value());
    CHECK(displayFrame(100, 2.4) == 98 && displayFrame(100, 0.0) == 100 && displayFrame(100, -1.6) == 102);
    lc.reset();
    CHECK(lc.count() == 0);
}

// ------------------------------------------------------------- analysis
static void testAnalysis() {
    section("analysis");
    std::vector<TraceSample> a, b;
    for (uint32_t f = 0; f < 10; ++f) {
        a.push_back({f, f * 5.0, 10.0, 0});
        b.push_back({f, f * 5.0, 10.0, 0});
    }
    CHECK(!firstDivergence(a, b).has_value());
    b[6].y += 0.2;
    auto d = firstDivergence(a, b);
    CHECK(d && d->frame == 6 && d->correctable && !d->missing);
    b[6].y += 5;
    d = firstDivergence(a, b);
    CHECK(d && d->frame == 6 && !d->correctable);
    b[6].y = a[6].y;
    b[3].yVel = 0.0005;  // within tolerance
    CHECK(!firstDivergence(a, b).has_value());
    b.erase(b.begin() + 4);  // missing frame
    d = firstDivergence(a, b);
    CHECK(d && d->frame == 4 && d->missing);
    CHECK(!firstDivergence({}, {}).has_value());

    b = a;
    b[2].x += 3;
    b[2].y += 4;
    auto diff = diffTraces(a, b);
    CHECK(diff.size() == 10 && std::abs(diff[2].dist - 5.0) < 1e-12 && diff[1].dist == 0);

    // snapshots
    SnapshotStore<int> ss(10, 3);
    for (uint32_t f = 0; f < 100; ++f) ss.record(f, static_cast<int>(f));
    CHECK(ss.size() == 3);                                   // bounded
    CHECK(ss.latestAtOrBefore(95)->frame == 90);
    CHECK(ss.latestAtOrBefore(70) != nullptr && ss.latestAtOrBefore(70)->frame == 70);
    CHECK(ss.latestAtOrBefore(10) == nullptr);               // dropped (oldest first)
    ss.discardAfter(75);
    CHECK(ss.latestAtOrBefore(999)->frame == 70);
    ss.record(70, 777);                                      // re-recording a frame replaces it
    CHECK(ss.size() == 1 && ss.latestAtOrBefore(70)->state == 777);
    SnapshotStore<int> s0(10, 8);
    s0.record(0, 1);
    s0.record(0, 2);
    CHECK(s0.size() == 1 && s0.latestAtOrBefore(0)->state == 2);

    // markers
    MarkerList ml;
    const uint32_t m2 = ml.add(200, "late");
    const uint32_t m1 = ml.add(50, "early");
    ml.add(120);
    CHECK(ml.all().size() == 3 && ml.all()[0].frame == 50 && ml.all()[2].frame == 200);
    CHECK(ml.next(50)->frame == 120 && ml.next(200) == nullptr);
    CHECK(ml.prev(120)->frame == 50 && ml.prev(50) == nullptr);
    CHECK(ml.setNote(m1, "x") && !ml.setNote(9999, "y"));
    CHECK(ml.remove(m2) && !ml.remove(m2) && ml.all().size() == 2);

    // trainer
    std::vector<ClickWindow> ref = {{0, 100, true, 0, 2, 3}, {1, 110, false, 0, 0, 0}, {2, 200, true, 0, 1, 1}};
    Trainer tr(ref);
    CHECK(tr.clickCount() == 2);  // releases excluded
    auto f1 = tr.onClick(0, 100);
    CHECK(f1 && f1->label == "perfect" && f1->inWindow);
    auto f2 = tr.onClick(1, 203);
    CHECK(f2 && f2->label == "late by 3" && !f2->inWindow);
    auto f3 = tr.onClick(0, 98);
    CHECK(f3 && f3->label == "early by 2" && f3->inWindow);
    CHECK(!tr.onClick(2, 5).has_value());
    CHECK(tr.totalClicks() == 3 && tr.hits() == 2);
    CHECK(std::abs(tr.hitRate() - 2.0 / 3.0) < 1e-12 && std::abs(tr.meanAbsError() - (0 + 3 + 2) / 3.0) < 1e-12);
    Trainer empty({});
    CHECK(empty.hitRate() == 0 && empty.meanAbsError() == 0);
}

// -------------------------------------------------------------- control
static void testControl() {
    section("control");
    LeaderboardGuard g;
    CHECK(!g.blocksSubmission() && g.badgeText().empty());
    g.setActive(Feature::Cbf, true);
    CHECK(!g.blocksSubmission());  // CBF alone is allowed by default
    g.setActive(Feature::Macro, true);
    CHECK(g.blocksSubmission());
    g.setActive(Feature::Macro, false);  // toggling off must NOT clear this attempt's taint
    CHECK(g.blocksSubmission());
    CHECK(g.badgeText() == "NO SUBMIT: Macro");
    g.beginAttempt();
    CHECK(!g.blocksSubmission());
    g.setActive(Feature::CustomTps, true);
    g.beginAttempt();  // still active -> next attempt starts tainted
    CHECK(g.blocksSubmission());
    g.setActive(Feature::Pathfinder, true);
    CHECK(g.badgeText() == "NO SUBMIT: TPS + Pathfinder");
    LeaderboardGuard strict(bit(Feature::Cbf));
    strict.setActive(Feature::Cbf, true);
    CHECK(strict.blocksSubmission());

    CHECK(fuzzyScore("", "anything") == 0);
    CHECK(fuzzyScore("xyz", "frame counter") == -1);
    CHECK(fuzzyScore("fc", "Frame Counter") > fuzzyScore("fc", "Wildfire cache"));
    CHECK(fuzzyScore("tps", "Custom TPS") >= 0);
    CHECK(fuzzyScore("TPS", "custom tps") >= 0);  // case-insensitive
    CHECK(fuzzyScore("abc", "ab") == -1);
    std::vector<std::string> items = {"Open Settings", "Custom TPS", "Toggle Pathfinder", "Frame Counter", "Export Macro"};
    auto ranked = fuzzyRank("tp", items);
    CHECK(!ranked.empty());
    CHECK(ranked[0] == 1 || ranked[0] == 2);  // "Custom TPS" / "Toggle Pathfinder" lead
    CHECK(fuzzyRank("zzz", items).empty());
    CHECK(fuzzyRank("", items).size() == items.size());
}

// ----------------------------------------------------------- pathfinder
static toy::Level twoSpikes() {
    toy::Level l;
    l.spikes = {{100, 120, 10}, {400, 420, 10}};
    l.endX = 600;
    return l;
}

// A low ceiling over a stretch of ground means the cube must NOT be airborne there,
// so a single held press (continuous hopping) can't work: a route needs >= 2 presses.
static toy::Level needsTwo() {
    toy::Level l;
    l.spikes = {{100, 120, 10}, {400, 420, 10}};
    l.ceilings = {{200, 300, 30}};
    l.endX = 600;
    return l;
}

static bool routeClears(const toy::Level& l, const Route& r) {
    toy::Sim sim(l);
    return replay(sim, r.macro).finished;
}

static void testPathfinder() {
    section("pathfinder");
    {
        toy::Level lvl = needsTwo();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.beamWidth = 600;
        s.maxFrames = 400;
        SolveResult r = solve(sim, s);
        CHECK(!r.routes.empty());
        bool allClear = true;
        for (const auto& rt : r.routes) allClear = allClear && routeClears(lvl, rt);
        CHECK(allClear);
        CHECK(r.routes[0].clicks == 2);  // the minimum: one hold would hop into the ceiling
        CHECK(r.routes.size() <= s.maxRoutes);

        // determinism
        SolveResult r2 = solve(sim, s);
        CHECK(r2.routes.size() == r.routes.size() && r2.routes[0].macro == r.routes[0].macro);

        // ranking
        auto ranked = r.routes;
        rankRoutes(ranked, sim);
        bool sane = true;
        for (const auto& rt : ranked) sane = sane && rt.humanness >= 0 && rt.humanness <= 100 && rt.minWidth >= 1;
        CHECK(sane);
        for (size_t i = 1; i < ranked.size(); ++i) CHECK(ranked[i - 1].humanness >= ranked[i].humanness);

        // windows on a verified route agree with the replay: widening by one past the window must fail
        SimRunner runner(sim);
        WindowReport w = computeWindows(ranked[0].macro, runner);
        CHECK(w.baselineOk && !w.windows.empty());
        bool edgesHold = true;
        for (const auto& cw : w.windows) {
            if (!cw.down) continue;
            auto test = [&](int delta) {
                Macro t = ranked[0].macro;
                for (auto& e : t.events)
                    if (&e == &t.events[cw.eventIndex]) e.frame = static_cast<uint32_t>(static_cast<int>(e.frame) + delta);
                t.normalize();
                return runner.succeeds(t);
            };
            if (cw.lateBy < 16) edgesHold = edgesHold && test(cw.lateBy) && !test(cw.lateBy + 1);
            if (cw.earlyBy < 16) edgesHold = edgesHold && test(-cw.earlyBy) && !test(-(cw.earlyBy + 1));
        }
        CHECK(edgesHold);
    }
    {  // impossible: spike taller than any jump
        toy::Level l;
        l.spikes = {{100, 120, 100}};
        toy::Sim sim(l);
        SolveSettings s;
        s.maxFrames = 400;
        SolveResult r = solve(sim, s);
        CHECK(r.routes.empty());
        CHECK(!r.failure.explain().empty() && r.failure.explain().find("died") != std::string::npos);
        CHECK(r.failure.bestProgress < 120);
    }
    {  // max clicks
        toy::Level lvl = needsTwo();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        s.maxClicks = 1;
        SolveResult r = solve(sim, s);
        CHECK(r.routes.empty() && r.failure.rejectedByMaxClicks > 0);
        CHECK(r.failure.explain().find("Max-clicks") != std::string::npos);
        s.maxClicks = 2;
        CHECK(!solve(sim, s).routes.empty());
    }
    {  // CPS cap: gap of 20 frames between toggles
        toy::Level lvl = needsTwo();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        s.minToggleGap = 20;
        SolveResult r = solve(sim, s);
        CHECK(!r.routes.empty());
        bool gapOk = true;
        for (const auto& rt : r.routes) {
            gapOk = gapOk && (rt.minGap == 0 || rt.minGap >= 20);
            gapOk = gapOk && routeClears(lvl, rt);
        }
        CHECK(gapOk);
        s.minToggleGap = 1000;  // absurd: needs a release and a second press, impossible this far apart
        CHECK(solve(sim, s).routes.empty());
    }
    {  // hints
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        // spike 1 is at x=100..120 => frames ~20..24. Forbid any press before frame 5.
        s.hints = {{0, 4, 0, HintKind::ForbidPress}};
        SolveResult r = solve(sim, s);
        CHECK(!r.routes.empty());
        bool honoured = true;
        for (const auto& rt : r.routes)
            for (const auto& e : rt.macro.events)
                if (e.down && e.frame <= 4) honoured = false;
        CHECK(honoured);
        // forcing release through the whole first spike makes the level impossible
        s.hints = {{0, 30, 0, HintKind::ForceRelease}};
        SolveResult none = solve(sim, s);
        CHECK(none.routes.empty() && none.failure.rejectedByHints > 0);
        // ForceHold over frames 10..12 is satisfiable (jump near frame 5..10 clears spike 1)
        s.hints = {{10, 12, 0, HintKind::ForceHold}};
        SolveResult hold = solve(sim, s);
        CHECK(!hold.routes.empty());
        bool startsHeld = true;
        for (const auto& rt : hold.routes) startsHeld = startsHeld && rt.macro.stateAt(10).held[0] && rt.macro.stateAt(11).held[0] && rt.macro.stateAt(12).held[0];
        CHECK(startsHeld);
    }
    {  // cache
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        SolveCache cache(2);
        const uint64_t k1 = solveKey(sim, s, "sec1");
        CHECK(cache.find(k1) == nullptr);
        cache.put(k1, solve(sim, s));
        CHECK(cache.find(k1) != nullptr && !cache.find(k1)->routes.empty());
        CHECK(solveKey(sim, s, "sec2") != k1);
        SolveSettings s2 = s;
        s2.minToggleGap = 3;
        CHECK(solveKey(sim, s2, "sec1") != k1);
        // failures aren't cached
        SolveResult bad;
        cache.put(12345, bad);
        CHECK(cache.find(12345) == nullptr);
        // LRU eviction
        SolveResult good = solve(sim, s);
        cache.put(2, good);
        cache.find(k1);  // refresh k1
        cache.put(3, good);
        CHECK(cache.size() == 2 && cache.find(k1) != nullptr && cache.find(2) == nullptr && cache.find(3) != nullptr);
    }
    {  // async job + cancel
        toy::Level lvl = twoSpikes();
        SolveSettings s;
        s.maxFrames = 400;
        SolveJob job(std::make_unique<toy::Sim>(lvl), s);
        const SolveResult& jr = job.wait();
        CHECK(job.done() && !jr.routes.empty() && !jr.cancelled);

        std::atomic<bool> stop{true};
        SolveSettings s2 = s;
        s2.cancel = [&] { return stop.load(); };
        SolveJob job2(std::make_unique<toy::Sim>(lvl), s2);
        const SolveResult& cr = job2.wait();
        CHECK(cr.cancelled && cr.routes.empty());
    }
    {  // time budget
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        s.timeBudgetSec = 1e-9;  // already exceeded by the first check
        SolveResult r = solve(sim, s);
        CHECK(r.timedOut && r.routes.empty());
    }
    {  // section stitching with anchored start states + held-input handoff
        toy::Level lvl = twoSpikes();
        lvl.endX = 250;
        toy::Sim sec1Start(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        SolveResult r1 = solve(sec1Start, s);
        CHECK(!r1.routes.empty());
        const Route& a = r1.routes[0];
        auto mid = simulateRoute(sec1Start, a);
        CHECK(mid->finished());

        toy::Level full = twoSpikes();
        auto* midToy = static_cast<toy::Sim*>(mid.get());
        midToy->retarget(full.endX);
        SolveSettings s2 = s;
        s2.initialMask = a.finalMask;
        SolveResult r2 = solve(*midToy, s2);
        CHECK(!r2.routes.empty());
        Macro joined = stitchRoutes({a, r2.routes[0]});
        toy::Sim whole(full);
        CHECK(replay(whole, joined).finished);
    }
    {  // dual players: P2 input is an extra action bit but doesn't change toy physics
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.players = 2;
        s.maxFrames = 400;
        s.beamWidth = 400;
        SolveResult r = solve(sim, s);
        CHECK(!r.routes.empty());
        CHECK(r.routes[0].macro.pressCount(1) == 0);  // fewer-clicks tie-break never presses P2
        CHECK(routeClears(lvl, r.routes[0]));
    }
    {  // initial held input
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        s.initialMask = 1;
        SolveResult r = solve(sim, s);
        CHECK(!r.routes.empty() && r.routes[0].macro.initialMask == 1);
        CHECK(routeClears(lvl, r.routes[0]));
    }
    {  // memory guard
        toy::Level lvl = twoSpikes();
        toy::Sim sim(lvl);
        SolveSettings s;
        s.maxFrames = 400;
        s.maxArenaNodes = 50;
        SolveResult r = solve(sim, s);
        CHECK(r.failure.arenaExhausted && r.routes.empty());
        CHECK(r.failure.explain().find("memory") != std::string::npos);
    }
}


// ----------------------------------------------------------------- fuzz
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint32_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(s >> 33);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

static bool isCanonical(const Macro& m) {
    Macro n = m;
    n.normalize();
    return n == m;
}

static void testFuzz() {
    section("fuzz");
    Rng rng(12345);

    // random edits never break the canonical form, and undo/redo are exact inverses
    bool canon = true, roundtrip = true, undoOk = true;
    for (int iter = 0; iter < 300; ++iter) {
        MacroEditor ed;
        std::vector<Macro> history{ed.macro()};
        for (int op = 0; op < 40; ++op) {
            bool changed = false;
            switch (rng.below(6)) {
                case 0: changed = ed.addEvent({rng.below(200), rng.below(2) == 0, static_cast<uint8_t>(rng.below(2))}); break;
                case 1: changed = ed.removeEvent(rng.below(static_cast<uint32_t>(ed.macro().events.size() + 1))); break;
                case 2: changed = ed.removeClick(rng.below(static_cast<uint32_t>(ed.macro().events.size() + 1))); break;
                case 3: changed = ed.moveEvent(rng.below(static_cast<uint32_t>(ed.macro().events.size() + 1)), rng.below(200)); break;
                case 4: {
                    const uint32_t a = rng.below(150);
                    changed = ed.trim(a, a + 1 + rng.below(100));
                    break;
                }
                case 5: {
                    Macro tail = mk({{rng.below(10), true, 0}, {10 + rng.below(10), false, 0}});
                    changed = ed.stitch(tail, rng.below(250));
                    break;
                }
            }
            canon = canon && isCanonical(ed.macro());
            auto parsed = Macro::parse(ed.macro().serialize());
            roundtrip = roundtrip && parsed.has_value() && *parsed == ed.macro();
            if (changed) history.push_back(ed.macro());
        }
        // walk all the way back, comparing with recorded history
        for (size_t i = history.size() - 1; i-- > 0;) {
            undoOk = undoOk && ed.undo() && ed.macro() == history[i];
        }
        undoOk = undoOk && !ed.undo();
    }
    CHECK(canon);
    CHECK(roundtrip);
    CHECK(undoOk);

    // random levels: every route the solver returns must clear the level when replayed on its own
    int solved = 0, unsolved = 0;
    bool allValid = true, withinLimits = true;
    for (int iter = 0; iter < 40; ++iter) {
        toy::Level l;
        const int nSpikes = 1 + static_cast<int>(rng.below(4));
        for (int i = 0; i < nSpikes; ++i) {
            const int x0 = 80 + static_cast<int>(rng.below(400));
            l.spikes.push_back({x0, x0 + 10 + static_cast<int>(rng.below(30)), 5 + static_cast<int>(rng.below(60))});
        }
        if (rng.below(2)) {
            const int x0 = 100 + static_cast<int>(rng.below(300));
            l.ceilings.push_back({x0, x0 + 30 + static_cast<int>(rng.below(80)), 25 + static_cast<int>(rng.below(60))});
        }
        l.endX = 600;
        toy::Sim sim(l);
        SolveSettings s;
        s.maxFrames = 400;
        s.beamWidth = 300;
        s.minToggleGap = 1 + rng.below(6);
        s.maxClicks = rng.below(3) ? UINT32_MAX : 3 + rng.below(4);
        SolveResult r = solve(sim, s);
        if (r.routes.empty()) {
            ++unsolved;
            continue;
        }
        ++solved;
        for (const auto& rt : r.routes) {
            allValid = allValid && replay(sim, rt.macro).finished && isCanonical(rt.macro);
            withinLimits = withinLimits && rt.clicks <= s.maxClicks && (rt.minGap == 0 || rt.minGap >= s.minToggleGap);
        }
    }
    CHECK(allValid);
    CHECK(withinLimits);
    CHECK(solved > 0 && unsolved > 0);  // the generator should produce both kinds
    std::printf("  (fuzz levels: %d solved, %d unsolved)\n", solved, unsolved);
}

int main() {
    testMacro();
    testEditor();
    testWindows();
    testTiming();
    testAnalysis();
    testControl();
    testPathfinder();
    testFuzz();
    std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
