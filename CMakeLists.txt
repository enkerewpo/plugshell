# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 plugshell contributors

cmake_minimum_required(VERSION 3.22)

project(plugshell
    VERSION 0.0.1
    DESCRIPTION "An agent-operable host for audio plugins"
    HOMEPAGE_URL "https://github.com/enkerewpo/plugshell"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(PLUGSHELL_BUILD_SPIKE "Build the phase-1 feasibility spike" ON)

# JUCE is vendored as a submodule so the build is reproducible without a
# system-wide install. Run `make deps` if this directory is empty.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "external/JUCE is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(external/JUCE)

add_subdirectory(src)

if(PLUGSHELL_BUILD_SPIKE)
    add_subdirectory(spike)
endif()
