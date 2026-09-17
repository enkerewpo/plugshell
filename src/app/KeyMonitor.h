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
        @param isDown     true for key down, false for key up
        @param isRepeat   true when the system is auto-repeating
        @param commandDown  whether the command modifier is held */
    using Handler = std::function<bool(int character, bool isDown, bool isRepeat, bool commandDown)>;

    KeyMonitor();
    ~KeyMonitor();

    void start(Handler h);
    void stop();

private:
    void* monitor = nullptr; // NSEvent monitor token
    Handler handler;
};

} // namespace plugshell
