// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    Reads a plugin's editor as an image, and puts synthetic input into it.

    This is the half of the problem that parameters do not cover. A plugin
    publishes the controls it chose to publish; everything else -- which
    wavetable is selected, what the modulation matrix routes where, which page
    of the editor is showing -- exists only as pixels and only responds to a
    mouse. A program that cannot see those pixels or move that mouse is
    working with a fraction of the plugin.

    Both operations have to go around JUCE rather than through it, because a
    plugin's editor is a native view that JUCE hosts but does not draw.
*/
class EditorProbe
{
public:
    enum class CaptureMethod
    {
        /** Ask the view to draw itself into a bitmap. Needs no permission and
            no compositing, but a view whose content is drawn by the GPU --
            OpenGL or Metal, which is most modern synth editors' spectrum
            displays and wavetable views -- has nothing for it to return, and
            comes back blank or missing those regions. */
        viewCache,

        /** Read back what the window server actually composited. Correct for
            every kind of view, including GPU-drawn ones, at the cost of
            needing Screen Recording permission, and of capturing whatever is
            on top if something is. */
        windowServer,

        /** viewCache, then windowServer if what came back was blank. */
        automatic
    };

    struct CaptureResult
    {
        bool ok = false;
        juce::String error;
        CaptureMethod used = CaptureMethod::viewCache;
        int width = 0, height = 0;
        double scale = 1.0; // pixels per point
    };

    /** Writes a PNG of `component` exactly as it appears on screen. */
    static CaptureResult capture(juce::Component& component, const juce::File& destination,
                                 CaptureMethod method = CaptureMethod::automatic);

    /** Whether the window server will hand over pixels, which on current macOS
        means whether the user has granted Screen Recording. Asking does not
        raise the permission prompt; capturing does. */
    static bool canUseWindowServer();

    enum class MouseAction
    {
        move,
        down,
        drag,
        up,
        scroll
    };

    /** Posts a synthetic mouse event at a point given in `component`'s own
        coordinates.

        Posted into the application's event queue rather than dispatched
        straight to the view. Sending `mouseDown:` directly is more certain to
        arrive, but a plugin that tracks a drag by running its own event loop
        inside `mouseDown:` -- which is ordinary AppKit practice, and what
        several plugins do for knobs -- would then block the message thread
        forever waiting for a `mouseUp:` that the same thread is supposed to
        deliver. Going through the queue lets such a loop find its events. */
    static void mouse(juce::Component& component, MouseAction action, juce::Point<float> pointInComponent,
                      float scrollDelta = 0.0f, juce::ModifierKeys mods = {});

    /** A press, a straight-line move, and a release, with enough intermediate
        points that a control tracking the drag sees a movement rather than a
        jump. Knobs commonly accumulate deltas, so a single large step lands
        somewhere different from the same distance travelled gradually. */
    static void drag(juce::Component& component, juce::Point<float> from, juce::Point<float> to,
                     int steps = 24, juce::ModifierKeys mods = {});
};

} // namespace plugshell
