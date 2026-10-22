// PredictiveBuffer v5 experimental Geode adapter.
// Independent integration around the tested pb::Predictor and pb::InputQueue.
// TARGET: GD 2.2081 / Geode 5.11.x / Windows x64. Not game-tested.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <pb/Engine.hpp>
#include "DebugConsole.hpp"

using namespace geode::prelude;

namespace {

// Predictors are always speculative: certain effect triggers cannot be replayed
// safely without mutating the live level. Do not claim these are exact replays.
class TrajectoryOracle final {
    std::array<PlayerObject*, 2> ghosts_{nullptr, nullptr};
    bool active_ = false;
    bool dead_ = false;
    bool uncertain_ = false;
    const char* uncertainReason_ = "none";
    float simulationStep_ = 1.f / 240.f;
    bool stepValid_ = true;

    PlayerObject* makeGhost(PlayLayer* layer) {
        if (!layer || !layer->m_objectLayer) return nullptr;
        auto* ghost = PlayerObject::create(1, 1, layer, layer, true);
        if (!ghost) return nullptr;
        ghost->retain();
        ghost->setVisible(false);
        layer->m_objectLayer->addChild(ghost);
        return ghost;
    }

    static void restoreGhost(PlayerObject* ghost, PlayerObject* real) {
        ghost->copyAttributes(real);
        ghost->setPosition(real->getPosition());
        ghost->setScale(real->getScale());
        ghost->setContentSize(real->getContentSize());
        ghost->m_width = real->m_width;
        ghost->m_height = real->m_height;
        ghost->m_isOnGround = real->m_isOnGround;
        ghost->m_isUpsideDown = real->m_isUpsideDown;
        ghost->m_gravityMod = real->m_gravityMod;
        ghost->m_vehicleSize = real->m_vehicleSize;
        ghost->m_playerSpeed = real->m_playerSpeed;
        ghost->setVisible(false);
    }

    static void setButton(PlayerObject* ghost, bool pressed) {
        if (pressed) ghost->pushButton(PlayerButton::Jump);
        else ghost->releaseButton(PlayerButton::Jump);
    }

