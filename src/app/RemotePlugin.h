// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <crt_externs.h>
#include <memory>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "../shared/LinkEndpoints.h"

namespace plugshell
{

/**
    A plugin running in another process, standing in for one running here.

    To everything above it this is an ordinary AudioProcessor: it has a name,
    parameters, programs, and a processBlock. What it does not have is the
    plugin's code, which is the point. A segmentation fault is not an
    exception and no handler catches it, so the only way to survive one is to
    be somewhere else when it happens.

    What survives a crash, and what does not, is worth being plain about.
    Parameter values survive, because this mirrors them as they change and can
    replay them into a fresh process. Anything the plugin kept to itself --
    an unsaved wavetable, a sample it loaded, where its editor was scrolled to
    -- does not. That is the same answer a DAW gives, and pretending otherwise
    would mean claiming to have state nobody handed over.
*/
class RemotePlugin : public juce::AudioProcessor, private juce::Timer
{
public:
    /** Called on the message thread when the child stops without being asked.
        @p status is what waitpid said, already read. */
    std::function<void(const juce::String& reason)> onChildLost;

    /** Called on the message thread once the child has a plugin loaded and
        its parameters have been mirrored. */
    std::function<void()> onReady;

    /** How big the editor wants to be, once it exists and again whenever the
        plugin resizes it. The host window is sized from this. */
    std::function<void(int width, int height)> onEditorSize;

    RemotePlugin()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("In", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Out", juce::AudioChannelSet::stereo(), true))
    {
    }

    ~RemotePlugin() override { shutdown(); }

    // --------------------------------------------------------------- launch

    struct Options
    {
        juce::File helper; ///< the plugshell-host executable
        juce::File plugin; ///< the .vst3 to load
        double sampleRate = 48000.0;
        int blockSize = 512;
        int inputChannels = 2;
        int outputChannels = 2;
    };

    /** One launch per object.

        JUCE gives an AudioProcessor no way to remove a parameter once it has
        been added, and a second plugin's parameters are not the first's. So a
        restart after a crash is a new proxy, not this one reused -- which is
        the honest model anyway: a fresh process is a fresh plugin, and
        anything carried across has to be carried deliberately. */
    bool launch(const Options& o, juce::String& error)
    {
        if (spent)
            return fail(error, "this proxy has already hosted a plugin; make another one");

        spent = true;

        if (!o.helper.existsAsFile())
            return fail(error, "the out-of-process helper is missing from the application bundle");

        std::string problem;
        const auto backing =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("plugshell-link-" + juce::String(juce::Random::getSystemRandom().nextInt64()))
                .getFullPathName()
                .toStdString();

        auto mapping = link::Mapping::create(backing, problem);

        if (!mapping.isValid())
            return fail(error, problem);

        link::initialise(*mapping, o.sampleRate, o.blockSize, o.inputChannels, o.outputChannels);

        auto wakeup = link::Wakeup::create();

        if (!wakeup.isValid())
            return fail(error, "could not create the wake pipe");

        int controlPair[2]{-1, -1};

        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, controlPair) != 0)
            return fail(error, "could not create the control socket");

        options = o;
        const auto pid = spawn(o, mapping.descriptor(), wakeup.readEnd(), controlPair[1], error);

        ::close(controlPair[1]); // the child's end; ours is controlPair[0]

        if (pid <= 0)
        {
            ::close(controlPair[0]);
            return false;
        }

        childPid = pid;
        controlFd = controlPair[0];

        wakeup.closeReadEnd();
        host.attach(std::move(mapping), std::move(wakeup));

        reader = std::make_unique<Reader>(*this);
        reader->startThread();

        auto* open = new juce::DynamicObject();
        open->setProperty("path", o.plugin.getFullPathName());
        open->setProperty("rate", o.sampleRate);
        open->setProperty("block", o.blockSize);
        send("open", juce::var(open));

        startTimerHz(10);
        return true;
    }

    void shutdown()
    {
        stopTimer();

        if (reader != nullptr)
        {
            reader->signalThreadShouldExit();

            if (controlFd >= 0)
                ::shutdown(controlFd, SHUT_RDWR); // unblocks the read

            reader->stopThread(1500);
            reader.reset();
        }

        // Closing the wake pipe is how the child is asked to stop, and the
        // same thing that happens if this process dies -- so there is one exit
        // path rather than a polite one and an abrupt one.
        host.closeWakeup();

        if (childPid > 0)
        {
            for (int i = 0; i < 100; ++i)
            {
                int status = 0;

                if (::waitpid(childPid, &status, WNOHANG) == childPid)
                    break;

                juce::Thread::sleep(10);
            }

            // It had a second. A plugin that will not stop is exactly the kind
            // this exists to be able to get rid of.
            if (::waitpid(childPid, nullptr, WNOHANG) == 0)
            {
                ::kill(childPid, SIGKILL);
                ::waitpid(childPid, nullptr, 0);
            }

            childPid = -1;
        }

        if (controlFd >= 0)
        {
            ::close(controlFd);
            controlFd = -1;
        }

        host.detach();

        // The parameters stay. They are owned by the AudioProcessor and there
        // is no way to take them back; what they can do is stop lying, so
        // they keep their last values and their writes go nowhere.
        programs.clear();
        ready = false;
    }

