#ifndef _autobot_model_hpp
#define _autobot_model_hpp

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace geode::prelude;

namespace autobot {

constexpr float kDefaultCubeGravity = -0.958199f;
constexpr float kDefaultJumpVelocity = 11.1803f;
constexpr float kDefaultMiniJumpVelocity = 9.4f;
constexpr float kHalfSpeedUnits = 5.77f;
constexpr float kNormalSpeedUnits = 5.77f;
constexpr float kDoubleSpeedUnits = 11.54f;
constexpr float kTripleSpeedUnits = 15.38f;
constexpr float kQuadSpeedUnits = 19.23f;

enum class GameMode : int {
    Cube = 0,
    Ship = 1,
    Ball = 2,
    UFO = 3,
    Wave = 4,
    Robot = 5,
    Spider = 6,
    Swing = 7,
};

enum class CachedObjectCategory : int {
    Unknown = 0,
    Hazard,
    Solid,
    Orb,
    Pad,
    Portal,
};

enum class CollisionApproximation : int {
    Unknown = 0,
    GameRect,
    FallbackAABB,
};

enum class OrbKind : int {
    None = 0,
    Yellow,
    Pink,
    Red,
    BlueGravity,
    GreenGravity,
    Black,
    DashGreen,
    DashMagenta,
    Rebound,
    Toggle,
    Spider,
};

enum class PadKind : int {
    None = 0,
    Yellow,
    Pink,
    BlueGravity,
    Red,
    Spider,
    Rebound,
};

enum class PortalKind : int {
    None = 0,
    GravityFlip,
    GravityNormal,
    SpeedHalf,
    SpeedNormal,
    SpeedDouble,
    SpeedTriple,
    SpeedQuad,
    ModeCube,
    ModeShip,
    ModeBall,
    ModeUFO,
    ModeWave,
    ModeRobot,
    ModeSpider,
    ModeSwing,
    SizeMini,
    SizeNormal,
};

struct RectF {
    float left = 0.f;
    float right = 0.f;
    float bottom = 0.f;
    float top = 0.f;

    float width() const {
        return right - left;
    }

    float height() const {
        return top - bottom;
    }

    float centerX() const {
        return (left + right) * 0.5f;
    }

    float centerY() const {
        return (bottom + top) * 0.5f;
    }

    bool overlaps(RectF const& other) const {
        return !(right < other.left || left > other.right || top < other.bottom || bottom > other.top);
    }
};

struct CachedLevelObject {
    int uniqueID = 0;
    int objectID = 0;
    GameObjectType objectType = static_cast<GameObjectType>(0);
    CachedObjectCategory category = CachedObjectCategory::Unknown;
    OrbKind orbKind = OrbKind::None;
    PadKind padKind = PadKind::None;
    PortalKind portalKind = PortalKind::None;
    GameMode targetMode = GameMode::Cube;
    float speedValue = 0.f;
    RectF bounds;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool flipX = false;
    bool flipY = false;
    bool solidColorBlock = false;
    bool slopeIsHazard = false;
    bool isGripSlope = false;
    int slopeDirection = 0;
    CollisionApproximation collisionApproximation = CollisionApproximation::Unknown;
    bool runtimeRect = false;

    bool isHazard() const {
        return category == CachedObjectCategory::Hazard || slopeIsHazard;
    }

    bool isSolid() const {
        return category == CachedObjectCategory::Solid;
    }

    bool isOrb() const {
        return category == CachedObjectCategory::Orb;
    }

    bool isPad() const {
        return category == CachedObjectCategory::Pad;
    }

