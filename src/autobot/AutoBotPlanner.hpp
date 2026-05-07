#ifndef _autobot_planner_hpp
#define _autobot_planner_hpp

#include "AutoBotDebug.hpp"
#include "AutoBotModel.hpp"
#include "../zBot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace geode::prelude;

namespace autobot {

class StateReader {
public:
    void reset() {
        m_hasObservedMotion = false;
        m_lastObservedFrame = -1;
        m_lastObservedX = 0.f;
        m_lastObservedXSpeed = kNormalSpeedUnits;
    }

    PlayerSnapshot capture(GJBaseGameLayer* gl, PlayerObject* player, bool inputHeld) {
        PlayerSnapshot snapshot;
        snapshot.frame = gl ? gl->m_gameState.m_currentProgress : -1;
        snapshot.x = player->getPositionX();
        snapshot.y = player->getPositionY();
        snapshot.yVelocity = static_cast<float>(player->m_yVelocity);
        snapshot.playerSpeed = player->m_playerSpeed;
        snapshot.vehicleSize = player->m_vehicleSize;
        snapshot.speedMultiplier = player->m_speedMultiplier;
        snapshot.onGround = player->m_isOnGround;
        snapshot.gravFlip = player->m_isUpsideDown;
        snapshot.isMini = player->m_vehicleSize < 0.8f || player->getScale() < 0.8f;
        snapshot.isDead = player->m_isDead;
        snapshot.inputHeld = inputHeld;
        snapshot.touchedRing = player->m_touchedRing || player->m_touchedCustomRing || player->m_hasEverHitRing;
        snapshot.touchedPad = player->m_touchedPad;
        snapshot.jumpBuffered = player->m_jumpBuffered || player->m_stateRingJump || player->m_stateRingJump2;
        snapshot.onSlope = player->m_isOnSlope || player->m_wasOnSlope || player->m_maybeUpsideDownSlope;
        snapshot.slopeAngle = player->m_slopeAngle;
        snapshot.dualMode = gl ? gl->m_gameState.m_isDualMode : false;
        snapshot.twoPlayerMode = gl && gl->m_levelSettings ? gl->m_levelSettings->m_twoPlayerMode : false;
        snapshot.mode = detectMode(player);

        float gravity = static_cast<float>(player->m_gravity);
        float gravityMod = player->m_gravityMod;
        if (!std::isfinite(gravity) || std::abs(gravity) < 0.05f) {
            gravity = kDefaultCubeGravity;
            snapshot.gravityApproximate = true;
        }
        if (!std::isfinite(gravityMod) || std::abs(gravityMod) < 0.001f) {
            gravityMod = 1.f;
        }
        gravity *= gravityMod;
        if (snapshot.gravFlip && gravity < 0.f) gravity = -gravity;
        if (!snapshot.gravFlip && gravity > 0.f) gravity = -gravity;
        snapshot.gravityPerTick = gravity;

        float fallbackSpeed = mappedSpeedFromPlayerSpeed(snapshot.playerSpeed);
        if (std::isfinite(snapshot.speedMultiplier) && snapshot.speedMultiplier > 0.01) {
            fallbackSpeed = static_cast<float>(snapshot.speedMultiplier) * kNormalSpeedUnits;
        }

        snapshot.observedXSpeed = fallbackSpeed;
        snapshot.speedApproximate = true;

        if (m_hasObservedMotion) {
            if (snapshot.frame > m_lastObservedFrame) {
                int frameDelta = std::max(1, snapshot.frame - m_lastObservedFrame);
                float observedXSpeed = (snapshot.x - m_lastObservedX) / static_cast<float>(frameDelta);
                if (std::isfinite(observedXSpeed) && observedXSpeed > 0.01f) {
                    snapshot.observedXSpeed = observedXSpeed;
                    snapshot.speedApproximate = false;
                }
            } else if (std::isfinite(m_lastObservedXSpeed) && m_lastObservedXSpeed > 0.01f) {
                snapshot.observedXSpeed = m_lastObservedXSpeed;
            }
        }

        m_hasObservedMotion = true;
        m_lastObservedFrame = snapshot.frame;
        m_lastObservedX = snapshot.x;
        m_lastObservedXSpeed = snapshot.observedXSpeed;
        return snapshot;
    }

private:
    bool m_hasObservedMotion = false;
    int m_lastObservedFrame = -1;
    float m_lastObservedX = 0.f;
    float m_lastObservedXSpeed = kNormalSpeedUnits;

    static GameMode detectMode(PlayerObject* player) {
        if (player->m_isShip)   return GameMode::Ship;
        if (player->m_isBall)   return GameMode::Ball;
        if (player->m_isBird)   return GameMode::UFO;
        if (player->m_isDart)   return GameMode::Wave;
        if (player->m_isRobot)  return GameMode::Robot;
        if (player->m_isSpider) return GameMode::Spider;
        if (player->m_isSwing)  return GameMode::Swing;
        return GameMode::Cube;
    }

    static float mappedSpeedFromPlayerSpeed(float playerSpeed) {
        if (playerSpeed <= 0.7f) return kHalfSpeedUnits;
        if (playerSpeed <= 1.1f) return kNormalSpeedUnits;
        if (playerSpeed <= 1.3f) return kDoubleSpeedUnits;
        if (playerSpeed <= 1.6f) return kTripleSpeedUnits;
        return kQuadSpeedUnits;
    }
};

class LevelObjectCache {
public:
    void invalidate() {
        m_objects.clear();
        m_loggedUnknownIDs.clear();
        m_lastObjectCount = 0;
        m_built = false;
    }

    bool isBuilt() const {
        return m_built;
    }

    size_t size() const {
        return m_objects.size();
    }

    bool build(GJBaseGameLayer* gl, bool logUnknownObjects) {
        invalidate();

        if (!gl || !gl->m_objects) {
            return false;
        }

        auto total = gl->m_objects->count();
        m_lastObjectCount = total;
        m_objects.reserve(total);

        for (unsigned int i = 0; i < total; ++i) {
            auto* obj = static_cast<GameObject*>(gl->m_objects->objectAtIndex(i));
            if (!obj) continue;
            if (!isReadableObjectPtr(obj)) continue;
            if (geode::DestructorLock::isLocked(static_cast<cocos2d::CCNode*>(obj))) continue;

            auto cached = buildObject(obj);
            if (!cached) {
                if (logUnknownObjects) {
                    logUnknownObject(obj, "level-cache");
                }
                continue;
            }

            m_objects.push_back(*cached);
        }

        std::sort(m_objects.begin(), m_objects.end(), [](CachedLevelObject const& a, CachedLevelObject const& b) {
            if (a.bounds.left != b.bounds.left) return a.bounds.left < b.bounds.left;
            if (a.bounds.bottom != b.bounds.bottom) return a.bounds.bottom < b.bounds.bottom;
            return a.objectID < b.objectID;
        });

        m_built = true;
        return true;
    }

    void queryRange(float left, float right, std::vector<CachedLevelObject const*>& out) const {
        out.clear();
        if (m_objects.empty()) return;

        auto begin = std::lower_bound(
            m_objects.begin(),
            m_objects.end(),
            left,
            [](CachedLevelObject const& object, float value) {
                return object.bounds.right < value;
            }
        );

        for (auto it = begin; it != m_objects.end() && it->bounds.left <= right; ++it) {
            out.push_back(&*it);
        }
    }

private:
    std::vector<CachedLevelObject> m_objects;
    std::unordered_set<int> m_loggedUnknownIDs;
    unsigned int m_lastObjectCount = 0;
    bool m_built = false;

    static bool isSolidObject(GameObject* obj) {
        if (!obj) return false;
        if (obj->m_isSolidColorBlock) return true;

        switch (obj->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable:
                return true;
            default:
                return false;
        }
    }

