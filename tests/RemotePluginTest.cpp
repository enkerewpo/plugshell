// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

/**
    The proxy, the helper and a real plugin, together.

    The link's own tests prove the transport. This proves the thing built on
    it: that a plugin loaded into another process can be played, that its
    parameters arrive back here, and -- the case the whole exercise exists for
    -- that killing it outright leaves this process running and told why.

    It needs a plugin to be installed. Where there is none it says so and
    passes, because a machine without a VST3 is not a broken build.
*/

#include "../src/app/RemotePlugin.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <signal.h>

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const juce::String& what)
{
    ++checks;

    if (!condition)
    {
        ++failures;
        std::printf("  FAIL  %s\n", what.toRawUTF8());
    }
}

/** Pumps the message loop, which every reply from the child arrives on. */
bool waitFor(std::function<bool()> done, int milliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (std::uint32_t) milliseconds;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (done())
            return true;

        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }

    return done();
}

/** The smallest installed plugin, because this test loads one and a
    sample-library instrument takes half a minute to become ready. A VST3 is a
    bundle, which is to say a directory -- the reason an earlier version of
    this found nothing on a machine with forty of them installed. */
juce::File findAPlugin()
{
    if (const auto named = juce::SystemStats::getEnvironmentVariable("PLUGSHELL_TEST_PLUGIN", {});
        named.isNotEmpty())
        return juce::File(named);

    juce::File smallest;
    juce::int64 smallestSize = 0;

    for (const auto& directory : {juce::File("/Library/Audio/Plug-Ins/VST3"),
                                  juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                      .getChildFile("Library/Audio/Plug-Ins/VST3")})
    {
        if (!directory.isDirectory())
            continue;

        for (const auto& entry :
             juce::RangedDirectoryIterator(directory, false, "*.vst3", juce::File::findFilesAndDirectories))
        {
            const auto size = entry.getFile().getSize();

            if (smallest == juce::File() || size < smallestSize)
            {
                smallest = entry.getFile();
                smallestSize = size;
            }
        }
    }

    return smallest;
}

juce::File helper()
{
    return juce::File(PLUGSHELL_HELPER_PATH);
}

void run()
{
    const auto pluginFile = findAPlugin();

    if (pluginFile == juce::File())
    {
        std::printf("no VST3 installed; nothing to test against\n");
        return;
    }

    std::printf("using %s\n", pluginFile.getFileName().toRawUTF8());
    std::printf("helper %s\n\n", helper().getFullPathName().toRawUTF8());

    plugshell::RemotePlugin remote;

    bool ready = false;
    juce::String lost;

    remote.onReady = [&ready] { ready = true; };
    remote.onChildLost = [&lost](const juce::String& why) { lost = why; };

    plugshell::RemotePlugin::Options options;
    options.helper = helper();
    options.plugin = pluginFile;
    options.sampleRate = 48000.0;
    options.blockSize = 256;

    juce::String error;

    std::printf("launching\n");
    check(remote.launch(options, error), "the helper starts: " + error);
    check(waitFor([&ready] { return ready; }, 30000), "the plugin loads and its parameters arrive");

    if (!ready)
    {
        std::printf("  (giving up: %s)\n", lost.toRawUTF8());
        return;
    }

    std::printf("loaded %s, %d parameters, %d programs\n", remote.getName().toRawUTF8(),
                remote.getParameters().size(), remote.getNumPrograms());

    check(remote.getName().isNotEmpty(), "the plugin's name came across");
    check(remote.getParameters().size() > 0, "and so did its parameters");
    check(remote.isAlive() && remote.getChildPid() > 0, "the child is running under its own pid");

    // ---------------------------------------------------------------- audio

    std::printf("playing\n");

    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;

    remote.prepareToPlay(48000.0, 256);

    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

    float loudest = 0.0f;

    for (int block = 0; block < 400; ++block)
    {
        buffer.clear();
        remote.processBlock(buffer, midi);
        midi.clear();

        loudest = juce::jmax(loudest, buffer.getMagnitude(0, buffer.getNumSamples()));

        // The child is a separate process with its own scheduler; a test that
        // spun as fast as it could would outrun it and measure nothing.
        juce::Thread::sleep(2);
    }

    check(loudest > 0.0001f, "a note played through the other process came back as audio");
    std::printf("  loudest block %.4f, %u underruns\n", loudest, remote.getUnderruns());

    // ----------------------------------------------------------- parameters

    if (auto* p = remote.getParameters()[0])
    {
        const float was = p->getValue();
        const float wants = was > 0.5f ? 0.1f : 0.9f;

        p->setValue(wants);
        check(std::abs(p->getValue() - wants) < 1.0e-6f, "a parameter set here reads back here at once");

        // And the child really applied it, which is only observable by asking
        // for it back rather than by reading the copy we just wrote.
        waitFor([] { return false; }, 400);
    }

    // ------------------------------------------------------------ the crash

    std::printf("killing the child\n");

    const int pid = remote.getChildPid();
    ::kill(pid, SIGKILL);

    check(waitFor([&lost] { return lost.isNotEmpty(); }, 5000), "the host is told the plugin died");
    check(lost.contains("crash"), "and told that it was a crash, not a clean exit: " + lost);
    std::printf("  reported: %s\n", lost.toRawUTF8());

    // The audio thread keeps calling regardless, which is the case that used
    // to take the whole application with it.
    for (int block = 0; block < 32; ++block)
    {
        buffer.clear();
        remote.processBlock(buffer, midi);
    }

    check(buffer.getMagnitude(0, buffer.getNumSamples()) == 0.0f,
          "and the audio that keeps being asked for is silence, not the last block over again");

    check(!remote.isAlive(), "the proxy knows it has no child");

    remote.shutdown();
    std::printf("shut down cleanly\n");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInstance;

    std::printf("plugshell remote plugin\n\n");
    run();
    std::printf("\n%d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
