// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <vector>

namespace plugshell::patch
{

/**
    A patch: what was done to a plugin, rather than what resulted.

    The reasoning, and the legal position that shapes it, are in
    docs/UNIVERSAL_PRESET.md. This is the data model and its file format, and
    two things about it are worth stating here because they are easy to
    undo by accident.

    **It is written from a recording, never from a preset file.** Nothing in
    this header reads a plugin's stored state. The authoring path is to watch
    a session and write down the operations, which is what makes a patch the
    author's own work rather than a reformatting of somebody else's.

    **Every operation names what it touched.** A parameter is addressed by
    name and index and can be verified by reading it back; a pointer operation
    is addressed by editor coordinate and can only be verified by what it
    moved. That difference is why a recorder prefers a parameter and reaches
    for a coordinate only when the control it needs is not published as one.

    The file is JSON rather than the YAML the design sketch used. The point of
    the sketch was that a patch should be readable and diffable, which JSON is;
    and this host already speaks JSON on its control socket, so a patch can be
    assembled and inspected by the same caller with no second parser.
*/

/** Which plugin a patch was recorded against.

    Checked on replay, and the check is the point: a pointer operation is a
    fact about one version of one editor's layout, and clicking where a knob
    used to be is worse than refusing. */
struct Target
{
    juce::String plugin, vendor, format, version;
    int parameterCount = 0;

    bool matches(const Target& other) const
    {
        return plugin == other.plugin && vendor == other.vendor && format == other.format;
    }
};

/** One parameter, as it stood at a moment.

    The text is the plugin's own rendering of the value, kept alongside it
    because `0.42` is the machine's truth and `"1.24 kHz"` is the reader's --
    and because a later run that renders 0.42 as something else has told you
    the mapping changed. */
struct Change
{
    int index = -1;
    juce::String name;
    float value = 0.0f;
    juce::String asText;
};

/**
    How a rectangle of the editor looked after an operation.

    The escape hatch's other half. A pointer operation that moves a parameter
    can be verified by reading that parameter back; one that does not move any
    -- a tab, a page, a wavetable chosen from the plugin's own list -- leaves
    nothing to read, and until now nothing to check. A picture of the region
    the operation was supposed to change is the only evidence there is.

    Not a hash of the pixels, which is what the design sketch said and what
    does not survive contact with a real editor: plugin interfaces animate.
    Serum's analyser moves on its own, and an exact comparison of any region
    near it fails for reasons that have nothing to do with whether the click
    landed.

    So the region is reduced to a square grid of brightnesses, kept as one
    byte each, and two of them are compared by how far apart they are on
    average. Two things about that were learned by getting them wrong.

    Keeping the brightness rather than a bit per cell. A bit saying "lighter
    than this region's average" is thrown by nothing smaller than half the
    picture changing, and the first thing this was asked to notice -- a tab
    strip where the highlight moved one tab across -- did not move a single
    bit.

    And the grid is sixteen by sixteen, not eight by eight. Eight is enough to
    see a highlight move and not enough to read a word: searching a plugin's
    wavetable list for a name stopped on the wrong one, because at three cells
    to a letter two different names are the same picture. The sound check
    caught it, which is the layering working as intended and not a reason to
    leave a check that cannot tell two labels apart.
*/
struct Look
{
    static constexpr int side = 16;
    static constexpr size_t cellCount = (size_t) side * side;

    juce::Rectangle<int> region; ///< in the editor's own coordinates
    std::vector<juce::uint8> cells;

    bool recorded() const { return !region.isEmpty() && !cells.empty(); }

