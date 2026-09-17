# SPDX-License-Identifier: GPL-3.0-or-later

juce_add_console_app(vstshell_spike PRODUCT_NAME "vstshell-spike")

target_sources(vstshell_spike PRIVATE main.cpp)

target_compile_definitions(vstshell_spike PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

target_link_libraries(vstshell_spike PRIVATE
    vstshell_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
