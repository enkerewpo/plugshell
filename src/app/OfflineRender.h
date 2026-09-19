// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace plugshell
{

/**
    Renders a plugin to a file, faster than real time and without a device.

    This is the operation everything else in the roadmap is waiting on. An
    agent adjusting a sound needs to hear the result of what it just did, and
    "hear" for a program means a file it can measure. Every published approach
    to recovering synthesizer parameters from audio needs the same thing in
    bulk -- a parameter vector in, audio out, thousands of times -- and the
    reason that research is almost all done on one open-source FM synth is
    that for commercial plugins this loop has not existed.

    It also answers the question a person actually has about a control, which
    is not what number it holds but what it does to the sound. That is
    measurable: move it, render, compare.

    Deliberately renders the plugin as it stands rather than a fresh copy, so
    what comes out is what is on screen, including every control that is not a
    parameter and could not have been set any other way.
*/
class OfflineRender
{
public:
    struct Options
    {
        double sampleRate = 48000.0;
        int blockSize = 512;
        double durationSec = 2.0;

        /** MIDI note to play, or -1 to render without playing anything, which
            is what an effect wants. */
        int note = 60;
        float velocity = 0.8f;

        /** When to release the note. Sustain and release are most of what
            distinguishes one patch from another, so the tail after this point
            is not padding -- it is half the sound. */
        double noteOffSec = 1.0;

        /** Audio to feed in, for effects. Instruments leave it empty. */
        juce::File input;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        int channels = 0;
        juce::int64 frames = 0;
        double peak = 0.0;
        double rms = 0.0;
        double renderedInSec = 0.0;
    };

    /** @param prepareAgain  restores the plugin to this rate and block size
                             afterwards, which is what the live device needs. */
    static Result run(juce::AudioProcessor& plugin, const juce::File& destination, const Options& options,
                      double restoreSampleRate, int restoreBlockSize)
    {
        Result result;

        if (options.durationSec <= 0.0 || options.durationSec > 120.0)
        {
            result.error = "durationSec must be between 0 and 120";
            return result;
        }

        const auto started = juce::Time::getMillisecondCounterHiRes();

        const int channels =
            juce::jmax(1, juce::jmax(plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()));
        const auto totalFrames = (juce::int64) (options.sampleRate * options.durationSec);
        const int blockSize = juce::jlimit(16, 8192, options.blockSize);

        std::unique_ptr<juce::AudioFormatReader> reader;
        if (options.input.existsAsFile())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            reader.reset(formats.createReaderFor(options.input));

            if (reader == nullptr)
            {
                result.error = "could not read " + options.input.getFullPathName();
                return result;
            }
        }

        plugin.releaseResources();
        plugin.setNonRealtime(true);
        plugin.prepareToPlay(options.sampleRate, blockSize);

        // Restoring the live configuration matters even when the render fails
        // partway, or the application is left with a plugin prepared for a
        // rate the device is not running at.
        const auto restore = [&]
        {
            plugin.releaseResources();
            plugin.setNonRealtime(false);
            plugin.prepareToPlay(restoreSampleRate, restoreBlockSize);
        };

        juce::AudioBuffer<float> output(channels, (int) totalFrames);
        output.clear();

        juce::AudioBuffer<float> block(channels, blockSize);
        juce::MidiBuffer midi;

        const auto noteOnAt = (juce::int64) 0;
        const auto noteOffAt = (juce::int64) (options.noteOffSec * options.sampleRate);

        for (juce::int64 pos = 0; pos < totalFrames;)
        {
            const int thisBlock = (int) juce::jmin((juce::int64) blockSize, totalFrames - pos);

            block.setSize(channels, thisBlock, false, false, true);
            block.clear();

            if (reader != nullptr)
                reader->read(&block, 0, thisBlock, pos, true, true);

            midi.clear();

            if (options.note >= 0)
            {
                if (pos <= noteOnAt && noteOnAt < pos + thisBlock)
                    midi.addEvent(juce::MidiMessage::noteOn(1, options.note, options.velocity),
                                  (int) (noteOnAt - pos));

                if (pos <= noteOffAt && noteOffAt < pos + thisBlock)
                    midi.addEvent(juce::MidiMessage::noteOff(1, options.note), (int) (noteOffAt - pos));
            }

            plugin.processBlock(block, midi);

            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, (int) pos, block, ch, 0, thisBlock);

            pos += thisBlock;
        }

        restore();

        // Reported so a caller can tell silence from a render that worked,
        // which is the failure that otherwise looks exactly like success: an
        // instrument that was never sent a note, or an effect with no input,
        // writes a perfectly valid file of nothing.
        result.peak = (double) output.getMagnitude(0, (int) totalFrames);
        result.rms = (double) output.getRMSLevel(0, 0, (int) totalFrames);

        destination.deleteFile();
        destination.getParentDirectory().createDirectory();

        std::unique_ptr<juce::FileOutputStream> stream(destination.createOutputStream());
        if (stream == nullptr)
        {
            result.error = "could not write " + destination.getFullPathName();
            return result;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), options.sampleRate, (unsigned int) channels, 24, {}, 0));

        if (writer == nullptr)
        {
            result.error = "could not create a WAV writer";
            return result;
        }

        stream.release(); // the writer owns it now
        writer->writeFromAudioSampleBuffer(output, 0, (int) totalFrames);
        writer.reset();

        result.ok = true;
        result.channels = channels;
        result.frames = totalFrames;
        result.renderedInSec = (juce::Time::getMillisecondCounterHiRes() - started) * 0.001;
        return result;
    }
};

} // namespace plugshell
