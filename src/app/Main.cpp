// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

#include "AnalyserTap.h"
#include "ChordName.h"
#include "KeyMonitor.h"
#include "Overlay.h"
#include "PluginIndex.h"
#include "QwertyKeys.h"
#include "ScopePanel.h"
#include "StripButton.h"
#include "TypeIcon.h"

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

/** One type scale for the whole application.

    Sizes were being chosen per call site, which produced nine of them and no
    relationship between any two. Four steps are enough here and keep panels
    consistent with the list and the strip. */
namespace size
{
constexpr float title = 15.0f; ///< window and panel headings
constexpr float body = 13.0f;  ///< list rows, primary labels
constexpr float label = 11.5f; ///< secondary text, values
constexpr float micro = 10.5f; ///< column headers, hints
} // namespace size

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
            g.setFont(theme::ui(theme::size::body));
            g.drawText(p.name, 16, y, 298, rowH, juce::Justification::centredLeft);

            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::label));
            g.drawText(p.vendor, 330, y, 200, rowH, juce::Justification::centredLeft);

            TypeIcon::draw(g, {544, y + 9, 15, 12}, p.category, theme::mute);
            g.drawText(p.category.isEmpty() ? juce::String("unknown") : p.category, 568, y, 110, rowH,
                       juce::Justification::centredLeft);

            g.setFont(theme::mono(theme::size::label));
            g.drawText("VST3", 690, y, 60, rowH, juce::Justification::centredLeft);
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

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double click, not single: loading a plugin runs third-party code and
        // can take seconds, so it should not happen on a stray click while
        // someone is scanning the list.
        const int i = e.y / rowH;
        if (i >= 0 && i < plugins.size() && onChoose)
            onChoose(plugins.getReference(i));
    }

    juce::MouseCursor getMouseCursor() override
    {
        return hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor;
    }

private:
    juce::Array<IndexedPlugin> plugins;
    int hovered = -1;
};

class ControlStrip : public juce::Component
{
public:
    std::function<void()> onBack;
    std::function<void()> onToggleKeys;
    std::function<void()> onHelp;
    std::function<void()> onSettings;
    std::function<void()> onScope;

    ControlStrip()
    {
        for (auto* b : {&back, &keys, &scope, &help, &settings})
        {
            b->setColours(theme::ink, theme::mute, theme::hair);
            addAndMakeVisible(b);
        }

        keys.setFramed(true);
        back.onClick = [this]
        {
            if (onBack)
                onBack();
        };
        keys.onClick = [this]
        {
            if (onToggleKeys)
                onToggleKeys();
        };
        help.onClick = [this]
        {
            if (onHelp)
                onHelp();
        };
        settings.onClick = [this]
        {
            if (onSettings)
                onSettings();
        };
        scope.onClick = [this]
        {
            if (onScope)
                onScope();
        };
        scope.setFramed(true);
        back.setVisible(false);
    }

    void setStatus(juce::String s, juce::String full = {}, juce::String medium = {},
                   juce::String shortForm = {})
    {
        status = std::move(s);
        right = std::move(full);
        rightMedium = medium.isEmpty() ? right : std::move(medium);
        rightShort = shortForm.isEmpty() ? rightMedium : std::move(shortForm);
        repaint();
    }

    void showBack(bool b) { back.setVisible(b); }

    void setScopeOpen(bool on) { scope.setToggled(on); }

    void setKeys(bool on, int octave)
    {
        keys.setToggled(on);
        keys.setText(on ? "KEYS  oct " + juce::String(octave) : "keys off");
    }

