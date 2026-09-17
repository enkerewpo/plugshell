// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

#include "AnalyserTap.h"
#include "Eased.h"

namespace plugshell
{

/**
    The master fader, with the output meter drawn inside it.

    One control rather than two, because they answer the same question from
    opposite ends: the bars say how loud it is, the handle says how loud you
    asked for. Separating them costs twice the width of a strip that is
    already full, and puts the number you are about to change away from the
    number you are watching while you change it.

    The bars are the two output channels, RMS above, peak marked as a tick.
    Full scale is the right-hand edge, so a bar reaching it is the signal
    reaching the point where the converter stops having room, and the colour
    says so before the number does.

    Dragging is relative, not absolute. A fader that jumps to wherever it was
    clicked is fine at the size of a mixer channel and dangerous at the size
    of a strip button, where the click that was meant to be a small adjustment
    lands on full volume and the monitors are already loud.
*/
class OutputMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    /** Called when the position moves, with the linear gain. */
    std::function<void(float)> onGainChange;

    /** The top of the fader, as a multiplier. +6 dB, which is enough to
        rescue a quiet patch and not enough to be a hazard. */
    static constexpr float maxGain = 2.0f;

    /** The bottom of the meter. Below this a bar would be a pixel wide and
        say nothing that silence does not. */
    static constexpr float floorDb = -60.0f;

    /** Above this the bar changes colour: the last of the headroom, where a
        master usually wants to sit and not go past. */
    static constexpr float hotDb = -6.0f;

    OutputMeter()
    {
        setTooltip("Master output. Drag to set the level, double-click for unity, "
                   "scroll for small steps. Bars are L and R; the tick is the recent peak.");
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        startTimerHz(30);
    }

    void setColours(juce::Colour normal, juce::Colour dim, juce::Colour outline)
    {
        ink = normal;
        mute = dim;
        hair = outline;
        repaint();
    }

    void setSource(const AnalyserTap* t) { tap = t; }

    /** Linear gain in, position out. Cubic, which is close enough to the
        taper of a real fader that the useful range is not crammed into the
        top tenth of the travel. */
    static float positionForGain(float gain)
    {
        return juce::jlimit(0.0f, 1.0f, std::cbrt(juce::jmax(0.0f, gain) / maxGain));
    }

    static float gainForPosition(float p)
    {
        const float q = juce::jlimit(0.0f, 1.0f, p);
        return q * q * q * maxGain;
    }

    void setGain(float linear)
    {
        position = positionForGain(linear);
        repaint();
    }

    float getGain() const { return gainForPosition(position); }

    void paint(juce::Graphics& g) override
    {
        const auto pill = getLocalBounds().reduced(2, juce::jmin(8, juce::jmax(2, getHeight() / 5)));

        if (glow.get() > 0.001f)
        {
            g.setColour(ink.withAlpha(0.16f * glow.get()));
            g.fillRoundedRectangle(pill.toFloat(), 5.0f);
        }

        g.setColour(hair);
        g.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 5.0f, 1.0f);

        auto inner = pill.reduced(5, 3);

        // Below this there is not enough width for both, and of the two the
        // bars are the part that still says something at half the size.
        if (hasReadout())
        {
            const auto readout = inner.removeFromRight(readoutWidth);
            inner.removeFromRight(5);

            // A rule between the two, so the number reads as a readout rather
            // than as something floating at the loud end of the meter.
            g.setColour(hair);
            g.drawLine((float) readout.getX() - 3.0f, (float) pill.getY() + 4.0f,
                       (float) readout.getX() - 3.0f, (float) pill.getBottom() - 4.0f, 1.0f);

            paintReadout(g, readout);
        }

        paintTrack(g, inner);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        glow.setTarget(1.0f);
        anim.nudge();
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        glow.setTarget(0.0f);
        anim.nudge();
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStart = position;
        dragging = true;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int span = juce::jmax(1, getWidth() - 14 - (hasReadout() ? readoutWidth + 5 : 0));