    static bool isReadableObjectPtr(GameObject* obj) {
        if (!obj) {
            return false;
        }

#ifdef _WIN32
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(obj, &mbi, sizeof(mbi))) {
            return false;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
            return false;
        }

        auto begin = reinterpret_cast<std::uintptr_t>(obj);
        auto end = begin + sizeof(GameObject);
        auto regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (end > regionEnd) {
            return false;
        }
#endif

        return true;
    }

    static bool isReasonableRect(RectF const& rect) {
        return std::isfinite(rect.left)
            && std::isfinite(rect.right)
            && std::isfinite(rect.bottom)
            && std::isfinite(rect.top)
            && rect.width() > 0.5f
            && rect.height() > 0.5f
            && rect.width() < 4096.f
            && rect.height() < 4096.f
            && std::abs(rect.left) < 10000000.f
            && std::abs(rect.right) < 10000000.f
            && std::abs(rect.bottom) < 10000000.f
            && std::abs(rect.top) < 10000000.f;
    }

    static std::pair<RectF, CollisionApproximation> safeObjectRect(GameObject* obj) {
        cocos2d::CCRect rect = obj->m_objectRect;
        RectF runtimeRect {
            rect.getMinX(),
            rect.getMaxX(),
            rect.getMinY(),
            rect.getMaxY(),
        };

        bool validRuntimeRect = std::isfinite(runtimeRect.left)
            && std::isfinite(runtimeRect.right)
            && std::isfinite(runtimeRect.bottom)
            && std::isfinite(runtimeRect.top)
            && runtimeRect.right > runtimeRect.left
            && runtimeRect.top > runtimeRect.bottom
            && !obj->m_isObjectRectDirty;

        if (validRuntimeRect) {
            return { runtimeRect, CollisionApproximation::GameRect };
        }

        float scaleX = std::abs(obj->m_scaleX);
        float scaleY = std::abs(obj->m_scaleY);
        if (scaleX < 0.001f) scaleX = 1.f;
        if (scaleY < 0.001f) scaleY = 1.f;

        float width = std::abs(obj->m_width) * scaleX;
        float height = std::abs(obj->m_height) * scaleY;
        float radius = std::max(0.f, obj->m_objectRadius);
        if (width < 4.f) width = radius > 0.f ? radius * 2.f : 30.f;
        if (height < 4.f) height = radius > 0.f ? radius * 2.f : 30.f;

        float boxOffsetX = obj->m_boxOffsetCalculated ? obj->m_boxOffset.x : 0.f;
        float boxOffsetY = obj->m_boxOffsetCalculated ? obj->m_boxOffset.y : 0.f;
        float centerX = static_cast<float>(obj->m_positionX) + boxOffsetX + obj->m_customBoxOffset.x;
        float centerY = static_cast<float>(obj->m_positionY) + boxOffsetY + obj->m_customBoxOffset.y;

        RectF fallbackRect {
            centerX - width * 0.5f,
            centerX + width * 0.5f,
            centerY - height * 0.5f,
            centerY + height * 0.5f,
        };

        return { fallbackRect, CollisionApproximation::FallbackAABB };
    }

    std::optional<CachedLevelObject> buildObject(GameObject* obj) const {
        CachedLevelObject cached;
        cached.uniqueID = obj->m_uniqueID;
        cached.objectID = obj->m_objectID;
        cached.objectType = obj->m_objectType;
        cached.orbKind = classifyOrbID(obj->m_objectID);
        cached.padKind = classifyPadID(obj->m_objectID);
        cached.portalKind = classifyPortalID(obj->m_objectID);
        cached.targetMode = gameModeFromPortalKind(cached.portalKind);
        cached.speedValue = speedUnitsFromPortalKind(cached.portalKind);
        cached.rotation = obj->getRotation();
        cached.scaleX = obj->m_scaleX;
        cached.scaleY = obj->m_scaleY;
        cached.flipX = obj->m_isFlipX;
        cached.flipY = obj->m_isFlipY;
        cached.solidColorBlock = obj->m_isSolidColorBlock;
        cached.slopeIsHazard = obj->m_slopeIsHazard;
        cached.isGripSlope = obj->m_isGripSlope;
        cached.slopeDirection = obj->m_slopeDirection;

        auto [bounds, approximation] = safeObjectRect(obj);
        if (!isReasonableRect(bounds)) {
            return std::nullopt;
        }

        cached.bounds = bounds;
        cached.collisionApproximation = approximation;
        cached.runtimeRect = approximation == CollisionApproximation::GameRect;
        if (std::abs(cached.rotation) > 0.01f || obj->m_objectType == GameObjectType::Slope || obj->m_slopeIsHazard) {
            cached.collisionApproximation = CollisionApproximation::FallbackAABB;
        }

        if (isHazardID(obj->m_objectID) || obj->m_slopeIsHazard) {
            cached.category = CachedObjectCategory::Hazard;
        } else if (cached.orbKind != OrbKind::None) {
            cached.category = CachedObjectCategory::Orb;
        } else if (cached.padKind != PadKind::None) {
            cached.category = CachedObjectCategory::Pad;
        } else if (cached.portalKind != PortalKind::None) {
            cached.category = CachedObjectCategory::Portal;
        } else if (isSolidObject(obj)) {
            cached.category = CachedObjectCategory::Solid;
        } else {
            return std::nullopt;
        }

        return cached;
    }

    void logUnknownObject(GameObject* obj, char const* source) {
        if (!obj) return;
        int id = obj->m_objectID;
        if (!m_loggedUnknownIDs.insert(id).second) return;

        std::ostringstream line;
        line << "source=" << source
             << " id=" << id
             << " type=" << static_cast<int>(obj->m_objectType)
             << " x=" << obj->m_positionX
             << " y=" << obj->m_positionY;
        log::warn("[AutoBot] unknown gameplay object encountered | {}", line.str());
        AutoBotDebug::get()->logEvent("unknown-object", line.str());
    }
};

struct CubeCandidate {
    std::vector<int> tapTicks;
    bool activateFirstOrb = false;
    std::string label = "candidate";
};

struct CubeSimulationResult {
    CubeCandidate candidate;
    bool dies = false;
    bool landedSafely = false;
    bool hazardClearanceComputed = false;
    bool hazardClearanceApproximate = false;
    int deathTick = -1;
    int supportLostTick = -1;
    int fallStartedTick = -1;
    int landedTick = -1;
    int landingSupportUID = -1;
    int activatedOrbUID = -1;
    float score = -1000000.f;
    float minHazardClearance = std::numeric_limits<float>::infinity();
    std::string deathReason;
    SimState finalState;
};

class CollisionProvider {
public:
    static RectF playerBounds(SimState const& state) {
        return {
            state.x - state.halfWidth(),
            state.x + state.halfWidth(),
            state.y - state.halfHeight(),
            state.y + state.halfHeight(),
        };
    }

    static float inferGroundY(SimState const& state, std::vector<CachedLevelObject const*> const& objects) {
        float footY = state.y - state.halfHeight();
        float probeLeft = state.x - state.halfWidth() - 6.f;
        float probeRight = state.x + state.halfWidth() + 6.f;
        float bestTop = -std::numeric_limits<float>::infinity();
        bool found = false;

        for (auto const* object : objects) {
            if (!object || !object->isSolid()) continue;
            if (object->bounds.right < probeLeft || object->bounds.left > probeRight) continue;
            if (object->bounds.top <= footY + 18.f && object->bounds.top >= footY - 180.f) {
                bestTop = std::max(bestTop, object->bounds.top);
                found = true;
            }
        }

        if (!found) {
            return state.y - 600.f;
        }

        return bestTop + state.halfHeight();
    }

