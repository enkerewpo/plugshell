// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_core/juce_core.h>

namespace plugshell
{

/**
    Turns the vendor fragment of a bundle identifier into the name the vendor
    actually uses, and reads a plugin's category without running it.

    Identifiers are written for uniqueness, not for reading: the same company
    appears as `native-instruments`, `Arturia` and `WavesAudio` depending on
    who typed it, and one vendor ships plugins whose identifier says only
    `vst3`. Shown verbatim, the list reads like a directory dump.

    Vendor logos are deliberately absent. A company logo is both a trademark
    and a copyrighted work, so shipping a collection of them inside this
    repository is the same kind of problem as redistributing factory content,
    only clearer cut. Reading them from installed plugins is not an
    alternative: of 89 plugins on the development machine only 12 carry an
    icon and none of the major instruments do.
*/
class VendorNames
{
public:
    static juce::String official(const juce::String& fragment, const juce::String& pluginName)
    {
        static const std::pair<const char*, const char*> table[] = {
            {"fabfilter", "FabFilter"},
            {"native-instruments", "Native Instruments"},
            {"arturia", "Arturia"},
            {"izotope", "iZotope"},
            {"spitfireaudio", "Spitfire Audio"},
            {"unfilteredaudio", "Unfiltered Audio"},
            {"plugin-alliance", "Plugin Alliance"},
            {"xfer", "Xfer Records"},
            {"splice", "Splice"},
            {"dreamtonics", "Dreamtonics"},
            {"oeksound", "oeksound"},
            {"xlnaudio", "XLN Audio"},
            {"beyerdynamic", "beyerdynamic"},
            {"lindellplugins", "Lindell Audio"},
            {"output", "Output"},
            {"voxengo", "Voxengo"},
            {"u-he", "u-he"},
            {"uvisoundsource", "UVI"},
            {"wavesaudio", "Waves"},
            {"youlean", "Youlean"},
            {"josephlyncheski", "MiniMeters"},
            {"zeek", "ZEEK"},
        };

        const auto key = fragment.toLowerCase();

        for (const auto& [id, name] : table)
            if (key == id)
                return name;

        if (key.isEmpty() || key == "vst3" || key == "com")
            return guessFromName(pluginName);

        return fragment;
    }

private:
    static juce::String guessFromName(const juce::String& pluginName)
    {
        if (pluginName.startsWithIgnoreCase("FM8") || pluginName.startsWithIgnoreCase("Massive") ||
            pluginName.startsWithIgnoreCase("Reaktor") || pluginName.startsWithIgnoreCase("Guitar Rig"))
            return "Native Instruments";

        return {};
    }
};

} // namespace plugshell
