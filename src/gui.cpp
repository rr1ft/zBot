// ============================================================================
// gui.cpp — ImGui overlay: replay controls, hacks, and auto-bot toggle
//
// Compatible with Geode v4.x / GD 2.206+
// ============================================================================

#include "gui.hpp"
#include "autobot/AutoBotDebug.hpp"
#include "autobot/AutoBot.hpp"
#include "zBot.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cstring>

using namespace geode::prelude;

// ---- Replay info -----------------------------------------------------------

void GUI::renderReplayInfo() {
    zBot* mgr = zBot::get();
    if (!mgr->currentReplay) return;
    auto* play = PlayLayer::get();

    ImGui::Text("Current Replay Name: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%s", mgr->currentReplay->name.c_str());

    ImGui::Text("Replay TPS: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%.0f", mgr->currentReplay->framerate);

    ImGui::Text("Replay Samples: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%d", static_cast<int>(mgr->currentReplay->positionSamples.size()));

    if (mgr->state == PLAYBACK) {
        ImGui::Separator();
        ImGui::Text("Playback Seek Mode: %s", zBot::seekMethodName(mgr->playbackState.seekMethod));
        ImGui::Text("Live Frame: %u", play ? play->m_gameState.m_currentProgress : 0u);
        ImGui::Text("Playback Replay Frame: %d",
            play ? mgr->getPlaybackTimelineFrame(play->m_gameState.m_currentProgress) : -1);
        ImGui::Text("Start Replay Frame: %d", mgr->playbackState.playbackStartReplayFrame);
        ImGui::Text("Session Start Game Frame: %d", mgr->playbackState.playbackSessionStartGameFrame);
        ImGui::Text("Cursor Index: %d", static_cast<int>(mgr->playbackState.currentInputIndex));
        ImGui::Text("Clickbot Cursor: %d", static_cast<int>(mgr->playbackState.clickbotInputIndex));
        ImGui::Text("Seek Player X: %.2f", mgr->playbackState.playbackStartPlayer1X);
        ImGui::Text("Seek Percent: %.2f", mgr->playbackState.playbackStartPercent);
        ImGui::Text("Matched Sample Frame: %d", mgr->playbackState.matchedSampleFrame);
        ImGui::Text("Held Reconstructed: %s", mgr->playbackState.heldStateReconstructed ? "yes" : "no");
    }
}

// ---- State radio buttons ---------------------------------------------------

void GUI::renderStateSwitcher() {
    zBot* mgr = zBot::get();
    int originalState = mgr->state;

    if (ImGui::RadioButton("Disable", reinterpret_cast<int*>(&mgr->state), NONE)) {
        if (originalState == PLAYBACK && PlayLayer::get()) {
            mgr->clearPlaybackHeldInputs(PlayLayer::get());
        }

        mgr->resetPlaybackState();
        mgr->resetRecordingState();
    }
    ImGui::SameLine();

    if (ImGui::RadioButton("Record", reinterpret_cast<int*>(&mgr->state), RECORD)) {
        if (originalState == PLAYBACK && PlayLayer::get()) {
            mgr->clearPlaybackHeldInputs(PlayLayer::get());
        }

        mgr->resetPlaybackState();

        if (PlayLayer::get() && !mgr->currentReplay) {
            mgr->createNewReplay(PlayLayer::get()->m_level);
        }
        if (PlayLayer::get() && mgr->currentReplay) {
            mgr->prepareRecordingFromCurrentPlayerState(PlayLayer::get(), "gui-record-toggle", true);
        }
    }
    ImGui::SameLine();

    if (ImGui::RadioButton("Playback", reinterpret_cast<int*>(&mgr->state), PLAYBACK)) {
        mgr->resetRecordingState();

        if (!mgr->currentReplay) {
            mgr->state = NONE;
        }
        else {
            mgr->markPlaybackSeekDirty();
            if (PlayLayer::get()) {
                mgr->preparePlaybackFromCurrentPlayerState(PlayLayer::get(), "gui-playback-toggle");
            }
        }
    }
}