    static float inferCeilingY(SimState const& state, std::vector<CachedLevelObject const*> const& objects) {
        float headY = state.y + state.halfHeight();
        float probeLeft = state.x - state.halfWidth() - 6.f;
        float probeRight = state.x + state.halfWidth() + 6.f;
        float bestBottom = std::numeric_limits<float>::infinity();
        bool found = false;

        for (auto const* object : objects) {
            if (!object || !object->isSolid()) continue;
            if (object->bounds.right < probeLeft || object->bounds.left > probeRight) continue;
            if (object->bounds.bottom >= headY - 18.f && object->bounds.bottom <= headY + 220.f) {
                bestBottom = std::min(bestBottom, object->bounds.bottom);
                found = true;
            }
        }

        if (!found) {
            return state.y + 600.f;
        }

        return bestBottom - state.halfHeight();
    }

    static bool hasSupportAt(float centerX, float bottomY, float halfWidth, std::vector<CachedLevelObject const*> const& objects, float tolerance = 10.f) {
        float probeLeft = centerX - halfWidth;
        float probeRight = centerX + halfWidth;

        for (auto const* object : objects) {
            if (!object || !object->isSolid()) continue;
            if (object->bounds.right < probeLeft || object->bounds.left > probeRight) continue;
            if (std::abs(object->bounds.top - bottomY) <= tolerance) {
                return true;
            }
        }

        return false;
    }

    static bool hasHazardBelow(float centerX, float y, float halfWidth, std::vector<CachedLevelObject const*> const& objects, float maxDrop = 140.f) {
        float probeLeft = centerX - halfWidth - 12.f;
        float probeRight = centerX + halfWidth + 12.f;

        for (auto const* object : objects) {
            if (!object || !object->isHazard()) continue;
            if (object->bounds.right < probeLeft || object->bounds.left > probeRight) continue;

            float drop = y - object->bounds.top;
            if (drop >= -12.f && drop <= maxDrop) {
                return true;
            }
        }

        return false;
    }

    static bool resolveCubeSolids(SimState& state, RectF const& previousBounds, std::vector<CachedLevelObject const*> const& objects, std::string& deathReason) {
        RectF currentBounds = playerBounds(state);

        for (auto const* object : objects) {
            if (!object || !object->isSolid()) continue;
            if (!currentBounds.overlaps(object->bounds)) continue;

            state.worstApproximation = mergeApproximation(state.worstApproximation, object->collisionApproximation);

            bool overlapX = currentBounds.right > object->bounds.left + 1.f && currentBounds.left < object->bounds.right - 1.f;
            bool overlapY = currentBounds.top > object->bounds.bottom + 1.f && currentBounds.bottom < object->bounds.top - 1.f;

            if (!state.gravFlip && overlapX && previousBounds.bottom >= object->bounds.top - 8.f && currentBounds.bottom <= object->bounds.top + 2.f) {
                state.y = object->bounds.top + state.halfHeight();
                state.yVelocity = 0.f;
                state.onGround = true;
                state.currentSupportUID = object->uniqueID;
                currentBounds = playerBounds(state);
                continue;
            }

            if (state.gravFlip && overlapX && previousBounds.top <= object->bounds.bottom + 8.f && currentBounds.top >= object->bounds.bottom - 2.f) {
                state.y = object->bounds.bottom - state.halfHeight();
                state.yVelocity = 0.f;
                state.onGround = true;
                state.currentSupportUID = object->uniqueID;
                currentBounds = playerBounds(state);
                continue;
            }

            bool hitLeftWall = overlapY && previousBounds.right <= object->bounds.left + 4.f && currentBounds.right >= object->bounds.left;
            bool hitRightWall = overlapY && previousBounds.left >= object->bounds.right - 4.f && currentBounds.left <= object->bounds.right;
            bool hitUnderside = overlapX && !state.gravFlip && previousBounds.top <= object->bounds.bottom + 4.f && currentBounds.top >= object->bounds.bottom;
            bool hitTopside = overlapX && state.gravFlip && previousBounds.bottom >= object->bounds.top - 4.f && currentBounds.bottom <= object->bounds.top;

            if (hitLeftWall || hitRightWall) {
                deathReason = "solid-wall";
                state.isDead = true;
                return false;
            }

            if (hitUnderside || hitTopside) {
                state.y = hitUnderside ? object->bounds.bottom - state.halfHeight() : object->bounds.top + state.halfHeight();
                state.yVelocity = 0.f;
                currentBounds = playerBounds(state);
            }
        }

        return true;
    }

    static int findSupportingSolidUID(SimState const& state, std::vector<CachedLevelObject const*> const& objects, float tolerance = 12.f) {
        float footY = state.y - state.halfHeight();
        float probeLeft = state.x - state.halfWidth();
        float probeRight = state.x + state.halfWidth();

        for (auto const* object : objects) {
            if (!object || !object->isSolid()) continue;
            if (object->bounds.right < probeLeft || object->bounds.left > probeRight) continue;
            if (std::abs(object->bounds.top - footY) <= tolerance || std::abs(object->bounds.bottom - (state.y + state.halfHeight())) <= tolerance) {
                return object->uniqueID;
            }
        }

        return -1;
    }

    static bool isLikelySpike(CachedLevelObject const& object) {
        switch (object.objectID) {
            case 8: case 39: case 103: case 135:
            case 392: case 9: case 61: case 243: case 244:
            case 363: case 364: case 365: case 366:
            case 367: case 368: case 369: case 370:
            case 446: case 447: case 667: case 720:
            case 721: case 722:
                return true;
            default:
                return false;
        }
    }

    static bool spikeOverlapsApprox(RectF const& playerBounds, CachedLevelObject const& object) {
        float overlapBottom = std::max(playerBounds.bottom, object.bounds.bottom);
        float overlapTop = std::min(playerBounds.top, object.bounds.top);
        if (overlapTop <= overlapBottom) {
            return false;
        }

        float centerX = object.bounds.centerX();
        float height = std::max(object.bounds.height(), 0.001f);
        float halfWidth = object.bounds.width() * 0.5f;
        float sampleYs[3] = {
            overlapBottom + 1.f,
            (overlapBottom + overlapTop) * 0.5f,
            overlapTop - 1.f,
        };

        for (float sampleY : sampleYs) {
            if (sampleY <= object.bounds.bottom || sampleY >= object.bounds.top) continue;
            float rel = std::clamp((sampleY - object.bounds.bottom) / height, 0.f, 1.f);
            float activeHalfWidth = halfWidth * (1.f - rel * 0.92f);
            float lethalLeft = centerX - activeHalfWidth;
            float lethalRight = centerX + activeHalfWidth;
            if (playerBounds.right > lethalLeft && playerBounds.left < lethalRight) {
                return true;
            }
        }

        return false;
    }

    static float rectSeparation(RectF const& a, RectF const& b) {
        float horizontalGap = std::max({
            b.left - a.right,
            a.left - b.right,
            0.f,
        });
        float verticalGap = std::max({
            b.bottom - a.top,
            a.bottom - b.top,
            0.f,
        });

        if (horizontalGap > 0.f && verticalGap > 0.f) {
            return std::sqrt(horizontalGap * horizontalGap + verticalGap * verticalGap);
        }
        if (horizontalGap > 0.f) {
            return horizontalGap;
        }
        if (verticalGap > 0.f) {
            return verticalGap;
        }
        return 0.f;
    }

