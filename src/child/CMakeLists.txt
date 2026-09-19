# SPDX-License-Identifier: AGPL-3.0-or-later

# A GUI application because it owns the plugin's editor, but never one the
# user sees in the Dock: LSUIElement keeps it out, since a plugin crashing
# should not leave an orphan icon bouncing there.
juce_add_gui_app(plugshell_child
    PRODUCT_NAME "plugshell-host"
    BUNDLE_ID "ai.plugshell.host"
    COMPANY_NAME "wheatfox"
    DOCUMENT_EXTENSIONS ""
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>LSUIElement</key><true/></dict></plist>")

target_sources(plugshell_child PRIVATE ChildMain.cpp)

if(APPLE)
    target_sources(plugshell_child PRIVATE AttachedPanelMac.mm)
    target_link_libraries(plugshell_child PRIVATE "-framework Cocoa")
endif()

target_compile_definitions(plugshell_child PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(plugshell_child PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
