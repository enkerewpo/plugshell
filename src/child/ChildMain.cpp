// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

/**
    plugshell-host: one plugin, in a process of its own.

    Launched by plugshell, never by a person. It is handed three inherited
    file descriptors and the path of a plugin, and from then on it is the only
    thing in the system that has that plugin's code mapped into it. When it
    dies -- and the whole reason it exists is that plugins do die -- the host
    notices, says which plugin it was, and offers to start it again.

    Nothing here is allowed to assume the host is still there. Every exit path
    is the same one: the wake pipe closes, the audio loop returns, the process
    ends. That includes the host crashing, which closes the pipe just as
    effectively as the host asking politely.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <mutex>
#include <unistd.h>
#include <vector>

#include "../shared/LinkEndpoints.h"
#include "AttachedPanel.h"

namespace plugshell::child
{

/** Fixed by agreement with the launcher rather than passed as numbers: the
    two sides have to get this right at the same time, and a constant in one
    header is easier to keep honest than a flag in two. */
enum Descriptors
{
    sharedRegionFd = 3,
    wakeupReadFd = 4,
    controlFd = 5
};

// ---------------------------------------------------------------- the editor

/**
    The plugin's editor, put where the host asks for it.

    AttachedPanel carries the reasoning for why it is shaped this way; this is
    the bookkeeping around it. Nothing here decides where to be: the host
    knows where its own content area is on screen and which window that is,
    and this end reports how big the editor wants to be and then does as it is
    told.
*/
class AttachedEditor : private juce::ComponentListener
{
public:
    /** The plugin resized its own editor, which several let the user do. */
    std::function<void(int, int)> onSizeChanged;

    explicit AttachedEditor(juce::AudioProcessorEditor* e) : editor(e)
    {
        panel.setBounds({0, 0, editor->getWidth(), editor->getHeight()});

        // Hosted in the panel's view rather than in a window of its own. This
        // is the same call a plugin wrapper makes to put an editor inside a
        // DAW, which is the right precedent: from the editor's point of view
        // this process is the host.
        editor->setTopLeftPosition(0, 0);
        editor->addToDesktop(0, panel.contentView());
        editor->setVisible(true);
        editor->addComponentListener(this);
    }

    ~AttachedEditor() override
    {
        // Off the panel before the panel goes: a view outliving the window it
        // is in is a crash, and a crash here is the one this whole process
        // exists to keep away from the host.
        if (editor != nullptr)
        {
            editor->removeComponentListener(this);
            editor->removeFromDesktop();
        }
    }

    juce::Point<int> size() const
    {
        return {editor != nullptr ? editor->getWidth() : 0, editor != nullptr ? editor->getHeight() : 0};
    }

    bool isShown() const noexcept { return shown; }

    void place(juce::Rectangle<int> screenBounds, bool visible, std::uint32_t hostWindowNumber, bool reorder)
    {
        if (!screenBounds.isEmpty())
            panel.setBounds(screenBounds);

        if (!visible)
        {
            panel.hide();
            shown = false;
            return;
        }

        // Moving is not reordering. A window being dragged sends one of these
        // per frame, and re-asking the window server for the same ordering
        // sixty times a second is work for nothing -- so the host says when
        // the ordering is actually in question.
        if (reorder || !shown || hostWindowNumber != orderedAgainst)
        {
            panel.showAbove(hostWindowNumber);
            orderedAgainst = hostWindowNumber;
        }

        shown = true;
    }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized && onSizeChanged != nullptr)
            onSizeChanged(editor->getWidth(), editor->getHeight());
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    AttachedPanel panel;
    std::uint32_t orderedAgainst = 0;
    bool shown = false;
};

// ---------------------------------------------------------------- the audio

