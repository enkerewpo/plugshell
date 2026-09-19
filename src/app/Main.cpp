// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <iostream>

#include "AgentBadge.h"
#include "AnalyserTap.h"
#include "ChordName.h"
#include "ControlServer.h"
#include "Eased.h"
#include "EditorProbe.h"
#include "HostPlayHead.h"
#include "KeyMonitor.h"
#include "KeyboardMap.h"
#include "OfflineRender.h"
#include "OutputMeter.h"
#include "Overlay.h"
#include "Patch.h"
#include "PluginIndex.h"
#include "QwertyKeys.h"
#include "RemotePlugin.h"
#include "ScopePanel.h"
#include "StripButton.h"
#include "TempoField.h"
#include "TypeIcon.h"

namespace plugshell
{

/** Dark only. A host sits behind plugin editors that are almost always dark;
    a light host frames every one of them in glare. */
namespace theme
{
/** The palette, which is a variable rather than a constant because the
    application has two of them.

    Named for what a colour is for rather than for what it looks like, so the
    same names work in both: `base` is whatever the page sits on and `ink` is
    whatever is written on it, dark on light or light on dark. Nothing outside
    this block should know which way round it currently is. */
inline juce::Colour base{0xff161616};
inline juce::Colour raised{0xff1e1e1e};
inline juce::Colour hair{0xff2e2e2e};
inline juce::Colour ink{0xffe4e4e4};
inline juce::Colour mute{0xff7a7a7a};
inline juce::Colour accent{0xffe4e4e4};

enum class Mode
{
    dark,
    light,
    automatic ///< follows macOS
};

inline Mode mode = Mode::dark;

/** Whether macOS is currently in dark mode. */
bool systemPrefersDark();

inline bool isDark()
{
    return mode == Mode::automatic ? systemPrefersDark() : mode == Mode::dark;
}

struct Palette
{
    juce::Colour base, raised, hair, ink, mute;
};

inline Palette paletteFor(bool dark)
{
    if (dark)
        return {juce::Colour{0xff161616}, juce::Colour{0xff1e1e1e}, juce::Colour{0xff2e2e2e},
                juce::Colour{0xffe4e4e4}, juce::Colour{0xff7a7a7a}};

    // Not an inversion. Pure white glares under a plugin editor and the
    // hairlines vanish into it, so the page is a shade off white and the rules
    // sit darker against it than their dark-mode counterparts sit light.
    return {juce::Colour{0xfff4f4f3}, juce::Colour{0xffe9e9e7}, juce::Colour{0xffcfcfcb},
            juce::Colour{0xff1c1c1b}, juce::Colour{0xff77776f}};
}

inline void useMix(const Palette& from, const Palette& to, float t)
{
    base = from.base.interpolatedWith(to.base, t);
    raised = from.raised.interpolatedWith(to.raised, t);
    hair = from.hair.interpolatedWith(to.hair, t);
    ink = from.ink.interpolatedWith(to.ink, t);
    mute = from.mute.interpolatedWith(to.mute, t);
    accent = ink;
}

inline void apply()
{
    const auto p = paletteFor(isDark());
    useMix(p, p, 0.0f);
}

inline void applyOld()
{
    if (isDark())
    {
        base = juce::Colour{0xff161616};
        raised = juce::Colour{0xff1e1e1e};
        hair = juce::Colour{0xff2e2e2e};
        ink = juce::Colour{0xffe4e4e4};
        mute = juce::Colour{0xff7a7a7a};
    }
    else
    {
        // Not an inversion. Pure white glares under a plugin editor and the
        // hairlines disappear, so the page is a shade off and the rules are
        // darker relative to it than their dark-mode counterparts are light.
        base = juce::Colour{0xfff4f4f3};
        raised = juce::Colour{0xffe9e9e7};
        hair = juce::Colour{0xffcfcfcb};
        ink = juce::Colour{0xff1c1c1b};
        mute = juce::Colour{0xff77776f};
    }

    accent = ink;
}

/** One type scale for the whole application.

    Sizes were being chosen per call site, which produced nine of them and no
    relationship between any two. Four steps are enough here and keep panels
    consistent with the list and the strip. */
namespace size
{
// Raised across the board. The old scale was chosen to keep the chrome out of
// the way of the plugin, and went far enough that it was hard to read -- which
// is the opposite of out of the way.
constexpr float title = 16.5f; ///< window and panel headings
constexpr float body = 14.0f;  ///< list rows, primary labels
constexpr float label = 12.5f; ///< secondary text, values
constexpr float micro = 11.5f; ///< column headers, hints
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

    const juce::Array<IndexedPlugin>& getItems() const { return plugins; }

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

        // One highlight that travels, rather than a fill switched on under
        // each row in turn. Moving it is what makes a list of eighty-nine
        // things feel like one surface instead of eighty-nine of them.
        if (glow.get() > 0.001f)
        {
            g.setColour(theme::raised.withMultipliedAlpha(glow.get()));
            g.fillRect(0, juce::roundToInt(highlightY.get()), getWidth(), rowH);
        }

        for (int i = 0; i < plugins.size(); ++i)
        {
            const auto& p = plugins.getReference(i);
            const int y = i * rowH;

            juce::ignoreUnused(i);

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

            if (hovered >= 0)
            {
                // Jumped into place when arriving from nowhere, so the
                // highlight does not fly across the whole list to meet the
                // cursor; slid when moving between rows.
                if (glow.getTarget() < 0.5f)
                    highlightY.snapTo((float) (hovered * rowH));
                else
                    highlightY.setTarget((float) (hovered * rowH));

                glow.setTarget(1.0f);
            }
            else
            {
                glow.setTarget(0.0f);
            }

            anim.nudge();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = -1;
        glow.setTarget(0.0f);
        anim.nudge();
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
    Eased highlightY{0.0f}, glow{0.0f};

    Animator anim{[this]
                  {
                      const bool moving = highlightY.advance(0.42f) | glow.advance(0.30f);
                      if (moving)
                          repaint();
                      return moving;
                  }};

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
    std::function<void()> onTransport;
    std::function<void(double bpm, int upper, int lower)> onTempo;
    std::function<void(float)> onGain;

    ControlStrip()
    {
        for (auto* b : {&back, &keys, &scope, &help, &settings, &transport})
        {
            b->setColours(theme::ink, theme::mute, theme::hair);
            addAndMakeVisible(b);
        }

        keys.setFramed(true);
        back.setGlyph(StripButton::Glyph::back);

        transport.setFramed(true);
        transport.setTooltip("Start and stop");
        transport.setGlyph(StripButton::Glyph::play);
        transport.onClick = [this]
        {
            if (onTransport)
                onTransport();
        };
        tempo.setTooltip("Drag for a nudge, click to type: \"94\", \"6/8\" or \"94 6/8\".");
        tempo.onChange = [this](double bpm, int upper, int lower)
        {
            if (onTempo)
                onTempo(bpm, upper, lower);
        };
        addAndMakeVisible(tempo);

        output.onGainChange = [this](float g)
        {
            if (onGain)
                onGain(g);
        };
        addAndMakeVisible(output);

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

    static constexpr int rowHeight = 42;

    /** A line of text needs a line of text, not a second row of buttons.

        Wrapping used to give the status a whole 42-point row of its own,
        which is why the strip looked like two strips. This is the height of
        the words and nothing else. */
    static constexpr int textRowHeight = 24;

    /** What the buttons take, measured rather than guessed.

        The widths are in one place so that adding a control cannot quietly
        make the row overflow somewhere else: whatever is added here moves the
        wrap point with it. */
    static constexpr int backWidth = 86, transportWidth = 38, tempoWidth = 92, scopeWidth = 64,
                         helpWidth = 52, settingsWidth = 72, outputWidest = 132, outputNarrowest = 62;

    /** The status text and the device summary read together -- what is loaded,
        and what is playing it -- so this is the width at which both are still
        legible beside the buttons rather than the width at which one of them
        starts to be truncated. */
    static constexpr int textWanted = 330;

    /** Everything between Back and the master fader, all of it fixed. */
    static constexpr int fixedButtonsWidth(int keysWidth)
    {
        return transportWidth + tempoWidth + keysWidth + scopeWidth + helpWidth + settingsWidth;
    }

    static constexpr int buttonsWidth(int keysWidth)
    {
        return backWidth + fixedButtonsWidth(keysWidth) + outputWidest;
    }

    /** How tall the strip needs to be at a given width.

        Every control stays present at every width. What gives way is the
        arrangement: below the point where the words and the buttons both fit
        on one line, the words take a line of their own rather than being
        squeezed into whatever the buttons left over -- which is what turned
        the name of the output device into an ellipsis. */
    static int heightFor(int width, bool withBack = true)
    {
        const int needed = buttonsWidth(wideKeysWidth) - (withBack ? 0 : backWidth) + textWanted;
        return width < needed ? rowHeight + textRowHeight : rowHeight;
    }

    void refreshColours()
    {
        for (auto* b : {&back, &keys, &scope, &help, &settings, &transport})
            b->setColours(theme::ink, theme::mute, theme::hair);

        tempo.setColours(theme::ink, theme::mute, theme::hair);
        output.setColours(theme::ink, theme::mute, theme::hair);

        repaint();
    }

    bool isBackShowing() const { return back.isVisible(); }

    /** Showing Back can change how tall the strip needs to be, and the strip
        does not own its own height. Asking the parent to lay out again is the
        only way the two do not disagree until something else happens to
        resize the window. */
    void showBack(bool b)
    {
        if (b == back.isVisible())
            return;

        back.setVisible(b);

        if (auto* parent = getParentComponent())
            parent->resized();
    }

    /** The meter reads the tap directly rather than being pushed values: the
        audio thread already publishes them, and a second copy on the message
        thread would only be a slower version of the same numbers. */
    void setOutputSource(const AnalyserTap* t) { output.setSource(t); }

    void setGain(float linear) { output.setGain(linear); }

    void setScopeOpen(bool on) { scope.setToggled(on); }

    /** Tempo and metre, which the plugins that sync to a clock need and which
        the host has to be asked for -- there is no DAW here to provide it. */
    void setTransport(bool playing, double bpm, int upper, int lower)
    {
        transport.setToggled(playing);
        transport.setGlyph(playing ? StripButton::Glyph::stop : StripButton::Glyph::play);
        tempo.setValue(bpm, upper, lower);
    }

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
        // On two rows the text has a row to itself and needs no dodging; on
        // one it has to keep clear of the buttons and of Back.
        auto row = textRow;
        if (!twoRows)
        {
            row = getLocalBounds();
            // From the leftmost of the right-hand buttons, not from a
            // particular one of them. Naming `keys` worked until something was
            // added to its left, and then the status text ran underneath the
            // new control.
            int leftmost = getWidth();
            for (const juce::Component* b :
                 {(const juce::Component*) &transport, (const juce::Component*) &tempo,
                  (const juce::Component*) &keys, (const juce::Component*) &scope,
                  (const juce::Component*) &help, (const juce::Component*) &settings,
                  (const juce::Component*) &output})
                if (b->isVisible())
                    leftmost = juce::jmin(leftmost, b->getX());

            row.removeFromRight(getWidth() - leftmost); // buttons own the right
            if (back.isVisible())
                row.removeFromLeft(back.getRight() + 12);
            else
                row.removeFromLeft(16);
            row.removeFromRight(12);
        }

        const bool playing = playedChord.isNotEmpty();

        // Status on the left of what remains, device summary on the right.
        // The split follows what the status actually needs rather than a fixed
        // share of the width, so a long device name cannot squeeze a short
        // status into nothing, and a long status still yields once the summary
        // is down to its shortest form.
        g.setFont(theme::ui(theme::size::body));
        const int statusWanted = juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                     theme::ui(theme::size::body), playing ? playedChord : status)) +
                                 12;

        auto left = row.removeFromLeft(
            juce::jlimit(0, row.getWidth(), juce::jmin(statusWanted, row.getWidth() * 70 / 100)));

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

            // Measured against the space that is actually left, not against
            // the width before the status took its share -- which is why the
            // longest form used to be chosen and then truncated.
            const auto fits = [&](const juce::String& t)
            {
                return juce::GlyphArrangement::getStringWidth(theme::mono(theme::size::label), t) <=
                       (float) row.getWidth();
            };

            const auto& text = fits(right) ? right : fits(rightMedium) ? rightMedium : rightShort;
            g.setFont(theme::mono(theme::size::label));
            g.drawText(text, row, juce::Justification::centredRight, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        twoRows = getHeight() > rowHeight + 4;

        auto text = twoRows ? r.removeFromTop(textRowHeight) : juce::Rectangle<int>();
        auto buttons = r;

        // Back takes its width before anything else does.
        //
        // It used to be laid out last, after the master fader had taken what
        // it wanted from the other end, and the fader's width has a floor --
        // so on a narrow strip the fader kept its 62 points and Back was left
        // with whatever was over, which was 28 points and the word "Bac".
        // The control that has to be findable without being read cannot be
        // the one that runs out of room.
        back.setBounds(buttons.removeFromLeft(back.isVisible() ? backWidth : 0).reduced(6, twoRows ? 2 : 0));

        // The keys button carries the octave number and wants the room, but
        // not at the cost of squeezing the fader to its floor. Measured
        // against what is actually left rather than against the window: a
        // plugin with a narrow editor gets a narrow strip whatever the
        // display is doing.
        const int keysWidth = buttons.getWidth() >= fixedButtonsWidth(wideKeysWidth) + outputNarrowest
                                  ? wideKeysWidth
                                  : narrowKeysWidth;

        settings.setBounds(buttons.removeFromRight(settingsWidth));
        help.setBounds(buttons.removeFromRight(helpWidth));
        scope.setBounds(buttons.removeFromRight(scopeWidth).reduced(4, 0));
        keys.setBounds(buttons.removeFromRight(keysWidth).reduced(6, 0));
        tempo.setBounds(buttons.removeFromRight(tempoWidth).reduced(4, 0));
        transport.setBounds(buttons.removeFromRight(transportWidth).reduced(3, 0));

        // Last in the row and so first to run out of room. It shrinks rather
        // than disappearing, because it is not only a reading: it is the
        // master volume, and a plugin with a narrow editor is not a reason to
        // leave someone unable to turn the sound down. On two rows it can
        // have everything left over; on one it has to leave the status text
        // somewhere to go.
        const int room = buttons.getWidth() - (twoRows ? 0 : 40);
        output.setBounds(
            buttons.removeFromRight(juce::jlimit(outputNarrowest, outputWidest, room)).reduced(4, 0));

        textRow = twoRows ? text.withTrimmedLeft(16).withTrimmedRight(16) : juce::Rectangle<int>();
    }

private:
    static constexpr int narrowKeysWidth = 104, wideKeysWidth = 126;

    bool twoRows = false;
    juce::Rectangle<int> textRow;
    StripButton back{"Back"}, keys{"keys off"}, scope{"Scope"}, help{"Help"}, settings{"Settings"},
        transport{{}};
    TempoField tempo;
    OutputMeter output;
    juce::String status{"Select a plugin"}, right, rightMedium, rightShort, playedNotes, playedChord;
};

/** JUCE's default widgets are light; this keeps the settings panel in the
    same palette as the rest of the app. */
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel() { refresh(); }

    /** The palette is copied into the look and feel, so switching themes has
        to put it back. */
    void refresh()
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

    /** JUCE opens a menu by putting its window on screen, fully drawn, in one
        frame. This is the hook it offers just before that happens, which is
        the only place a fade can be started without reimplementing the menu.

        Short on purpose: a menu is opened to be read, and anything long
        enough to notice is time spent not reading it. */
    void preparePopupMenuWindow(juce::Component& window) override
    {
        window.setAlpha(0.0f);
        juce::Desktop::getInstance().getAnimator().fadeIn(&window, 80);
    }

