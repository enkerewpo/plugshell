// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "QwertyKeys.h"

namespace plugshell
{

/** Draws a key of the computer keyboard, and the marks that go on one.

    Shortcuts written as text -- "Cmd+K", or the ⌘ character set in whatever
    the system font happens to have -- read as prose about a key rather than as
    the key. A drawn cap reads as the thing you are being asked to press.
*/
struct KeyCap
{
    /** The looped square, built rather than typed: four rings at the corners
        of a square, joined by its edges. Every font draws this differently and
        several draw it badly at small sizes. */
    static void drawCommand(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour,
                            float thickness = 1.3f)
    {
        const float side = juce::jmin(box.getWidth(), box.getHeight());
        const auto centre = box.getCentre();
        const float ring = side * 0.22f;
        const float half = side * 0.5f - ring * 0.5f;

        juce::Path path;

        // The square joining the four rings.
        path.startNewSubPath(centre.x - half, centre.y - half);
        path.lineTo(centre.x + half, centre.y - half);
        path.lineTo(centre.x + half, centre.y + half);
        path.lineTo(centre.x - half, centre.y + half);
        path.closeSubPath();

        for (const auto corner :
             {juce::Point<float>{-half, -half}, {half, -half}, {half, half}, {-half, half}})
        {
            const auto at = centre + corner;
            path.addCentredArc(at.x, at.y, ring, ring, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        }

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness));
    }

    /** A cap with a letter on it, or with the command mark. */
    static void draw(juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text,
                     juce::Colour face, juce::Colour edge, juce::Colour ink, bool commandMark = false)
    {
        g.setColour(face);
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);

        if (commandMark)
        {
            drawCommand(g, box.withSizeKeepingCentre(box.getHeight() * 0.52f, box.getHeight() * 0.52f), ink);
            return;
        }

        g.setColour(ink);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(12.5f, box.getHeight() * 0.44f))));
        g.drawText(text, box, juce::Justification::centred);
    }
};

/**
    The computer keyboard, drawn, with the note each key plays written on it.

    A list of which letters are white keys and which are black is accurate and
    almost unusable: it asks the reader to hold a keyboard layout in their head
    and check every letter against it. The layout is the thing being explained,
    so the layout is what should be shown -- in the shape the keys are actually
    in, with the staggered rows, so a glance finds the key rather than a search
    through a line of text.
*/
class KeyboardMap : public juce::Component
{
public:
    KeyboardMap(juce::Colour bg, juce::Colour ink, juce::Colour mute, juce::Colour hair)
        : colBase(bg), colInk(ink), colMute(mute), colHair(hair)
    {
    }

    static constexpr int preferredHeight = 186;

    void paint(juce::Graphics& g) override
    {
        // Four rows, offset the way they sit on a real keyboard, because the
        // offsets are what the hand navigates by.
        static const struct Row
        {
            const char* keys;
            float indent; ///< in key widths
        } rows[] = {{"`1234567890-=", 0.0f},
                    {"qwertyuiop[]\\", 1.35f},
                    {"asdfghjkl;'", 1.75f},
                    {"zxcvbnm,./", 2.25f}};

        auto area = getLocalBounds().reduced(2, 0);

        const int gap = 3;
        const float unit = (float) (area.getWidth() - gap * 13) / 14.5f;
        const int keyH = juce::jmin(34, (area.getHeight() - 26 - gap * 3) / 4);

        int y = area.getY();

        for (const auto& row : rows)
        {
            float x = (float) area.getX() + row.indent * (unit + (float) gap);

            for (const char* c = row.keys; *c != 0; ++c)
            {
                const juce::Rectangle<float> cap(x, (float) y, unit, (float) keyH);
                drawKey(g, cap, *c);
                x += unit + (float) gap;
            }

            y += keyH + gap;
        }

        // The legend, which is two facts: what the shading means, and that the
        // two octave keys sit at the ends of the rows so the hand does not
        // have to leave the playing position to reach them.
        auto legend = juce::Rectangle<int>(area.getX(), y + 6, area.getWidth(), 18);

        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("filled = white key    outlined = black key    \\ and / shift the octave", legend,
                   juce::Justification::centredLeft);
    }

private:
    void drawKey(juce::Graphics& g, juce::Rectangle<float> cap, char c) const
    {
        const int offset = QwertyKeys::noteForKey((juce::juce_wchar) c);
        const bool octaveKey = QwertyKeys::isOctaveKey(c);

        if (offset < 0)
        {
            // Unmapped: present, because its absence is what makes the mapped
            // keys findable, but not competing for attention.
            g.setColour(colHair.withAlpha(0.55f));
            g.drawRoundedRectangle(cap.reduced(0.5f), 3.5f, 1.0f);

            if (octaveKey)
            {
                g.setColour(colInk);
                g.drawRoundedRectangle(cap.reduced(0.5f), 3.5f, 1.2f);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(juce::String::charToString((juce::juce_wchar) c), cap,
                           juce::Justification::centred);
                return;
            }

            g.setColour(colMute.withAlpha(0.45f));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(juce::String::charToString((juce::juce_wchar) c), cap, juce::Justification::centred);
            return;
        }

        // Filled for a white key, outlined for a black one: the same
        // distinction a piano makes, so the eye can read the pattern of a
        // keyboard out of a row of letters.
        static const bool isBlack[12] = {false, true,  false, true,  false, false,
                                         true,  false, true,  false, true,  false};

        const bool black = isBlack[offset % 12];

        if (black)
        {
            g.setColour(colInk.withAlpha(0.10f));
            g.fillRoundedRectangle(cap, 3.5f);
            g.setColour(colInk.withAlpha(0.75f));
            g.drawRoundedRectangle(cap.reduced(0.5f), 3.5f, 1.2f);
        }
        else
        {
            g.setColour(colInk.withAlpha(0.88f));
            g.fillRoundedRectangle(cap, 3.5f);
        }

        auto text = cap.reduced(1.0f);

        g.setColour(black ? colInk : colBase);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(12.0f, cap.getHeight() * 0.42f))));
        g.drawText(juce::String::charToString((juce::juce_wchar) c).toUpperCase(),
                   text.removeFromTop(text.getHeight() * 0.58f), juce::Justification::centred);

        static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

        g.setColour((black ? colInk : colBase).withAlpha(0.62f));
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(9.5f, cap.getHeight() * 0.3f))));
        g.drawText(names[offset % 12], text, juce::Justification::centredTop);
    }

    juce::Colour colBase, colInk, colMute, colHair;
};

} // namespace plugshell