    /**
        How far apart two signatures are, on the 0-255 scale the cells are on:
        the average over the tenth of cells that disagree most.

        Not the average over all of them, which is the obvious measure and
        gets less useful the finer the grid gets. What separates two words is a
        strong difference in the few cells the letters fall in, and averaging
        that across two hundred and fifty-six cells divides it away -- so a
        search through a plugin's wavetable list stopped on the wrong name at
        both grid sizes, and stopped on it more confidently at the finer one.

        Taking the worst tenth keeps a local difference local. It is still an
        average, so one noisy cell does not decide anything.

        Negative when there is nothing to compare.
    */
    static double distance(const std::vector<juce::uint8>& a, const std::vector<juce::uint8>& b)
    {
        // Sizes rather than a fixed count: a patch written by an older version
        // carries a coarser grid, and refusing to compare it is better than
        // comparing it against something it is not.
        if (a.empty() || a.size() != b.size())
            return -1.0;

        std::vector<int> apart;
        apart.reserve(a.size());

        for (size_t i = 0; i < a.size(); ++i)
            apart.push_back(std::abs((int) a[i] - (int) b[i]));

        const auto worst = (size_t) juce::jmax(1, (int) (apart.size() / 10));
        std::partial_sort(apart.begin(), apart.begin() + (long) worst, apart.end(), std::greater<int>());

        double total = 0.0;

        for (size_t i = 0; i < worst; ++i)
            total += apart[i];

        return total / (double) worst;
    }

    juce::String toHex() const
    {
        juce::String hex;
        hex.preallocateBytes((size_t) cells.size() * 2);

        for (const auto c : cells)
            hex += juce::String::toHexString((int) c).paddedLeft('0', 2);

        return hex;
    }

    static std::vector<juce::uint8> fromHex(const juce::String& hex)
    {
        std::vector<juce::uint8> out;

        for (int i = 0; i + 1 < hex.length(); i += 2)
            out.push_back((juce::uint8) hex.substring(i, i + 2).getHexValue32());

        return out;
    }
};

struct Operation
{
    enum class Kind
    {
        set,   ///< a published parameter, addressed by name
        click, ///< the escape hatch, addressed by editor coordinate
        drag,
        scroll
    };

    Kind kind = Kind::set;

    /** For Kind::set. */
    Change change;

    /** For the pointer kinds, in the editor's own coordinate space -- not the
        screen and not the window, so a patch does not care where the window
        was when it was recorded. */
    juce::Point<float> at, to;

    /** Whether two operations are the same gesture, so a run of them can be
        collapsed into one search. */
    bool sameGestureAs(const Operation& other) const
    {
        return kind == other.kind && at == other.at && to == other.to &&
               std::abs(delta - other.delta) < 1.0e-6f;
    }
    float delta = 0.0f;

    /** Why a coordinate was needed rather than a parameter. Written by whoever
        records the operation; a patch full of unexplained clicks is a macro
        recording, and the difference is the whole of this format's claim. */
    juce::String why;

    /** What the operation moved, for pointer kinds. Most editor actions -- even
        on controls that are not themselves parameters -- move something that
        is, so this is usually enough to tell a click that landed from one that
        missed, without comparing a single pixel. */
    std::vector<Change> expect;

    /** And what it was supposed to look like, for the operations that move no
        parameter at all. */
    Look look;

    /**
        How many times replay may repeat this operation while it waits for the
        editor to look like @c look. Zero means do it once.

        This is the difference between recording a path and recording a
        destination, and it is the whole of what makes a pointer operation
        worth distributing. "Click the wavetable arrow three times" is a fact
        about the list as it stood when the patch was made: reorder the
        folder, start from a different wavetable, and three clicks land
        somewhere else with nothing to say so. "Click the arrow until the name
        box looks like this" is a fact about where the patch was trying to
        get to -- which is the thing a preset file has and an action log does
        not.

        It is a search, so it can fail, and failing is the point: a budget
        that runs out is a patch saying it could not find what it was looking
        for, rather than one that clicked a fixed number of times and stopped
        somewhere.
    */
    int seekSteps = 0;

    static const char* nameOf(Kind k)
    {
        switch (k)
        {
        case Kind::set:
            return "set";
        case Kind::click:
            return "click";
        case Kind::drag:
            return "drag";
        case Kind::scroll:
            return "scroll";
        }

        return "set";
    }

