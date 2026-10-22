// Frame counter HUD, attempt lifecycle, FPS application.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/platform/cplatform.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

class $modify(PrismPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        CCLabelBMFont* clickLabel = nullptr;
        CCLabelBMFont* timingTitle = nullptr;
        std::array<CCLabelBMFont*, 9> timingRows{};
        std::string lastText;
        std::string lastClickText;
        std::array<std::string, 9> lastTimingRows{};
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        auto* label = CCLabelBMFont::create("", "chatFont.fnt");
        label->setID("frame-counter"_spr);
        label->setAnchorPoint({0.f, 1.f});
        label->setAlignment(kCCTextAlignmentLeft);
        // The UI layer is not affected by camera triggers and stays above gameplay.
        m_uiLayer->addChild(label, 100);
        m_fields->label = label;

        m_fields->clickLabel = CCLabelBMFont::create("NO PRESSES YET", "chatFont.fnt");
        m_fields->clickLabel->setAnchorPoint({0.f, 1.f});
        m_fields->clickLabel->setScale(0.42f);
        m_uiLayer->addChild(m_fields->clickLabel, 100);

        m_fields->timingTitle = CCLabelBMFont::create("OBSERVED CLICK GAPS", "chatFont.fnt");
        m_fields->timingTitle->setAnchorPoint({0.f, 1.f});
        m_fields->timingTitle->setScale(0.38f);
        m_fields->timingTitle->setColor({180, 205, 213});
        m_uiLayer->addChild(m_fields->timingTitle, 100);

        static const ccColor3B bucketColors[] = {
            {255, 91, 117}, {255, 137, 90}, {255, 185, 83}, {244, 218, 92}, {170, 220, 100},
            {99, 215, 139}, {75, 205, 188}, {95, 178, 226}, {171, 151, 245},
        };
        for (size_t i = 0; i < m_fields->timingRows.size(); ++i) {
            m_fields->timingRows[i] = CCLabelBMFont::create("", "chatFont.fnt");
            m_fields->timingRows[i]->setAnchorPoint({0.f, 1.f});
            m_fields->timingRows[i]->setScale(0.36f);
            m_fields->timingRows[i]->setColor(bucketColors[i]);
            m_uiLayer->addChild(m_fields->timingRows[i], 100);
        }

        state().beginAttempt();
        applyFps();
        updateHud(0.f);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        state().beginAttempt();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        updateHud(dt);
    }

    void onQuit() {
        // Leaving the level always ends any recording/playback session.
        restoreFps();
        state().mode = MacroMode::Idle;
        state().refreshGuard();
        PlayLayer::onQuit();
    }

    void restoreFps() {
        auto& st = state();
        if (st.appliedFps == 0) return;
        const float original = st.originalFpsTarget > 0.f ? st.originalFpsTarget : 60.f;
        GameManager::get()->m_customFPSTarget = original;
    #if !defined(GEODE_IS_ANDROID)
        GameManager::get()->setGameVariable("0116", st.originalFpsVariable);
    #endif
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / static_cast<double>(original));
        st.appliedFps = 0;
        st.originalFpsTarget = 0.f;
    }

    void applyFps() {
        auto& st = state();
        if (!cfgBool("custom-fps-enabled")) return restoreFps();
        const int fps = static_cast<int>(std::clamp<int64_t>(cfgInt("custom-fps"), 30, 1000));
        if (st.appliedFps == fps) return;
        if (st.appliedFps == 0) {
            st.originalFpsTarget = GameManager::get()->m_customFPSTarget;
            if (st.originalFpsTarget <= 0.f) st.originalFpsTarget = 60.f;
        #if !defined(GEODE_IS_ANDROID)
            st.originalFpsVariable = GameManager::get()->getGameVariable("0116");
        #endif
        }
        st.appliedFps = fps;
        GameManager::get()->m_customFPSTarget = static_cast<float>(fps);
    #if !defined(GEODE_IS_ANDROID)
        GameManager::get()->setGameVariable("0116", true);
    #endif
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / static_cast<double>(fps));
    }

    void updateHud(float dt) {
        auto* label = m_fields->label;
        if (!label) return;

        applyFps();

        const bool show = cfgBool("show-frame-counter");
        label->setVisible(show);
        const bool showTimings = show && cfgBool("hud-show-click-timings");
        m_fields->clickLabel->setVisible(showTimings);
        m_fields->timingTitle->setVisible(showTimings);
        for (auto* row : m_fields->timingRows) row->setVisible(showTimings);
        if (!show) return;

        auto& st = state();
        st.refreshGuard();
        if (dt > 0.f) st.fpsSmooth = st.fpsSmooth <= 0.0 ? 1.0 / dt : st.fpsSmooth * 0.9 + (1.0 / dt) * 0.1;

        const int frame = static_cast<int>(std::max<int64_t>(0, m_gameState.m_currentProgress));
        std::string text = "FB COUNTER\nFrame " + std::to_string(frame);

        if (cfgBool("hud-show-time")) {
            const float tps = st.tpsActive() ? static_cast<float>(cfgInt("custom-tps")) : 240.f;
            char buf[48];
            std::snprintf(buf, sizeof buf, "\nTime %.3fs", static_cast<double>(frame) / static_cast<double>(tps));
            text += buf;
        }
        if (cfgBool("hud-show-rates")) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "\n%.0f FPS  %d TPS", st.fpsSmooth, st.tpsActive() ? static_cast<int>(cfgInt("custom-tps")) : 240);
            text += buf;
        }
        if (st.mode == MacroMode::Recording) {
            text += "\nREC " + std::to_string(st.macro.pressCount()) + " clicks";
        } else if (st.mode == MacroMode::Playing) {
            text += "\nPLAY " + std::to_string(st.playIdx) + "/" + std::to_string(st.macro.events.size());
        }
        const std::string badge = st.guard.badgeText();
        if (!badge.empty()) text += "\n" + badge;

        // Only touch the label when something changed: rebuilding BMFont text every frame is wasteful.
        if (text != m_fields->lastText) {
            m_fields->lastText = text;
            label->setString(text.c_str());
        }

        const auto win = CCDirector::get()->getWinSize();
        const float x = std::clamp(static_cast<float>(cfgInt("hud-x")), 8.f, std::max(8.f, win.width - 180.f));
        const float desiredTop = win.height - static_cast<float>(cfgInt("hud-y"));
        const float maxTop = std::max(8.f, win.height - 8.f);
        const float requestedScale = static_cast<float>(std::clamp(cfgFloat("hud-scale"), 0.2, 2.0));
        const float availableWidth = std::max(80.f, win.width - x - 12.f);
        label->limitLabelWidth(availableWidth, requestedScale, 0.3f);
        label->setOpacity(static_cast<GLubyte>(std::clamp(cfgFloat("hud-opacity"), 0.1, 1.0) * 255.0));

        if (showTimings) {
            auto& clickLabel = m_fields->clickLabel;
            std::string clickText = "NO PRESSES YET";
            const auto& recentPresses = st.clickTiming.recent();
            if (!recentPresses.empty()) {
                clickText.clear();
            }
            for (size_t i = 0; i < recentPresses.size(); ++i) {
                const auto& press = recentPresses[i];
                const char* player = press.player == 0 ? "P1" : "P2";
                if (press.hasGap) {
                    const double tps = st.tpsActive() ? static_cast<double>(std::clamp<int64_t>(cfgInt("custom-tps"), 30, 2000)) : 240.0;
                    char gapText[48];
                    std::snprintf(gapText, sizeof gapText, "+%uf / %.1fms est", press.gapFrames,
                                  static_cast<double>(press.gapFrames) * 1000.0 / tps);
                    clickText += std::string(player) + "  #" + std::to_string(press.sequence) + "  F " +
                                 std::to_string(press.frame) + "  " + gapText;
                } else {
                    clickText += std::string(player) + "  #" + std::to_string(press.sequence) + "  F " +
                                 std::to_string(press.frame) + "  FIRST";
                }
                if (i + 1 < recentPresses.size()) clickText += "\n";
            }
            if (clickText != m_fields->lastClickText) {
                m_fields->lastClickText = clickText;
                clickLabel->setString(clickText.c_str());
            }
            clickLabel->limitLabelWidth(availableWidth, 0.42f, 0.28f);
            const float labelHeight = label->getContentSize().height * label->getScale();
            const float clickHeight = clickLabel->getContentSize().height * clickLabel->getScale();
            const float titleHeight = m_fields->timingTitle->getContentSize().height * m_fields->timingTitle->getScale();
            float rowHeight = 0.f;
            for (auto* row : m_fields->timingRows) {
                rowHeight = std::max(rowHeight, row->getContentSize().height * row->getScale());
            }
            const float detailHeight = labelHeight + 12.f + clickHeight + 8.f + titleHeight + 4.f +
                                       static_cast<float>(m_fields->timingRows.size() - 1) * 15.f + rowHeight;
            const bool timingFits = detailHeight + 8.f <= maxTop;
            for (auto* row : m_fields->timingRows) row->setVisible(timingFits);
            m_fields->timingTitle->setVisible(timingFits);
            clickLabel->setVisible(timingFits);
            const float top = timingFits
                ? std::clamp(desiredTop, detailHeight + 8.f, maxTop)
                : std::clamp(desiredTop, 8.f, maxTop);
            label->setPosition({x, top});
            const float detailTop = top - labelHeight - 12.f;
            if (timingFits) {
                clickLabel->setPosition({x, detailTop});
                m_fields->timingTitle->setPosition({x, detailTop - clickHeight - 8.f});
            }

            static const char* const bucketNames[] = {"0-1f", "2f", "3f", "4f", "5f", "6f", "7-8f", "9-10f", "11+f"};
            uint32_t peak = 1;
            for (const auto count : st.clickTiming.gapCounts()) peak = std::max(peak, count);
            for (size_t row = 0; row < m_fields->timingRows.size(); ++row) {
                const size_t bucket = m_fields->timingRows.size() - row - 1;
                const auto count = st.clickTiming.gapCounts()[bucket];
                const size_t barLength = std::min<size_t>(10, static_cast<size_t>(count) * 10 / peak);
                const std::string value = std::string(bucketNames[bucket]) + "  " + std::to_string(count) + "  " + std::string(barLength, '|');
                if (value != m_fields->lastTimingRows[row]) {
                    m_fields->lastTimingRows[row] = value;
                    m_fields->timingRows[row]->setString(value.c_str());
                }
                const float rowsTop = detailTop - clickHeight - 8.f - titleHeight - 4.f;
                m_fields->timingRows[row]->setPosition({x, rowsTop - static_cast<float>(row) * 15.f});
            }
        } else {
            label->setPosition({x, std::clamp(desiredTop, 8.f, maxTop)});
        }
    }
};