    static float spikeClearanceApprox(RectF const& playerBounds, CachedLevelObject const& object) {
        float centerX = object.bounds.centerX();
        float height = std::max(object.bounds.height(), 0.001f);
        float halfWidth = object.bounds.width() * 0.5f;
        float bestClearance = std::numeric_limits<float>::infinity();
        float sampleYs[5] = {
            object.bounds.bottom + 1.f,
            object.bounds.bottom + height * 0.25f,
            object.bounds.bottom + height * 0.50f,
            object.bounds.bottom + height * 0.75f,
            object.bounds.top - 1.f,
        };

        for (float sampleY : sampleYs) {
            if (sampleY <= object.bounds.bottom || sampleY >= object.bounds.top) continue;

            float rel = std::clamp((sampleY - object.bounds.bottom) / height, 0.f, 1.f);
            float activeHalfWidth = halfWidth * (1.f - rel * 0.92f);
            RectF lethalBand {
                centerX - activeHalfWidth,
                centerX + activeHalfWidth,
                sampleY - 1.f,
                sampleY + 1.f,
            };
            bestClearance = std::min(bestClearance, rectSeparation(playerBounds, lethalBand));
        }

        if (!std::isfinite(bestClearance)) {
            return rectSeparation(playerBounds, object.bounds);
        }
        return bestClearance;
    }

    static void updateMinHazardClearance(RectF const& bounds, std::vector<CachedLevelObject const*> const& objects, CubeSimulationResult& result) {
        for (auto const* object : objects) {
            if (!object || !object->isHazard()) continue;

            float clearance = isLikelySpike(*object)
                ? spikeClearanceApprox(bounds, *object)
                : rectSeparation(bounds, object->bounds);
            if (!std::isfinite(clearance)) continue;

            result.hazardClearanceComputed = true;
            if (isLikelySpike(*object) || object->collisionApproximation != CollisionApproximation::GameRect) {
                result.hazardClearanceApproximate = true;
            }
            result.minHazardClearance = std::min(result.minHazardClearance, clearance);
        }
    }

    static bool touchesHazard(SimState& state, std::vector<CachedLevelObject const*> const& objects, std::string& deathReason) {
        RectF bounds = playerBounds(state);

        for (auto const* object : objects) {
            if (!object || !object->isHazard()) continue;
            bool overlapsHazard = isLikelySpike(*object)
                ? spikeOverlapsApprox(bounds, *object)
                : bounds.overlaps(object->bounds);
            if (!overlapsHazard) continue;

            state.worstApproximation = mergeApproximation(
                state.worstApproximation,
                isLikelySpike(*object) ? CollisionApproximation::FallbackAABB : object->collisionApproximation
            );
            deathReason = "hazard";
            state.isDead = true;
            return true;
        }

        return false;
    }
};

class RollingLookaheadPlanner {
public:
    void reset() {
        m_lastDecision = {};
    }