    /** JUCE sizes its widgets for a light desktop app and they read as
        oversized next to the rest of this interface. */
    juce::Font getLabelFont(juce::Label&) override { return theme::ui(13.0f); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return theme::ui(13.0f); }
    juce::Font getPopupMenuFont() override { return theme::ui(13.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return theme::ui(13.0f); }
};

/** What the About panel holds. Links have to be real controls rather than
    painted text, or they are decoration that looks like a link. */
class AboutContent : public juce::Component
{
public:
    static constexpr int preferredHeight = 176;

    AboutContent()
    {
        blurb.setText("An agent-operable VST3 plugin host: parameters, offline rendering, an image "
                      "of the plugin's editor, and synthetic input into the controls that are "
                      "not parameters.",
                      juce::dontSendNotification);
        blurb.setJustificationType(juce::Justification::topLeft);
        blurb.setColour(juce::Label::textColourId, theme::mute);
        blurb.setFont(theme::ui(theme::size::label));
        addAndMakeVisible(blurb);

        for (auto* link : {&site, &repo})
        {
            link->setColour(juce::HyperlinkButton::textColourId, theme::ink);
            link->setJustificationType(juce::Justification::centredLeft);
            link->setFont(theme::ui(theme::size::body), false, juce::Justification::centredLeft);
            addAndMakeVisible(link);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        blurb.setBounds(r.removeFromTop(66));
        r.removeFromTop(14);
        site.setBounds(r.removeFromTop(26));
        r.removeFromTop(6);
        repo.setBounds(r.removeFromTop(26));
    }

private:
    juce::Label blurb;
    juce::HyperlinkButton site{"www.oscommunity.cn", juce::URL("https://www.oscommunity.cn")};
    juce::HyperlinkButton repo{"github.com/enkerewpo/plugshell",
                               juce::URL("https://github.com/enkerewpo/plugshell")};
};

class MainComponent : public juce::Component,
                      private juce::ComponentListener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent()
    {
        // Before anything is constructed that copies the palette.
        loadSettings();
        theme::apply();

        juce::addDefaultFormatsToManager(formats);

        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setColour(juce::ScrollBar::thumbColourId, theme::hair);
        addAndMakeVisible(viewport);
        addAndMakeVisible(strip);

        scopePanel = std::make_unique<ScopePanel>(tap, theme::base, theme::ink, theme::mute, theme::hair);
        addChildComponent(agentBadge);
        addAndMakeVisible(themeButton);
        themeButton.setColours(theme::ink, theme::mute, theme::hair);
        themeButton.onClick = [this] { cycleTheme(); };
        refreshThemeButton();

        scopePanel->onClick = [this] { showScopeDetail(); };
        scopePanel->onExtentChanged = [this]
        {
            if (hostedEditorSize().y > 0)
                refitWindow(/*force*/ true);
            else
                resized();
        };
        addAndMakeVisible(scopePanel.get());

        list.onChoose = [this](const IndexedPlugin& p) { load(p); };
        strip.onBack = [this] { confirmUnload(); };
        strip.onToggleKeys = [this] { setKeysEnabled(!keysEnabled); };
        strip.onHelp = [this] { showHelp(); };
        strip.onSettings = [this] { showSettings(); };
        strip.onTransport = [this]
        {
            playHead.setPlaying(!playHead.isPlaying());

            // From the top each time it starts. A transport that resumes from
            // wherever it was left is a DAW's job; here the point is to hear a
            // synced effect from a known place.
            if (playHead.isPlaying())
                playHead.rewind();

            refreshTransport();
        };

        strip.onTempo = [this](double bpm, int upper, int lower)
        {
            playHead.setTempo(bpm);
            playHead.setTimeSignature(upper, lower);
            refreshTransport();
        };
        strip.onScope = [this]
        {
            // An editor that already fills the display has no room to grow
            // into, and growing anyway only clips it -- so for those the
            // analyser opens over the whole window instead of under it. Same
            // button, same instrument, the one that fits.
            if (!scopePanel->isOpen() && hostedEditorSize().y > 0 && !roomForInlineScope())
                return showScopeDetail();

            const bool willOpen = !scopePanel->isOpen();
            scopePanel->setOpen(willOpen);
            strip.setScopeOpen(willOpen);

            // The window grows to make room rather than the editor shrinking
            // to give it up. Squeezing the editor clips plugin controls, which
            // is the one thing this host promises not to do.
            refitWindow();
        };

        strip.setOutputSource(&tap);
        strip.setGain(masterGain);
        tap.setGain(masterGain);

        strip.onGain = [this](float g)
        {
            masterGain = g;
            tap.setGain(g);

            // Written on change rather than only on quit. The level people
            // want kept is the one they were listening at, and a host that
            // crashes -- which is the failure this project is built around --
            // is exactly the case where "on quit" never runs.
            saveSettings();
        };

        tap.setPlayHead(&playHead);
        startAudio();
        devices.addChangeListener(this);

        // Watched at the platform layer rather than through focus: a plugin
        // editor is a native view, so once the user clicks inside it the host's
        // component tree stops seeing keys at all.
        keyMonitor.start([this](int c, int code, bool down, bool repeat, bool cmd)
                         { return onKey(c, code, down, repeat, cmd); });

        stuckKeyWatchdog.tick = [this] { releaseKeysNoLongerHeld(); };

        const auto found = PluginIndex::scanDirectories();
        list.setItems(found);
        indexedCount = found.size();
        setStripStatus(juce::String(found.size()) + " plugins indexed");

        applyTheme();
        refreshTransport();
        setSize(920, 640);
    }

    /** How tall whichever editor is loaded wants to be, or zero for none.

        The window is sized around the plugin, and which process the plugin is
        in does not change that -- so everything that measures the editor asks
        this rather than picking one of the two pointers and quietly getting
        the other case wrong. */
    juce::Point<int> hostedEditorSize() const
    {
        if (editor != nullptr)
            return {editor->getWidth(), editor->getHeight()};

        return remoteEditorSize;
    }

    /** Whether the window can grow by the analyser's height without the
        editor losing anything off the bottom of the display. */
    bool roomForInlineScope() const
    {
        const auto wanted = hostedEditorSize();

        if (wanted.y <= 0)
            return true;

        const auto work =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        return wanted.y + chromeHeight() + scopeHeight <= work.getHeight() - 60;
    }

    /** The inline strip is a glance; this is the look. It covers the window
        rather than growing it, so the size of the detailed view does not
        depend on how much display is left over, and the keyboard keeps
        playing underneath -- watching the trace while playing is the whole
        reason to have it. */
    void showScopeDetail()
    {
        auto o = std::make_unique<Overlay>("Analyser", theme::base, theme::ink, theme::mute, theme::hair);
        o->setPanelWidth(juce::jmax(560, getWidth() - 60));

        auto big = std::make_unique<ScopePanel>(tap, theme::base, theme::ink, theme::mute, theme::hair);
        big->setDetailed(true);
        big->setOpen(true);

        o->setContent(std::move(big), juce::jmax(280, getHeight() - 220));
        o->setFooter("Keys keep playing while this is open.");
        showOverlay(std::move(o));
    }

    /** Answers one request from the control socket. Runs on the message
        thread, so it may touch the plugin and the editor freely. */
    /**
        What a request is, in words a person reading the header can use.

        Named from the operation rather than described by the caller. A caller
        supplying its own wording would eventually supply wording that is not
        true, and the one thing this badge cannot afford is to be a place where
        a program says whatever it likes about what it is doing to the user's
        machine. Where a name is worth having -- which plugin, which parameter
        -- it is taken from the request's own arguments, which are the thing
        actually being acted on.
    */
    juce::String describeActivity(const juce::String& op, const juce::var& request) const
    {
        if (op == "load")
            return "loading " +
                   juce::File(request.getProperty("path", "").toString()).getFileNameWithoutExtension();

        if (op == "set")
        {
            if (auto* p = hosted() != nullptr ? findParameter(request) : nullptr)
                return "setting " + p->getName(40);

            return "setting a parameter";
        }

        if (op == "unload")
            return "closing the plugin";
        if (op == "params")
            return "reading parameters";
        if (op == "note")
            return "playing a note";
        if (op == "render")
            return "rendering audio";
        if (op == "capture")
            return "looking at the editor";
        if (op == "click" || op == "move")
            return "clicking in the editor";
        if (op == "drag")
            return "dragging in the editor";
        if (op == "scroll")
            return "scrolling the editor";
        if (op == "record")
            return request.getProperty("action", "").toString() == "learn" ? "learning this patch"
                                                                           : "recording a patch";
        if (op == "patch")
            return "writing a patch";
        if (op == "replay")
            return "replaying a patch";
        if (op == "program" || op == "programs")
            return "changing programs";
        if (op == "transport")
            return "moving the transport";
        if (op == "state" || op == "permissions" || op == "hosting" || op == "plugins")
            return "looking around";

        return "driving";
    }

    juce::var handleControl(const juce::var& request)
    {
        const auto op = request.getProperty("op", "").toString();

        // Every request, not only the one that introduces itself. An agent
        // that never says who it is still gets a badge, because the question
        // the badge answers is "is something driving this", and the answer is
        // yes whether or not it gave a name.
        agentBadge.sawRequest(request.getProperty("agent", agentName).toString(),
                              describeActivity(op, request));

        if (op == "identify")
        {
            // A name to show, and nothing else. Kept for the session rather
            // than per request, so a caller says it once.
            if (request.hasProperty("agent"))
                agentName = request.getProperty("agent", "").toString();

            agentBadge.sawRequest(agentName, "connecting");

            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("agent", agentBadge.getAgent());
                    o.setProperty("doing", agentBadge.getActivity());
                    o.setProperty("showing", agentBadge.isActive());
                });
        }

        if (op == "state")
            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("loaded", hosted() != nullptr);
                    o.setProperty("name", loadedName);
                    o.setProperty("hasEditor", editor != nullptr);
                    o.setProperty("keysEnabled", keysEnabled);
                    o.setProperty("theme", theme::mode == theme::Mode::dark    ? "dark"
                                           : theme::mode == theme::Mode::light ? "light"
                                                                               : "auto");
                    o.setProperty("themeIsDark", theme::isDark());
                    o.setProperty("overlayOpen", overlay != nullptr);
                    o.setProperty("agent", agentBadge.getAgent());
                    o.setProperty("agentDoing", agentBadge.getActivity());
                    o.setProperty("agentSecondsSinceRequest", agentBadge.secondsSinceRequest());
                    o.setProperty("lastKeyChar", lastKeyChar);
                    o.setProperty("themeBase", theme::base.toDisplayString(false));
                    o.setProperty("settingsFile", settingsFile().getFullPathName());

                    const auto work = juce::Desktop::getInstance()
                                          .getDisplays()
                                          .getPrimaryDisplay()
                                          ->userBounds.toNearestInt();
                    o.setProperty("workArea", work.toString());

                    if (auto* top = getTopLevelComponent())
                        o.setProperty("windowBounds", top->getScreenBounds().toString());
                    o.setProperty("octave", octave);
                    o.setProperty("masterGain", masterGain);

                    const auto levels = tap.getLevels();
                    o.setProperty("outputRmsL", levels.rms[0]);
                    o.setProperty("outputRmsR", levels.rms[1]);
                    o.setProperty("outputPeakL", levels.peak[0]);
                    o.setProperty("outputPeakR", levels.peak[1]);
                    o.setProperty("outputClipped", levels.clipped);
                    if (auto* p = hosted())
                    {
                        o.setProperty("parameterCount", p->getParameters().size());
                        o.setProperty("programCount", p->getNumPrograms());
                        o.setProperty("currentProgram", p->getCurrentProgram());
                    }