    /** What is being played right now, named. */
    void setPlaying(juce::String notes, juce::String chord)
    {
        playedNotes = std::move(notes);
        playedChord = std::move(chord);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::base);
        g.setColour(theme::hair);
        g.drawLine(0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

        // The strip is laid out as one row of regions rather than as several
        // independently positioned strings. Each piece drew itself wherever it
        // liked before, and any two of them collided as soon as the window was
        // narrow or a device name was long.
        auto row = getLocalBounds();
        row.removeFromRight(getWidth() - keys.getX()); // buttons own the right
        if (back.isVisible())
            row.removeFromLeft(back.getRight() + 12);
        else
            row.removeFromLeft(16);
        row.removeFromRight(12);

        const bool playing = playedChord.isNotEmpty();

        // Status on the left of what remains, device summary on the right, and
        // the summary is dropped entirely when there is no room for it.
        auto left = row.removeFromLeft(juce::jmax(0, row.getWidth() * 55 / 100));

        g.setColour(theme::ink);
        g.setFont(theme::ui(theme::size::body));
        g.drawText(playing ? playedChord : status, left, juce::Justification::centredLeft, true);

        if (playing)
        {
            g.setColour(theme::mute);
            g.setFont(theme::mono(theme::size::label));
            g.drawText(playedNotes, row, juce::Justification::centredRight, true);
        }
        else if (row.getWidth() > 40)
        {
            // Progressive rather than all-or-nothing: the full summary if it
            // fits, then the parts that matter most, and at the narrowest just
            // the latency, which is the number worth watching while playing.
            g.setColour(theme::mute);
            g.setFont(theme::mono(theme::size::label));

            const auto& text = row.getWidth() > 260 ? right : row.getWidth() > 150 ? rightMedium : rightShort;
            g.drawText(text, row, juce::Justification::centredRight, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        settings.setBounds(r.removeFromRight(74));
        help.setBounds(r.removeFromRight(56));
        scope.setBounds(r.removeFromRight(72).reduced(4, 0));
        keys.setBounds(r.removeFromRight(126).reduced(6, 0));
        back.setBounds(8, 0, 56, getHeight());
    }

private:
    StripButton back{"Back"}, keys{"keys off"}, scope{"Scope"}, help{"Help"}, settings{"Settings"};
    juce::String status{"Select a plugin"}, right, rightMedium, rightShort, playedNotes, playedChord;
};

/** JUCE's default widgets are light; this keeps the settings panel in the
    same palette as the rest of the app. */
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, theme::base);
        setColour(juce::Label::textColourId, theme::ink);
        setColour(juce::ComboBox::backgroundColourId, theme::raised);
        setColour(juce::ComboBox::textColourId, theme::ink);
        setColour(juce::ComboBox::outlineColourId, theme::hair);
        setColour(juce::ComboBox::arrowColourId, theme::mute);
        setColour(juce::PopupMenu::backgroundColourId, theme::raised);
        setColour(juce::PopupMenu::textColourId, theme::ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::hair);
        setColour(juce::TextButton::buttonColourId, theme::raised);
        setColour(juce::TextButton::textColourOffId, theme::ink);
        setColour(juce::ToggleButton::textColourId, theme::ink);
        setColour(juce::ToggleButton::tickColourId, theme::ink);
        setColour(juce::ToggleButton::tickDisabledColourId, theme::hair);
        setColour(juce::ListBox::backgroundColourId, theme::base);
        setColour(juce::ListBox::textColourId, theme::ink);
    }

    /** JUCE sizes its widgets for a light desktop app and they read as
        oversized next to the rest of this interface. */
    juce::Font getLabelFont(juce::Label&) override { return theme::ui(13.0f); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return theme::ui(13.0f); }
    juce::Font getPopupMenuFont() override { return theme::ui(13.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return theme::ui(13.0f); }
};

