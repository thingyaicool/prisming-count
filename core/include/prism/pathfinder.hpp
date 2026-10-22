#pragma once
// Pathfinder: finds click routes through a level section.
//
// The game's physics is hidden behind ISim. The in-game glue implements ISim by
// driving the real game step; tests implement it with a toy physics model. The
// solver itself is a deterministic beam search over per-frame input choices.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "frame_windows.hpp"
#include "macro.hpp"

namespace prism {

struct ISim {
    virtual ~ISim() = default;
    virtual std::unique_ptr<ISim> clone() const = 0;
    virtual void step(InputState in) = 0;       // advance exactly one physics step
    virtual bool dead() const = 0;
    virtual bool finished() const = 0;          // reached the end of the section
    virtual double progress() const = 0;        // higher = further along
    virtual uint64_t stateHash() const = 0;     // hash of quantized state, for dedupe
    virtual uint32_t frame() const = 0;         // absolute frame of the NEXT step
};

enum class HintKind : uint8_t { ForceHold, ForceRelease, ForbidPress, ForbidRelease };

// User guidance: "hold here", "don't click here". Frames are absolute and the
// range is inclusive.
struct Hint {
    uint32_t startFrame = 0, endFrame = 0;
    uint8_t player = 0;
    HintKind kind = HintKind::ForbidPress;
};

struct SolveSettings {
    int players = 1;                       // 1 or 2
    size_t beamWidth = 512;
    uint32_t maxFrames = 20000;            // frames to simulate at most
    uint32_t maxClicks = UINT32_MAX;       // per-route press limit (all players)
    uint32_t minToggleGap = 1;             // min frames between toggles of one player (CPS cap)
    uint8_t initialMask = 0;               // inputs already held at the start frame
    size_t maxRoutes = 5;
    size_t maxFinishedCandidates = 256;    // stop expanding once this many nodes finished
    size_t maxArenaNodes = 20'000'000;     // memory guard for route history
    double timeBudgetSec = 0;              // 0 = unlimited
    std::vector<Hint> hints;
    std::function<bool()> cancel;                       // true = abort
    std::function<void(uint32_t frame, double best)> progress;
};

struct Route {
    Macro macro;                // absolute frames
    uint32_t clicks = 0;
    uint32_t minGap = 0;        // smallest gap between consecutive toggles (0 if < 2 toggles)
    uint32_t finishFrame = 0;
    uint8_t finalMask = 0;
    // filled by rankRoutes():
    int minWidth = 0;
    double meanLogWidth = 0;
    double humanness = 0;       // 0..100
};

struct FailureReport {
    double bestProgress = 0;
    uint32_t bestProgressFrame = 0;
    std::map<uint32_t, uint64_t> deathsByBucket;  // bucket start frame -> candidates that died there
    uint32_t bucketSize = 30;
    uint64_t rejectedByMaxClicks = 0, rejectedByGap = 0, rejectedByHints = 0;
    bool arenaExhausted = false;

