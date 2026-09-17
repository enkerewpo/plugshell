// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 plugshell contributors
//
// Phase-1 feasibility spike.
//
// This does not build a product. It answers one question: can a host capture
// a plugin's editor and deliver synthetic input to it? Both are per-plugin
// behaviours, so the only useful answer comes from running against real
// plugins that render in different ways. See docs/SPIKE.md.

#include <juce_core/juce_core.h>

#include <iostream>

#include "core/PluginHost.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "plugshell spike " << argc << " arg(s)\n";
    (void) argv;

    plugshell::PluginHost host;
    const auto found = host.scan();

    std::cout << "discovered " << found.size() << " plugin(s)\n";
    for (const auto& d : found)
        std::cout << "  [" << d.format << "] " << d.manufacturer << " / " << d.name
                  << (d.isInstrument ? "  (instrument)" : "") << "\n";

    // TODO(phase-1): load each spike target, open its editor, attempt every
    // capture and input strategy in turn, and write the outcome matrix to
    // docs/SPIKE.md. Nothing below this line is implemented yet.
    return 0;
}