    static bool kindFor(const juce::String& name, Kind& out)
    {
        for (const auto k : {Kind::set, Kind::click, Kind::drag, Kind::scroll})
            if (name == nameOf(k))
            {
                out = k;
                return true;
            }

        return false;
    }
};

/**
    What the patch is supposed to sound like.

    A patch made only of parameters can be complete, correct, fully applied,
    and still not reproduce anything: a plugin whose timbre lives somewhere its
    parameter list does not reach -- Serum's wavetable selection, for one --
    will report every operation as successful and play something else entirely.
    That is the failure this format most needs to be able to see, because it is
    the one that looks like success from every other angle.

    So a patch carries a measurement of its own sound: one note, rendered
    offline, reduced to a peak and an RMS. It is a coarse fingerprint and it
    does not need to be more than that. The case it exists to catch is not a
    subtle drift, it is playing a saw where a wavetable should be, and that is
    four times the level difference.

    A peak and an RMS rather than a comparison of the waveforms, and that is
    not a shortcut. Measured on Serum: two renders of one unchanged state
    correlate at -0.73, because the oscillator's phase is free-running and
    every note starts somewhere else. A waveform comparison would call an
    exact reproduction a failure. Peak and RMS do not move with phase -- the
    same two renders agree to four decimal places -- and they were four times
    apart for the reproduction that genuinely failed. */
struct Sound
{
    bool measured = false;
    int note = 60;
    double durationSec = 2.0;
    double peak = 0.0, rms = 0.0;

    /** How far off @p other is, as a fraction of this one. Negative when
        nothing was measured to compare against. */
    double differenceFrom(const Sound& other) const
    {
        if (!measured || !other.measured)
            return -1.0;

        const auto relative = [](double a, double b)
        { return std::abs(a - b) / juce::jmax(1.0e-4, juce::jmax(std::abs(a), std::abs(b))); };

        return juce::jmax(relative(peak, other.peak), relative(rms, other.rms));
    }
};

struct Patch
{
    static constexpr const char* formatName = "plugshell-patch/1";

    Target target;

    /** The starting point, and the only one this version supports. A patch is
        a path from the plugin's own freshly instantiated state: determinate,
        the same on every machine, and nobody's data. Anything else would mean
        naming a factory preset or carrying a blob, which is what this format
        exists not to do. */
    juce::String from{"factory-init"};

    /** The editor's size when the patch was recorded, so coordinates recorded
        against it can be scaled to an editor opened at a different one. */
    juce::Point<int> editorSize;

    std::vector<Operation> operations;

    /** What it sounded like when it was recorded. */
    Sound sound;

    // ------------------------------------------------------------ the file

    juce::var toVar() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("format", formatName);

        auto* t = new juce::DynamicObject();
        t->setProperty("plugin", target.plugin);
        t->setProperty("vendor", target.vendor);
        t->setProperty("format", target.format);
        t->setProperty("version", target.version);
        t->setProperty("parameterCount", target.parameterCount);
        root->setProperty("target", juce::var(t));

        root->setProperty("from", from);

        if (editorSize.x > 0 && editorSize.y > 0)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty("width", editorSize.x);
            e->setProperty("height", editorSize.y);
            root->setProperty("editor", juce::var(e));
        }

        juce::Array<juce::var> items;

        for (const auto& op : operations)
            items.add(toVar(op));

        root->setProperty("operations", items);

        if (sound.measured)
        {
            auto* snd = new juce::DynamicObject();
            snd->setProperty("note", sound.note);
            snd->setProperty("durationSec", sound.durationSec);
            snd->setProperty("peak", sound.peak);
            snd->setProperty("rms", sound.rms);
            root->setProperty("sound", juce::var(snd));
        }