    std::string explain() const {
        std::string s;
        if (arenaExhausted) return "Search ran out of memory budget; lower beam width or solve a shorter section.";
        uint64_t worst = 0;
        uint32_t worstBucket = 0;
        for (const auto& [b, n] : deathsByBucket)
            if (n > worst) {
                worst = n;
                worstBucket = b;
            }
        if (worst > 0)
            s += "Most candidates died between frames " + std::to_string(worstBucket) + " and " + std::to_string(worstBucket + bucketSize - 1) +
                 " (" + std::to_string(worst) + " deaths). ";
        s += "Furthest any route got: frame " + std::to_string(bestProgressFrame) + ". ";
        if (rejectedByMaxClicks) s += "Max-clicks limit blocked " + std::to_string(rejectedByMaxClicks) + " options; try raising it. ";
        if (rejectedByGap) s += "CPS cap blocked " + std::to_string(rejectedByGap) + " options; try a smaller minimum gap. ";
        if (rejectedByHints) s += "Your hints blocked " + std::to_string(rejectedByHints) + " options; try removing some. ";
        if (s.empty()) s = "No route found.";
        return s;
    }
};

struct SolveResult {
    std::vector<Route> routes;
    FailureReport failure;     // meaningful when routes is empty
    bool cancelled = false;
    bool timedOut = false;
    uint64_t nodesExpanded = 0;
    uint32_t framesSimulated = 0;
};

namespace detail {

inline uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
inline uint64_t combine(uint64_t h, uint64_t v) { return mix(h ^ mix(v)); }

struct Hist {
    int32_t parent;
    uint8_t mask;
};

struct Node {
    std::unique_ptr<ISim> sim;
    int32_t hist = -1;
    uint8_t mask = 0;
    uint32_t clicks = 0;
    uint32_t lastToggle[2] = {0, 0};
    bool everToggled[2] = {false, false};
    double score = 0;
};

inline bool hintAllows(const std::vector<Hint>& hints, uint32_t frame, uint8_t prevMask, uint8_t mask, int players) {
    for (const auto& h : hints) {
        if (frame < h.startFrame || frame > h.endFrame || h.player >= players) continue;
        const bool prev = (prevMask >> h.player) & 1, now = (mask >> h.player) & 1;
        switch (h.kind) {
            case HintKind::ForceHold:
                if (!now) return false;
                break;
            case HintKind::ForceRelease:
                if (now) return false;
                break;
            case HintKind::ForbidPress:
                if (!prev && now) return false;
                break;
            case HintKind::ForbidRelease:
                if (prev && !now) return false;
                break;
        }
    }
    return true;
}

}  // namespace detail

inline Route buildRoute(const std::vector<uint8_t>& masks, uint32_t startFrame, uint8_t initialMask) {
    Route r;
    r.macro.initialMask = initialMask;
    uint8_t prev = initialMask;
    int64_t lastToggleFrame[2] = {-1, -1};
    uint32_t minGap = UINT32_MAX;
    for (size_t i = 0; i < masks.size(); ++i) {
        const uint32_t f = startFrame + static_cast<uint32_t>(i);
        for (uint8_t p = 0; p < 2; ++p) {
            const bool was = (prev >> p) & 1, now = (masks[i] >> p) & 1;
            if (was == now) continue;
            r.macro.events.push_back({f, now, p});
            if (now) ++r.clicks;
            if (lastToggleFrame[p] >= 0) minGap = std::min<uint32_t>(minGap, static_cast<uint32_t>(f - lastToggleFrame[p]));
            lastToggleFrame[p] = f;
        }
        prev = masks[i];
    }
    r.macro.normalize();
    r.minGap = minGap == UINT32_MAX ? 0 : minGap;
    r.finalMask = prev;
    r.finishFrame = startFrame + static_cast<uint32_t>(masks.size());
    return r;
}

inline SolveResult solve(const ISim& start, const SolveSettings& s) {
    using namespace detail;
    SolveResult res;
    FailureReport& fail = res.failure;
    const int players = std::clamp(s.players, 1, 2);
    const uint8_t maskCount = static_cast<uint8_t>(1u << players);
    const auto t0 = std::chrono::steady_clock::now();
    auto timeUp = [&] {
        return s.timeBudgetSec > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > s.timeBudgetSec;
    };

    std::vector<Hist> arena;
    std::vector<Node> beam;
    {
        Node root;
        root.sim = start.clone();
        root.mask = static_cast<uint8_t>(s.initialMask & (maskCount - 1));
        root.score = root.sim->progress();
        beam.push_back(std::move(root));
    }
    const uint32_t startFrame = start.frame();

    struct Finished {
        int32_t hist;
        uint8_t mask;
        uint32_t frame;
    };
    std::vector<Finished> finished;

    auto reconstruct = [&](int32_t h) {
        std::vector<uint8_t> masks;
        for (int32_t i = h; i >= 0; i = arena[static_cast<size_t>(i)].parent) masks.push_back(arena[static_cast<size_t>(i)].mask);
        std::reverse(masks.begin(), masks.end());
        return masks;
    };

    for (uint32_t step = 0; step < s.maxFrames && !beam.empty(); ++step) {
        if (s.cancel && s.cancel()) {
            res.cancelled = true;
            break;
        }
        if (timeUp()) {
            res.timedOut = true;
            break;
        }
        const uint32_t frame = startFrame + step;
        std::vector<Node> cand;
        std::unordered_map<uint64_t, size_t> seen;

        for (auto& n : beam) {
            for (uint8_t mask = 0; mask < maskCount; ++mask) {
                // ---- legality checks
                uint32_t addClicks = 0;
                bool ok = true;
                for (int p = 0; p < players; ++p) {
                    const bool was = (n.mask >> p) & 1, now = (mask >> p) & 1;
                    if (was == now) continue;
                    if (n.everToggled[p] && frame - n.lastToggle[p] < s.minToggleGap) {
                        ++fail.rejectedByGap;
                        ok = false;
                        break;
                    }
                    if (now) ++addClicks;
                }
                if (!ok) continue;
                if (n.clicks + addClicks > s.maxClicks) {
                    ++fail.rejectedByMaxClicks;
                    continue;
                }
                if (!hintAllows(s.hints, frame, n.mask, mask, players)) {
                    ++fail.rejectedByHints;
                    continue;
                }

                // ---- advance
                Node c;
                c.sim = n.sim->clone();
                c.sim->step(InputState::fromMask(mask));
                ++res.nodesExpanded;
                if (c.sim->dead()) {
                    ++fail.deathsByBucket[(frame / fail.bucketSize) * fail.bucketSize];
                    continue;
                }
                if (arena.size() >= s.maxArenaNodes) {
                    fail.arenaExhausted = true;
                    res.framesSimulated = step;
                    goto done;
                }
                c.hist = static_cast<int32_t>(arena.size());
                arena.push_back({n.hist, mask});
                c.mask = mask;
                c.clicks = n.clicks + addClicks;
                for (int p = 0; p < 2; ++p) {
                    c.lastToggle[p] = n.lastToggle[p];
                    c.everToggled[p] = n.everToggled[p];
                    if (p < players && (((n.mask >> p) & 1) != ((mask >> p) & 1))) {
                        c.lastToggle[p] = frame;
                        c.everToggled[p] = true;
                    }
                }
                c.score = c.sim->progress();
                if (c.score > fail.bestProgress) {
                    fail.bestProgress = c.score;
                    fail.bestProgressFrame = frame;
                }

                if (c.sim->finished()) {
                    finished.push_back({c.hist, mask, frame + 1});
                    continue;
                }

                uint64_t key = c.sim->stateHash();
                key = combine(key, mask);
                for (int p = 0; p < players; ++p) {
                    const uint32_t since = c.everToggled[p] ? std::min<uint32_t>(frame - c.lastToggle[p], s.minToggleGap) : s.minToggleGap;
                    key = combine(key, since);
                }
                auto it = seen.find(key);
                if (it == seen.end()) {
                    seen.emplace(key, cand.size());
                    cand.push_back(std::move(c));
                } else if (c.clicks < cand[it->second].clicks) {
                    cand[it->second] = std::move(c);  // same state, fewer clicks: strictly better
                }
            }
        }
        res.framesSimulated = step + 1;
        if (s.progress) s.progress(frame, fail.bestProgress);

        // keep the best `beamWidth` candidates: progress desc, clicks asc, history id asc (deterministic)
        auto better = [](const Node& a, const Node& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.clicks != b.clicks) return a.clicks < b.clicks;
            return a.hist < b.hist;
        };
        if (cand.size() > s.beamWidth) {
            std::nth_element(cand.begin(), cand.begin() + static_cast<std::ptrdiff_t>(s.beamWidth), cand.end(), better);
            cand.resize(s.beamWidth);
        }
        beam = std::move(cand);
        if (finished.size() >= s.maxFinishedCandidates) break;
    }
done:

