#ifndef _zbot_hpp
#define _zbot_hpp
#include "replay.hpp"
#include <Geode/Geode.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>
using namespace geode::prelude;

class PlayLayer;
class GJBaseGameLayer;

// ============================================================================
// zBot state enumerations
// ============================================================================

/// Recording/playback state for the replay system
enum zState {
    NONE,       ///< Bot idle — no recording or playback
    RECORD,     ///< Recording player inputs each physics tick
    PLAYBACK    ///< Playing back a saved replay
};

enum class ReplaySeekMethod {
    Uninitialized,
    PositionSamples,
    CurrentProgress,
    PercentApproximation,
    XApproximation,
    FallbackFrameZero
};

struct ReplayPlaybackState {
    int playbackStartReplayFrame = 0;
    int playbackSessionStartGameFrame = 0;
    float playbackStartPlayer1X = 0.f;
    float playbackStartPlayer1Y = 0.f;
    float playbackStartPercent = 0.f;
    bool playbackSeekInitialized = false;
    bool heldStateReconstructed = false;
    std::size_t currentInputIndex = 0;
    std::size_t clickbotInputIndex = 0;
    int matchedSampleFrame = -1;
    float matchedSampleDistance = -1.f;
    ReplaySeekMethod seekMethod = ReplaySeekMethod::Uninitialized;
    std::string lastSeekReason;
    std::unordered_map<int, bool> appliedHeldInputs;
};

struct ReplayRecordingState {
    int recordingStartReplayFrame = 0;
    int recordingSessionStartGameFrame = 0;
    float recordingStartPlayer1X = 0.f;
    float recordingStartPlayer1Y = 0.f;
    float recordingStartPercent = 0.f;
    bool recordingOffsetInitialized = false;
    int lastPeriodicSampleFrame = -1;
    ReplaySeekMethod alignmentMethod = ReplaySeekMethod::Uninitialized;
    std::string lastAlignmentReason;
};

// ============================================================================
// zBot — central singleton that owns replay + bot state
// ============================================================================

class zBot {
public:
    // --- Replay state ---
    zState state = NONE;

    bool fmodified = false;

    float extraTPS = 0.f;

    bool disableRender = false;
    bool ignoreBypass = false;
    bool justLoaded = false;
    bool ignoreInput = false;
    bool frameAdvance = false;
    bool doAdvance = false;
    bool internalRenderer = false;
    bool speedHackAudio = true;
    bool clickbotEnabled = false;
    bool autoBotFileLogging = true;

    double speed = 1;
    double tps = 240.0;
    zReplay* currentReplay = nullptr;
    ReplayPlaybackState playbackState;
    ReplayRecordingState recordingState;

    // --- Autonomous bot state ---
    bool autoBotEnabled = false;   ///< Master toggle for autonomous play mode
    bool autoBotUsePlanner = false;
    bool autoBotExperimentalMultiMode = false;
    bool autoBotLogUnknownObjects = false;
    bool autoBotApproximateCollisionFallback = true;
    float autoBotPlannerHorizonSeconds = 1.0f;
    int autoBotCubeTimingSafetyTicks = 12;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void createNewReplay(GJGameLevel* level) {
        resetPlaybackState();
        resetRecordingState();
        currentReplay = new zReplay();
        currentReplay->levelInfo.id = level->m_levelID;
        currentReplay->levelInfo.name = level->m_levelName;
        currentReplay->name = level->m_levelName;
        currentReplay->framerate = static_cast<float>(tps);
    }

    /// Meyer's singleton accessor
    static auto* get() {
        static zBot* instance = new zBot();
        return instance;
    }
    
    /// Play a click sound through the clickbot audio system
    void playSound(bool p2, int button, bool down);

    static char const* seekMethodName(ReplaySeekMethod method);

    void resetPlaybackState();
    void resetRecordingState();
    void markPlaybackSeekDirty();
    void clearPlaybackHeldInputs(PlayLayer* play);

    int resolveReplayFrameForCurrentState(
        PlayLayer* play,
        ReplaySeekMethod* outMethod,
        int* outMatchedSampleFrame = nullptr,
        float* outMatchedSampleDistance = nullptr
    ) const;

    int getPlaybackTimelineFrame(unsigned int currentProgress) const;
    int getRecordingTimelineFrame(unsigned int currentProgress) const;

    void preparePlaybackFromCurrentPlayerState(PlayLayer* play, const char* reason);
    void prepareRecordingFromCurrentPlayerState(PlayLayer* play, const char* reason, bool truncateExisting);
    void captureReplayPositionSample(PlayLayer* play, int replayFrame, const char* reason, bool force);
    void capturePeriodicReplayPositionSample(GJBaseGameLayer* layer, const char* reason);
};

#endif
