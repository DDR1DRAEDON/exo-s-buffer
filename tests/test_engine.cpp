#include <pb/Engine.hpp>
#ifdef NDEBUG
#undef NDEBUG // Test assertions must also run in Release builds.
#endif
#include <cassert>
#include <iostream>
#include <vector>
#include <random>

using Event = std::pair<int, bool>;
int main() {
    using namespace pb;
    {
        auto decision = Predictor::choose({65, 8, 8, true}, [](int delay, int horizon) -> Forecast {
            return {delay == 0 ? 12 : delay >= 3 ? horizon : 14, true};
        });
        assert(decision.delay == 3); // Smallest safe candidate, no excess lag.
        assert(decision.safe);
        assert(decision.probes <= 8);
    }
    {
        int called = 0;
        auto decision = Predictor::choose({65, 16, 8, true}, [&](int, int h) -> Forecast { ++called; return {h, true}; });
        assert(decision.delay == 0 && called == 1); // Safe immediate actions require one probe.
    }
    {
        auto decision = Predictor::choose({65, 8, 8, true}, [](int d, int) -> Forecast { return {d ? 65 : 10, d == 0}; });
        assert(decision.delay == 0 && decision.uncertain); // Unknown simulation -> raw.
    }
    {
        // A wave candidate reaching the horizon but improving only 1 tick is
        // NOT a justified turn delay.
        auto decision = Predictor::choose({65, 8, 8, true}, [](int d, int h) -> Forecast {
            return {d ? h : h - 1, true};
        });
        assert(decision.delay == 0);
    }
    {
        std::vector<Event> events;
        auto emit = [&](int player, bool value) { events.emplace_back(player, value); };
        InputQueue queue;
        queue.input(0, true, 100, 4, emit);
        queue.input(0, false, 101, 0, emit);
        queue.input(0, true, 102, 0, emit);
        queue.input(0, false, 103, 0, emit);
        assert(events.empty());
        queue.tick(104, emit);
        queue.tick(107, emit);
        assert(events == (std::vector<Event>{{0,true},{0,false},{0,true},{0,false}}));
        assert(!queue.holding(0));
    }
    {
        std::vector<Event> events;
        auto emit = [&](int player, bool value) { events.emplace_back(player, value); };
        InputQueue queue;
        queue.input(0, true, 1, 4, emit);
        queue.input(1, true, 1, 0, emit);
        assert(events == (std::vector<Event>{{1,true}}));
        queue.tick(5, emit);
        assert(events == (std::vector<Event>{{1,true},{0,true}}));
        queue.input(0, false, 6, 4, emit);
        queue.reconcile(emit);
        assert(events.back() == Event(0, false));
        assert(queue.pending(0) == 0);
        queue.reset();
        assert(!queue.holding(0));
    }
    {
        // Every physical edge must reach the game in order, even with fast spam.
        InputQueue queue;
        std::mt19937 rng(12345);
        std::vector<Event> expected, actual;
        auto emit = [&](int player, bool value) { actual.emplace_back(player, value); };
        bool state[2] = {false, false};
        for (std::uint64_t tick = 0; tick < 10000; ++tick) {
            int player = rng() % 2;
            state[player] = !state[player];
            expected.emplace_back(player, state[player]);
            queue.input(player, state[player], tick, rng() % 17, emit);
            queue.tick(tick, emit);
        }
        queue.tick(10100, emit);
        // Interleaving across players can differ; ordering per player must never change.
        for (int player = 0; player < 2; ++player) {
            std::vector<bool> actualLane, expectedLane;
            for (auto [p,v]: actual) if (p == player) actualLane.push_back(v);
            for (auto [p,v]: expected) if (p == player) expectedLane.push_back(v);
            assert(actualLane == expectedLane);
            assert(queue.pending(player) == 0);
        }
    }
    {
        // Prediction always respects the simulation budget, even when the
        // outcome is ambiguous and none of the delayed candidates is helpful.
        for (int maxProbes = 1; maxProbes <= 12; ++maxProbes) {
            int calls = 0;
            auto d = Predictor::choose({120, 16, maxProbes, true}, [&](int delay, int) -> Forecast {
                ++calls;
                return {20 + delay, true};
            });
            assert(d.probes == calls);
            assert(d.probes <= maxProbes);
            assert(d.delay == 0);
        }
    }
    {
        // No artificial latency if the future projection is untrusted.
        int calls = 0;
        auto d = Predictor::choose({65, 8, 8, true}, [&](int delay, int) -> Forecast {
            ++calls;
            return {delay == 0 ? 10 : delay == 1 ? 11 : 65, delay != 2};
        });
        assert(d.uncertain && d.delay == 0 && d.probes == calls);
    }
    {
        // Ship/portal changes reconcile the queued state without suppressing
        // a physical release or leaving the button stuck.
        std::vector<Event> events;
        InputQueue q;
        auto emit = [&](int p, bool b) { events.emplace_back(p,b); };
        q.input(0, true, 100, 8, emit);
        q.tick(102, emit);
        assert(events.empty());
        q.reconcile(emit);
        assert(events == (std::vector<Event>{{0, true}}));
        q.input(0, false, 103, 0, emit);
        assert(events == (std::vector<Event>{{0,true},{0,false}}));
        assert(q.pending(0) == 0);
    }
    {
        // A wave gain just below the threshold must not change the input.
        auto decision = Predictor::choose({65, 8, 8, true}, [](int delay, int) -> Forecast {
            return {10 + (delay ? 6 + 2 * delay - 1 : 0), true};
        });
        assert(decision.delay == 0);
    }
    std::cout << "OK: predictor probes, uncertainty, short taps, portal reconciliation, dual lanes, reset and 10k stress.\n";
}
