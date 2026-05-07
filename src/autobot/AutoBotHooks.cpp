// ============================================================================
// AutoBotHooks.cpp — Hook integration layer for the autonomous play system
//
// This file contains ONLY the Geode hook definitions that bridge the AutoBot
// engine into GD's runtime.  All prediction/decision logic lives in
// AutoBot.hpp so that the hooking layer remains thin and maintainable.
//
// Hooks:
//   PlayLayer::resetLevel             — reset the bot's internal state on retry
//   PlayLayer::onExit                 — level lifecycle cleanup
//
// Compatible with Geode v4.x / GD 2.206+
// ============================================================================

#include "AutoBot.hpp"
#include "../zBot.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
using namespace geode::prelude;

// ---- PlayLayer::resetLevel — reset bot state on level retry ---------------
//
// When the player dies and the level restarts (or when loading a checkpoint),
// we need to reset the bot's held-button tracking so it doesn't carry stale
// state into the new attempt.

class $modify(ABPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();

        autobot::AutoBot::get()->resetState();

        // Nothing extra needed — the bot re-scans every tick and the
        // m_holding flag is implicitly corrected by the next tick() call.
        // However, if the player was holding a button from the bot, we
        // release it to avoid a stuck input.
        if (zBot::get()->autoBotEnabled) {
            this->GJBaseGameLayer::handleButton(false, static_cast<int>(PlayerButton::Jump), true);
            autobot::AutoBot::get()->warmupLevel(this, 1.f / 240.f);
        }
    }

    void startGame() {
        PlayLayer::startGame();

        if (zBot::get()->autoBotEnabled) {
            autobot::AutoBot::get()->invalidateLevelCache();
            autobot::AutoBot::get()->resetState();
            autobot::AutoBot::get()->warmupLevel(this, 1.f / 240.f);
        }
    }

    void startGameDelayed() {
        PlayLayer::startGameDelayed();

        if (zBot::get()->autoBotEnabled) {
            autobot::AutoBot::get()->invalidateLevelCache();
            autobot::AutoBot::get()->resetState();
            autobot::AutoBot::get()->warmupLevel(this, 1.f / 240.f);
        }
    }

    void onExit() {
        autobot::AutoBot::get()->invalidateLevelCache();
        autobot::AutoBot::get()->resetState();

        if (zBot::get()->autoBotEnabled) {
            this->GJBaseGameLayer::handleButton(false, static_cast<int>(PlayerButton::Jump), true);
        }

        PlayLayer::onExit();
    }
};