/**
    The one real-time thread in this process.

    It blocks on the wake pipe, answers whatever the host has submitted, and
    blocks again. It never allocates and never takes a lock the message thread
    can hold.
*/
class AudioWorker : public juce::Thread
{
public:
    AudioWorker(link::ChildLink& l, juce::AudioPluginInstance& p, std::mutex& guard)
        : juce::Thread("plugshell-host audio"), lnk(l), plugin(p), processLock(&guard)
    {
        scratch.setSize(link::maxChannels, link::maxBlock, false, true, false);
    }

    void run() override
    {
        auto& s = lnk.shared();
        const int channels = juce::jmax((int) s.inputChannels, (int) s.outputChannels);

        lnk.pump(
            [this, &s, channels](int slot, int numSamples, const unsigned char* midi, int midiSize)
            {
                if (numSamples <= 0 || numSamples > link::maxBlock)
                    return;

                midiBuffer.clear();

                int at = 0, length = 0;
                const unsigned char* bytes = nullptr;
                link::MidiReader reader{midi, midiSize};

                while (reader.next(at, bytes, length))
                    midiBuffer.addEvent(juce::MidiMessage(bytes, length), at);

                // JUCE processes in place, so the block starts as the input
                // and ends as the output. Channels the host did not send are
                // cleared rather than left holding the previous block.
                for (int ch = 0; ch < channels; ++ch)
                {
                    float* dest = scratch.getWritePointer(ch);

                    if (ch < (int) s.inputChannels)
                        std::copy_n(s.input[slot][ch], numSamples, dest);
                    else
                        std::fill_n(dest, numSamples, 0.0f);
                }

                juce::AudioBuffer<float> block(scratch.getArrayOfWritePointers(), channels, numSamples);

                // Tried, not taken. The plugin is being prepared for a
                // different sample rate exactly when this fails, and a block
                // of silence during that is right where blocking the audio
                // thread would not be.
                if (std::unique_lock<std::mutex> held(*processLock, std::try_to_lock); held.owns_lock())
                    plugin.processBlock(block, midiBuffer);
                else
                    block.clear();

                for (int ch = 0; ch < (int) s.outputChannels; ++ch)
                    if (ch < channels)
                        std::copy_n(block.getReadPointer(ch), numSamples, s.output[slot][ch]);
                    else
                        std::fill_n(s.output[slot][ch], numSamples, 0.0f);
            });
    }

private:
    link::ChildLink& lnk;
    juce::AudioPluginInstance& plugin;
    std::mutex* processLock;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiBuffer;
};

// --------------------------------------------------------------- the control

/** Reads newline-delimited JSON from the inherited descriptor and hands each
    request to the message thread. The same shape as the host's own agent
    socket, for the same reason: it is already understood, and a second
    protocol would be a second thing to get wrong. */
class ControlReader : public juce::Thread
{
public:
    std::function<void(juce::var)> onRequest;

    ControlReader() : juce::Thread("plugshell-host control") {}

    void run() override
    {
        juce::String pending;
        char buffer[4096];

        while (!threadShouldExit())
        {
            const auto n = ::read(controlFd, buffer, sizeof(buffer));

            if (n <= 0)
                break; // the host has gone; the audio loop will notice too

            pending += juce::String::fromUTF8(buffer, (int) n);

            for (;;)
            {
                const auto newline = pending.indexOfChar('\n');

                if (newline < 0)
                    break;

                const auto line = pending.substring(0, newline).trim();
                pending = pending.substring(newline + 1);

                if (line.isEmpty())
                    continue;

                juce::var parsed;

                if (juce::JSON::parse(line, parsed).wasOk() && onRequest != nullptr)
                {
                    const auto copy = parsed;
                    juce::MessageManager::callAsync([this, copy] { onRequest(copy); });
                }
            }
        }
    }
};

inline void writeLine(const juce::String& text)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock held(lock);

    const auto utf8 = (text + "\n").toStdString();
    size_t written = 0;

    while (written < utf8.size())
    {
        const auto n = ::write(controlFd, utf8.data() + written, utf8.size() - written);

        if (n <= 0)
            return; // the host is gone; there is nobody to tell

        written += (size_t) n;
    }
}

