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
        const juce::Font font{juce::FontOptions(textHeight)};

        if (glyph != Glyph::none)
        {
            auto box = getLocalBounds();
            const int side = juce::jlimit(9, 16, box.getHeight() - 12);

            if (label.isEmpty())
            {
                drawGlyph(g, box.withSizeKeepingCentre(side, side).toFloat());
                return;
            }

            // Mark then word, centred as a pair rather than pinned to the
            // edges, so the two read as one label.
            const int textWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label));

            auto pair = box.withSizeKeepingCentre(side + 7 + textWidth, box.getHeight());
            drawGlyph(g, pair.removeFromLeft(side).withSizeKeepingCentre(side, side).toFloat());
            pair.removeFromLeft(7);

            g.setFont(font);
            g.drawText(label, pair, juce::Justification::centredLeft);
            return;
        }

        g.setFont(font);
        g.drawText(label, getLocalBounds(), juce::Justification::centred);
    }

    void drawGlyph(juce::Graphics& g, juce::Rectangle<float> box) const
    {
        const auto centre = box.getCentre();
        const float r = box.getWidth() * 0.5f;

        switch (glyph)
        {
        case Glyph::back:
        {
            // A chevron rather than an arrow: fewer strokes, and it survives
            // being small, which an arrowhead does not.
            juce::Path chevron;
            chevron.startNewSubPath(centre.x + r * 0.34f, centre.y - r * 0.72f);
            chevron.lineTo(centre.x - r * 0.38f, centre.y);
            chevron.lineTo(centre.x + r * 0.34f, centre.y + r * 0.72f);

            g.strokePath(chevron, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            break;
        }

        case Glyph::sun:
        {
            g.fillEllipse(centre.x - r * 0.4f, centre.y - r * 0.4f, r * 0.8f, r * 0.8f);

            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                g.drawLine(centre.x + std::cos(a) * r * 0.64f, centre.y + std::sin(a) * r * 0.64f,
                           centre.x + std::cos(a) * r, centre.y + std::sin(a) * r, 1.3f);
            }
            break;
        }

        case Glyph::moon:
        {
            // A disc with a bite out of it, which is how to get a crescent
            // without asking two arcs to meet exactly.
            juce::Path disc;
            disc.addEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);

            juce::Path bite;
            bite.addEllipse(centre.x - r * 0.25f, centre.y - r * 1.3f, r * 2.0f, r * 2.0f);

            disc.setUsingNonZeroWinding(false);
            disc.addPath(bite);
            g.fillPath(disc);
            break;
        }

        case Glyph::automatic:
        {
            juce::Path half;
            half.addPieSegment(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 0.0f,
                               juce::MathConstants<float>::pi, 0.0f);
            g.fillPath(half);
            g.drawEllipse(centre.x - r + 0.6f, centre.y - r + 0.6f, r * 2.0f - 1.2f, r * 2.0f - 1.2f, 1.2f);
            break;
        }

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
    static constexpr float textHeight = 12.5f;

    Glyph glyph = Glyph::none;
    bool toggled = false, framed = false;
};

} // namespace plugshell