    static void clearContacts(PlayerObject* ghost) {
        if (ghost->m_collisionLogTop) ghost->m_collisionLogTop->removeAllObjects();
        if (ghost->m_collisionLogBottom) ghost->m_collisionLogBottom->removeAllObjects();
        if (ghost->m_collisionLogLeft) ghost->m_collisionLogLeft->removeAllObjects();
        if (ghost->m_collisionLogRight) ghost->m_collisionLogRight->removeAllObjects();
    }

public:
    bool isActive() const { return active_; }
    void markUncertain(const char* reason = "unsupported-effect") {
        if (active_ && !uncertain_) {
            uncertain_ = true;
            uncertainReason_ = reason;
        }
    }
    const char* lastUncertainty() const { return uncertainReason_; }
    bool ready() const { return ghosts_[0] && ghosts_[1]; }
    bool onDeath(PlayerObject* who) {
        if (!active_) return false;
        if (who != ghosts_[0] && who != ghosts_[1]) return false;
        dead_ = true;
        return true;
    }
    bool frameStep(float delta) {
        // Large/non-finite dt is not safe for speculative physics. Do not
        // reuse a stale prior frame as if the forecast were trustworthy.
        stepValid_ = std::isfinite(delta) && delta >= 0.0005f && delta <= 0.05f;
        if (stepValid_) simulationStep_ = delta;
        return stepValid_;
    }
    void initialize(PlayLayer* layer) {
        shutdown();
        ghosts_[0] = makeGhost(layer);
        ghosts_[1] = makeGhost(layer);
    }
    void shutdown() {
        active_ = false;
        for (auto*& ghost : ghosts_) {
            if (!ghost) continue;
            if (ghost->getParent()) ghost->removeFromParentAndCleanup(true);
            ghost->release();
            ghost = nullptr;
        }
        dead_ = false;
        uncertain_ = false;
        uncertainReason_ = "none";
        stepValid_ = true;
        simulationStep_ = 1.f / 240.f;
    }
    pb::Forecast predict(PlayLayer* level, int lane, bool mirroredDual,
                         bool targetPressed, bool oldPressed,
                         int delay, int horizon) {
        if (!stepValid_ || !level || lane < 0 || lane > 1 || !ghosts_[lane])
            return {0, false};
        auto* original = lane == 0 ? level->m_player1 : level->m_player2;
        if (!original || original->m_isDead) return {0, false};
        const bool second = lane == 0 && mirroredDual && level->m_player2 && ghosts_[1];
        if (mirroredDual && lane == 0 && !second) return {0, false};
        auto* ghost = ghosts_[lane];
        restoreGhost(ghost, original);
        if (second) restoreGhost(ghosts_[1], level->m_player2);
        struct SimScope {
            TrajectoryOracle& self;
            explicit SimScope(TrajectoryOracle& obj) : self(obj) {
                self.dead_ = false;
                self.uncertain_ = false;
                self.uncertainReason_ = "none";
                self.active_ = true;
            }
            ~SimScope() { self.active_ = false; }
        } scope(*this);

        // The pre-input state is exactly the last action actually delivered to the game.
        setButton(ghost, oldPressed);
        if (second) setButton(ghosts_[1], oldPressed);
        for (int t = 0; t < horizon; ++t) {
            if (t == delay) {
                setButton(ghost, targetPressed);
                if (second) setButton(ghosts_[1], targetPressed);
            }
            ghost->update(simulationStep_);
            ghost->updateRotation(simulationStep_);
            if (second) {
                ghosts_[1]->update(simulationStep_);
                ghosts_[1]->updateRotation(simulationStep_);
            }
            if (dead_ || uncertain_) return {t, !uncertain_};
            clearContacts(ghost);
            level->checkCollisions(ghost, simulationStep_, false);
            if (dead_ || uncertain_) return {t, !uncertain_};
            if (second) {
                clearContacts(ghosts_[1]);
                level->checkCollisions(ghosts_[1], simulationStep_, false);
                if (dead_ || uncertain_) return {t, !uncertain_};
            }
        }
        return {horizon, !uncertain_};
    }
};

struct Context {
    TrajectoryOracle oracle;
    pb::InputQueue inputs;
    std::uint64_t tick = 0;
    bool injecting = false;
    bool lastF6 = false;
    bool lastF7 = false;
    bool lastF8 = false;
    bool lastF9 = false;
    bool lastF10 = false;
    bool rawInputMode = false;
    bool wasInvalidDelta = false;
    std::uint64_t inputEdges = 0;
    std::uint64_t deliveredEdges = 0;
    std::uint64_t predictions = 0;
    std::uint64_t delayed = 0;
    std::uint64_t uncertain = 0;
    std::uint64_t totalProbes = 0;
    bool wasShipMode = false;
    int preset = 0;
    int normalTicks = 2;
    int waveTicks = 2;
    bool enabled = true;
    int waveStreak[2]{};
    std::uint64_t prevEdge[2]{};

