if(NOT DEFINED GD_IMGUI_COCOS_SOURCE_DIR)
    message(FATAL_ERROR "GD_IMGUI_COCOS_SOURCE_DIR is not defined")
endif()

set(HOOKS_FILE "${GD_IMGUI_COCOS_SOURCE_DIR}/src/hooks.cpp")

if(NOT EXISTS "${HOOKS_FILE}")
    message(FATAL_ERROR "Could not find gd-imgui-cocos hooks.cpp at ${HOOKS_FILE}")
endif()

file(READ "${HOOKS_FILE}" HOOKS_CONTENT)

string(REPLACE
    "bool dispatchKeyboardMSG(enumKeyCodes key, bool down IF_2_2(, bool repeat)) {"
    "bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {"
    HOOKS_CONTENT
    "${HOOKS_CONTENT}"
)

string(REPLACE
    "return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down IF_2_2(, repeat));"
    "return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);"
    HOOKS_CONTENT
    "${HOOKS_CONTENT}"
)

file(WRITE "${HOOKS_FILE}" "${HOOKS_CONTENT}")
