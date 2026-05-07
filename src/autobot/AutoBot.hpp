#ifndef _autobot_hpp
#define _autobot_hpp

#include <Geode/Geode.hpp>

#include "AutoBotDebug.hpp"
#include "AutoBotModel.hpp"
#include "AutoBotPlanner.hpp"
#include "../zBot.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace autobot {

class AutoBot {
public:
    static AutoBot* get() {
        static AutoBot* instance = new AutoBot();
        return instance;
    }

    void tick(GJBaseGameLayer* gl, float dt) {
        m_tickCounter++;

        if (!gl) return;

        auto* play = PlayLayer::get();
        if (!play || static_cast<GJBaseGameLayer*>(play) != gl) return;
        if (geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(play))
         || geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(gl))) {
            return;
        }

        auto* player = gl->m_player1;
        if (!player || geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(player))) {
            return;
        }

        if (player->m_isDead) {
            resetState();
            return;
        }

        ensureLevelCache(gl);
        if (!m_levelCache.isBuilt()) {
            m_lastDecisionReason = "level-cache-unavailable";
            return;
        }

        PlayerSnapshot snapshot = m_stateReader.capture(gl, player, m_holding);
        auto config = currentPlannerConfig();
        collectNearbyObjects(snapshot, config);

        PlannerDecision decision = decide(snapshot, config);
        m_lastDecision = decision;
        m_lastDecisionReason = decision.reason;

        if (zBot::get()->autoBotFileLogging) {
            std::ostringstream line;
            line << "frame=" << snapshot.frame
                 << " mode=" << gameModeName(snapshot.mode)
                 << " x=" << snapshot.x
                 << " y=" << snapshot.y
                 << " xSpeed=" << snapshot.observedXSpeed
                 << " yVel=" << snapshot.yVelocity
                 << " onGround=" << snapshot.onGround
                 << " gravFlip=" << snapshot.gravFlip
                 << " plannerEnabled=" << config.rollingPlannerEnabled
                 << " horizonSeconds=" << config.horizonSeconds
                 << " cacheObjects=" << m_levelCache.size()
                 << " nearbyObjects=" << m_nearbyObjects.size()
                 << " candidates=" << decision.candidateCount
                 << " collision=" << collisionApproximationName(decision.collisionApproximation)
                 << " reason=" << decision.reason;
            AutoBotDebug::get()->logEvent("tick", line.str());
        }

        applyDecision(gl, snapshot, decision.shouldPress);
    }

    void resetState() {
        m_holding = false;
        m_holdFramesRemaining = 0;
        m_nearbyObjects.clear();
        m_stateReader.reset();
        m_planner.reset();
        m_lastDecision = {};
        m_lastDecisionReason = "reset-state";

        if (zBot::get()->autoBotFileLogging) {
            AutoBotDebug::get()->logEvent("reset-state", "holding=0 runtime-state-reset");
        }
    }

    void invalidateLevelCache() {
        m_levelCache.invalidate();
    }

    void warmupLevel(GJBaseGameLayer* gl, float dt = 0.f) {
        (void)dt;
        if (!gl || !gl->m_player1 || gl->m_player1->m_isDead) {
            return;
        }

        ensureLevelCache(gl);
        auto snapshot = m_stateReader.capture(gl, gl->m_player1, m_holding);
        collectNearbyObjects(snapshot, currentPlannerConfig());
    }

    std::string const& getLastDecisionReason() const {
        return m_lastDecisionReason;
    }

    size_t getCachedObjectCount() const {
        return m_levelCache.size();
    }

    size_t getNearbyObjectCount() const {
        return m_nearbyObjects.size();
    }

private:
    bool m_holding = false;
    int m_holdFramesRemaining = 0;
    size_t m_tickCounter = 0;
    std::string m_lastDecisionReason = "startup";
    PlannerDecision m_lastDecision;
    StateReader m_stateReader;
    LevelObjectCache m_levelCache;
    RollingLookaheadPlanner m_planner;
    std::vector<CachedLevelObject const*> m_nearbyObjects;

    PlannerConfig currentPlannerConfig() const {
        PlannerConfig config;
        config.rollingPlannerEnabled = zBot::get()->autoBotUsePlanner;
        config.experimentalMultiMode = zBot::get()->autoBotExperimentalMultiMode;
        config.useApproximateCollisionFallback = zBot::get()->autoBotApproximateCollisionFallback;
        config.horizonSeconds = zBot::get()->autoBotPlannerHorizonSeconds;
        config.ticksPerSecond = static_cast<float>(zBot::get()->tps);
        config.cubeTimingSafetyTicks = zBot::get()->autoBotCubeTimingSafetyTicks;
        return config;
    }

    void ensureLevelCache(GJBaseGameLayer* gl) {
        if (m_levelCache.isBuilt()) {
            return;
        }

        if (m_levelCache.build(gl, zBot::get()->autoBotLogUnknownObjects) && zBot::get()->autoBotFileLogging) {
            std::ostringstream line;
            line << "objects=" << m_levelCache.size();
            AutoBotDebug::get()->logEvent("level-cache", line.str());
        }
    }

    void collectNearbyObjects(PlayerSnapshot const& snapshot, PlannerConfig const& config) {
        int horizonTicks = std::clamp(
            static_cast<int>(std::round(config.ticksPerSecond * std::max(config.horizonSeconds, 0.5f))),
            45,
            360
        );
        float lookahead = std::max(snapshot.observedXSpeed * static_cast<float>(horizonTicks) + 240.f, 480.f);
        m_levelCache.queryRange(snapshot.x - 80.f, snapshot.x + lookahead, m_nearbyObjects);
    }

    PlannerDecision decide(PlayerSnapshot const& snapshot, PlannerConfig const& config) {
        switch (snapshot.mode) {
            case GameMode::Cube:
                return m_planner.planCube(snapshot, m_nearbyObjects, config);

            default:
                return m_planner.planUnsupported(snapshot, config);
        }
    }

    void applyDecision(GJBaseGameLayer* gl, PlayerSnapshot const& snapshot, bool shouldPress) {
        if (shouldPress && !m_holding) {
            m_holdFramesRemaining = desiredHoldFrames(snapshot.mode);
            gl->handleButton(true, static_cast<int>(PlayerButton::Jump), true);
            m_holding = true;
            return;
        }

        if (!shouldPress && m_holding) {
            if (m_holdFramesRemaining > 0) {
                --m_holdFramesRemaining;
                return;
            }

            gl->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
            m_holding = false;
            m_holdFramesRemaining = 0;
        }
    }

    static int desiredHoldFrames(GameMode mode) {
        switch (mode) {
            case GameMode::Cube:
                return 0;
            case GameMode::Robot:
                return 4;
            default:
                return 1;
        }
    }
};

} // namespace autobot

#endif
