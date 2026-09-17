// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cmath>
#include <vector>

#include "HostPlayHead.h"

namespace plugshell
{

/**
    The last thing between the plugin and the device.

    Three jobs, in this order: advance the playhead so the plugin reads a
    position describing the block it is about to render, apply the master
    gain, and keep a copy of what left for the analysers to draw.

    The gain is applied here rather than anywhere else because here is the
    only point that sees every sample on its way out, whatever the plugin did
    with its channel layout. Metering it afterwards means the numbers describe
    what the speakers were sent, which is the question a master meter answers.

    The audio thread must not allocate, lock, or wait, so the buffer is sized
    once up front and the writer publishes its position with a single atomic
    store. A reader may see a partially overwritten window when the drawing
    thread is slow; for a display redrawn thirty times a second, a momentarily
    torn frame is a better trade than making the audio thread wait for a
    painter.
*/
class AnalyserTap : public juce::AudioIODeviceCallback
{
public:
    static constexpr int capacity = 1 << 15; // 32768 frames, about 0.7 s at 48 kHz

    /** Integration time for the RMS bars.

        300 ms is what the broadcast meters standardised on, and the reason is
        that it is roughly how long the ear takes to settle on a loudness. A
        faster meter shows the waveform; a slower one shows the mix. */
    static constexpr double rmsSeconds = 0.300;

    /** How long a peak takes to fall by 1/e. Slow enough that a transient is
        still on screen when you look up from the keyboard. */
    static constexpr double peakFallSeconds = 1.700;

    /** How long the clip indication stays lit after the last sample over
        full scale -- long enough to be seen, short enough to be believed when
        it goes out. */
    static constexpr double clipHoldSeconds = 1.500;

    explicit AnalyserTap(juce::AudioIODeviceCallback& next) : inner(next)
    {
        left.resize((size_t) capacity, 0.0f);
        right.resize((size_t) capacity, 0.0f);
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int numInput, float* const* output,
                                          int numOutput, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& ctx) override
    {
        // Advanced before the plugin runs, not after, so what it reads
        // describes the block it is about to render. This sits here because
        // this is the one object that sees every block before the processor
        // does.
        if (playHead != nullptr)
            playHead->advance(numSamples, sampleRate);

        inner.audioDeviceIOCallbackWithContext(input, numInput, output, numOutput, numSamples, ctx);

        if (numOutput <= 0 || output == nullptr || numSamples <= 0)
            return;

        applyGain(output, numOutput, numSamples);
        measure(output, numOutput, numSamples);
        record(output, numOutput, numSamples);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override
    {
        sampleRate = d != nullptr ? d->getCurrentSampleRate() : 48000.0;
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        // Snap rather than ramp: there is no previous block to ramp from, and
        // starting from zero would fade the first buffer in from silence.
        appliedGain = gainTarget.load(std::memory_order_relaxed);

        meanSquare[0] = meanSquare[1] = 0.0f;
        heldPeak[0] = heldPeak[1] = 0.0f;
        clipCountdown = 0;
        publish();

        inner.audioDeviceAboutToStart(d);
    }

    void audioDeviceStopped() override
    {
        // Otherwise the bars keep their last reading for as long as the device
        // is away, which reads as signal.
        meanSquare[0] = meanSquare[1] = 0.0f;
        heldPeak[0] = heldPeak[1] = 0.0f;
        clipCountdown = 0;
        publish();

        inner.audioDeviceStopped();
    }

    /** The most recent @p count frames summed to mono, oldest first. */
    void readLatest(float* dest, int count) const
    {
        const int w = writePos.load(std::memory_order_acquire);
        int r = (w - count) & (capacity - 1);

        for (int i = 0; i < count; ++i)
        {
            dest[i] = 0.5f * (left[(size_t) r] + right[(size_t) r]);
            r = (r + 1) & (capacity - 1);
        }
    }

    /** The same window, both channels, for anything that needs the difference
        between them. */
    void readLatestStereo(float* destL, float* destR, int count) const
    {
        const int w = writePos.load(std::memory_order_acquire);
        int r = (w - count) & (capacity - 1);

        for (int i = 0; i < count; ++i)
        {
            destL[i] = left[(size_t) r];
            destR[i] = right[(size_t) r];
            r = (r + 1) & (capacity - 1);
        }
    }

    double getSampleRate() const { return sampleRate; }

    void setPlayHead(HostPlayHead* p) { playHead = p; }

    // ---------------------------------------------------------- master gain

    /** Linear, not decibels. Ramped across one block so that a control being
        dragged does not step the waveform, which is audible as a click. */
    void setGain(float linear) { gainTarget.store(linear, std::memory_order_relaxed); }

    float getGain() const { return gainTarget.load(std::memory_order_relaxed); }

    // --------------------------------------------------------------- levels

    /** What the meter draws. One struct because the four numbers are only
        meaningful together, and reading them one at a time invites a frame
        that mixes two different moments. */
    struct Levels
    {
        float rms[2]{0.0f, 0.0f};
        float peak[2]{0.0f, 0.0f};
        bool clipped = false;
    };

    Levels getLevels() const
    {
        Levels l;
        l.rms[0] = outRms[0].load(std::memory_order_relaxed);
        l.rms[1] = outRms[1].load(std::memory_order_relaxed);
        l.peak[0] = outPeak[0].load(std::memory_order_relaxed);
        l.peak[1] = outPeak[1].load(std::memory_order_relaxed);
        l.clipped = outClipped.load(std::memory_order_relaxed);
        return l;
    }

private:
    void applyGain(float* const* output, int numOutput, int numSamples)
    {
        const float target = gainTarget.load(std::memory_order_relaxed);

        if (target == appliedGain && target == 1.0f)
            return; // unity and staying there: nothing to multiply by

        const float step = (target - appliedGain) / (float) numSamples;

        for (int ch = 0; ch < numOutput; ++ch)
        {
            if (output[ch] == nullptr)
                continue;

            float g = appliedGain;
            for (int i = 0; i < numSamples; ++i)
            {
                g += step;
                output[ch][i] *= g;
            }
        }

        appliedGain = target;
    }

    void measure(const float* const* output, int numOutput, int numSamples)
    {
        // The coefficients depend on how long this block was, not on how many
        // blocks have gone by, so a device that changes its buffer size does
        // not change how fast the meter moves.
        const float rise = sampleRate > 0.0
                               ? (float) (1.0 - std::exp(-(double) numSamples / (rmsSeconds * sampleRate)))
                               : 1.0f;
        const float fall =
            sampleRate > 0.0 ? (float) std::exp(-(double) numSamples / (peakFallSeconds * sampleRate)) : 0.0f;

        bool over = false;

        for (int ch = 0; ch < 2; ++ch)
        {
            // Mono output is shown on both bars rather than leaving one dead.
            const float* src = output[ch < numOutput ? ch : 0];

            if (src == nullptr)
                continue;

            double sum = 0.0;
            float peak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float v = src[i];
                sum += (double) v * (double) v;
                peak = juce::jmax(peak, std::abs(v));
            }

            const float blockMeanSquare = (float) (sum / (double) numSamples);

            // Rising and falling at the same rate, because an RMS meter that
            // is fast in one direction is a peak meter wearing its badge.
            meanSquare[ch] += rise * (blockMeanSquare - meanSquare[ch]);

            // Peaks attack instantly and decay slowly: missing a transient is
            // the one error a peak meter may not make.
            heldPeak[ch] = juce::jmax(peak, heldPeak[ch] * fall);

            over = over || peak >= 1.0f;
        }

        if (over)
            clipCountdown = (int) (clipHoldSeconds * sampleRate);
        else
            clipCountdown = juce::jmax(0, clipCountdown - numSamples);

        publish();
    }

