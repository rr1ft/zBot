#ifndef _gui_hpp
#define _gui_hpp

#include <imgui-cocos.hpp>

// ============================================================================
// GUI — ImGui-based overlay for zBot controls
// ============================================================================

class GUI {
private:
    ImFont* s = nullptr;    ///< Small font (18pt)
    ImFont* l = nullptr;    ///< Large font (28pt)
    ImFont* vl = nullptr;   ///< Very large font (100pt)

    char location[30] = {};
    char tempReplayName[30] = {};

public:
    static auto* get() {
        static GUI* instance = new GUI();
        return instance;
    }

    bool showCBFMessage = false;
    bool shownCBFMessage = false;

    bool visible = false;
    bool callbackInit = false;

    void renderReplayInfo();
    void renderStateSwitcher();
    void renderMainPanel();
    void renderAutoBotPanel();   ///< Autonomous bot toggle + status

    void renderer();
    void setup();
};

#endif
