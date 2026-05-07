// ============================================================================
// recordmanager.cpp — Replay recording hooks
//
// Compatible with Geode v4.x / GD 2.206+
// Uniquely-named $modify classes to avoid ODR clashes.
// ============================================================================

#include "zBot.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
using namespace geode::prelude;

// ---------- GJBaseGameLayer::handleButton — capture inputs ------------------

class $modify(RecGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool p1) {
        GJBaseGameLayer::handleButton(down, button, p1);

        zBot* mgr = zBot::get();
        if (mgr->state != RECORD || !mgr->currentReplay) return;

        auto* play = PlayLayer::get();
        if (play && static_cast<GJBaseGameLayer*>(play) == this && !mgr->recordingState.recordingOffsetInitialized) {
            mgr->prepareRecordingFromCurrentPlayerState(play, "handleButton-auto-init", false);
        }

        bool p2 = !p1
            && m_levelSettings->m_twoPlayerMode
            && m_gameState.m_isDualMode;

        int replayFrame = std::max(0, mgr->getRecordingTimelineFrame(m_gameState.m_currentProgress));
        mgr->currentReplay->addInput(replayFrame, button, p2, down);

        if (play && static_cast<GJBaseGameLayer*>(play) == this) {
            mgr->captureReplayPositionSample(play, replayFrame, "input", true);
        }
    }
};

// ---------- PlayLayer — init / reset / exit hooks for recording -------------

class $modify(RecPlayLayer, PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreateObjects) {
        zBot* mgr = zBot::get();
        if (mgr->state == RECORD) {
            mgr->createNewReplay(lvl);
        }

        bool result = PlayLayer::init(lvl, useReplay, dontCreateObjects);
        if (result && mgr->state == RECORD && mgr->currentReplay) {
            mgr->prepareRecordingFromCurrentPlayerState(this, "init", false);
        }

        return result;
    }

    void startGame() {
        PlayLayer::startGame();

        zBot* mgr = zBot::get();
        if (mgr->state == RECORD && mgr->currentReplay) {
            mgr->prepareRecordingFromCurrentPlayerState(this, "startGame", false);
        }
    }

    void startGameDelayed() {
        PlayLayer::startGameDelayed();

        zBot* mgr = zBot::get();
        if (mgr->state == RECORD && mgr->currentReplay) {
            mgr->prepareRecordingFromCurrentPlayerState(this, "startGameDelayed", false);
        }
    }

    CheckpointObject* createCheckpoint() {
        auto* checkpoint = PlayLayer::createCheckpoint();

        zBot* mgr = zBot::get();
        if (mgr->state == RECORD && mgr->currentReplay) {
            int replayFrame = std::max(0, mgr->getRecordingTimelineFrame(m_gameState.m_currentProgress) + 1);
            mgr->captureReplayPositionSample(this, replayFrame, "createCheckpoint", true);
        }

        return checkpoint;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        zBot* mgr = zBot::get();
        if (mgr->state != RECORD || !mgr->currentReplay) return;

        mgr->prepareRecordingFromCurrentPlayerState(this, "resetLevel", true);

        int replayFrame = mgr->recordingState.recordingStartReplayFrame;

        // Release jump for player 1
        mgr->currentReplay->addInput(
            replayFrame,
            static_cast<int>(PlayerButton::Jump), false, false);
        m_player1->m_isDashing = false;

        // Release jump for player 2 when in 2-player dual mode
        if (m_gameState.m_isDualMode && m_levelSettings->m_twoPlayerMode) {
            mgr->currentReplay->addInput(
                replayFrame,
                static_cast<int>(PlayerButton::Jump), true, false);
            m_player2->m_isDashing = false;
        }
    }

    void levelComplete() {
        zBot* mgr = zBot::get();
        if (mgr->state == RECORD && mgr->currentReplay)
            mgr->currentReplay->save();
        PlayLayer::levelComplete();
    }

    void onExit() {
        zBot* mgr = zBot::get();
        if (mgr->state == RECORD && mgr->currentReplay)
            mgr->currentReplay->save();
        mgr->resetRecordingState();
        PlayLayer::onExit();
    }
};
