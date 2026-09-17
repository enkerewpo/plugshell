// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Eased.h"
#include "Icons.h"

namespace plugshell
{

/**
    A text control for the strip.

    Painting a word and hit-testing its coordinates looks the same in a
    screenshot and is not the same thing: there is no hover state, the cursor
    never changes, and nothing tells the user the word can be clicked. This is
    a real component so that all three come for free.
*/
class StripButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** Drawn marks rather than words, for the controls whose meaning a shape
        carries better: an appearance switch does not need the word "dark" once
        it shows a moon, and Back is the one control in the strip that has to
        be found without being read. */
    enum class Glyph
    {
        none,
        moon,
        sun,
        automatic, ///< half of each
        back
    };

    void setGlyph(Glyph g)
    {
        glyph = g;
        repaint();
    }

    std::function<void()> onClick;

    /** Vertical drag, in whole steps, for a control that holds a number.
        Set it and the button stops reporting a click that was really a drag. */
    std::function<void(int steps)> onDrag;

    explicit StripButton(juce::String text) : label(std::move(text)) {}

    void setText(juce::String t)
    {
        label = std::move(t);
        repaint();
    }

    void setToggled(bool on)
    {
        toggled = on;
        on ? lit.setTarget(1.0f) : lit.setTarget(0.0f);
        anim.nudge();
    }

    /** Re-applied whenever the palette changes: the colours are copied in
        rather than read from the theme each paint, so a switch has to push
        them or the strip keeps the old scheme. */
    void setColours(juce::Colour normal, juce::Colour dim, juce::Colour outline)
    {
        ink = normal;
        mute = dim;
        hair = outline;
        repaint();
    }

    /** Draws an outline, which marks it as a switch rather than a label. */
    void setFramed(bool b)
    {
        framed = b;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // Proportional inset, not a fixed one. Eight pixels off the top and
        // bottom of a 42px strip button leaves a sensible pill; off a 26px
        // header button it leaves a ten-pixel sliver.
        const auto pill =
            getLocalBounds().reduced(2, juce::jmin(8, juce::jmax(2, getHeight() / 5))).toFloat();

        const float hoverAmount = hover.get();
        const float litAmount = lit.get();

        if (hoverAmount > 0.001f)
        {
            g.setColour(ink.withAlpha(0.16f * hoverAmount));
            g.fillRoundedRectangle(pill, 5.0f);
        }

        if (framed)
        {
            if (litAmount > 0.001f)
            {
                g.setColour(ink.withAlpha(0.18f * litAmount));
                g.fillRoundedRectangle(pill, 5.0f);
            }

            g.setColour(hair.interpolatedWith(ink, litAmount));
            g.drawRoundedRectangle(pill, 5.0f, 1.0f);
        }

        g.setColour(framed ? mute.interpolatedWith(ink, litAmount) : ink);

        // Braces, not parentheses: with parentheses this is a function
        // declaration, not a font.
        const auto textColour = framed ? mute.interpolatedWith(ink, litAmount) : ink;
        g.setColour(textColour);

        const juce::Font font{juce::FontOptions(textHeight)};

        if (glyph != Glyph::none)
        {
            auto box = getLocalBounds();
            const int side = juce::jlimit(9, 16, box.getHeight() - 12);

            if (label.isEmpty())
            {
                drawGlyph(g, box.withSizeKeepingCentre(side, side).toFloat(), textColour);
                return;
            }

            // Mark then word, centred as a pair rather than pinned to the
            // edges, so the two read as one label.
            const int textWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label));

            auto pair = box.withSizeKeepingCentre(side + 7 + textWidth, box.getHeight());
            drawGlyph(g, pair.removeFromLeft(side).withSizeKeepingCentre(side, side).toFloat(), textColour);
            pair.removeFromLeft(7);

            g.setFont(font);
            g.drawText(label, pair, juce::Justification::centredLeft);
            return;
        }

        g.setFont(font);
        g.drawText(label, getLocalBounds(), juce::Justification::centred);
    }

    void drawGlyph(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) const
    {
        switch (glyph)
        {
        case Glyph::back:
            Icons::draw(g, Icons::Name::chevronLeft, box, colour);
            break;
        case Glyph::sun:
            Icons::draw(g, Icons::Name::sun, box, colour);
            break;
        case Glyph::moon:
            Icons::draw(g, Icons::Name::moon, box, colour);
            break;
        case Glyph::automatic:
            Icons::draw(g, Icons::Name::sunMoon, box, colour);
            break;
        case Glyph::none:
            break;
        }
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        hover.setTarget(1.0f);
        anim.nudge();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover.setTarget(0.0f);
        anim.nudge();
    }

    void mouseDown(const juce::MouseEvent&) override { dragged = 0; }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!onDrag)
            return;

        // Four points to a step, so a whole number can be landed on.
        const int steps = -e.getDistanceFromDragStartY() / 4;

        if (steps != dragged)
        {
            onDrag(steps - dragged);
            dragged = steps;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragged != 0)
            return; // that was a drag, not a click

        if (getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

    juce::MouseCursor getMouseCursor() override
    {
        return onDrag ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::PointingHandCursor;
    }

private:
    Eased hover{0.0f}, lit{0.0f};

    // Fast, but not so fast that it may as well not be there: at 0.38 the
    // whole crossfade was over in five frames and read as an instant switch.
    Animator anim{[this]
                  {
                      const bool moving = hover.advance(0.20f) | lit.advance(0.24f);
                      if (moving)
                          repaint();
                      return moving;
                  }};

    juce::String label;
    juce::Colour ink{0xffe4e4e4}, mute{0xff7a7a7a}, hair{0xff2e2e2e};
    static constexpr float textHeight = 12.5f;

    int dragged = 0;
    Glyph glyph = Glyph::none;
    bool toggled = false, framed = false;
};

} // namespace plugshell
