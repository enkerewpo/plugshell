// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StripButton.h"

namespace plugshell
{

/**
    A panel that covers the editor while it is needed and then goes away.

    Help and settings are consulted occasionally and would be dead weight as
    permanent chrome, which the strip exists to avoid. Dismissing on Escape or
    on a click outside keeps them from feeling like a mode the user is stuck
    in.
*/
class Overlay : public juce::Component
{
public:
    std::function<void()> onDismiss;

    ~Overlay() override
    {
        if (content != nullptr)
            content->setLookAndFeel(nullptr);
    }

    Overlay(juce::String heading, juce::Colour base, juce::Colour ink, juce::Colour mute, juce::Colour hair)
        : title(std::move(heading)), colBase(base), colInk(ink), colMute(mute), colHair(hair)
    {
        setWantsKeyboardFocus(true);
    }

    void setRows(juce::Array<juce::StringArray> r)
    {
        rows = std::move(r);
        resized();
        repaint();
    }

    /** Widens the panel past the default reading width. A list of shortcuts
        wants a column; an analyser wants the whole window. */
    void setPanelWidth(int w)
    {
        maxPanelWidth = w;
        resized();
        repaint();
    }

    /** A note under the content, for context the controls cannot express. */
    void setFooter(juce::String f)
    {
        footer = std::move(f);
        repaint();
    }

    /** Hosts a real control instead of static rows, so a settings panel can
        actually change something rather than only report it. */
    void setContent(std::unique_ptr<juce::Component> c, int preferredHeight)
    {
        content = std::move(c);
        contentHeight = preferredHeight;
        if (content != nullptr)
            addAndMakeVisible(content.get());
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colBase.withAlpha(0.94f));

        const auto panel = panelBounds();
        g.setColour(colBase);
        g.fillRect(panel);
        g.setColour(colHair);
        g.drawRect(panel, 1);

        g.setColour(colInk);
        g.setFont(juce::Font(juce::FontOptions(17.0f)));
        g.drawText(title, panel.getX() + 24, panel.getY() + 18, 300, 24, juce::Justification::centredLeft);

        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(13.5f)));
        g.drawText("esc to close", panel.getRight() - 140, panel.getY() + 20, 116, 20,
                   juce::Justification::centredRight);

        g.setColour(colHair);
        g.drawLine((float) panel.getX() + 24, (float) panel.getY() + 52, (float) panel.getRight() - 24,
                   (float) panel.getY() + 52, 1.0f);

        int y = panel.getY() + 68;
        for (const auto& row : rows)
        {
            if (row.size() == 1)
            {
                // A single cell is a section heading.
                y += 10;
                g.setColour(colMute);
                g.setFont(juce::Font(juce::FontOptions(13.5f)));
                g.drawText(row[0], panel.getX() + 24, y, 400, 20, juce::Justification::centredLeft);
                y += 28;
                continue;
            }

            g.setColour(colInk);
            g.setFont(juce::Font(
                juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            g.drawText(row[0], panel.getX() + 24, y, 150, 20, juce::Justification::centredLeft);

            g.setColour(colMute);
            g.setFont(juce::Font(juce::FontOptions(13.5f)));
            g.drawText(row[1], panel.getX() + 186, y, panel.getWidth() - 220, 20,
                       juce::Justification::centredLeft);
            y += 26;
        }
    }

    void resized() override
    {
        if (content != nullptr)
        {
            const auto panel = panelBounds();
            const int reserve = footer.isEmpty() ? 92 : 148;
            content->setBounds(panel.getX() + 24, panel.getY() + 68, panel.getWidth() - 48,
                               panel.getHeight() - reserve);
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (footer.isEmpty())
            return;

        const auto panel = panelBounds();
        const auto area =
            juce::Rectangle<int>(panel.getX() + 24, panel.getBottom() - 56, panel.getWidth() - 48, 44);
        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText(footer, area, juce::Justification::topLeft, 3);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!panelBounds().contains(e.getPosition()) && onDismiss)
            onDismiss();
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && onDismiss)
        {
            onDismiss();
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<int> panelBounds() const
    {
        const int w = juce::jmin(maxPanelWidth, getWidth() - 80);
        const int h = content != nullptr
                          ? juce::jmin(contentHeight + (footer.isEmpty() ? 100 : 156), getHeight() - 80)
                          : juce::jmin(72 + rows.size() * 28 + 40, getHeight() - 80);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    juce::String title;
    juce::Array<juce::StringArray> rows;
    std::unique_ptr<juce::Component> content;
    int contentHeight = 320;
    int maxPanelWidth = 560;
    juce::String footer;
    juce::Colour colBase, colInk, colMute, colHair;
};

} // namespace plugshell
