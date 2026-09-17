// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Eased.h"

namespace plugshell
{

/**
    Tempo and metre: draggable for a nudge, typeable for a number.

    Both, because they answer different questions. Dragging is for "a bit
    faster than this", which is a feeling, and typing is for "ninety-four",
    which is a fact -- and a control that only drags makes the second one a
    game of inching towards a target you can already name.

    Typing accepts the tempo alone, the metre alone, or both: `94`, `6/8`,
    `94 6/8`. Parsing all three is a few lines and saves the alternative, which
    is two fields and a decision about which one to click.
*/
class TempoField : public juce::Component, public juce::SettableTooltipClient, private juce::Label::Listener
{
public:
    std::function<void(double bpm, int upper, int lower)> onChange;

    TempoField()
    {
        editor.setJustificationType(juce::Justification::centred);
        editor.setEditable(true, true, false); // one click is enough for a field this small
        editor.addListener(this);
        editor.setVisible(false);
        addChildComponent(editor);

        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void setColours(juce::Colour normal, juce::Colour dim, juce::Colour outline)
    {
        ink = normal;
        mute = dim;
        hair = outline;

        editor.setColour(juce::Label::textColourId, ink);
        editor.setColour(juce::Label::backgroundColourId, hair.withAlpha(0.4f));
        editor.setColour(juce::Label::outlineWhenEditingColourId, ink);
        editor.setColour(juce::TextEditor::highlightColourId, ink.withAlpha(0.25f));
        editor.setColour(juce::TextEditor::textColourId, ink);
        repaint();
    }

    void setValue(double beatsPerMinute, int upper, int lower)
    {
        bpm = beatsPerMinute;
        numerator = upper;
        denominator = lower;
        repaint();
    }

    juce::String text() const
    {
        return juce::String(bpm, bpm == std::floor(bpm) ? 0 : 1) + "  " + juce::String(numerator) + "/" +
               juce::String(denominator);
    }

    void paint(juce::Graphics& g) override
    {
        if (editor.isVisible())
            return;

        const auto pill = getLocalBounds().reduced(2, juce::jmin(8, juce::jmax(2, getHeight() / 5)));

        if (glow.get() > 0.001f)
        {
            g.setColour(ink.withAlpha(0.16f * glow.get()));
            g.fillRoundedRectangle(pill.toFloat(), 5.0f);
        }

        g.setColour(hair);
        g.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 5.0f, 1.0f);

        g.setColour(ink);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(text(), getLocalBounds(), juce::Justification::centred);
    }

    void resized() override { editor.setBounds(getLocalBounds().reduced(2, 6)); }

    void mouseEnter(const juce::MouseEvent&) override
    {
        glow.setTarget(1.0f);
        anim.nudge();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        glow.setTarget(0.0f);
        anim.nudge();
    }

    void mouseDown(const juce::MouseEvent&) override { dragged = 0; }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int steps = -e.getDistanceFromDragStartY() / 4;

        if (steps != dragged)
        {
            emit(bpm + (steps - dragged), numerator, denominator);
            dragged = steps;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        // A drag that moved is not also a click on the way up.
        if (dragged != 0 || !getLocalBounds().contains(e.getPosition()))
            return;

        beginEditing();
    }

private:
    void beginEditing()
    {
        editor.setText(juce::String(bpm, bpm == std::floor(bpm) ? 0 : 1) + " " + juce::String(numerator) +
                           "/" + juce::String(denominator),
                       juce::dontSendNotification);
        editor.setVisible(true);
        editor.showEditor();

        if (auto* active = editor.getCurrentTextEditor())
        {
            active->setJustification(juce::Justification::centred);
            active->setFont(juce::Font(juce::FontOptions(12.5f)));
            active->grabKeyboardFocus();
            active->selectAll();
        }

        repaint();
    }

    void labelTextChanged(juce::Label*) override { commit(); }

    void editorHidden(juce::Label*, juce::TextEditor&) override
    {
        editor.setVisible(false);
        repaint();
    }

    void commit()
    {
        auto typed = editor.getText().trim();

        double newBpm = bpm;
        int upper = numerator, lower = denominator;

        // The metre is whatever has a slash in it; the tempo is whatever is
        // left. That way the order does not matter and neither is required.
        juce::StringArray parts;
        parts.addTokens(typed, " \t", "");
        parts.removeEmptyStrings();

        for (const auto& part : parts)
        {
            if (part.contains("/"))
            {
                const int u = part.upToFirstOccurrenceOf("/", false, false).getIntValue();
                const int l = part.fromFirstOccurrenceOf("/", false, false).getIntValue();

                if (u > 0 && l > 0)
                {
                    upper = u;
                    lower = l;
                }
            }
            else if (part.getDoubleValue() > 0.0)
            {
                newBpm = part.getDoubleValue();
            }
        }

        emit(newBpm, upper, lower);
    }

    void emit(double newBpm, int upper, int lower)
    {
        if (onChange)
            onChange(juce::jlimit(20.0, 999.0, newBpm), upper, lower);
    }

    juce::Label editor;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    int dragged = 0;

    Eased glow{0.0f};
    Animator anim{[this]
                  {
                      const bool moving = glow.advance(0.20f);
                      if (moving)
                          repaint();
                      return moving;
                  }};

    juce::Colour ink{0xffe4e4e4}, mute{0xff7a7a7a}, hair{0xff2e2e2e};
};

} // namespace plugshell