    void reset(PlayLayer* layer) {
        inputs.reset();
        tick = 0;
        wasShipMode = false;
        lastF6 = lastF7 = lastF8 = lastF9 = lastF10 = false;
        rawInputMode = false;
        wasInvalidDelta = false;
        inputEdges = deliveredEdges = predictions = delayed = uncertain = totalProbes = 0;
        pb::diagnostic::console().write("LIFECYCLE", "PlayLayer initialized; creating trajectory ghosts");
        waveStreak[0] = waveStreak[1] = 0;
        prevEdge[0] = prevEdge[1] = 0;
        oracle.initialize(layer);
        if (!oracle.ready()) {
            pb::diagnostic::console().write("WARN", "Trajectory ghosts could not initialize; predictions will fail closed");
        } else {
            pb::diagnostic::console().write("LIFECYCLE", "Trajectory ghosts initialized");
        }
    }
    void shutdown() {
        printStats("EXIT");
        pb::diagnostic::console().write("LIFECYCLE", "PlayLayer quitting / changing scene");
        inputs.reset();
        oracle.shutdown();
    }
    void printStats(const char* label) {
        pb::diagnostic::console().writef(label,
            "tick=%llu input=%llu delivered=%llu predictions=%llu delayed=%llu uncertain=%llu probes=%llu P1queue=%zu P2queue=%zu preset=%d enabled=%d",
            static_cast<unsigned long long>(tick),
            static_cast<unsigned long long>(inputEdges),
            static_cast<unsigned long long>(deliveredEdges),
            static_cast<unsigned long long>(predictions),
            static_cast<unsigned long long>(delayed),
            static_cast<unsigned long long>(uncertain),
            static_cast<unsigned long long>(totalProbes),
            inputs.pending(0), inputs.pending(1), preset + 1, enabled && !rawInputMode ? 1 : 0);
    }
    void emit(int lane, bool down) {
        auto* layer = PlayLayer::get();
        if (!layer) return;
        ++deliveredEdges;
        if (pb::diagnostic::console().trace())
            pb::diagnostic::console().writef("EMIT", "tick=%llu P%d %s remaining=%zu", 
                static_cast<unsigned long long>(tick), lane + 1, down ? "PRESS" : "RELEASE", inputs.pending(lane));
        struct Guard { bool& flag; explicit Guard(bool& f): flag(f) { flag = true; } ~Guard() {flag = false;} } guard(injecting);
        layer->handleButton(down, 1, lane == 0);
        const bool mirror = lane == 0 && layer->m_gameState.m_isDualMode &&
            (!layer->m_level || !layer->m_level->m_twoPlayerMode);
        if (mirror) layer->handleButton(down, 1, false);
    }
    void setPreset(int index) {
        preset = index % 5;
        constexpr std::array<int, 5> normal{2, 4, 8, 16, 0};
        constexpr std::array<int, 5> wave{2, 4, 8, 8, 0};
        normalTicks = normal[preset];
        waveTicks = wave[preset];
        enabled = preset != 4;
        inputs.reconcile([this](int player, bool down) { emit(player, down); });
        pb::diagnostic::console().writef("STAGE", "F6 -> STAGE %d; normal=%d ticks wave=%d ticks %s",
            preset + 1, normalTicks, waveTicks, enabled ? "ACTIVE" : "RAW INPUT");
        geode::log::info("PB5 stage {} normal={} wave={}", preset + 1, normalTicks, waveTicks);
    }
    void frame(PlayerObject* player, float dt) {
        auto* level = PlayLayer::get();
        if (!level || player != level->m_player1 || oracle.isActive()) return;
        // GD's player dt is normalized to 240 TPS; timewarp modifies its scale.
        const float warp = level->m_gameState.m_timeWarp;
        const float simDt = warp > 0.001f ? dt / warp : dt;
        const bool validDelta = oracle.frameStep(simDt);
        if (!validDelta && !wasInvalidDelta)
            pb::diagnostic::console().writef("WARN", "Invalid prediction delta dt=%f warp=%f; passing inputs through", dt, warp);
        if (validDelta && wasInvalidDelta)
            pb::diagnostic::console().write("RECOVERY", "Prediction delta valid again");
        wasInvalidDelta = !validDelta;
        ++tick;
        // No delayed wave/cube edge should spill into ship/dual-ship gameplay.
        const bool shipMode = (level->m_player1 && level->m_player1->m_isShip) ||
            (level->m_gameState.m_isDualMode && level->m_player2 && level->m_player2->m_isShip);
        if (shipMode && !wasShipMode) {
            pb::diagnostic::console().write("MODE", "Ship portal detected; flushing pending inputs");
            inputs.reconcile([this](int p, bool down) { emit(p, down); });
        }
        wasShipMode = shipMode;
        const bool nowF6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        if (nowF6 && !lastF6) setPreset((preset + 1) % 5);
        lastF6 = nowF6;
        const bool nowF7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (nowF7 && !lastF7) pb::diagnostic::console().toggleTrace();
        lastF7 = nowF7;
        const bool nowF8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (nowF8 && !lastF8) pb::diagnostic::console().toggleVisible();
        lastF8 = nowF8;
        const bool nowF9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (nowF9 && !lastF9) printStats("MANUAL-STATS");
        lastF9 = nowF9;
        const bool nowF10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (nowF10 && !lastF10) {
            rawInputMode = !rawInputMode;
            if (rawInputMode) {
                inputs.reconcile([this](int p, bool down) { emit(p, down); });
                pb::diagnostic::console().write("SAFE", "F10 -> RAW INPUT ON; prediction/queuing bypassed (release jump before toggling)");
            } else {
                inputs.reset();
                pb::diagnostic::console().write("SAFE", "F10 -> BUFFER ON; prediction/queuing enabled (release jump before toggling)");
            }
        }
        lastF10 = nowF10;
        inputs.tick(tick, [this](int p, bool down) { emit(p, down); });
        if (tick % 960 == 0) printStats("STATS");
    }
    void onRealDeath() {
        // The scene resets its own button state on death. No stale buffered edge
        // should execute after the next attempt starts.
        pb::diagnostic::console().write("DEATH", "Real player died; clearing queued inputs");
        inputs.reset();
        wasShipMode = false;
    }

