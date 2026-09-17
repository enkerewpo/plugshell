// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_core/juce_core.h>

namespace plugshell
{

/** One plugin as read from disk, without loading any of its code. */
struct IndexedPlugin
{
    juce::String name;
    juce::String vendor;
    juce::String version;
    juce::File bundle;
};

/**
    Lists installed VST3 plugins by reading each bundle's Info.plist.

    This is deliberately not a scan. A scan loads and instantiates every
    plugin it finds, which on a real machine starts vendor licensing
    services, raises dialogs, and can crash the host outright — all
    observed on the development machine, recorded as F1 in docs/SPIKE.md.
    Indexing only reads files, so it cannot do any of that. Anything the
    index cannot know, such as whether a plugin is an instrument or how
    many parameters it has, is filled in the first time the user loads it.
*/
class PluginIndex
{
public:
    static juce::Array<IndexedPlugin> scanDirectories()
    {
        juce::Array<IndexedPlugin> out;

        for (const auto& dir : searchPaths())
        {
            if (!dir.isDirectory())
                continue;

            for (const auto& entry :
                 juce::RangedDirectoryIterator(dir, false, "*.vst3", juce::File::findDirectories))
                if (auto p = read(entry.getFile()))
                    out.add(*p);
        }

        std::sort(out.begin(), out.end(), [](const IndexedPlugin& a, const IndexedPlugin& b)
                  { return a.name.compareIgnoreCase(b.name) < 0; });

        return out;
    }

private:
    static juce::Array<juce::File> searchPaths()
    {
        return {juce::File("/Library/Audio/Plug-Ins/VST3"),
                juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile("Library/Audio/Plug-Ins/VST3")};
    }

    static juce::String vendorFromBundleId(const juce::String& id)
    {
        // Bundle identifiers are conventionally reverse-DNS, so the second
        // component is the vendor: com.Arturia.Pigments.vst3 -> Arturia.
        const auto parts = juce::StringArray::fromTokens(id, ".", "");
        return parts.size() >= 2 ? parts[1] : juce::String();
    }

    static std::optional<IndexedPlugin> read(const juce::File& bundle)
    {
        const auto plist = bundle.getChildFile("Contents/Info.plist");
        if (!plist.existsAsFile())
            return std::nullopt;

        const auto xml = juce::XmlDocument::parse(plist);
        if (xml == nullptr)
            return std::nullopt;

        // A plist is <dict><key>K</key><string>V</string>...</dict>; walk the
        // pairs rather than depending on ordering assumptions.
        juce::StringPairArray values;
        if (auto* dict = xml->getChildByName("dict"))
        {
            juce::String pendingKey;
            for (auto* child : dict->getChildIterator())
            {
                if (child->hasTagName("key"))
                    pendingKey = child->getAllSubText().trim();
                else if (pendingKey.isNotEmpty())
                {
                    values.set(pendingKey, child->getAllSubText().trim());
                    pendingKey = {};
                }
            }
        }

        IndexedPlugin p;
        p.bundle = bundle;
        p.name = values.getValue("CFBundleName", bundle.getFileNameWithoutExtension());
        p.vendor = vendorFromBundleId(values.getValue("CFBundleIdentifier", {}));
        p.version = values.getValue("CFBundleShortVersionString", {});

        if (p.name.isEmpty())
            p.name = bundle.getFileNameWithoutExtension();

        return p;
    }
};

} // namespace plugshell
