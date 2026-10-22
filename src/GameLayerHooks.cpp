// Hooks on GJBaseGameLayer: step tracking, macro record/playback, custom TPS.
//
// ASSUMPTIONS TO VERIFY IN-GAME (see README "First-run checklist"):
//  A1. m_gameState.m_currentProgress advances by 1 per physics step.
//  A2. real clicks reach GJBaseGameLayer::handleButton(down, button, isPlayer1).
//  A3. getModifiedDelta() decides the physics step size (custom TPS).

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

class $modify(PrismBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& st = state();
        const uint32_t frame = static_cast<uint32_t>(std::max<int64_t>(0, m_gameState.m_currentProgress));
        st.curFrame = frame;

        if (st.mode == MacroMode::Playing) {
            // Frame went backwards (checkpoint load): re-seek instead of replaying from the start.
            if (frame < st.playLastFrame) {
                st.playIdx = static_cast<size_t>(
                    std::lower_bound(st.macro.events.begin(), st.macro.events.end(), frame,
                                     [](prism::InputEvent const& e, uint32_t f) { return e.frame < f; }) -
                    st.macro.events.begin());
            }
            st.playLastFrame = frame;

            st.injecting = true;
            while (st.playIdx < st.macro.events.size() && st.macro.events[st.playIdx].frame <= frame) {
                auto const& e = st.macro.events[st.playIdx];
                // button 1 = jump. Player 2 events drive the second icon.
                GJBaseGameLayer::handleButton(e.down, 1, e.player == 0);
                ++st.playIdx;
            }
            st.injecting = false;
        }

        st.inCommands = true;
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        st.inCommands = false;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& st = state();
        if (!st.injecting) {
            if (st.mode == MacroMode::Playing && cfgBool("macro-block-input")) return;  // ignore real clicks
            if (st.mode == MacroMode::Recording && button == 1) {
                // Inputs handled inside processCommands act on this step; inputs handled between
                // steps act on the next one. Using the same sampling point as playback keeps
                // record and replay in step.
                const uint32_t f = st.inCommands ? st.curFrame : st.curFrame + 1;
                st.macro.events.push_back({f, down, static_cast<uint8_t>(isPlayer1 ? 0 : 1)});
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Custom TPS: same shape as the vanilla step-size calculation, with our tick rate.
    double getModifiedDelta(float delta) {
        auto& st = state();
        if (!st.tpsActive()) return GJBaseGameLayer::getModifiedDelta(delta);

        const double tps = static_cast<double>(std::clamp<int64_t>(cfgInt("custom-tps"), 30, 2000));
        const double modifier = std::min(1.f, m_gameState.m_timeWarp) / tps;
        const double total = delta + m_extraDelta;
        const int steps = std::max(1, static_cast<int>(total / modifier));
        const double newDelta = static_cast<double>(steps) * modifier;
        m_extraDelta = total - newDelta;
        return newDelta;
    }
};
