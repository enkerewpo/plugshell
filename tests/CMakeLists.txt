# SPDX-License-Identifier: AGPL-3.0-or-later

# No JUCE. Everything in src/shared has to compile on both sides of the
# process boundary and be testable without an audio device, and a test that
# needed a device would not be run.
add_executable(plugshell_link_test AudioLinkTest.cpp)

target_compile_features(plugshell_link_test PRIVATE cxx_std_20)

add_test(NAME audio-link COMMAND plugshell_link_test)

# The proxy, against the real helper and a real plugin. Skipped by itself when
# no plugin is installed, because a test that cannot run is not a failure.
juce_add_console_app(plugshell_remote_test PRODUCT_NAME "plugshell-remote-test")

target_sources(plugshell_remote_test PRIVATE RemotePluginTest.cpp)

target_compile_definitions(plugshell_remote_test PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    PLUGSHELL_HELPER_PATH="$<TARGET_FILE:plugshell_child>")

target_link_libraries(plugshell_remote_test PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

add_dependencies(plugshell_remote_test plugshell_child)

add_test(NAME remote-plugin COMMAND plugshell_remote_test)

# The patch format, without a plugin or an audio device: what is being tested
# is that a file written by one program can be read by another, which is the
# whole claim of a format that exists to be distributed.
juce_add_console_app(plugshell_patch_test PRODUCT_NAME "plugshell-patch-test")

target_sources(plugshell_patch_test PRIVATE PatchTest.cpp)

target_compile_definitions(plugshell_patch_test PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(plugshell_patch_test PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)

add_test(NAME patch-format COMMAND plugshell_patch_test)
