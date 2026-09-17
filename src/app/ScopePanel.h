// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

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
        stereoL.resize(scopeFrames);
        stereoR.resize(scopeFrames);
        fftData.resize((size_t) fftSize * 2);
        magnitudes.resize((size_t) fftSize / 2, -100.0f);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setOpen(bool shouldOpen)
    {
        open = shouldOpen;
        extent = detailed ? 1.0f : extent;
        startTimerHz(60); // runs until the slide settles; stops itself after
    }

    bool isOpen() const { return open; }

    /** 0 when closed, 1 when fully out. */
    float getExtent() const { return extent; }

    bool isAnimating() const { return std::abs(extent - (open ? 1.0f : 0.0f)) > 0.001f; }

    std::function<void()> onExtentChanged;

    /** Clicking the inline strip asks for the full-size view. */
    std::function<void()> onClick;

    /** The inline strip is deliberately small, because the window has to grow
        by whatever it takes and a tall panel pushes the strip off the bottom
        of the display on a laptop. Detail is a separate, larger view rather
        than a taller inline one. */
    void setColours(juce::Colour bg, juce::Colour ink, juce::Colour mute, juce::Colour hair)
    {
        colBase = bg;
        colInk = ink;
        colMute = mute;
        colHair = hair;
        repaint();
    }

    void setDetailed(bool d)
    {
        detailed = d;
        if (d)
            setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!detailed && onClick)
            onClick();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colBase);
        g.setColour(colHair);
        g.drawLine(0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

        auto r = getLocalBounds().reduced(detailed ? 22 : 16, detailed ? 18 : 9);

        // Two by two when four across would make each panel a tall sliver,
        // one row when it would not. Decided by proportion rather than by an
        // absolute height, which got it wrong the moment the window was a
        // different size: the panel that prompted this was 420 tall against a
        // threshold of "taller than 420".
        //
        // The stereo display is what makes it matter. It is a shape, and a
        // shape needs both dimensions; the traces survive a letterbox and it
        // does not.
        const bool grid = detailed && r.getWidth() < r.getHeight() * 4;
        const int gap = detailed ? 20 : 12;

        juce::Rectangle<int> cells[4];

        if (grid)
        {
            auto top = r.removeFromTop(r.getHeight() / 2 - gap / 2);
            r.removeFromTop(gap);

            cells[0] = top.removeFromLeft(top.getWidth() / 2 - gap / 2);
            top.removeFromLeft(gap);
            cells[1] = top;

            cells[2] = r.removeFromLeft(r.getWidth() / 2 - gap / 2);
            r.removeFromLeft(gap);
            cells[3] = r;
        }
        else
        {
            const int each = (r.getWidth() - gap * 3) / 4;
            for (auto& cell : cells)
            {
                cell = r.removeFromLeft(each);
                r.removeFromLeft(gap);
            }
        }

        drawScope(g, cells[0]);
        drawSpectrum(g, cells[1]);
        drawSpectrogram(g, cells[2]);
        drawStereo(g, cells[3]);
    }