// ----------------------------------------------------------------- the child

class Application : public juce::JUCEApplication, private juce::AudioProcessorListener, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "plugshell-host"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    /** Takes nothing from the command line but the fact that it was started.

        The plugin's path arrives over the control channel instead, in an
        `open` request. A command line is a string, and a string has quoting
        rules: the first plugin this was tried against was called "Mini V3",
        and the space in that name is where the path came apart. A channel
        that carries typed values has no such problem, and this process
        already has one. */
    void initialise(const juce::String&) override
    {
        std::string error;

        if (!lnk.attach(sharedRegionFd, wakeupReadFd, error))
            return quitWith("could not attach to the host: " + juce::String(error));

        control.onRequest = [this](juce::var request) { handle(request); };
        control.startThread();

        startTimerHz(30);
        report("started", [](juce::DynamicObject& o) { o.setProperty("pid", (int) ::getpid()); });
    }

    void open(const juce::var& request)
    {
        if (plugin != nullptr)
            return;

        const auto path = request.getProperty("path", "").toString();
        const auto rate = (double) request.getProperty("rate", 0.0);
        const auto blockSize = (int) request.getProperty("block", 0);

        if (path.isEmpty())
            return quitWith("open needs a path");

        if (!openPlugin(path, rate > 0.0 ? rate : lnk.shared().sampleRate,
                        blockSize > 0 ? blockSize : (int) lnk.shared().maxBlockSize))
            return;

        worker = std::make_unique<AudioWorker>(lnk, *plugin, processLock);

        // Real-time priority, because this thread has the same deadline the
        // host's device callback has: it is inside it, one process over.
        worker->startRealtimeThread(juce::Thread::RealtimeOptions{}.withMaximumProcessingTimeMs(
            1000.0 * (double) lnk.shared().maxBlockSize / juce::jmax(1.0, lnk.shared().sampleRate)));

        lnk.announceReady();

        report("ready", [this](juce::DynamicObject& o) { describe(o); });
    }

    void shutdown() override
    {
        stopTimer();
        control.signalThreadShouldExit();

        // The editor first, then the instance: an editor outliving the
        // processor it is looking at is a crash, and this process crashing is
        // the thing the host is depending on not being contagious.
        attachedEditor.reset();

        if (plugin != nullptr)
        {
            plugin->removeListener(this);
            plugin->releaseResources();
        }

        if (worker != nullptr)
        {
            worker->stopThread(2000);
            worker.reset();
        }

        plugin.reset();
        control.stopThread(1000);
    }

    /** The host's wake pipe closed, so the audio loop returned. Nothing more
        is coming and there is no reason to stay. */
    void anotherInstanceStarted(const juce::String&) override {}

