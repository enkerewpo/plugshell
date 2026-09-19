// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

#include "LinkMapping.h"

namespace plugshell::link
{

/**
    MIDI for one block, as bytes.

    A length-prefixed run of records, each one a sample offset and a message.
    Deliberately not a JUCE type: everything in this directory has to compile
    on both sides of the process boundary and be testable without an audio
    device, and a buffer of bytes is the widest agreement two processes can
    reach about anything.
*/
struct MidiWriter
{
    unsigned char* data;
    int capacity;
    int used = 0;

    bool add(int samplePosition, const unsigned char* bytes, int numBytes) noexcept
    {
        const int needed = 8 + numBytes;

        if (used + needed > capacity)
            return false;

        std::memcpy(data + used, &samplePosition, 4);
        std::memcpy(data + used + 4, &numBytes, 4);
        std::memcpy(data + used + 8, bytes, (size_t) numBytes);
        used += needed;
        return true;
    }
};

struct MidiReader
{
    const unsigned char* data;
    int size;
    int position = 0;

    bool next(int& samplePosition, const unsigned char*& bytes, int& numBytes) noexcept
    {
        if (position + 8 > size)
            return false;

        std::memcpy(&samplePosition, data + position, 4);
        std::memcpy(&numBytes, data + position + 4, 4);

        if (numBytes < 0 || position + 8 + numBytes > size)
            return false;

        bytes = data + position + 8;
        position += 8 + numBytes;
        return true;
    }
};

/**
    The host's end of the link. Every method here except attach() and detach()
    is called from the audio thread.
*/
class HostLink
{
public:
    void attach(Mapping&& m, Wakeup&& w)
    {
        mapping = std::move(m);
        wakeup = std::move(w);
        submitted = 0;
    }

    void detach()
    {
        mapping = {};
        wakeup = {};
    }

    bool isAttached() const noexcept { return mapping.isValid(); }

    Shared* shared() const noexcept { return mapping.get(); }

    /** @return true when the child has said it is prepared and processing. */
    bool isReady() const noexcept
    {
        return mapping.isValid() &&
               mapping->childState.load(std::memory_order_acquire) == (std::uint32_t) ChildState::ready;
    }

    /**
        Hands one block over and takes back the previous one.

        The output written here is what the child produced for the previous
        call, which is the whole bargain: one block of latency in exchange for
        never waiting. When the child has not kept up the output is silence and
        the block is not submitted at all -- submitting it would write into the
        slot the child is still reading, and a glitch that corrupts the next
        block as well is two glitches.

        @return true if the block was handed over.
    */
    bool exchange(const float* const* input, int numInputChannels, float* const* output,
                  int numOutputChannels, int numSamples, const unsigned char* midi, int midiSize) noexcept
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (output[ch] != nullptr)
                std::fill_n(output[ch], numSamples, 0.0f);

        if (!isReady() || numSamples <= 0 || numSamples > maxBlock)
            return false;

        auto& s = *mapping;
        const auto n = submitted;

        // The child is a whole block behind, which means it is still reading
        // the slot this block would go in.
        if (n >= 1 && s.responseSeq.load(std::memory_order_acquire) + 1 < n)
        {
            s.underruns.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Take the previous block's output before overwriting anything, so a
        // child that is exactly on time is not raced with.
        if (n >= 1 && s.responseSeq.load(std::memory_order_acquire) >= n)
        {
            const int slot = (int) ((n - 1) & 1);
            const int frames = std::min<int>((int) s.frames[slot], numSamples);
            const int channels = std::min(numOutputChannels, (int) s.outputChannels);

            for (int ch = 0; ch < channels; ++ch)
                if (output[ch] != nullptr)
                    std::copy_n(s.output[slot][ch], frames, output[ch]);
        }
        else if (n >= 1)
        {
            s.underruns.fetch_add(1, std::memory_order_relaxed);
        }

        const int slot = (int) (n & 1);
        const int inChannels = std::min(numInputChannels, (int) s.inputChannels);

        for (int ch = 0; ch < inChannels; ++ch)
            if (input != nullptr && input[ch] != nullptr)
                std::copy_n(input[ch], numSamples, s.input[slot][ch]);
            else
                std::fill_n(s.input[slot][ch], numSamples, 0.0f);

        for (int ch = inChannels; ch < (int) s.inputChannels; ++ch)
            std::fill_n(s.input[slot][ch], numSamples, 0.0f);

        const int bytes = std::min(midiSize, midiCapacity);
        if (bytes > 0 && midi != nullptr)
            std::memcpy(s.midi[slot], midi, (size_t) bytes);

        s.midiBytes[slot] = (std::uint32_t) std::max(0, bytes);
        s.frames[slot] = (std::uint32_t) numSamples;

        submitted = n + 1;
        s.requestSeq.store(submitted, std::memory_order_release);
        wakeup.signal();

        return true;
    }