        // Fine mode is the same gesture with a modifier rather than a second
        // control: the adjustment that needs it is the one already underway.
        const float scale = e.mods.isShiftDown() || e.mods.isAltDown() ? 0.2f : 1.0f;

        moveTo(dragStart + scale * (float) e.getDistanceFromDragStartX() / (float) span);
    }

    void mouseUp(const juce::MouseEvent&) override { dragging = false; }

    void mouseDoubleClick(const juce::MouseEvent&) override { moveTo(positionForGain(1.0f)); }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY != 0.0f)
            moveTo(position + wheel.deltaY * 0.15f);
    }

private:
    static constexpr int readoutWidth = 42;

    /** The number is dropped before the bars are, because a bar at half the
        width still shows a level and a truncated number shows nothing. */
    bool hasReadout() const { return getWidth() - 14 >= readoutWidth + 56; }

    void moveTo(float p)
    {
        const float next = juce::jlimit(0.0f, 1.0f, p);

        // Unity is the position you return to most and the one hardest to hit
        // by hand, so it catches anything passing close.
        const float unity = positionForGain(1.0f);
        position = std::abs(next - unity) < 0.012f ? unity : next;

        if (onGainChange)
            onGainChange(getGain());

        repaint();
    }

    /** Full scale at the right-hand edge, the floor at the left, linear in
        decibels between them -- which is how a level is heard and how every
        meter worth reading is scaled. */
    static float normalisedFor(float linear)
    {
        const float db = juce::Decibels::gainToDecibels(linear, floorDb);
        return juce::jlimit(0.0f, 1.0f, (db - floorDb) / -floorDb);
    }

    juce::Colour colourForLevel(float linear, bool clipped) const
    {
        if (clipped)
            return juce::Colour{0xffd2553f};

        if (juce::Decibels::gainToDecibels(linear, floorDb) >= hotDb)
            return juce::Colour{0xffd9a441};

        // Short of full ink. A meter sits at two thirds of its travel for
        // most of a session, and at full contrast that makes the brightest
        // thing on the strip something nobody is looking at.
        return ink.withAlpha(0.82f);
    }

    void paintTrack(juce::Graphics& g, juce::Rectangle<int> track)
    {
        // Three lanes, not two layers. Drawn on top of each other the fader
        // fill and the level bar were the same shape growing from the same
        // edge, and there was no telling which of them you were looking at.
        // Above and below settles it: the top lane is what you asked for, the
        // two beneath are what came out.
        const int meterHeight = juce::jmax(2, (track.getHeight() - 10) / 2);

        auto lanes = track;
        const auto fader = lanes.removeFromTop(juce::jmax(3, track.getHeight() - meterHeight * 2 - 3));
        lanes.removeFromTop(2);

        paintFader(g, fader);

        const auto levels = tap != nullptr ? tap->getLevels() : AnalyserTap::Levels{};

        // Two bars rather than one summed bar, because a channel that has
        // gone silent is the fault a master meter is most likely to be the
        // first to see.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto row = lanes.removeFromTop(meterHeight);
            if (ch == 0)
                lanes.removeFromTop(juce::jmax(0, lanes.getHeight() - meterHeight));

            paintBar(g, row, levels, ch);
        }
    }

    void paintFader(juce::Graphics& g, juce::Rectangle<int> lane)
    {
        const float w = (float) lane.getWidth();
        const float mid = (float) lane.getCentreY();

        g.setColour(hair);
        g.fillRect(juce::Rectangle<float>((float) lane.getX(), mid - 0.5f, w, 1.0f));

        // The travel already used, so the handle has somewhere to have come
        // from and the position is legible without reading the number.
        g.setColour(mute.withAlpha(0.7f));
        g.fillRect(juce::Rectangle<float>((float) lane.getX(), mid - 0.5f, w * position, 1.0f));

        // Unity marked on the scale. It is the position worth returning to
        // and the only one on the fader that means anything by itself.
        const float unity = (float) lane.getX() + w * positionForGain(1.0f);
        g.setColour(hair);
        g.fillRect(juce::Rectangle<float>(unity, (float) lane.getY(), 1.0f, (float) lane.getHeight()));

        const float x = juce::jlimit(0.0f, w - 3.0f, w * position);
        g.setColour(ink);
        g.fillRoundedRectangle((float) lane.getX() + x, (float) lane.getY(), 3.0f, (float) lane.getHeight(),
                               1.5f);
    }

    void paintBar(juce::Graphics& g, juce::Rectangle<int> row, const AnalyserTap::Levels& levels, int ch)
    {
        const float w = (float) row.getWidth();

        g.setColour(hair.withAlpha(0.45f));
        g.fillRect(row.toFloat());

        // Where the headroom runs short, marked on the scale rather than left
        // to be inferred from a colour that has not changed yet.
        g.setColour(hair);
        g.fillRect(juce::Rectangle<float>(
            (float) row.getX() + w * normalisedFor(juce::Decibels::decibelsToGain(hotDb, floorDb)),
            (float) row.getY(), 1.0f, (float) row.getHeight()));

        const float level = normalisedFor(levels.rms[ch]);

        if (level > 0.0f)
        {
            g.setColour(colourForLevel(levels.rms[ch], levels.clipped));
            g.fillRect(row.toFloat().withWidth(juce::jmax(1.0f, w * level)));
        }

        const float peak = normalisedFor(levels.peak[ch]);

        if (peak > 0.0f)
        {
            g.setColour(colourForLevel(levels.peak[ch], levels.clipped));
            g.fillRect(juce::Rectangle<float>((float) row.getX() + juce::jmin(w - 1.5f, w * peak),
                                              (float) row.getY(), 1.5f, (float) row.getHeight()));
        }
    }

    void paintReadout(juce::Graphics& g, juce::Rectangle<int> box)
    {
        const auto levels = tap != nullptr ? tap->getLevels() : AnalyserTap::Levels{};

        // Under the pointer it shows what you are setting; the rest of the
        // time it shows what came out, which is the number you would
        // otherwise have to guess from the length of a bar.
        const bool showingGain = dragging || isMouseOver(true);

        const float value = showingGain ? getGain() : juce::jmax(levels.peak[0], levels.peak[1]);
        const float db = juce::Decibels::gainToDecibels(value, floorDb);

        const juce::String text = db <= floorDb          ? juce::String::fromUTF8("-\xe2\x88\x9e")
                                  : std::abs(db) < 10.0f ? juce::String(db, 1)
                                                         : juce::String(juce::roundToInt(db));

        g.setColour(showingGain ? ink : levels.clipped ? juce::Colour{0xffd2553f} : mute);
        g.setFont(juce::Font(
            juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain)));
        g.drawText(text, box, juce::Justification::centredRight, false);
    }

    void timerCallback() override
    {
        if (tap == nullptr)
            return;

        const auto now = tap->getLevels();

        // A meter at rest is still a meter: repainting thirty times a second
        // to draw the same two bars is work nobody asked for, and this runs
        // for as long as the application is open.
        const auto changed = [](float a, float b) { return std::abs(a - b) > 0.0005f; };

        if (changed(now.rms[0], shown.rms[0]) || changed(now.rms[1], shown.rms[1]) ||
            changed(now.peak[0], shown.peak[0]) || changed(now.peak[1], shown.peak[1]) ||
            now.clipped != shown.clipped)
        {
            shown = now;
            repaint();
        }
    }

    const AnalyserTap* tap = nullptr;
    AnalyserTap::Levels shown;

    float position = positionForGain(1.0f);
    float dragStart = 0.0f;
    bool dragging = false;

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
