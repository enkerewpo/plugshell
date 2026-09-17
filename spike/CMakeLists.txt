# SPDX-License-Identifier: AGPL-3.0-or-later

juce_add_console_app(plugshell_spike PRODUCT_NAME "plugshell-spike")

target_sources(plugshell_spike PRIVATE main.cpp)

target_compile_definitions(plugshell_spike PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

target_link_libraries(plugshell_spike PRIVATE
    plugshell_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