    bool isAlive() const noexcept { return childPid > 0; }
    bool isReady() const noexcept { return ready; }
    int getChildPid() const noexcept { return childPid; }
    std::uint32_t getUnderruns() const noexcept { return host.underruns(); }

    // -------------------------------------------------------- AudioProcessor

    const juce::String getName() const override
    {
        return loadedName.isNotEmpty() ? loadedName : juce::String("plugshell remote");
    }

    /**
        Forwarded, and waited for.

        Offline rendering re-prepares a plugin at whatever rate was asked for,
        and the plugin is in the other process, so a prepare that stopped here
        would leave a caller asking for 96 kHz and quietly getting whatever
        the audio device happens to be set to. Waiting is safe because this is
        only ever called from the message thread, with nothing on the audio
        thread pointed at this processor.
    */
    void prepareToPlay(double rate, int blockSize) override
    {
        auto* args = new juce::DynamicObject();
        args->setProperty("rate", rate);
        args->setProperty("block", blockSize);
        args->setProperty("nonRealtime", isNonRealtime());

        callAndWait("prepare", juce::var(args), 10000);
    }

    void releaseResources() override {}

    void setNonRealtime(bool shouldBeNonRealtime) noexcept override
    {
        juce::AudioProcessor::setNonRealtime(shouldBeNonRealtime);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int numSamples = buffer.getNumSamples();

        midiBytes = 0;
        link::MidiWriter writer{midiScratch, (int) sizeof(midiScratch)};

        for (const auto metadata : midi)
            if (!writer.add(metadata.samplePosition, metadata.data, metadata.numBytes))
                break;

        midiBytes = writer.used;

        const auto* const* in = buffer.getArrayOfReadPointers();
        auto* const* out = buffer.getArrayOfWritePointers();

        // The input has to be copied out before the output is written into the
        // same buffer, which exchange() does in that order for exactly this
        // reason: JUCE hands one buffer for both.
        if (offline)
            host.exchangeBlocking(in, buffer.getNumChannels(), out, buffer.getNumChannels(), numSamples,
                                  midiScratch, midiBytes, 2000);
        else
            host.exchange(in, buffer.getNumChannels(), out, buffer.getNumChannels(), numSamples, midiScratch,
                          midiBytes);

        midi.clear();
    }

    /**
        Renders block for block instead of one behind, waiting for each.

        Offline rendering measures a plugin, and a measurement that is one
        block out of step with the note that caused it is not a measurement.
        There is no device deadline here to miss, so waiting is allowed --
        which is true only because nothing calls processBlock from the audio
        thread while this is on, and the caller is responsible for that.
    */
    void setOfflineMode(bool shouldBeOffline) { offline = shouldBeOffline; }

    // -------------------------------------------------- surviving the crash

    /** Every parameter's value, as this process last saw it.

        This is what survives a plugin dying, and it is worth being exact
        about why: these numbers were never in the child's memory to begin
        with. They were mirrored here as they changed, so they are still here
        when the process holding the plugin is not. */
    std::vector<float> captureParameters() const
    {
        std::vector<float> values;
        values.reserve(mirrored.size());

        for (const auto* p : mirrored)
            values.push_back(p->getValue());

        return values;
    }

    /** Puts them back into a freshly started plugin.

        Down the control channel in one message rather than through the
        parameter ring: the ring holds two thousand changes and a plugin can
        publish twice that, so a restore sent through it would arrive with the
        beginning missing. */
    /**
        Asks the child for one parameter as it stands, and waits.

        Only the plugin can turn 0.31 into "153 Hz", and the mirror learns the
        new text a frame or two later, on the child's own timer. That is soon
        enough for a display and too late for a reply to the caller who just
        set it -- which would otherwise answer with the text from before the
        change and look like the change had not taken.
    */
    void refreshParameter(int index)
    {
        if ((size_t) index >= mirrored.size())
            return;

        auto* args = new juce::DynamicObject();
        args->setProperty("offset", index);
        args->setProperty("limit", 1);

        const auto answer = callAndWait("params", juce::var(args), 1000);

        if (auto* items = answer.getProperty("parameters", juce::var()).getArray())
            if (!items->isEmpty())
            {
                const auto& item = items->getReference(0);
                mirrored[(size_t) index]->setValueFromChild((float) (double) item.getProperty("value", 0.0));
                mirrored[(size_t) index]->setTextFromChild(item.getProperty("text", "").toString());
            }
    }

