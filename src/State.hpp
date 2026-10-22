#pragma once
// Shared runtime state for all Prism hooks. One instance, main thread only.

#include <Geode/Geode.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "prism/analysis.hpp"
#include "prism/control.hpp"
#include "prism/macro.hpp"

namespace prism_glue {

// ---- settings helpers (keys are defined in mod.json) -----------------------
inline bool cfgBool(char const* key) { return geode::Mod::get()->getSettingValue<bool>(key); }
inline int64_t cfgInt(char const* key) { return geode::Mod::get()->getSettingValue<int64_t>(key); }
inline double cfgFloat(char const* key) { return geode::Mod::get()->getSettingValue<double>(key); }

enum class MacroMode { Idle, Recording, Playing };

struct State {
    // --- macro ---
    MacroMode mode = MacroMode::Idle;
    prism::Macro macro;      // being recorded, or being played
    size_t playIdx = 0;
    uint32_t playLastFrame = 0;

    // --- step tracking (see GameLayerHooks.cpp) ---
    uint32_t curFrame = 0;       // frame sampled in the most recent processCommands pre-hook
    bool inCommands = false;     // true while the original processCommands runs
    bool injecting = false;      // true while we feed playback inputs to the game

    // --- tools ---
    prism::MarkerList markers;
    prism::LeaderboardGuard guard;

    // --- display ---
    double fpsSmooth = 0.0;
    int appliedFps = 0;

    static State& get() {
        static State s;
        return s;
    }

    bool tpsActive() const { return cfgBool("custom-tps-enabled"); }

    // Re-evaluate which physics-altering features are on and taint the guard.
    void refreshGuard() {
        guard.setActive(prism::Feature::Macro, mode == MacroMode::Playing);
        guard.setActive(prism::Feature::CustomTps, tpsActive());
    }

    void beginAttempt() {
        guard.beginAttempt();
        playIdx = 0;
        playLastFrame = 0;
        curFrame = 0;
        if (mode == MacroMode::Recording) macro = prism::Macro{};
        refreshGuard();
    }
};

inline State& state() { return State::get(); }

// ---- macro files -------------------------------------------------------------

inline std::filesystem::path macroDir() {
    auto dir = geode::Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::string safeName(std::string s) {
    for (auto& c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    return s.empty() ? std::string("unnamed") : s;
}

inline std::filesystem::path macroPathFor(GJGameLevel* level) {
    std::string stem;
    if (level && level->m_levelID.value() != 0)
        stem = "level-" + std::to_string(level->m_levelID.value());
    else
        stem = "local-" + safeName(level ? std::string(level->m_levelName) : std::string());
    return macroDir() / (stem + ".pmacro");
}

inline bool saveMacroFile(std::filesystem::path const& path, prism::Macro const& m) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << m.serialize();
    return static_cast<bool>(f);
}

inline std::optional<prism::Macro> loadMacroFile(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return prism::Macro::parse(ss.str());
}

}  // namespace prism_glue
