// ============================================================================
// physicsbypass.cpp — TPS override, speedhack, and frame advance hooks
//
// Compatible with Geode v4.x / GD 2.206+
// Uses uniquely-named $modify classes to avoid ODR conflicts with other
// translation units that also hook the same GD classes.
// ============================================================================

#include "zBot.hpp"
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
using namespace geode::prelude;

// ---------- PlayLayer: skip updateVisibility when rendering is disabled ------

class $modify(PhysPlayLayer, PlayLayer) {
    void updateVisibility(float dt) {
        if (zBot::get()->disableRender) return;
        PlayLayer::updateVisibility(dt);
    }
};

// ---------- CCScheduler: apply speedhack multiplier -------------------------

class $modify(PhysCCScheduler, CCScheduler) {
    void update(float dt) {
        zBot* mgr = zBot::get();
        CCScheduler::update(dt * static_cast<float>(mgr->speed));
    }
};

// ---------- GJBaseGameLayer: TPS lock + frame advance -----------------------

class $modify(PhysGJBGL, GJBaseGameLayer) {
    void update(float dt) {
        zBot* mgr = zBot::get();

        mgr->extraTPS += dt;
        float newDelta = 1.f / static_cast<float>(mgr->tps);

        // Frame advance mode — step exactly one tick on demand
        if (mgr->frameAdvance) {
            mgr->extraTPS = 0;
            if (!mgr->doAdvance) return;
            mgr->doAdvance = false;
            return GJBaseGameLayer::update(newDelta);
        }

        if (mgr->extraTPS >= newDelta) {
            int times = static_cast<int>(std::floor(mgr->extraTPS / newDelta));
            mgr->extraTPS -= newDelta * times;

            // Render only the last sub-step to save GPU time
            mgr->disableRender = true;
            for (int i = 0; i < times - 1; i++) {
                GJBaseGameLayer::update(newDelta);
            }
            mgr->disableRender = false;
            return GJBaseGameLayer::update(newDelta);
        }
    }

    double getModifiedDelta(float dt) {
        // Call original to let GD do internal bookkeeping, then override
        GJBaseGameLayer::getModifiedDelta(dt);

        zBot* mgr = zBot::get();
        return 1.0 / mgr->tps;
    }
};
