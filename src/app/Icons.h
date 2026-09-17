// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
//
// The icon outlines in this file are from Lucide (https://lucide.dev),
// ISC License, Copyright (c) Lucide Icons and Contributors, embedded
// unmodified as source strings.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    A handful of drawn marks, taken from an icon set rather than invented.

    Drawing them by hand produced shapes recognisable to whoever had just drawn
    them and to nobody else: a crescent that read as a bitten circle, a
    half-filled disc that read as nothing at all. An icon has to be recognised
    rather than deduced, and a set that has already been through that test is
    worth more than the hour it takes to draw a worse one.

    Parsed as SVG rather than through Path::restoreFromString, which takes
    JUCE's own path format and not this one -- it has no arc command, and half
    of these outlines are arcs.
*/
class Icons
{
public:
    enum class Name
    {
        sun,
        moon,
        sunMoon, ///< follows the system
        chevronLeft
    };

    /** Draws `name` centred in `box`, recoloured to `colour`. */
    static void draw(juce::Graphics& g, Name name, juce::Rectangle<float> box, juce::Colour colour)
    {
        auto path = cached(name);

        // Square, so the 24-unit grid the set is drawn on keeps its
        // proportions and every icon comes out at the same visual size.
        const float side = juce::jmin(box.getWidth(), box.getHeight());
        const float scale = side / 24.0f;

        path.applyTransform(juce::AffineTransform::scale(scale).translated(box.getCentreX() - side * 0.5f,
                                                                           box.getCentreY() - side * 0.5f));

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(2.0f * scale, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

private:
    /** Parsed once per icon. These are redrawn on every hover frame and SVG
        parsing is not work to repeat sixty times a second.

        parseSVGPath rather than the document parser: it takes exactly the "d"
        attribute these outlines are made of, including the arc command that
        JUCE's own path format has no equivalent for. */
    static const juce::Path& cached(Name name)
    {
        static std::map<Name, juce::Path> store;

        auto found = store.find(name);
        if (found != store.end())
            return found->second;

        juce::Path built;
        for (const auto& d : outlines(name))
            built.addPath(juce::Drawable::parseSVGPath(d));

        return store.emplace(name, std::move(built)).first->second;
    }

    static juce::StringArray rays()
    {
        return {"M12 2v2", "M12 20v2", "m4.93 4.93 1.41 1.41",  "m17.66 17.66 1.41 1.41",
                "M2 12h2", "M20 12h2", "m6.34 17.66-1.41 1.41", "m19.07 4.93-1.41 1.41"};
    }

    static juce::StringArray outlines(Name name)
    {
        switch (name)
        {
        case Name::sun:
        {
            auto all = rays();
            // The disc, as a path, since parseSVGPath takes no <circle>.
            all.add("M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0");
            return all;
        }

        case Name::moon:
            return {"M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 "
                    "8.268 8.268c.344-.215.825-.004.803.401"};

        case Name::sunMoon:
            return {"M12 2v2",
                    "M14.837 16.385a6 6 0 1 1-7.223-7.222c.624-.147.97.66.715 1.248a4 4 0 0 0 "
                    "5.26 5.259c.589-.255 1.396.09 1.248.715",
                    "M16 12a4 4 0 0 0-4-4", "m19 5-1.256 1.256", "M20 12h2"};

        case Name::chevronLeft:
            return {"m15 18-6-6 6-6"};
        }

        return {};
    }
};

} // namespace plugshell