    bool intercept(bool down, int lane) {
        auto* level = PlayLayer::get();
        if (!level || oracle.isActive() || injecting || rawInputMode) return false;
        if (lane == 1 && (!level->m_level || !level->m_level->m_twoPlayerMode)) {
            // Do not swallow ordinary P2 events in non-dual levels. In automatic
            // 1P dual, suppress the game's extra P2 edge only while P1 is pending.
            return level->m_gameState.m_isDualMode && inputs.pending(0) != 0;
        }
        auto* p = lane == 0 ? level->m_player1 : level->m_player2;
        if (!p || p->m_isDead) return false;
        ++inputEdges;
        if (pb::diagnostic::console().trace())
            pb::diagnostic::console().writef("INPUT", "tick=%llu P%d %s hold=%d pending=%zu", 
                static_cast<unsigned long long>(tick), lane + 1, down ? "PRESS" : "RELEASE",
                inputs.holding(lane) ? 1 : 0, inputs.pending(lane));
        const bool isWave = p->m_isDart;
        const bool isShip = p->m_isShip;
        const bool isRobot = p->m_isRobot;
        const bool mirror = lane == 0 && level->m_gameState.m_isDualMode &&
            (!level->m_level || !level->m_level->m_twoPlayerMode);
        const bool mixedShip = mirror && level->m_player2 && level->m_player2->m_isShip;
        const bool mixedWave = mirror && level->m_player2 && level->m_player2->m_isDart;
        int delay = 0;
        if (isShip || mixedShip) {
            // On the exact portal transition, do not strand an old queued edge.
            // Both press and release are emitted immediately in ship.
            if (inputs.pending(lane))
                inputs.reconcile([this](int p, bool pressed) { emit(p, pressed); });
        }
        if (isWave || mixedWave) {
            waveStreak[lane] = prevEdge[lane] && tick - prevEdge[lane] < 8
                ? waveStreak[lane] + 1 : 0;
        } else waveStreak[lane] = 0;
        prevEdge[lane] = tick;
        if (enabled && !isShip && !mixedShip && inputs.pending(lane) == 0 &&
            waveStreak[lane] < 3 && (down || isWave || mixedWave || isRobot)) {
            const bool mini = (isWave && p->m_vehicleSize < 1.f) ||
                (mixedWave && level->m_player2 && level->m_player2->m_vehicleSize < 1.f);
            const int horizon = mini ? 50 : ((isWave || mixedWave) ? 65 : 120);
            const int maxDelay = isWave || mixedWave ? waveTicks : normalTicks;
            pb::Options options{horizon, maxDelay, 8, isWave || mixedWave};
            const bool oldHold = inputs.holding(lane);
            const auto choice = pb::Predictor::choose(options, [&](int candidate, int window) {
                return oracle.predict(level, lane, mirror, down, oldHold, candidate, window);
            });
            ++predictions;
            totalProbes += choice.probes;
            if (choice.uncertain) ++uncertain;
            delay = choice.delay;
            if (delay > 0) {
                ++delayed;
                pb::diagnostic::console().writef("PREDICTED",
                    "tick=%llu P%d %s %s delay=%d forecast=%d->%d / horizon=%d probes=%d [NOT VERIFIED SAVE]",
                    static_cast<unsigned long long>(tick), lane + 1,
                    down ? "PRESS" : "RELEASE", (isWave || mixedWave) ? "WAVE" : "NORMAL",
                    delay, choice.baseline, choice.predicted, horizon, choice.probes);
            }
            if (pb::diagnostic::console().trace() || choice.uncertain) {
                pb::diagnostic::console().writef(choice.uncertain ? "UNCERTAIN" : "FORECAST",
                    "tick=%llu P%d delay=%d baseline=%d predicted=%d probes=%d uncertain=%d reason=%s",
                    static_cast<unsigned long long>(tick), lane + 1, delay,
                    choice.baseline, choice.predicted, choice.probes,
                    choice.uncertain ? 1 : 0, oracle.lastUncertainty());
            }
        }
        inputs.input(lane, down, tick, delay,
                     [this](int who, bool pressed) { emit(who, pressed); });
        if (pb::diagnostic::console().trace()) {
            pb::diagnostic::console().writef("QUEUE", "tick=%llu P%d newDelay=%d pending=%zu",
                static_cast<unsigned long long>(tick), lane + 1, delay, inputs.pending(lane));
        }
        return true; // Consumed. The queue owns delivery, including delay=0.
    }
};
Context g;

} // namespace

