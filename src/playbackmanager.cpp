// ============================================================================
// playbackmanager.cpp — Replay playback hooks
//
// Compatible with Geode v4.x / GD 2.206+
// Uniquely-named $modify classes to avoid ODR clashes with recordmanager.cpp
// and clickbetweenframescompat.cpp which also hook PlayLayer/GJBaseGameLayer.
// ============================================================================

#include "zBot.hpp"
#include "replay.hpp"
#include "autobot/AutoBot.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
using namespace geode::prelude;

static int autoBotProcessLogCounter = 0;

// ---------- GJBaseGameLayer::processCommands — inject replay inputs ----------

class $modify(PBGJBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float delta, bool isHalfTick, bool isLastTick) {
        zBot* mgr = zBot::get();
        auto* play = PlayLayer::get();
        bool activePlayLayer = play && static_cast<GJBaseGameLayer*>(play) == this;
        bool activeLayerLocked = geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(this));
        bool playLayerLocked = play && geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(play));
        bool autoBotDriving = mgr->autoBotEnabled && mgr->state != PLAYBACK && activePlayLayer && !activeLayerLocked && !playLayerLocked;

        if (autoBotDriving) {
            if ((autoBotProcessLogCounter++ % 30) == 0) {
                log::info(
                    "[AutoBot] processCommands pre | frame={} delta={:.6f} halfTick={} lastTick={} planner={} replayLoaded={}",
                    m_gameState.m_currentProgress,
                    delta,
                    isHalfTick,
                    isLastTick,
                    mgr->autoBotUsePlanner,
                    mgr->currentReplay != nullptr
                );
            }

            autobot::AutoBot::get()->tick(this, delta);
        }

        // When the bot is driving, suppress real player input
        if (!zBot::get()->ignoreInput) {
            GJBaseGameLayer::processCommands(delta, isHalfTick, isLastTick);
        }

        if (mgr->state == RECORD && mgr->currentReplay && activePlayLayer) {
            mgr->capturePeriodicReplayPositionSample(this, "processCommands");
        }

        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            if (activePlayLayer && !mgr->playbackState.playbackSeekInitialized) {
                mgr->preparePlaybackFromCurrentPlayerState(play, "processCommands-auto-init");
            }

            int replayTimelineFrame = mgr->getPlaybackTimelineFrame(m_gameState.m_currentProgress);

            // Feed replay inputs whose frame stamp has been reached
            while (mgr->playbackState.currentInputIndex < mgr->currentReplay->inputs.size() &&
                   mgr->currentReplay->inputs[mgr->playbackState.currentInputIndex].frame <= replayTimelineFrame) {

                auto input = mgr->currentReplay->inputs[mgr->playbackState.currentInputIndex++];
                GJBaseGameLayer::handleButton(input.down, input.button, !input.player2);
            }

            // Pre-fire click sounds slightly ahead so audio feels responsive
            int offset = static_cast<int>(mgr->currentReplay->framerate * 0.1f);
            while (mgr->playbackState.clickbotInputIndex < mgr->currentReplay->inputs.size() &&
                   mgr->currentReplay->inputs[mgr->playbackState.clickbotInputIndex].frame <= replayTimelineFrame + offset) {

                auto click = mgr->currentReplay->inputs[mgr->playbackState.clickbotInputIndex++];
                mgr->playSound(click.player2, click.button, click.down);
            }
        }

        // Autonomous bot mode shares the same input hook point as replay
        // playback so that only one processCommands hook drives button events.
        if (mgr->autoBotEnabled && mgr->state != PLAYBACK && (!activePlayLayer || activeLayerLocked || playLayerLocked) && (autoBotProcessLogCounter++ % 120) == 0) {
            log::warn(
                "[AutoBot] processCommands skipped | activePlayLayer={} layerLocked={} playLayerLocked={} frame={}",
                activePlayLayer,
                activeLayerLocked,
                playLayerLocked,
                m_gameState.m_currentProgress
            );
        } else if (mgr->autoBotEnabled && mgr->state == PLAYBACK && (autoBotProcessLogCounter++ % 120) == 0) {
            log::warn("[AutoBot] Enabled, but replay playback state is active. Autonomous input is skipped.");
        }
    }
};

// ---------- PlayLayer::resetLevel — resync replay cursors -------------------

class $modify(PBPlayLayer, PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreateObjects) {
        bool result = PlayLayer::init(lvl, useReplay, dontCreateObjects);

        zBot* mgr = zBot::get();
        if (result && mgr->state == PLAYBACK && mgr->currentReplay) {
            mgr->preparePlaybackFromCurrentPlayerState(this, "init");
        }

        return result;
    }

    void startGame() {
        PlayLayer::startGame();

        zBot* mgr = zBot::get();
        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            mgr->preparePlaybackFromCurrentPlayerState(this, "startGame");
        }
    }

    void startGameDelayed() {
        PlayLayer::startGameDelayed();

        zBot* mgr = zBot::get();
        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            mgr->preparePlaybackFromCurrentPlayerState(this, "startGameDelayed");
        }
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);

        zBot* mgr = zBot::get();
        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            mgr->preparePlaybackFromCurrentPlayerState(this, checkpoint ? "loadFromCheckpoint" : "loadFromCheckpoint-null");
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        zBot* mgr = zBot::get();
        if (mgr->state != PLAYBACK || !mgr->currentReplay) return;

        mgr->tps = mgr->currentReplay->framerate;
        mgr->preparePlaybackFromCurrentPlayerState(this, "resetLevel");
    }

    void onExit() {
        zBot* mgr = zBot::get();
        if (mgr->state == PLAYBACK) {
            mgr->clearPlaybackHeldInputs(this);
            mgr->resetPlaybackState();
        }

        PlayLayer::onExit();
    }
};
