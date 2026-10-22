// Pause-menu controls: record / play / stop / save / load macros, frame markers.

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>

#include "State.hpp"

using namespace geode::prelude;
using namespace prism_glue;

namespace {
void toast(char const* msg, NotificationIcon icon = NotificationIcon::Info) {
    Notification::create(msg, icon, 1.5f)->show();
}

CCMenuItemSpriteExtra* makeButton(char const* text, CCObject* target, SEL_MenuHandler callback) {
    auto* sprite = ButtonSprite::create(text);
    sprite->setScale(0.72f);
    return CCMenuItemSpriteExtra::create(sprite, target, callback);
}
}  // namespace

class PrismHubPopup : public Popup {
    enum class Page { Modules, FrameCounter, Pathfinder };

    Page m_page = Page::Modules;
    CCMenu* m_pageMenu = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;

    bool init() override {
        if (!Popup::init(360.f, 300.f)) return false;
        this->setTitle("Prism");
        this->showPage(Page::Modules);
        return true;
    }

    void clearPage() {
        if (m_pageMenu) {
            m_pageMenu->removeFromParentAndCleanup(true);
            m_pageMenu = nullptr;
        }
        if (m_pageLabel) {
            m_pageLabel->removeFromParentAndCleanup(true);
            m_pageLabel = nullptr;
        }
    }

    void addLabel(char const* text) {
        m_pageLabel = CCLabelBMFont::create(text, "bigFont.fnt");
        m_pageLabel->setScale(0.42f);
        m_pageLabel->setAlignment(kCCTextAlignmentCenter);
        m_mainLayer->addChildAtPosition(m_pageLabel, Anchor::Center, {0.f, 92.f});
    }

    CCMenu* createPageMenu() {
        m_pageMenu = CCMenu::create();
        m_pageMenu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(m_pageMenu);
        return m_pageMenu;
    }

    void showPage(Page page) {
        this->clearPage();
        m_page = page;
        auto* menu = this->createPageMenu();

        switch (page) {
            case Page::Modules:
                this->addLabel("Choose a module");
                menu->addChild(makeButton("Frame Counter", this, menu_selector(PrismHubPopup::onFrameCounter)), 0, 1);
                menu->addChild(makeButton("Pathfinder", this, menu_selector(PrismHubPopup::onPathfinder)), 0, 2);
                menu->addChild(makeButton("Settings", this, menu_selector(PrismHubPopup::onSettings)), 0, 3);
                static_cast<CCNode*>(menu->getChildByTag(1))->setPosition({0.f, 42.f});
                static_cast<CCNode*>(menu->getChildByTag(2))->setPosition({0.f, -12.f});
                static_cast<CCNode*>(menu->getChildByTag(3))->setPosition({0.f, -66.f});
                break;
            case Page::FrameCounter: {
                this->addLabel("Frame Counter and Macros");
                const struct ActionButton {
                    char const* text;
                    SEL_MenuHandler callback;
                } buttons[] = {
                    {"Record", menu_selector(PrismHubPopup::onRec)},
                    {"Play", menu_selector(PrismHubPopup::onPlay)},
                    {"Stop", menu_selector(PrismHubPopup::onStop)},
                    {"Save", menu_selector(PrismHubPopup::onSave)},
                    {"Load", menu_selector(PrismHubPopup::onLoad)},
                    {"Mark", menu_selector(PrismHubPopup::onMark)},
                };
                for (int i = 0; i < 6; ++i) {
                    auto* item = makeButton(buttons[i].text, this, buttons[i].callback);
                    const int row = i / 2;
                    const int col = i % 2;
                    item->setPosition({col == 0 ? -82.f : 82.f, 44.f - row * 48.f});
                    menu->addChild(item);
                }
                auto* settings = makeButton("Settings", this, menu_selector(PrismHubPopup::onSettings));
                settings->setPosition({-70.f, -102.f});
                menu->addChild(settings);
                auto* back = makeButton("Modules", this, menu_selector(PrismHubPopup::onModules));
                back->setPosition({70.f, -102.f});
                menu->addChild(back);
                break;
            }
            case Page::Pathfinder:
                this->addLabel("Pathfinder core only\nIn-game simulator not implemented");
                menu->addChild(makeButton("Modules", this, menu_selector(PrismHubPopup::onModules)));
                static_cast<CCNode*>(menu->getChildren()->objectAtIndex(0))->setPosition({0.f, -70.f});
                break;
        }
    }

    void onModules(CCObject*) { this->showPage(Page::Modules); }
    void onFrameCounter(CCObject*) { this->showPage(Page::FrameCounter); }
    void onPathfinder(CCObject*) {
        this->showPage(Page::Pathfinder);
        toast("Pathfinder's in-game simulator is not implemented", NotificationIcon::Info);
    }
    void onSettings(CCObject*) { geode::openSettingsPopup(geode::Mod::get()); }

    void onRec(CCObject*) {
        auto& st = state();
        st.mode = MacroMode::Recording;
        st.macro = prism::Macro{};
        st.refreshGuard();
        toast("Recording armed; restart the attempt");
    }

    void onPlay(CCObject*) {
        auto& st = state();
        st.macro.normalize();
        if (st.macro.events.empty()) return toast("No macro loaded", NotificationIcon::Error);
        st.mode = MacroMode::Playing;
        st.playIdx = 0;
        st.playLastFrame = 0;
        st.refreshGuard();
        toast("Playback armed; restart the attempt");
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
        auto* layer = PlayLayer::get();
        const auto path = macroPathFor(layer ? layer->m_level : nullptr);
          const bool saved = saveMacroFile(path, st.macro);
          toast(saved ? "Macro saved" : "Save failed", saved ? NotificationIcon::Success : NotificationIcon::Error);
    }

    void onLoad(CCObject*) {
        auto* layer = PlayLayer::get();
        auto macro = loadMacroFile(macroPathFor(layer ? layer->m_level : nullptr));
        if (!macro) return toast("No valid macro for this level", NotificationIcon::Error);
        state().macro = std::move(*macro);
        toast("Macro loaded", NotificationIcon::Success);
    }

    void onMark(CCObject*) {
        auto& st = state();
        st.markers.add(st.curFrame);
        toast(("Marked frame " + std::to_string(st.curFrame)).c_str(), NotificationIcon::Success);
    }

public:
    static PrismHubPopup* create() {
        auto* popup = new PrismHubPopup();
        if (popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class $modify(PrismPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto* menu = CCMenu::create();
        menu->setID("prism-menu"_spr);
        const auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width * 0.5f, win.height * 0.17f});
        auto* item = makeButton("Prism", this, menu_selector(PrismPauseLayer::onPrism));
        menu->addChild(item);
        this->addChild(menu);
    }

    void onPrism(CCObject*) { PrismHubPopup::create()->show(); }
};
