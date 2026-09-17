// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalyserTap.h"

namespace plugshell
{

/**
    Waveform and spectrum, side by side.

    The reason this exists is that a parameter value does not teach anything.
    Turning a filter and watching the curve move, or changing a waveform and
    seeing which harmonics appear, is the part that makes the parameter mean
    something. That is worth building into a host whose purpose is to make
    plugins legible.

    The slide is not decoration either: a panel that appears instantly reads as
    a different screen, whereas one that grows out of the strip reads as the
    same screen showing more, and the eye keeps its place.
*/
class ScopePanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int fftOrder = 11; // 2048 points
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int scopeFrames = 1024;

    ScopePanel(const AnalyserTap& t, juce::Colour bg, juce::Colour ink, juce::Colour mute, juce::Colour hair)
        : tap(t), colBase(bg), colInk(ink), colMute(mute), colHair(hair), fft(fftOrder),
          window(fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        scope.resize(scopeFrames);
        fftData.resize((size_t) fftSize * 2);
        magnitudes.resize((size_t) fftSize / 2, -100.0f);
        setInterceptsMouseClicks(false, false);
    }

    void setOpen(bool shouldOpen)
    {
        open = shouldOpen;
        if (open)
            startTimerHz(30);
        else if (!isAnimating())
            stopTimer();

        startTimerHz(60); // 60 so the traces move smoothly, not just the slide
    }

    bool isOpen() const { return open; }

    /** 0 when closed, 1 when fully out. */
    float getExtent() const { return extent; }

    bool isAnimating() const { return std::abs(extent - (open ? 1.0f : 0.0f)) > 0.001f; }

    std::function<void()> onExtentChanged;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colBase);
        g.setColour(colHair);
        g.drawLine(0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

        auto r = getLocalBounds().reduced(16, 12);
        auto left = r.removeFromLeft(r.getWidth() / 2 - 8);
        r.removeFromLeft(16);

        drawScope(g, left);
        drawSpectrum(g, r);
    }

