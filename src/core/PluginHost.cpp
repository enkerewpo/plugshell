// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 vstshell contributors

#include "core/PluginHost.h"

#include <map>

namespace vstshell
{

namespace
{
constexpr double kScanSampleRate = 48000.0;
constexpr int kScanBlockSize = 512;
} // namespace

struct PluginHost::Impl
{
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::map<int, std::unique_ptr<juce::AudioPluginInstance>> loaded;
    int nextId = 1;

    Impl() { juce::addDefaultFormatsToManager(formats); }
};

PluginHost::PluginHost() : impl(std::make_unique<Impl>()) {}
PluginHost::~PluginHost() = default;

std::vector<PluginDescription> PluginHost::scan()
{
    std::vector<PluginDescription> out;

    for (auto* format : impl->formats.getFormats())
    {
        // TODO(phase-2): scanning is synchronous here, which is fine for a
        // CLI but must move to PluginDirectoryScanner once this is driven
        // over RPC, so a slow plugin cannot stall the caller.
        const auto paths = format->searchPathsForPlugins(format->getDefaultLocationsToSearch(),
                                                         /*recursive=*/true,
                                                         /*allowAsync=*/false);

        for (const auto& path : paths)
        {
            juce::OwnedArray<juce::PluginDescription> found;
            impl->known.scanAndAddFile(path, /*dontRescanIfAlreadyInList=*/true, found, *format);

            for (const auto* d : found)
                out.push_back({d->createIdentifierString().toStdString(), d->name.toStdString(),
                               d->pluginFormatName.toStdString(), d->manufacturerName.toStdString(),
                               d->isInstrument});
        }
    }

    return out;
}

std::optional<int> PluginHost::load(const std::string& identifier, std::string& error)
{
    const auto description = impl->known.getTypeForIdentifierString(juce::String(identifier));

    if (description == nullptr)
    {
        error = "no plugin matches identifier: " + identifier;
        return std::nullopt;
    }

    juce::String juceError;
    auto instance =
        impl->formats.createPluginInstance(*description, kScanSampleRate, kScanBlockSize, juceError);

    if (instance == nullptr)
    {
        error = juceError.toStdString();
        return std::nullopt;
    }

    const int id = impl->nextId++;
    impl->loaded.emplace(id, std::move(instance));
    return id;
}

void PluginHost::unload(int pluginId)
{
    impl->loaded.erase(pluginId);
}

std::vector<ParameterInfo> PluginHost::parameters(int pluginId) const
{
    std::vector<ParameterInfo> out;

    const auto it = impl->loaded.find(pluginId);
    if (it == impl->loaded.end())
        return out;

    int index = 0;
    for (auto* p : it->second->getParameters())
    {
        out.push_back({index++, p->getName(256).toStdString(), p->getValue(),
                       p->getCurrentValueAsText().toStdString(), p->isAutomatable()});
    }

    return out;
}

bool PluginHost::setParameter(int pluginId, int index, float normalised)
{
    const auto it = impl->loaded.find(pluginId);
    if (it == impl->loaded.end())
        return false;

    auto& params = it->second->getParameters();
    if (index < 0 || index >= params.size())
        return false;

    params[index]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
    return true;
}

} // namespace vstshell
