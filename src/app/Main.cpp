// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginIndex.h"

namespace plugshell
{

/** Dark only. A host sits behind plugin editors that are almost always dark;
    a light host frames every one of them in glare. */
namespace theme
{
const juce::Colour base{0xff161616};
const juce::Colour raised{0xff1e1e1e};
const juce::Colour hair{0xff2e2e2e};
const juce::Colour ink{0xffe4e4e4};
const juce::Colour mute{0xff7a7a7a};
const juce::Colour accent{0xffe4e4e4};

inline juce::Font ui(float h)
{
    return juce::Font(juce::FontOptions(h));
}
inline juce::Font mono(float h)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}
} // namespace theme

class PluginList : public juce::Component
{
public:
    static constexpr int rowH = 30;
    std::function<void(const IndexedPlugin&)> onChoose;

    void setItems(juce::Array<IndexedPlugin> items)
    {
        plugins = std::move(items);
        // The viewport scrolls this component, so it must be as tall as its
        // content rather than as tall as the window.
        setSize(getWidth(), juce::jmax(1, plugins.size() * rowH));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::base);

        for (int i = 0; i < plugins.size(); ++i)
        {
            const auto& p = plugins.getReference(i);
            const int y = i * rowH;

            if (i == hovered)
            {
                g.setColour(theme::raised);
                g.fillRect(0, y, getWidth(), rowH);
            }

            g.setColour(theme::ink);
            g.setFont(theme::ui(13.0f));
            g.drawText(p.name, 8, y, 306, rowH, juce::Justification::centredLeft);

            g.setColour(theme::mute);
            g.setFont(theme::ui(12.0f));
            g.drawText(p.vendor, 322, y, 200, rowH, juce::Justification::centredLeft);

            g.setFont(theme::mono(11.5f));
            g.drawText(p.version, getWidth() - 132, y, 124, rowH, juce::Justification::centredRight);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int i = e.y / rowH;
        const int next = (i >= 0 && i < plugins.size()) ? i : -1;
        if (next != hovered)
        {
            hovered = next;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = e.y / rowH;
        if (i >= 0 && i < plugins.size() && onChoose)
            onChoose(plugins.getReference(i));
    }

private:
    juce::Array<IndexedPlugin> plugins;
    int hovered = -1;
};

class ControlStrip : public juce::Component
{
public:
    std::function<void()> onBack;

    void setStatus(juce::String s, juce::String r = {})
    {
        status = std::move(s);
        right = std::move(r);
        repaint();
    }

    void showBack(bool b)
    {
        backVisible = b;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::base);
        g.setColour(theme::hair);
        g.drawLine(0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

        int x = 16;
        if (backVisible)
        {
            g.setColour(theme::ink);
            g.setFont(theme::ui(12.5f));
            g.drawText("Back", x, 0, 48, getHeight(), juce::Justification::centredLeft);
            x += 68;
        }

        g.setColour(theme::ink);
        g.setFont(theme::ui(12.5f));
        g.drawText(status, x, 0, getWidth() - x - 200, getHeight(), juce::Justification::centredLeft);

        g.setColour(theme::mute);
        g.setFont(theme::mono(11.5f));
        g.drawText(right.isEmpty() ? juce::String("no agent") : right, 0, 0, getWidth() - 16, getHeight(),
                   juce::Justification::centredRight);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (backVisible && e.x < 64 && onBack)
            onBack();
    }

private:
    juce::String status{"Select a plugin"};
    juce::String right;
    bool backVisible = false;
};

class MainComponent : public juce::Component, private juce::ComponentListener
{
public:
    MainComponent()
    {
        juce::addDefaultFormatsToManager(formats);

        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setColour(juce::ScrollBar::thumbColourId, theme::hair);
        addAndMakeVisible(viewport);
        addAndMakeVisible(strip);

        list.onChoose = [this](const IndexedPlugin& p) { load(p); };
        strip.onBack = [this] { unload(); };

        startAudio();

        const auto found = PluginIndex::scanDirectories();
        list.setItems(found);
        strip.setStatus(juce::String(found.size()) + " plugins indexed", audioSummary());

        setSize(920, 640);
    }

    ~MainComponent() override
    {
        // Order matters: detach from the audio thread before the plugin dies.
        devices.removeAudioCallback(&player);
        devices.removeMidiInputDeviceCallback({}, &player);
        player.setProcessor(nullptr);
        unload();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::base);

        g.setColour(theme::ink);
        g.setFont(theme::ui(14.5f));
        g.drawText("plugshell", 16, 12, 120, 22, juce::Justification::centredLeft);

