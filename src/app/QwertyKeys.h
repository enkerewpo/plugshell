// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    Maps the computer keyboard to MIDI notes.

    This exists because testing an instrument needs some way to play it and a
    hardware controller is not always to hand. It is off by default and must be
    switched on: plugin editors want the keyboard too, for typing values and
    searching presets, and silently stealing keys from them would be worse than
    having no keyboard at all.

    The layout is two octaves stacked, as trackers and Cubase lay them out:

        upper octave   black  2 3   5 6 7   9 0
                       white  q w e r t y u i o p

        lower octave   black  s d   g h j   l ;
                       white  z x c v b n m , .

    Each pair of rows is a piano: the lower row is the white keys and the row
    above holds the black keys in the gaps where a piano has them, which is why
    there is no key above `c`-`v` or `m`-`,`.

    The upper octave stops at `p` rather than running on to `[`, `]` and `=`.
    Those three are not keys this can have: a Chinese or Japanese input method
    takes them for paging through its candidates, and it takes them before any
    application is offered them at all -- measurably, with a monitor that saw
    `q` arrive and never saw `[`. Reaching below the input method means an
    event tap sitting in the path of every keystroke on the machine, which was
    tried and made typing worse everywhere; see the note in KeyMonitor.h.

    So they are gone rather than dead. A key drawn on the map that does
    nothing for half the people who try it is worse than a shorter keyboard,
    and `=` goes with them because it is the black key between the two white
    ones being removed -- a sharp with nothing either side of it.
*/
class QwertyKeys
{
public:
    /** Semitones above the base note, or -1 when the key is not mapped. */
    static int noteForKey(juce::juce_wchar c)
    {
        switch (juce::CharacterFunctions::toLowerCase(c))
        {
        // Lower octave, white keys.
        case 'z':
            return 0;
        case 'x':
            return 2;
        case 'c':
            return 4;
        case 'v':
            return 5;
        case 'b':
            return 7;
        case 'n':
            return 9;
        case 'm':
            return 11;
        case ',':
            return 12;
        case '.':
            return 14;

        // Lower octave, black keys.
        case 's':
            return 1;
        case 'd':
            return 3;
        case 'g':
            return 6;
        case 'h':
            return 8;
        case 'j':
            return 10;
        case 'l':
            return 13;
        case ';':
            return 15;

        // Upper octave, white keys.
        case 'q':
            return 12;
        case 'w':
            return 14;
        case 'e':
            return 16;
        case 'r':
            return 17;
        case 't':
            return 19;
        case 'y':
            return 21;
        case 'u':
            return 23;
        case 'i':
            return 24;
        case 'o':
            return 26;
        case 'p':
            return 28;

        // Upper octave, black keys.
        case '2':
            return 13;
        case '3':
            return 15;
        case '5':
            return 18;
        case '6':
            return 20;
        case '7':
            return 22;
        case '9':
            return 25;
        case '0':
            return 27;

        default:
            return -1;
        }
    }

    /** Octave shift, on the two keys immediately left of the note rows so the
        hand does not have to leave the playing position: `\\` sits under the
        upper octave's left edge and `/` under the lower octave's right. */
    static int octaveShiftForKey(int keyCode)
    {
        if (keyCode == '\\')
            return -1;
        if (keyCode == '/')
            return 1;
        return 0;
    }

    static bool isOctaveKey(int keyCode) { return octaveShiftForKey(keyCode) != 0; }
};

} // namespace plugshell
