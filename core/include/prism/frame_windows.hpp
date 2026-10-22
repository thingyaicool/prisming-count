#pragma once
// Frame windows: for every input event in a macro, how many frames earlier
// and later it can land while the run still succeeds.
//
// The game itself is hidden behind IRunner so this logic is testable on its own
// and the in-game glue only has to implement "replay this macro, did it clear?".

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "macro.hpp"

namespace prism {

struct IRunner {
    virtual ~IRunner() = default;
    // Replay `m` from the run's start. True if the run is completed.
    virtual bool succeeds(const Macro& m) = 0;
};

struct ClickWindow {
    size_t eventIndex = 0;
    uint32_t frame = 0;
    bool down = false;
    uint8_t player = 0;
    int earlyBy = 0;  // frames earlier it still works
    int lateBy = 0;   // frames later it still works
    int width() const { return earlyBy + lateBy + 1; }
};

struct WindowReport {
    bool baselineOk = false;  // the unmodified macro must clear, or windows are meaningless
    bool cancelled = false;
    std::vector<ClickWindow> windows;
};

struct WindowOptions {
    int maxRadius = 30;  // search at most this many frames each way
    std::function<bool()> cancel;                  // return true to abort
    std::function<void(size_t, size_t)> progress;  // (done events, total events)
};

// Assumes the success region around each event's original frame is contiguous
// (it is expanded outward until the first failure in each direction).
inline WindowReport computeWindows(const Macro& base, IRunner& runner, const WindowOptions& opt = {}) {
    WindowReport rep;
    rep.baselineOk = runner.succeeds(base);
    if (!rep.baselineOk) return rep;

    const auto& ev = base.events;
    for (size_t i = 0; i < ev.size(); ++i) {
        if (opt.cancel && opt.cancel()) {
            rep.cancelled = true;
            return rep;
        }
        const InputEvent e = ev[i];

        // Same-player neighbours bound how far the event may move.
        int64_t lo = -1;  // exclusive lower bound
        int64_t hi = -1;  // exclusive upper bound, -1 = unbounded
        for (size_t j = i; j-- > 0;)
            if (ev[j].player == e.player) {
                lo = ev[j].frame;
                break;
            }
        for (size_t j = i + 1; j < ev.size(); ++j)
            if (ev[j].player == e.player) {
                hi = ev[j].frame;
                break;
            }

        auto works = [&](int64_t newFrame) {
            Macro t = base;
            t.events[i].frame = static_cast<uint32_t>(newFrame);
            t.normalize();
            if (t.events.size() != base.events.size()) return false;
            return runner.succeeds(t);
        };

        ClickWindow w;
        w.eventIndex = i;
        w.frame = e.frame;
        w.down = e.down;
        w.player = e.player;

        for (int d = 1; d <= opt.maxRadius; ++d) {
            const int64_t nf = static_cast<int64_t>(e.frame) + d;
            if (hi >= 0 && nf >= hi) break;
            if (!works(nf)) break;
            w.lateBy = d;
        }
        for (int d = 1; d <= opt.maxRadius; ++d) {
            const int64_t nf = static_cast<int64_t>(e.frame) - d;
            if (nf < 0 || nf <= lo) break;
            if (!works(nf)) break;
            w.earlyBy = d;
        }
        rep.windows.push_back(w);
        if (opt.progress) opt.progress(i + 1, ev.size());
    }
    return rep;
}

inline std::string windowsToCsv(const WindowReport& r) {
    std::ostringstream o;
    o << "index,frame,type,player,early_by,late_by,width\n";
    for (const auto& w : r.windows)
        o << w.eventIndex << ',' << w.frame << ',' << (w.down ? "press" : "release") << ',' << int(w.player) + 1 << ','
          << w.earlyBy << ',' << w.lateBy << ',' << w.width() << '\n';
    return o.str();
}

inline std::string windowsToJson(const WindowReport& r) {
    std::ostringstream o;
    o << "{\"baselineOk\":" << (r.baselineOk ? "true" : "false") << ",\"windows\":[";
    for (size_t i = 0; i < r.windows.size(); ++i) {
        const auto& w = r.windows[i];
        if (i) o << ',';
        o << "{\"index\":" << w.eventIndex << ",\"frame\":" << w.frame << ",\"type\":\"" << (w.down ? "press" : "release")
          << "\",\"player\":" << int(w.player) + 1 << ",\"earlyBy\":" << w.earlyBy << ",\"lateBy\":" << w.lateBy
          << ",\"width\":" << w.width() << '}';
    }
    o << "]}";
    return o.str();
}

// ---- Consistency map -------------------------------------------------------

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

// 1 frame wide = 1.0 (hardest), >= 16 frames wide = 0.0 (easiest).
inline float tightness(int width) {
    if (width <= 1) return 1.f;
    const float t = std::log2(static_cast<float>(width)) / 4.f;
    return std::clamp(1.f - t, 0.f, 1.f);
}

// green (0) -> yellow (0.5) -> red (1)
inline Rgb tightnessColor(float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto lerp = [](float a, float b, float k) { return a + (b - a) * k; };
    float r, g;
    if (t < 0.5f) {
        const float k = t / 0.5f;
        r = lerp(60, 240, k);
        g = lerp(220, 220, k);
    } else {
        const float k = (t - 0.5f) / 0.5f;
        r = lerp(240, 235, k);
        g = lerp(220, 50, k);
    }
    return {static_cast<uint8_t>(std::lround(r)), static_cast<uint8_t>(std::lround(g)), 60};
}

// One value per bucket along the level: the tightest press in the bucket.
// Buckets with no clicks are 0 (nothing to fail on there).
inline std::vector<float> consistencyBuckets(const WindowReport& r, uint32_t levelFrames, size_t bucketCount) {
    std::vector<float> out(bucketCount, 0.f);
    if (bucketCount == 0 || levelFrames == 0) return out;
    for (const auto& w : r.windows) {
        if (!w.down) continue;
        size_t b = static_cast<size_t>(static_cast<uint64_t>(w.frame) * bucketCount / levelFrames);
        if (b >= bucketCount) b = bucketCount - 1;
        out[b] = std::max(out[b], tightness(w.width()));
    }
    return out;
}

}  // namespace prism
