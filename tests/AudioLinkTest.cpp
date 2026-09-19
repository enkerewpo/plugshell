// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

/**
    Tests for the process boundary the audio crosses.

    This is the one part of out-of-process hosting where being wrong is a
    glitch in somebody's recording rather than a message on a screen, so it is
    tested against a real second process rather than against a mock: the cases
    that matter -- a child that falls behind, a child that is killed outright --
    only exist because there are two processes, and a test that fakes the
    second one cannot produce them.
*/

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "../src/shared/LinkEndpoints.h"

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& what)
{
    ++checks;

    if (!condition)
    {
        ++failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

std::string scratchPath()
{
    return "/tmp/plugshell-link-test-" + std::to_string(::getpid()) + ".bin";
}

// ---------------------------------------------------------------- the rings

void testParameterRing()
{
    std::printf("parameter ring\n");

    auto ring = std::make_unique<plugshell::link::ParamRing>();
    plugshell::link::ParamChange out{};

    check(!ring->pop(out), "an empty ring has nothing to pop");

    check(ring->push(7, 0.25f), "a push into an empty ring succeeds");
    check(ring->pop(out), "what was pushed can be popped");
    check(out.index == 7 && out.value == 0.25f, "and comes back unchanged");

    // Order, not just contents. A plugin told to go to 0.2 and then 0.8 must
    // not be told only the last one.
    for (int i = 0; i < 100; ++i)
        ring->push((std::uint32_t) i, (float) i);

    bool ordered = true;
    for (int i = 0; i < 100; ++i)
        ordered = ordered && ring->pop(out) && out.index == (std::uint32_t) i;

    check(ordered, "changes come back in the order they were made");

    ring->reset();

    int accepted = 0;
    for (int i = 0; i < plugshell::link::paramCapacity * 2; ++i)
        if (ring->push((std::uint32_t) i, 0.0f))
            ++accepted;

    check(accepted == plugshell::link::paramCapacity - 1,
          "a full ring refuses rather than wraps over itself");
    check(ring->dropped.load() > 0, "and says how much it refused");
}

// ----------------------------------------------------------------- the midi

void testMidiCoding()
{
    std::printf("midi coding\n");

    std::vector<unsigned char> buffer(256);
    plugshell::link::MidiWriter w{buffer.data(), (int) buffer.size()};

    const unsigned char noteOn[3] = {0x90, 60, 100};
    const unsigned char noteOff[3] = {0x80, 60, 0};

    check(w.add(0, noteOn, 3), "a message fits in an empty buffer");
    check(w.add(128, noteOff, 3), "and so does a second");

    plugshell::link::MidiReader r{buffer.data(), w.used};

    int at = 0;
    int n = 0;
    const unsigned char* bytes = nullptr;

    check(r.next(at, bytes, n) && at == 0 && n == 3 && bytes[1] == 60,
          "the first comes back with its offset");
    check(r.next(at, bytes, n) && at == 128 && bytes[0] == 0x80, "and the second");
    check(!r.next(at, bytes, n), "and then there are no more");

    // A reader handed a truncated buffer must stop, not walk off the end.
    plugshell::link::MidiReader truncated{buffer.data(), 6};
    check(!truncated.next(at, bytes, n), "a truncated record is refused rather than read");

    // Eight bytes of header and three of message, so one fits exactly and a
    // second cannot.
    plugshell::link::MidiWriter small{buffer.data(), 11};
    check(small.add(0, noteOn, 3), "one message fits in exactly its own size");
    check(!small.add(0, noteOn, 3), "a second does not, and is refused rather than truncated");
}

// ------------------------------------------------------- against a real child

/** What the child does to a block, so the parent can recognise its own audio
    coming back: double it, and count the notes. */
constexpr float childGain = 2.0f;

[[noreturn]] void runChild(int sharedFd, int wakeupFd)
{
    plugshell::link::ChildLink child;
    std::string error;

    if (!child.attach(sharedFd, wakeupFd, error))
        ::_exit(3);

    auto& s = child.shared();
    child.announceReady();

    child.pump(
        [&s](int slot, int numSamples, const unsigned char* midi, int midiSize)
        {
            int midiCount = 0;
            int at = 0, n = 0;
            const unsigned char* bytes = nullptr;
            plugshell::link::MidiReader reader{midi, midiSize};

            while (reader.next(at, bytes, n))
                ++midiCount;

            // Parameter changes arrive before the block they precede.
            plugshell::link::ParamChange change{};
            float extra = 0.0f;

            while (s.toChild.pop(change))
            {
                extra = change.value;
                s.toHost.push(change.index + 1000, change.value);
            }

            for (int ch = 0; ch < (int) s.outputChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    s.output[slot][ch][i] =
                        s.input[slot][ch][i] * childGain + extra + (float) midiCount * 0.001f;
        });

    ::_exit(0);
}

struct Harness
{
    plugshell::link::HostLink host;
    pid_t pid = -1;
    std::string path;

    bool start(double rate, int blockSize, int channels)
    {
        path = scratchPath();
        ::unlink(path.c_str());

        std::string error;
        auto mapping = plugshell::link::Mapping::create(path, error);

        if (!mapping.isValid())
            return false;

        plugshell::link::initialise(*mapping, rate, blockSize, channels, channels);

        auto wakeup = plugshell::link::Wakeup::create();

        if (!wakeup.isValid())
            return false;

        const int readFd = wakeup.readEnd();
        const int sharedFd = mapping.descriptor();

        pid = ::fork();

        if (pid == 0)
        {
            wakeup.closeWriteEnd();
            runChild(sharedFd, readFd);
        }

        wakeup.closeReadEnd();
        host.attach(std::move(mapping), std::move(wakeup));

        // The child has to have opened the region and said so before the first
        // block goes over, or the first block is simply dropped.
        for (int i = 0; i < 500 && !host.isReady(); ++i)
            ::usleep(2000);

        return host.isReady();
    }

    void stop()
    {
        host.closeWakeup();

        if (pid > 0)
        {
            int status = 0;
            ::waitpid(pid, &status, 0);
            pid = -1;
        }

        host.detach();
        ::unlink(path.c_str());
    }
};

/** One exchange, waiting for the child to answer it. Real audio never waits;
    a test that did not would be testing the scheduler. */
bool exchangeAndSettle(plugshell::link::HostLink& host, const float* const* in, float* const* out,
                       int channels, int numSamples, const unsigned char* midi = nullptr, int midiSize = 0)
{
    const auto before = host.heartbeat();

    if (!host.exchange(in, channels, out, channels, numSamples, midi, midiSize))
        return false;

    for (int i = 0; i < 2000 && host.heartbeat() == before; ++i)
        ::usleep(500);

    return host.heartbeat() != before;
}

void testRoundTrip()
{
    std::printf("audio round trip\n");

    constexpr int channels = 2;
    constexpr int frames = 256;

    Harness h;

    if (!h.start(48000.0, frames, channels))
    {
        check(false, "the child starts and says it is ready");
        return;
    }

    check(true, "the child starts and says it is ready");

    std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
    const float* in[channels] = {inL.data(), inR.data()};
    float* out[channels] = {outL.data(), outR.data()};

    // Block zero: submitted, and there is nothing yet to come back.
    for (int i = 0; i < frames; ++i)
    {
        inL[i] = std::sin((float) i * 0.01f);
        inR[i] = -inL[i];
    }

    exchangeAndSettle(h.host, in, out, channels, frames);

    bool firstIsSilent = true;
    for (int i = 0; i < frames; ++i)
        firstIsSilent = firstIsSilent && outL[i] == 0.0f;

    check(firstIsSilent, "the first block comes back silent, because nothing has been through yet");

    // Block one: what comes back is block zero, doubled.
    std::vector<float> second(frames);
    for (int i = 0; i < frames; ++i)
        second[i] = 0.5f;

    const float* in2[channels] = {second.data(), second.data()};
    exchangeAndSettle(h.host, in2, out, channels, frames);

    bool matchesFirst = true;
    for (int i = 0; i < frames; ++i)
        matchesFirst = matchesFirst && std::abs(outL[i] - inL[i] * childGain) < 1.0e-6f;

    check(matchesFirst, "the second call returns the first block's audio, processed");

    bool rightIsRight = true;
    for (int i = 0; i < frames; ++i)
        rightIsRight = rightIsRight && std::abs(outR[i] - inR[i] * childGain) < 1.0e-6f;

    check(rightIsRight, "channels do not cross on the way");

    // And the third returns the second, which proves the slots alternate
    // rather than one of them being written every time.
    exchangeAndSettle(h.host, in, out, channels, frames);

    bool matchesSecond = true;
    for (int i = 0; i < frames; ++i)
        matchesSecond = matchesSecond && std::abs(outL[i] - 1.0f) < 1.0e-6f;

    check(matchesSecond, "and the third returns the second, so the two slots alternate");
    check(h.host.underruns() == 0, "none of that was late");

    h.stop();
}

void testShortAndLongBlocks()
{
    std::printf("varying block sizes\n");

    constexpr int channels = 2;
    Harness h;

    if (!h.start(48000.0, 1024, channels))
    {
        check(false, "the child starts");
        return;
    }

    std::vector<float> in(1024, 0.25f), outL(1024), outR(1024);
    const float* ins[channels] = {in.data(), in.data()};
    float* outs[channels] = {outL.data(), outR.data()};

    // A device may change its buffer size between callbacks, and the block
    // that follows a longer one must not read the tail of it.
    const int sizes[] = {64, 512, 128, 1024, 32};

    bool ok = true;
    for (int i = 0; i < 5; ++i)
    {
        std::fill(outL.begin(), outL.end(), -1.0f);
        exchangeAndSettle(h.host, ins, outs, channels, sizes[i]);

        if (i > 0)
        {
            const int expect = std::min(sizes[i], sizes[i - 1]);

            for (int j = 0; j < expect; ++j)
                ok = ok && std::abs(outL[j] - 0.5f) < 1.0e-6f;
        }
    }

    check(ok, "a block of a different length than the one before it comes back correct");

    h.stop();
}

void testParameterCrossing()
{
    std::printf("parameters across the boundary\n");

    constexpr int channels = 2;
    constexpr int frames = 128;

    Harness h;

    if (!h.start(48000.0, frames, channels))
    {
        check(false, "the child starts");
        return;
    }

    std::vector<float> in(frames, 0.0f), outL(frames), outR(frames);
    const float* ins[channels] = {in.data(), in.data()};
    float* outs[channels] = {outL.data(), outR.data()};

    check(h.host.sendParameter(42, 0.75f), "a parameter change is accepted");

    exchangeAndSettle(h.host, ins, outs, channels, frames);
    exchangeAndSettle(h.host, ins, outs, channels, frames);

    check(std::abs(outL[0] - 0.75f) < 1.0e-6f, "and reached the child before the block it precedes");

    plugshell::link::ParamChange back{};
    bool received = false;

    for (int i = 0; i < 100 && !received; ++i)
        received = h.host.receiveParameter(back);

    check(received && back.index == 1042 && back.value == 0.75f, "and the child can answer the same way");

    h.stop();
}

void testMidiCrossing()
{
    std::printf("midi across the boundary\n");

    constexpr int channels = 2;
    constexpr int frames = 128;

    Harness h;

    if (!h.start(48000.0, frames, channels))
    {
        check(false, "the child starts");
        return;
    }

    std::vector<float> in(frames, 0.0f), outL(frames), outR(frames);
    const float* ins[channels] = {in.data(), in.data()};
    float* outs[channels] = {outL.data(), outR.data()};

    unsigned char midi[64];
    plugshell::link::MidiWriter w{midi, sizeof(midi)};
    const unsigned char noteOn[3] = {0x90, 60, 100};
    w.add(0, noteOn, 3);
    w.add(8, noteOn, 3);
    w.add(16, noteOn, 3);

    exchangeAndSettle(h.host, ins, outs, channels, frames, midi, w.used);
    exchangeAndSettle(h.host, ins, outs, channels, frames);

    check(std::abs(outL[0] - 0.003f) < 1.0e-5f, "three messages arrived as three messages");

    h.stop();
}

void testChildKilled()
{
    std::printf("the child is killed\n");

    constexpr int channels = 2;
    constexpr int frames = 256;

    Harness h;

    if (!h.start(48000.0, frames, channels))
    {
        check(false, "the child starts");
        return;
    }

    std::vector<float> in(frames, 0.5f), outL(frames), outR(frames);
    const float* ins[channels] = {in.data(), in.data()};
    float* outs[channels] = {outL.data(), outR.data()};

    exchangeAndSettle(h.host, ins, outs, channels, frames);
    exchangeAndSettle(h.host, ins, outs, channels, frames);

    check(std::abs(outL[0] - 1.0f) < 1.0e-6f, "audio is flowing before the kill");

    // Not a request to stop. The case being tested is the one this whole
    // exercise exists for: a plugin that dies without warning, mid-block.
    ::kill(h.pid, SIGKILL);
    int status = 0;
    ::waitpid(h.pid, &status, 0);
    h.pid = -1;

    check(WIFSIGNALED(status), "and the child really did die");

    bool everSilent = false;
    bool everCrashed = false;

    for (int i = 0; i < 8; ++i)
    {
        std::fill(outL.begin(), outL.end(), 99.0f);
        h.host.exchange(ins, channels, outs, channels, frames, nullptr, 0);

        bool silent = true;
        for (int j = 0; j < frames; ++j)
            silent = silent && outL[j] == 0.0f;

        everSilent = everSilent || silent;
        (void) everCrashed;
    }

    check(everSilent, "the host reads silence rather than whatever was left in the slot");
    check(h.host.underruns() > 0, "and counts the blocks the child did not answer");

    h.stop();
}

void testHostClosingEndsTheChild()
{
    std::printf("the host goes away\n");

    Harness h;

    if (!h.start(48000.0, 256, 2))
    {
        check(false, "the child starts");
        return;
    }

    const pid_t child = h.pid;

    // Closing the write end is the only signal sent. A child that needed to be
    // told separately would be a child left running after a crash.
    h.host.closeWakeup();

    int status = 0;
    bool exited = false;

    for (int i = 0; i < 500 && !exited; ++i)
    {
        if (::waitpid(child, &status, WNOHANG) == child)
            exited = true;
        else
            ::usleep(4000);
    }

    check(exited && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "the child stops by itself when the pipe closes, without being signalled");

    h.pid = -1;
    h.stop();
}

} // namespace

int main()
{
    std::printf("plugshell audio link\n\n");

    testParameterRing();
    testMidiCoding();
    testRoundTrip();
    testShortAndLongBlocks();
    testParameterCrossing();
    testMidiCrossing();
    testChildKilled();
    testHostClosingEndsTheChild();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