    void restoreParameters(const std::vector<float>& values)
    {
        if (values.empty())
            return;

        juce::Array<juce::var> items;
        items.ensureStorageAllocated((int) values.size());

        for (const auto v : values)
            items.add(v);

        for (size_t i = 0; i < values.size() && i < mirrored.size(); ++i)
            mirrored[i]->setValueFromChild(values[i]);

        auto* args = new juce::DynamicObject();
        args->setProperty("values", items);
        send("setMany", juce::var(args));
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return juce::jmax(1, programs.size()); }
    int getCurrentProgram() override { return currentProgram; }

    void setCurrentProgram(int index) override
    {
        currentProgram = index;
        auto* args = new juce::DynamicObject();
        args->setProperty("index", index);
        send("program", juce::var(args));
    }

    const juce::String getProgramName(int index) override
    {
        return juce::isPositiveAndBelow(index, programs.size()) ? programs[index] : juce::String();
    }

    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // ------------------------------------------------------------ the editor

    /**
        Opens or closes the editor.

        It is drawn by the other process, so it cannot be a component in this
        window's tree; what it can be is a borderless window of the child's,
        held exactly over the area this window leaves for it. Which of the two
        it is does not show, and is not meant to: see AttachedPanel in the
        child for the whole of the reasoning.

        The size comes back through onEditorSize rather than from here,
        because only the plugin knows it and it can change afterwards.
    */
    void setEditorVisible(bool shouldShow)
    {
        auto* args = new juce::DynamicObject();
        args->setProperty("show", shouldShow);
        send("editor", juce::var(args));
    }

    /** Where the editor should be, which window it should sit directly above,
        and whether it should be seen at all.

        @p hostWindow is the window server's number for this application's
        window. The child cannot ask for it -- a window number is the one name
        for a window that crosses a process boundary, and this side is the
        only side that knows it.

        Nothing is waited for: this is a position, and a position that arrives
        a frame late is a position, while one that blocks the message thread
        is a stutter. */
    void placeEditor(juce::Rectangle<int> screenBounds, bool visible, std::uint32_t hostWindow, bool reorder)
    {
        auto* args = new juce::DynamicObject();
        args->setProperty("x", screenBounds.getX());
        args->setProperty("y", screenBounds.getY());
        args->setProperty("width", screenBounds.getWidth());
        args->setProperty("height", screenBounds.getHeight());
        args->setProperty("show", visible);
        args->setProperty("window", (juce::int64) hostWindow);
        args->setProperty("reorder", reorder);
        send("place", juce::var(args));
    }

private:
    // ---------------------------------------------------------- parameters

    /** One of the child's parameters, as far as everything here is concerned.

        The value is a local copy. Reading it must not involve the other
        process -- getValue() is called from the audio thread and from paint --
        and writing it goes into the shared ring, which the child drains and
        applies with the same gesture a hand would make. */
    class RemoteParameter : public juce::AudioProcessorParameter
    {
    public:
        RemoteParameter(RemotePlugin& p, int i, juce::String n, juce::String l, float v, juce::String t)
            : owner(p), index(i), paramName(std::move(n)), paramLabel(std::move(l)), text(std::move(t)),
              value(v)
        {
        }

        float getValue() const override { return value.load(std::memory_order_relaxed); }

        void setValue(float newValue) override
        {
            value.store(newValue, std::memory_order_relaxed);
            owner.host.sendParameter(index, newValue);
        }

        /** Set by the child telling us what it did, which must not be sent
            straight back as an instruction. */
        void setValueFromChild(float newValue) { value.store(newValue, std::memory_order_relaxed); }

        void setTextFromChild(juce::String t) { text = std::move(t); }

        float getDefaultValue() const override { return 0.0f; }
        juce::String getName(int maximumLength) const override
        {
            return paramName.substring(0, maximumLength);
        }
        juce::String getLabel() const override { return paramLabel; }
        juce::String getText(float, int) const override { return text; }
        float getValueForText(const juce::String&) const override { return getValue(); }

    private:
        RemotePlugin& owner;
        const int index;
        juce::String paramName, paramLabel, text;
        std::atomic<float> value;
    };

