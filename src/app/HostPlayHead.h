// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace plugshell
{

/**
    Tempo, metre and transport position, for the plugins that ask.

    Without one of these a plugin's getPlayHead() returns nothing, and
    everything that syncs to a tempo has to guess: a delay set to a dotted
    eighth has no idea how long that is, an arpeggiator has nothing to run
    against, an LFO set to 1/4 free-runs. Several plugins assume 120 and carry
    on, which is worse than failing, because the sound is wrong in a way that
    looks deliberate.

    Every value is an atomic because the audio thread reads them while the
    message thread writes them, and neither may wait for the other. They are
    read one at a time rather than as a set, which means a block can in
    principle see a new tempo with an old metre; for controls a person turns,
    that is a difference no listener can hear, and the alternative is a lock on
    the audio thread.
*/
class HostPlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;

        info.setBpm(bpm.load(std::memory_order_relaxed));
        info.setTimeSignature(TimeSignature{numerator.load(std::memory_order_relaxed),
                                            denominator.load(std::memory_order_relaxed)});

        const auto samples = samplePosition.load(std::memory_order_relaxed);
        const auto rate = sampleRate.load(std::memory_order_relaxed);

        info.setTimeInSamples(samples);
        info.setTimeInSeconds(rate > 0.0 ? (double) samples / rate : 0.0);
        info.setPpqPosition(ppqPosition.load(std::memory_order_relaxed));
        info.setIsPlaying(playing.load(std::memory_order_relaxed));
        info.setIsRecording(false);
        info.setIsLooping(false);

        return info;
    }

    /** Called from the audio thread, before the plugin processes the block, so
        that what it reads describes the block it is about to render rather
        than the one before. */
    void advance(int numSamples, double rate)
    {
        sampleRate.store(rate, std::memory_order_relaxed);

        if (!playing.load(std::memory_order_relaxed) || rate <= 0.0)
            return;

        samplePosition.fetch_add(numSamples, std::memory_order_relaxed);

        const double beatsPerSecond = bpm.load(std::memory_order_relaxed) / 60.0;
        ppqPosition.store(ppqPosition.load(std::memory_order_relaxed) +
                              (double) numSamples / rate * beatsPerSecond,
                          std::memory_order_relaxed);
    }

    void setTempo(double beatsPerMinute)
    {
        bpm.store(juce::jlimit(20.0, 999.0, beatsPerMinute), std::memory_order_relaxed);
    }

    void setTimeSignature(int upper, int lower)
    {
        numerator.store(juce::jlimit(1, 32, upper), std::memory_order_relaxed);

        // Powers of two only, because that is what a denominator is.
        const int allowed[] = {1, 2, 4, 8, 16, 32};
        int nearest = 4;
        for (const int d : allowed)
            if (std::abs(d - lower) < std::abs(nearest - lower))
                nearest = d;

        denominator.store(nearest, std::memory_order_relaxed);
    }

    void setPlaying(bool shouldPlay)
    {
        if (!shouldPlay)
            return playing.store(false, std::memory_order_relaxed);

        playing.store(true, std::memory_order_relaxed);
    }

    /** Back to the top. Anything counting bars should see bar one. */
    void rewind()
    {
        samplePosition.store(0, std::memory_order_relaxed);
        ppqPosition.store(0.0, std::memory_order_relaxed);
    }

    double getTempo() const { return bpm.load(std::memory_order_relaxed); }
    int getNumerator() const { return numerator.load(std::memory_order_relaxed); }
    int getDenominator() const { return denominator.load(std::memory_order_relaxed); }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }
    double getPpq() const { return ppqPosition.load(std::memory_order_relaxed); }

private:
    std::atomic<double> bpm{120.0};
    std::atomic<int> numerator{4}, denominator{4};
    std::atomic<double> ppqPosition{0.0};
    std::atomic<juce::int64> samplePosition{0};
    std::atomic<double> sampleRate{48000.0};
    std::atomic<bool> playing{false};
};

} // namespace plugshell
