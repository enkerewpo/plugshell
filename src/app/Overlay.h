// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Eased.h"
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
        appear.snapTo(0.0f);
    }

    void setRows(juce::Array<juce::StringArray> r)
    {
        rows = std::move(r);
        resized();
        repaint();
    }

    /** Where the darkening stops, in this component's own coordinates.

        The panel's window is allowed to be larger than the host's, so that a
        narrow plugin does not squeeze it into an unreadable column. That makes
        the scrim overhang, dimming the desktop and whatever else is behind --
        which says the whole screen is blocked when only this window is. */
    void setScrimArea(juce::Rectangle<int> area)
    {
        scrim = area;
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

    /** Called back when a dismissal has finished fading, so the owner can
        let go of the window rather than removing it mid-fade. */
    std::function<void()> onFadedOut;

    /** Started once the window is actually on screen, not in the constructor.

        Beginning it at construction meant the first frames of the fade ran
        while the window was still being created and laid out, so by the time
        anything was visible the alpha had already jumped most of the way and
        the rest arrived in two steps. Closing looked smooth because by then
        there was nothing left to set up. */
    void beginFadeIn()
    {
        appear.snapTo(0.0f);
        setAlpha(0.0f);
        appear.setTarget(1.0f);
        anim.nudge();
    }

    /** Starts the panel on its way out. It is not gone until onFadedOut. */
    void beginFadeOut()
    {
        if (leaving)
            return;

        leaving = true;
        appear.setTarget(0.0f);
        anim.nudge();
    }

    void paint(juce::Graphics& g) override
    {
        const float t = appear.get();

        // Only over the window it belongs to. Rounded to the same degree the
        // window is, so the corners do not show a dark square behind a light
        // curve.
        const auto darkened = scrim.isEmpty() ? getLocalBounds() : scrim;

        g.setColour(colBase.withAlpha(0.94f * t));
        g.fillRoundedRectangle(darkened.toFloat(), 10.0f);

        // Rounded, because a panel floating over the page is a card and a
        // card has corners. The scrim behind it keeps its square edges -- it
        // is the window, not the card.
        const auto panel = panelBounds().toFloat();

        g.setColour(colBase);
        g.fillRoundedRectangle(panel, cornerRadius);
        g.setColour(colHair);
        g.drawRoundedRectangle(panel.reduced(0.5f), cornerRadius, 1.0f);

        if (tooNarrow())
        {
            g.setColour(colMute);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawFittedText(title + " needs a wider window.\nWiden it, or press esc and open this again.",
                             panel.reduced(18.0f).toNearestInt(), juce::Justification::centred, 4);
            return;
        }

        g.setColour(colInk);
        g.setFont(juce::Font(juce::FontOptions(17.0f)));
        g.drawText(title, juce::Rectangle<float>(panel.getX() + 24.0f, panel.getY() + 18.0f, 300.0f, 24.0f),
                   juce::Justification::centredLeft);

        // A drawn control rather than a written instruction. "esc to close"
        // is a promise about a key, and a key can be taken by an input method
        // before it ever reaches this application -- which is exactly what was
        // happening. A cross is a promise about something on screen.
        closeHit = juce::Rectangle<int>(juce::roundToInt(panel.getRight()) - 46,
                                        juce::roundToInt(panel.getY()) + 16, 30, 30);

        if (closeHot)
        {
            g.setColour(colInk.withAlpha(0.12f));
            g.fillRoundedRectangle(closeHit.toFloat(), 5.0f);
        }

        {
            const auto box = closeHit.toFloat().reduced(9.0f);
            g.setColour(closeHot ? colInk : colMute);
            g.drawLine(box.getX(), box.getY(), box.getRight(), box.getBottom(), 1.4f);
            g.drawLine(box.getRight(), box.getY(), box.getX(), box.getBottom(), 1.4f);
        }

        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(juce::String::fromUTF8("\xe2\x8c\x98W"), panel.getRight() - 92.0f, panel.getY() + 20.0f,
                   40, 20, juce::Justification::centredRight);

        g.setColour(colHair);
        g.drawLine((float) panel.getX() + 24.0f, (float) panel.getY() + 52.0f,
                   (float) panel.getRight() - 24.0f, (float) panel.getY() + 52.0f, 1.0f);

        int y = panel.getY() + 68.0f;
        for (const auto& row : rows)
        {
            if (row.size() == 1)
            {
                // A single cell is a section heading.
                y += 10;
                g.setColour(colMute);
                g.setFont(juce::Font(juce::FontOptions(13.5f)));
                g.drawText(row[0], panel.getX() + 24.0f, y, 400, 20, juce::Justification::centredLeft);
                y += 28;
                continue;
            }

            g.setColour(colInk);
            g.setFont(juce::Font(
                juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            g.drawText(row[0], panel.getX() + 24.0f, y, 150, 20, juce::Justification::centredLeft);

            g.setColour(colMute);
            g.setFont(juce::Font(juce::FontOptions(13.5f)));
            g.drawText(row[1], panel.getX() + 186, y, panel.getWidth() - 220.0f, 20,
                       juce::Justification::centredLeft);
            y += 26;
        }
    }

    void resized() override
    {
        if (content != nullptr)
            content->setVisible(!tooNarrow());

        if (content != nullptr && !tooNarrow())
        {
            const auto panel = panelBounds();
            const int reserve = footer.isEmpty() ? 92 : 148;
            content->setBounds(panel.getX() + 24.0f, panel.getY() + 68.0f, panel.getWidth() - 48,
                               panel.getHeight() - reserve);
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (footer.isEmpty() || tooNarrow())
            return;

        const auto panel = panelBounds();
        const auto area =
            juce::Rectangle<int>(panel.getX() + 24.0f, panel.getBottom() - 56, panel.getWidth() - 48, 44);
        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText(footer, area, juce::Justification::topLeft, 3);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!onDismiss)
            return;

        if (closeHit.contains(e.getPosition()) || !panelBounds().contains(e.getPosition()))
            onDismiss();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool hot = closeHit.contains(e.getPosition());

        if (hot != closeHot)
        {
            closeHot = hot;
            setMouseCursor(hot ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint(closeHit.expanded(2));
        }
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
    // Fade only. The panel sits where it sits; sliding it in from below said
    // it had come from the bottom of the window, which is not where it came
    // from and not something worth animating.
    /** Below this the panel cannot lay itself out.

        The title is drawn in a 300-wide box from `panel.getX() + 24.0f`, and
        "esc to close" in a 116-wide box ending at `panel.getRight() - 24.0f`;
        narrower than this the two overlap and the label/value rows lose their
        value column entirely. Rendering it anyway produced a panel with the
        heading printed on top of the close hint and every label truncated to
        three characters.
    */
    static constexpr int minPanelWidth = 420;

    bool tooNarrow() const { return getWidth() - 80 < minPanelWidth; }

    static constexpr float cornerRadius = 10.0f;

    juce::Rectangle<int> panelBounds() const
    {
        const int w = juce::jmin(maxPanelWidth, getWidth() - 80);
        const int h = content != nullptr
                          ? juce::jmin(contentHeight + (footer.isEmpty() ? 100 : 156), getHeight() - 80)
                          : juce::jmin(72 + rows.size() * 28 + 40, getHeight() - 80);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    juce::Rectangle<int> scrim;
    juce::Rectangle<int> closeHit;
    bool closeHot = false;
    Eased appear{0.0f};
    bool leaving = false;

    Animator anim{[this]
                  {
                      const bool moving = appear.advance(0.36f);

                      if (moving)
                      {
                          setAlpha(appear.get());
                          repaint();
                      }
                      else if (leaving && onFadedOut)
                      {
                          onFadedOut();
                          return false;
                      }

                      return moving;
                  }};

    juce::String title;
    juce::Array<juce::StringArray> rows;
    std::unique_ptr<juce::Component> content;
    int contentHeight = 320;
    int maxPanelWidth = 560;
    juce::String footer;
    juce::Colour colBase, colInk, colMute, colHair;
};

} // namespace plugshell