        return juce::var(root);
    }

    static Patch fromVar(const juce::var& v, juce::String& error)
    {
        Patch p;

        if (v.getProperty("format", "").toString() != formatName)
        {
            error = "not a " + juce::String(formatName) + " file";
            return p;
        }

        const auto t = v.getProperty("target", juce::var());
        p.target.plugin = t.getProperty("plugin", "").toString();
        p.target.vendor = t.getProperty("vendor", "").toString();
        p.target.format = t.getProperty("format", "").toString();
        p.target.version = t.getProperty("version", "").toString();
        p.target.parameterCount = (int) t.getProperty("parameterCount", 0);

        p.from = v.getProperty("from", "factory-init").toString();

        const auto e = v.getProperty("editor", juce::var());
        p.editorSize = {(int) e.getProperty("width", 0), (int) e.getProperty("height", 0)};

        if (v.hasProperty("sound"))
        {
            const auto snd = v.getProperty("sound", juce::var());
            p.sound.measured = true;
            p.sound.note = (int) snd.getProperty("note", 60);
            p.sound.durationSec = (double) snd.getProperty("durationSec", 2.0);
            p.sound.peak = (double) snd.getProperty("peak", 0.0);
            p.sound.rms = (double) snd.getProperty("rms", 0.0);
        }

        if (auto* items = v.getProperty("operations", juce::var()).getArray())
            for (const auto& item : *items)
            {
                Operation op;

                if (fromVar(item, op))
                    p.operations.push_back(std::move(op));
            }

        return p;
    }

    juce::String toJSON() const { return juce::JSON::toString(toVar(), false); }

    static Patch fromJSON(const juce::String& text, juce::String& error)
    {
        juce::var parsed;

        if (juce::JSON::parse(text, parsed).failed() || !parsed.isObject())
        {
            error = "the patch is not valid JSON";
            return {};
        }

        return fromVar(parsed, error);
    }

private:
    static juce::var toVar(const Change& c)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("index", c.index);
        o->setProperty("name", c.name);
        o->setProperty("value", c.value);

        if (c.asText.isNotEmpty())
            o->setProperty("asText", c.asText);

        return juce::var(o);
    }

    static Change changeFrom(const juce::var& v)
    {
        Change c;
        c.index = (int) v.getProperty("index", -1);
        c.name = v.getProperty("name", "").toString();
        c.value = (float) (double) v.getProperty("value", 0.0);
        c.asText = v.getProperty("asText", "").toString();
        return c;
    }

    static juce::var toVar(const Operation& op)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("op", Operation::nameOf(op.kind));

        if (op.kind == Operation::Kind::set)
        {
            o->setProperty("parameter", op.change.name);
            o->setProperty("index", op.change.index);
            o->setProperty("value", op.change.value);

            if (op.change.asText.isNotEmpty())
                o->setProperty("asText", op.change.asText);

            return juce::var(o);
        }

        juce::Array<juce::var> at{op.at.x, op.at.y};
        o->setProperty("at", at);

        if (op.kind == Operation::Kind::drag)
        {
            juce::Array<juce::var> to{op.to.x, op.to.y};
            o->setProperty("to", to);
        }

        if (op.kind == Operation::Kind::scroll)
            o->setProperty("delta", op.delta);

        if (op.seekSteps > 0)
        {
            auto* seek = new juce::DynamicObject();
            seek->setProperty("maxSteps", op.seekSteps);
            o->setProperty("seek", juce::var(seek));
        }

        if (op.why.isNotEmpty())
            o->setProperty("why", op.why);

        if (!op.expect.empty() || op.look.recorded())
        {
            auto* expect = new juce::DynamicObject();

            if (!op.expect.empty())
            {
                juce::Array<juce::var> moved;

                for (const auto& c : op.expect)
                    moved.add(toVar(c));

                expect->setProperty("moved", moved);
            }

            if (op.look.recorded())
            {
                const auto& r = op.look.region;
                expect->setProperty("region",
                                    juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()});
                expect->setProperty("looksLike", op.look.toHex());
            }

            o->setProperty("expect", juce::var(expect));
        }

        return juce::var(o);
    }

    static bool fromVar(const juce::var& v, Operation& op)
    {
        if (!Operation::kindFor(v.getProperty("op", "").toString(), op.kind))
            return false;

        if (op.kind == Operation::Kind::set)
        {
            op.change.name = v.getProperty("parameter", "").toString();
            op.change.index = (int) v.getProperty("index", -1);
            op.change.value = (float) (double) v.getProperty("value", 0.0);
            op.change.asText = v.getProperty("asText", "").toString();
            return true;
        }

        const auto point = [](const juce::var& a)
        {
            if (auto* array = a.getArray(); array != nullptr && array->size() >= 2)
                return juce::Point<float>((float) (double) array->getReference(0),
                                          (float) (double) array->getReference(1));

            return juce::Point<float>();
        };

        op.at = point(v.getProperty("at", juce::var()));
        op.to = point(v.getProperty("to", juce::var()));
        op.delta = (float) (double) v.getProperty("delta", 0.0);
        op.why = v.getProperty("why", "").toString();
        op.seekSteps = (int) v.getProperty("seek", juce::var()).getProperty("maxSteps", 0);

        const auto expect = v.getProperty("expect", juce::var());

        if (auto* moved = expect.getProperty("moved", juce::var()).getArray())
            for (const auto& c : *moved)
                op.expect.push_back(changeFrom(c));

        if (auto* r = expect.getProperty("region", juce::var()).getArray(); r != nullptr && r->size() >= 4)
        {
            op.look.region = {(int) r->getReference(0), (int) r->getReference(1), (int) r->getReference(2),
                              (int) r->getReference(3)};
            op.look.cells = Look::fromHex(expect.getProperty("looksLike", "").toString());
        }

        return true;
    }
};