    PlannerDecision planCube(PlayerSnapshot const& snapshot, std::vector<CachedLevelObject const*> const& objects, PlannerConfig const& config) {
        PlannerDecision decision;
        decision.valid = true;
        decision.horizonSeconds = config.rollingPlannerEnabled
            ? std::clamp(config.horizonSeconds, 1.0f, 1.5f)
            : 0.5f;
        decision.leadFrames = snapshot.isMini ? 12.5f : 10.5f;
        decision.configuredSafetyTicks = std::max(0, config.cubeTimingSafetyTicks);

        float playerFront = snapshot.x + (snapshot.isMini ? 7.5f : 15.f);
        float footY = snapshot.y - (snapshot.isMini ? 7.5f : 15.f);
        float checkDist = std::clamp(snapshot.observedXSpeed * 18.0f, 90.0f, 260.0f);
        float checkLeft = playerFront - 4.f;
        float checkRight = playerFront + checkDist;

        for (auto const* object : objects) {
            if (!object || !object->isHazard()) continue;
            if (object->bounds.right < checkLeft || object->bounds.left > checkRight) continue;

            float hazardWidth = object->bounds.width();
            float hazardHeight = object->bounds.height();
            if (hazardWidth < 2.f || hazardWidth > 80.f) continue;
            if (hazardHeight < 4.f || hazardHeight > 60.f) continue;

            bool crossesGroundBand = !(object->bounds.top < footY - 10.f || object->bounds.bottom > footY + 40.f);
            bool rootedOnFloor = std::abs(object->bounds.bottom - footY) <= 16.f;
            bool risesIntoPlayer = object->bounds.top >= footY + 12.f;
            if (!crossesGroundBand && !(rootedOnFloor && risesIntoPlayer)) continue;

            float distanceAhead = object->bounds.left - playerFront;
            if (distanceAhead < decision.nearestHazardDistance) {
                decision.nearestHazardDistance = distanceAhead;
            }
        }

        float safeXSpeed = std::max(snapshot.observedXSpeed, 0.001f);
        decision.timeToHazard = std::isfinite(decision.nearestHazardDistance)
            ? decision.nearestHazardDistance / safeXSpeed
            : std::numeric_limits<float>::infinity();

        int horizonTicks = std::clamp(
            static_cast<int>(std::round(config.ticksPerSecond * decision.horizonSeconds)),
            45,
            360
        );
        constexpr int kDropWaitImminentThreshold = 24;

        auto noJump = simulateCubeCandidate(snapshot, objects, config, CubeCandidate { {}, false, "no-input" }, horizonTicks);
        decision.noJumpDeathTick = noJump.deathTick;
        decision.noInputSupportLostTick = noJump.supportLostTick;
        decision.noInputFallStartedTick = noJump.fallStartedTick;
        decision.noInputLandedTick = noJump.landedTick;
        decision.noInputLandedSafely = noJump.landedSafely;
        decision.noInputLandingSupportUID = noJump.landingSupportUID;
        decision.predictedDeathTick = noJump.deathTick;
        decision.collisionApproximation = noJump.finalState.worstApproximation;

        if (!noJump.dies) {
            decision.noInputWon = true;
            if (std::isfinite(decision.nearestHazardDistance)) {
                std::ostringstream reason;
                reason << "cube-hazard-wait dist=" << decision.nearestHazardDistance
                       << " timeToHazard=" << decision.timeToHazard
                       << " leadFrames=" << decision.leadFrames
                       << " noJumpSurvives=1"
                       << " supportLostTick=" << noJump.supportLostTick
                       << " fallStartedTick=" << noJump.fallStartedTick
                       << " landedTick=" << noJump.landedTick
                       << " landedSafely=" << noJump.landedSafely
                       << " noJumpIncluded=1"
                       << " noJumpScore=" << noJump.score;
                decision.reason = reason.str();
            } else {
                decision.reason = "cube-clear";
            }

            m_lastDecision = decision;
            return decision;
        }

        bool hasOrbCandidate = false;
        if (config.rollingPlannerEnabled) {
            for (auto const* object : objects) {
                if (!object || !object->isOrb()) continue;
                if (object->bounds.left >= snapshot.x - 32.f && object->bounds.left <= snapshot.x + snapshot.observedXSpeed * horizonTicks + 120.f) {
                    hasOrbCandidate = true;
                    break;
                }
            }
        }

        auto addDelay = [horizonTicks](std::vector<int>& delays, int tick) {
            if (tick < 0 || tick >= horizonTicks) return;
            delays.push_back(tick);
        };

        std::vector<int> delaySamples;
        for (int tick = 0; tick < std::min(horizonTicks, 30); ++tick) {
            addDelay(delaySamples, tick);
        }
        for (int tick = 30; tick < std::min(horizonTicks, 120); tick += 2) {
            addDelay(delaySamples, tick);
        }
        for (int tick = 120; tick < std::min(horizonTicks, 240); tick += 4) {
            addDelay(delaySamples, tick);
        }
        for (int tick = 240; tick < horizonTicks; tick += 8) {
            addDelay(delaySamples, tick);
        }

        addDelay(delaySamples, noJump.supportLostTick);
        addDelay(delaySamples, noJump.supportLostTick + 1);
        addDelay(delaySamples, noJump.supportLostTick + 2);
        addDelay(delaySamples, noJump.landedTick);
        addDelay(delaySamples, noJump.landedTick + 1);
        addDelay(delaySamples, noJump.landedTick + 2);
        addDelay(delaySamples, noJump.landedTick + 4);
        addDelay(delaySamples, noJump.deathTick - 1);
        addDelay(delaySamples, static_cast<int>(std::floor(decision.timeToHazard)));

        std::sort(delaySamples.begin(), delaySamples.end());
        delaySamples.erase(std::unique(delaySamples.begin(), delaySamples.end()), delaySamples.end());
        if (!delaySamples.empty()) {
            decision.maxDelayTested = delaySamples.back();
        }

        std::vector<CubeSimulationResult> candidates;
        candidates.reserve(static_cast<size_t>(delaySamples.size()) * (hasOrbCandidate ? 4u : 2u) + 1u);
        candidates.push_back(noJump);

        auto candidateLabelForDelay = [&noJump](int delay, bool activateOrb) {
            std::string label = "tap-after-delay";

            if (noJump.supportLostTick >= 0) {
                if (delay == noJump.supportLostTick || delay == noJump.supportLostTick + 1) {
                    label = "jump-after-support-loss";
                }
                else if (delay > noJump.supportLostTick && (noJump.landedTick < 0 || delay < noJump.landedTick)) {
                    label = "drop-then-tap";
                }
            }

            if (noJump.landedTick >= 0) {
                if (delay == noJump.landedTick + 1 || delay == noJump.landedTick + 2 || delay == noJump.landedTick + 4) {
                    label = "drop-then-jump-on-landing";
                }
                else if (delay > noJump.landedTick) {
                    label = "jump-after-safe-drop";
                }
            }

            if (activateOrb) {
                label += "-orb";
            }
            return label;
        };

        for (int delay : delaySamples) {
            candidates.push_back(simulateCubeCandidate(snapshot, objects, config, CubeCandidate { { delay }, false, candidateLabelForDelay(delay, false) }, horizonTicks));
            if (hasOrbCandidate) {
                candidates.push_back(simulateCubeCandidate(snapshot, objects, config, CubeCandidate { { delay }, true, candidateLabelForDelay(delay, true) }, horizonTicks));
            }
        }

        auto oneTapResults = candidates;
        std::sort(oneTapResults.begin(), oneTapResults.end(), [](CubeSimulationResult const& a, CubeSimulationResult const& b) {
            if (a.dies != b.dies) return !a.dies;
            if (!a.dies && !b.dies) return a.score > b.score;
            return a.deathTick > b.deathTick;
        });

        size_t expandedCount = std::min<size_t>(8, oneTapResults.size());
        for (size_t i = 0; i < expandedCount; ++i) {
            auto const& base = oneTapResults[i];
            if (base.candidate.tapTicks.empty()) continue;

            std::vector<int> secondTapTicks;
            addDelay(secondTapTicks, base.landedTick);
            addDelay(secondTapTicks, base.landedTick + 1);
            addDelay(secondTapTicks, base.landedTick + 2);
            addDelay(secondTapTicks, base.landedTick + 4);
            addDelay(secondTapTicks, base.deathTick - 1);
            std::sort(secondTapTicks.begin(), secondTapTicks.end());
            secondTapTicks.erase(std::unique(secondTapTicks.begin(), secondTapTicks.end()), secondTapTicks.end());

            for (int secondTap : secondTapTicks) {
                if (secondTap <= base.candidate.tapTicks.front()) continue;
                candidates.push_back(simulateCubeCandidate(
                    snapshot,
                    objects,
                    config,
                    CubeCandidate { { base.candidate.tapTicks.front(), secondTap }, false, "tap-then-tap" },
                    horizonTicks
                ));
            }
        }

        decision.candidateCount = static_cast<int>(candidates.size());
        decision.maxTapCountTested = 2;

        std::vector<CubeSimulationResult const*> safeJumpCandidates;
        safeJumpCandidates.reserve(candidates.size());
        for (auto const& candidate : candidates) {
            if (candidate.candidate.tapTicks.empty()) continue;
            if (candidate.dies) continue;
            safeJumpCandidates.push_back(&candidate);
        }

        if (!safeJumpCandidates.empty()) {
            std::vector<int> safeTapTimings;
            safeTapTimings.reserve(safeJumpCandidates.size());
            for (auto const* candidate : safeJumpCandidates) {
                safeTapTimings.push_back(candidate->candidate.tapTicks.front());
            }
            std::sort(safeTapTimings.begin(), safeTapTimings.end());
            safeTapTimings.erase(std::unique(safeTapTimings.begin(), safeTapTimings.end()), safeTapTimings.end());

            decision.safeTapCount = static_cast<int>(safeTapTimings.size());
            decision.earliestSafeTap = safeTapTimings.front();
            decision.latestSafeTap = safeTapTimings.back();
            decision.safeWindowWidth = decision.latestSafeTap - decision.earliestSafeTap;

            for (size_t i = 1; i < safeTapTimings.size(); ++i) {
                if (safeTapTimings[i] != safeTapTimings[i - 1] + 1) {
                    decision.safeTapSparse = true;
                    break;
                }
            }

            if (decision.safeTapCount == 1) {
                decision.framePerfectFallbackUsed = true;
                decision.effectiveSafetyTicks = 0;
                decision.targetSafeTap = decision.earliestSafeTap;
            } else {
                decision.effectiveSafetyTicks = std::min(decision.configuredSafetyTicks, std::max(0, decision.safeWindowWidth / 2));
                decision.targetSafeTap = decision.latestSafeTap - decision.effectiveSafetyTicks;
            }

            std::vector<CubeSimulationResult const*> bufferedSafeCandidates;
            bufferedSafeCandidates.reserve(safeJumpCandidates.size());
            int closestDistance = std::numeric_limits<int>::max();
            for (auto const* candidate : safeJumpCandidates) {
                int tap = candidate->candidate.tapTicks.front();
                int distanceToTarget = std::abs(tap - decision.targetSafeTap);
                closestDistance = std::min(closestDistance, distanceToTarget);
            }

            for (auto const* candidate : safeJumpCandidates) {
                int tap = candidate->candidate.tapTicks.front();
                int distanceToTarget = std::abs(tap - decision.targetSafeTap);
                if (distanceToTarget == closestDistance) {
                    bufferedSafeCandidates.push_back(candidate);
                }
            }

            auto const* selected = bufferedSafeCandidates.front();
            int selectedTapFromFullSafeSet = selected->candidate.tapTicks.front();
            int bestDistanceToTarget = std::abs(selected->candidate.tapTicks.front() - decision.targetSafeTap);
            for (auto const* candidate : bufferedSafeCandidates) {
                int tap = candidate->candidate.tapTicks.front();
                int distanceToTarget = std::abs(tap - decision.targetSafeTap);
                if (distanceToTarget < bestDistanceToTarget) {
                    selected = candidate;
                    bestDistanceToTarget = distanceToTarget;
                    continue;
                }
                if (distanceToTarget == bestDistanceToTarget) {
                    if (candidate->minHazardClearance > selected->minHazardClearance) {
                        selected = candidate;
                        continue;
                    }
                    if (candidate->minHazardClearance == selected->minHazardClearance && candidate->score > selected->score) {
                        selected = candidate;
                    }
                }
            }

            bool noPressGateUsed = false;
            bool dropWaitGateUsed = false;
            bool holdForDropReplan = false;
            bool immediatePressRequired = selectedTapFromFullSafeSet == 0;
            std::string noPressGateReason = "disabled";
            std::string noInputRejectedReason = noJump.deathReason.empty() ? "eventual-death" : noJump.deathReason;
            std::string immediateTapSelectionReason = "buffered-safe-window";

            CubeSimulationResult const* delayedAlternative = nullptr;
            CubeSimulationResult const* delayedDropAlternative = nullptr;
            auto isDropSpecificLabel = [](std::string const& label) {
                return label.find("drop") != std::string::npos || label.find("support-loss") != std::string::npos;
            };

            if (selectedTapFromFullSafeSet == 0) {
                for (auto const* candidate : safeJumpCandidates) {
                    if (candidate->candidate.tapTicks.empty()) continue;
                    if (candidate->candidate.tapTicks.front() <= 0) continue;

                    if (!delayedAlternative
                     || candidate->candidate.tapTicks.front() < delayedAlternative->candidate.tapTicks.front()
                     || (candidate->candidate.tapTicks.front() == delayedAlternative->candidate.tapTicks.front() && candidate->minHazardClearance > delayedAlternative->minHazardClearance)
                     || (candidate->candidate.tapTicks.front() == delayedAlternative->candidate.tapTicks.front() && candidate->minHazardClearance == delayedAlternative->minHazardClearance && candidate->score > delayedAlternative->score)) {
                        delayedAlternative = candidate;
                    }

                    if (isDropSpecificLabel(candidate->candidate.label)) {
                        if (!delayedDropAlternative
                         || candidate->candidate.tapTicks.front() < delayedDropAlternative->candidate.tapTicks.front()
                         || (candidate->candidate.tapTicks.front() == delayedDropAlternative->candidate.tapTicks.front() && candidate->minHazardClearance > delayedDropAlternative->minHazardClearance)
                         || (candidate->candidate.tapTicks.front() == delayedDropAlternative->candidate.tapTicks.front() && candidate->minHazardClearance == delayedDropAlternative->minHazardClearance && candidate->score > delayedDropAlternative->score)) {
                            delayedDropAlternative = candidate;
                        }
                    }
                }

                bool dropScenario = noJump.supportLostTick >= 0 || noJump.fallStartedTick >= 0;
                bool noInputSafeForNow = noJump.deathTick < 0 || noJump.deathTick > kDropWaitImminentThreshold;
                if (dropScenario && noInputSafeForNow) {
                    dropWaitGateUsed = true;
                    holdForDropReplan = true;
                    if (delayedDropAlternative) {
                        selected = delayedDropAlternative;
                    } else if (delayedAlternative) {
                        selected = delayedAlternative;
                    }
                    noPressGateReason = "drop-specific-wait";
                    noInputRejectedReason = "safe-for-now-drop-replan";
                    immediateTapSelectionReason = "drop-specific-wait-replan";
                    immediatePressRequired = false;
                } else if (dropScenario && !noInputSafeForNow) {
                    noPressGateReason = "drop-wait-rejected-imminent-death";
                    immediateTapSelectionReason = "immediate-required-imminent-drop-death";
                } else {
                    immediateTapSelectionReason = "flat-ground-buffered-immediate";
                }
            }

            decision.selectedTap = holdForDropReplan && selected->candidate.tapTicks.front() == 0
                ? -1
                : selected->candidate.tapTicks.front();
            decision.chosenDelay = decision.selectedTap;
            decision.selectedTapScore = selected->score;
            decision.selectedTapClearance = selected->minHazardClearance;
            decision.selectedTapClearanceComputed = selected->hazardClearanceComputed;
            decision.selectedTapClearanceApproximate = selected->hazardClearanceApproximate;
            decision.selectedTapLabel = holdForDropReplan && selected->candidate.tapTicks.front() == 0
                ? "safe-drop-replan"
                : selected->candidate.label;
            decision.predictedDeathTick = selected->deathTick;
            decision.activatedOrbUID = selected->activatedOrbUID;
            decision.collisionApproximation = selected->finalState.worstApproximation;
            decision.shouldPress = !holdForDropReplan && decision.selectedTap == 0;
            decision.safetyBufferedTimingUsed = decision.safeTapCount > 1 && decision.selectedTap != decision.latestSafeTap;
            decision.noPressGateUsed = noPressGateUsed;
            decision.dropWaitGateUsed = dropWaitGateUsed;
            decision.immediatePressRequired = immediatePressRequired;
            decision.safeJumpCandidatesTotal = static_cast<int>(safeJumpCandidates.size());
            decision.safeTapTimingsBeforeFiltering = static_cast<int>(safeTapTimings.size());
            decision.safeTapTimingsAfterFiltering = static_cast<int>(bufferedSafeCandidates.size());
            decision.selectedTapFromFullSafeSet = selectedTapFromFullSafeSet;
            decision.selectedTapAfterBuffer = decision.selectedTap;
            decision.noJumpDeathReason = noJump.deathReason;
            decision.bestDeathReason = selected->deathReason;
            decision.noPressGateReason = noPressGateReason;
            decision.noInputRejectedReason = noInputRejectedReason;
            decision.immediateTapSelectionReason = immediateTapSelectionReason;

            std::ostringstream reason;
            reason << (decision.dropWaitGateUsed ? "cube-safe-drop-replan" : (decision.shouldPress ? "cube-hazard" : "cube-hazard-wait"))
                   << " dist=" << decision.nearestHazardDistance
                   << " timeToHazard=" << decision.timeToHazard
                   << " leadFrames=" << decision.leadFrames
                   << " noJumpDeathTick=" << decision.noJumpDeathTick
                   << " noJumpScore=" << noJump.score
                   << " noJumpIncluded=1"
                    << " safeJumpCandidatesTotal=" << decision.safeJumpCandidatesTotal
                    << " safeTapTimingsBeforeFiltering=" << decision.safeTapTimingsBeforeFiltering
                    << " safeTapTimingsAfterFiltering=" << decision.safeTapTimingsAfterFiltering
                   << " safeTapCount=" << decision.safeTapCount
                   << " earliestSafeTap=" << decision.earliestSafeTap
                   << " latestSafeTap=" << decision.latestSafeTap
                   << " safeWindowWidth=" << decision.safeWindowWidth
                   << " configuredSafetyTicks=" << decision.configuredSafetyTicks
                   << " effectiveSafetyTicks=" << decision.effectiveSafetyTicks
                   << " targetSafeTap=" << decision.targetSafeTap
                   << " selectedTapFromFullSafeSet=" << decision.selectedTapFromFullSafeSet
                   << " selectedTapAfterBuffer=" << decision.selectedTapAfterBuffer
                   << " selectedTap=" << decision.selectedTap
                   << " selectedTapLabel=" << decision.selectedTapLabel
                   << " selectedTapScore=" << decision.selectedTapScore
                   << " selectedTapClearance=" << decision.selectedTapClearance
                   << " selectedTapClearanceComputed=" << decision.selectedTapClearanceComputed
                   << " selectedTapClearanceApproximate=" << decision.selectedTapClearanceApproximate
                    << " supportLostTick=" << noJump.supportLostTick
                    << " fallStartedTick=" << noJump.fallStartedTick
                    << " landedTick=" << noJump.landedTick
                    << " landedSafely=" << noJump.landedSafely
                    << " bufferedTiming=" << decision.safetyBufferedTimingUsed
                    << " framePerfectFallback=" << decision.framePerfectFallbackUsed
                    << " sparseSafeTaps=" << decision.safeTapSparse
                    << " noPressGateUsed=" << decision.noPressGateUsed
                   << " noPressGateReason=" << decision.noPressGateReason
                   << " dropWaitGateUsed=" << decision.dropWaitGateUsed
                   << " immediatePressRequired=" << decision.immediatePressRequired
                   << " noJumpDeathReason=" << decision.noJumpDeathReason
                   << " noInputRejectedReason=" << decision.noInputRejectedReason
                   << " immediateTapSelectionReason=" << decision.immediateTapSelectionReason;
            if (selected->candidate.tapTicks.size() > 1) {
                reason << " secondTap=" << selected->candidate.tapTicks[1];
            }
            if (selected->candidate.activateFirstOrb) {
                reason << " activateOrb=1";
            }
            decision.reason = reason.str();
            m_lastDecision = decision;
            return decision;
        }

        auto isBetter = [](CubeSimulationResult const& lhs, CubeSimulationResult const& rhs) {
            if (lhs.dies != rhs.dies) return !lhs.dies;
            if (!lhs.dies && !rhs.dies) {
                float scoreDelta = lhs.score - rhs.score;
                if (std::abs(scoreDelta) > 5.f) {
                    return scoreDelta > 0.f;
                }

                float clearanceDelta = lhs.minHazardClearance - rhs.minHazardClearance;
                if (std::abs(clearanceDelta) > 0.5f) {
                    return clearanceDelta > 0.f;
                }

                int lhsFirstTap = lhs.candidate.tapTicks.empty() ? std::numeric_limits<int>::max() : lhs.candidate.tapTicks.front();
                int rhsFirstTap = rhs.candidate.tapTicks.empty() ? std::numeric_limits<int>::max() : rhs.candidate.tapTicks.front();
                return lhsFirstTap > rhsFirstTap;
            }
            if (lhs.deathTick != rhs.deathTick) {
                return lhs.deathTick > rhs.deathTick;
            }
            return lhs.score > rhs.score;
        };

        CubeSimulationResult best = noJump;
        for (auto const& candidate : candidates) {
            if (isBetter(candidate, best)) {
                best = candidate;
            }
        }

        decision.chosenDelay = best.candidate.tapTicks.empty() ? -1 : best.candidate.tapTicks.front();
        decision.selectedTap = decision.chosenDelay;
        decision.predictedDeathTick = best.deathTick;
        decision.activatedOrbUID = best.activatedOrbUID;
        decision.collisionApproximation = best.finalState.worstApproximation;
        decision.selectedTapScore = best.score;
        decision.selectedTapClearance = best.minHazardClearance;
        decision.selectedTapClearanceComputed = best.hazardClearanceComputed;
        decision.selectedTapClearanceApproximate = best.hazardClearanceApproximate;
        decision.selectedTapLabel = best.candidate.label;
        decision.noJumpDeathReason = noJump.deathReason;
        decision.bestDeathReason = best.deathReason;
        decision.noInputRejectedReason = noJump.deathReason.empty() ? "eventual-death" : noJump.deathReason;

        if (!best.dies) {
            decision.shouldPress = !best.candidate.tapTicks.empty() && best.candidate.tapTicks.front() == 0;
            decision.immediateTapSelectionReason = decision.shouldPress
                ? "best-safe-or-longest-survivor-immediate"
                : "best-safe-or-longest-survivor-delayed";
            std::ostringstream reason;
            reason << (decision.shouldPress ? "cube-hazard" : "cube-hazard-wait")
                   << " dist=" << decision.nearestHazardDistance
                   << " timeToHazard=" << decision.timeToHazard
                   << " leadFrames=" << decision.leadFrames
                   << " noJumpDeathStep=" << noJump.deathTick
                   << " supportLostTick=" << noJump.supportLostTick
                   << " fallStartedTick=" << noJump.fallStartedTick
                   << " landedTick=" << noJump.landedTick
                   << " landedSafely=" << noJump.landedSafely
                   << " noJumpIncluded=1"
                   << " bestLabel=" << best.candidate.label
                   << " bestScore=" << best.score
                   << " noJumpScore=" << noJump.score
                   << " selectedTapScore=" << best.score
                   << " selectedTapClearance=" << best.minHazardClearance
                   << " selectedTapClearanceComputed=" << decision.selectedTapClearanceComputed
                   << " selectedTapClearanceApproximate=" << decision.selectedTapClearanceApproximate
                   << " noJumpDeathReason=" << decision.noJumpDeathReason
                   << " noInputRejectedReason=" << decision.noInputRejectedReason
                   << " immediateTapSelectionReason=" << decision.immediateTapSelectionReason
                   << " chosenDelay=" << decision.chosenDelay;
            if (best.candidate.tapTicks.size() > 1) {
                reason << " secondTap=" << best.candidate.tapTicks[1];
            }
            if (best.candidate.activateFirstOrb) {
                reason << " activateOrb=1";
            }
            decision.reason = reason.str();
            m_lastDecision = decision;
            return decision;
        }

        if (noJump.deathTick >= 0 && noJump.deathTick <= 4) {
            decision.shouldPress = !best.candidate.tapTicks.empty() && best.candidate.tapTicks.front() == 0;
            decision.emergency = true;
            decision.reason = "cube-hazard-emergency noJumpIncluded=1";
            m_lastDecision = decision;
            return decision;
        }

        std::ostringstream reason;
        reason << "cube-hazard-failing-all dist=" << decision.nearestHazardDistance
               << " timeToHazard=" << decision.timeToHazard
               << " leadFrames=" << decision.leadFrames
               << " noJumpDeathStep=" << noJump.deathTick
               << " supportLostTick=" << noJump.supportLostTick
               << " fallStartedTick=" << noJump.fallStartedTick
               << " landedTick=" << noJump.landedTick
               << " landedSafely=" << noJump.landedSafely
               << " noJumpIncluded=1"
               << " bestLabel=" << best.candidate.label
               << " bestScore=" << best.score
               << " noJumpScore=" << noJump.score
               << " selectedTapScore=" << best.score
               << " selectedTapClearance=" << best.minHazardClearance
               << " selectedTapClearanceComputed=" << decision.selectedTapClearanceComputed
               << " selectedTapClearanceApproximate=" << decision.selectedTapClearanceApproximate
               << " noJumpDeathReason=" << decision.noJumpDeathReason
               << " noInputRejectedReason=" << decision.noInputRejectedReason
                << " bestDelay=" << decision.chosenDelay
                << " bestDeathStep=" << best.deathTick;
        decision.reason = reason.str();
        decision.shouldPress = !best.candidate.tapTicks.empty() && best.candidate.tapTicks.front() == 0;
        decision.immediateTapSelectionReason = decision.shouldPress
            ? "failing-all-immediate-best-effort"
            : "failing-all-delayed-best-effort";
        decision.noInputWon = best.candidate.tapTicks.empty();
        m_lastDecision = decision;
        return decision;
    }

