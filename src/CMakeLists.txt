# SPDX-License-Identifier: GPL-3.0-or-later

add_library(vstshell_core STATIC
    core/PluginHost.cpp
    core/PluginHost.h
    core/EditorSurface.h)

if(APPLE)
    target_sources(vstshell_core PRIVATE
        platform/macos/EditorSurfaceMac.mm)
endif()

target_include_directories(vstshell_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(vstshell_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(vstshell_core
    PUBLIC
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