    bool isPortal() const {
        return category == CachedObjectCategory::Portal;
    }
};

struct PlayerSnapshot {
    int frame = 0;
    float x = 0.f;
    float y = 0.f;
    float observedXSpeed = 0.f;
    float yVelocity = 0.f;
    float playerSpeed = 0.f;
    float vehicleSize = 1.f;
    double speedMultiplier = 0.0;
    float gravityPerTick = kDefaultCubeGravity;
    bool gravityApproximate = true;
    bool speedApproximate = true;
    bool onGround = false;
    bool gravFlip = false;
    bool isMini = false;
    bool isDead = false;
    bool inputHeld = false;
    bool touchedRing = false;
    bool touchedPad = false;
    bool jumpBuffered = false;
    bool onSlope = false;
    bool dualMode = false;
    bool twoPlayerMode = false;
    float slopeAngle = 0.f;
    GameMode mode = GameMode::Cube;
};

struct SimState {
    float x = 0.f;
    float y = 0.f;
    float xSpeed = 0.f;
    float yVelocity = 0.f;
    float gravityPerTick = kDefaultCubeGravity;
    bool onGround = false;
    bool gravFlip = false;
    bool isMini = false;
    bool isDead = false;
    bool inputHeld = false;
    bool touchedRing = false;
    bool touchedPad = false;
    bool jumpBuffered = false;
    GameMode mode = GameMode::Cube;
    int lastOrbUID = -1;
    int lastPadUID = -1;
    int lastPortalUID = -1;
    int activatedOrbUID = -1;
    int currentSupportUID = -1;
    CollisionApproximation worstApproximation = CollisionApproximation::GameRect;

    float halfWidth() const {
        return isMini ? 7.5f : 15.f;
    }