    PlannerDecision planUnsupported(PlayerSnapshot const& snapshot, PlannerConfig const& config) {
        PlannerDecision decision;
        decision.valid = true;
        decision.horizonSeconds = config.horizonSeconds;
        decision.reason = config.experimentalMultiMode
            ? "experimental-mode-not-implemented"
            : "mode-unsupported";
        decision.collisionApproximation = CollisionApproximation::Unknown;
        if (snapshot.inputHeld) {
            decision.shouldPress = false;
        }
        m_lastDecision = decision;
        return decision;
    }

private:
    PlannerDecision m_lastDecision;

    static SimState makeSimState(PlayerSnapshot const& snapshot) {
        SimState state;
        state.x = snapshot.x;
        state.y = snapshot.y;
        state.xSpeed = snapshot.observedXSpeed;
        state.yVelocity = snapshot.yVelocity;
        state.gravityPerTick = snapshot.gravityPerTick;
        state.onGround = snapshot.onGround;
        state.gravFlip = snapshot.gravFlip;
        state.isMini = snapshot.isMini;
        state.isDead = snapshot.isDead;
        state.inputHeld = snapshot.inputHeld;
        state.touchedRing = snapshot.touchedRing;
        state.touchedPad = snapshot.touchedPad;
        state.jumpBuffered = snapshot.jumpBuffered;
        state.mode = snapshot.mode;
        state.currentSupportUID = -1;
        state.worstApproximation = snapshot.gravityApproximate || snapshot.speedApproximate
            ? CollisionApproximation::FallbackAABB
            : CollisionApproximation::GameRect;
        return state;
    }

