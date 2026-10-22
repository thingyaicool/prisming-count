// Pause-menu controls: record / play / stop / save / load macros, frame markers.

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

namespace {
void toast(char const* msg, NotificationIcon icon = NotificationIcon::Info) {
    Notification::create(msg, icon, 1.5f)->show();
}
}  // namespace

class $modify(PrismPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto* menu = CCMenu::create();
        menu->setID("prism-menu"_spr);
        const auto win = CCDirector::get()->getWinSize();

        struct Btn {
            char const* text;
            SEL_MenuHandler cb;
        };
        const Btn btns[] = {
            {"Rec", menu_selector(PrismPauseLayer::onRec)},
            {"Play", menu_selector(PrismPauseLayer::onPlay)},
            {"Stop", menu_selector(PrismPauseLayer::onStop)},
            {"Save", menu_selector(PrismPauseLayer::onSave)},
            {"Load", menu_selector(PrismPauseLayer::onLoad)},
            {"Mark", menu_selector(PrismPauseLayer::onMark)},
        };
        const int n = static_cast<int>(sizeof btns / sizeof btns[0]);
        constexpr float minTouchHeight = 44.f;
        const bool useGrid = win.height < n * minTouchHeight + 32.f;
        const float menuX = std::clamp(win.width * 0.1f, 48.f, 72.f);
        const float verticalGap = std::min(48.f, std::max(0.f, win.height - 32.f) / (n - 1));
        menu->setPosition({useGrid ? 0.f : menuX, win.height * 0.5f});
        for (int i = 0; i < n; ++i) {
            auto* spr = ButtonSprite::create(btns[i].text);
            const float contentHeight = std::max(1.f, spr->getContentSize().height);
            spr->setScale(std::max(0.75f, minTouchHeight / contentHeight));
            auto* item = CCMenuItemSpriteExtra::create(spr, this, btns[i].cb);
            if (useGrid) {
                const int row = i / 2;
                const float columnGap = std::clamp(win.width * 0.18f, 72.f, 108.f);
                const float rowGap = std::min(48.f, std::max(0.f, win.height - 32.f) / 2.f);
                item->setPosition({columnGap * (i % 2) + 48.f,
                                   (1.f - static_cast<float>(row)) * rowGap});
            } else {
                item->setPosition({0.f, (static_cast<float>(n - 1) * 0.5f - static_cast<float>(i)) * verticalGap});
            }
            menu->addChild(item);
        }
        this->addChild(menu);
    }

    void onRec(CCObject*) {
        auto& st = state();
        st.mode = MacroMode::Recording;
        st.macro = prism::Macro{};
        st.refreshGuard();
        toast("Recording: restart the attempt to begin");
    }

    void onPlay(CCObject*) {
        auto& st = state();
        st.macro.normalize();
        if (st.macro.events.empty()) return toast("No macro loaded", NotificationIcon::Error);
        st.mode = MacroMode::Playing;
        st.playIdx = 0;
        st.playLastFrame = 0;
        st.refreshGuard();
        toast("Playback armed: restart the attempt");
    }

    void onStop(CCObject*) {
        auto& st = state();
        if (st.mode == MacroMode::Recording) st.macro.normalize();
        st.mode = MacroMode::Idle;
        st.refreshGuard();
        toast("Stopped");
    }

    void onSave(CCObject*) {
        auto& st = state();
        st.macro.normalize();
        if (st.macro.events.empty()) return toast("Nothing to save", NotificationIcon::Error);
        auto* pl = PlayLayer::get();
        const auto path = macroPathFor(pl ? pl->m_level : nullptr);
        saveMacroFile(path, st.macro) ? toast("Macro saved", NotificationIcon::Success) : toast("Save failed", NotificationIcon::Error);
    }

    void onLoad(CCObject*) {
        auto* pl = PlayLayer::get();
        auto m = loadMacroFile(macroPathFor(pl ? pl->m_level : nullptr));
        if (!m) return toast("No valid macro for this level", NotificationIcon::Error);
        state().macro = std::move(*m);
        toast("Macro loaded", NotificationIcon::Success);
    }

    void onMark(CCObject*) {
        auto& st = state();
        st.markers.add(st.curFrame);
        toast(("Marked frame " + std::to_string(st.curFrame)).c_str(), NotificationIcon::Success);
    }
};
