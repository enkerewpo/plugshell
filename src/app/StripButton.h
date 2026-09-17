// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Eased.h"

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
    std::function<void()> onClick;

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
        const auto pill = getLocalBounds().reduced(2, 8).toFloat();

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
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(label, getLocalBounds(), juce::Justification::centred);
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

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }

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
    bool toggled = false, framed = false;
};

} // namespace plugshell
