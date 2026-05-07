// ============================================================================
// zBot.cpp — runtime helpers for the zBot singleton
//
// Currently provides the click/release sound shim used by replay playback.
// The original project shipped dedicated clickbot sources that are no longer
// present in this workspace, so this file restores the required symbol with a
// lightweight FMOD-backed implementation compatible with Geode v4.x.
// ============================================================================

#include "zBot.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

using namespace geode::prelude;

namespace {
    constexpr int kReplaySampleInterval = 8;

    int encodeHeldInputKey(int button, bool player2) {
        return (button << 1) | (player2 ? 1 : 0);
    }

    int decodeHeldInputButton(int encoded) {
        return encoded >> 1;
    }

    bool decodeHeldInputPlayer2(int encoded) {
        return (encoded & 1) != 0;
    }

    int clampReplayFrame(int frame, zReplay const* replay) {
        if (!replay) {
            return std::max(0, frame);
        }

        int lastReplayFrame = 0;
        if (!replay->inputs.empty()) {
            lastReplayFrame = replay->inputs.back().frame;
        }
        else if (!replay->positionSamples.empty()) {
            lastReplayFrame = replay->positionSamples.back().replayFrame;
        }

        return std::clamp(frame, 0, lastReplayFrame);
    }

    std::size_t firstInputIndexAfterFrame(zReplay const* replay, int frame) {
        if (!replay) {
            return 0;
        }

        auto it = std::upper_bound(replay->inputs.begin(), replay->inputs.end(), frame,
            [](int targetFrame, zInput const& input) {
                return targetFrame < input.frame;
            }
        );

        return static_cast<std::size_t>(std::distance(replay->inputs.begin(), it));
    }

    bool replayHasExistingTimeline(zReplay const* replay) {
        return replay && (!replay->inputs.empty() || !replay->positionSamples.empty());
    }
}

void zBot::playSound(bool p2, int button, bool down) {
    (void)p2;
    (void)button;

    if (!clickbotEnabled) return;

    auto* audio = FMODAudioEngine::sharedEngine();
    if (!audio) return;

    // The original mod used packaged click / release resources. Those zip
    // resources are still present, but without the original clickbot source we
    // fall back to a simple bundled effect path when available. If the file
    // isn't resolved by FMOD, the call simply becomes a no-op rather than
    // breaking replay playback.
    //
    // We intentionally separate push vs release so replays still preserve the
    // rhythm of recorded input even with the simplified audio backend.
    gd::string effectPath;
    if (down) {
        effectPath = "playSound_01.ogg";
    }
    else {
        effectPath = "playSound_02.ogg";
    }

    audio->playEffect(effectPath, 1.0f, 0.0f, 1.0f);
}

char const* zBot::seekMethodName(ReplaySeekMethod method) {
    switch (method) {
        case ReplaySeekMethod::PositionSamples:      return "exact position samples";
        case ReplaySeekMethod::CurrentProgress:      return "currentProgress";
        case ReplaySeekMethod::PercentApproximation: return "x/percent approximation (percent)";
        case ReplaySeekMethod::XApproximation:       return "x/percent approximation (x)";
        case ReplaySeekMethod::FallbackFrameZero:    return "fallback frame 0";
        case ReplaySeekMethod::Uninitialized:
        default:                                     return "uninitialized";
    }
}

void zBot::resetPlaybackState() {
    playbackState = ReplayPlaybackState();
}

void zBot::resetRecordingState() {
    recordingState = ReplayRecordingState();
}

void zBot::markPlaybackSeekDirty() {
    playbackState.playbackSeekInitialized = false;
    playbackState.heldStateReconstructed = false;
    playbackState.currentInputIndex = 0;
    playbackState.clickbotInputIndex = 0;
    playbackState.seekMethod = ReplaySeekMethod::Uninitialized;
    playbackState.matchedSampleFrame = -1;
    playbackState.matchedSampleDistance = -1.f;
    playbackState.lastSeekReason.clear();
}

void zBot::clearPlaybackHeldInputs(PlayLayer* play) {
    if (play) {
        auto* layer = static_cast<GJBaseGameLayer*>(play);
        for (auto const& [encodedKey, held] : playbackState.appliedHeldInputs) {
            if (!held) {
                continue;
            }

            layer->handleButton(false, decodeHeldInputButton(encodedKey), !decodeHeldInputPlayer2(encodedKey));
        }
    }

    playbackState.appliedHeldInputs.clear();
    playbackState.heldStateReconstructed = false;
}

