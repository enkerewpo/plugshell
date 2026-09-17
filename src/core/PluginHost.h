// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vstshell contributors

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vstshell
{

/** One discovered plugin on disk, before it is instantiated. */
struct PluginDescription
{
    std::string identifier; ///< format-specific unique id
    std::string name;
    std::string format; ///< "VST3" or "AudioUnit"
    std::string manufacturer;
    bool isInstrument = false;
};

/** A parameter as the plugin publishes it. */
struct ParameterInfo
{
    int index = 0;
    std::string name;
    float value = 0.0f; ///< normalised, 0..1
    std::string text;   ///< plugin's own rendering of the current value
    bool isAutomatable = true;
};

/**
    Loads plugins and exposes what they publish.

    This class deliberately covers only the mechanisms that every host has:
    discovery, instantiation, parameters, presets and state. Everything to do
    with the editor lives behind EditorSurface, because that is the part whose
    feasibility is still unproven (see docs/SPIKE.md).
*/
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    /** Scans the standard macOS plugin directories. */
    std::vector<PluginDescription> scan();

    /** Instantiates a plugin. Returns nullopt with `error` set on failure. */
    std::optional<int> load(const std::string& identifier, std::string& error);

    void unload(int pluginId);

    std::vector<ParameterInfo> parameters(int pluginId) const;
    bool setParameter(int pluginId, int index, float normalised);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

} // namespace vstshell
