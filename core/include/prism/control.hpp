#pragma once
// Leaderboard guard + command palette fuzzy matching.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace prism {

// ---- Leaderboard guard -----------------------------------------------------

enum class Feature : uint32_t {
    Macro = 1u << 0,
    CustomTps = 1u << 1,
    Pathfinder = 1u << 2,
    FrameStep = 1u << 3,
    SeedLock = 1u << 4,
    Cbf = 1u << 5,
    SnapshotRewind = 1u << 6,
};

constexpr uint32_t bit(Feature f) { return static_cast<uint32_t>(f); }

// Tracks which physics-altering features were active during the current
// attempt. A feature that was on at any point in the attempt stays "tainted"
// until the next attempt begins, so toggling it off right before the finish
// line can't sneak a submission through.
class LeaderboardGuard {
public:
    // Features that block submission. CBF is intentionally not in the default
    // set (it only changes input timing, not physics); configurable.
    static constexpr uint32_t kDefaultBlocking = bit(Feature::Macro) | bit(Feature::CustomTps) | bit(Feature::Pathfinder) |
                                                 bit(Feature::FrameStep) | bit(Feature::SeedLock) | bit(Feature::SnapshotRewind);

    explicit LeaderboardGuard(uint32_t blockingMask = kDefaultBlocking) : blocking_(blockingMask) {}

    void setActive(Feature f, bool on) {
        if (on) {
            active_ |= bit(f);
            tainted_ |= bit(f);
        } else {
            active_ &= ~bit(f);
        }
    }

    // New attempt: taint resets to whatever is active right now.
    void beginAttempt() { tainted_ = active_; }

    bool blocksSubmission() const { return (tainted_ & blocking_) != 0; }
    uint32_t taintedMask() const { return tainted_; }

    // Short text for an on-screen badge; empty when the run is clean.
    std::string badgeText() const {
        struct N {
            Feature f;
            const char* name;
        };
        static const N names[] = {{Feature::Macro, "Macro"},          {Feature::CustomTps, "TPS"},   {Feature::Pathfinder, "Pathfinder"},
                                  {Feature::FrameStep, "Frame Step"}, {Feature::SeedLock, "Seed"},   {Feature::SnapshotRewind, "Rewind"}};
        std::string out;
        for (const auto& n : names)
            if ((tainted_ & blocking_ & bit(n.f)) != 0) {
                if (!out.empty()) out += " + ";
                out += n.name;
            }
        return out.empty() ? out : "NO SUBMIT: " + out;
    }

private:
    uint32_t blocking_;
    uint32_t active_ = 0;
    uint32_t tainted_ = 0;
};

// ---- Fuzzy matching --------------------------------------------------------

// Subsequence match, case-insensitive. Returns -1 if `query` isn't a
// subsequence of `text`; otherwise a score where higher is better.
// Bonuses: consecutive characters, matches at word starts, earlier matches.
inline int fuzzyScore(const std::string& query, const std::string& text) {
    if (query.empty()) return 0;
    int score = 0;
    size_t t = 0;
    int streak = 0;
    for (char qc : query) {
        const char q = static_cast<char>(std::tolower(static_cast<unsigned char>(qc)));
        bool found = false;
        for (; t < text.size(); ++t) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[t])));
            if (c != q) {
                streak = 0;
                continue;
            }
            const bool wordStart = t == 0 || text[t - 1] == ' ' || text[t - 1] == '_' || text[t - 1] == '-' || text[t - 1] == '/';
            score += 10 + (wordStart ? 15 : 0) + streak * 8 - static_cast<int>(std::min<size_t>(t, 20)) / 4;
            ++streak;
            ++t;
            found = true;
            break;
        }
        if (!found) return -1;
    }
    // Prefer shorter candidates when everything else is equal.
    score -= static_cast<int>(std::min<size_t>(text.size(), 60)) / 6;
    return score;
}

// Indices of `items` that match, best first. Stable for equal scores.
inline std::vector<size_t> fuzzyRank(const std::string& query, const std::vector<std::string>& items) {
    std::vector<std::pair<int, size_t>> hits;
    for (size_t i = 0; i < items.size(); ++i) {
        const int s = fuzzyScore(query, items[i]);
        if (s >= 0) hits.push_back({s, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<size_t> out;
    out.reserve(hits.size());
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

}  // namespace prism