int zBot::resolveReplayFrameForCurrentState(
    PlayLayer* play,
    ReplaySeekMethod* outMethod,
    int* outMatchedSampleFrame,
    float* outMatchedSampleDistance
) const {
    if (outMethod) {
        *outMethod = ReplaySeekMethod::FallbackFrameZero;
    }
    if (outMatchedSampleFrame) {
        *outMatchedSampleFrame = -1;
    }
    if (outMatchedSampleDistance) {
        *outMatchedSampleDistance = -1.f;
    }

    if (!play || !currentReplay) {
        return 0;
    }

    int lastReplayFrame = 0;
    if (!currentReplay->inputs.empty()) {
        lastReplayFrame = currentReplay->inputs.back().frame;
    }
    else if (!currentReplay->positionSamples.empty()) {
        lastReplayFrame = currentReplay->positionSamples.back().replayFrame;
    }

    float liveX = 0.f;
    float liveY = 0.f;
    if (play->m_player1) {
        liveX = play->m_player1->getPositionX();
        liveY = play->m_player1->getPositionY();
    }
    float livePercent = play->getCurrentPercent();
    unsigned int liveProgress = play->m_gameState.m_currentProgress;

    if (!currentReplay->positionSamples.empty() && play->m_player1) {
        int bestFrame = 0;
        float bestSpatialDistance = std::numeric_limits<float>::max();
        float bestPercentDelta = std::numeric_limits<float>::max();
        float bestScore = std::numeric_limits<float>::max();

        for (auto const& sample : currentReplay->positionSamples) {
            float dx = sample.player1X - liveX;
            float dy = sample.player1Y - liveY;
            float spatialDistance = std::sqrt(dx * dx + dy * dy);
            float percentDelta = std::fabs(sample.percent - livePercent);
            float progressDelta = std::fabs(static_cast<float>(sample.gameProgress) - static_cast<float>(liveProgress));
            float score = spatialDistance + percentDelta * 24.f + progressDelta * 0.01f;

            if (score < bestScore) {
                bestScore = score;
                bestSpatialDistance = spatialDistance;
                bestPercentDelta = percentDelta;
                bestFrame = sample.replayFrame;
            }
        }

        bool exactEnough = bestSpatialDistance <= 140.f || (bestSpatialDistance <= 220.f && bestPercentDelta <= 1.5f);
        if (exactEnough) {
            if (outMethod) {
                *outMethod = ReplaySeekMethod::PositionSamples;
            }
            if (outMatchedSampleFrame) {
                *outMatchedSampleFrame = bestFrame;
            }
            if (outMatchedSampleDistance) {
                *outMatchedSampleDistance = bestSpatialDistance;
            }
            return clampReplayFrame(bestFrame, currentReplay);
        }
    }

    bool suspiciousStartPosProgress = play->m_startPosObject && livePercent > 1.f && liveProgress <= 5u;
    bool progressUseful = liveProgress > 0u && static_cast<int>(liveProgress) <= lastReplayFrame && !suspiciousStartPosProgress;
    if (progressUseful) {
        if (outMethod) {
            *outMethod = ReplaySeekMethod::CurrentProgress;
        }
        return clampReplayFrame(static_cast<int>(liveProgress), currentReplay);
    }

    if (livePercent > 0.05f && lastReplayFrame > 0) {
        int approxFrame = static_cast<int>(std::lround((livePercent / 100.f) * static_cast<float>(lastReplayFrame)));
        if (outMethod) {
            *outMethod = ReplaySeekMethod::PercentApproximation;
        }
        return clampReplayFrame(approxFrame, currentReplay);
    }

    if (play->m_player1 && lastReplayFrame > 0) {
        auto endPosition = play->getEndPosition();
        if (endPosition.x > 1.f) {
            float ratio = std::clamp(liveX / endPosition.x, 0.f, 1.f);
            int approxFrame = static_cast<int>(std::lround(ratio * static_cast<float>(lastReplayFrame)));
            if (outMethod) {
                *outMethod = ReplaySeekMethod::XApproximation;
            }
            return clampReplayFrame(approxFrame, currentReplay);
        }
    }

    return 0;
}

int zBot::getPlaybackTimelineFrame(unsigned int currentProgress) const {
    if (!playbackState.playbackSeekInitialized) {
        return -1;
    }

    int liveSessionFrame = static_cast<int>(currentProgress) - playbackState.playbackSessionStartGameFrame - 1;
    return playbackState.playbackStartReplayFrame + liveSessionFrame;
}

int zBot::getRecordingTimelineFrame(unsigned int currentProgress) const {
    if (!recordingState.recordingOffsetInitialized) {
        return std::max(0, static_cast<int>(currentProgress) - 1);
    }

    int liveSessionFrame = static_cast<int>(currentProgress) - recordingState.recordingSessionStartGameFrame - 1;
    return recordingState.recordingStartReplayFrame + liveSessionFrame;
}