    // ---- turn finished nodes into distinct routes
    std::vector<Route> routes;
    for (const auto& f : finished) {
        Route r = buildRoute(reconstruct(f.hist), startFrame, s.initialMask & (maskCount - 1));
        r.finishFrame = f.frame;
        const bool dup = std::any_of(routes.begin(), routes.end(), [&](const Route& o) { return o.macro == r.macro; });
        if (!dup) routes.push_back(std::move(r));
    }
    std::stable_sort(routes.begin(), routes.end(), [](const Route& a, const Route& b) {
        if (a.clicks != b.clicks) return a.clicks < b.clicks;
        return a.minGap > b.minGap;
    });
    if (routes.size() > s.maxRoutes) routes.resize(s.maxRoutes);
    res.routes = std::move(routes);
    return res;
}

// ---- Replay a route in a fresh sim (used for verification and windows) ------

struct ReplayResult {
    bool finished = false;
    bool died = false;
    uint32_t endFrame = 0;
};

inline ReplayResult replay(const ISim& start, const Macro& m, uint32_t maxFrames = 200000) {
    auto sim = start.clone();
    MacroCursor cur(m);
    ReplayResult r;
    for (uint32_t i = 0; i < maxFrames; ++i) {
        const uint32_t f = sim->frame();
        sim->step(cur.at(f));
        if (sim->dead()) {
            r.died = true;
            break;
        }
        if (sim->finished()) {
            r.finished = true;
            break;
        }
    }
    r.endFrame = sim->frame();
    return r;
}

