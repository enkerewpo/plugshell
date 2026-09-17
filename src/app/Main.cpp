// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <iostream>

#include "AnalyserTap.h"
#include "ChordName.h"
#include "ControlServer.h"
#include "Eased.h"
#include "EditorProbe.h"
#include "KeyMonitor.h"
#include "KeyboardMap.h"
#include "OfflineRender.h"
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

    ControlStrip()
    {
        for (auto* b : {&back, &keys, &scope, &help, &settings})
        {
            b->setColours(theme::ink, theme::mute, theme::hair);
            addAndMakeVisible(b);
        }

        keys.setFramed(true);
        back.setGlyph(StripButton::Glyph::back);
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

    /** How tall the strip needs to be at a given width.

        Five buttons and two pieces of status do not fit across a narrow
        window, and the previous answer -- drop whatever does not fit -- took
        the Scope button with it, so the analyser became unreachable on any
        plugin with a narrow editor. Wrapping to a second row keeps every
        control present at every width, which is what a control strip is for. */
    static int heightFor(int width) { return width < 620 ? rowHeight * 2 - 8 : rowHeight; }

    void refreshColours()
    {
        for (auto* b : {&back, &keys, &scope, &help, &settings})
            b->setColours(theme::ink, theme::mute, theme::hair);

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
        // On two rows the text has a row to itself and needs no dodging; on
        // one it has to keep clear of the buttons and of Back.
        auto row = textRow;
        if (!twoRows)
        {
            row = getLocalBounds();
            row.removeFromRight(getWidth() - keys.getX()); // buttons own the right
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
        twoRows = getHeight() >= rowHeight * 2 - 8;

        auto buttons = twoRows ? r.removeFromBottom(rowHeight) : r;

        // The keys button carries the octave number and wants the room, but
        // not at the cost of pushing a whole button off a narrow strip.
        const int keysWidth = getWidth() < 420 ? 104 : 126;

        settings.setBounds(buttons.removeFromRight(74));
        help.setBounds(buttons.removeFromRight(56));
        scope.setBounds(buttons.removeFromRight(72).reduced(4, 0));
        keys.setBounds(buttons.removeFromRight(keysWidth).reduced(6, 0));

        if (twoRows)
        {
            // Back goes up with the status rather than competing with four
            // other buttons for a narrow row, where it was left showing "B...".
            back.setBounds(r.removeFromLeft(back.isVisible() ? 86 : 16).reduced(6, 4));
            textRow = r.withTrimmedRight(16);
        }
        else
        {
            back.setBounds(buttons.removeFromLeft(86).reduced(6, 0));
            textRow = juce::Rectangle<int>();
        }
    }

private:
    bool twoRows = false;
    juce::Rectangle<int> textRow;
    StripButton back{"Back"}, keys{"keys off"}, scope{"Scope"}, help{"Help"}, settings{"Settings"};
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
        blurb.setText("A macOS host for audio plugins, built so that a program can work one: "
                      "read its parameters, see its editor, and operate the controls that are not "
                      "parameters.",
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
        addAndMakeVisible(themeButton);
        themeButton.setColours(theme::ink, theme::mute, theme::hair);
        themeButton.onClick = [this] { cycleTheme(); };
        refreshThemeButton();

        scopePanel->onClick = [this] { showScopeDetail(); };
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
            // An editor that already fills the display has no room to grow
            // into, and growing anyway only clips it -- so for those the
            // analyser opens over the whole window instead of under it. Same
            // button, same instrument, the one that fits.
            if (!scopePanel->isOpen() && editor != nullptr && !roomForInlineScope())
                return showScopeDetail();

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
        setSize(920, 640);
    }

    /** Whether the window can grow by the analyser's height without the
        editor losing anything off the bottom of the display. */
    bool roomForInlineScope() const
    {
        if (editor == nullptr)
            return true;

        const auto work =
            juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        return editor->getHeight() + chromeHeight() + scopeHeight <= work.getHeight() - 60;
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
        o->setFooter("Keys keep playing while this is open. esc to close.");
        showOverlay(std::move(o));
    }

    /** Answers one request from the control socket. Runs on the message
        thread, so it may touch the plugin and the editor freely. */
    juce::var handleControl(const juce::var& request)
    {
        const auto op = request.getProperty("op", "").toString();

        if (op == "state")
            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("loaded", instance != nullptr);
                    o.setProperty("name", loadedName);
                    o.setProperty("hasEditor", editor != nullptr);
                    o.setProperty("keysEnabled", keysEnabled);
                    o.setProperty("theme", theme::mode == theme::Mode::dark    ? "dark"
                                           : theme::mode == theme::Mode::light ? "light"
                                                                               : "auto");
                    o.setProperty("themeIsDark", theme::isDark());
                    o.setProperty("themeBase", theme::base.toDisplayString(false));
                    o.setProperty("settingsFile", settingsFile().getFullPathName());
                    o.setProperty("octave", octave);
                    if (instance != nullptr)
                    {
                        o.setProperty("parameterCount", instance->getParameters().size());
                        o.setProperty("programCount", instance->getNumPrograms());
                        o.setProperty("currentProgram", instance->getCurrentProgram());
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

        if (instance == nullptr)
            return error("no plugin is loaded");

        if (op == "params")
        {
            // Paged and searchable, not a dump. Pigments publishes 4446
            // parameters; asking for all of them at once is slow to build,
            // slow to send, and not what a caller wanted anyway -- the real
            // question is almost always "which parameter is the filter
            // cutoff", which is a search.
            const auto& ps = instance->getParameters();
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
            for (int i = 0; i < instance->getNumPrograms(); ++i)
                items.add(instance->getProgramName(i));
            return okWith([&items](juce::DynamicObject& o) { o.setProperty("programs", items); });
        }

        if (op == "program")
        {
            const int index = (int) request.getProperty("index", -1);
            if (index < 0 || index >= instance->getNumPrograms())
                return error("program index out of range");
            instance->setCurrentProgram(index);
            return okWith(
                [this](juce::DynamicObject& o)
                {
                    o.setProperty("currentProgram", instance->getCurrentProgram());
                    o.setProperty("name", instance->getProgramName(instance->getCurrentProgram()));
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

            auto* device = devices.getCurrentAudioDevice();
            const auto r = OfflineRender::run(
                *instance, juce::File(path), o, device != nullptr ? device->getCurrentSampleRate() : 48000.0,
                device != nullptr ? device->getCurrentBufferSizeSamples() : 512);

            player.setProcessor(instance.get());

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

        if (op == "scroll")
        {
            EditorProbe::mouse(*editor, EditorProbe::MouseAction::scroll, at,
                               (float) (double) request.getProperty("delta", 1.0));
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
            return okWith([](juce::DynamicObject&) {});
        }

        const auto to = point("to");
        if (to.x < 0.0f)
            return error("drag needs \"to\": [x, y]");

        EditorProbe::drag(*editor, at, to, (int) request.getProperty("steps", 24));
        return okWith([](juce::DynamicObject&) {});
    }

    juce::AudioProcessorParameter* findParameter(const juce::var& request) const
    {
        const auto& ps = instance->getParameters();
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
        if (editor != nullptr && !roomForInlineScope())
            return showScopeDetail();

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

        theme::mode = theme::mode == Mode::dark    ? Mode::light
                      : theme::mode == Mode::light ? Mode::automatic
                                                   : Mode::dark;

        beginThemeChange();
        saveSettings();
    }

    /** Cross-fades between the two palettes rather than cutting.

        Every surface in the window changes at once, and a cut of that size
        reads as the window having been replaced. Fading it makes it obviously
        the same window in different light, which is what actually happened. */
    void beginThemeChange()
    {
        themeFrom = theme::paletteFor(!theme::isDark());
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
    }

    void saveSettings() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("theme", theme::mode == theme::Mode::dark    ? "dark"
                                : theme::mode == theme::Mode::light ? "light"
                                                                    : "auto");

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
        themeButton.setBounds(r.getWidth() - 84, 9, 70, 32);
        strip.setBounds(r.removeFromBottom(stripHeight()));

        if (scopePanel != nullptr)
        {
            const int h = juce::roundToInt(scopeHeight * scopePanel->getExtent());
            scopePanel->setBounds(r.removeFromBottom(h));
            scopePanel->setVisible(h > 0);
        }
        keepOnScreen();

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

        overlay->toFront(true);
        overlay->grabKeyboardFocus();

        // After the window exists, is positioned and has been drawn once.
        // Fading something that is still being built animates the setup cost
        // rather than the panel.
        overlay->beginFadeIn();
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
                      KeyboardMap::preferredHeight);

        o->setFooter("Cmd-K turns the computer keyboard on and off. It is off by default, because "
                     "plugin editors want the keyboard too -- for typing values and searching presets. "
                     "Double click a plugin in the list to load it.");

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
        SettingsBody(std::unique_ptr<juce::Component> main, std::function<juce::String()> latency)
            : content(std::move(main)), latencyText(std::move(latency))
        {
            addAndMakeVisible(content.get());

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

        static int heightOfRows() { return rowHeight * 2 + latencyHeight; }

        void paint(juce::Graphics& g) override
        {
            // The buffer size above this says 5 ms and the real latency is
            // often thirty times that, which reads as a contradiction unless
            // the arithmetic is shown. It is not a contradiction: one is what
            // the host schedules, the other is what reaches the ear.
            auto row = getLocalBounds()
                           .removeFromBottom(rowHeight * 2 + latencyHeight)
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

            r.removeFromBottom(latencyHeight);
            content->setBounds(r.withTrimmedBottom(8));
        }

    private:
        static constexpr int rowHeight = 58;
        static constexpr int latencyHeight = 34;

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
        o->setContent(std::make_unique<SettingsBody>(std::move(sel), [this] { return latencyBreakdown(); }),
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
    }

    static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    /** @return true when the event has been consumed. */
    bool onKey(int c, int keyCode, bool isDown, bool isRepeat, bool commandDown)
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

        const int chrome =
            headerHeight + ControlStrip::heightFor(juce::jmin(editor->getWidth(), availW)) + stripGap;

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

        const int wantW = editor->getWidth();
        const int wantH = editor->getHeight() + chrome + scopeTaken;

        editorTooBig = wantW > availW || editor->getHeight() + chrome > availH;

        setSize(juce::jmin(wantW, availW), juce::jmin(wantH, availH));
    }

    /** The window is sized to the plugin rather than to the display, so it can
        still end up hanging off an edge -- after a plugin resizes itself, or
        after the analyser opens on a short screen. A window whose bottom is
        off-screen has no reachable control strip, which is how the analyser
        became impossible to close. */
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
            removeChildComponent(editor.get());
            editor.reset();
        }
        instance.reset();
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
            addAndMakeVisible(editor.get());
            // Several plugins let the user resize their own editor. When that
            // happens the editor changes its bounds and the host has to follow,
            // or the window no longer matches what the plugin is drawing.
            editor->addComponentListener(this);
            fitWindowToEditor();
        }

        player.setProcessor(instance.get());

        setStripStatus(juce::String(instance->getParameters().size()) + " parameters");
        repaint();
    }

    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    AnalyserTap tap{player};
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
    std::unique_ptr<ScopePanel> scopePanel;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    static constexpr int headerHeight = 50;
    static constexpr int stripGap = 4;

    int stripHeight() const { return ControlStrip::heightFor(getWidth()); }
    int chromeHeight() const { return headerHeight + stripHeight() + stripGap; }
    double editorScale = 1.0;
    bool fitting = false;
    bool editorTooBig = false;
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
