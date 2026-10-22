#pragma once
// Timing engine: fixed-point TPS/FPS scheduling, click-between-frames
// resolution and input-latency calibration. Integer math only, so the step
// pattern is exactly reproducible (no float drift between runs).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace prism {

inline constexpr uint32_t kMaxTps = 100000;
inline constexpr uint32_t kMaxFps = 100000;

// Distributes `tps` physics steps over `fps` render frames per second using a
// Bresenham-style accumulator: the per-frame step count is deterministic and
// the total over any whole second is exactly `tps`.
class StepScheduler {
public:
    StepScheduler(uint32_t tps = 240, uint32_t fps = 60) { configure(tps, fps); }

    bool configure(uint32_t tps, uint32_t fps) {
        if (tps == 0 || fps == 0 || tps > kMaxTps || fps > kMaxFps) return false;
        tps_ = tps;
        fps_ = fps;
        counter_ = 0;
        return true;
    }

    uint32_t nextFrameSteps() {
        counter_ += tps_;
        const uint32_t steps = static_cast<uint32_t>(counter_ / fps_);
        counter_ %= fps_;
        return steps;
    }

    uint32_t tps() const { return tps_; }
    uint32_t fps() const { return fps_; }

private:
    uint32_t tps_ = 240, fps_ = 60;
    uint64_t counter_ = 0;
};

// Closest FPS to `requested` that divides `tps` evenly, so every render frame
// runs the same whole number of steps (no stutter from uneven step counts).
// Ties go to the higher FPS.
inline uint32_t nearestDivisorFps(uint32_t tps, uint32_t requested) {
    if (tps == 0) return 0;
    uint32_t best = tps;
    uint64_t bestDist = UINT64_MAX;
    for (uint32_t d = 1; d <= tps; ++d) {
        if (tps % d) continue;
        const uint32_t fps = tps / d;
        const uint64_t dist = fps > requested ? fps - requested : requested - fps;
        if (dist < bestDist || (dist == bestDist && fps > best)) {
            bestDist = dist;
            best = fps;
        }
    }
    return best;
}

// Maps a real input timestamp (microseconds since run start) to the physics
// step it lands on. Valid for t_us <= 1e12 and tps/fps <= kMax*.
//   CBF on : the first step that starts at or after the input.
//   CBF off: the first step of the first render frame that starts at or after
//            the input (input is only seen at frame boundaries).
inline uint64_t resolveInputStep(uint64_t t_us, uint32_t tps, uint32_t fps, bool cbf) {
    constexpr uint64_t kMicro = 1000000ull;
    if (cbf) return (t_us * tps + kMicro - 1) / kMicro;
    const uint64_t frame = (t_us * fps + kMicro - 1) / kMicro;
    return frame * tps / fps;
}

// Measures how late inputs arrive on this machine (ms). Uses a trimmed mean so
// a few outliers (alt-tab, GC pause) don't skew the result.
class LatencyCalibrator {
public:
    static constexpr size_t kMinSamples = 8;

    void addSample(double latencyMs) {
        if (std::isfinite(latencyMs)) samples_.push_back(latencyMs);
    }
    void reset() { samples_.clear(); }
    size_t count() const { return samples_.size(); }

    std::optional<double> offsetMs() const {
        if (samples_.size() < kMinSamples) return std::nullopt;
        std::vector<double> s = samples_;
        std::sort(s.begin(), s.end());
        const size_t trim = s.size() / 5;  // 20% each side
        double sum = 0;
        size_t n = 0;
        for (size_t i = trim; i < s.size() - trim; ++i, ++n) sum += s[i];
        return n ? std::optional<double>(sum / static_cast<double>(n)) : std::nullopt;
    }

    std::optional<double> offsetFrames(double fps) const {
        auto ms = offsetMs();
        if (!ms || fps <= 0) return std::nullopt;
        return *ms * fps / 1000.0;
    }

private:
    std::vector<double> samples_;
};

// If inputs reach the game `offsetFrames` late, you must click that much
// earlier to land on `frame`. This is the frame to *display* as your target.
inline int displayFrame(uint32_t frame, double offsetFrames) {
    return static_cast<int>(frame) - static_cast<int>(std::lround(offsetFrames));
}

}  // namespace prism