// IRunner over an ISim so frame windows can be computed for any route.
// The macro carries its own initialMask, so held-at-start inputs just work.
class SimRunner : public IRunner {
public:
    explicit SimRunner(const ISim& start) : start_(start.clone()) {}
    bool succeeds(const Macro& m) override { return replay(*start_, m).finished; }

private:
    std::unique_ptr<ISim> start_;
};

// Compute window widths for each route and a humanness score (0..100).
//   width score : log2(minWidth)/4 clamped      (16+ frames = full marks)  weight 0.55
//   gap score   : log2(minGap)/4 clamped        (16+ frame gaps = full)   weight 0.25
//   click score : 1/(1 + clicks/50)                                         weight 0.20
inline void rankRoutes(std::vector<Route>& routes, const ISim& start, int windowRadius = 16) {
    for (auto& r : routes) {
        SimRunner runner(start);
        WindowOptions o;
        o.maxRadius = windowRadius;
        const WindowReport rep = computeWindows(r.macro, runner, o);
        int minW = INT32_MAX;
        double sumLog = 0;
        size_t n = 0;
        for (const auto& w : rep.windows) {
            if (!w.down) continue;
            minW = std::min(minW, w.width());
            sumLog += std::log2(static_cast<double>(w.width()));
            ++n;
        }
        r.minWidth = n ? minW : 0;
        r.meanLogWidth = n ? sumLog / static_cast<double>(n) : 0;
        const double widthScore = n ? std::clamp(std::log2(static_cast<double>(r.minWidth)) / 4.0, 0.0, 1.0) : 1.0;
        const double gapScore = r.minGap ? std::clamp(std::log2(static_cast<double>(r.minGap)) / 4.0, 0.0, 1.0) : 1.0;
        const double clickScore = 1.0 / (1.0 + static_cast<double>(r.clicks) / 50.0);
        r.humanness = 100.0 * (0.55 * widthScore + 0.25 * gapScore + 0.20 * clickScore);
    }
    std::stable_sort(routes.begin(), routes.end(), [](const Route& a, const Route& b) { return a.humanness > b.humanness; });
}

// ---- Section stitching ------------------------------------------------------

// Run `route` through `start` and return the sim at the end (anchor for the next section).
inline std::unique_ptr<ISim> simulateRoute(const ISim& start, const Route& route) {
    auto sim = start.clone();
    MacroCursor cur(route.macro);
    while (sim->frame() < route.finishFrame && !sim->dead() && !sim->finished()) sim->step(cur.at(sim->frame()));
    return sim;
}

