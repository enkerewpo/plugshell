// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <vector>

#include "HostPlayHead.h"

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

        if (numOutput <= 0 || output == nullptr)
            return;

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

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override
    {
        sampleRate = d != nullptr ? d->getCurrentSampleRate() : 48000.0;
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        inner.audioDeviceAboutToStart(d);
    }

    void audioDeviceStopped() override { inner.audioDeviceStopped(); }

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

private:
    juce::AudioIODeviceCallback& inner;
    HostPlayHead* playHead = nullptr;
    std::vector<float> left, right;
    std::atomic<int> writePos{0};
    double sampleRate = 48000.0;
};

} // namespace plugshell