    static float jumpVelocity(SimState const& state) {
        return state.isMini ? kDefaultMiniJumpVelocity : kDefaultJumpVelocity;
    }

    static void stepCubePhysics(SimState& state, bool hold) {
        if (state.onGround && hold) {
            state.yVelocity = jumpVelocity(state) * (state.gravFlip ? -1.f : 1.f);
            state.onGround = false;
            state.currentSupportUID = -1;
        }

        float gravity = state.gravityPerTick;
        if (state.gravFlip && gravity < 0.f) gravity = -gravity;
        if (!state.gravFlip && gravity > 0.f) gravity = -gravity;
        state.yVelocity += gravity;
        state.y += state.yVelocity;
        state.x += state.xSpeed;
    }

    static void applyPortalEffect(SimState& state, CachedLevelObject const& object) {
        switch (object.portalKind) {
            case PortalKind::GravityFlip:
                state.gravFlip = true;
                break;
            case PortalKind::GravityNormal:
                state.gravFlip = false;
                break;
            case PortalKind::SpeedHalf:
            case PortalKind::SpeedNormal:
            case PortalKind::SpeedDouble:
            case PortalKind::SpeedTriple:
            case PortalKind::SpeedQuad:
                state.xSpeed = object.speedValue;
                break;
            case PortalKind::ModeCube:
            case PortalKind::ModeShip:
            case PortalKind::ModeBall:
            case PortalKind::ModeUFO:
            case PortalKind::ModeWave:
            case PortalKind::ModeRobot:
            case PortalKind::ModeSpider:
            case PortalKind::ModeSwing:
                state.mode = object.targetMode;
                break;
            case PortalKind::SizeMini:
                state.isMini = true;
                break;
            case PortalKind::SizeNormal:
                state.isMini = false;
                break;
            default:
                break;
        }
    }

