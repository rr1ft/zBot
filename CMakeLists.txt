cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(zBot VERSION 3.1.0 LANGUAGES CXX)

# Some Geode / dependency packages probe Clang-specific warning flags such as
# -Wno-everything. With MSVC generators this can fail noisily during
# try_compile checks, so seed the cache to skip that probe on Windows.
if (MSVC)
    set(SUPPORTS_W_NO_EVERYTHING OFF CACHE INTERNAL "MSVC does not support -Wno-everything" FORCE)
endif()

file(GLOB SOURCES
    src/gui.cpp
    src/keybinds.cpp
    src/physicsbypass.cpp
    src/playbackmanager.cpp
    src/recordmanager.cpp
    src/speedhackaudio.cpp
    src/clickbetweenframescompat.cpp
    src/zBot.cpp
    src/autobot/AutoBotHooks.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/autobot
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} $ENV{GEODE_SDK}/build)

CPMAddPackage("gh:maxnut/GDReplayFormat#9c32b5529067901418ea6e4dfbd36437dc8712bc")

CPMAddPackage(
    NAME imgui-cocos
    GITHUB_REPOSITORY matcool/gd-imgui-cocos
    GIT_TAG 8973953519fa05d81c61634e585559ab87ca559d
    PATCH_COMMAND ${CMAKE_COMMAND} -DGD_IMGUI_COCOS_SOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_gd_imgui_cocos.cmake
)

target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")

if (WIN32)
    target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR)
    target_link_options(${PROJECT_NAME} PRIVATE "/MAP:${CMAKE_BINARY_DIR}/Release/${PROJECT_NAME}.map")
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR "-framework IOKit")
endif()

setup_geode_mod(${PROJECT_NAME})
