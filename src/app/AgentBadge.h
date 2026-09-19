// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Eased.h"

namespace plugshell
{

/**
    Says who is driving, while they are driving.

    This host is meant to be operated by a program, which creates a situation
    no ordinary application has: controls move, plugins load and parameters
    change while nobody is touching the machine. Without something saying so,
    that reads as the application doing things by itself -- and the first
    instinct on seeing a window operate itself is to reach for it, which is
    the one thing that makes the session worse.

    So the badge is not decoration and it is not branding. It answers the
    question a person actually has in front of a window that is moving: is
    something doing this, and is it still doing it.

    It answers the second part by fading rather than by a light that is on or
    off. An agent does not disconnect; it stops sending. The badge is brightest
    on the most recent request and decays from there, so a run that has gone
    quiet looks quiet within a couple of seconds without ever claiming the
    agent has gone.
*/
class AgentBadge : public juce::Component, private juce::Timer
{
public:
    AgentBadge()
    {
        setInterceptsMouseClicks(false, false);
        setVisible(false);
    }

    void setColours(juce::Colour normal, juce::Colour dim, juce::Colour accent)
    {
        ink = normal;
        mute = dim;
        live = accent;
        repaint();
    }

    /** Called on every request from a controlling program.

        @p name is whatever the caller has said to call it, or empty for one
        that has not said. "An agent" is the honest answer then: something is
        driving this, and it did not introduce itself. */
    /** @param doing  what the request is, in the present tense: "loading
                       Serum", "setting Filter 1 Cutoff", "capturing the
                       editor". This is the half of the badge that is worth
                       reading -- that something is driving is visible from
                       the controls moving, and what it is doing is not. */
    void sawRequest(const juce::String& name, const juce::String& doing)
    {
        const auto wanted = name.isNotEmpty() ? name : juce::String("An agent");
        const bool renamed = wanted != agent || doing != activity;

        agent = wanted;
        activity = doing.isNotEmpty() ? doing : juce::String("driving");
        lastSeen = juce::Time::getMillisecondCounterHiRes();

        // This is as wide as the name it is showing, and the name arrives with
        // the first request rather than at construction -- so the parent has
        // to lay out again or the badge keeps the zero width it was given
        // before anything was driving.
        if (renamed)
            if (auto* parent = getParentComponent())
                parent->resized();

        if (!isVisible())
        {
            setVisible(true);
            glow.snapTo(0.0f);
        }

        glow.setTarget(1.0f);

        if (!isTimerRunning())
            startTimerHz(30);
    }

    /** How long since the last request, in seconds, or a negative number when
        nothing has ever driven this. */
    double secondsSinceRequest() const
    {
        if (lastSeen <= 0.0)
            return -1.0;

        return (juce::Time::getMillisecondCounterHiRes() - lastSeen) / 1000.0;
    }

    juce::String getAgent() const { return agent; }
    juce::String getActivity() const { return activity; }
    /** Whether a badge is up. True from the first request, not from the first
        frame after it: the caller asking whether it is showing has just made
        the request that put it there, and answering "no" because the fade has
        not had a frame yet would be a lie about the caller's own effect. */
    bool isActive() const { return isVisible(); }

    /** Wide enough for the name it is showing, so it can be laid out beside
        controls that must not be overlapped. Zero when nothing is driving.

        Measured the same way paint lays it out, piece by piece rather than as
        one string. Measuring the whole sentence and drawing it in two colours
        is not the same sum -- it is a few points short, which is enough to
        turn "Claude Code" into "Claude Co...". */
    int preferredWidth() const
    {
        if (agent.isEmpty())
            return 0;

        return dotSize + gap + widthOf(agent) + widthOf(suffixText()) + trailing;
    }

    void paint(juce::Graphics& g) override
    {
        const float lit = glow.get();

        if (lit <= 0.01f)
            return;

        auto box = getLocalBounds();

        // The dot breathes while an agent is active and stops when it stops.
        // A pulse that continued after the last request would be a claim that
        // something is still happening.
        const float breath = 0.55f + 0.45f * (float) std::sin(phase);
        const float dotAlpha = lit * (0.35f + 0.65f * breath);

        auto dot = box.removeFromLeft(dotSize).withSizeKeepingCentre(dotSize, dotSize).toFloat();

        g.setColour(live.withAlpha(0.22f * lit * breath));
        g.fillEllipse(dot.expanded(3.0f));
        g.setColour(live.withAlpha(dotAlpha));
        g.fillEllipse(dot.reduced(1.0f));

        box.removeFromLeft(gap);

        // The name in the reading colour and what it is doing in the quiet
        // one, so the eye lands on which agent rather than on the sentence.
        g.setFont(font());
        g.setColour(ink.withAlpha(0.85f * lit));
        g.drawText(agent, box.removeFromLeft(widthOf(agent)), juce::Justification::centredLeft, false);

        g.setColour(mute.withAlpha(0.8f * lit));
        g.drawText(suffixText(), box, juce::Justification::centredLeft, false);
    }

private:
    /** Brightest on the most recent request, decaying from there. Two seconds
        of quiet takes it out; nothing about that says the agent has gone, only
        that it is not doing anything now. */
    static constexpr double quietAfterSeconds = 2.0;
    static constexpr int dotSize = 7, gap = 8, trailing = 10;
    juce::String suffixText() const { return "  is " + activity; }

    static juce::Font font() { return juce::Font(juce::FontOptions(11.5f)); }

    static int widthOf(const juce::String& text)
    {
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(font(), text)) + 2;
    }

    void timerCallback() override
    {
        phase += 0.16;

        if (secondsSinceRequest() > quietAfterSeconds)
            glow.setTarget(0.0f);

        const bool moving = glow.advance(0.12f);
        repaint();

        if (!moving && glow.get() <= 0.01f)
        {
            stopTimer();
            setVisible(false);
        }
    }

    juce::String agent, activity{"driving"};
    double lastSeen = 0.0, phase = 0.0;
    Eased glow{0.0f};
    juce::Colour ink{0xffe4e4e4}, mute{0xff7a7a7a}, live{0xff6ee7a0};
};

} // namespace plugshell