    // ------------------------------------------------------------- spawning

    pid_t spawn(const Options& o, int sharedFd, int wakeupFd, int controlChildFd, juce::String& error)
    {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);

        // Fixed numbers agreed with the child rather than passed as flags: two
        // sides have to get this right at once, and a constant is easier to
        // keep honest than a pair of command lines.
        posix_spawn_file_actions_adddup2(&actions, sharedFd, 3);
        posix_spawn_file_actions_adddup2(&actions, wakeupFd, 4);
        posix_spawn_file_actions_adddup2(&actions, controlChildFd, 5);

        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);

        // Everything not named above is closed in the child. A plugin's code
        // is about to be mapped into that process, and it has no business
        // inheriting this one's sockets and files.
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);

        // Standard error is kept, so that anything the plugin's own code
        // prints on its way down is visible in the host's log rather than
        // going into a closed descriptor.
        posix_spawn_file_actions_adddup2(&actions, 2, 2);

        const auto exePath = o.helper.getFullPathName().toStdString();

        // Nothing but the executable. What to open is sent over the control
        // channel, where a path with a space in it is a value rather than
        // something a quoting rule has to survive.
        std::vector<std::string> args{exePath};

        std::vector<char*> argv;
        for (auto& a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int result =
            ::posix_spawn(&pid, exePath.c_str(), &actions, &attributes, argv.data(), *::_NSGetEnviron());

        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);

        if (result != 0)
        {
            fail(error, "could not start the plugin process: " + juce::String(std::strerror(result)));
            return -1;
        }

        return pid;
    }

    static bool fail(juce::String& error, const juce::String& why)
    {
        error = why;
        return false;
    }

    // --------------------------------------------------------- the control

    class Reader : public juce::Thread
    {
    public:
        explicit Reader(RemotePlugin& o) : juce::Thread("plugshell remote control"), owner(o) {}

        void run() override
        {
            juce::String pending;
            char buffer[8192];

            while (!threadShouldExit())
            {
                const auto n = ::read(owner.controlFd, buffer, sizeof(buffer));

                if (n <= 0)
                    break;

                pending += juce::String::fromUTF8(buffer, (int) n);

                for (;;)
                {
                    const auto newline = pending.indexOfChar('\n');

                    if (newline < 0)
                        break;

                    const auto line = pending.substring(0, newline).trim();
                    pending = pending.substring(newline + 1);

                    juce::var parsed;

                    if (line.isNotEmpty() && juce::JSON::parse(line, parsed).wasOk())
                    {
                        // Answers first, and on this thread: whoever is
                        // waiting for one may be the message thread.
                        if (owner.deliverToWaiter(parsed))
                            continue;

                        auto& target = owner;
                        const auto copy = parsed;
                        juce::MessageManager::callAsync([&target, copy] { target.received(copy); });
                    }
                }
            }
        }

    private:
        RemotePlugin& owner;
    };

    /**
        Sends a request and waits for its reply.

        The reply is handed over by the reader thread directly rather than
        through the message queue, which matters: the caller of this is on the
        message thread, so a reply that had to be delivered by callAsync would
        be waiting behind the very thread that is waiting for it.
    */
    juce::var callAndWait(const juce::String& op, juce::var args, int timeoutMs)
    {
        if (controlFd < 0)
            return {};

        const int id = ++nextId;

        auto waiter = std::make_shared<Waiter>();

        {
            const juce::ScopedLock held(waitersLock);
            waiters.push_back({id, waiter});
        }

        if (auto* o = args.getDynamicObject())
            o->setProperty("id", id);

        send(op, args);
        waiter->arrived.wait(timeoutMs);

        {
            const juce::ScopedLock held(waitersLock);

            for (auto it = waiters.begin(); it != waiters.end(); ++it)
                if (it->first == id)
                {
                    waiters.erase(it);
                    break;
                }
        }

        return waiter->result;
    }

    struct Waiter
    {
        juce::WaitableEvent arrived;
        juce::var result;
    };

    /** @return true when this message was somebody's answer and has been
        handed to them, so it needs no further handling. */
    bool deliverToWaiter(const juce::var& message)
    {
        if (!message.hasProperty("id"))
            return false;

        const int id = (int) message.getProperty("id", -1);
        const juce::ScopedLock held(waitersLock);

        for (auto& [waiting, waiter] : waiters)
            if (waiting == id)
            {
                waiter->result = message;
                waiter->arrived.signal();
                return true;
            }

        return false;
    }

    void send(const juce::String& op, juce::var args = {})
    {
        if (controlFd < 0)
            return;

        juce::DynamicObject* o = args.getDynamicObject();

        if (o == nullptr)
        {
            o = new juce::DynamicObject();
            args = juce::var(o);
        }

        o->setProperty("op", op);

        const auto line = (juce::JSON::toString(args, true) + "\n").toStdString();
        size_t written = 0;

        while (written < line.size())
        {
            const auto n = ::write(controlFd, line.data() + written, line.size() - written);

            if (n <= 0)
                return;

            written += (size_t) n;
        }
    }

    /** Runs on the message thread. */
    void received(const juce::var& message)
    {
        const auto event = message.getProperty("event", "").toString();

        if (event == "ready" || event == "changed")
        {
            loadedName = message.getProperty("name", "").toString();
            currentProgram = (int) message.getProperty("currentProgram", 0);

            programs.clear();
            if (auto* array = message.getProperty("programs", juce::var()).getArray())
                for (const auto& p : *array)
                    programs.add(p.toString());

            expecting = (int) message.getProperty("parameterCount", 0);

            if (event == "ready")
            {
                mirrored.clear();
                requestParameterPage(0);
            }
            return;
        }

        if (event == "error")
        {
            if (onChildLost)
                onChildLost(message.getProperty("error", "the plugin process could not start").toString());
            return;
        }

        if (event == "editorSize")
        {
            if (onEditorSize)
                onEditorSize((int) message.getProperty("width", 0), (int) message.getProperty("height", 0));

            return;
        }

        if (event == "text")
        {
            if (auto* items = message.getProperty("items", juce::var()).getArray())
                for (const auto& item : *items)
                {
                    const size_t index = (size_t) (int) item.getProperty("index", -1);

                    if (index < mirrored.size())
                        mirrored[index]->setTextFromChild(item.getProperty("text", "").toString());
                }

            return;
        }

        if (message.hasProperty("parameters"))
            return absorbParameterPage(message);
    }

    void requestParameterPage(int offset)
    {
        auto* args = new juce::DynamicObject();
        args->setProperty("offset", offset);
        args->setProperty("limit", pageSize);
        send("params", juce::var(args));
    }

    void absorbParameterPage(const juce::var& message)
    {
        auto* items = message.getProperty("parameters", juce::var()).getArray();

        if (items == nullptr)
            return;

        for (const auto& item : *items)
        {
            auto* p = new RemoteParameter(
                *this, (int) item.getProperty("index", 0), item.getProperty("name", "").toString(),
                item.getProperty("label", "").toString(), (float) (double) item.getProperty("value", 0.0),
                item.getProperty("text", "").toString());

            mirrored.push_back(p);
            addParameter(p);
        }

        const int have = (int) mirrored.size();

        if (have < expecting && !items->isEmpty())
            return requestParameterPage(have);

        ready = true;

        if (onReady)
            onReady();
    }

    /** Two things, both on the message thread: notice that the child has gone,
        and take the parameter changes its editor has made. */
    void timerCallback() override
    {
        link::ParamChange change{};
        int drained = 0;

        while (drained < 512 && host.receiveParameter(change))
        {
            if (change.index < mirrored.size())
                mirrored[change.index]->setValueFromChild(change.value);

            ++drained;
        }

        if (childPid <= 0)
            return;

        int status = 0;

        if (::waitpid(childPid, &status, WNOHANG) != childPid)
            return;

        const int pid = childPid;
        childPid = -1;
        ready = false;
        stopTimer();

        const auto reason =
            WIFSIGNALED(status)
                ? "the plugin crashed (" + juce::String(::strsignal(WTERMSIG(status))) + ")"
                : "the plugin process stopped (exit " + juce::String(WEXITSTATUS(status)) + ")";

        juce::Logger::writeToLog("plugshell: child " + juce::String(pid) + " gone: " + reason);

        if (onChildLost)
            onChildLost(reason);
    }

    static constexpr int pageSize = 512;

    link::HostLink host;
    Options options;

    pid_t childPid = -1;
    int controlFd = -1;
    std::unique_ptr<Reader> reader;

    juce::String loadedName;
    juce::StringArray programs;
    int currentProgram = 0;
    int expecting = 0;
    std::vector<RemoteParameter*> mirrored; ///< owned by the AudioProcessor
    bool ready = false;
    bool spent = false;
    bool offline = false;

    juce::CriticalSection waitersLock;
    std::vector<std::pair<int, std::shared_ptr<Waiter>>> waiters;
    std::atomic<int> nextId{0};

    unsigned char midiScratch[link::midiCapacity]{};
    int midiBytes = 0;

    friend class RemoteParameter;
};

} // namespace plugshell
