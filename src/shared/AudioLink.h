// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace plugshell::link
{

/**
    The audio channel between the host and a plugin running in another process.

    One region of shared memory and one pipe. The region holds two slots of
    input and output audio, the MIDI for each, and two rings of parameter
    changes; the pipe is how the host says a slot is ready, and -- because it
    closes when the host goes away -- how the child learns to stop.

    Nothing here allocates, locks, or makes a system call in the steady state
    except the one write and one read on the pipe, which is what the two
    processes are for. The invariant that makes it safe is the slot parity:
    while the child is working on slot N & 1 the host is filling slot
    (N + 1) & 1, so neither touches what the other is reading.

    One block of latency is the price, and it is a fixed price: the host reads
    the output the child produced for the previous callback rather than waiting
    for this one. Waiting is the thing an audio thread may not do, and a host
    that waits for a plugin in another process has given back the reason for
    putting it there.
*/

inline constexpr std::uint32_t magic = 0x50534c4bu; ///< "PSLK"
inline constexpr std::uint32_t version = 1;

/** Fixed rather than negotiated. The region costs half a megabyte at these
    limits, which is not worth the arithmetic -- and every offset computed at
    runtime is an offset that can be computed differently by the two sides. */
inline constexpr int maxChannels = 8;
inline constexpr int maxBlock = 4096;
inline constexpr int slots = 2;
inline constexpr int midiCapacity = 8192;
inline constexpr int paramCapacity = 2048;

struct ParamChange
{
    std::uint32_t index;
    float value;
};

/**
    A single-producer, single-consumer ring of parameter changes.

    Values rather than a snapshot, because the order matters: a plugin told to
    go to 0.2 and then to 0.8 must not be told only the last one if something
    downstream is watching the gesture. When it overflows the oldest change is
    the one lost, and the writer says so, because a caller that has overrun a
    ring of two thousand is doing something the ring cannot fix.
*/
struct ParamRing
{
    std::atomic<std::uint32_t> head{0}; ///< written by the producer
    std::atomic<std::uint32_t> tail{0}; ///< written by the consumer
    std::atomic<std::uint32_t> dropped{0};
    ParamChange items[paramCapacity]{};

    bool push(std::uint32_t index, float value) noexcept
    {
        const auto h = head.load(std::memory_order_relaxed);
        const auto next = (h + 1) % paramCapacity;

        if (next == tail.load(std::memory_order_acquire))
        {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        items[h].index = index;
        items[h].value = value;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(ParamChange& out) noexcept
    {
        const auto t = tail.load(std::memory_order_relaxed);

        if (t == head.load(std::memory_order_acquire))
            return false;

        out = items[t];
        tail.store((t + 1) % paramCapacity, std::memory_order_release);
        return true;
    }

    void reset() noexcept
    {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        dropped.store(0, std::memory_order_relaxed);
    }
};

/** What the child is doing, as far as the host can tell without asking. */
enum class ChildState : std::uint32_t
{
    starting = 0,
    ready = 1,
    faulted = 2 ///< the child caught something and chose to stop rather than lie
};

struct Shared
{
    // ------------------------------------------------ written once, by the host
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint32_t reserved;

    double sampleRate;
    std::uint32_t maxBlockSize;
    std::uint32_t inputChannels;
    std::uint32_t outputChannels;

    // ------------------------------------------------------------- liveness
    std::atomic<std::uint32_t> childState;
    std::atomic<std::uint64_t> childHeartbeat; ///< ++ for every block answered
    std::atomic<std::uint32_t> childPid;

    // ------------------------------------------------------------ handshake
    std::atomic<std::uint64_t> requestSeq;  ///< host: this slot is filled
    std::atomic<std::uint64_t> responseSeq; ///< child: this slot is answered
    std::atomic<std::uint32_t> underruns;   ///< host: the child was not in time

    std::uint32_t frames[slots];
    std::uint32_t midiBytes[slots];

    // ----------------------------------------------------------- parameters
    ParamRing toChild;
    ParamRing toHost;

    // --------------------------------------------------------------- bulk
    unsigned char midi[slots][midiCapacity];
    float input[slots][maxChannels][maxBlock];
    float output[slots][maxChannels][maxBlock];

    float* inputChannel(int slot, int channel) noexcept { return input[slot][channel]; }
    float* outputChannel(int slot, int channel) noexcept { return output[slot][channel]; }
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "the handshake is shared between processes, so it cannot fall back to a lock: "
              "a lock in one process's address space means nothing in the other's");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "see above");

/** Both sides agree on this or neither can trust a byte of the region. */
inline bool looksValid(const Shared& s) noexcept
{
    return s.magic == magic && s.version == version && s.headerBytes == sizeof(Shared);
}

inline void initialise(Shared& s, double rate, int blockSize, int inputs, int outputs) noexcept
{
    std::memset(&s, 0, sizeof(Shared));

    s.magic = magic;
    s.version = version;
    s.headerBytes = (std::uint32_t) sizeof(Shared);
    s.sampleRate = rate;
    s.maxBlockSize = (std::uint32_t) blockSize;
    s.inputChannels = (std::uint32_t) inputs;
    s.outputChannels = (std::uint32_t) outputs;

    s.childState.store((std::uint32_t) ChildState::starting, std::memory_order_relaxed);
    s.toChild.reset();
    s.toHost.reset();
}

} // namespace plugshell::link