private:
    void quitWith(const juce::String& why)
    {
        report("error", [&why](juce::DynamicObject& o) { o.setProperty("error", why); });
        setApplicationReturnValue(2);
        quit();
    }

    bool openPlugin(const juce::String& path, double rate, int blockSize)
    {
        juce::addDefaultFormatsToManager(formats);

        juce::OwnedArray<juce::PluginDescription> found;
        juce::KnownPluginList list;

        for (auto* format : formats.getFormats())
            if (format->fileMightContainThisPluginType(path))
                list.scanAndAddFile(path, true, found, *format);

        if (found.isEmpty())
        {
            quitWith("nothing in " + path + " looks like a plugin this host can load");
            return false;
        }

        juce::String error;
        plugin = formats.createPluginInstance(*found[0], rate, blockSize, error);

        if (plugin == nullptr)
        {
            quitWith(error.isNotEmpty() ? error : "the plugin refused to load");
            return false;
        }

        auto& s = lnk.shared();
        plugin->setPlayConfigDetails((int) s.inputChannels, (int) s.outputChannels, rate, blockSize);
        plugin->prepareToPlay(rate, blockSize);

        textDirty = std::vector<std::atomic<bool>>((size_t) plugin->getParameters().size());
        plugin->addListener(this);

        return true;
    }

    // ------------------------------------------------------------- requests

    void handle(const juce::var& request)
    {
        const auto op = request.getProperty("op", "").toString();
        const auto id = request.getProperty("id", juce::var());

        const auto reply = [&id](std::function<void(juce::DynamicObject&)> fill)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("ok", true);

            if (!id.isVoid())
                o->setProperty("id", id);

            fill(*o);
            writeLine(juce::JSON::toString(juce::var(o), true));
        };

        if (op == "open")
            return open(request);

        if (plugin == nullptr)
            return reply([](juce::DynamicObject& o) { o.setProperty("error", "no plugin"); });

        if (op == "info")
            return reply([this](juce::DynamicObject& o) { describe(o); });

        if (op == "params")
            return reply([this, &request](juce::DynamicObject& o) { listParameters(o, request); });

        if (op == "set")
        {
            const int index = (int) request.getProperty("index", -1);
            const auto value = (float) (double) request.getProperty("value", 0.0);

            if (auto* p = parameterAt(index))
            {
                // The same gesture a person's hand makes, because a plugin
                // that only repaints its editor on a gesture will otherwise
                // take the value and carry on drawing the old position.
                p->beginChangeGesture();
                p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
                p->endChangeGesture();

                return reply(
                    [p](juce::DynamicObject& o)
                    {
                        o.setProperty("value", p->getValue());
                        o.setProperty("text", p->getCurrentValueAsText());
                    });
            }

            return reply([](juce::DynamicObject& o) { o.setProperty("error", "no such parameter"); });
        }

        if (op == "setMany")
        {
            // A whole plugin's worth at once, which is what a restart after a
            // crash needs: the host has every value and the plugin has none.
            auto* values = request.getProperty("values", juce::var()).getArray();
            int applied = 0;

            if (values != nullptr)
            {
                const auto& all = plugin->getParameters();

                for (int i = 0; i < juce::jmin(values->size(), all.size()); ++i)
                {
                    all[i]->beginChangeGesture();
                    all[i]->setValueNotifyingHost(
                        juce::jlimit(0.0f, 1.0f, (float) (double) values->getReference(i)));
                    all[i]->endChangeGesture();
                    ++applied;
                }
            }

            return reply([applied](juce::DynamicObject& o) { o.setProperty("applied", applied); });
        }

        if (op == "prepare")
        {
            const auto rate = (double) request.getProperty("rate", 48000.0);
            const int blockSize = juce::jlimit(1, link::maxBlock, (int) request.getProperty("block", 512));

            {
                const std::lock_guard<std::mutex> held(processLock);
                plugin->releaseResources();
                plugin->setNonRealtime((bool) request.getProperty("nonRealtime", false));
                plugin->prepareToPlay(rate, blockSize);
            }

            return reply(
                [rate, blockSize](juce::DynamicObject& o)
                {
                    o.setProperty("rate", rate);
                    o.setProperty("block", blockSize);
                });
        }

        if (op == "program")
        {
            plugin->setCurrentProgram((int) request.getProperty("index", 0));
            return reply([this](juce::DynamicObject& o)
                         { o.setProperty("currentProgram", plugin->getCurrentProgram()); });
        }

        if (op == "state")
        {
            juce::MemoryBlock block;
            plugin->getStateInformation(block);

            return reply([&block](juce::DynamicObject& o)
                         { o.setProperty("state", block.toBase64Encoding()); });
        }

        if (op == "setState")
        {
            juce::MemoryBlock block;

            if (block.fromBase64Encoding(request.getProperty("state", "").toString()))
                plugin->setStateInformation(block.getData(), (int) block.getSize());

            return reply([&block](juce::DynamicObject& o) { o.setProperty("bytes", (int) block.getSize()); });
        }

        if (op == "editor")
        {
            setEditorVisible((bool) request.getProperty("show", true));
            return reply([this](juce::DynamicObject& o) { describeEditor(o); });
        }

        // Where to be, which window to sit directly above, and whether to be
        // seen at all. Sent by the host whenever its window moves or is
        // reordered, and whenever it puts a panel of its own over the top --
        // which is often, so this one answers nothing.
        if (op == "place")
        {
            if (attachedEditor != nullptr)
                attachedEditor->place({(int) request.getProperty("x", 0), (int) request.getProperty("y", 0),
                                       (int) request.getProperty("width", 0),
                                       (int) request.getProperty("height", 0)},
                                      (bool) request.getProperty("show", true),
                                      (std::uint32_t) (juce::int64) request.getProperty("window", 0),
                                      (bool) request.getProperty("reorder", false));

            return;
        }

        if (op == "quit")
        {
            reply([](juce::DynamicObject& o) { o.setProperty("quitting", true); });
            quit();
            return;
        }

        reply([&op](juce::DynamicObject& o) { o.setProperty("error", "unknown op: " + op); });
    }

    juce::AudioProcessorParameter* parameterAt(int index) const
    {
        const auto& all = plugin->getParameters();
        return juce::isPositiveAndBelow(index, all.size()) ? all[index] : nullptr;
    }

    void describe(juce::DynamicObject& o) const
    {
        o.setProperty("name", plugin->getName());
        o.setProperty("parameterCount", plugin->getParameters().size());
        o.setProperty("programCount", plugin->getNumPrograms());
        o.setProperty("currentProgram", plugin->getCurrentProgram());
        o.setProperty("latencySamples", plugin->getLatencySamples());
        o.setProperty("acceptsMidi", plugin->acceptsMidi());
        o.setProperty("hasEditor", plugin->hasEditor());
        o.setProperty("pid", (int) ::getpid());

        juce::Array<juce::var> programs;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            programs.add(plugin->getProgramName(i));

        o.setProperty("programs", programs);
    }

    void listParameters(juce::DynamicObject& o, const juce::var& request) const
    {
        const auto& all = plugin->getParameters();
        const int offset = juce::jmax(0, (int) request.getProperty("offset", 0));
        const int limit = juce::jlimit(1, 20000, (int) request.getProperty("limit", 512));

        juce::Array<juce::var> items;

        for (int i = offset; i < juce::jmin(all.size(), offset + limit); ++i)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("index", i);
            entry->setProperty("name", all[i]->getName(64));
            entry->setProperty("label", all[i]->getLabel());
            entry->setProperty("value", all[i]->getValue());
            entry->setProperty("text", all[i]->getCurrentValueAsText());
            items.add(juce::var(entry));
        }

        o.setProperty("total", all.size());
        o.setProperty("offset", offset);
        o.setProperty("parameters", items);
    }

    void setEditorVisible(bool shouldShow)
    {
        if (!shouldShow)
            return attachedEditor.reset();

        if (attachedEditor != nullptr)
            return reportEditorSize();

        if (!plugin->hasEditor())
            return;

        if (auto* editor = plugin->createEditorIfNeeded())
        {
            attachedEditor = std::make_unique<AttachedEditor>(editor);
            attachedEditor->onSizeChanged = [this](int w, int h) { reportEditorSize(w, h); };

            // Created off-screen and unshown. The host has to size its
            // window around this before there is anywhere right to put it,
            // and a panel that appeared first would appear in the wrong place.
            reportEditorSize();
        }
    }

    /** How big the editor wants to be, which is the only thing the host
        cannot work out for itself. It answers with a `place`. */
    void reportEditorSize()
    {
        if (attachedEditor != nullptr)
            reportEditorSize(attachedEditor->size().x, attachedEditor->size().y);
    }

    void reportEditorSize(int w, int h)
    {
        report("editorSize",
               [w, h](juce::DynamicObject& o)
               {
                   o.setProperty("width", w);
                   o.setProperty("height", h);
               });
    }

    void describeEditor(juce::DynamicObject& o) const
    {
        o.setProperty("editorOpen", attachedEditor != nullptr);

        if (attachedEditor != nullptr)
        {
            const auto size = attachedEditor->size();
            o.setProperty("width", size.x);
            o.setProperty("height", size.y);
            o.setProperty("shown", attachedEditor->isShown());
        }
    }

    // ------------------------------------------------------- what we report

    void report(const juce::String& event, std::function<void(juce::DynamicObject&)> fill) const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("event", event);
        fill(*o);
        writeLine(juce::JSON::toString(juce::var(o), true));
    }

    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float value) override
    {
        // Into the ring rather than down the control channel: these arrive at
        // the rate a hand moves a knob, and the host wants them in order and
        // without a round trip.
        lnk.shared().toHost.push((std::uint32_t) index, value);

        // The text is a separate matter. Only the plugin can turn 0.31 into
        // "463 Hz", the host cannot work it out from the number, and a host
        // showing last week's text beside this second's value is worse than
        // showing no text. Marked here, sent from the timer, because this is
        // called from whichever thread moved the control.
        if (juce::isPositiveAndBelow(index, (int) textDirty.size()))
            textDirty[(size_t) index].store(true, std::memory_order_relaxed);
    }

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override
    {
        report("changed", [this](juce::DynamicObject& o) { describe(o); });
    }

    /** Parameter changes the host asked for, applied here with the gesture a
        hand would make. Drained on the message thread rather than in the
        audio callback because that is where a gesture may be made. */
    void timerCallback() override
    {
        link::ParamChange change{};
        int applied = 0;

        while (applied < 256 && lnk.shared().toChild.pop(change))
        {
            if (auto* p = parameterAt((int) change.index))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, change.value));
                p->endChangeGesture();
            }

            ++applied;
        }

        sendChangedText();

        // The audio loop returns when the host's pipe closes, and that is the
        // only signal that the host has gone. Nothing else is watched for.
        if (worker != nullptr && !worker->isThreadRunning())
            quit();

        // Before a plugin has been opened there is no audio loop, so the
        // control channel closing is the signal instead.
        if (worker == nullptr && !control.isThreadRunning())
            quit();
    }

    /** Up to this many per tick. A plugin whose every parameter changes at
        once -- a preset being loaded -- would otherwise put four thousand
        strings down a socket in one go, and the host has thirty-three
        milliseconds to be getting on with. */
    static constexpr int textPerTick = 96;

    void sendChangedText()
    {
        if (plugin == nullptr || textDirty.empty())
            return;

        juce::Array<juce::var> items;
        const auto& all = plugin->getParameters();

        for (size_t i = 0; i < textDirty.size() && items.size() < textPerTick; ++i)
        {
            if (!textDirty[i].exchange(false, std::memory_order_relaxed))
                continue;

            if ((int) i >= all.size())
                continue;

            auto* entry = new juce::DynamicObject();
            entry->setProperty("index", (int) i);
            entry->setProperty("text", all[(int) i]->getCurrentValueAsText());
            items.add(juce::var(entry));
        }

        if (items.isEmpty())
            return;

        report("text", [&items](juce::DynamicObject& o) { o.setProperty("items", items); });
    }

    link::ChildLink lnk;
    std::vector<std::atomic<bool>> textDirty;

    /** Held while the plugin is being prepared, tried while it is processing.
        The audio worker gives up a block rather than waiting for it. */
    std::mutex processLock;

    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<AudioWorker> worker;
    std::unique_ptr<AttachedEditor> attachedEditor;
    ControlReader control;
};

} // namespace plugshell::child

START_JUCE_APPLICATION(plugshell::child::Application)