void zBot::captureReplayPositionSample(PlayLayer* play, int replayFrame, const char* reason, bool force) {
    if (!play || !currentReplay || !play->m_player1) {
        return;
    }

    zReplaySample sample;
    sample.replayFrame = std::max(0, replayFrame);
    sample.gameProgress = play->m_gameState.m_currentProgress;
    sample.percent = play->getCurrentPercent();
    sample.player1X = play->m_player1->getPositionX();
    sample.player1Y = play->m_player1->getPositionY();
    if (play->m_player2) {
        sample.player2X = play->m_player2->getPositionX();
        sample.player2Y = play->m_player2->getPositionY();
    }
    sample.dual = play->m_gameState.m_isDualMode;
    sample.twoPlayer = play->m_levelSettings && play->m_levelSettings->m_twoPlayerMode;

    currentReplay->addPositionSample(sample, true);

    if (force) {
        log::info(
            "Recorded replay sample | reason={} replayFrame={} liveProgress={} x={:.2f} y={:.2f} percent={:.2f}",
            reason ? reason : "unknown",
            sample.replayFrame,
            sample.gameProgress,
            sample.player1X,
            sample.player1Y,
            sample.percent
        );
    }
}

void zBot::capturePeriodicReplayPositionSample(GJBaseGameLayer* layer, const char* reason) {
    if (state != RECORD || !currentReplay) {
        return;
    }

    auto* play = PlayLayer::get();
    if (!play || static_cast<GJBaseGameLayer*>(play) != layer || !play->m_player1) {
        return;
    }

    if (!recordingState.recordingOffsetInitialized) {
        prepareRecordingFromCurrentPlayerState(play, reason ? reason : "periodic-auto-init", false);
    }

    int replayFrame = std::max(0, getRecordingTimelineFrame(play->m_gameState.m_currentProgress) + 1);
    if (recordingState.lastPeriodicSampleFrame >= 0 && replayFrame - recordingState.lastPeriodicSampleFrame < kReplaySampleInterval) {
        return;
    }

    captureReplayPositionSample(play, replayFrame, reason, false);
    recordingState.lastPeriodicSampleFrame = replayFrame;
}

void zBot::preparePlaybackFromCurrentPlayerState(PlayLayer* play, const char* reason) {
    if (!play || !currentReplay || !play->m_player1) {
        return;
    }

    ReplaySeekMethod method = ReplaySeekMethod::FallbackFrameZero;
    int matchedSampleFrame = -1;
    float matchedSampleDistance = -1.f;
    int startReplayFrame = resolveReplayFrameForCurrentState(play, &method, &matchedSampleFrame, &matchedSampleDistance);
    startReplayFrame = clampReplayFrame(startReplayFrame, currentReplay);

    playbackState.playbackStartReplayFrame = startReplayFrame;
    playbackState.playbackSessionStartGameFrame = static_cast<int>(play->m_gameState.m_currentProgress);
    playbackState.playbackStartPlayer1X = play->m_player1->getPositionX();
    playbackState.playbackStartPlayer1Y = play->m_player1->getPositionY();
    playbackState.playbackStartPercent = play->getCurrentPercent();
    playbackState.playbackSeekInitialized = true;
    playbackState.seekMethod = method;
    playbackState.matchedSampleFrame = matchedSampleFrame;
    playbackState.matchedSampleDistance = matchedSampleDistance;
    playbackState.lastSeekReason = reason ? reason : "unknown";

    int holdFrame = startReplayFrame - 1;
    playbackState.currentInputIndex = firstInputIndexAfterFrame(currentReplay, holdFrame);
    playbackState.clickbotInputIndex = playbackState.currentInputIndex;

    std::unordered_map<int, bool> desiredHeldInputs;
    for (auto const& input : currentReplay->inputs) {
        if (input.frame > holdFrame) {
            break;
        }

        desiredHeldInputs[encodeHeldInputKey(input.button, input.player2)] = input.down;
    }

    auto* layer = static_cast<GJBaseGameLayer*>(play);

    for (auto const& [encodedKey, held] : playbackState.appliedHeldInputs) {
        bool shouldHold = false;
        if (auto desired = desiredHeldInputs.find(encodedKey); desired != desiredHeldInputs.end()) {
            shouldHold = desired->second;
        }

        if (held && !shouldHold) {
            layer->handleButton(false, decodeHeldInputButton(encodedKey), !decodeHeldInputPlayer2(encodedKey));
        }
    }

    for (auto const& [encodedKey, held] : desiredHeldInputs) {
        if (!held) {
            continue;
        }

        bool alreadyHeld = false;
        if (auto current = playbackState.appliedHeldInputs.find(encodedKey); current != playbackState.appliedHeldInputs.end()) {
            alreadyHeld = current->second;
        }

        if (!alreadyHeld) {
            layer->handleButton(true, decodeHeldInputButton(encodedKey), !decodeHeldInputPlayer2(encodedKey));
        }
    }

    playbackState.appliedHeldInputs.clear();
    for (auto const& [encodedKey, held] : desiredHeldInputs) {
        if (held) {
            playbackState.appliedHeldInputs[encodedKey] = true;
        }
    }
    playbackState.heldStateReconstructed = true;

    log::info(
        "Playback seek initialized | reason={} liveProgress={} liveX={:.2f} liveY={:.2f} percent={:.2f} startReplayFrame={} sessionStartGameFrame={} seekMethod={} matchedSampleFrame={} matchedSampleDistance={:.2f} currIndex={} clickBotIndex={} heldInputs={}",
        playbackState.lastSeekReason,
        play->m_gameState.m_currentProgress,
        playbackState.playbackStartPlayer1X,
        playbackState.playbackStartPlayer1Y,
        playbackState.playbackStartPercent,
        playbackState.playbackStartReplayFrame,
        playbackState.playbackSessionStartGameFrame,
        seekMethodName(playbackState.seekMethod),
        playbackState.matchedSampleFrame,
        playbackState.matchedSampleDistance,
        playbackState.currentInputIndex,
        playbackState.clickbotInputIndex,
        playbackState.appliedHeldInputs.size()
    );

    if (method == ReplaySeekMethod::PercentApproximation || method == ReplaySeekMethod::XApproximation || method == ReplaySeekMethod::FallbackFrameZero) {
        log::warn(
            "Playback seek used non-exact alignment | reason={} method={} liveProgress={} livePercent={:.2f}",
            playbackState.lastSeekReason,
            seekMethodName(method),
            play->m_gameState.m_currentProgress,
            playbackState.playbackStartPercent
        );
    }
}

