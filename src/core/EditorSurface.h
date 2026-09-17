// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 plugshell contributors

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <vector>

namespace plugshell
{

/**
    Capture of, and synthetic input into, a plugin's editor.

    Both operations are per-plugin behaviours rather than per-platform ones: a
    plugin that renders through Metal may not yield its contents to a
    view-level snapshot, and one that runs its own event handling may ignore a
    synthesised event. Implementations therefore report which strategy
    succeeded so callers can record it.

    Nothing here is proven yet. See docs/SPIKE.md.
*/
class EditorSurface
{
public:
    enum class CaptureStrategy
    {
        none,
        juceSnapshot,    ///< juce::createSnapshotOfNativeWindow
        viewCache,       ///< NSView bitmapImageRepForCachingDisplayInRect:
        screenCaptureKit ///< on-screen capture, needs Screen Recording
    };

    enum class InputStrategy
    {
        none,
        syntheticNSEvent, ///< NSEvent posted to the editor's view
        sessionCGEvent    ///< CGEvent at session level
    };

    virtual ~EditorSurface() = default;

    /** Opens the editor without blocking the caller. */
    virtual bool open(juce::AudioPluginInstance& plugin) = 0;
    virtual void close() = 0;

    /** Renders the editor to PNG bytes. Empty on failure. */
    virtual std::vector<std::uint8_t> capture(CaptureStrategy& used) = 0;

    virtual bool click(juce::Point<int> editorLocal, InputStrategy& used) = 0;
    virtual bool drag(juce::Point<int> from, juce::Point<int> to, InputStrategy& used) = 0;
};

} // namespace plugshell
