// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

namespace plugshell
{

/**
    Copies the output signal so it can be looked at.

    Sits between the device and the player, passing everything through and
    keeping a copy of what went past. The copy is what the scope and the
    analyser draw.

    The audio thread must not allocate, lock, or wait, so the buffer is sized
    once up front and the writer publishes its position with a single atomic
    store. A reader may see a partially overwritten window when the drawing
    thread is slow; for a display that is redrawn thirty times a second, a
    momentarily torn frame is a better trade than making the audio thread wait
    for a painter.
*/
class AnalyserTap : public juce::AudioIODeviceCallback
{
public:
    static constexpr int capacity = 1 << 15; // 32768 frames, about 0.7 s at 48 kHz

    explicit AnalyserTap(juce::AudioIODeviceCallback& next) : inner(next)
    {
        buffer.resize((size_t) capacity, 0.0f);
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int numInput, float* const* output,
                                          int numOutput, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& ctx) override
    {
        inner.audioDeviceIOCallbackWithContext(input, numInput, output, numOutput, numSamples, ctx);

        if (numOutput <= 0 || output == nullptr)
            return;

        int w = writePos.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            // Mono sum: the scope shows what is there, not where it is.
            float sum = 0.0f;
            for (int ch = 0; ch < numOutput; ++ch)
                sum += output[ch][i];

            buffer[(size_t) w] = sum / (float) numOutput;
            w = (w + 1) & (capacity - 1);
        }

        writePos.store(w, std::memory_order_release);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override
    {
        sampleRate = d != nullptr ? d->getCurrentSampleRate() : 48000.0;
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        inner.audioDeviceAboutToStart(d);
    }

    void audioDeviceStopped() override { inner.audioDeviceStopped(); }

    /** Copies the most recent @p count frames, oldest first. */
    void readLatest(float* dest, int count) const
    {
        const int w = writePos.load(std::memory_order_acquire);
        int r = (w - count) & (capacity - 1);

        for (int i = 0; i < count; ++i)
        {
            dest[i] = buffer[(size_t) r];
            r = (r + 1) & (capacity - 1);
        }
    }

    double getSampleRate() const { return sampleRate; }

private:
    juce::AudioIODeviceCallback& inner;
    std::vector<float> buffer;
    std::atomic<int> writePos{0};
    double sampleRate = 48000.0;
};

} // namespace plugshell
