// Leaderboard Guard: refuse to save progress for attempts that used physics-altering features.
//
// ASSUMPTION A4 (verify in-game): savePercentage has this signature in 2.2081 and is the path
// that records level progress. If a completion still submits, report it and we hook the next layer.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJGameLevel.hpp>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

class $modify(PrismGameLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        auto& st = state();
        st.refreshGuard();
        if (cfgBool("safe-mode") && st.guard.blocksSubmission()) {
            log::info("Prism safe mode: progress not saved ({})", st.guard.badgeText());
            return;
        }
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};
