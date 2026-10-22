#pragma once
// Macro data model + editor. No game dependencies.
//
// A macro is a list of input events (press/release per player) on absolute
// frames. Canonical form: sorted by (frame, player, release-before-press) and
// strictly alternating press/release per player, starting with a press.
// Every mutation goes through normalize() so the invariant always holds.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace prism {

struct InputEvent {
    uint32_t frame = 0;
    bool down = false;     // true = press, false = release
    uint8_t player = 0;    // 0 = P1, 1 = P2
    bool operator==(const InputEvent&) const = default;
};

struct InputState {
    bool held[2] = {false, false};
    uint8_t mask() const { return static_cast<uint8_t>((held[0] ? 1 : 0) | (held[1] ? 2 : 0)); }
    static InputState fromMask(uint8_t m) {
        InputState s;
        s.held[0] = (m & 1) != 0;
        s.held[1] = (m & 2) != 0;
        return s;
    }
};

class Macro {
public:
    std::vector<InputEvent> events;
    uint8_t initialMask = 0;  // inputs already held when the macro starts (bit0 = P1, bit1 = P2)

    // Restore the canonical-form invariant. Drops redundant events
    // (press while held, release while not held) and invalid players.
    void normalize() {
        std::stable_sort(events.begin(), events.end(), [](const InputEvent& a, const InputEvent& b) {
            if (a.frame != b.frame) return a.frame < b.frame;
            if (a.player != b.player) return a.player < b.player;
            return a.down < b.down;  // release first
        });
        bool held[2] = {(initialMask & 1) != 0, (initialMask & 2) != 0};
        std::vector<InputEvent> out;
        out.reserve(events.size());
        for (const auto& e : events) {
            if (e.player > 1) continue;
            if (held[e.player] == e.down) continue;
            held[e.player] = e.down;
            out.push_back(e);
        }
        events.swap(out);
    }

    // Input state that applies to the step executed on `frame`.
    InputState stateAt(uint32_t frame) const {
        InputState s = InputState::fromMask(initialMask);
        for (const auto& e : events) {
            if (e.frame > frame) break;
            s.held[e.player] = e.down;
        }
        return s;
    }

    InputState finalState() const {
        InputState s = InputState::fromMask(initialMask);
        for (const auto& e : events) s.held[e.player] = e.down;
        return s;
    }

    uint32_t lastFrame() const { return events.empty() ? 0 : events.back().frame; }

    size_t pressCount(int player = -1) const {
        size_t n = 0;
        for (const auto& e : events)
            if (e.down && (player < 0 || e.player == player)) ++n;
        return n;
    }

    std::string serialize() const {
        std::ostringstream o;
        o << "PRISMMACRO 1\n";
        if (initialMask) o << "INIT " << int(initialMask) << '\n';
        for (const auto& e : events) o << e.frame << ' ' << (e.down ? 'D' : 'U') << ' ' << int(e.player) << '\n';
        return o.str();
    }

    // Strict parser: rejects malformed numbers, unknown tokens, out-of-order or
    // non-alternating events.
    static std::optional<Macro> parse(const std::string& text) {
        std::istringstream in(text);
        std::string magic;
        int ver = 0;
        if (!(in >> magic >> ver) || magic != "PRISMMACRO" || ver != 1) return std::nullopt;
        Macro m;
        std::string f, k, p;
        while (in >> f) {
            if (f == "INIT") {
                std::string v;
                unsigned mask = 0;
                if (!(in >> v)) return std::nullopt;
                auto r = std::from_chars(v.data(), v.data() + v.size(), mask);
                if (r.ec != std::errc() || r.ptr != v.data() + v.size() || mask > 3 || !m.events.empty()) return std::nullopt;
                m.initialMask = static_cast<uint8_t>(mask);
                continue;
            }
            if (!(in >> k >> p)) return std::nullopt;
            uint32_t frame = 0;
            unsigned pl = 0;
            auto r1 = std::from_chars(f.data(), f.data() + f.size(), frame);
            if (r1.ec != std::errc() || r1.ptr != f.data() + f.size()) return std::nullopt;
            auto r2 = std::from_chars(p.data(), p.data() + p.size(), pl);
            if (r2.ec != std::errc() || r2.ptr != p.data() + p.size() || pl > 1) return std::nullopt;
            if (k != "D" && k != "U") return std::nullopt;
            m.events.push_back({frame, k == "D", static_cast<uint8_t>(pl)});
        }
        Macro n = m;
        n.normalize();
        if (!(n.events == m.events)) return std::nullopt;
        return m;
    }

    bool operator==(const Macro& o) const { return events == o.events && initialMask == o.initialMask; }
};

// Walks a macro forward in time without rescanning from the start.
// Frames passed to at() must be non-decreasing.
class MacroCursor {
public:
    explicit MacroCursor(const Macro& m) : m_(m), s_(InputState::fromMask(m.initialMask)) {}
    InputState at(uint32_t frame) {
        while (i_ < m_.events.size() && m_.events[i_].frame <= frame) {
            s_.held[m_.events[i_].player] = m_.events[i_].down;
            ++i_;
        }
        return s_;
    }

private:
    const Macro& m_;
    size_t i_ = 0;
    InputState s_;
};