    /**
        The same exchange, but waits for this block's own answer.

        Only for offline work, and only from a thread that is allowed to wait
        -- which the audio thread is not, ever. Rendering faster than real time
        has no device deadline to miss and no previous block to return, so
        waiting is both permissible and the only way to get the block that was
        actually asked for.

        @param timeoutMs  gives up rather than hanging if the child has died
                          between the submit and the answer.
        @return true if the child answered.
    */
    bool exchangeBlocking(const float* const* input, int numInputChannels, float* const* output,
                          int numOutputChannels, int numSamples, const unsigned char* midi, int midiSize,
                          int timeoutMs)
    {
        if (!isReady() || numSamples <= 0 || numSamples > maxBlock)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (output[ch] != nullptr)
                    std::fill_n(output[ch], numSamples, 0.0f);

            return false;
        }

        auto& s = *mapping;
        const auto n = submitted;
        const int slot = (int) (n & 1);
        const int inChannels = std::min(numInputChannels, (int) s.inputChannels);

        for (int ch = 0; ch < (int) s.inputChannels; ++ch)
            if (ch < inChannels && input != nullptr && input[ch] != nullptr)
                std::copy_n(input[ch], numSamples, s.input[slot][ch]);
            else
                std::fill_n(s.input[slot][ch], numSamples, 0.0f);

        const int bytes = std::min(midiSize, midiCapacity);
        if (bytes > 0 && midi != nullptr)
            std::memcpy(s.midi[slot], midi, (size_t) bytes);

        s.midiBytes[slot] = (std::uint32_t) std::max(0, bytes);
        s.frames[slot] = (std::uint32_t) numSamples;

        submitted = n + 1;
        s.requestSeq.store(submitted, std::memory_order_release);
        wakeup.signal();

        for (int waited = 0; waited < timeoutMs * 20; ++waited)
        {
            if (s.responseSeq.load(std::memory_order_acquire) >= submitted)
            {
                const int channels = std::min(numOutputChannels, (int) s.outputChannels);

                for (int ch = 0; ch < numOutputChannels; ++ch)
                    if (output[ch] != nullptr)
                        (ch < channels) ? std::copy_n(s.output[slot][ch], numSamples, output[ch])
                                        : std::fill_n(output[ch], numSamples, 0.0f);

                return true;
            }

            ::usleep(50);
        }

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (output[ch] != nullptr)
                std::fill_n(output[ch], numSamples, 0.0f);

        return false;
    }

    bool sendParameter(int index, float value) noexcept
    {
        return mapping.isValid() && mapping->toChild.push((std::uint32_t) index, value);
    }

    bool receiveParameter(ParamChange& out) noexcept { return mapping.isValid() && mapping->toHost.pop(out); }

    std::uint32_t underruns() const noexcept
    {
        return mapping.isValid() ? mapping->underruns.load(std::memory_order_relaxed) : 0;
    }

    std::uint64_t heartbeat() const noexcept
    {
        return mapping.isValid() ? mapping->childHeartbeat.load(std::memory_order_relaxed) : 0;
    }

    /** Nothing more is coming. The child's read returns zero and it stops. */
    void closeWakeup() { wakeup.closeWriteEnd(); }

private:
    Mapping mapping;
    Wakeup wakeup;
    std::uint64_t submitted = 0;
};

/**
    The child's end. Its only thread of control is the worker running pump(),
    which blocks until the host has something and exits when the host does not.
*/
class ChildLink
{
public:
    bool attach(int sharedFd, int wakeupFd, std::string& error)
    {
        mapping = Mapping::fromDescriptor(sharedFd, error);

        if (!mapping.isValid())
            return false;

        if (!looksValid(*mapping))
            return (error = "the shared region is not one of ours, or is a different version", false);

        wakeup = Wakeup::adoptReadEnd(wakeupFd);
        return true;
    }

    Shared& shared() const noexcept { return *mapping; }

    void announceReady() noexcept
    {
        mapping->childState.store((std::uint32_t) ChildState::ready, std::memory_order_release);
    }

    void announceFault() noexcept
    {
        mapping->childState.store((std::uint32_t) ChildState::faulted, std::memory_order_release);
    }

    /** One block. @p process is handed the slot's buffers in place. */
    using Process = std::function<void(int slot, int numSamples, const unsigned char* midi, int midiSize)>;

    /** Blocks until the host wakes it, answers everything outstanding, and
        repeats. Returns when the host's end of the pipe closes, which is the
        only exit that is not an error -- including when the host closed it by
        dying. */
    void pump(const Process& process)
    {
        auto& s = *mapping;

        while (wakeup.wait())
        {
            const auto wanted = s.requestSeq.load(std::memory_order_acquire);

            // Behind by more than one: the slots the host has since reused are
            // gone, and pretending otherwise would play a block of whatever
            // happens to be in memory. Skip to the newest and stay one behind,
            // which is what the latency budget already assumes.
            if (wanted > answered + 1)
                answered = wanted - 1;

            while (answered < wanted)
            {
                const int slot = (int) (answered & 1);

                process(slot, (int) s.frames[slot], s.midi[slot], (int) s.midiBytes[slot]);

                ++answered;
                s.responseSeq.store(answered, std::memory_order_release);
                s.childHeartbeat.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

private:
    Mapping mapping;
    Wakeup wakeup;
    std::uint64_t answered = 0;
};

} // namespace plugshell::link