void zBot::prepareRecordingFromCurrentPlayerState(PlayLayer* play, const char* reason, bool truncateExisting) {
    if (!play || !currentReplay || !play->m_player1) {
        return;
    }

    bool replayWasEmpty = !replayHasExistingTimeline(currentReplay);

    ReplaySeekMethod method = ReplaySeekMethod::FallbackFrameZero;
    int startReplayFrame = replayWasEmpty
        ? 0
        : resolveReplayFrameForCurrentState(play, &method, nullptr, nullptr);

    startReplayFrame = clampReplayFrame(startReplayFrame, currentReplay);
    if (truncateExisting) {
        currentReplay->purgeAfter(startReplayFrame);
    }

    recordingState.recordingStartReplayFrame = startReplayFrame;
    recordingState.recordingSessionStartGameFrame = static_cast<int>(play->m_gameState.m_currentProgress);
    recordingState.recordingStartPlayer1X = play->m_player1->getPositionX();
    recordingState.recordingStartPlayer1Y = play->m_player1->getPositionY();
    recordingState.recordingStartPercent = play->getCurrentPercent();
    recordingState.recordingOffsetInitialized = true;
    recordingState.lastPeriodicSampleFrame = startReplayFrame;
    recordingState.alignmentMethod = replayWasEmpty ? ReplaySeekMethod::FallbackFrameZero : method;
    recordingState.lastAlignmentReason = reason ? reason : "unknown";

    if (replayWasEmpty) {
        currentReplay->recordedFromStartPos = play->m_startPosObject != nullptr;
        currentReplay->recordingStartGameProgress = play->m_gameState.m_currentProgress;
        currentReplay->recordingStartPercent = recordingState.recordingStartPercent;
        currentReplay->recordingStartPlayer1X = recordingState.recordingStartPlayer1X;
        currentReplay->recordingStartPlayer1Y = recordingState.recordingStartPlayer1Y;
        currentReplay->recordingStartSource = play->m_startPosObject
            ? "startpos"
            : (play->getLastCheckpoint() ? "checkpoint" : "level-start");
    }

    captureReplayPositionSample(play, startReplayFrame, reason, true);

    log::info(
        "Recording alignment initialized | reason={} truncateExisting={} liveProgress={} liveX={:.2f} liveY={:.2f} percent={:.2f} startReplayFrame={} sessionStartGameFrame={} method={}",
        recordingState.lastAlignmentReason,
        truncateExisting,
        play->m_gameState.m_currentProgress,
        recordingState.recordingStartPlayer1X,
        recordingState.recordingStartPlayer1Y,
        recordingState.recordingStartPercent,
        recordingState.recordingStartReplayFrame,
        recordingState.recordingSessionStartGameFrame,
        seekMethodName(recordingState.alignmentMethod)
    );
}