    static void applyPadEffect(SimState& state, CachedLevelObject const& object) {
        switch (object.padKind) {
            case PadKind::BlueGravity:
                state.gravFlip = !state.gravFlip;
                break;
            default:
                break;
        }

        state.yVelocity = jumpVelocity(state) * (state.gravFlip ? -1.f : 1.f);
        state.onGround = false;
        state.touchedPad = true;
    }

    static void applyOrbEffect(SimState& state, CachedLevelObject const& object) {
        switch (object.orbKind) {
            case OrbKind::Black:
                state.yVelocity = -jumpVelocity(state) * (state.gravFlip ? -1.f : 1.f);
                break;

            case OrbKind::BlueGravity:
            case OrbKind::GreenGravity:
                state.gravFlip = !state.gravFlip;
                state.yVelocity = jumpVelocity(state) * (state.gravFlip ? -1.f : 1.f);
                break;

            case OrbKind::DashGreen:
            case OrbKind::DashMagenta:
                state.yVelocity = jumpVelocity(state) * 1.2f * (state.gravFlip ? -1.f : 1.f);
                state.x += state.xSpeed * 1.5f;
                break;

            default:
                state.yVelocity = jumpVelocity(state) * (state.gravFlip ? -1.f : 1.f);
                break;
        }

        state.onGround = false;
        state.touchedRing = true;
    }

    static bool overlaps(RectF const& playerBounds, CachedLevelObject const& object) {
        return playerBounds.overlaps(object.bounds);
    }

    static CubeSimulationResult simulateCubeCandidate(
        PlayerSnapshot const& snapshot,
        std::vector<CachedLevelObject const*> const& objects,
        PlannerConfig const& config,
        CubeCandidate const& candidate,
        int horizonTicks
    ) {
        CubeSimulationResult result;
        result.candidate = candidate;

        SimState state = makeSimState(snapshot);
        state.currentSupportUID = CollisionProvider::findSupportingSolidUID(state, objects);
        bool firstOrbConsumed = false;

        for (int step = 0; step < horizonTicks; ++step) {
            RectF previousBounds = CollisionProvider::playerBounds(state);
            bool hold = false;
            for (int tapTick : candidate.tapTicks) {
                if (step == tapTick) {
                    hold = true;
                    break;
                }
            }

            bool wasOnGround = state.onGround;
            int previousSupportUID = state.currentSupportUID;

            stepCubePhysics(state, hold);
            state.onGround = false;
            state.currentSupportUID = -1;

            std::string deathReason;
            if (!CollisionProvider::resolveCubeSolids(state, previousBounds, objects, deathReason)) {
                result.dies = true;
                result.deathTick = step;
                result.deathReason = deathReason;
                result.finalState = state;
                result.score = -500000.f + static_cast<float>(step) * 1000.f;
                return result;
            }

            RectF currentBounds = CollisionProvider::playerBounds(state);

            for (auto const* object : objects) {
                if (!object || !overlaps(currentBounds, *object)) continue;

                state.worstApproximation = mergeApproximation(state.worstApproximation, object->collisionApproximation);

                if (object->isPad() && state.lastPadUID != object->uniqueID) {
                    state.lastPadUID = object->uniqueID;
                    applyPadEffect(state, *object);
                    currentBounds = CollisionProvider::playerBounds(state);
                    continue;
                }

                if (object->isPortal() && state.lastPortalUID != object->uniqueID) {
                    state.lastPortalUID = object->uniqueID;
                    applyPortalEffect(state, *object);
                    currentBounds = CollisionProvider::playerBounds(state);
                    if (state.mode != GameMode::Cube) {
                        result.dies = true;
                        result.deathTick = step;
                        result.deathReason = "unsupported-mode-transition";
                        result.finalState = state;
                        result.score = -250000.f + static_cast<float>(step) * 1000.f;
                        return result;
                    }
                    continue;
                }

                if (candidate.activateFirstOrb && object->isOrb() && !firstOrbConsumed && state.lastOrbUID != object->uniqueID) {
                    state.lastOrbUID = object->uniqueID;
                    state.activatedOrbUID = object->uniqueID;
                    firstOrbConsumed = true;
                    applyOrbEffect(state, *object);
                    currentBounds = CollisionProvider::playerBounds(state);
                }
            }

            if (wasOnGround && !state.onGround && result.supportLostTick < 0) {
                result.supportLostTick = step;
                result.fallStartedTick = step;
            }

            if (!wasOnGround && state.onGround && result.landedTick < 0) {
                result.landedTick = step;
                result.landingSupportUID = state.currentSupportUID;
                result.landedSafely = true;
            }

            if (wasOnGround && state.onGround && previousSupportUID != state.currentSupportUID && state.currentSupportUID >= 0) {
                result.landingSupportUID = state.currentSupportUID;
            }

            if (CollisionProvider::touchesHazard(state, objects, deathReason)) {
                CollisionProvider::updateMinHazardClearance(currentBounds, objects, result);
                result.dies = true;
                result.deathTick = step;
                result.deathReason = deathReason;
                result.finalState = state;
                result.activatedOrbUID = state.activatedOrbUID;
                result.score = -400000.f + static_cast<float>(step) * 1000.f;
                return result;
            }

            float groundY = CollisionProvider::inferGroundY(state, objects);
            float ceilingY = CollisionProvider::inferCeilingY(state, objects);
            if (state.y < groundY - 5.f || state.y > ceilingY + 5.f) {
                result.dies = true;
                result.deathTick = step;
                result.deathReason = "world-bounds";
                result.finalState = state;
                result.activatedOrbUID = state.activatedOrbUID;
                result.score = -350000.f + static_cast<float>(step) * 1000.f;
                return result;
            }

            CollisionProvider::updateMinHazardClearance(currentBounds, objects, result);
        }

        result.finalState = state;
        result.activatedOrbUID = state.activatedOrbUID;
        result.score = state.x;
        if (result.landedSafely) {
            result.score += 120.f;
        }
        result.score -= static_cast<float>(candidate.tapTicks.size()) * 6.f;
        if (candidate.activateFirstOrb) {
            result.score -= 2.f;
        }
        if (state.worstApproximation == CollisionApproximation::FallbackAABB && !config.useApproximateCollisionFallback) {
            result.dies = true;
            result.deathReason = "fallback-collision-disabled";
            result.score = -300000.f;
        }
        return result;
    }
};

} // namespace autobot

#endif
