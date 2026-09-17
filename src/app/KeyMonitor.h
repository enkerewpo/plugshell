// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <functional>

namespace plugshell
{

/**
    Sees key events before they reach whatever currently has focus.

    A plugin's editor is a native view, so once the user clicks inside it — to
    change a preset, say — keyboard focus leaves the host's component tree and
    ordinary key listeners stop receiving anything. That is why switching a
    preset appeared to break the computer keyboard: nothing was broken, the
    events were simply going somewhere else.

    Watching at the platform's event layer removes focus from the question.
    Events are still passed through untouched unless the caller says it wants
    them, so the plugin keeps every key while the mode is off.
*/
class KeyMonitor
{
public:
    /** Returns true to consume the event, false to let it through.
        @param character  the character ignoring modifiers, lowercased
        @param keyCode    the hardware key, for asking later whether it is
                          still held; the character is not enough, because the
                          layout can change between a press and its release
        @param isDown     true for key down, false for key up
        @param isRepeat   true when the system is auto-repeating
        @param commandDown  whether the command modifier is held */
    using Handler =
        std::function<bool(int character, int keyCode, bool isDown, bool isRepeat, bool commandDown)>;

    KeyMonitor();
    ~KeyMonitor();

    void start(Handler h);
    void stop();

    /** Whether the hardware key is held right now, asked of the window server
        rather than inferred from the events seen so far.

        A key-up can go missing -- an input method swallows it, focus moves
        between the press and the release, the system drops it under several
        keys at once -- and a note whose release never arrives sounds forever.
        Rather than trying to enumerate the ways an event can be lost, the
        keyboard is polled: anything no longer physically down is released. */
    static bool isHeld(int keyCode);

private:
    void* monitor = nullptr; // NSEvent monitor token
    Handler handler;
};

} // namespace plugshell
