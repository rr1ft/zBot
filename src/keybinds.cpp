// ============================================================================
// keybinds.cpp — native keyboard hooks for zBot hotkeys
//
// Replaces the old custom-keybinds integration, which is not compatible with
// the Geode 5.x SDK installed in this workspace. Hotkeys are handled directly
// through CCKeyboardDispatcher so the mod remains self-contained.
//
// Hotkeys:
//   B — Toggle GUI visibility
//   V — Toggle frame advance mode
//   C — Advance one frame while frame advance is enabled
//   N — Toggle autonomous bot mode
// ============================================================================

#include <imgui-cocos.hpp>

#include "autobot/AutoBotDebug.hpp"
#include "autobot/AutoBot.hpp"
#include "gui.hpp"
#include "zBot.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(zBotKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double timestamp) {
        if (isKeyDown && !isKeyRepeat && !ImGui::GetIO().WantCaptureKeyboard) {
            zBot* mgr = zBot::get();

            switch (key) {
                case KEY_B: {
                    GUI* gui = GUI::get();
                    gui->visible = !gui->visible;
                    log::info("[zBot] GUI toggled via keyboard | visible={}", gui->visible);

                    auto pl = PlayLayer::get();
                    if (!gui->visible && pl && !pl->m_isPaused) {
                        PlatformToolbox::hideCursor();
                    }
                    break;
                }

                case KEY_V:
                    mgr->frameAdvance = !mgr->frameAdvance;
                    log::info("[zBot] Frame advance toggled via keyboard | enabled={}", mgr->frameAdvance);
                    break;

                case KEY_C:
                    mgr->doAdvance = true;
                    log::info("[zBot] Frame advance step requested via keyboard");
                    break;

                case KEY_N: {
                    mgr->autoBotEnabled = !mgr->autoBotEnabled;
                    autobot::AutoBot::get()->resetState();
                    auto* play = PlayLayer::get();
                    if (play) {
                        static_cast<GJBaseGameLayer*>(play)->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
                    }
                    if (mgr->autoBotFileLogging) {
                        if (mgr->autoBotEnabled) {
                            autobot::AutoBotDebug::get()->beginSession("keyboard-toggle-enable");
                        }

                        std::ostringstream debugLine;
                        debugLine << "enabled=" << mgr->autoBotEnabled
                                  << " state=" << static_cast<int>(mgr->state)
                                  << " playLayerPresent=" << (PlayLayer::get() != nullptr);
                        autobot::AutoBotDebug::get()->logEvent("toggle-keyboard", debugLine.str());

                        if (!mgr->autoBotEnabled) {
                            autobot::AutoBotDebug::get()->endSession("keyboard-toggle-disable");
                        }
                    }
                    if (mgr->autoBotEnabled && play) {
                        autobot::AutoBot::get()->warmupLevel(static_cast<GJBaseGameLayer*>(play), 1.f / 240.f);
                    }
                    log::info(
                        "[AutoBot] Toggled via keyboard | enabled={} state={} playLayerPresent={}",
                        mgr->autoBotEnabled,
                        static_cast<int>(mgr->state),
                        PlayLayer::get() != nullptr
                    );
                    break;
                }

                default:
                    break;
            }
        }

        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, timestamp);
    }
};
