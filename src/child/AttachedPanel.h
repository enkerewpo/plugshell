// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace plugshell::child
{

/**
    The window the plugin's editor lives in, made to not look like one.

    The editor is in this process and the host's window is in another, and the
    one mechanism that would make them literally the same window -- publishing
    the editor's layer tree through CAContext and hosting it in the host's
    view hierarchy with CALayerHost -- does not work on this system. That was
    measured rather than assumed: a CALayerHost shows nothing here even when
    the context it is given was created in the same process, so it is not a
    matter of getting the cross-process half right. The experiment is kept in
    the scratchpad and the note in docs/OUT_OF_PROCESS.md says what would have
    to change for it to be worth another attempt.

    So: a second window, held exactly over the area the host leaves empty.
    Two properties make that hold up rather than merely look right.

    It is a non-activating panel, so clicking a knob does not make this
    process the active application. That is not cosmetic: the host watches the
    keyboard to play the plugin from the computer keys, and it can only see
    events sent to the active application. A plain window stole them, and the
    computer keyboard went dead the moment anyone touched the plugin.

    And it is ordered directly above the host's window rather than raised to a
    floating level. Ownership is what cannot cross processes; ordering can,
    because a window number is a name the window server gives out and both
    processes can say. Floating was the first attempt and it was wrong in the
    way that matters: floating is a property of the whole screen, so the panel
    had to be hidden whenever the host was not the front application, and the
    plugin blinked out of existence every time the user looked at another
    window.

    What is left over, and is not fixable this way, is written down in
    docs/OUT_OF_PROCESS.md: this does not minimise with the host and does not
    appear in a screenshot of the host's window, because it is a window.
*/
class AttachedPanel
{
public:
    AttachedPanel();
    ~AttachedPanel();

    AttachedPanel(const AttachedPanel&) = delete;
    AttachedPanel& operator=(const AttachedPanel&) = delete;

    /** The NSView the editor is attached to, as JUCE's addToDesktop wants it. */
    void* contentView() const noexcept;

    /** Where to be, in the screen coordinates JUCE uses: origin at the top
        left of the main display, y downwards. */
    void setBounds(juce::Rectangle<int> screenBounds);

    /** Shows it directly above the host's window. @p hostWindowNumber is the
        window server's number for that window, which is the one name for a
        window that means the same thing in both processes. */
    void showAbove(std::uint32_t hostWindowNumber);

    void hide();

private:
    void* panel = nullptr; // NSPanel*
};

} // namespace plugshell::child