    float halfHeight() const {
        return isMini ? 7.5f : 15.f;
    }
};

struct PlannerConfig {
    bool rollingPlannerEnabled = false;
    bool experimentalMultiMode = false;
    bool useApproximateCollisionFallback = true;
    float horizonSeconds = 1.0f;
    float ticksPerSecond = 240.0f;
    int cubeTimingSafetyTicks = 12;
};

struct PlannerDecision {
    bool shouldPress = false;
    bool valid = false;
    bool emergency = false;
    bool noInputLandedSafely = false;
    bool noInputWon = false;
    bool safetyBufferedTimingUsed = false;
    bool framePerfectFallbackUsed = false;
    bool safeTapSparse = false;
    bool noPressGateUsed = false;
    bool dropWaitGateUsed = false;
    bool immediatePressRequired = false;
    bool selectedTapClearanceComputed = false;
    bool selectedTapClearanceApproximate = false;
    int candidateCount = 0;
    int chosenDelay = -1;
    int noJumpDeathTick = -1;
    int noInputSupportLostTick = -1;
    int noInputFallStartedTick = -1;
    int noInputLandedTick = -1;
    int noInputLandingSupportUID = -1;
    int safeTapCount = 0;
    int earliestSafeTap = -1;
    int latestSafeTap = -1;
    int safeWindowWidth = 0;
    int configuredSafetyTicks = 0;
    int effectiveSafetyTicks = 0;
    int targetSafeTap = -1;
    int selectedTap = -1;
    int safeJumpCandidatesTotal = 0;
    int safeTapTimingsBeforeFiltering = 0;
    int safeTapTimingsAfterFiltering = 0;
    int selectedTapFromFullSafeSet = -1;
    int selectedTapAfterBuffer = -1;
    int maxDelayTested = 0;
    int maxTapCountTested = 0;
    int predictedDeathTick = -1;
    int activatedOrbUID = -1;
    float nearestHazardDistance = std::numeric_limits<float>::infinity();
    float timeToHazard = std::numeric_limits<float>::infinity();
    float leadFrames = 0.f;
    float horizonSeconds = 0.f;
    CollisionApproximation collisionApproximation = CollisionApproximation::Unknown;
    float selectedTapScore = -1000000.f;
    float selectedTapClearance = std::numeric_limits<float>::infinity();
    std::string reason;
    std::string selectedTapLabel;
    std::string noJumpDeathReason;
    std::string bestDeathReason;
    std::string noPressGateReason;
    std::string noInputRejectedReason;
    std::string immediateTapSelectionReason;
};

inline char const* gameModeName(GameMode mode) {
    switch (mode) {
        case GameMode::Cube:   return "Cube";
        case GameMode::Ship:   return "Ship";
        case GameMode::Ball:   return "Ball";
        case GameMode::UFO:    return "UFO";
        case GameMode::Wave:   return "Wave";
        case GameMode::Robot:  return "Robot";
        case GameMode::Spider: return "Spider";
        case GameMode::Swing:  return "Swing";
        default:               return "Unknown";
    }
}

inline char const* collisionApproximationName(CollisionApproximation approximation) {
    switch (approximation) {
        case CollisionApproximation::GameRect:     return "game-rect";
        case CollisionApproximation::FallbackAABB: return "fallback-aabb";
        default:                                   return "unknown";
    }
}

inline CollisionApproximation mergeApproximation(CollisionApproximation current, CollisionApproximation next) {
    if (current == CollisionApproximation::Unknown) return next;
    if (next == CollisionApproximation::Unknown) return current;
    if (current == CollisionApproximation::FallbackAABB || next == CollisionApproximation::FallbackAABB) {
        return CollisionApproximation::FallbackAABB;
    }
    return CollisionApproximation::GameRect;
}

inline bool isHazardID(int id) {
    switch (id) {
        case 8: case 39: case 103: case 135:
        case 392: case 9: case 61: case 243: case 244:
        case 363: case 364: case 365: case 366:
        case 367: case 368: case 369: case 370:
        case 446: case 447: case 667: case 720:
        case 721: case 722:
        case 88: case 89: case 98:
        case 183: case 184: case 185: case 186:
        case 187: case 188: case 678: case 679:
        case 680: case 681:
        case 1705: case 1706: case 1707: case 1708:
        case 1709: case 1710: case 1711: case 1712:
        case 1713: case 1714: case 1715: case 1716:
        case 1717: case 1718: case 1719: case 1720:
        case 1831: case 1832: case 1833: case 1834: case 1835:
            return true;
        default:
            return false;
    }
}

inline OrbKind classifyOrbID(int id) {
    switch (id) {
        case 36:   return OrbKind::Yellow;
        case 141:  return OrbKind::Pink;
        case 1022: return OrbKind::Red;
        case 1330: return OrbKind::BlueGravity;
        case 1332: return OrbKind::GreenGravity;
        case 1333: return OrbKind::Black;
        case 1594: return OrbKind::DashGreen;
        case 1704: return OrbKind::DashMagenta;
        case 1751: return OrbKind::Rebound;
        case 1768: return OrbKind::Toggle;
        case 1587: return OrbKind::Spider;
        default:   return OrbKind::None;
    }
}

inline PadKind classifyPadID(int id) {
    switch (id) {
        case 35:   return PadKind::Yellow;
        case 67:   return PadKind::BlueGravity;
        case 140:  return PadKind::Pink;
        case 1332: return PadKind::Red;
        case 3027: return PadKind::Spider;
        case 3005: return PadKind::Rebound;
        default:   return PadKind::None;
    }
}

inline PortalKind classifyPortalID(int id) {
    switch (id) {
        case 10:   return PortalKind::GravityFlip;
        case 11:   return PortalKind::GravityNormal;
        case 200:  return PortalKind::SpeedHalf;
        case 201:  return PortalKind::SpeedNormal;
        case 202:  return PortalKind::SpeedDouble;
        case 203:  return PortalKind::SpeedTriple;
        case 1334: return PortalKind::SpeedQuad;
        case 12:   return PortalKind::ModeCube;
        case 13:   return PortalKind::ModeShip;
        case 47:   return PortalKind::ModeBall;
        case 111:  return PortalKind::ModeUFO;
        case 660:  return PortalKind::ModeWave;
        case 745:  return PortalKind::ModeRobot;
        case 1331: return PortalKind::ModeSpider;
        case 1933: return PortalKind::ModeSwing;
        case 99:   return PortalKind::SizeMini;
        case 101:  return PortalKind::SizeNormal;
        default:   return PortalKind::None;
    }
}

inline GameMode gameModeFromPortalKind(PortalKind portalKind) {
    switch (portalKind) {
        case PortalKind::ModeShip:   return GameMode::Ship;
        case PortalKind::ModeBall:   return GameMode::Ball;
        case PortalKind::ModeUFO:    return GameMode::UFO;
        case PortalKind::ModeWave:   return GameMode::Wave;
        case PortalKind::ModeRobot:  return GameMode::Robot;
        case PortalKind::ModeSpider: return GameMode::Spider;
        case PortalKind::ModeSwing:  return GameMode::Swing;
        case PortalKind::ModeCube:
        default:
            return GameMode::Cube;
    }
}

inline float speedUnitsFromPortalKind(PortalKind portalKind) {
    switch (portalKind) {
        case PortalKind::SpeedHalf:   return kHalfSpeedUnits;
        case PortalKind::SpeedDouble: return kDoubleSpeedUnits;
        case PortalKind::SpeedTriple: return kTripleSpeedUnits;
        case PortalKind::SpeedQuad:   return kQuadSpeedUnits;
        case PortalKind::SpeedNormal:
        default:
            return kNormalSpeedUnits;
    }
}

} // namespace autobot

#endif