// ---- Utilities panel (TPS, speed, frame counter) ---------------------------

static void RenderInfoPanel() {
    zBot* mgr = zBot::get();

    ImGui::SetNextWindowSize(ImVec2(200, 320), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(385, 10), ImGuiCond_Once);
    ImGui::Begin("utilities", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("TPS: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%.0f", mgr->tps);

    ImGui::Text("Speed: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%.2f", mgr->speed);

    ImGui::Text("Frame: ");
    ImGui::SameLine();
    ImGui::TextColored({ 0, 255, 255, 255 }, "%i",
        PlayLayer::get() ? PlayLayer::get()->m_gameState.m_currentProgress : 0);

    static float tempTPS = static_cast<float>(mgr->tps);
    ImGui::Text("Set TPS: ");
    ImGui::InputFloat("##tps", &tempTPS);
    if (ImGui::Button("Apply TPS")) {
        if (mgr->state == NONE || !PlayLayer::get())
            mgr->tps = tempTPS;
    }

    ImGui::NewLine();

    static float tempSpeed = 1.f;
    ImGui::Text("Set Speed: ");
    ImGui::InputFloat("##speed", &tempSpeed);
    if (ImGui::Button("Apply Speedhack")) {
        mgr->speed = tempSpeed;
    }

    ImGui::End();
}

// ---- Hacks panel -----------------------------------------------------------

static void RenderHackPanel() {
    zBot* mgr = zBot::get();

    ImGui::SetNextWindowSize(ImVec2(200, 320), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(610, 10), ImGuiCond_Once);
    ImGui::Begin("hacks", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar);

    ImGui::Checkbox("Frame Advance", &mgr->frameAdvance);
    ImGui::Checkbox("Speedhack Audio", &mgr->speedHackAudio);
    ImGui::Checkbox("Clickbot", &mgr->clickbotEnabled);

    ImGui::NewLine();
    ImGui::End();
}

// ---- Autonomous bot panel --------------------------------------------------

void GUI::renderAutoBotPanel() {
    zBot* mgr = zBot::get();

    ImGui::SetNextWindowSize(ImVec2(290, 250), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(835, 10), ImGuiCond_Once);
    ImGui::Begin("autobot", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushFont(s);
    ImGui::TextColored(ImVec4(0.2f, 1.f, 0.4f, 1.f), "Auto-Bot");

    if (ImGui::Checkbox("Enable Auto-Play", &mgr->autoBotEnabled)) {
        autobot::AutoBot::get()->resetState();
        auto* play = PlayLayer::get();
        if (play) {
            static_cast<GJBaseGameLayer*>(play)->handleButton(false, static_cast<int>(PlayerButton::Jump), true);
        }
        if (mgr->autoBotFileLogging) {
            if (mgr->autoBotEnabled) {
                autobot::AutoBotDebug::get()->beginSession("gui-toggle-enable");
            }

            std::ostringstream debugLine;
            debugLine << "enabled=" << mgr->autoBotEnabled
                      << " state=" << static_cast<int>(mgr->state)
                      << " playLayerPresent=" << (PlayLayer::get() != nullptr);
            autobot::AutoBotDebug::get()->logEvent("toggle-gui", debugLine.str());

            if (!mgr->autoBotEnabled) {
                autobot::AutoBotDebug::get()->endSession("gui-toggle-disable");
            }
        }
        if (mgr->autoBotEnabled && play) {
            autobot::AutoBot::get()->warmupLevel(static_cast<GJBaseGameLayer*>(play), 1.f / 240.f);
        }
        log::info(
            "[AutoBot] Toggled via GUI | enabled={} state={} playLayerPresent={}",
            mgr->autoBotEnabled,
            static_cast<int>(mgr->state),
            PlayLayer::get() != nullptr
        );
    }

    if (ImGui::Checkbox("Use Planner", &mgr->autoBotUsePlanner)) {
        autobot::AutoBot::get()->resetState();

        if (mgr->autoBotFileLogging) {
            std::ostringstream debugLine;
            debugLine << "plannerEnabled=" << mgr->autoBotUsePlanner
                      << " autoBotEnabled=" << mgr->autoBotEnabled
                      << " playLayerPresent=" << (PlayLayer::get() != nullptr);
            autobot::AutoBotDebug::get()->logEvent("planner-toggle", debugLine.str());
        }

        if (mgr->autoBotEnabled) {
            if (auto* play = PlayLayer::get()) {
                autobot::AutoBot::get()->warmupLevel(static_cast<GJBaseGameLayer*>(play), 1.f / 240.f);
            }
        }
    }

    if (ImGui::SliderFloat("Planner Horizon (s)", &mgr->autoBotPlannerHorizonSeconds, 0.5f, 1.5f, "%.2f")) {
        autobot::AutoBot::get()->resetState();
    }

    if (ImGui::SliderInt("Cube Timing Safety", &mgr->autoBotCubeTimingSafetyTicks, 0, 30)) {
        autobot::AutoBot::get()->resetState();
    }

    if (ImGui::Checkbox("Experimental Multi-mode", &mgr->autoBotExperimentalMultiMode)) {
        autobot::AutoBot::get()->resetState();
    }

    if (ImGui::Checkbox("Log Unknown Objects", &mgr->autoBotLogUnknownObjects)) {
        autobot::AutoBot::get()->resetState();
    }

    if (ImGui::Checkbox("Approximate Collision Fallback", &mgr->autoBotApproximateCollisionFallback)) {
        autobot::AutoBot::get()->resetState();
    }

    ImGui::TextWrapped("Mode: %s", mgr->autoBotUsePlanner ? "rolling planner" : "short-horizon cube planner");
    ImGui::TextWrapped("Cache Objects: %zu", autobot::AutoBot::get()->getCachedObjectCount());
    ImGui::TextWrapped("Nearby Objects: %zu", autobot::AutoBot::get()->getNearbyObjectCount());
    ImGui::TextWrapped("Last Decision: %s", autobot::AutoBot::get()->getLastDecisionReason().c_str());

    if (ImGui::Checkbox("Write Debug Log", &mgr->autoBotFileLogging)) {
        if (mgr->autoBotFileLogging) {
            autobot::AutoBotDebug::get()->beginSession("debug-log-enabled");
            autobot::AutoBotDebug::get()->logEvent("debug-log", "enabled=true");
        } else {
            autobot::AutoBotDebug::get()->logEvent("debug-log", "enabled=false");
            autobot::AutoBotDebug::get()->endSession("debug-log-disabled");
        }
    }

    if (ImGui::Button("Open Debug Folder")) {
        utils::file::openFolder(autobot::AutoBotDebug::get()->getLogDirectory());
    }

    ImGui::TextWrapped("Latest log: %s", autobot::AutoBotDebug::get()->getLatestLogPath().string().c_str());

    if (mgr->autoBotEnabled) {
        ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "BOT ACTIVE");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "BOT INACTIVE");
    }

    ImGui::PopFont();
    ImGui::End();
}