        if (loadedName.isNotEmpty())
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(12.5f));
            g.drawText(loadedName, 110, 12, getWidth() - 140, 22, juce::Justification::centredLeft);
        }

        g.setColour(theme::hair);
        g.drawLine(16.0f, 42.0f, (float) getWidth() - 16.0f, 42.0f, 1.0f);

        if (viewport.isVisible())
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(10.5f));
            const int hy = 50;
            g.drawText("PLUGIN", 16, hy, 300, 20, juce::Justification::centredLeft);
            g.drawText("VENDOR", 330, hy, 200, 20, juce::Justification::centredLeft);
            g.drawText("VERSION", getWidth() - 140, hy, 124, 20, juce::Justification::centredRight);
            g.setColour(theme::hair);
            g.drawLine(16.0f, (float) hy + 22.0f, (float) getWidth() - 16.0f, (float) hy + 22.0f, 1.0f);
        }

        if (instance != nullptr && editor == nullptr)
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(13.0f));
            g.drawText("This plugin provides no editor", contentArea(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        strip.setBounds(r.removeFromBottom(42));
        const auto la = listArea();
        viewport.setBounds(la);
        list.setSize(la.getWidth() - (viewport.isVerticalScrollBarShown() ? 10 : 0),
                     juce::jmax(la.getHeight(), list.getHeight()));
        if (editor != nullptr)
        {
            // setTopLeftPosition works in unscaled coordinates, so divide the
            // target back out by the transform we applied.
            const auto a = contentArea();
            editor->setTopLeftPosition(juce::roundToInt(a.getX() / editorScale),
                                       juce::roundToInt(a.getY() / editorScale));
        }
    }

private:
    juce::Rectangle<int> contentArea() const
    {
        return getLocalBounds().withTrimmedTop(50).withTrimmedBottom(46);
    }

    /** The list gets margins; an embedded editor must not, or the window
        ends up larger than the plugin with dead space around it. */
    juce::Rectangle<int> listArea() const { return contentArea().withTrimmedTop(24).reduced(8, 0); }

    void componentMovedOrResized(juce::Component& c, bool, bool wasResized) override
    {
        if (wasResized && editor != nullptr && &c == editor.get())
            fitWindowToEditor();
    }

    void startAudio()
    {
        // Stereo out, no input: an instrument needs none and asking for one
        // triggers the microphone permission prompt for no reason.
        const auto error = devices.initialiseWithDefaultDevices(0, 2);
        if (error.isNotEmpty())
            return;

        devices.addAudioCallback(&player);

        // Accept every MIDI source, so a controller works without setup.
        for (const auto& in : juce::MidiInput::getAvailableDevices())
            devices.setMidiInputDeviceEnabled(in.identifier, true);

        devices.addMidiInputDeviceCallback({}, &player);
    }

    juce::String audioSummary() const
    {
        if (auto* d = devices.getCurrentAudioDevice())
            return d->getName() + "  " + juce::String(juce::roundToInt(d->getCurrentSampleRate())) +
                   " Hz  ·  " + juce::String(juce::MidiInput::getAvailableDevices().size()) + " midi in";
        return "no audio device";
    }

    /** Sizes the window to the editor, scaling down only if the editor is
        larger than the display. Plugin editors have a designed size and
        several are bigger than a laptop screen; clipping them silently, or
        forcing them into a fixed window, loses controls with no indication
        that anything is missing. */
    void fitWindowToEditor()
    {
        if (editor == nullptr || fitting)
            return;

        // Resizing the window makes the editor lay out again, which calls back
        // in here; without this guard the two chase each other.
        const juce::ScopedValueSetter<bool> guard(fitting, true);

        const auto work = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;

        const int wantW = editor->getWidth();
        const int wantH = editor->getHeight() + chromeHeight;

        const double fit = juce::jmin(1.0, (double) (work.getWidth() - 40) / (double) wantW,
                                      (double) (work.getHeight() - 60) / (double) wantH);

        editorScale = fit;
        editor->setTransform(fit < 1.0 ? juce::AffineTransform::scale((float) fit) : juce::AffineTransform());

        // setContentOwned(c, true) makes the window track the content's size,
        // so resize this component rather than the window.
        setSize(juce::roundToInt(wantW * fit), juce::roundToInt(editor->getHeight() * fit) + chromeHeight);
    }

    void unload()
    {
        player.setProcessor(nullptr);
        editorScale = 1.0;
        setSize(920, 640);

        if (editor != nullptr)
        {
            editor->removeComponentListener(this);
            removeChildComponent(editor.get());
            editor.reset();
        }
        instance.reset();
        loadedName = {};
        viewport.setVisible(true);
        strip.showBack(false);
        strip.setStatus("Select a plugin", audioSummary());
        repaint();
    }

    void load(const IndexedPlugin& p)
    {
        unload();
        strip.setStatus("Loading " + p.name + "...");
        repaint();

        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : formats.getFormats())
            if (format->getName() == "VST3")
                format->findAllTypesForFile(found, p.bundle.getFullPathName());

        if (found.isEmpty())
        {
            strip.setStatus("Could not read " + p.name);
            return;
        }

        juce::String error;
        instance = formats.createPluginInstance(*found[0], 48000.0, 512, error);

        if (instance == nullptr)
        {
            strip.setStatus("Failed: " + error);
            return;
        }

        loadedName = p.vendor + "  " + p.name;
        viewport.setVisible(false);
        strip.showBack(true);

        if (auto* e = instance->createEditorIfNeeded())
        {
            editor.reset(e);
            addAndMakeVisible(editor.get());
            // Several plugins let the user resize their own editor. When that
            // happens the editor changes its bounds and the host has to follow,
            // or the window no longer matches what the plugin is drawing.
            editor->addComponentListener(this);
            fitWindowToEditor();
        }

        player.setProcessor(instance.get());

        strip.setStatus(juce::String(instance->getParameters().size()) + " parameters", audioSummary());
        repaint();
    }

    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    static constexpr int chromeHeight = 50 + 46;
    double editorScale = 1.0;
    bool fitting = false;
    juce::String loadedName;
    PluginList list;
    juce::Viewport viewport;
    ControlStrip strip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class Application : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "plugshell"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("plugshell", theme::base, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            centreWithSize(920, 640);
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Window> window;
};

} // namespace plugshell

START_JUCE_APPLICATION(plugshell::Application)