class MainComponent : public juce::Component, private juce::ComponentListener, private juce::Timer
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

        scopePanel = std::make_unique<ScopePanel>(tap, theme::base, theme::ink, theme::mute, theme::hair);
        scopePanel->onExtentChanged = [this]
        {
            if (editor != nullptr)
                fitWindowToEditor(/*force*/ true);
            else
                resized();
        };
        addAndMakeVisible(scopePanel.get());

        list.onChoose = [this](const IndexedPlugin& p) { load(p); };
        strip.onBack = [this] { unload(); };
        strip.onToggleKeys = [this] { setKeysEnabled(!keysEnabled); };
        strip.onHelp = [this] { showHelp(); };
        strip.onSettings = [this] { showSettings(); };
        strip.onScope = [this]
        {
            const bool willOpen = !scopePanel->isOpen();
            scopePanel->setOpen(willOpen);
            strip.setScopeOpen(willOpen);

            // The window grows to make room rather than the editor shrinking
            // to give it up. Squeezing the editor clips plugin controls, which
            // is the one thing this host promises not to do.
            if (editor != nullptr)
                fitWindowToEditor();
        };

        startAudio();

        // Watched at the platform layer rather than through focus: a plugin
        // editor is a native view, so once the user clicks inside it the host's
        // component tree stops seeing keys at all.
        keyMonitor.start([this](int c, bool down, bool repeat, bool cmd)
                         { return onKey(c, down, repeat, cmd); });

        const auto found = PluginIndex::scanDirectories();
        list.setItems(found);
        indexedCount = found.size();
        strip.setStatus(juce::String(found.size()) + " plugins indexed", audioSummary());

        setSize(920, 640);
    }

    /** Opens the analyser panel, for command-line and agent use. */
    void openScope()
    {
        if (scopePanel != nullptr && !scopePanel->isOpen())
        {
            scopePanel->setOpen(true);
            strip.setScopeOpen(true);
            if (editor != nullptr)
                fitWindowToEditor(true);
        }
    }

    /** Loads a plugin bundle by path, for command-line and agent use. */
    void loadFromPath(const juce::String& path)
    {
        const juce::File bundle(path);
        if (!bundle.exists())
            return;

        for (const auto& p : PluginIndex::scanDirectories())
            if (p.bundle == bundle)
                return load(p);

        IndexedPlugin p;
        p.bundle = bundle;
        p.name = bundle.getFileNameWithoutExtension();
        load(p);
    }

    ~MainComponent() override
    {
        // Order matters: detach from the audio thread before the plugin dies.
        devices.removeAudioCallback(&tap);
        devices.removeMidiInputDeviceCallback({}, &player);
        player.setProcessor(nullptr);
        unload();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme::base);

        g.setColour(theme::ink);
        g.setFont(theme::ui(theme::size::title));
        g.drawText("plugshell", 16, 12, 120, 22, juce::Justification::centredLeft);

        if (loadedName.isEmpty())
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::label));
            g.drawText("by wheatfox", 104, 13, 160, 20, juce::Justification::centredLeft);
        }

        if (loadedName.isNotEmpty())
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::body));
            g.drawText(loadedName, 110, 12, getWidth() - 140, 22, juce::Justification::centredLeft);
        }

        g.setColour(theme::hair);
        g.drawLine(16.0f, 42.0f, (float) getWidth() - 16.0f, 42.0f, 1.0f);

        if (viewport.isVisible())
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::micro));
            const int hy = 50;
            g.drawText("PLUGIN", 16, hy, 300, 20, juce::Justification::centredLeft);
            g.drawText("VENDOR", 330, hy, 200, 20, juce::Justification::centredLeft);
            g.drawText("TYPE", 544, hy, 110, 20, juce::Justification::centredLeft);
            g.drawText("FORMAT", 690, hy, 70, 20, juce::Justification::centredLeft);
            g.drawText("VERSION", getWidth() - 140, hy, 124, 20, juce::Justification::centredRight);
            g.setFont(theme::ui(theme::size::micro));
            g.drawText("double click to load", 16, hy, 300, 20, juce::Justification::centredRight);
            g.setColour(theme::hair);
            g.drawLine(16.0f, (float) hy + 22.0f, (float) getWidth() - 16.0f, (float) hy + 22.0f, 1.0f);
        }

        if (loading)
        {
            const auto a = contentArea();
            g.setColour(theme::ink);
            g.setFont(theme::ui(theme::size::body));
            g.drawText("Loading " + loadingName, a.withTrimmedBottom(40), juce::Justification::centred);

            // A determinate bar would be a lie: the plugin controls how long
            // this takes and reports no progress. This only says work is
            // happening and the app has not hung.
            const auto bar = juce::Rectangle<int>(a.getCentreX() - 90, a.getCentreY() + 18, 180, 2);
            g.setColour(theme::hair);
            g.fillRect(bar);
            g.setColour(theme::ink);
            const float t = (float) std::fmod(spinner, 1.0);
            const int w = 54;
            g.fillRect(bar.getX() + juce::roundToInt((bar.getWidth() - w) * t), bar.getY(), w, 2);
        }
        else if (instance != nullptr && editor == nullptr)
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::body));
            g.drawText("This plugin provides no editor", contentArea(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        positionOverlay();

        auto r = getLocalBounds();
        strip.setBounds(r.removeFromBottom(42));

        if (scopePanel != nullptr)
        {
            const int h = juce::roundToInt(scopeHeight * scopePanel->getExtent());
            scopePanel->setBounds(r.removeFromBottom(h));
            scopePanel->setVisible(h > 0);
        }
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
    static constexpr int scopeHeight = 190;

    juce::Rectangle<int> contentArea() const
    {
        const int scopeTaken =
            scopePanel != nullptr ? juce::roundToInt(scopeHeight * scopePanel->getExtent()) : 0;
        return getLocalBounds().withTrimmedTop(50).withTrimmedBottom(46 + scopeTaken);
    }

    /** The list gets margins; an embedded editor must not, or the window
        ends up larger than the plugin with dead space around it. */
    juce::Rectangle<int> listArea() const { return contentArea().withTrimmedTop(24).reduced(8, 0); }

    void showOverlay(std::unique_ptr<Overlay> o)
    {
        // A plugin editor is a native view, and native views composite above
        // anything the framework draws inside the same window regardless of
        // child order. The overlay therefore lives in its own always-on-top
        // window placed over this one, which is the only way to draw across an
        // embedded editor rather than around it.
        overlay = std::move(o);
        overlay->onDismiss = [this] { dismissOverlay(); };

        overlay->setOpaque(false);
        overlay->addToDesktop(juce::ComponentPeer::windowIsTemporary |
                              juce::ComponentPeer::windowIgnoresKeyPresses * 0);
        overlay->setAlwaysOnTop(true);
        positionOverlay();
        overlay->setVisible(true);
        overlay->toFront(true);
        overlay->grabKeyboardFocus();
    }

    void positionOverlay()
    {
        if (overlay == nullptr)
            return;

        // Screen coordinates, because the overlay is no longer a child.
        overlay->setBounds(getScreenBounds());
    }

    void dismissOverlay()
    {
        if (overlay != nullptr)
        {
            overlay->removeFromDesktop();
            overlay.reset();
        }
        repaint();
    }

    void showHelp()
    {
        if (overlay != nullptr)
            return dismissOverlay();

        auto o = std::make_unique<Overlay>("Help", theme::base, theme::ink, theme::mute, theme::hair);
        juce::Array<juce::StringArray> rows;
        rows.add({"PLUGINS"});
        rows.add({"double click", "load the plugin under the cursor"});
        rows.add({"Back", "unload and return to the list"});
        rows.add({"KEYBOARD AS MIDI"});
        rows.add({juce::String(juce::CharPointer_UTF8("\xe2\x8c\x98")) + "K",
                  "turn the computer keyboard on or off"});
        rows.add({"z x c v b n m , .", "white keys, lower octave"});
        rows.add({"s d   g h j   l ;", "black keys, lower octave"});
        rows.add({"q w e r t y u i o p", "white keys, upper octave"});
        rows.add({"2 3   5 6 7   9 0", "black keys, upper octave"});
        rows.add({"\\    /", "octave down, octave up"});
        rows.add({"NOTE"});
        rows.add({"", "The keyboard is off by default because plugin editors"});
        rows.add({"", "want the keyboard too, for typing values and searching."});
        o->setRows(rows);
        showOverlay(std::move(o));
    }

    void showSettings()
    {
        if (overlay != nullptr)
            return dismissOverlay();

        auto o = std::make_unique<Overlay>("Settings", theme::base, theme::ink, theme::mute, theme::hair);

        // JUCE's own selector rather than a hand-rolled one: it already knows
        // every device, rate and buffer size the system will accept, and gets
        // the validation right. A read-only list of what is currently in use
        // is not a settings panel.
        auto sel = std::make_unique<juce::AudioDeviceSelectorComponent>(devices,
                                                                        /*minInput*/ 0, /*maxInput*/ 0,
                                                                        /*minOutput*/ 2, /*maxOutput*/ 2,
                                                                        /*showMidiInput*/ true,
                                                                        /*showMidiOutput*/ false,
                                                                        /*showChannelsAsStereoPairs*/ true,
                                                                        /*hideAdvanced*/ false);

        sel->setLookAndFeel(&darkLookAndFeel);
        o->setContent(std::move(sel), 420);

        if (outputIsBluetooth())
            o->setFooter("Bluetooth output. Most of the " +
                         juce::String(juce::roundToInt(outputLatencyMs())) +
                         " ms comes from the wireless link, not the buffer: "
                         "AAC runs 80-160 ms and no host can shorten it. "
                         "A wired interface is the only real fix.");

        showOverlay(std::move(o));
    }

    void setKeysEnabled(bool on)
    {
        if (keysEnabled == on)
            return;

        keysEnabled = on;

        if (!on)
            allNotesOff();

        strip.setKeys(keysEnabled, octave);
    }

    /** Shows what is sounding and what it is called. Seeing the chord named
        is the point: the keyboard is for trying ideas, and an idea you cannot
        name is one you cannot write down afterwards. */
    void updatePlaying()
    {
        juce::Array<int> sorted;
        for (juce::HashMap<int, int>::Iterator it(sounding); it.next();)
            sorted.add(it.getValue());

        if (sorted.isEmpty())
        {
            strip.setPlaying({}, {});
            return;
        }

        std::sort(sorted.begin(), sorted.end());

        juce::StringArray names;
        for (const auto n : sorted)
        {
            static const char* nm[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            names.add(juce::String(nm[n % 12]) + juce::String(n / 12 - 1));
        }

        strip.setPlaying(names.joinIntoString(" "), ChordName::describe(sorted));
    }

    void allNotesOff()
    {
        for (juce::HashMap<int, int>::Iterator it(sounding); it.next();)
            sendNoteOff(it.getValue());

        sounding.clear();
        strip.setPlaying({}, {});
    }

    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    /** @return true when the event has been consumed. */
    bool onKey(int c, bool isDown, bool isRepeat, bool commandDown)
    {
        if (commandDown)
        {
            if (isDown && c == 'k')
            {
                setKeysEnabled(!keysEnabled);
                return true;
            }
            return false; // every other command shortcut belongs to the host or plugin
        }

        if (!keysEnabled || instance == nullptr)
            return false;

        // Auto-repeat would retrigger a held note at the system's repeat rate.
        if (isRepeat)
            return true;

        if (QwertyKeys::isOctaveKey(c))
        {
            if (isDown)
            {
                // Release everything first, or held notes are stranded at the
                // old pitch with no matching note-off.
                allNotesOff();
                octave = juce::jlimit(0, 9, octave + QwertyKeys::octaveShiftForKey(c));
                strip.setKeys(keysEnabled, octave);
            }
            return true;
        }

        const int offset = QwertyKeys::noteForKey((juce::juce_wchar) c);
        if (offset < 0)
            return false;

        // Tracked by character, not by pitch. A key-up can go missing — the
        // window loses focus mid-press, or the system consumes the event — and
        // with pitch-keyed bookkeeping that note stays marked as held and can
        // never sound again. The character is also the only stable identity
        // across an octave change, which alters the pitch a key produces
        // between its press and its release.
        if (isDown)
        {
            if (sounding.contains(c))
            {
                // A second key-down without a key-up means the release was
                // lost. Retrigger rather than ignore.
                sendNoteOff(sounding[c]);
                sounding.remove(c);
            }

            const int note = juce::jlimit(0, 127, octave * 12 + offset);
            sounding.set(c, note);
            player.getMidiMessageCollector().addMessageToQueue(
                juce::MidiMessage::noteOn(1, note, 0.8f).withTimeStamp(now()));
            updatePlaying();
        }
        else if (sounding.contains(c))
        {
            sendNoteOff(sounding[c]);
            sounding.remove(c);
            updatePlaying();
        }

        return true;
    }

    void sendNoteOff(int note)
    {
        player.getMidiMessageCollector().addMessageToQueue(
            juce::MidiMessage::noteOff(1, note).withTimeStamp(now()));
    }

    void timerCallback() override
    {
        spinner += 0.035;
        repaint(contentArea());
    }

    void componentMovedOrResized(juce::Component& c, bool, bool wasResized) override
    {
        if (wasResized && editor != nullptr && &c == editor.get())
            fitWindowToEditor();
    }

    void startAudio()
    {
        // Stereo out, no input. Asking for an input would raise the microphone
        // prompt for nothing, and on a Bluetooth headset it is worse than that:
        // opening the microphone makes macOS switch the device to the
        // hands-free profile, which drops it to mono at a much lower rate.
        const auto error = devices.initialiseWithDefaultDevices(0, 2);
        if (error.isNotEmpty())
            return;

        useSmallestBuffer();
        devices.addAudioCallback(&tap);

        for (const auto& in : juce::MidiInput::getAvailableDevices())
            devices.setMidiInputDeviceEnabled(in.identifier, true);

        devices.addMidiInputDeviceCallback({}, &player);
    }

    /** Asks for the shortest buffer the device offers.

        This is worth doing and worth not overselling. On a wired interface the
        buffer is most of the latency, so shortening it is most of the fix. On
        Bluetooth the transport contributes something like 80 to 160 ms that no
        host can influence, and the buffer is a rounding error beside it. The
        app therefore reports the total rather than implying it has solved
        anything. */
    void useSmallestBuffer()
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
            return;

        const auto sizes = device->getAvailableBufferSizes();
        if (sizes.isEmpty())
            return;

        // Not the absolute smallest: a buffer the machine cannot service in
        // time produces dropouts, which are worse than latency. 128 frames is
        // the usual floor for a software instrument under a general-purpose
        // scheduler.
        int chosen = sizes[sizes.size() - 1];
        for (const auto size : sizes)
            if (size >= 128)
            {
                chosen = size;
                break;
            }

        auto setup = devices.getAudioDeviceSetup();
        if (setup.bufferSize != chosen)
        {
            setup.bufferSize = chosen;
            devices.setAudioDeviceSetup(setup, true);
        }
    }

    /** Output latency in milliseconds, as the device reports it. */
    double outputLatencyMs() const
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
            return 0.0;

        const auto rate = device->getCurrentSampleRate();
        if (rate <= 0.0)
            return 0.0;

        const auto frames = device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples();
        return 1000.0 * (double) frames / rate;
    }

    bool outputIsBluetooth() const
    {
        auto* device = devices.getCurrentAudioDevice();
        return device != nullptr && device->getName().containsIgnoreCase("AirPods");
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
    void fitWindowToEditor(bool force = false)
    {
        if (editor == nullptr || (fitting && !force))
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
        // Follows the animation rather than the switch: keying this off
        // isOpen() made the window jump to its final height on the first frame
        // while the panel was still growing, which reads as content dropping
        // in from above instead of the panel pushing the window open.
        const int scopeTaken =
            scopePanel != nullptr ? juce::roundToInt(scopeHeight * scopePanel->getExtent()) : 0;

        setSize(juce::roundToInt(wantW * fit),
                juce::roundToInt(editor->getHeight() * fit) + chromeHeight + scopeTaken);
    }

    void finishLoading()
    {
        loading = false;
        stopTimer();
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void unload()
    {
        finishLoading();
        allNotesOff();
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

        loading = true;
        loadingName = p.name;
        spinner = 0.0;
        viewport.setVisible(false);
        setMouseCursor(juce::MouseCursor::WaitCursor);
        startTimerHz(30);
        strip.setStatus("Loading " + p.name + "...", audioSummary());
        repaint();

        // Hand the blocking work back to the message loop so the loading
        // state is painted first. Nested dispatch loops would do it sooner but
        // re-enter the app in the middle of a click, which is worse.
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync(
            [safe, p]
            {
                if (safe != nullptr)
                    safe->doLoad(p);
            });
    }

    void doLoad(const IndexedPlugin& p)
    {

        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : formats.getFormats())
            if (format->getName() == "VST3")
                format->findAllTypesForFile(found, p.bundle.getFullPathName());

        if (found.isEmpty())
        {
            finishLoading();
            viewport.setVisible(true);
            strip.setStatus("Could not read " + p.name, audioSummary());
            return;
        }

        juce::String error;
        instance = formats.createPluginInstance(*found[0], 48000.0, 512, error);

        if (instance == nullptr)
        {
            finishLoading();
            viewport.setVisible(true);
            strip.setStatus("Failed: " + error, audioSummary());
            return;
        }

        finishLoading();

        // The plugin is the only reliable source for this, so ask now that it
        // is running and keep the answer for the list.
        PluginIndex::remember(p.bundle,
                              instance->getPluginDescription().isInstrument ? "Instrument" : "Effect");

        loadedName = p.vendor + "  " + p.name;
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
    AnalyserTap tap{player};
    std::unique_ptr<ScopePanel> scopePanel;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    static constexpr int chromeHeight = 50 + 46;
    double editorScale = 1.0;
    bool fitting = false;
    bool keysEnabled = false;
    int octave = 4;
    bool loading = false;
    double spinner = 0.0;
    juce::String loadingName;
    int indexedCount = 0;
    std::unique_ptr<Overlay> overlay;
    juce::HashMap<int, int> sounding; ///< character -> sounding note
    KeyMonitor keyMonitor;
    DarkLookAndFeel darkLookAndFeel;
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

    void initialise(const juce::String& args) override
    {
        window = std::make_unique<Window>();

        // Loading from the command line is here because this host exists to be
        // driven by something other than a person. It is also the only way to
        // put the app into a known state for a screenshot.
        // The real argument vector, not the joined string: splitting that on
        // whitespace breaks every path containing a space, which is most of
        // them under /Library/Audio/Plug-Ins.
        juce::ignoreUnused(args);
        const auto tokens = getCommandLineParameterArray();
        const auto idx = tokens.indexOf("--load");

        auto* main = dynamic_cast<MainComponent*>(window->getContentComponent());
        if (main == nullptr)
            return;

        if (idx >= 0 && idx + 1 < tokens.size())
            main->loadFromPath(tokens[idx + 1]);

        if (tokens.contains("--scope"))
            main->openScope();
    }
    void shutdown() override { window.reset(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("plugshell", theme::base, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            // Not resizable: the window is sized to the plugin's editor, and an
            // editor does not scale to fit a window the user dragged. Plugins
            // that can be resized are resized from inside themselves, and the
            // host follows.
            setResizable(false, false);
            centreWithSize(920, 640);
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Window> window;
};

} // namespace plugshell

START_JUCE_APPLICATION(plugshell::Application)
