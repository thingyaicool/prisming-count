#pragma once
// A tiny deterministic cube-like platformer used only to test the solver.
// Integer physics, so results are exactly reproducible.

#include <memory>
#include <vector>

#include "prism/pathfinder.hpp"

namespace toy {

struct Spike {
    int x0, x1, h;  // dies if x in [x0,x1) and y < h
};
struct Ceiling {
    int x0, x1, y;  // dies if x in [x0,x1) and y >= y
};
struct Level {
    std::vector<Spike> spikes;
    std::vector<Ceiling> ceilings;
    int endX = 600;
};

class Sim : public prism::ISim {
public:
    static constexpr int kSpeed = 5, kJump = 11, kGravity = 1;

    Sim(const Level& lvl, uint32_t startFrame = 0) : lvl_(lvl), frame_(startFrame) {}

    std::unique_ptr<prism::ISim> clone() const override { return std::make_unique<Sim>(*this); }

    void step(prism::InputState in) override {
        if (dead_ || fin_) return;
        if (in.held[0] && y_ == 0) vy_ = kJump;
        y_ += vy_;
        vy_ -= kGravity;
        if (y_ <= 0) {
            y_ = 0;
            vy_ = 0;
        }
        x_ += kSpeed;
        ++frame_;
        for (const auto& s : lvl_.spikes)
            if (x_ >= s.x0 && x_ < s.x1 && y_ < s.h) dead_ = true;
        for (const auto& c : lvl_.ceilings)
            if (x_ >= c.x0 && x_ < c.x1 && y_ >= c.y) dead_ = true;
        if (!dead_ && x_ >= lvl_.endX) fin_ = true;
    }

    bool dead() const override { return dead_; }
    bool finished() const override { return fin_; }
    double progress() const override { return x_; }
    uint64_t stateHash() const override {
        using prism::detail::combine;
        uint64_t h = combine(0, static_cast<uint64_t>(x_));
        h = combine(h, static_cast<uint64_t>(y_));
        h = combine(h, static_cast<uint64_t>(vy_ + 1000));
        return h;
    }
    uint32_t frame() const override { return frame_; }

    // For section tests: same physical state, new finish line.
    void retarget(int newEndX) {
        lvl_.endX = newEndX;
        fin_ = false;
    }
    int x() const { return x_; }

private:
    Level lvl_;
    int x_ = 0, y_ = 0, vy_ = 0;
    uint32_t frame_;
    bool dead_ = false, fin_ = false;
};

}  // namespace toy