    void record(const float* const* output, int numOutput, int numSamples)
    {
        int w = writePos.load(std::memory_order_relaxed);

        // Channels kept apart rather than summed. The waveform and the
        // spectrum want the sum, but where a sound sits between the speakers
        // is only visible in the difference between the two, and that cannot
        // be recovered once they have been added together.
        const float* l = output[0];
        const float* r = numOutput > 1 ? output[1] : output[0];

        for (int i = 0; i < numSamples; ++i)
        {
            left[(size_t) w] = l[i];
            right[(size_t) w] = r[i];
            w = (w + 1) & (capacity - 1);
        }

        writePos.store(w, std::memory_order_release);
    }

    void publish()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            outRms[ch].store(std::sqrt(meanSquare[ch]), std::memory_order_relaxed);
            outPeak[ch].store(heldPeak[ch], std::memory_order_relaxed);
        }

        outClipped.store(clipCountdown > 0, std::memory_order_relaxed);
    }

    juce::AudioIODeviceCallback& inner;
    HostPlayHead* playHead = nullptr;
    std::vector<float> left, right;
    std::atomic<int> writePos{0};
    double sampleRate = 48000.0;

    std::atomic<float> gainTarget{1.0f};
    float appliedGain = 1.0f; ///< audio thread only

    float meanSquare[2]{0.0f, 0.0f}; ///< audio thread only
    float heldPeak[2]{0.0f, 0.0f};   ///< audio thread only
    int clipCountdown = 0;           ///< audio thread only, in samples

    std::atomic<float> outRms[2]{{0.0f}, {0.0f}};
    std::atomic<float> outPeak[2]{{0.0f}, {0.0f}};
    std::atomic<bool> outClipped{false};
};

} // namespace plugshell
