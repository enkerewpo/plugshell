// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
        repaint();
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

        if (hovering)
        {
            g.setColour(ink.withAlpha(0.12f));
            g.fillRoundedRectangle(pill, 5.0f);
        }

        if (framed)
        {
            if (toggled)
            {
                g.setColour(ink.withAlpha(0.18f));
                g.fillRoundedRectangle(pill, 5.0f);
            }
            g.setColour(toggled ? ink : hair);
            g.drawRoundedRectangle(pill, 5.0f, 1.0f);
        }

        g.setColour(toggled || !framed ? ink : mute);
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(label, getLocalBounds(), juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        hovering = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovering = false;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }

private:
    juce::String label;
    juce::Colour ink{0xffe4e4e4}, mute{0xff7a7a7a}, hair{0xff2e2e2e};
    bool hovering = false, toggled = false, framed = false;
};

} // namespace plugshell
