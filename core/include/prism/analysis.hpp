#pragma once
// Run analysis tools: desync detection, physics snapshot diff, state snapshots
// for rewind, frame markers and the frame-perfect trainer.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "frame_windows.hpp"

namespace prism {

// ---- Desync detector + physics diff ---------------------------------------

struct TraceSample {
    uint32_t frame = 0;
    double x = 0, y = 0, yVel = 0;
};

struct Divergence {
    uint32_t frame = 0;
    double dx = 0, dy = 0, dyVel = 0;
    bool missing = false;      // one trace has no sample for this frame
    bool correctable = false;  // small enough to snap back instead of failing
};

struct DesyncOptions {
    double posTolerance = 1e-3;       // below this counts as identical
    double velTolerance = 1e-3;
    double correctableLimit = 0.5;    // position error up to this may be auto-corrected
};

// Both traces must be sorted by frame. Returns the first frame where they
// disagree beyond tolerance, or nullopt if they match over their common range.
inline std::optional<Divergence> firstDivergence(const std::vector<TraceSample>& a, const std::vector<TraceSample>& b,
                                                 const DesyncOptions& o = {}) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].frame < b[j].frame) {
            return Divergence{a[i].frame, 0, 0, 0, true, false};
        }
        if (b[j].frame < a[i].frame) {
            return Divergence{b[j].frame, 0, 0, 0, true, false};
        }
        const double dx = b[j].x - a[i].x, dy = b[j].y - a[i].y, dv = b[j].yVel - a[i].yVel;
        if (std::abs(dx) > o.posTolerance || std::abs(dy) > o.posTolerance || std::abs(dv) > o.velTolerance) {
            Divergence d{a[i].frame, dx, dy, dv, false, false};
            d.correctable = std::max(std::abs(dx), std::abs(dy)) <= o.correctableLimit;
            return d;
        }
        ++i;
        ++j;
    }
    return std::nullopt;
}

struct DiffSample {
    uint32_t frame = 0;
    double dx = 0, dy = 0, dyVel = 0, dist = 0;
};

// Per-frame difference (b - a) over the frames both traces contain.
inline std::vector<DiffSample> diffTraces(const std::vector<TraceSample>& a, const std::vector<TraceSample>& b) {
    std::vector<DiffSample> out;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].frame < b[j].frame) {
            ++i;
        } else if (b[j].frame < a[i].frame) {
            ++j;
        } else {
            DiffSample d;
            d.frame = a[i].frame;
            d.dx = b[j].x - a[i].x;
            d.dy = b[j].y - a[i].y;
            d.dyVel = b[j].yVel - a[i].yVel;
            d.dist = std::hypot(d.dx, d.dy);
            out.push_back(d);
            ++i;
            ++j;
        }
    }
    return out;
}

// ---- Snapshots for rewind / re-record -------------------------------------

// Stores a copy of game state every `interval` frames, bounded to `maxCount`
// entries (oldest dropped first).
template <class T>
class SnapshotStore {
public:
    struct Entry {
        uint32_t frame;
        T state;
    };

    SnapshotStore(uint32_t interval = 30, size_t maxCount = 512) : interval_(interval ? interval : 1), max_(maxCount ? maxCount : 1) {}

    // Safe to call every frame; only stores on interval boundaries. Recording a
    // frame at or before the newest snapshot discards the now-stale future.
    void record(uint32_t frame, const T& state) {
        if (frame % interval_ != 0 && !entries_.empty()) return;
        while (!entries_.empty() && entries_.back().frame >= frame) entries_.pop_back();
        entries_.push_back({frame, state});
        while (entries_.size() > max_) entries_.pop_front();
    }

    const Entry* latestAtOrBefore(uint32_t frame) const {
        const Entry* best = nullptr;
        for (const auto& e : entries_) {
            if (e.frame > frame) break;
            best = &e;
        }
        return best;
    }

    // Drop every snapshot taken after `frame`.
    void discardAfter(uint32_t frame) {
        while (!entries_.empty() && entries_.back().frame > frame) entries_.pop_back();
    }

    size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    uint32_t interval_;
    size_t max_;
    std::deque<Entry> entries_;
};

// ---- Frame markers ---------------------------------------------------------

struct Marker {
    uint32_t id = 0;
    uint32_t frame = 0;
    std::string note;
};

class MarkerList {
public:
    uint32_t add(uint32_t frame, std::string note = {}) {
        Marker m{nextId_++, frame, std::move(note)};
        auto it = std::upper_bound(items_.begin(), items_.end(), m, [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
        items_.insert(it, std::move(m));
        return nextId_ - 1;
    }
    bool remove(uint32_t id) {
        auto it = std::find_if(items_.begin(), items_.end(), [&](const Marker& m) { return m.id == id; });
        if (it == items_.end()) return false;
        items_.erase(it);
        return true;
    }
    bool setNote(uint32_t id, std::string note) {
        for (auto& m : items_)
            if (m.id == id) {
                m.note = std::move(note);
                return true;
            }
        return false;
    }
    // First marker strictly after / before `frame`.
    const Marker* next(uint32_t frame) const {
        for (const auto& m : items_)
            if (m.frame > frame) return &m;
        return nullptr;
    }
    const Marker* prev(uint32_t frame) const {
        const Marker* best = nullptr;
        for (const auto& m : items_) {
            if (m.frame >= frame) break;
            best = &m;
        }
        return best;
    }
    const std::vector<Marker>& all() const { return items_; }

private:
    std::vector<Marker> items_;
    uint32_t nextId_ = 1;
};

// ---- Frame-perfect trainer -------------------------------------------------

struct ClickFeedback {
    int offset = 0;       // actual - reference frame (negative = early)
    bool inWindow = false;
    std::string label;    // "perfect", "early by 2", "late by 1"
};

// Compares the player's actual click frames against a reference window set
// (e.g. from a pathfinder route or recorded macro) and keeps running stats.
class Trainer {
public:
    explicit Trainer(std::vector<ClickWindow> reference) : ref_(std::move(reference)) {
        std::erase_if(ref_, [](const ClickWindow& w) { return !w.down; });
    }

    size_t clickCount() const { return ref_.size(); }

    // `clickIndex` indexes presses in order (0 = first press).
    std::optional<ClickFeedback> onClick(size_t clickIndex, uint32_t actualFrame) {
        if (clickIndex >= ref_.size()) return std::nullopt;
        const ClickWindow& w = ref_[clickIndex];
        ClickFeedback f;
        f.offset = static_cast<int>(actualFrame) - static_cast<int>(w.frame);
        f.inWindow = f.offset >= -w.earlyBy && f.offset <= w.lateBy;
        if (f.offset == 0)
            f.label = "perfect";
        else if (f.offset < 0)
            f.label = "early by " + std::to_string(-f.offset);
        else
            f.label = "late by " + std::to_string(f.offset);
        ++clicks_;
        if (f.inWindow) ++hits_;
        absErrSum_ += std::abs(f.offset);
        return f;
    }

    size_t totalClicks() const { return clicks_; }
    size_t hits() const { return hits_; }
    double hitRate() const { return clicks_ ? static_cast<double>(hits_) / static_cast<double>(clicks_) : 0.0; }
    double meanAbsError() const { return clicks_ ? absErrSum_ / static_cast<double>(clicks_) : 0.0; }

private:
    std::vector<ClickWindow> ref_;
    size_t clicks_ = 0, hits_ = 0;
    double absErrSum_ = 0;
};

}  // namespace prism