// Editing operations with undo/redo. Every operation either fully applies and
// returns true, or leaves the macro untouched and returns false.
class MacroEditor {
public:
    static constexpr size_t kMaxUndo = 256;

    explicit MacroEditor(Macro m = {}) : cur_(std::move(m)) { cur_.normalize(); }

    const Macro& macro() const { return cur_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    bool addEvent(InputEvent e) {
        if (e.player > 1) return false;
        Macro next = cur_;
        next.events.push_back(e);
        next.normalize();
        if (std::find(next.events.begin(), next.events.end(), e) == next.events.end()) return false;
        if (next == cur_) return false;
        commit(std::move(next));
        return true;
    }

    // Removes a single event, then re-normalizes. Removing a press also drops
    // the now-orphaned release that followed it (and vice versa the next press
    // that becomes redundant), which is what a user expects.
    bool removeEvent(size_t index) {
        if (index >= cur_.events.size()) return false;
        Macro next = cur_;
        next.events.erase(next.events.begin() + static_cast<std::ptrdiff_t>(index));
        next.normalize();
        commit(std::move(next));
        return true;
    }

    // Removes a click: the press at `pressIndex` and its paired release.
    bool removeClick(size_t pressIndex) {
        if (pressIndex >= cur_.events.size() || !cur_.events[pressIndex].down) return false;
        const uint8_t pl = cur_.events[pressIndex].player;
        Macro next = cur_;
        size_t rel = next.events.size();
        for (size_t i = pressIndex + 1; i < next.events.size(); ++i)
            if (next.events[i].player == pl) {
                rel = i;
                break;
            }
        if (rel < next.events.size()) next.events.erase(next.events.begin() + static_cast<std::ptrdiff_t>(rel));
        next.events.erase(next.events.begin() + static_cast<std::ptrdiff_t>(pressIndex));
        next.normalize();
        commit(std::move(next));
        return true;
    }

    // Move an event to a new frame. The event must stay strictly between its
    // same-player neighbours so click order never changes.
    bool moveEvent(size_t index, uint32_t newFrame) {
        if (index >= cur_.events.size()) return false;
        const InputEvent e = cur_.events[index];
        if (e.frame == newFrame) return false;
        for (size_t i = index; i-- > 0;)
            if (cur_.events[i].player == e.player) {
                if (newFrame <= cur_.events[i].frame) return false;
                break;
            }
        for (size_t i = index + 1; i < cur_.events.size(); ++i)
            if (cur_.events[i].player == e.player) {
                if (newFrame >= cur_.events[i].frame) return false;
                break;
            }
        Macro next = cur_;
        next.events[index].frame = newFrame;
        next.normalize();
        if (next.events.size() != cur_.events.size()) return false;
        commit(std::move(next));
        return true;
    }

    // Keep frames in [start, end), shifted so `start` becomes frame 0.
    // Inputs held across a boundary are closed/opened with synthetic events.
    bool trim(uint32_t start, uint32_t end) {
        if (end <= start) return false;
        Macro next;
        const InputState atStart = cur_.stateAt(start);
        for (uint8_t p = 0; p < 2; ++p)
            if (atStart.held[p]) next.events.push_back({0, true, p});
        for (const auto& e : cur_.events)
            if (e.frame > start && e.frame < end) next.events.push_back({e.frame - start, e.down, e.player});
        // events exactly at `start` are already reflected in atStart
        next.normalize();
        const InputState atEnd = next.finalState();
        for (uint8_t p = 0; p < 2; ++p)
            if (atEnd.held[p]) next.events.push_back({end - start, false, p});
        next.normalize();
        if (next == cur_) return false;
        commit(std::move(next));
        return true;
    }

    // Append `tail` so its frame 0 lands on `offset`. Anything held by the head
    // at `offset` is released there first.
    bool stitch(const Macro& tail, uint32_t offset) {
        Macro next;
        next.initialMask = cur_.initialMask;
        for (const auto& e : cur_.events)
            if (e.frame < offset) next.events.push_back(e);
        next.normalize();
        const InputState held = next.finalState();
        for (uint8_t p = 0; p < 2; ++p)
            if (held.held[p]) next.events.push_back({offset, false, p});
        for (const auto& e : tail.events) next.events.push_back({e.frame + offset, e.down, e.player});
        next.normalize();
        if (next == cur_) return false;
        commit(std::move(next));
        return true;
    }

    bool undo() {
        if (undo_.empty()) return false;
        redo_.push_back(std::move(cur_));
        cur_ = std::move(undo_.back());
        undo_.pop_back();
        return true;
    }

    bool redo() {
        if (redo_.empty()) return false;
        undo_.push_back(std::move(cur_));
        cur_ = std::move(redo_.back());
        redo_.pop_back();
        return true;
    }

private:
    void commit(Macro next) {
        undo_.push_back(std::move(cur_));
        if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
        cur_ = std::move(next);
        redo_.clear();
    }

    Macro cur_;
    std::vector<Macro> undo_, redo_;
};

}  // namespace prism
