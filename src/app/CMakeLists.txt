# SPDX-License-Identifier: AGPL-3.0-or-later

juce_add_gui_app(plugshell_app
    PRODUCT_NAME "plugshell"
    BUNDLE_ID "ai.plugshell.app"
    COMPANY_NAME "wheatfox"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon.icns")

target_sources(plugshell_app PRIVATE Main.cpp)

target_compile_definitions(plugshell_app PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(plugshell_app PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
