// ============================================================================
// clickbetweenframescompat.cpp — Disable CBF's soft-toggle when zBot is active
//
// Compatible with Geode v4.x / GD 2.206+
// ============================================================================

#include <Geode/modify/PlayLayer.hpp>
#include "zBot.hpp"
#include "gui.hpp"

using namespace geode::prelude;

class $modify(CBFPlayLayer, PlayLayer) {
    void resetLevel() {
        static Mod* cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames");

        if (cbf) {
            cbf->setSettingValue<bool>("soft-toggle", zBot::get()->state != NONE);
            GUI::get()->showCBFMessage = true;
        }

        PlayLayer::resetLevel();
    }
};