inline Macro stitchRoutes(const std::vector<Route>& sections) {
    Macro out;
    if (!sections.empty()) out.initialMask = sections.front().macro.initialMask;
    for (const auto& r : sections) out.events.insert(out.events.end(), r.macro.events.begin(), r.macro.events.end());
    out.normalize();
    return out;
}

// ---- Solve cache --------------------------------------------------------------

inline uint64_t settingsFingerprint(const SolveSettings& s) {
    using namespace detail;
    uint64_t h = 0x1234;
    h = combine(h, static_cast<uint64_t>(s.players));
    h = combine(h, s.beamWidth);
    h = combine(h, s.maxFrames);
    h = combine(h, s.maxClicks);
    h = combine(h, s.minToggleGap);
    h = combine(h, s.initialMask);
    h = combine(h, s.maxRoutes);
    for (const auto& hint : s.hints) {
        h = combine(h, hint.startFrame);
        h = combine(h, hint.endFrame);
        h = combine(h, hint.player);
        h = combine(h, static_cast<uint64_t>(hint.kind));
    }
    return h;
}

inline uint64_t solveKey(const ISim& start, const SolveSettings& s, const std::string& sectionId) {
    using namespace detail;
    uint64_t h = settingsFingerprint(s);
    h = combine(h, start.stateHash());
    h = combine(h, start.frame());
    for (char c : sectionId) h = combine(h, static_cast<uint8_t>(c));
    return h;
}

// Small LRU. Only successful, un-cancelled results are cached.
class SolveCache {
public:
    explicit SolveCache(size_t capacity = 32) : cap_(capacity ? capacity : 1) {}

    const SolveResult* find(uint64_t key) {
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second;
    }
    void put(uint64_t key, SolveResult r) {
        if (r.cancelled || r.timedOut || r.routes.empty()) return;
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = std::move(r);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(key, std::move(r));
        map_[key] = order_.begin();
        if (order_.size() > cap_) {
            map_.erase(order_.back().first);
            order_.pop_back();
        }
    }
    size_t size() const { return order_.size(); }

private:
    size_t cap_;
    std::list<std::pair<uint64_t, SolveResult>> order_;
    std::unordered_map<uint64_t, std::list<std::pair<uint64_t, SolveResult>>::iterator> map_;
};

// ---- Background solving ---------------------------------------------------------

// Runs solve() on its own thread so the game's render loop never blocks.
class SolveJob {
public:
    SolveJob(std::unique_ptr<ISim> start, SolveSettings settings) : start_(std::move(start)), settings_(std::move(settings)) {
        userCancel_ = settings_.cancel;
        auto userProgress = settings_.progress;
        settings_.cancel = [this] { return cancel_.load() || (userCancel_ && userCancel_()); };
        settings_.progress = [this, userProgress](uint32_t f, double best) {
            frame_.store(f);
            bestBits_.store(best);
            if (userProgress) userProgress(f, best);
        };
        thread_ = std::thread([this] {
            SolveResult r = solve(*start_, settings_);
            {
                std::lock_guard<std::mutex> lk(m_);
                result_ = std::move(r);
            }
            done_.store(true);
        });
    }
    ~SolveJob() {
        cancel_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    SolveJob(const SolveJob&) = delete;
    SolveJob& operator=(const SolveJob&) = delete;

    void cancel() { cancel_.store(true); }
    bool done() const { return done_.load(); }
    uint32_t currentFrame() const { return frame_.load(); }
    double bestProgress() const { return bestBits_.load(); }

    // Blocks until finished and returns the result.
    const SolveResult& wait() {
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lk(m_);
        return result_;
    }

private:
    std::unique_ptr<ISim> start_;
    SolveSettings settings_;
    std::function<bool()> userCancel_;
    std::atomic<bool> cancel_{false}, done_{false};
    std::atomic<uint32_t> frame_{0};
    std::atomic<double> bestBits_{0};
    std::mutex m_;
    SolveResult result_;
    std::thread thread_;
};

}  // namespace prism
