# SPDX-License-Identifier: AGPL-3.0-or-later

juce_add_gui_app(plugshell_app
    PRODUCT_NAME "plugshell"
    BUNDLE_ID "ai.plugshell.app"
    COMPANY_NAME "wheatfox"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon-1024.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icon-128.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "plugshell uses audio input so effect plugins can process a live signal."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "plugshell uses Bluetooth to find and pair Bluetooth MIDI controllers.")

target_sources(plugshell_app PRIVATE Main.cpp)

if(APPLE)
    target_sources(plugshell_app PRIVATE KeyMonitorMac.mm EditorProbeMac.mm SystemTheme.mm)
    target_link_libraries(plugshell_app PRIVATE "-framework Cocoa" "-framework CoreGraphics" "-framework ScreenCaptureKit")
endif()

# The out-of-process helper travels inside the application bundle, in the
# place macOS expects a helper with a user interface to be. It has to be there
# before the bundle is signed, and it has to be signed itself first: a nested
# bundle signed after its container invalidates the container's signature, and
# an invalid signature is a permission grant lost.
add_dependencies(plugshell_app plugshell_child)

# The out-of-process helper travels inside the application bundle, in the
# place macOS expects a helper with a user interface to be. It has to be there
# before the bundle is signed, and it has to be signed itself first: a nested
# bundle signed after its container invalidates the container's signature, and
# an invalid signature is a permission grant lost.
#
# A target of its own rather than a POST_BUILD on the application, because a
# POST_BUILD only runs when the application itself is relinked. A build that
# changed the helper and nothing else left the application's inputs untouched,
# so the copy never ran and the bundle went on carrying the previous helper --
# which is the worst shape a build bug can take: everything succeeds and the
# thing you just changed is not the thing that runs.
add_custom_target(plugshell_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:plugshell_app>/Contents/Helpers"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_BUNDLE_DIR:plugshell_app>/Contents/Helpers/plugshell-host.app"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:plugshell_child>"
            "$<TARGET_BUNDLE_DIR:plugshell_app>/Contents/Helpers/plugshell-host.app"
    COMMENT "Placing the out-of-process helper inside the bundle"
    VERBATIM)

add_dependencies(plugshell_package plugshell_app plugshell_child)

target_compile_definitions(plugshell_app PRIVATE
    PLUGSHELL_VERSION="${PROJECT_VERSION}"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(plugshell_app PRIVATE
    juce::juce_dsp
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)

# macOS ties Screen Recording and Accessibility to an application's code
# signature. An ad-hoc signature -- the default, and what JUCE applies -- has
# no stable identity, so the system falls back to identifying the app by its
# path and contents, and every rebuild produces a different app as far as the
# permission database is concerned. The permissions then have to be granted
# again after every single build, which makes them useless during development.
#
# Signing with any stable identity fixes it, including a self-signed one. The
# first codesigning identity in the keychain is used unless one is named, and
# "-" forces ad-hoc back.
if(APPLE)
    if(NOT DEFINED PLUGSHELL_CODESIGN_IDENTITY)
        execute_process(
            COMMAND security find-identity -v -p codesigning
            OUTPUT_VARIABLE _identities OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

        string(REGEX MATCH "\"([^\"]+)\"" _first "${_identities}")

        if(CMAKE_MATCH_1)
            set(PLUGSHELL_CODESIGN_IDENTITY "${CMAKE_MATCH_1}")
            message(STATUS "plugshell: signing with '${CMAKE_MATCH_1}' so macOS permissions survive rebuilds")
        else()
            set(PLUGSHELL_CODESIGN_IDENTITY "-")
            message(STATUS "plugshell: no codesigning identity found; permissions will need re-granting "
                           "after each build (see docs/BUILD.md)")
        endif()
    endif()

    if(NOT PLUGSHELL_CODESIGN_IDENTITY STREQUAL "-")
        add_custom_command(
            TARGET plugshell_package POST_BUILD
            COMMAND codesign --force --sign "${PLUGSHELL_CODESIGN_IDENTITY}"
                    --identifier "ai.plugshell.host"
                    "$<TARGET_BUNDLE_DIR:plugshell_app>/Contents/Helpers/plugshell-host.app"
            COMMAND codesign --force --sign "${PLUGSHELL_CODESIGN_IDENTITY}"
                    --identifier "ai.plugshell.app"
                    "$<TARGET_BUNDLE_DIR:plugshell_app>"
            COMMENT "Signing plugshell.app with a stable identity"
            VERBATIM)
    endif()
endif()
