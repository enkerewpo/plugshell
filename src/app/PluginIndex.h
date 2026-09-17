// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_core/juce_core.h>

#include "VendorNames.h"

namespace plugshell
{

/** One plugin as read from disk, without loading any of its code. */
struct IndexedPlugin
{
    juce::String name;
    juce::String vendor;
    juce::String version;
    juce::String category; ///< "Instrument", "Effect", or empty
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
        // Categories are learned when a plugin is first loaded and kept here,
        // keyed by path, size and timestamp so a reinstall invalidates them.
        auto cache = loadCache();
        bool cacheChanged = false;

        juce::Array<IndexedPlugin> out;

        for (const auto& dir : searchPaths())
        {
            if (!dir.isDirectory())
                continue;

            for (const auto& entry :
                 juce::RangedDirectoryIterator(dir, false, "*.vst3", juce::File::findDirectories))
                if (auto p = read(entry.getFile()))
                {
                    const auto key = cacheKey(entry.getFile());

                    if (const auto* cached = cache.getVarPointer(key))
                    {
                        p->category = cached->toString();
                    }
                    else
                    {
                        // Nothing on disk says whether a plugin is an
                        // instrument. Reading the binary's strings looked like
                        // it worked and did not: the VST3 SDK defines every
                        // category as a constant, so "Instrument|" and "Fx|"
                        // both appear in almost any plugin that links it —
                        // 73 bytes apart in one case. The only source of truth
                        // is the plugin itself, so this stays unknown until
                        // the user loads it and is remembered afterwards.
                    }

                    out.add(*p);
                }
        }

        if (cacheChanged)
            saveCache(cache);

        std::sort(out.begin(), out.end(), [](const IndexedPlugin& a, const IndexedPlugin& b)
                  { return a.name.compareIgnoreCase(b.name) < 0; });

        return out;
    }

    /** Records what a plugin turned out to be, once it has been loaded. */
    static void remember(const juce::File& bundle, const juce::String& category)
    {
        auto cache = loadCache();
        cache.set(cacheKey(bundle), category);
        saveCache(cache);
    }

private:
    static juce::File cacheFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("plugshell/index.json");
    }

    /** Path, size and timestamp together, so a reinstall invalidates the entry. */
    static juce::Identifier cacheKey(const juce::File& bundle)
    {
        const auto bin = bundle.getChildFile("Contents/MacOS");
        juce::Array<juce::File> files;
        bin.findChildFiles(files, juce::File::findFiles, false);

        const auto size = files.isEmpty() ? 0 : files.getFirst().getSize();
        const auto stamp = files.isEmpty() ? 0 : files.getFirst().getLastModificationTime().toMilliseconds();

        return juce::Identifier(bundle.getFullPathName() + "|" + juce::String(size) + "|" +
                                juce::String(stamp));
    }

    static juce::NamedValueSet loadCache()
    {
        juce::NamedValueSet set;
        const auto parsed = juce::JSON::parse(cacheFile());

        if (auto* obj = parsed.getDynamicObject())
            for (const auto& prop : obj->getProperties())
                set.set(prop.name, prop.value);

        return set;
    }

    static void saveCache(const juce::NamedValueSet& set)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        for (int i = 0; i < set.size(); ++i)
            obj->setProperty(set.getName(i), set.getValueAt(i));

        const auto file = cacheFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(juce::var(obj.release())));
    }

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
        if (p.name.isEmpty())
            p.name = bundle.getFileNameWithoutExtension();
        p.vendor =
            VendorNames::official(vendorFromBundleId(values.getValue("CFBundleIdentifier", {})), p.name);
        p.version = values.getValue("CFBundleShortVersionString", {});

        return p;
    }
};

} // namespace plugshell