private:
    void timerCallback() override
    {
        // Eased towards the target so the movement decelerates rather than
        // stopping dead.
        const float target = open ? 1.0f : 0.0f;
        const bool moving = std::abs(target - extent) > 0.002f;

        if (moving)
        {
            extent += (target - extent) * 0.30f;
            repaint();
        }
        else if (extent != target)
        {
            extent = target;
            if (onExtentChanged)
                onExtentChanged();
        }
        else if (!open)
        {
            stopTimer();
            return;
        }

        // Only tells the window to relayout while the panel is actually
        // moving. Doing it every frame made the host lay out the plugin editor
        // thirty times a second, which is what made this feel slow.
        if (moving && onExtentChanged)
            onExtentChanged();

        if (open)
        {
            refresh();
            repaint();
        }
    }

    void refresh()
    {
        tap.readLatest(scope.data(), scopeFrames);

        std::fill(fftData.begin(), fftData.end(), 0.0f);
        tap.readLatest(fftData.data(), fftSize);
        window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        // Slew the display down slowly so peaks stay readable, and up fast so
        // nothing is missed.
        for (size_t i = 0; i < magnitudes.size(); ++i)
        {
            const float db = juce::Decibels::gainToDecibels(fftData[i] / (float) (fftSize / 4), -100.0f);
            magnitudes[i] = db > magnitudes[i] ? db : magnitudes[i] * 0.88f + db * 0.12f;
        }
    }

    void drawScope(juce::Graphics& g, juce::Rectangle<int> area)
    {
        label(g, area, "WAVEFORM");
        auto plot = area.withTrimmedTop(18);

        g.setColour(colHair);
        g.drawRect(plot, 1);
        g.drawLine((float) plot.getX(), (float) plot.getCentreY(), (float) plot.getRight(),
                   (float) plot.getCentreY(), 1.0f);

        // Min and max per column rather than one sample per column: at these
        // widths a single sample misses most of the waveform and draws a
        // sparse, aliased line instead of the shape that is actually there.
        const float h = plot.getHeight() * 0.44f;
        const int w = juce::jmax(1, plot.getWidth());

        juce::Path body;
        body.startNewSubPath((float) plot.getX(), (float) plot.getCentreY());

        juce::Array<float> tops, bottoms;
        tops.ensureStorageAllocated(w);
        bottoms.ensureStorageAllocated(w);

        for (int x = 0; x < w; ++x)
        {
            const int from = x * scopeFrames / w;
            const int to = juce::jmax(from + 1, (x + 1) * scopeFrames / w);

            float lo = 1.0f, hi = -1.0f;
            for (int i = from; i < to && i < scopeFrames; ++i)
            {
                lo = juce::jmin(lo, scope[(size_t) i]);
                hi = juce::jmax(hi, scope[(size_t) i]);
            }

            tops.add(plot.getCentreY() - juce::jlimit(-1.0f, 1.0f, hi) * h);
            bottoms.add(plot.getCentreY() - juce::jlimit(-1.0f, 1.0f, lo) * h);
        }

        for (int x = 0; x < w; ++x)
            body.lineTo((float) (plot.getX() + x), tops[x]);
        for (int x = w - 1; x >= 0; --x)
            body.lineTo((float) (plot.getX() + x), bottoms[x]);
        body.closeSubPath();

        g.setColour(colInk.withAlpha(0.16f));
        g.fillPath(body);
        g.setColour(colInk);
        g.strokePath(body, juce::PathStrokeType(1.0f));
    }

    void drawSpectrum(juce::Graphics& g, juce::Rectangle<int> area)
    {
        label(g, area, "SPECTRUM");
        auto plot = area.withTrimmedTop(18);

        g.setColour(colHair);
        g.drawRect(plot, 1);

        const double rate = tap.getSampleRate();

        // Decade gridlines, because a spectrum on a linear axis hides
        // everything that matters below a few kilohertz.
        for (const double f : {100.0, 1000.0, 10000.0})
        {
            const float x = freqToX(f, rate, plot);
            g.setColour(colHair);
            g.drawVerticalLine(juce::roundToInt(x), (float) plot.getY(), (float) plot.getBottom());
            g.setColour(colMute);
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : juce::String(f, 0),
                       juce::roundToInt(x) + 3, plot.getBottom() - 14, 40, 12, juce::Justification::left);
        }

        // Horizontal dB gridlines, so the trace can be read as a level and
        // not only as a shape.
        for (const int db : {-20, -40, -60, -80})
        {
            const float y =
                juce::jmap((float) db, -90.0f, 0.0f, (float) plot.getBottom(), (float) plot.getY());
            g.setColour(colHair.withAlpha(0.6f));
            g.drawHorizontalLine(juce::roundToInt(y), (float) plot.getX(), (float) plot.getRight());
            g.setColour(colMute);
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.drawText(juce::String(db), plot.getX() + 3, juce::roundToInt(y) - 11, 30, 11,
                       juce::Justification::left);
        }

        juce::Path p;
        bool started = false;
        for (int x = 0; x < plot.getWidth(); ++x)
        {
            const double f = xToFreq((float) x / (float) juce::jmax(1, plot.getWidth()), rate);
            const int bin = juce::jlimit(0, (int) magnitudes.size() - 1, (int) (f * fftSize / rate));
            const float db = magnitudes[(size_t) bin];
            const float y = juce::jmap(juce::jlimit(-90.0f, 0.0f, db), -90.0f, 0.0f, (float) plot.getBottom(),
                                       (float) plot.getY());

            if (!started)
            {
                p.startNewSubPath((float) plot.getX(), y);
                started = true;
            }
            else
            {
                p.lineTo((float) (plot.getX() + x), y);
            }
        }

        // Filled under the curve: a spectrum reads as a mass of energy rather
        // than as a wire, and the fill also hides the noise floor's jitter.
        juce::Path filled(p);
        filled.lineTo((float) plot.getRight(), (float) plot.getBottom());
        filled.lineTo((float) plot.getX(), (float) plot.getBottom());
        filled.closeSubPath();

        g.setColour(colInk.withAlpha(0.14f));
        g.fillPath(filled);
        g.setColour(colInk);
        g.strokePath(p, juce::PathStrokeType(1.2f));
    }

    void label(juce::Graphics& g, juce::Rectangle<int> area, const char* text)
    {
        g.setColour(colMute);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(text, area.getX(), area.getY(), 160, 14, juce::Justification::left);
    }

    static float freqToX(double f, double rate, juce::Rectangle<int> plot)
    {
        const double lo = std::log10(20.0);
        const double hi = std::log10(juce::jmax(1000.0, rate * 0.5));
        const double t = (std::log10(juce::jmax(20.0, f)) - lo) / (hi - lo);
        return (float) (plot.getX() + t * plot.getWidth());
    }

    static double xToFreq(float t, double rate)
    {
        const double lo = std::log10(20.0);
        const double hi = std::log10(juce::jmax(1000.0, rate * 0.5));
        return std::pow(10.0, lo + (double) t * (hi - lo));
    }

    const AnalyserTap& tap;
    juce::Colour colBase, colInk, colMute, colHair;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> scope, fftData, magnitudes;
    bool open = false;
    float extent = 0.0f;
};

} // namespace plugshell
