// ============================================================================
// speedhackaudio.cpp — Pitch-shift FMOD audio to match speedhack multiplier
//
// Compatible with Geode v4.x / GD 2.206+
// ============================================================================

#include "zBot.hpp"
#include <Geode/modify/FMODAudioEngine.hpp>
using namespace geode::prelude;

class $modify(SHAFMODAudioEngine, FMODAudioEngine) {
    struct Fields {
        float pitch = 1.f;
    };

    void update(float delta) {
        FMODAudioEngine::update(delta);

        zBot* mgr = zBot::get();
        float pitch = mgr->speedHackAudio
            ? static_cast<float>(mgr->speed)
            : 1.f;

        if (pitch == m_fields->pitch) return;
        m_fields->pitch = pitch;

        FMOD::ChannelGroup* group = nullptr;
        if (m_system->getMasterChannelGroup(&group) == FMOD_OK) {
            group->setPitch(pitch);
        }
    }
};