private:
    void timerCallback() override
    {
        const float target = open ? 1.0f : 0.0f;
        const bool moving = std::abs(target - extent) > 0.002f;

        if (moving)
        {
            // Eased, so the movement decelerates instead of stopping dead.
            extent += (target - extent) * 0.30f;
            if (onExtentChanged)
                onExtentChanged();
        }
        else if (extent != target)
        {
            extent = target;
            if (onExtentChanged)
                onExtentChanged();
        }
        else if (!open)
        {
            stopTimer(); // closed and settled: nothing left to draw
            return;
        }

        if (open || moving)
        {
            refresh();
            repaint();
        }
    }

    void refresh()
    {
        tap.readLatest(scope.data(), scopeFrames);
        tap.readLatestStereo(stereoL.data(), stereoR.data(), scopeFrames);

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

        pushSpectrogramColumn();
    }

    /** History for the waterfall, newest last.

        Kept as decibel columns rather than as pixels so the display can be
        resized, or moved between the strip and the full view, without losing
        what has already gone past. */
    void pushSpectrogramColumn()
    {
        static constexpr int rows = 128;

        std::vector<float> column((size_t) rows);

        for (int y = 0; y < rows; ++y)
        {
            // Log frequency, so the octaves are evenly spaced and the bottom
            // four of them are not squeezed into three pixels.
            const double f = 20.0 * std::pow(1000.0, (double) y / (double) (rows - 1));
            const int bin = juce::jlimit(0, (int) magnitudes.size() - 1,
                                         (int) (f * fftSize / juce::jmax(1.0, tap.getSampleRate())));
            column[(size_t) y] = magnitudes[(size_t) bin];
        }

        history.push_back(std::move(column));

        while ((int) history.size() > historyLength)
            history.pop_front();
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
            const int w = juce::jmax(1, plot.getWidth());
            const double f = xToFreq((float) x / (float) w, rate);
            const double fNext = xToFreq((float) (x + 1) / (float) w, rate);

            // A log frequency axis spreads the low bins over many pixels and
            // crams the high ones into a fraction of a pixel, so one bin per
            // pixel gives a staircase at the bottom and aliasing at the top.
            // Below a bin per pixel, interpolate between neighbours; above it,
            // take the loudest bin in the span, which is what a peak reading
            // should do and is stable while the display slews.
            const double binF = f * fftSize / rate;
            const double binNext = fNext * fftSize / rate;
            const int last = (int) magnitudes.size() - 1;

            float db;

            if (binNext - binF < 1.0)
            {
                const int lo = juce::jlimit(0, last, (int) binF);
                const int hi = juce::jlimit(0, last, lo + 1);
                const float t = (float) (binF - (double) lo);
                db = magnitudes[(size_t) lo] * (1.0f - t) + magnitudes[(size_t) hi] * t;
            }
            else
            {
                db = -100.0f;
                for (int b = juce::jlimit(0, last, (int) binF); b <= juce::jlimit(0, last, (int) binNext);
                     ++b)
                    db = juce::jmax(db, magnitudes[(size_t) b]);
            }

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

    /** Frequency against time, loudness as brightness.

        The spectrum says what is sounding now; this says what has been. That
        is the difference that matters for anything with movement in it -- an
        envelope closing a filter, an LFO, a delay repeating -- because none of
        those exist in a single frame. */
    void drawSpectrogram(juce::Graphics& g, juce::Rectangle<int> area)
    {
        label(g, area, "SPECTROGRAM");
        auto plot = area.withTrimmedTop(18);

        g.setColour(colHair);
        g.drawRect(plot, 1);

        if (history.empty() || plot.getWidth() < 4 || plot.getHeight() < 4)
            return;

        const int columns = (int) history.size();
        const int rows = (int) history.front().size();

        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(plot);

        // One rectangle per cell would be tens of thousands of fills a frame.
        // The image is built once per paint and scaled up, which is also what
        // gives the display its smooth vertical gradient for free.
        juce::Image image(juce::Image::RGB, columns, rows, false);

        {
            juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);

            for (int x = 0; x < columns; ++x)
            {
                const auto& column = history[(size_t) x];

                for (int y = 0; y < rows; ++y)
                {
                    // Bottom of the panel is the lowest frequency.
                    const float db = column[(size_t) (rows - 1 - y)];
                    const float lit = juce::jlimit(0.0f, 1.0f, (db + 80.0f) / 80.0f);

                    pixels.setPixelColour(x, y, colBase.interpolatedWith(colInk, lit * lit));
                }
            }
        }

        g.drawImage(image, plot.toFloat(), juce::RectanglePlacement::stretchToFit);

        g.setColour(colHair);
        g.drawRect(plot, 1);
    }

    /** Where the sound sits between the speakers, as a polar sample display.

        Each sample pair becomes one point: its angle is where it is panned,
        its distance from the origin is how loud it is. Centred material stands
        straight up, a hard-panned sound lies along one edge, and a wide mix
        fans out to fill the arc.

        The other common drawing of the same data is the goniometer, which
        plots mid against side directly and fills a diamond. Both are correct;
        this one reads more directly because the axis you care about -- where
        the sound is -- is the one your eye follows around the curve.

        The number underneath is the correlation: one is mono, zero is
        uncorrelated, and below zero is the state that partly disappears when
        somebody plays it on a phone. */
    void drawStereo(juce::Graphics& g, juce::Rectangle<int> area)
    {
        label(g, area, "STEREO");
        auto plot = area.withTrimmedTop(18);

        // The frame is the whole plot, the same as the other three. Taking a
        // strip off the bottom for the number left this one panel shorter
        // than the one beside it, and four boxes that nearly line up look
        // worse than four that plainly do not.
        g.setColour(colHair);
        g.drawRect(plot, 1);

        auto field = plot.reduced(1).withTrimmedBottom(15);

        // Origin at the bottom centre, so the arc opens upwards into the panel
        // rather than being a full circle half of which is always empty.
        const auto origin = juce::Point<float>((float) field.getCentreX(), (float) field.getBottom() - 4.0f);
        const float radius = juce::jmin((float) field.getWidth() * 0.47f, (float) field.getHeight() - 8.0f);

        g.setColour(colHair.withAlpha(0.8f));
        {
            juce::Path arc;
            arc.addCentredArc(origin.x, origin.y, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi,
                              juce::MathConstants<float>::halfPi, true);
            g.strokePath(arc, juce::PathStrokeType(1.0f));
        }

        // L, centre and R, which is where the eye looks first.
        for (const float a : {-juce::MathConstants<float>::halfPi, 0.0f, juce::MathConstants<float>::halfPi})
        {
            g.setColour(colHair.withAlpha(a == 0.0f ? 0.9f : 0.6f));
            g.drawLine(origin.x, origin.y, origin.x + std::sin(a) * radius, origin.y - std::cos(a) * radius,
                       1.0f);
        }

        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        juce::Path dots;

        for (int i = 0; i < scopeFrames; i += 2)
        {
            const float l = stereoL[(size_t) i], r = stereoR[(size_t) i];

            sumLR += (double) l * r;
            sumLL += (double) l * l;
            sumRR += (double) r * r;

            const float magnitude = std::sqrt(l * l + r * r);
            if (magnitude < 1.0e-4f)
                continue;

            // atan2 gives a quarter turn between hard left and hard right;
            // doubling it opens that quarter out across the half circle, which
            // is what makes the display worth the space it takes.
            const float pan = (std::atan2(r, l) - juce::MathConstants<float>::pi * 0.25f) * 2.0f;
            const float reach = juce::jlimit(0.0f, 1.0f, magnitude) * radius;

            const float x = origin.x + std::sin(pan) * reach;
            const float y = origin.y - std::cos(pan) * reach;

            dots.addEllipse(x - 0.7f, y - 0.7f, 1.4f, 1.4f);
        }

        g.setColour(colInk.withAlpha(0.5f));
        g.fillPath(dots);

        const double denominator = std::sqrt(sumLL * sumRR);
        const double correlation = denominator > 1.0e-12 ? sumLR / denominator : 1.0;

        g.setColour(correlation < 0.0 ? juce::Colour{0xffd9a441} : colMute);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText("correlation " + juce::String(correlation, 2),
                   plot.reduced(4).removeFromBottom(13), juce::Justification::centred);
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
    bool detailed = false;
    static constexpr int historyLength = 220;

    std::vector<float> stereoL, stereoR;
    std::deque<std::vector<float>> history;

    juce::Colour colBase, colInk, colMute, colHair;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> scope, fftData, magnitudes;
    bool open = false;
    float extent = 0.0f;
};

} // namespace plugshell
