#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace pb {

// Simulation result is explicitly marked trusted. Unknown behavior must not cause edits.
struct Forecast {
    int survived = 0;       // Number of safe ticks after the candidate action.
    bool trusted = false;   // Set false when the game simulation cannot be reproduced.
};

struct Options {
    int horizon = 64;
    int maxDelay = 8;
    int maxProbes = 8;      // Includes frame-zero probe.
    bool wave = true;
};

struct Decision {
    int delay = 0;
    int baseline = 0;
    int predicted = 0;
    int probes = 0;
    bool safe = false;
    bool uncertain = false;
};

// Pure, deterministic decision engine: evaluator(delay, horizon) is the only game dependency.
class Predictor {
public:
    template<class Eval>
    static Decision choose(Options opt, Eval&& evaluate) {
        opt.horizon = std::clamp(opt.horizon, 1, 240);
        opt.maxDelay = std::clamp(opt.maxDelay, 0, 32);
        opt.maxProbes = std::clamp(opt.maxProbes, 1, 33);
        Decision result;
        const auto first = evaluate(0, opt.horizon);
        result.probes = 1;
        result.baseline = result.predicted = first.survived;
        if (!first.trusted) { result.uncertain = true; return result; }
        if (first.survived >= opt.horizon) { result.safe = true; return result; }
        if (!opt.maxDelay || opt.maxProbes == 1) return result;

        // Evaluate short candidate delays first. Farther delays are progressively sampled,
        // then refined only if a candidate is promising. Avoid O(maxDelay*horizon) scans.
        std::array<bool, 33> evaluated{};
        std::array<Forecast, 33> cache{};
        evaluated[0] = true;
        cache[0] = first;
        auto probe = [&](int d) -> std::optional<Forecast> {
            if (d < 0 || d > opt.maxDelay) return std::nullopt;
            if (evaluated[d]) return cache[d];
            if (result.probes >= opt.maxProbes) return std::nullopt;
            const auto f = evaluate(d, opt.horizon);
            evaluated[d] = true;
            cache[d] = f;
            ++result.probes;
            return f;
        };
        auto qualifies = [&](int d, const Forecast& f) {
            if (!f.trusted) return false;
            // A late input must have a material survival benefit, especially for wave.
            const int requiredGain = opt.wave ? 6 + 2 * d : 10 + d;
            return f.survived - first.survived >= requiredGain;
        };

        int bestDelay = 0, bestSurvival = first.survived;
        int prior = 0;
        for (int d = 1; d <= opt.maxDelay;) {
            auto f = probe(d);
            if (!f) break;
            if (!f->trusted) {
                result.uncertain = true;
                // Fail closed on an untrusted simulator; never change the player's input.
                return result;
            }
            if (qualifies(d, *f)) {
                bestDelay = d;
                bestSurvival = f->survived;
                // Refine the gap so we don't unnecessarily add latency.
                for (int k = prior + 1; k < d; ++k) {
                    auto refined = probe(k);
                    if (!refined) break;
                    if (!refined->trusted) { result.uncertain = true; return Decision{0, first.survived, first.survived, result.probes, false, true}; }
                    if (qualifies(k, *refined)) { bestDelay = k; bestSurvival = refined->survived; break; }
                }
                break;
            }
            if (f->survived > bestSurvival) {
                bestSurvival = f->survived;
                // Only adopt a fallback after meaningful improvement (not tiny rounding drift).
                if (qualifies(d, *f)) bestDelay = d;
            }
            prior = d;
            const int next = (d < 4) ? d + 1 : (d * 2);
            d = std::min(opt.maxDelay, next);
            if (d == prior) break;
        }
        result.delay = bestDelay;
        result.predicted = bestDelay ? bestSurvival : first.survived;
        result.safe = bestDelay && bestSurvival >= opt.horizon;
        return result;
    }
};

struct InputEvent {
    bool pressed = false;
    std::uint64_t dueTick = 0;
};

// Monotonic, per-player input event queue. No loss of short press/release pairs.
// Every event runs no earlier than its due tick and after previously queued events.
class InputQueue {
    static constexpr std::size_t Capacity = 64;
    struct Lane {
        std::array<InputEvent, Capacity> entries{};
        std::size_t head = 0, count = 0;
        bool physical = false, delivered = false;
    };
    std::array<Lane, 2> lanes_{};

    template<class Emit>
    static void deliver(Lane& lane, bool pressed, int player, Emit&& emit) {
        if (lane.delivered != pressed) {
            lane.delivered = pressed;
            emit(player, pressed);
        }
    }
    template<class Emit>
    static void drain(Lane& lane, int player, std::uint64_t tick, Emit&& emit) {
        while (lane.count && lane.entries[lane.head].dueTick <= tick) {
            deliver(lane, lane.entries[lane.head].pressed, player, emit);
            lane.head = (lane.head + 1) % Capacity;
            --lane.count;
        }
    }
public:
    template<class Emit>
    void input(int player, bool pressed, std::uint64_t tick, int delay, Emit&& emit) {
        if (player < 0 || player > 1) return;
        auto& lane = lanes_[player];
        drain(lane, player, tick, emit);
        if (lane.physical == pressed) return; // Suppress repeat key events.
        lane.physical = pressed;
        delay = std::clamp(delay, 0, 32);
        if (!lane.count && delay == 0) {
            deliver(lane, pressed, player, emit);
            return;
        }
        if (lane.count == Capacity) {
            // Overflow is explicit and lossless: flush queued inputs in their original order.
            while (lane.count) {
                const auto next = lane.entries[lane.head];
                deliver(lane, next.pressed, player, emit);
                lane.head = (lane.head + 1) % Capacity;
                --lane.count;
            }
        }
        std::uint64_t due = tick + static_cast<std::uint64_t>(delay);
        if (lane.count) {
            const auto last = lane.entries[(lane.head + lane.count - 1) % Capacity].dueTick;
            due = std::max(due, last + 1); // Hold/release order never collapses.
        }
        lane.entries[(lane.head + lane.count) % Capacity] = {pressed, due};
        ++lane.count;
    }

    template<class Emit>
    void tick(std::uint64_t at, Emit&& emit) {
        for (int i = 0; i < 2; ++i) drain(lanes_[i], i, at, emit);
    }

    template<class Emit>
    void reconcile(Emit&& emit) {
        // On mode switch, pause, or after a reset: discard predictions, restore physical hold.
        for (int i = 0; i < 2; ++i) {
            auto& lane = lanes_[i];
            lane.head = lane.count = 0;
            deliver(lane, lane.physical, i, emit);
        }
    }

    void reset(bool physical = false) {
        for (auto& lane : lanes_) {
            lane.head = lane.count = 0;
            lane.physical = physical;
            lane.delivered = physical;
        }
    }
    bool holding(int player) const { return lanes_[player].delivered; }
    bool physicallyHolding(int player) const { return lanes_[player].physical; }
    std::size_t pending(int player) const { return lanes_[player].count; }
};

} // namespace pb