$on_mod(Loaded) {
    pb::diagnostic::console().init();
    pb::diagnostic::console().write("BOOT", "PredictiveBuffer V5 diagnostic build v0.3.0 loaded, Win64/GD 2.2081");
    pb::diagnostic::console().write("BOOT", "F10 = RAW INPUT BYPASS for A/B troubleshooting (release jump first)");
    pb::diagnostic::console().write("BOOT", "IMPORTANT: forecast is approximate; predicted save is not proof of a real save");
    geode::log::info("PredictiveBuffer V5 diagnostic build loaded; log: %LOCALAPPDATA%/PredictiveBufferV5/debug.log");
}

class $modify(PB5Play, PlayLayer) {
    bool init(GJGameLevel* level, bool replay, bool noObjects) {
        if (!PlayLayer::init(level, replay, noObjects)) return false;
        g.reset(this);
        return true;
    }
    void destroyPlayer(PlayerObject* player, GameObject* cause) {
        if (g.oracle.onDeath(player)) return;
        g.onRealDeath();
        PlayLayer::destroyPlayer(player, cause);
    }
    void onQuit() {
        g.shutdown();
        PlayLayer::onQuit();
    }
    void flipGravity(PlayerObject* player, bool flip, bool flipSprites) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("gravity-or-spider-or-ring"); return;}
        PlayLayer::flipGravity(player, flip, flipSprites);
    }
    void playEndAnimationToPos(cocos2d::CCPoint where) {
        if (g.oracle.isActive()) return;
        PlayLayer::playEndAnimationToPos(where);
    }
};

class $modify(PB5Editor, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool unknown) {
        g.shutdown();
        return LevelEditorLayer::init(level, unknown);
    }
};

class $modify(PB5Player, PlayerObject) {
    void update(float dt) {
        if (!g.oracle.isActive()) g.frame(this, dt);
        PlayerObject::update(dt);
    }
    void incrementJumps() {
        if (g.oracle.isActive()) return;
        PlayerObject::incrementJumps();
    }
    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("gravity-or-spider-or-ring"); return;}
        PlayerObject::playSpiderDashEffect(from, to);
    }
    void ringJump(RingObject* ring, bool pressed) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("gravity-or-spider-or-ring"); return;}
        PlayerObject::ringJump(ring, pressed);
    }
};

class $modify(PB5Layer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        if (button == 1 && PlayLayer::get() && !g.injecting && !g.oracle.isActive() &&
            g.intercept(down, player1 ? 0 : 1)) return;
        GJBaseGameLayer::handleButton(down, button, player1);
    }
    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* effect) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("effect-activation"); return false;}
        return GJBaseGameLayer::canBeActivatedByPlayer(player, effect);
    }
    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("gravity-or-spider-or-ring"); return;}
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects,
                               int count, float dt) {
        if (!g.oracle.isActive()) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
            return;
        }
        gd::vector<GameObject*> solids;
        solids.reserve(count);
        for (int i = 0; i < count; ++i) {
            GameObject* object = objects->at(i);
            if (!object) continue;
            auto type = object->m_objectType;
            if (type == GameObjectType::Solid || type == GameObjectType::Hazard ||
                type == GameObjectType::AnimatedHazard ||
                type == GameObjectType::CollisionObject ||
                type == GameObjectType::Slope) {
                solids.push_back(object);
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &solids, solids.size(), dt);
    }
};

class $modify(PB5Effects, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int value, const gd::vector<int>* ids) {
        if (g.oracle.isActive()) {g.oracle.markUncertain("gravity-or-spider-or-ring"); return;}
        EffectGameObject::triggerObject(layer, value, ids);
    }
};
class $modify(PB5Objects, GameObject) {
    void playShineEffect() {
        if (g.oracle.isActive()) return;
        GameObject::playShineEffect();
    }
};
class $modify(PB5Streak, HardStreak) {
    void addPoint(cocos2d::CCPoint at) {
        if (g.oracle.isActive()) return;
        HardStreak::addPoint(at);
    }
};