// ---- Main panel ------------------------------------------------------------

void GUI::renderMainPanel() {
    ImGui::SetNextWindowSize(ImVec2(350, 525), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::Begin("info", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushFont(l);
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.17f, 1.f), "zBot");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.f), "v" MOD_VERSION);
    ImGui::PopFont();

    ImGui::PushFont(s);
    renderReplayInfo();
    renderStateSwitcher();

    ImGui::NewLine();

    zBot* mgr = zBot::get();

    ImGui::Text("Import Replay by name\n(must be in replays folder)");
    ImGui::InputText("##replaylocation", location, sizeof(location));

    if (ImGui::Button("Import")) {
        zReplay* rec = zReplay::fromFile(location);
        if (rec) {
            if (mgr->state == PLAYBACK && PlayLayer::get()) {
                mgr->clearPlaybackHeldInputs(PlayLayer::get());
            }
            mgr->currentReplay = rec;
            mgr->state = PLAYBACK;
            mgr->markPlaybackSeekDirty();
            if (PlayLayer::get()) {
                mgr->preparePlaybackFromCurrentPlayerState(PlayLayer::get(), "gui-import");
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Open Replays Folder")) {
        auto dir = Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(dir))
            std::filesystem::create_directories(dir);
        utils::file::openFolder(dir);
    }

    if (mgr->currentReplay) {
        ImGui::NewLine();
        ImGui::Text("Override Recording Name");
        ImGui::InputText("##replayname", tempReplayName, sizeof(tempReplayName));
        if (ImGui::Button("Apply")) {
            mgr->currentReplay->name = tempReplayName;
            memset(tempReplayName, 0, sizeof(tempReplayName));
        }
        ImGui::SameLine();
        if (ImGui::Button("Manually Save to File")) {
            mgr->currentReplay->save();
        }
    }

    ImGui::PopFont();
    ImGui::End();
}

