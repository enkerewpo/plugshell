// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace plugshell
{

/**
    Small marks for what a plugin is.

    Drawn rather than bundled. Vendor logos are trademarks and copyrighted
    artwork, and shipping a folder of them is a problem this project does not
    need; these are two primitive shapes of our own that say the one thing the
    list actually has to convey.

    An instrument is keys. An effect is a response curve.
*/
class TypeIcon
{
public:
    static void draw(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& category,
                     juce::Colour colour)
    {
        const auto r = area.toFloat().reduced(1.0f);
        g.setColour(colour);

        if (category == "Instrument")
            drawKeys(g, r);
        else if (category == "Effect")
            drawCurve(g, r);
        else
            drawUnknown(g, r);
    }

private:
    static void drawKeys(juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float w = r.getWidth();
        const float h = r.getHeight() * 0.72f;
        const float y = r.getCentreY() - h * 0.5f;

        g.drawRect(r.getX(), y, w, h, 1.0f);

        // Three white-key divisions and two black keys, which reads as a
        // keyboard at this size without trying to be a literal octave.
        for (int i = 1; i < 3; ++i)
        {
            const float x = r.getX() + w * (float) i / 3.0f;
            g.drawLine(x, y, x, y + h, 1.0f);
        }

        const float bw = w * 0.11f;
        const float bh = h * 0.58f;
        for (const float frac : {0.30f, 0.63f})
            g.fillRect(r.getX() + w * frac - bw * 0.5f, y, bw, bh);
    }

    static void drawCurve(juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path p;
        const float x0 = r.getX();
        const float x1 = r.getRight();
        const float mid = r.getCentreY();

        p.startNewSubPath(x0, mid + r.getHeight() * 0.22f);
        p.quadraticTo(r.getCentreX() - r.getWidth() * 0.08f, mid + r.getHeight() * 0.22f, r.getCentreX(),
                      mid - r.getHeight() * 0.28f);
        p.quadraticTo(r.getCentreX() + r.getWidth() * 0.18f, mid - r.getHeight() * 0.05f, x1,
                      mid - r.getHeight() * 0.02f);

        g.strokePath(p, juce::PathStrokeType(1.4f));
    }

    static void drawUnknown(juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto box = r.withSizeKeepingCentre(r.getWidth() * 0.5f, r.getHeight() * 0.5f);
        g.drawRect(box, 1.0f);
    }
};

} // namespace plugshell