                    o.setProperty("outOfProcess", outOfProcess);
                    if (remote != nullptr)
                    {
                        o.setProperty("pluginPid", remote->getChildPid());
                        o.setProperty("linkUnderruns", (int) remote->getUnderruns());
                    }
                    if (editor != nullptr)
                    {
                        o.setProperty("editorWidth", editor->getWidth());
                        o.setProperty("editorHeight", editor->getHeight());
                        o.setProperty("editorScale", editorScale);

                        // Where the editor actually is, so a caller can check its
                        // own arithmetic instead of estimating from a screenshot.
                        const auto b = editor->getScreenBounds();
                        o.setProperty("editorScreenX", b.getX());
                        o.setProperty("editorScreenY", b.getY());
                        o.setProperty("editorScreenW", b.getWidth());
                        o.setProperty("editorScreenH", b.getHeight());
                    }
                });

        if (op == "permissions")
        {
            if ((bool) request.getProperty("request", false))
                EditorProbe::requestScreenRecordingPermission();

            if ((bool) request.getProperty("request", false))
                EditorProbe::requestAccessibilityPermission();

            return okWith(
                [](juce::DynamicObject& o)
                {
                    o.setProperty("accessibility", EditorProbe::hasAccessibilityPermission());
                    o.setProperty("screenRecording", EditorProbe::hasScreenRecordingPermission());
                    o.setProperty("note", "macOS shows its prompt only once; after a refusal, grant it in "
                                          "System Settings > Privacy & Security > Screen Recording, then "
                                          "restart plugshell");
                });
        }

        if (op == "transport")
        {
            if (request.hasProperty("bpm"))
                playHead.setTempo((double) request.getProperty("bpm", 120.0));

            if (request.hasProperty("numerator") || request.hasProperty("denominator"))
                playHead.setTimeSignature(
                    (int) request.getProperty("numerator", playHead.getNumerator()),
                    (int) request.getProperty("denominator", playHead.getDenominator()));

            if (request.hasProperty("playing"))
                playHead.setPlaying((bool) request.getProperty("playing", false));

            if ((bool) request.getProperty("rewind", false))
                playHead.rewind();

            strip.repaint();

            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("bpm", playHead.getTempo());
                    o.setProperty("numerator", playHead.getNumerator());
                    o.setProperty("denominator", playHead.getDenominator());
                    o.setProperty("playing", playHead.isPlaying());
                    o.setProperty("ppq", playHead.getPpq());
                });
        }

        if (op == "note")
        {
            if (hosted() == nullptr)
                return error("no plugin loaded");

            if ((bool) request.getProperty("allOff", false))
            {
                allNotesOff();
                return okWith([](juce::DynamicObject& o) { o.setProperty("allOff", true); });
            }

            const int note = juce::jlimit(0, 127, (int) request.getProperty("note", 60));
            const float velocity =
                juce::jlimit(0.0f, 1.0f, (float) (double) request.getProperty("velocity", 0.8));
            const int durationMs = (int) request.getProperty("durationMs", 0);
            const bool wantOn = (bool) request.getProperty("on", true);

            if (!wantOn)
            {
                sendNoteOff(note);
                return okWith([note](juce::DynamicObject& o) { o.setProperty("note", note); });
            }

            player.getMidiMessageCollector().addMessageToQueue(
                juce::MidiMessage::noteOn(1, note, velocity).withTimeStamp(now()));

            // A held note and a struck one are both wanted, and which is which
            // is the caller's business: with no duration the note stays down
            // until something turns it off, which is what a sustained probe
            // needs, and with one it releases itself, which is what measuring
            // a single hit needs without a second round trip timed by hand.
            if (durationMs > 0)
            {
                const auto ref = juce::Component::SafePointer<MainComponent>(this);

                juce::Timer::callAfterDelay(durationMs,
                                            [ref, note]
                                            {
                                                if (ref != nullptr)
                                                    ref->sendNoteOff(note);
                                            });
            }

            return okWith(
                [&](juce::DynamicObject& o)
                {
                    o.setProperty("note", note);
                    o.setProperty("velocity", velocity);
                    o.setProperty("durationMs", durationMs);
                    o.setProperty("held", durationMs <= 0);
                });
        }

        // ----------------------------------------------------- patches

        if (op == "record")
        {
            const auto action = request.getProperty("action", "status").toString();

            if (action == "start")
            {
                if (hosted() == nullptr)
                    return error("no plugin loaded");

                startRecording();
                return okWith([this](juce::DynamicObject& o) { describeRecording(o); });
            }

            if (action == "stop")
            {
                stopRecording();

                // A recorded session gets the same fingerprint a learned one
                // does. It is the recording that most needs it: a session can
                // contain pointer operations, and a pointer operation is the
                // kind that reproduces or does not.
                if ((bool) request.getProperty("probe", true) && hosted() != nullptr)
                    recording.sound = measureSound(60, 2.0);

                return okWith([this](juce::DynamicObject& o) { describeRecording(o); });
            }

            if (action == "mark")
            {
                if (editor == nullptr)
                    return error("marking needs an editor in this process");

                if (!recordingActive)
                    return error("nothing is being recorded");

                pollRecording(); // close off whatever the gestures moved

                return markDestination(regionFor(request, recording.operations.empty()
                                                              ? juce::Point<float>()
                                                              : recording.operations.back().at),
                                       request.getProperty("why", "").toString());
            }

            if (action == "learn")
            {
                if (hosted() == nullptr)
                    return error("no plugin loaded");

                stopRecording();
                return learnFromInitial(request.getProperty("from", "").toString(),
                                        (bool) request.getProperty("probe", true));
            }

            if (action == "status")
            {
                if (recordingActive)
                    pollRecording();

                return okWith([this](juce::DynamicObject& o) { describeRecording(o); });
            }

            return error("record needs \"action\": start, stop, mark, learn or status");
        }

        if (op == "patch")
        {
            const auto path = request.getProperty("path", "").toString();

            // Written out, and read back as a check that what was written can
            // be parsed. A patch that only this process can understand is not
            // a format, it is a memory dump with punctuation.
            if (path.isEmpty())
            {
                if (recordingActive)
                    pollRecording();

                return okWith([this](juce::DynamicObject& o) { o.setProperty("patch", recording.toVar()); });
            }

            if (recordingActive)
                pollRecording();

            const juce::File file(path);

            if (!file.getParentDirectory().exists() || !file.replaceWithText(recording.toJSON()))
                return error("could not write " + path);

            return okWith(
                [this, &path](juce::DynamicObject& o)
                {
                    o.setProperty("path", path);
                    o.setProperty("operations", (int) recording.operations.size());
                });
        }

        if (op == "replay")
        {
            if (hosted() == nullptr)
                return error("no plugin loaded");

            juce::String problem;
            patch::Patch loaded;

            if (request.hasProperty("patch"))
                loaded = patch::Patch::fromVar(request.getProperty("patch", juce::var()), problem);
            else
            {
                const auto path = request.getProperty("path", "").toString();
                const juce::File file(path);

                if (!file.existsAsFile())
                    return error("no patch at " + path);

                loaded = patch::Patch::fromJSON(file.loadFileAsString(), problem);
            }

            if (problem.isNotEmpty())
                return error(problem);

            return replayPatch(loaded, (bool) request.getProperty("force", false));
        }

        if (op == "hosting")
        {
            if (request.hasProperty("outOfProcess"))
                setOutOfProcess((bool) request.getProperty("outOfProcess", false));

            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("outOfProcess", outOfProcess);
                    o.setProperty("appliesToNextLoad", true);
                    o.setProperty("helper", helperExecutable().getFullPathName());
                    o.setProperty("helperPresent", helperExecutable().existsAsFile());

                    if (remote != nullptr)
                    {
                        o.setProperty("running", remote->isAlive());
                        o.setProperty("pid", remote->getChildPid());
                        o.setProperty("underruns", (int) remote->getUnderruns());
                    }

                    o.setProperty("note", "out-of-process survives a plugin crash and costs one buffer of "
                                          "latency; the plugin's editor is drawn by the child process "
                                          "and held over this window's content area");
                });
        }

        if (op == "output")
        {
            if (request.hasProperty("gain"))
                setMasterGain((float) (double) request.getProperty("gain", 1.0));

            // Decibels as well as a multiplier, because the number a caller
            // has is usually the one it wants to change by, and doing the
            // conversion here means it is done the same way every time.
            if (request.hasProperty("db"))
                setMasterGain(juce::Decibels::decibelsToGain((float) (double) request.getProperty("db", 0.0),
                                                             OutputMeter::floorDb));

            return okWith(
                [this](juce::DynamicObject& o)
                {
                    const auto levels = tap.getLevels();

                    o.setProperty("gain", masterGain);
                    o.setProperty("db", juce::Decibels::gainToDecibels(masterGain, OutputMeter::floorDb));
                    o.setProperty("rmsL", levels.rms[0]);
                    o.setProperty("rmsR", levels.rms[1]);
                    o.setProperty("peakL", levels.peak[0]);
                    o.setProperty("peakR", levels.peak[1]);
                    o.setProperty("peakDb",
                                  juce::Decibels::gainToDecibels(juce::jmax(levels.peak[0], levels.peak[1]),
                                                                 OutputMeter::floorDb));
                    o.setProperty("clipped", levels.clipped);
                    o.setProperty("note", "gain is post-plugin and pre-device; it does not affect "
                                          "the render op, which reports the plugin's own output");
                });
        }

        if (op == "plugins")
        {
            juce::Array<juce::var> items;
            for (const auto& p : list.getItems())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("name", p.name);
                o->setProperty("vendor", p.vendor);
                o->setProperty("version", p.version);
                o->setProperty("category", p.category);
                o->setProperty("path", p.bundle.getFullPathName());
                items.add(juce::var(o));
            }
            return okWith([&items](juce::DynamicObject& o) { o.setProperty("plugins", items); });
        }

        if (op == "load")
        {
            const auto path = request.getProperty("path", "").toString();
            if (path.isEmpty())
                return error("load needs a path");
            loadFromPath(path);
            return okWith([](juce::DynamicObject&) {});
        }

        if (op == "unload")
        {
            unload();
            return okWith([](juce::DynamicObject&) {});
        }

        if (hosted() == nullptr)
            return error("no plugin is loaded");

        if (op == "params")
        {
            // Paged and searchable, not a dump. Pigments publishes 4446
            // parameters; asking for all of them at once is slow to build,
            // slow to send, and not what a caller wanted anyway -- the real
            // question is almost always "which parameter is the filter
            // cutoff", which is a search.
            const auto& ps = hosted()->getParameters();
            const auto search = request.getProperty("search", "").toString();
            const int offset = juce::jmax(0, (int) request.getProperty("offset", 0));
            const int limit = juce::jlimit(1, 2000, (int) request.getProperty("limit", 200));

            juce::Array<int> matching;
            for (int i = 0; i < ps.size(); ++i)
                if (search.isEmpty() || ps[i]->getName(96).containsIgnoreCase(search))
                    matching.add(i);

            juce::Array<juce::var> items;
            for (int n = offset; n < juce::jmin(matching.size(), offset + limit); ++n)
            {
                const int i = matching[n];
                auto* o = new juce::DynamicObject();
                o->setProperty("index", i);
                o->setProperty("name", ps[i]->getName(96));
                o->setProperty("label", ps[i]->getLabel());
                o->setProperty("value", ps[i]->getValue());
                o->setProperty("text", ps[i]->getCurrentValueAsText());
                o->setProperty("steps", ps[i]->getNumSteps());
                o->setProperty("automatable", ps[i]->isAutomatable());
                items.add(juce::var(o));
            }

            return okWith(
                [&](juce::DynamicObject& o)
                {
                    o.setProperty("total", ps.size());
                    o.setProperty("matched", matching.size());
                    o.setProperty("offset", offset);
                    o.setProperty("params", items);
                });
        }

        if (op == "set")
        {
            auto* p = findParameter(request);
            if (p == nullptr)
                return error("no such parameter");

            const auto value = (float) (double) request.getProperty("value", -1.0);
            if (value < 0.0f || value > 1.0f)
                return error("value must be between 0 and 1");

            // Through the host-facing gesture calls, not setValue alone: a
            // plugin that only repaints its editor on a gesture would take the
            // change and go on drawing the old position.
            p->beginChangeGesture();
            p->setValueNotifyingHost(value);
            p->endChangeGesture();

            // A plugin in another process has not been asked yet what the new
            // value reads as, and answering with the old text would look like
            // the change had not taken.
            if (remote != nullptr)
                remote->refreshParameter(p->getParameterIndex());

            recordSet(*p);

            return okWith(
                [p](juce::DynamicObject& o)
                {
                    o.setProperty("value", p->getValue());
                    o.setProperty("text", p->getCurrentValueAsText());
                });
        }

        if (op == "programs")
        {
            juce::Array<juce::var> items;
            for (int i = 0; i < hosted()->getNumPrograms(); ++i)
                items.add(hosted()->getProgramName(i));
            return okWith([&items](juce::DynamicObject& o) { o.setProperty("programs", items); });
        }

        if (op == "program")
        {
            const int index = (int) request.getProperty("index", -1);
            if (index < 0 || index >= hosted()->getNumPrograms())
                return error("program index out of range");
            hosted()->setCurrentProgram(index);
            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("currentProgram", hosted()->getCurrentProgram());
                    o.setProperty("name", hosted()->getProgramName(hosted()->getCurrentProgram()));
                });
        }

        if (op == "render")
        {
            const auto path = request.getProperty("path", "").toString();
            if (path.isEmpty())
                return error("render needs a path");

            OfflineRender::Options o;
            o.sampleRate = (double) request.getProperty("sampleRate", 48000.0);
            o.blockSize = (int) request.getProperty("blockSize", 512);
            o.durationSec = (double) request.getProperty("durationSec", 2.0);
            o.note = (int) request.getProperty("note", 60);
            o.velocity = (float) (double) request.getProperty("velocity", 0.8);
            o.noteOffSec = (double) request.getProperty("noteOffSec", 1.0);

            const auto in = request.getProperty("input", "").toString();
            if (in.isNotEmpty())
                o.input = juce::File(in);

            // Detached first: the plugin cannot be driven by the device and
            // rendered offline at the same time, and leaving the player
            // attached would have the audio thread calling processBlock on an
            // instance being prepared for another sample rate underneath it.
            player.setProcessor(nullptr);

            // A plugin in another process renders one block behind, which is
            // the right trade against a device deadline and the wrong one for
            // a measurement. Nothing is on the audio thread now, so for the
            // length of the render the link is allowed to wait for each block
            // it asked for.
            if (remote != nullptr)
                remote->setOfflineMode(true);

            auto* device = devices.getCurrentAudioDevice();
            const auto r = OfflineRender::run(
                *hosted(), juce::File(path), o, device != nullptr ? device->getCurrentSampleRate() : 48000.0,
                device != nullptr ? device->getCurrentBufferSizeSamples() : 512);

            if (remote != nullptr)
                remote->setOfflineMode(false);

            player.setProcessor(hosted());

            if (!r.ok)
                return error(r.error);

            return okWith(
                [&r, &path](juce::DynamicObject& o)
                {
                    o.setProperty("path", path);
                    o.setProperty("channels", r.channels);
                    o.setProperty("frames", (juce::int64) r.frames);
                    o.setProperty("peak", r.peak);
                    o.setProperty("rms", r.rms);
                    o.setProperty("renderedInSec", r.renderedInSec);
                });
        }

        if (editor == nullptr)
            return error("the plugin has no editor open");

        if (op == "capture")
        {
            const auto path = request.getProperty("path", "").toString();
            if (path.isEmpty())
                return error("capture needs a path");

            const auto r = EditorProbe::capture(*editor, juce::File(path));
            if (!r.ok)
                return error(r.error);

            return okWith(
                [&r, &path](juce::DynamicObject& o)
                {
                    o.setProperty("path", path);
                    o.setProperty("width", r.width);
                    o.setProperty("height", r.height);
                    o.setProperty("scale", r.scale);
                    o.setProperty("method", r.used == EditorProbe::CaptureMethod::viewCache ? "viewCache"
                                                                                            : "windowServer");
                });
        }

        if (op == "click" || op == "drag" || op == "scroll" || op == "move")
            return handlePointer(op, request);

        return error("unknown op: " + op);
    }

    /** Writes a PNG of the plugin's editor exactly as it appears on screen.

        The point of this is that a parameter list is not the plugin. Which
        wavetable is selected, what the modulation matrix routes where, which
        page is showing -- none of it is a parameter, all of it is on screen,
        and an image is the only representation that contains it. */
    juce::String captureEditor(const juce::File& destination)
    {
        if (editor == nullptr)
            return "no editor is open";

        const auto r = EditorProbe::capture(*editor, destination);

        if (!r.ok)
            return "capture failed: " + r.error;

        return "captured " + juce::String(r.width) + "x" + juce::String(r.height) + " at " +
               juce::String(r.scale, 2) + "x via " +
               (r.used == EditorProbe::CaptureMethod::viewCache ? "view cache" : "window server") + " -> " +
               destination.getFullPathName();
    }

    /** Pointer operations address the editor in its own coordinates, so a
        caller works in the same space the capture gave it and does not have to
        know where the window is or what scale it is drawn at. Fractions are
        accepted too, since an agent reasoning about an image it was handed
        usually knows a position as a proportion of the picture. */
    juce::var handlePointer(const juce::String& op, const juce::var& request)
    {
        const auto point = [this, &request](const char* key) -> juce::Point<float>
        {
            const auto v = request.getProperty(key, juce::var());
            if (!v.isArray() || v.size() < 2)
                return {-1.0f, -1.0f};

            auto x = (float) (double) v[0], y = (float) (double) v[1];

            if ((bool) request.getProperty("normalised", request.getProperty("normalized", false)))
            {
                x *= (float) editor->getWidth();
                y *= (float) editor->getHeight();
            }

            return {x, y};
        };

        const auto at = point("at");
        if (at.x < 0.0f)
            return error(op + " needs \"at\": [x, y]");

        const auto why = request.getProperty("why", "").toString();

        // Real mouse events, only when asked for. They move the user's cursor
        // and go to whatever window is in front, so they are the caller's
        // deliberate choice for an editor the polite path cannot reach, and
        // never the default.
        const juce::ScopedValueSetter<bool> delivery(EditorProbe::useSystemEvents,
                                                     (bool) request.getProperty("system", false));

        if (op == "scroll")
        {
            const auto delta = (float) (double) request.getProperty("delta", 1.0);
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::scroll, at, delta);
            recordPointer(patch::Operation::Kind::scroll, at, {}, delta, why, regionFor(request, at));
            return okWith([](juce::DynamicObject&) {});
        }

        if (op == "move")
        {
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::move, at);
            return okWith([&at](juce::DynamicObject& o)
                          { o.setProperty("at", juce::Array<juce::var>{at.x, at.y}); });
        }

        if (op == "click")
        {
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::move, at);
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::down, at);
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::up, at);
            recordPointer(patch::Operation::Kind::click, at, {}, 0.0f, why, regionFor(request, at));
            return okWith([](juce::DynamicObject&) {});
        }

        const auto to = point("to");
        if (to.x < 0.0f)
            return error("drag needs \"to\": [x, y]");

        EditorProbe::drag(*editor, at, to, (int) request.getProperty("steps", 24));
        recordPointer(patch::Operation::Kind::drag, at, to, 0.0f, why, regionFor(request, to));
        return okWith([](juce::DynamicObject&) {});
    }

    // ------------------------------------------------------------ recording

    /**
        Watches a session and writes down what was done to the plugin.

        Parameter changes are found by diffing, not by listening. A listener
        would be the obvious route and it is the wrong one here: the two
        hosting paths deliver changes by different mechanisms -- a plugin in
        this process notifies JUCE, and one in the child process has its
        values mirrored across a ring -- and a recorder built on either would
        quietly record nothing in the other. A diff of the values this host
        can see is the same operation in both, and it is also the only way to
        catch a change made by hand in the editor, which is the case this
        whole format is for.

        The cost of diffing is missing a parameter that moved and moved back
        inside one tick. That is the right thing to lose.
    */
    /**
        The plugin as it constructs itself, kept so a patch can be a path from
        it.

        This is the anchor the whole format hangs on: replay starts from a
        freshly instantiated plugin, so what a patch has to carry is the
        difference from that and nothing else. It is also the one starting
        point that is determinate, identical on every machine, and nobody's
        data -- which is why it is this rather than a named factory preset or
        a saved blob.
    */
    /**
        Renders one note and reduces it to a level, so a patch can be checked
        against its own sound rather than only against its own numbers.

        To a temporary file, deleted immediately: what is wanted is the two
        figures the render already computes, and a caller who wants the audio
        has the render operation for that.
    */
    patch::Sound measureSound(int note, double durationSec)
    {
        patch::Sound s;
        s.note = note;
        s.durationSec = durationSec;

        if (hosted() == nullptr)
            return s;

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("plugshell-probe-" +
                                            juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");

        OfflineRender::Options o;
        o.durationSec = durationSec;
        o.note = note;
        o.noteOffSec = durationSec * 0.5;

        player.setProcessor(nullptr);

        if (remote != nullptr)
            remote->setOfflineMode(true);

        auto* device = devices.getCurrentAudioDevice();
        const auto r = OfflineRender::run(*hosted(), file, o,
                                          device != nullptr ? device->getCurrentSampleRate() : 48000.0,
                                          device != nullptr ? device->getCurrentBufferSizeSamples() : 512);

        if (remote != nullptr)
            remote->setOfflineMode(false);

        player.setProcessor(hosted());
        file.deleteFile();

        if (r.ok)
        {
            s.measured = true;
            s.peak = r.peak;
            s.rms = r.rms;
        }

        return s;
    }

    /**
        What a rectangle of the editor looks like right now, as a signature.

        Through the same capture the agent surface already offers, written to
        a temporary file and read back, rather than a second platform-specific
        path into the window server. It costs a file per check and it is only
        done once per operation, which buys one implementation of "what does
        this editor look like" instead of two that can disagree.
    */
    bool lookAt(juce::Rectangle<int> regionInEditor, patch::Look& out)
    {
        if (editor == nullptr || regionInEditor.isEmpty())
            return false;

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("plugshell-look-" +
                                            juce::String(juce::Random::getSystemRandom().nextInt()) + ".png");

        const auto captured = EditorProbe::capture(*editor, file);

        if (!captured.ok)
        {
            file.deleteFile();
            return false;
        }

        const auto image = juce::ImageFileFormat::loadFrom(file);
        file.deleteFile();

        if (!image.isValid())
            return false;

        // The capture is in backing pixels and the region is in the editor's
        // own points, which are not the same on a retina display.
        const auto scale = captured.scale > 0.0 ? captured.scale : 1.0;
        const juce::Rectangle<int> inPixels(juce::roundToInt(regionInEditor.getX() * scale),
                                            juce::roundToInt(regionInEditor.getY() * scale),
                                            juce::roundToInt(regionInEditor.getWidth() * scale),
                                            juce::roundToInt(regionInEditor.getHeight() * scale));

        out.region = regionInEditor;
        out.cells = patch::signatureOf(image, inPixels);
        return !out.cells.empty();
    }

    /** The region a pointer operation says to watch, or a box around the point
        it touched. The caller's own rectangle is the useful one -- what a
        click on a preset arrow changes is the name box beside it, not the
        arrow -- and the default is there so an operation is never recorded
        with no evidence at all. */
    juce::Rectangle<int> regionFor(const juce::var& request, juce::Point<float> at) const
    {
        if (auto* r = request.getProperty("region", juce::var()).getArray(); r != nullptr && r->size() >= 4)
        {
            const bool fraction =
                (bool) request.getProperty("normalised", request.getProperty("normalized", false));
            const auto w = editor != nullptr ? (float) editor->getWidth() : 1.0f;
            const auto h = editor != nullptr ? (float) editor->getHeight() : 1.0f;
            const auto sx = fraction ? w : 1.0f, sy = fraction ? h : 1.0f;

            return {juce::roundToInt((float) (double) r->getReference(0) * sx),
                    juce::roundToInt((float) (double) r->getReference(1) * sy),
                    juce::roundToInt((float) (double) r->getReference(2) * sx),
                    juce::roundToInt((float) (double) r->getReference(3) * sy)};
        }

        return juce::Rectangle<int>(160, 44).withCentre(at.roundToInt());
    }

    void rememberInitialState()
    {
        initialValues = hosted() != nullptr ? patch::valuesOf(*hosted()) : std::vector<float>{};
    }

    /**
        Writes down how the plugin differs from the state it was built in.

        This is the other way to author a patch, and the one that suits a
        plugin whose preset browser is its own: rather than watching every
        move, look at where it ended up. An agent that has driven a plugin's
        interface -- loaded something, turned things, decided it likes the
        result -- can ask for the difference and get a patch that reproduces
        it from init, without any of the plugin's own bytes.

        @p origin is written into the file's `from` field. It defaults to the
        init state, and a caller that arrived somewhere by loading a factory
        preset is expected to say so: a patch that began from vendor content
        is a different thing to pass around than one somebody dialled in, and
        the file is where that has to be visible.
    */
    juce::var learnFromInitial(const juce::String& origin, bool probe)
    {
        if (initialValues.empty())
            return error("the plugin's initial state was not captured; reload it");

        recording = {};
        recording.target = currentTarget();
        recording.editorSize = hostedEditorSize();
        recording.from = origin.isNotEmpty() ? origin : juce::String("factory-init");

        for (auto& c : patch::movedBetween(*hosted(), initialValues, patch::valuesOf(*hosted())))
        {
            patch::Operation op;
            op.kind = patch::Operation::Kind::set;
            op.change = std::move(c);
            recording.operations.push_back(std::move(op));
        }

        // The next diff must not report the same differences again.
        lastValues = patch::valuesOf(*hosted());
        pendingExpectation = -1;

        if (probe)
            recording.sound = measureSound(60, 2.0);

        return okWith([this](juce::DynamicObject& o) { describeRecording(o); });
    }

    void startRecording()
    {
        recording = {};
        recording.target = currentTarget();
        recording.editorSize = hostedEditorSize();

        lastValues = patch::valuesOf(*hosted());
        pendingExpectation = -1;
        recordingActive = true;

        recordTicker.tick = [this] { pollRecording(); };
        recordTicker.startTimerHz(10);
    }

    void stopRecording()
    {
        if (!recordingActive)
            return;

        pollRecording(); // whatever moved since the last tick still counts
        recordTicker.stopTimer();
        recordingActive = false;
    }

    /**
        Rewrites the run of identical gestures just made into one search for
        where they arrived.

        An agent choosing from a plugin's own list works by clicking and
        looking: next, next, next, that is the one. What it has at the end is
        a destination, and what the recorder has written down is three
        clicks -- a path, correct only for the list as it stood. This is the
        agent saying "that is what I was aiming at", and the host photographing
        it.

        The budget is generous compared to the run that was recorded, because
        the whole point is that replay may have further to travel than the
        recording did.
    */
    juce::var markDestination(juce::Rectangle<int> region, const juce::String& why)
    {
        if (recording.operations.empty())
            return error("nothing has been recorded to mark");

        auto& ops = recording.operations;
        const auto& last = ops.back();

        if (last.kind == patch::Operation::Kind::set)
            return error("the last thing recorded was a parameter, which already names what it set");

        size_t run = 1;

        while (run < ops.size() && ops[ops.size() - run - 1].sameGestureAs(last))
            ++run;

        patch::Operation seek = last;
        seek.expect.clear();

        if (why.isNotEmpty())
            seek.why = why;

        // Four times what it took to record, and never fewer than sixteen: a
        // list is walked from wherever the plugin happens to start, which is
        // not where the recording started.
        seek.seekSteps = juce::jmax(16, (int) run * 4);

        if (!lookAt(region, seek.look))
            return error("could not look at the editor to record what this was aiming at");

        ops.erase(ops.end() - (long) run, ops.end());
        ops.push_back(std::move(seek));

        // Nothing is outstanding now; the run it belonged to has gone.
        pendingExpectation = -1;

        return okWith(
            [this, run](juce::DynamicObject& o)
            {
                o.setProperty("collapsed", (int) run);
                o.setProperty("maxSteps", recording.operations.back().seekSteps);
                describeRecording(o);
            });
    }

    void describeRecording(juce::DynamicObject& o) const
    {
        o.setProperty("recording", recordingActive);
        o.setProperty("operations", (int) recording.operations.size());
        o.setProperty("target", recording.target.plugin);
        o.setProperty("from", recording.from);
    }

    patch::Target currentTarget() const
    {
        patch::Target t;
        t.plugin = lastLoaded.name;
        t.vendor = lastLoaded.vendor;
        t.version = lastLoaded.version;
        t.format = "VST3";
        t.parameterCount = hosted() != nullptr ? hosted()->getParameters().size() : 0;
        return t;
    }

    /** A pointer operation, recorded with the parameters it is about to move
        left blank: what it moved is not known until the plugin has responded,
        so the next diff is attributed to it rather than being written down as
        a set of its own. That is what makes the click verifiable later. */
    void recordPointer(patch::Operation::Kind kind, juce::Point<float> at, juce::Point<float> to, float delta,
                       const juce::String& why, juce::Rectangle<int> watch)
    {
        if (!recordingActive)
            return;

        pollRecording(); // close off anything still outstanding first

        patch::Operation op;
        op.kind = kind;
        op.at = at;
        op.to = to;
        op.delta = delta;
        op.why = why;

        recording.operations.push_back(std::move(op));
        pendingExpectation = (int) recording.operations.size() - 1;
        expectationTicksLeft = expectationTicks;
        pendingRegion = watch;
    }

    /** A parameter the caller set on purpose, written down as it is asked for
        rather than discovered by the next diff.

        The diff is for changes made by hand in the editor, where there is
        nothing else to notice them. A deliberate set has a better moment
        available -- this one -- and using it removes the one ambiguity the
        recorder would otherwise have: a set issued a moment after a click is
        the caller's own decision, not the click's doing, and only the caller
        knows which. */
    void recordSet(juce::AudioProcessorParameter& p)
    {
        if (!recordingActive)
            return;

        patch::Change c;
        c.index = p.getParameterIndex();
        c.name = p.getName(128);
        c.value = p.getValue();
        c.asText = p.getCurrentValueAsText();

        // Kept out of the next diff, which would otherwise record it twice.
        if ((size_t) c.index < lastValues.size())
            lastValues[(size_t) c.index] = c.value;

        if (!recording.operations.empty())
        {
            auto& last = recording.operations.back();

            if (last.kind == patch::Operation::Kind::set && last.change.index == c.index)
            {
                last.change = std::move(c);
                return;
            }
        }

        patch::Operation op;
        op.kind = patch::Operation::Kind::set;
        op.change = std::move(c);
        recording.operations.push_back(std::move(op));
    }

    void pollRecording()
    {
        if (!recordingActive || hosted() == nullptr)
            return;

        auto now = patch::valuesOf(*hosted());
        auto moved = patch::movedBetween(*hosted(), lastValues, now);
        lastValues = std::move(now);

        // A pointer operation owns what moves in the moments right after it,
        // and only those. Without a bound, a parameter the caller set on
        // purpose a second later would be swallowed as the click's doing --
        // which is wrong, and worse, invisible in the file.
        if (pendingExpectation >= 0 && --expectationTicksLeft <= 0)
        {
            // Photographed as the claim expires rather than immediately: an
            // editor redraws after the click, not during it, and a picture
            // taken in the same breath is a picture of the state before.
            if (pendingExpectation < (int) recording.operations.size())
                lookAt(pendingRegion, recording.operations[(size_t) pendingExpectation].look);

            pendingExpectation = -1;
        }

        if (moved.empty())
            return;

        // Everything that moved just after a pointer operation is that
        // operation's doing, and belongs to it as the expectation replay will
        // check against -- not as separate set operations, which would make
        // replay set by hand the very values the click is supposed to produce.
        if (pendingExpectation >= 0 && pendingExpectation < (int) recording.operations.size())
        {
            auto& owner = recording.operations[(size_t) pendingExpectation];

            for (auto& c : moved)
                owner.expect.push_back(std::move(c));

            pendingExpectation = -1;
            return;
        }

        for (auto& c : moved)
        {
            // One operation per parameter per gesture. A knob dragged across a
            // second produces ten ticks of movement, and a patch that replayed
            // all ten would be a recording of the hand rather than of the
            // decision.
            if (!recording.operations.empty())
            {
                auto& last = recording.operations.back();

                if (last.kind == patch::Operation::Kind::set && last.change.index == c.index)
                {
                    last.change = c;
                    continue;
                }
            }

            patch::Operation op;
            op.kind = patch::Operation::Kind::set;
            op.change = std::move(c);
            recording.operations.push_back(std::move(op));
        }
    }

    // ------------------------------------------------------------- replaying

    /** Applies a patch to whatever is loaded, and says what did not match.

        Refusing on a different plugin rather than trying is deliberate: a
        coordinate is a fact about one editor's layout, and clicking where a
        knob used to be is worse than declining to. Parameters are addressed by
        name first and index second, because a name survives a plugin adding a
        parameter in the middle and an index does not. */
    juce::var replayPatch(const patch::Patch& p, bool force)
    {
        const auto mine = currentTarget();

        if (!p.target.matches(mine) && !force)
            return error("this patch was recorded against " + p.target.vendor + " " + p.target.plugin +
                         ", and " + mine.vendor + " " + mine.plugin + " is loaded");

        juce::Array<juce::var> problems, looked, sought;
        int applied = 0;

        const auto note = [&problems](const juce::String& what) { problems.add(juce::var(what)); };

        if (p.target.version != mine.version && p.target.version.isNotEmpty())
            note("recorded against version " + p.target.version + ", replaying against " + mine.version);

        for (const auto& op : p.operations)
        {
            if (op.kind == patch::Operation::Kind::set)
            {
                juce::String mismatch;
                auto* target = parameterNamed(op.change.name, op.change.index, mismatch);

                if (mismatch.isNotEmpty())
                    note(mismatch);

                if (target == nullptr)
                {
                    note("no parameter called \"" + op.change.name + "\"");
                    continue;
                }

                target->beginChangeGesture();
                target->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, op.change.value));
                target->endChangeGesture();

                if (remote != nullptr)
                    remote->refreshParameter(target->getParameterIndex());

                ++applied;

                // Read back rather than assume. The plugin may quantise, and a
                // value that did not take is exactly what a patch format is
                // supposed to be able to say out loud.
                const auto text = target->getCurrentValueAsText();

                if (op.change.asText.isNotEmpty() && text != op.change.asText)
                    note("\"" + op.change.name + "\" reads " + text + ", recorded as " + op.change.asText);

                continue;
            }

            if (editor == nullptr)
            {
                note("skipped a " + juce::String(patch::Operation::nameOf(op.kind)) +
                     ": pointer operations need an editor in this process");
                continue;
            }

            // Scaled from the editor the patch was recorded against, which is
            // the whole reason that size is in the file.
            const auto scale = p.editorSize.x > 0 && p.editorSize.y > 0
                                   ? juce::Point<float>((float) editor->getWidth() / (float) p.editorSize.x,
                                                        (float) editor->getHeight() / (float) p.editorSize.y)
                                   : juce::Point<float>(1.0f, 1.0f);

            const juce::Point<float> at{op.at.x * scale.x, op.at.y * scale.y};
            const juce::Point<float> to{op.to.x * scale.x, op.to.y * scale.y};

            const auto before = patch::valuesOf(*hosted());

            const auto perform = [&]
            {
                if (op.kind == patch::Operation::Kind::scroll)
                    EditorProbe::mouse(*editor, EditorProbe::MouseAction::scroll, at, op.delta);
                else if (op.kind == patch::Operation::Kind::drag)
                    EditorProbe::drag(*editor, at, to);
                else
                {
                    EditorProbe::mouse(*editor, EditorProbe::MouseAction::move, at);
                    EditorProbe::mouse(*editor, EditorProbe::MouseAction::down, at);
                    EditorProbe::mouse(*editor, EditorProbe::MouseAction::up, at);
                }

                // Delivered, not merely posted -- and then given a moment to
                // be drawn. Everything checked afterwards is checked against
                // the editor after the operation rather than against the
                // editor as it was when the operation was queued.
                EditorProbe::settle(160);
            };

            // The region the operation was aiming at, scaled from the editor
            // the patch was recorded against.
            const juce::Rectangle<int> watched(juce::roundToInt(op.look.region.getX() * scale.x),
                                               juce::roundToInt(op.look.region.getY() * scale.y),
                                               juce::roundToInt(op.look.region.getWidth() * scale.x),
                                               juce::roundToInt(op.look.region.getHeight() * scale.y));

            perform();
            int steps = 1;

            // A search rather than a count. What the patch recorded is where
            // this was going, not how far away it was on the day -- so replay
            // keeps going until it arrives, and says so when it does not.
            if (op.seekSteps > 0 && op.look.recorded())
            {
                patch::Look now;

                while (steps < op.seekSteps && lookAt(watched, now) &&
                       patch::Look::distance(now.cells, op.look.cells) > lookTolerance)
                {
                    perform();
                    ++steps;
                }

                sought.add(juce::var(juce::String(patch::Operation::nameOf(op.kind)) + " at " +
                                     juce::String(op.at.x, 1) + "," + juce::String(op.at.y, 1) + ": " +
                                     juce::String(steps) + " of " + juce::String(op.seekSteps) + " steps"));
            }

            ++applied;

            const auto where = juce::String(patch::Operation::nameOf(op.kind)) + " at " +
                               juce::String(op.at.x, 1) + "," + juce::String(op.at.y, 1);

            // A click that missed usually moved nothing, and that is visible
            // without looking at a pixel. This is the difference between a
            // patch format and a macro recorder.
            if (!op.expect.empty())
            {
                const auto moved = patch::movedBetween(*hosted(), before, patch::valuesOf(*hosted()));

                for (const auto& wanted : op.expect)
                {
                    const auto found = std::find_if(moved.begin(), moved.end(), [&wanted](const auto& m)
                                                    { return m.index == wanted.index; });

                    if (found == moved.end())
                        note(where + " did not move \"" + wanted.name + "\" as recorded");
                }
            }

            // And the operations that move no parameter at all -- a tab, a
            // page, a name chosen from the plugin's own list. Until this,
            // those were the ones a patch had to take on trust, which is the
            // same as not checking them.
            if (op.look.recorded())
            {
                patch::Look now;

                if (!lookAt(watched, now))
                    note(where + ": could not look at the editor to check it");
                else
                {
                    const auto off = patch::Look::distance(now.cells, op.look.cells);
                    looked.add(juce::var(where + ": " + juce::String(off, 1) + " away"));

                    if (off > lookTolerance)
                        note(where +
                             (op.seekSteps > 0 ? " never reached what it was aiming at in " +
                                                     juce::String(op.seekSteps) + " steps ("
                                               : " did not leave the editor looking as recorded (") +
                             juce::String(off, 1) + " of 255 average brightness away)");
                }
            }
        }

        // The check that catches the failure every other check misses: a
        // patch whose operations all applied, whose values all read back
        // correctly, and which does not sound like what was recorded. That
        // happens whenever a plugin keeps part of its voice somewhere its
        // parameter list does not reach, and without this the host would
        // report a clean success for a patch that reproduces nothing.
        patch::Sound got;

        if (p.sound.measured)
        {
            got = measureSound(p.sound.note, p.sound.durationSec);
            const auto off = p.sound.differenceFrom(got);

            if (off > soundTolerance)
                note("this does not sound like the recording (peak " + juce::String(got.peak, 3) +
                     " against " + juce::String(p.sound.peak, 3) + ", rms " + juce::String(got.rms, 3) +
                     " against " + juce::String(p.sound.rms, 3) +
                     "): the plugin keeps something this patch could not record");
        }

        return okWith(
            [applied, &problems, &looked, &sought, &p, &got](juce::DynamicObject& o)
            {
                o.setProperty("applied", applied);
                o.setProperty("operations", (int) p.operations.size());
                o.setProperty("problems", problems);

                if (!looked.isEmpty())
                    o.setProperty("looked", looked);

                if (!sought.isEmpty())
                    o.setProperty("sought", sought);

                if (p.sound.measured)
                {
                    auto* heard = new juce::DynamicObject();
                    heard->setProperty("recordedPeak", p.sound.peak);
                    heard->setProperty("recordedRms", p.sound.rms);
                    heard->setProperty("peak", got.peak);
                    heard->setProperty("rms", got.rms);
                    heard->setProperty("reproduced", p.sound.differenceFrom(got) <= soundTolerance);
                    o.setProperty("sound", juce::var(heard));
                }
            });
    }

    /**
        The parameter a recorded operation meant: the index, checked by name.

        The order of those two is not obvious and getting it wrong is silent.
        Name first is the tempting rule -- a name survives a plugin inserting a
        parameter in the middle, an index does not -- and it is wrong for any
        plugin whose names are not identities. Serum's are not: its effect
        slots take their parameter names from whichever effect is loaded, so
        three parameters recorded as "VerbPDly", "VerbDamp" and "VerbWdth"
        were found under those names somewhere else entirely and replayed onto
        a delay's controls. Every value landed, all sixty-four operations
        reported success, and the patch did not reproduce.

        So: the index is the address and the name is the check. When they
        agree, that is the parameter. When they do not, the plugin's parameter
        list has moved under the patch, and a name search is the better guess
        -- with the mismatch reported either way, because a patch replaying
        against a changed parameter list is exactly the situation this format
        exists to be able to notice.
    */
    juce::AudioProcessorParameter* parameterNamed(const juce::String& name, int index,
                                                  juce::String& mismatch) const
    {
        const auto& all = hosted()->getParameters();

        if (juce::isPositiveAndBelow(index, all.size()))
        {
            if (name.isEmpty() || all[index]->getName(128) == name)
                return all[index];

            mismatch = "index " + juce::String(index) + " is now \"" + all[index]->getName(128) +
                       "\", recorded as \"" + name + "\"";
        }

        for (auto* p : all)
            if (p->getName(128) == name)
                return p;

        return juce::isPositiveAndBelow(index, all.size()) ? all[index] : nullptr;
    }

    juce::AudioProcessorParameter* findParameter(const juce::var& request) const
    {
        const auto& ps = hosted()->getParameters();
        const int index = (int) request.getProperty("index", -1);

        if (index >= 0)
            return index < ps.size() ? ps[index] : nullptr;

        const auto name = request.getProperty("name", "").toString();
        for (auto* p : ps)
            if (p->getName(64).equalsIgnoreCase(name))
                return p;

        return nullptr;
    }

    template <typename Fn>
    static juce::var okWith(Fn&& fill)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", true);
        fill(*o);
        return juce::var(o);
    }

    static juce::var error(const juce::String& why)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", false);
        o->setProperty("error", why);
        return juce::var(o);
    }

    /** @return the port, or 0 if the port could not be bound. */
    int startControlServer(int port) { return control.start(port) ? control.getPort() : 0; }

    /** Opens the analyser panel, for command-line and agent use. */
    void openScope()
    {
        if (hostedEditorSize().y > 0 && !roomForInlineScope())
            return showScopeDetail();

        if (scopePanel != nullptr && !scopePanel->isOpen())
        {
            scopePanel->setOpen(true);
            strip.setScopeOpen(true);
            refitWindow(true);
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

        // One baseline for the whole row, and the byline placed after the
        // wordmark by measuring it rather than by a number that was right at
        // one type size and wrong at the next.
        g.setColour(theme::ink);
        g.setFont(theme::ui(theme::size::title));
        g.drawText("plugshell", headerInset, 12, 160, 22, juce::Justification::centredLeft);

        const int afterName = headerInset +
                              juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                  theme::ui(theme::size::title), "plugshell")) +
                              14;

        // Stops short of the appearance control instead of running underneath it.
        const int rightEdge = getWidth() - headerInset - themeButtonWidth - 12;

        g.setColour(theme::mute);
        g.setFont(theme::ui(loadedName.isEmpty() ? theme::size::label : theme::size::body));
        g.drawText(loadedName.isEmpty() ? "by wheatfox" : loadedName, afterName, 12,
                   juce::jmax(0, rightEdge - afterName), 22, juce::Justification::centredLeft, true);

        g.setColour(theme::hair);
        g.drawLine((float) headerInset, 42.0f, (float) (getWidth() - headerInset), 42.0f, 1.0f);

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
        else if (remote != nullptr)
        {
            paintRemoteNotice(g);
        }
        else if (instance != nullptr && editor == nullptr)
        {
            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::body));
            g.drawText("This plugin provides no editor", contentArea(), juce::Justification::centred);
        }
    }

    static juce::String versionString() { return "v" PLUGSHELL_VERSION; }

    /** The wordmark's hit area: the name, the version and the byline together,
        because they read as one thing and a control should be the size of the
        thing it looks like. */
    juce::Rectangle<int> wordmarkBounds() const
    {
        const int nameWidth = juce::roundToInt(
            juce::GlyphArrangement::getStringWidth(theme::ui(theme::size::title), "plugshell"));
        const int tailWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(
            theme::ui(theme::size::label), versionString() + "   by wheatfox"));

        return {10, 8, nameWidth + tailWidth + 26, 30};
    }

    /** Dark, light, or whatever macOS is doing. Three states on one control
        because the choice is small and a panel for it would be larger than
        the thing it sets. */
    void cycleTheme()
    {
        using Mode = theme::Mode;

        // Captured before the mode moves. Working the starting palette out
        // afterwards from the opposite of where we ended up is wrong whenever
        // the two modes agree -- switching between "dark" and "automatic" on a
        // dark system changes nothing visible, but that reasoning said the
        // window had been light, so it flashed white on the way to where it
        // already was.
        const auto before = theme::paletteFor(theme::isDark());

        theme::mode = theme::mode == Mode::dark    ? Mode::light
                      : theme::mode == Mode::light ? Mode::automatic
                                                   : Mode::dark;

        beginThemeChange(before);
        saveSettings();
    }

    /** Cross-fades between the two palettes rather than cutting.

        Every surface in the window changes at once, and a cut of that size
        reads as the window having been replaced. Fading it makes it obviously
        the same window in different light, which is what actually happened. */
    void beginThemeChange(const theme::Palette& from)
    {
        themeFrom = from;
        themeTo = theme::paletteFor(theme::isDark());

        // Start from where we are, not from the destination. Applying the new
        // palette here and then letting the animation begin at the old one
        // meant the window arrived, went back, and crossed again -- which is
        // the flash.
        themeMix.snapTo(0.0f);
        theme::useMix(themeFrom, themeTo, 0.0f);
        pushColours();

        themeMix.setTarget(1.0f);
        themeAnim.nudge();
    }

    void applyTheme()
    {
        theme::apply();
        pushColours();
    }

    /** Hands the current palette to everything that copied it.

        These all take their colours once, when they are made, which is also
        why the theme loaded at startup did not appear: members are
        constructed before the constructor body runs, so they had all taken
        the default scheme before the saved one was read. */
    void pushColours()
    {

        // Every one of these copied the palette in when it was made, so a
        // switch has to hand it back to each of them. Reading the theme at
        // paint time instead would avoid this, at the cost of a lookup in
        // every drawing call in the application.
        darkLookAndFeel.refresh();
        strip.refreshColours();
        themeButton.setColours(theme::ink, theme::mute, theme::hair);

        // A colour of its own rather than one from the palette. Whether
        // something is driving the host is not a property of the theme, and a
        // live indication that changes hue with the appearance setting reads
        // as decoration; this one stays the same green in both.
        agentBadge.setColours(theme::ink, theme::mute, juce::Colour(0xff4ade80));
        list.setColour(juce::ListBox::backgroundColourId, theme::base);

        if (scopePanel != nullptr)
            scopePanel->setColours(theme::base, theme::ink, theme::mute, theme::hair);

        viewport.setColour(juce::ScrollBar::thumbColourId, theme::hair);

        if (auto* window = getTopLevelComponent())
            window->setColour(juce::DocumentWindow::backgroundColourId, theme::base);

        refreshThemeButton();
        repaintTree(*this);

        // An open panel keeps the colours it was built with, and rebuilding it
        // under the user would lose whatever they were reading.
        if (overlay != nullptr)
            dismissOverlay();
    }

    /** Repaints every descendant, not just this component.

        A repaint marks one component's area dirty, and that turned out to be
        enough for the header and not for the plugin list inside the viewport:
        after a theme change the bar had changed and the list under it had
        not. */
    static void repaintTree(juce::Component& c)
    {
        c.repaint();

        for (int i = c.getNumChildComponents(); --i >= 0;)
            if (auto* child = c.getChildComponent(i))
                repaintTree(*child);
    }

    void refreshTransport()
    {
        strip.setTransport(playHead.isPlaying(), playHead.getTempo(), playHead.getNumerator(),
                           playHead.getDenominator());
    }

    void refreshThemeButton()
    {
        themeButton.setText({});
        themeButton.setGlyph(theme::mode == theme::Mode::dark    ? StripButton::Glyph::moon
                             : theme::mode == theme::Mode::light ? StripButton::Glyph::sun
                                                                 : StripButton::Glyph::automatic);

        themeButton.setTooltip(theme::mode == theme::Mode::dark    ? "Appearance: dark"
                               : theme::mode == theme::Mode::light ? "Appearance: light"
                                                                   : "Appearance: following macOS");
    }

    /** The one place the level changes. The fader, the socket and a
        restored setting all come through here, so the control and the audio
        can never be showing different numbers. */
    void setMasterGain(float linear)
    {
        masterGain = juce::jlimit(0.0f, OutputMeter::maxGain, linear);
        tap.setGain(masterGain);
        strip.setGain(masterGain);
        saveSettings();
    }

    /** Takes effect at the next load rather than immediately.

        Moving a running plugin between processes would mean unloading and
        reloading it, which loses whatever is in it -- so the setting changes
        and the plugin that is already open carries on where it is. */
    void setOutOfProcess(bool on)
    {
        outOfProcess = on;
        saveSettings();
    }

    /** Whichever plugin is loaded, wherever it is running.

        Almost everything above this -- parameters, programs, the keyboard,
        offline rendering -- has no business knowing which process the plugin
        is in, and asking each of them to check two pointers is how the two
        paths drift apart. */
    juce::AudioProcessor* hosted() const
    {
        if (instance != nullptr)
            return instance.get();

        return remote != nullptr && remote->isReady() ? remote.get() : nullptr;
    }

    /** The helper, inside this application's own bundle.

        Found relative to the running executable rather than by name, so a
        build in a tree and an installed copy both work, and neither can be
        made to launch something else by putting it earlier in the path. */
    static juce::File helperExecutable()
    {
        const auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

        return exe.getChildFile("Contents/Helpers/plugshell-host.app/Contents/MacOS/plugshell-host");
    }

    juce::File settingsFile() const
    {
        // userApplicationDataDirectory is ~/Library on macOS, not
        // ~/Library/Application Support, which is where everything else this
        // application writes already lives.
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support")
            .getChildFile("plugshell")
            .getChildFile("settings.json");
    }

    void loadSettings()
    {
        const auto file = settingsFile();
        if (!file.existsAsFile())
            return;

        juce::var parsed;
        if (juce::JSON::parse(file.loadFileAsString(), parsed).failed() || !parsed.isObject())
            return;

        const auto name = parsed.getProperty("theme", "dark").toString();
        theme::mode = name == "light"  ? theme::Mode::light
                      : name == "auto" ? theme::Mode::automatic
                                       : theme::Mode::dark;

        masterGain =
            juce::jlimit(0.0f, OutputMeter::maxGain, (float) (double) parsed.getProperty("masterGain", 1.0));

        outOfProcess = (bool) parsed.getProperty("outOfProcess", false);
    }

    void saveSettings() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("theme", theme::mode == theme::Mode::dark    ? "dark"
                                : theme::mode == theme::Mode::light ? "light"
                                                                    : "auto");
        o->setProperty("masterGain", (double) masterGain);
        o->setProperty("outOfProcess", outOfProcess);

        const auto file = settingsFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(juce::var(o), true));
    }

    void showAbout()
    {
        if (overlay != nullptr)
            return dismissOverlay();

        auto o = std::make_unique<Overlay>("plugshell " + versionString(), theme::base, theme::ink,
                                           theme::mute, theme::hair);

        o->setContent(std::make_unique<AboutContent>(), AboutContent::preferredHeight);
        o->setFooter("AGPL-3.0-or-later.  Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>");
        showOverlay(std::move(o));
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool over = wordmarkBounds().contains(e.getPosition());

        if (over != overWordmark)
        {
            overWordmark = over;
            wordmarkGlow.setTarget(over ? 1.0f : 0.0f);
            wordmarkAnim.nudge();
            setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        overWordmark = false;
        wordmarkGlow.setTarget(0.0f);
        wordmarkAnim.nudge();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (wordmarkBounds().contains(e.getPosition()))
            showAbout();
    }

    void resized() override
    {
        positionOverlay();

        auto r = getLocalBounds();
        // Right edge on the same margin as the rule beneath it, centred on
        // the same line as the wordmark beside it.
        themeButton.setBounds(r.getWidth() - headerInset - themeButtonWidth, 23 - 15, themeButtonWidth, 30);

        // Left of the appearance control, on the wordmark's baseline. Sized to
        // its own text so a long agent name does not run under the button.
        const int badgeWidth = agentBadge.preferredWidth();
        agentBadge.setBounds(themeButton.getX() - 14 - badgeWidth, 12, badgeWidth, 22);
        strip.setBounds(r.removeFromBottom(stripHeight()));

        if (scopePanel != nullptr)
        {
            const int h = juce::roundToInt(scopeHeight * scopePanel->getExtent());
            scopePanel->setBounds(r.removeFromBottom(h));
            scopePanel->setVisible(h > 0);
        }
        keepOnScreenSoon();

        const auto la = listArea();
        viewport.setBounds(la);
        list.setSize(la.getWidth() - (viewport.isVerticalScrollBarShown() ? 10 : 0),
                     juce::jmax(la.getHeight(), list.getHeight()));
        if (editor != nullptr)
        {
            // setTopLeftPosition works in unscaled coordinates, so divide the
            // target back out by the transform we applied.
            const auto a = contentArea();
            editorView.setBounds(a);
        }

        updateRemoteEditorPlacement();
    }

private:
    // Small on purpose. The window grows by this much when the analyser
    // opens, and on a laptop display a tall panel pushed the bottom of the
    // window -- control strip included -- off the screen.
    static constexpr int scopeHeight = 104;

    juce::Rectangle<int> contentArea() const
    {
        const int scopeTaken =
            scopePanel != nullptr ? juce::roundToInt(scopeHeight * scopePanel->getExtent()) : 0;
        return getLocalBounds()
            .withTrimmedTop(headerHeight)
            .withTrimmedBottom(stripHeight() + stripGap + scopeTaken);
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
        finishDismissingOverlay();

        overlay = std::move(o);
        overlay->onDismiss = [this] { dismissOverlay(); };

        overlay->setOpaque(false);
        overlay->addToDesktop(juce::ComponentPeer::windowIsTemporary |
                              juce::ComponentPeer::windowIgnoresKeyPresses * 0);
        positionOverlay();
        overlay->setVisible(true);

        // Parented rather than always-on-top. Always-on-top is a property of
        // the whole machine, not of this application, so it floated the panel
        // over every other window the user had open.
        EditorProbe::attachAsChildWindow(*overlay, *this);

        // Faded in first, focused second. A window sitting at zero alpha is
        // not a window the system will hand the keyboard to, so grabbing focus
        // before starting the fade grabbed nothing -- which is why esc stopped
        // closing anything.
        overlay->beginFadeIn();
        overlay->toFront(true);
        overlay->grabKeyboardFocus();

        // The other process's editor is ordered above this window, and no
        // public interface puts one process's window between two of
        // another's. So while a panel of ours is up, the editor stands aside.
        updateRemoteEditorPlacement();
    }

    void positionOverlay()
    {
        if (overlay == nullptr)
            return;

        // The panel is its own window, so it does not have to be the size of
        // this one -- and it must not be. The host's window is the size of
        // whatever plugin is loaded, and a meter's editor can be 170 points
        // wide, which turned the settings panel into a column of truncated
        // labels with the title written across the close hint.
        //
        // So: covering the host window when that is big enough to read, and a
        // readable size centred on it when it is not, clamped to the display.
        const auto host = getScreenBounds();
        const auto work =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();

        const int w = juce::jlimit(juce::jmin(overlayMinWidth, work.getWidth()), work.getWidth(),
                                   juce::jmax(host.getWidth(), overlayMinWidth));
        const int h = juce::jlimit(juce::jmin(overlayMinHeight, work.getHeight()), work.getHeight(),
                                   juce::jmax(host.getHeight(), overlayMinHeight));

        const auto bounds = juce::Rectangle<int>(w, h).withCentre(host.getCentre()).constrainedWithin(work);
        overlay->setBounds(bounds);

        // The host's rectangle, expressed where the panel can use it.
        overlay->setScrimArea(host - bounds.getPosition());
    }

    void dismissOverlay()
    {
        if (overlay != nullptr)
        {
            // Asked to leave rather than removed outright: a panel that
            // vanishes between two frames reads as a glitch, and the same
            // movement that brought it in is what makes it clear it has gone
            // back rather than gone wrong.
            overlay->onFadedOut = [this] { finishDismissingOverlay(); };
            overlay->beginFadeOut();
            return;
        }

        finishDismissingOverlay();
    }

    void finishDismissingOverlay()
    {
        const juce::ScopeGuard restoreEditor{[this] { updateRemoteEditorPlacement(); }};

        if (overlay != nullptr)
        {
            overlay->removeFromDesktop();
            overlay.reset();
        }

        // The overlay is its own window, so closing it leaves the main window
        // unfocused and macOS spends the next click on activating it instead
        // of on the button that was clicked. Take the focus back here so the
        // strip responds to the first click rather than the second.
        if (auto* top = getTopLevelComponent())
            top->toFront(true);

        repaint();
    }

    void showHelp()
    {
        if (overlay != nullptr)
            return dismissOverlay();

        auto o = std::make_unique<Overlay>("Help", theme::base, theme::ink, theme::mute, theme::hair);
        o->setPanelWidth(juce::jmax(660, getWidth() - 80));

        // The keyboard layout is the thing being explained, so it is shown
        // rather than described. A list of which letters are white keys is
        // accurate and asks the reader to hold a keyboard in their head and
        // check every letter against it.
        o->setContent(std::make_unique<KeyboardMap>(theme::base, theme::ink, theme::mute, theme::hair),
                      KeyboardMap::preferredHeight());

        o->setFooter("The computer keyboard is off by default: plugin editors want it too, for typing "
                     "values and searching presets.");

        showOverlay(std::move(o));
    }

    /** The device selector with a permission row beneath it.

        Screen Recording belongs in settings because without it the host cannot
        see a plugin that draws with the GPU, and that failure otherwise
        surfaces as a capture error in a log somewhere rather than as something
        the user can act on. macOS only ever shows its prompt once, so after a
        refusal the only route is System Settings, and the row has to offer
        that rather than pretending the prompt will come back. */
    class SettingsBody : public juce::Component
    {
    public:
        SettingsBody(std::unique_ptr<juce::Component> main, std::function<juce::String()> latency,
                     bool outOfProcessNow, std::function<void(bool)> setOutOfProcess)
            : content(std::move(main)), latencyText(std::move(latency))
        {
            addAndMakeVisible(content.get());

            toggle = std::make_unique<Toggle>(
                "Run plugins in their own process",
                "Off: a plugin that crashes takes plugshell with it. Faster by one buffer.",
                "On: a crash is survivable, at one buffer more latency. The editor becomes a second "
                "window and takes no typed text. Takes effect at the next load.",
                outOfProcessNow, std::move(setOutOfProcess));

            addAndMakeVisible(toggle.get());

            // The relaunch note is not padding. macOS applies Screen
            // Recording only at launch, so allowing it and watching this row
            // go on saying no is the expected behaviour, and looks exactly
            // like the grant having failed.
            rows.add(new Row(
                "Screen recording",
                "Needed to capture editors that draw with the GPU. Quit and reopen after allowing it.",
                "Granted.", &EditorProbe::hasScreenRecordingPermission,
                &EditorProbe::requestScreenRecordingPermission,
                "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"));

            rows.add(new Row(
                "Accessibility", "Needed to click and drag inside a plugin's editor.", "Granted.",
                &EditorProbe::hasAccessibilityPermission, &EditorProbe::requestAccessibilityPermission,
                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"));

            for (auto* r : rows)
                addAndMakeVisible(r);
        }

        static int heightOfRows() { return rowHeight * 3 + latencyHeight; }

        void paint(juce::Graphics& g) override
        {
            // The buffer size above this says 5 ms and the real latency is
            // often thirty times that, which reads as a contradiction unless
            // the arithmetic is shown. It is not a contradiction: one is what
            // the host schedules, the other is what reaches the ear.
            // Counted from the bottom, and the count has to match the one
            // resized() makes: three rows sit below this band, not two. It
            // said two from back when the process toggle was not one of them,
            // which drew this line straight across the toggle's description.
            auto row = getLocalBounds()
                           .removeFromBottom(rowHeight * 3 + latencyHeight)
                           .removeFromTop(latencyHeight)
                           .reduced(0, 6);

            g.setColour(theme::mute);
            g.setFont(theme::ui(theme::size::micro));
            g.drawText(latencyText ? latencyText() : juce::String(), row, juce::Justification::centredLeft,
                       true);
        }

        void resized() override
        {
            auto r = getLocalBounds();

            for (int i = rows.size(); --i >= 0;)
                rows[i]->setBounds(r.removeFromBottom(rowHeight));

            toggle->setBounds(r.removeFromBottom(rowHeight));

            r.removeFromBottom(latencyHeight);
            content->setBounds(r.withTrimmedBottom(8));
        }

    private:
        static constexpr int rowHeight = 58;
        static constexpr int latencyHeight = 34;

        /** A setting that is on or off, drawn like the permission rows
            beneath it so the panel reads as one list.

            The two descriptions are not the same sentence with a "not" in it.
            Each state has its own cost -- one risks the application, the
            other costs a buffer of latency and an editor that cannot be typed
            into -- and a reader deciding between them needs to see the one
            they are not currently paying. */
        class Toggle : public juce::Component
        {
        public:
            Toggle(juce::String t, juce::String off, juce::String on, bool startsOn,
                   std::function<void(bool)> change)
                : title(std::move(t)), whenOff(std::move(off)), whenOn(std::move(on)),
                  onChange(std::move(change)), value(startsOn)
            {
                addAndMakeVisible(action);
                action.setColours(theme::ink, theme::mute, theme::hair);
                action.setFramed(true);
                action.onClick = [this]
                {
                    value = !value;

                    if (onChange)
                        onChange(value);

                    refresh();
                };

                refresh();
            }

            void refresh()
            {
                action.setText(value ? "On" : "Off");
                action.setToggled(value);
                repaint();
            }

            void paint(juce::Graphics& g) override
            {
                auto r = getLocalBounds();

                g.setColour(theme::hair);
                g.drawLine((float) r.getX(), (float) r.getY(), (float) r.getRight(), (float) r.getY(), 1.0f);

                r = r.reduced(0, 8);
                r.removeFromLeft(28);
                r.removeFromRight(104);

                g.setColour(value ? theme::ink : theme::mute);
                g.setFont(theme::ui(theme::size::body));
                g.drawText(title, r.removeFromTop(17), juce::Justification::centredLeft);

                // Two lines rather than one with an ellipsis. What each state
                // costs is the whole reason this row is worth reading, and a
                // sentence cut off at "the editor is drawn by t..." costs the
                // reader the part they were told to weigh.
                g.setColour(theme::mute);
                g.setFont(theme::ui(theme::size::micro));
                g.drawFittedText(value ? whenOn : whenOff, r, juce::Justification::topLeft, 2);
            }

            void resized() override { action.setBounds(getLocalBounds().removeFromRight(96).reduced(0, 14)); }

        private:
            juce::String title, whenOff, whenOn;
            std::function<void(bool)> onChange;
            bool value;
            StripButton action{"Off"};
        };

        /** One permission: what it is for, whether it is granted, and the one
            button that can change that.

            macOS shows its prompt only once. After a refusal the prompt never
            returns, so the button has to fall through to the settings pane
            rather than appearing to do nothing -- which is exactly how this
            failed before it was built: the capture and the click both reported
            success and neither did anything. */
        class Row : public juce::Component
        {
        public:
            using Query = bool (*)();

            Row(juce::String t, juce::String w, juce::String ok, Query has, Query request, juce::String pane)
                : title(std::move(t)), why(std::move(w)), grantedNote(std::move(ok)), isGranted(has),
                  ask(request), settingsPane(std::move(pane))
            {
                addAndMakeVisible(action);
                action.setColours(theme::ink, theme::mute, theme::hair);
                action.setFramed(true);
                action.onClick = [this]
                {
                    if (!isGranted() && !ask())
                        juce::URL(settingsPane).launchInDefaultBrowser();

                    refresh();
                };

                refresh();
                startTimer();
            }

            void refresh()
            {
                granted = isGranted();
                action.setText(granted ? "Granted" : "Allow...");
                action.setEnabled(!granted);
                repaint();
            }

            void paint(juce::Graphics& g) override
            {
                auto r = getLocalBounds();

                g.setColour(theme::hair);
                g.drawLine((float) r.getX(), (float) r.getY(), (float) r.getRight(), (float) r.getY(), 1.0f);

                r = r.reduced(0, 8);

                // The state has to be readable without reading it, because
                // the whole row exists to answer one yes-or-no question.
                drawMark(g, r.removeFromLeft(20).withSizeKeepingCentre(13, 13).toFloat());
                r.removeFromLeft(8);
                r.removeFromRight(104);

                g.setColour(granted ? theme::ink : theme::mute);
                g.setFont(theme::ui(theme::size::body));
                g.drawText(title, r.removeFromTop(17), juce::Justification::centredLeft);

                g.setColour(theme::mute);
                g.setFont(theme::ui(theme::size::micro));
                g.drawText(granted ? grantedNote : why, r, juce::Justification::centredLeft, true);
            }

            /** Drawn rather than set in a font, so it carries the same weight
                as the rest of the interface and does not depend on an emoji
                the system may decide to render in colour. */
            void drawMark(juce::Graphics& g, juce::Rectangle<float> box) const
            {
                juce::Path path;

                if (granted)
                {
                    path.startNewSubPath(box.getX(), box.getCentreY() + 0.5f);
                    path.lineTo(box.getCentreX() - 1.0f, box.getBottom() - 2.0f);
                    path.lineTo(box.getRight(), box.getY() + 1.0f);

                    g.setColour(juce::Colour{0xff5fbf7f});
                    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));
                    return;
                }

                path.startNewSubPath(box.getCentreX(), box.getY());
                path.lineTo(box.getRight(), box.getBottom());
                path.lineTo(box.getX(), box.getBottom());
                path.closeSubPath();

                g.setColour(juce::Colour{0xffd9a441});
                g.strokePath(path, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::butt));

                g.fillRect(box.getCentreX() - 0.7f, box.getY() + 4.8f, 1.4f, box.getHeight() * 0.33f);
                g.fillEllipse(box.getCentreX() - 0.8f, box.getBottom() - 3.6f, 1.6f, 1.6f);
            }

            void resized() override { action.setBounds(getLocalBounds().removeFromRight(96).reduced(4, 10)); }

        private:
            /** Granting happens in System Settings, in another window, and
                macOS tells nobody. Polling is how the row notices. */
            struct Poll : juce::Timer
            {
                std::function<void()> tick;
                void timerCallback() override
                {
                    if (tick)
                        tick();
                }
            };

            void startTimer()
            {
                poll.tick = [this]
                {
                    if (!granted)
                        refresh();
                };
                poll.startTimerHz(2);
            }

            juce::String title, why, grantedNote;
            Query isGranted, ask;
            juce::String settingsPane;
            StripButton action{"Allow..."};
            Poll poll;
            bool granted = false;
        };

        std::unique_ptr<juce::Component> content;
        std::unique_ptr<Toggle> toggle;
        std::function<juce::String()> latencyText;
        juce::OwnedArray<Row> rows;
    };

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
        o->setContent(std::make_unique<SettingsBody>(
                          std::move(sel), [this] { return latencyBreakdown(); }, outOfProcess,
                          [this](bool on) { setOutOfProcess(on); }),
                      420 + SettingsBody::heightOfRows());

        if (outputIsBluetooth())
            o->setFooter("Bluetooth output. The wireless link is where nearly all of that latency is, "
                         "and no host can shorten it: AAC runs 80-160 ms by design. A wired interface "
                         "is the only real fix.");

        showOverlay(std::move(o));
    }

    void setKeysEnabled(bool on)
    {
        if (keysEnabled == on)
            return;

        keysEnabled = on;

        if (on)
            stuckKeyWatchdog.startTimerHz(20);
        else
        {
            stuckKeyWatchdog.stopTimer();
            allNotesOff();
        }

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
        heldCodes.clear();
        strip.setPlaying({}, {});

        // Then the controller messages, for everything this host is not
        // tracking. The map above holds notes struck from the computer
        // keyboard; a note sent over the socket, or one the plugin started on
        // its own -- an arpeggiator latched, a sequencer running -- is not in
        // it, and before this those notes had no way to be stopped at all.
        //
        // Both messages, on every channel: All Notes Off releases them into
        // their release stage, which a long pad can sustain for seconds, and
        // All Sound Off is the one that means now.
        for (int channel = 1; channel <= 16; ++channel)
        {
            auto& queue = player.getMidiMessageCollector();
            queue.addMessageToQueue(juce::MidiMessage::allNotesOff(channel).withTimeStamp(now()));
            queue.addMessageToQueue(juce::MidiMessage::allSoundOff(channel).withTimeStamp(now()));
        }
    }

    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    /** @return true when the event has been consumed. */
    bool onKey(int c, int keyCode, bool isDown, bool isRepeat, bool commandDown)
    {
        // A panel is a window of its own and keyboard focus does not reliably
        // sit on it, which is the same reason the note keys are read here
        // rather than through the component tree.
        if (isDown)
            lastKeyChar = c;

        // By hardware key, not by character. An input method sits between the
        // keyboard and the character, and a Chinese one consumes Escape to
        // dismiss its candidate window -- measurably, since "x" reached this
        // handler while Escape did not. The key code is the physical key and
        // no input method rewrites it.
        if (isDown && (c == 27 || keyCode == 53) && overlay != nullptr)
        {
            dismissOverlay();
            return true;
        }

        if (commandDown)
        {
            if (isDown && c == 'k')
            {
                setKeysEnabled(!keysEnabled);
                return true;
            }

            // Closing a panel is bound here because Escape demonstrably does
            // not arrive -- with a panel open, "x" reaches this handler and
            // Escape does not, by character or by key code, so something takes
            // it before the application is given the chance. Command chords do
            // arrive, which is how Cmd-K has been working all along.
            if (isDown && c == 'w' && overlay != nullptr)
            {
                dismissOverlay();
                return true;
            }

            return false; // every other command shortcut belongs to the host or plugin
        }

        // Not while something is being typed into. The monitor sits ahead of
        // the whole component tree, so with the keyboard mode on it was taking
        // the keys out of any text field in the application -- typing a tempo
        // played a chord instead.
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (dynamic_cast<juce::TextEditor*>(focused) != nullptr ||
                focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        if (!keysEnabled || hosted() == nullptr)
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
                releaseKey(c);
            }

            const int note = juce::jlimit(0, 127, octave * 12 + offset);
            sounding.set(c, note);
            heldCodes.set(c, keyCode);
            player.getMidiMessageCollector().addMessageToQueue(
                juce::MidiMessage::noteOn(1, note, 0.8f).withTimeStamp(now()));
            updatePlaying();
        }
        else if (sounding.contains(c))
        {
            releaseKey(c);
            updatePlaying();
        }

        return true;
    }

    /** Two keys can be the same note: the two octave rows overlap at the top,
        so `,` and `q` are both C, as are `.` and `w`, `l` and `2`, `;` and `3`.
        Releasing either of a held pair used to send a note-off that stopped
        the other one too, and the key still being held then looked dead
        because its note-down had already been answered. Only the last holder
        of a pitch releases it. */
    void releaseKey(int c)
    {
        const int note = sounding[c];
        sounding.remove(c);
        heldCodes.remove(c);

        for (juce::HashMap<int, int>::Iterator it(sounding); it.next();)
            if (it.getValue() == note)
                return; // another key is still holding this note

        sendNoteOff(note);
    }

    /** Releases anything whose key the window server says is no longer down.

        Every stuck note is the same bug in the end -- a key-up that never
        arrived -- and the causes are not worth enumerating, because the
        keyboard itself can be asked. This runs while the mode is on and costs
        one syscall per held key, twenty times a second. */
    void releaseKeysNoLongerHeld()
    {
        juce::Array<int> lost;
        for (juce::HashMap<int, int>::Iterator it(heldCodes); it.next();)
            if (!KeyMonitor::isHeld(it.getValue()))
                lost.add(it.getKey());

        if (lost.isEmpty())
            return;

        for (const auto c : lost)
            releaseKey(c);

        updatePlaying();
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
            return fitWindowToEditor();

        // The window itself. Dragging it has to take the other process's
        // editor with it in the same frame, which a timer cannot promise.
        if (&c == getTopLevelComponent())
            updateRemoteEditorPlacement();
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

    /** Total latency, and where it came from. Written as the sum rather than
        the total alone, because the buffer size sits directly above it in the
        panel reporting a number thirty times smaller. */
    juce::String latencyBreakdown() const
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
            return "No output device.";

        const auto rate = device->getCurrentSampleRate();
        if (rate <= 0.0)
            return {};

        const double buffer = 1000.0 * device->getCurrentBufferSizeSamples() / rate;
        const double hardware = 1000.0 * device->getOutputLatencyInSamples() / rate;

        return "Output latency " + juce::String(buffer + hardware, 1) + " ms  =  " + juce::String(buffer, 1) +
               " ms buffer  +  " + juce::String(hardware, 1) + " ms device";
    }

    bool outputIsBluetooth() const
    {
        auto* device = devices.getCurrentAudioDevice();
        return device != nullptr && device->getName().containsIgnoreCase("AirPods");
    }

    /** Three lengths of the same fact. The strip picks whichever fits, so the
        summary shortens rather than ending in an ellipsis: the shorter forms
        were never supplied before, which left every caller handing over one
        long string for the strip to cut in half. */
    struct AudioSummary
    {
        juce::String full, medium, brief;
    };

    AudioSummary audioSummary() const
    {
        auto* d = devices.getCurrentAudioDevice();
        if (d == nullptr)
            return {"no audio device", "no audio", "no audio"};

        const auto rate = juce::String(juce::roundToInt(d->getCurrentSampleRate() / 1000.0)) + "k";
        const auto midi = juce::String(juce::MidiInput::getAvailableDevices().size());

        return {d->getName() + "  " + juce::String(juce::roundToInt(d->getCurrentSampleRate())) + " Hz  ·  " +
                    midi + " midi in",
                d->getName() + "  " + rate, rate + "  ·  " + midi + " midi"};
    }

    void setStripStatus(juce::String text)
    {
        lastStatus = std::move(text);
        const auto s = audioSummary();
        strip.setStatus(lastStatus, s.full, s.medium, s.brief);
    }

    /** The device summary is only true until the device changes, and it
        changes without anything here being asked -- headphones connect, a
        interface is unplugged. Refreshing it on the manager's own change
        message is the only way the strip does not go on naming a device that
        is no longer playing anything. */
    void changeListenerCallback(juce::ChangeBroadcaster*) override { setStripStatus(lastStatus); }

    /** Sizes the window to the editor, whichever process it is drawn by.

        Plugin editors have a designed size and several are bigger than a
        laptop screen; clipping them silently, or forcing them into a fixed
        window, loses controls with no indication that anything is missing.

        @param scrollable  whether the too-big case can be scrolled. It can
                           when the editor is a component in this window, and
                           it cannot when it is the other process's view held
                           over the top -- there is nothing here to scroll.
    */
    void fitWindowTo(int editorWidth, int editorHeight, bool scrollable)
    {
        const auto work =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();

        // Follows the animation rather than the switch: keying this off
        // isOpen() made the window jump to its final height on the first frame
        // while the panel was still growing, which reads as content dropping
        // in from above instead of the panel pushing the window open.
        const int scopeTaken =
            scopePanel != nullptr ? juce::roundToInt(scopeHeight * scopePanel->getExtent()) : 0;

        const int availW = work.getWidth() - 40;
        const int availH = work.getHeight() - 60;

        const int chrome = headerHeight + ControlStrip::heightFor(juce::jmin(editorWidth, availW)) + stripGap;

        editorTooBig = editorWidth > availW || editorHeight + chrome > availH;

        // Room for the scrollbars when the editor will not fit, so they do
        // not sit on top of the plugin's own bottom row.
        const int bars = editorTooBig && scrollable ? 12 : 0;

        setSize(juce::jmin(editorWidth + bars, availW),
                juce::jmin(editorHeight + chrome + scopeTaken + bars, availH));
    }

    /** Whichever editor is loaded, fitted. */
    void refitWindow(bool force = false)
    {
        if (editor != nullptr)
            return fitWindowToEditor(force);

        if (remote != nullptr)
            fitWindowToRemoteEditor(force);
    }

    void fitWindowToEditor(bool force = false)
    {
        if (editor == nullptr || (fitting && !force))
            return;

        // Resizing the window makes the editor lay out again, which calls back
        // in here; without this guard the two chase each other.
        const juce::ScopedValueSetter<bool> guard(fitting, true);

        // No scaling. A plugin's editor is a native view: giving it a smaller
        // frame does not make it draw smaller, it makes it draw the same
        // picture and lose the right and bottom of it. The transform that used
        // to be applied here was producing exactly that, which only became
        // visible once the analyser made the scale drop well below one.
        //
        // Plugins big enough to need it have their own zoom control, and that
        // one actually works, because the plugin does the scaling itself.
        editorScale = 1.0;
        editor->setTransform({});

        fitWindowTo(editor->getWidth(), editor->getHeight(), true);
    }

    void fitWindowToRemoteEditor(bool force = false)
    {
        if (remoteEditorSize.x <= 0 || remoteEditorSize.y <= 0 || (fitting && !force))
            return;

        const juce::ScopedValueSetter<bool> guard(fitting, true);
        fitWindowTo(remoteEditorSize.x, remoteEditorSize.y, false);

        // The window has a new size, so the rectangle the other process is
        // holding its view over is stale until this says otherwise.
        updateRemoteEditorPlacement();
    }

    /**
        Keeps the other process's editor sitting exactly where this window has
        left a hole for it.

        Everything this decides is something the child cannot know: where this
        window is on screen, which window it has to sit above, and whether
        there is anywhere to sit at all. Sent only when one of those changes,
        so dragging the window is a message per frame and sitting still is
        none at all.

        Not in the list: whether this application is in front. It was, and
        that was a mistake -- the editor was taken off the screen every time
        the user looked at another window. The editor is ordered directly
        above this window instead, so another application's window goes above
        both or below both. Activation still matters, but only because it
        reorders windows: it re-sends the placement rather than hiding it.
    */
    void updateRemoteEditorPlacement()
    {
        if (remote == nullptr || !remote->isReady() || remoteEditorSize.x <= 0)
            return;

        auto* top = getTopLevelComponent();
        auto* peer = top != nullptr ? top->getPeer() : nullptr;

        // A panel of this window's own is the one case ordering cannot solve:
        // it is a window too, and no public interface puts one process's
        // window between two of another's. So the editor stands aside for it.
        const bool visible = !loading && overlay == nullptr && top != nullptr && top->isShowing() &&
                             (peer == nullptr || !peer->isMinimised());

        const auto where = localAreaToGlobal(contentArea());
        const auto window = top != nullptr ? EditorProbe::windowNumberOf(*top) : 0;
        const bool inFront = juce::Process::isForegroundProcess();

        if (visible == remoteEditorShown && where == remoteEditorPlaced && window == remoteHostWindow &&
            inFront == remoteWasInFront)
            return;

        const bool appeared = visible != remoteEditorShown;

        // Coming to the front is what reorders this window out from under the
        // editor, so it is what has to put it back. A move is only a move.
        const bool reorder = appeared || inFront != remoteWasInFront || window != remoteHostWindow;

        remoteEditorShown = visible;
        remoteEditorPlaced = where;
        remoteHostWindow = window;
        remoteWasInFront = inFront;
        remote->placeEditor(where, visible, window, reorder);

        // What is painted underneath depends on whether anything is over it.
        if (appeared)
            repaint(contentArea());
    }

    /** The window is sized to the plugin rather than to the display, so it can
        still end up hanging off an edge -- after a plugin resizes itself, or
        after the analyser opens on a short screen. A window whose bottom is
        off-screen has no reachable control strip, which is how the analyser
        became impossible to close. */
    /** Scheduled rather than run inline.

        This is called from resized(), and at that moment this component has
        its new size but the window around it has not caught up -- the window
        tracks the content, and that happens after. Measuring there measures
        the old window, finds nothing to correct, and the window then grows off
        the bottom of the screen with nobody left to notice. */
    void keepOnScreenSoon()
    {
        juce::Component::SafePointer<MainComponent> self(this);
        juce::MessageManager::callAsync(
            [self]
            {
                if (self != nullptr)
                    self->keepOnScreen();
            });
    }

    void keepOnScreen()
    {
        auto* top = getTopLevelComponent();
        if (top == nullptr || top == this)
            return;

        const auto work =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        const auto b = top->getScreenBounds();

        const int x =
            juce::jlimit(work.getX(), juce::jmax(work.getX(), work.getRight() - b.getWidth()), b.getX());
        const int y =
            juce::jlimit(work.getY(), juce::jmax(work.getY(), work.getBottom() - b.getHeight()), b.getY());

        if (x != b.getX() || y != b.getY())
            top->setTopLeftPosition(x, y);
    }

    /** Back closes the plugin, and closing it loses anything unsaved inside
        it. The host cannot know whether there is anything unsaved -- a plugin
        does not tell it -- so it asks rather than guessing. */
    void confirmUnload()
    {
        if (instance == nullptr && remote == nullptr)
            return unload();

        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Close " + loadedName + "?")
                .withMessage("This unloads the plugin. Anything you have not saved inside it -- a "
                             "preset you have been editing, settings it keeps to itself -- goes with "
                             "it.\n\nSave your work in the plugin first if you need it.")
                .withButton("Close plugin")
                .withButton("Cancel"),
            [this](int result)
            {
                // Zero is the first button. It was reading this as one, so
                // "Close plugin" did nothing and "Cancel" would have closed it.
                if (result == 0)
                    unload();
            });
    }

    /** Brings the plugin list back, opaque.

        Loading fades the list out before it begins, and the failure paths only
        set it visible again -- which left it visible at zero alpha. An empty
        window, no list, and no way back to one, after the single operation
        most likely to fail. */
    void showList()
    {
        viewport.setVisible(true);
        pageFade.snapTo(1.0f);
        viewport.setAlpha(1.0f);
        repaintTree(*this);
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
            editorView.setViewedComponent(nullptr, false);
            removeChildComponent(&editorView);
            editor.reset();
        }
        instance.reset();

        // The child is stopped by closing the pipe, which is the same thing
        // that happens if this process dies -- so there is one way out rather
        // than a polite one and an abrupt one.
        trackRemoteEditor(false);
        remote.reset();
        lastLoaded = {};
        loadedName = {};
        viewport.setVisible(true);
        viewport.setAlpha(0.0f);
        pageFade.snapTo(0.0f);
        pageFade.setTarget(1.0f);
        pageAnim.nudge();
        strip.showBack(false);
        setStripStatus("Select a plugin");
        repaint();
    }

    void load(const IndexedPlugin& p)
    {
        unload();

        loading = true;
        loadingName = p.name;
        spinner = 0.0;
        pageFade.setTarget(0.0f);
        pageAnim.nudge();
        setMouseCursor(juce::MouseCursor::WaitCursor);
        startTimerHz(30);
        setStripStatus("Loading " + p.name + "...");
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
        lastLoaded = p;

        if (outOfProcess)
            return doLoadRemote(p);

        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : formats.getFormats())
            if (format->getName() == "VST3")
                format->findAllTypesForFile(found, p.bundle.getFullPathName());

        if (found.isEmpty())
        {
            finishLoading();
            showList();
            setStripStatus("Could not read " + p.name);
            return;
        }

        juce::String error;
        instance = formats.createPluginInstance(*found[0], 48000.0, 512, error);

        if (instance == nullptr)
        {
            finishLoading();
            showList();
            setStripStatus("Failed: " + error);
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
            // Hosted in a viewport rather than placed directly.
            //
            // Editors are routinely larger than the display -- Youlean's is
            // 1740 wide against 1670 of usable screen -- and the host has only
            // three options for the excess: scale it, clip it, or scroll it.
            // Scaling a native view does not shrink what it draws, it only
            // takes away the room it draws into. Clipping is what produced the
            // overlapping, doubled rendering that made this visible. Scrolling
            // is the one that loses nothing.
            editorView.setViewedComponent(editor.get(), false);
            editorView.setScrollBarsShown(true, true, true, true);
            addAndMakeVisible(editorView);
            // Several plugins let the user resize their own editor. When that
            // happens the editor changes its bounds and the host has to follow,
            // or the window no longer matches what the plugin is drawing.
            editor->addComponentListener(this);
            fitWindowToEditor();
        }

        // Given to the plugin before it is given any audio. A plugin reads
        // the playhead inside processBlock, so it has to be there by the time
        // the first block arrives.
        instance->setPlayHead(&playHead);
        player.setProcessor(instance.get());
        rememberInitialState();

        setStripStatus(juce::String(instance->getParameters().size()) + " parameters");
        repaint();
    }

    /** The same load, one process over.

        Nothing here waits for the child: launching returns as soon as the
        process exists, and everything that follows -- the name, the parameter
        list, whether it even loaded -- arrives later on the message thread.
        Which is the right shape anyway. A host that blocked until a plugin
        had loaded would be a host that hangs when a plugin hangs, and that is
        one of the three failures this is supposed to survive. */
    void doLoadRemote(const IndexedPlugin& p)
    {
        auto* device = devices.getCurrentAudioDevice();

        RemotePlugin::Options options;
        options.helper = helperExecutable();
        options.plugin = p.bundle;
        options.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
        options.blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

        remote = std::make_unique<RemotePlugin>();

        juce::Component::SafePointer<MainComponent> safe(this);
        const auto name = p.vendor + "  " + p.name;

        remote->onReady = [safe, name]
        {
            if (safe != nullptr)
                safe->remoteReady(name);
        };

        remote->onChildLost = [safe](const juce::String& why)
        {
            if (safe != nullptr)
                safe->remoteLost(why);
        };

        // Set before the launch rather than after it: the child reports this
        // as soon as the editor exists, and a plugin that opens quickly would
        // otherwise report into a callback that was not there yet.
        remote->onEditorSize = [safe](int w, int h)
        {
            if (safe != nullptr)
                safe->remoteEditorResized(w, h);
        };

        juce::String error;

        if (!remote->launch(options, error))
        {
            remote.reset();
            finishLoading();
            showList();
            setStripStatus("Failed: " + error);
            return;
        }

        setStripStatus("Starting " + p.name + " in its own process...");
    }

    void remoteReady(const juce::String& name)
    {
        if (remote == nullptr)
            return;

        finishLoading();

        loadedName = name;
        strip.showBack(true);

        remote->setPlayHead(&playHead);
        player.setProcessor(remote.get());
        rememberInitialState();

        // Only after a crash, and only if the plugin has the same shape it
        // had before. A restore into a different parameter list would be
        // worse than none: the numbers would land on the wrong controls.
        if (!pendingRestore.values.empty())
        {
            if (pendingRestore.from == lastLoaded.bundle &&
                pendingRestore.values.size() == (size_t) remote->getParameters().size())
                remote->restoreParameters(pendingRestore.values);

            pendingRestore = {};
        }

        // Its editor is drawn by the other process and held over the hole
        // this window leaves for it, so from here it is one window. The size
        // comes back from the child, and the window is fitted to it then.
        trackRemoteEditor(true);
        remote->setEditorVisible(true);

        setStripStatus(juce::String(remote->getParameters().size()) + " parameters  \xc2\xb7  own process");
        repaint();
    }

    /** The child has an editor, and has said how big it wants to be. */
    void remoteEditorResized(int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        const bool first = remoteEditorSize.x <= 0;
        remoteEditorSize = {w, h};
        fitWindowToRemoteEditor(/*force*/ first);
        updateRemoteEditorPlacement();
        repaint();
    }

    /**
        Watches everything that moves the hole the other process is filling.

        The window moving is the one this has to be quick about, so it comes
        from the window itself rather than from a poll. The rest -- another
        application coming to the front, this window being minimised, the
        display arrangement changing -- has no notification worth the code, so
        a slow timer asks. It sends nothing unless the answer has changed.
    */
    void trackRemoteEditor(bool shouldTrack)
    {
        auto* top = getTopLevelComponent();

        if (shouldTrack == trackingRemoteEditor)
            return;

        trackingRemoteEditor = shouldTrack;

        if (!shouldTrack)
        {
            remoteEditorTicker.stopTimer();

            if (top != nullptr && top != this)
                top->removeComponentListener(this);

            remoteEditorSize = {};
            remoteEditorPlaced = {};
            remoteHostWindow = 0;
            remoteEditorShown = false;
            remoteWasInFront = false;
            return;
        }

        if (top != nullptr && top != this)
            top->addComponentListener(this);

        remoteEditorTicker.tick = [this] { updateRemoteEditorPlacement(); };
        remoteEditorTicker.startTimerHz(20);
    }

    /** The case this was all for.

        By the time this runs the plugin's process is already gone and this one
        is still here, which is the whole claim. What is left is to say so, and
        to offer the one thing that is actually useful afterwards. */
    void remoteLost(const juce::String& why)
    {
        const auto name = loadedName;
        const auto plugin = lastLoaded;

        // Taken before the proxy goes, because the proxy is where they are.
        pendingRestore = {plugin.bundle,
                          remote != nullptr ? remote->captureParameters() : std::vector<float>{}};

        player.setProcessor(nullptr);
        allNotesOff();
        trackRemoteEditor(false);
        remote.reset();
        finishLoading();

        setStripStatus(name + " stopped");
        repaint();

        juce::Component::SafePointer<MainComponent> safe(this);

        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle(name.isNotEmpty() ? name + " stopped" : "The plugin stopped")
                .withMessage(why + ".\n\nplugshell kept running because the plugin was in a process of its "
                                   "own. Starting it again restores the parameter values this host was "
                                   "mirroring; anything the plugin kept to itself is gone.")
                .withButton("Start it again")
                .withButton("Back to the list"),
            [safe, plugin](int result)
            {
                if (safe == nullptr)
                    return;

                if (result == 0 && plugin.bundle != juce::File())
                    return safe->load(plugin);

                safe->pendingRestore = {};
                safe->unload();
            });
    }

    /** What is underneath the editor.

        Usually nothing is: the other process holds its view over exactly this
        rectangle. This is what shows in the moments it cannot -- while the
        plugin is still loading, and while this application is not the one in
        front, when the editor has to step off the screen entirely. */
    void paintRemoteNotice(juce::Graphics& g)
    {
        if (remoteEditorShown)
            return;

        auto area = contentArea();

        g.setColour(theme::ink);
        g.setFont(theme::ui(theme::size::title));
        g.drawText(remote->isReady() ? loadedName : "Starting...",
                   area.removeFromTop(area.getHeight() / 2).withTrimmedBottom(34),
                   juce::Justification::centredBottom, true);

        g.setColour(theme::mute);
        g.setFont(theme::ui(theme::size::body));
        g.drawText(remote->isReady() ? "Running in a process of its own. If it crashes, this window stays."
                                     : "Loading in a separate process.",
                   area.removeFromTop(26), juce::Justification::centredTop, true);
    }

    HostPlayHead playHead;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    AnalyserTap tap{player};
    float masterGain = 1.0f;
    /** A timer that is not the loading spinner's. */
    struct Watchdog : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override
        {
            if (tick)
                tick();
        }
    };

    StripButton themeButton{"Dark"};
    ControlServer control{[this](const juce::var& r) { return handleControl(r); }};
    /** The list fades rather than being switched off. Loading takes seconds
        and the window resizes to the editor on the way, so an instant cut in
        the middle of that reads as the window having been replaced. */
    Eased pageFade{1.0f};

    Animator pageAnim{[this]
                      {
                          const bool moving = pageFade.advance(0.22f);

                          if (moving)
                              viewport.setAlpha(pageFade.get());
                          else if (pageFade.get() < 0.01f)
                              viewport.setVisible(false);

                          return moving;
                      }};

    Watchdog stuckKeyWatchdog;
    juce::HashMap<int, int> heldCodes; // character -> hardware key
    AgentBadge agentBadge;
    juce::String agentName;
    std::unique_ptr<ScopePanel> scopePanel;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<RemotePlugin> remote;
    bool outOfProcess = false;

    /** What the other process told us its editor wants to be, and the last
        thing we told it about where to be. Both are how the two windows stay
        one window. */
    /** The session being written down, and what the last tick saw. */
    /** How far a replayed sound may be from the recorded one before it is
        worth saying so. A tenth is past anything quantisation explains and
        well inside the difference a missing wavetable makes. */
    static constexpr double soundTolerance = 0.10;

    /** How far a region may be from what was recorded, on the 0-255 scale the
        cells are on, before it is worth saying so.

        Measured rather than guessed. Searching Serum's wavetable list for a
        name: arriving at it comes back 0.0 away, from two different starting
        points and after a different number of steps each time. Stopping on
        the wrong name comes back between 29.7 and 46.7. There is an order of
        magnitude of daylight between those, and twelve sits in it. */
    static constexpr double lookTolerance = 12.0;

    patch::Patch recording;
    std::vector<float> lastValues, initialValues;
    bool recordingActive = false;
    /** Which operation the next parameter movement belongs to, or -1, and how
        many ticks that claim still has to run. Three is about a third of a
        second: long enough for a plugin to respond to a click, short enough
        that the caller's next deliberate move is its own. */
    static constexpr int expectationTicks = 3;
    int pendingExpectation = -1, expectationTicksLeft = 0;
    juce::Rectangle<int> pendingRegion;
    Watchdog recordTicker;

    juce::Point<int> remoteEditorSize;
    juce::Rectangle<int> remoteEditorPlaced;
    std::uint32_t remoteHostWindow = 0;
    bool remoteEditorShown = false, remoteWasInFront = false, trackingRemoteEditor = false;
    Watchdog remoteEditorTicker;

    /** Kept so that a plugin that dies can be started again without asking
        which one it was. */
    IndexedPlugin lastLoaded;

    /** Every parameter value, taken from a plugin that has just died and put
        back into the one that replaces it.

        Tied to the plugin it came from. Loading something else after a crash
        must not be handed the dead plugin's numbers, and two plugins having
        the same parameter count is not evidence that they are the same
        plugin. */
    struct Restore
    {
        juce::File from;
        std::vector<float> values;
    };

    Restore pendingRestore;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    static constexpr int headerHeight = 50;
    static constexpr int headerInset = 16;
    static constexpr int overlayMinWidth = 700;
    static constexpr int overlayMinHeight = 560;
    static constexpr int themeButtonWidth = 40;
    static constexpr int stripGap = 4;

    int stripHeight() const { return ControlStrip::heightFor(getWidth(), strip.isBackShowing()); }
    int chromeHeight() const { return headerHeight + stripHeight() + stripGap; }
    double editorScale = 1.0;
    bool fitting = false;
    bool editorTooBig = false;
    int lastKeyChar = 0;
    bool overWordmark = false;
    theme::Palette themeFrom, themeTo;
    Eased themeMix{1.0f};

    Animator themeAnim{[this]
                       {
                           const bool moving = themeMix.advance(0.20f);

                           if (moving)
                           {
                               theme::useMix(themeFrom, themeTo, themeMix.get());
                               pushColours();
                           }

                           return moving;
                       }};

    Eased wordmarkGlow{0.0f};

    Animator wordmarkAnim{[this]
                          {
                              const bool moving = wordmarkGlow.advance(0.22f);
                              if (moving)
                                  repaint(wordmarkBounds());
                              return moving;
                          }};
    bool keysEnabled = false;
    int octave = 4;
    bool loading = false;
    double spinner = 0.0;
    juce::String loadingName;
    juce::String lastStatus;
    int indexedCount = 0;
    std::unique_ptr<Overlay> overlay;
    juce::HashMap<int, int> sounding; ///< character -> sounding note
    KeyMonitor keyMonitor;
    DarkLookAndFeel darkLookAndFeel;
    juce::String loadedName;
    PluginList list;
    juce::Viewport viewport;
    juce::Viewport editorView;
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

        const auto serve = tokens.indexOf("--serve");
        if (serve >= 0)
        {
            const int wanted = serve + 1 < tokens.size() && tokens[serve + 1].containsOnly("0123456789")
                                   ? tokens[serve + 1].getIntValue()
                                   : ControlServer::defaultPort;

            const int port = main->startControlServer(wanted);
            std::cout << (port != 0 ? "control: listening on 127.0.0.1:" + juce::String(port)
                                    : "control: could not bind port " + juce::String(wanted))
                      << std::endl;
        }

        // Capture has to wait for the editor to exist and to have drawn at
        // least once; a plugin that opens its window and then loads its skin
        // photographs as an empty rectangle if asked immediately.
        const auto cap = tokens.indexOf("--capture");
        if (cap >= 0 && cap + 1 < tokens.size())
        {
            const juce::File out(tokens[cap + 1]);
            const bool quit = tokens.contains("--quit-after");

            juce::Timer::callAfterDelay(2500,
                                        [main, out, quit]
                                        {
                                            std::cout << main->captureEditor(out) << std::endl;
                                            if (quit)
                                                juce::JUCEApplication::getInstance()->systemRequestedQuit();
                                        });
        }
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