// ---- Top-level renderer called every frame ---------------------------------

void GUI::renderer() {
    // ---- Auto-bot HUD indicator (always visible when active) ----
    if (zBot::get()->autoBotEnabled && PlayLayer::get()) {
        ImGui::PushFont(l);
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const char* label = "[AUTO-BOT ACTIVE]";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(displaySize.x - labelSize.x - 10, 10),
            ImColor(0.2f, 1.f, 0.4f, 0.85f), label);
        ImGui::PopFont();
    }

    if (!visible) return;

    PlatformToolbox::showCursor();

    renderMainPanel();
    RenderInfoPanel();
    RenderHackPanel();
    renderAutoBotPanel();

    if (showCBFMessage && !shownCBFMessage) {
        shownCBFMessage = true;
        ImGui::OpenPopup("CBF Detected!");
    }

    ImGui::SetNextWindowSize(ImVec2(500, 140), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x / 2 - 250,
               ImGui::GetIO().DisplaySize.y / 2 - 70),
        ImGuiCond_Always);

    if (ImGui::BeginPopupModal("CBF Detected!", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Click between frames has been detected!");
        ImGui::Text("Even when disabled in options, playback may be affected.");
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---- ImGui style + font setup ----------------------------------------------

void GUI::setup() {
    ImGuiStyle* style = &ImGui::GetStyle();

    style->WindowPadding    = ImVec2(15, 15);
    style->WindowRounding   = 5.0f;
    style->FramePadding     = ImVec2(5, 5);
    style->FrameRounding    = 4.0f;
    style->ItemSpacing      = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing    = 25.0f;
    style->ScrollbarSize    = 15.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize      = 5.0f;
    style->GrabRounding     = 3.0f;

    style->Colors[ImGuiCol_Text]                = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled]        = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg]            = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_PopupBg]             = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_Border]              = ImVec4(0, 0, 0, 0);
    style->Colors[ImGuiCol_BorderShadow]        = ImVec4(0, 0, 0, 0);
    style->Colors[ImGuiCol_FrameBg]             = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive]       = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_TitleBg]             = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed]    = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
    style->Colors[ImGuiCol_TitleBgActive]       = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_MenuBarBg]           = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_CheckMark]           = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrab]          = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_Button]              = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered]       = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive]        = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_Header]              = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered]       = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_HeaderActive]        = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_PlotLines]           = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotLinesHovered]    = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram]       = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotHistogramHovered]= ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg]      = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
    style->Colors[ImGuiCol_ModalWindowDimBg]    = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);

    ImGuiIO& io = ImGui::GetIO();
    auto path = (Mod::get()->getResourcesDir() / "micross.ttf").string();

    s  = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f);
    l  = io.Fonts->AddFontFromFileTTF(path.c_str(), 28.0f);
    vl = io.Fonts->AddFontFromFileTTF(path.c_str(), 100.0f);
    io.Fonts->Build();
}

// ---- Bootstrap ImGui on game load ------------------------------------------

class $modify(GUILoadingLayer, LoadingLayer) {
    bool init(bool fromReload) {
        ImGuiCocos::get().setup([] {
            GUI::get()->setup();
        }).draw([] {
            GUI::get()->renderer();
        });

        return LoadingLayer::init(fromReload);
    }
};