// ----------------------------------------------------------------- recording

/** Every parameter's value, for diffing against a moment later.

    Values only. The text is what makes a patch readable, and it is also a
    call into the plugin per parameter -- with four thousand of them that is
    not something to do on a timer, so it is asked for afterwards and only for
    what actually moved. */
inline std::vector<float> valuesOf(const juce::AudioProcessor& processor)
{
    const auto& all = processor.getParameters();

    std::vector<float> values;
    values.reserve((size_t) all.size());

    for (const auto* p : all)
        values.push_back(p->getValue());

    return values;
}

/** What moved between two snapshots, with the plugin asked how it reads now.

    @param epsilon  how much counts as a move. Parameters arrive from a plugin
                    as floats it has itself quantised, and a knob that reports
                    0.4199999 where it reported 0.42 has not been touched. */
inline std::vector<Change> movedBetween(const juce::AudioProcessor& processor,
                                        const std::vector<float>& before, const std::vector<float>& after,
                                        float epsilon = 1.0e-5f)
{
    const auto& all = processor.getParameters();

    std::vector<Change> moved;

    for (size_t i = 0; i < after.size() && i < before.size(); ++i)
    {
        if (std::abs(after[i] - before[i]) <= epsilon)
            continue;

        if ((int) i >= all.size())
            break;

        Change c;
        c.index = (int) i;
        c.name = all[(int) i]->getName(128);
        c.value = after[i];
        c.asText = all[(int) i]->getCurrentValueAsText();
        moved.push_back(std::move(c));
    }

    return moved;
}

/**
    The signature of a rectangle of an image: a square grid of cells, each the
    average brightness of its part of the region.

    Coarse enough not to move with a pixel of antialiasing or with whatever an
    analyser is drawing in the corner of the region, and fine enough to tell
    one word from another -- which is what a plugin's own lists are made of.
*/
inline std::vector<juce::uint8> signatureOf(const juce::Image& image, juce::Rectangle<int> region,
                                            int side = Look::side)
{
    const auto area = region.getIntersection(image.getBounds());

    if (area.isEmpty() || side <= 0)
        return {};

    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);

    std::vector<juce::uint8> out;
    out.reserve((size_t) side * (size_t) side);

    for (int cy = 0; cy < side; ++cy)
        for (int cx = 0; cx < side; ++cx)
        {
            const juce::Rectangle<int> box(
                area.getX() + area.getWidth() * cx / side, area.getY() + area.getHeight() * cy / side,
                juce::jmax(1, area.getWidth() / side), juce::jmax(1, area.getHeight() / side));

            const auto clipped = box.getIntersection(area);

            double total = 0.0;
            int counted = 0;

            for (int y = clipped.getY(); y < clipped.getBottom(); ++y)
                for (int x = clipped.getX(); x < clipped.getRight(); ++x)
                {
                    total += pixels.getPixelColour(x, y).getBrightness();
                    ++counted;
                }

            out.push_back((juce::uint8) juce::jlimit(
                0, 255, juce::roundToInt((counted > 0 ? total / counted : 0.0) * 255.0)));
        }

    return out;
}

} // namespace plugshell::patch
