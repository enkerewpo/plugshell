// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

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

    /** Makes `child`'s window a child of `parent`'s.

        A panel that has to appear over an embedded plugin editor cannot be
        drawn by the framework, because the editor is a native view that
        composites above anything drawn in the same window. It therefore has
        to be a window of its own -- and a window of its own, marked always on
        top, floats above every other application on the machine, not just
        above this one. A child window stays above its parent and travels with
        it while the pair behaves normally against everything else. */
    static void attachAsChildWindow(juce::Component& child, juce::Component& parent);

    /** The window server's number for the window `c` is in, or 0.

        This is the one identifier for a window that means the same thing in
        another process, which is what makes it the only way to say "put your
        window directly above that one" across a process boundary. */
    static std::uint32_t windowNumberOf(juce::Component& c);

    /** Whether Screen Recording has already been granted. Does not prompt. */
    static bool hasScreenRecordingPermission();

    /** Raises the system permission prompt if it has not been answered before.
        Only the first call shows anything; after that macOS remembers, and a
        change of mind has to be made in System Settings. */
    static bool requestScreenRecordingPermission();

    /** Whether synthetic input will be delivered.

        macOS treats generating input as separate from observing the screen and
        gates it behind Accessibility. Without it, event injection fails the
        way it always does here: silently, with the call reporting success and
        nothing on screen moving. */
    static bool hasAccessibilityPermission();

    /** Prompts for Accessibility, once. */
    static bool requestAccessibilityPermission();

    enum class MouseAction
    {
        move,
        down,
        drag,
        up,
        scroll
    };

    /** Whether to inject events at the system level rather than into this
        application's own event queue.

        Off by default. A system event moves the user's real cursor and is
        delivered to whichever window is in front, so with the host in the
        background it lands in somebody else's application -- which is not a
        thing an agent-driven host may do while a person is using the machine.
        The application queue has neither problem and reaches most editors;
        see the note in the implementation for the ones it does not. */
    static bool useSystemEvents;

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

    /** Runs the application's event queue until it is empty or @p timeoutMs
        has passed, and returns having done so.

        Synthetic input is posted, not delivered. That distinction costs
        nothing while a session is being recorded -- the message loop is
        running anyway, and the click arrives a few milliseconds later -- and
        it invalidates everything about replay, where the operations run one
        after another inside a single call and every click is still sitting in
        the queue when the next one is posted and when the result is checked.
        Replaying a patch without this compared the editor against itself
        before anything had happened to it, and reported that the patch had
        reproduced.

        This is a bounded pump rather than a modal loop: it delivers what is
        waiting and returns, so nothing re-enters the caller except the events
        the caller just created. */
    static void settle(int timeoutMs = 120);

    /** A press, a straight-line move, and a release, with enough intermediate
        points that a control tracking the drag sees a movement rather than a
        jump. Knobs commonly accumulate deltas, so a single large step lands
        somewhere different from the same distance travelled gradually. */
    static void drag(juce::Component& component, juce::Point<float> from, juce::Point<float> to,
                     int steps = 24, juce::ModifierKeys mods = {});
};

} // namespace plugshell
