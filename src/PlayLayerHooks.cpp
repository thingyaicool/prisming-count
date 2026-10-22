// Frame counter HUD, attempt lifecycle, FPS application.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

class $modify(PrismPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        std::string lastText;
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
        state().mode = MacroMode::Idle;
        state().refreshGuard();
        PlayLayer::onQuit();
    }

    void applyFps() {
        if (!cfgBool("custom-fps-enabled")) return;
        const int fps = static_cast<int>(std::clamp<int64_t>(cfgInt("custom-fps"), 30, 1000));
        auto& st = state();
        if (st.appliedFps == fps) return;
        st.appliedFps = fps;
        GameManager::get()->m_customFPSTarget = static_cast<float>(fps);
        GameManager::get()->setGameVariable("0116", true);
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / fps);
    }

    void updateHud(float dt) {
        auto* label = m_fields->label;
        if (!label) return;

        const bool show = cfgBool("show-frame-counter");
        label->setVisible(show);
        if (!show) return;

        auto& st = state();
        st.refreshGuard();
        if (dt > 0.f) st.fpsSmooth = st.fpsSmooth <= 0.0 ? 1.0 / dt : st.fpsSmooth * 0.9 + (1.0 / dt) * 0.1;

        const int frame = std::max(0, m_gameState.m_currentProgress);
        std::string text = "Frame " + std::to_string(frame);

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
        label->setScale(static_cast<float>(cfgFloat("hud-scale")));
        label->setOpacity(static_cast<GLubyte>(std::clamp(cfgFloat("hud-opacity"), 0.1, 1.0) * 255.0));
        label->setPosition({static_cast<float>(cfgInt("hud-x")), win.height - static_cast<float>(cfgInt("hud-y"))});

        applyFps();
    }
};
