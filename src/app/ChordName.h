// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace plugshell
{

/**
    Names the chord formed by a set of held notes.

    Naming a chord from pitches alone is ambiguous: the same three pitch
    classes are a minor chord on one root and a sixth on another, and nothing
    in the notes themselves settles it. This resolves the ambiguity the way a
    player hears it, by preferring the reading whose root is the lowest note,
    and falls back to the shortest interval spelling otherwise. It reports what
    it cannot name rather than guessing.
*/
class ChordName
{
public:
    static juce::String describe(const juce::Array<int>& midiNotes)
    {
        if (midiNotes.isEmpty())
            return {};

        juce::Array<int> sorted(midiNotes);
        std::sort(sorted.begin(), sorted.end());

        if (sorted.size() == 1)
            return noteName(sorted[0]) + octaveOf(sorted[0]);

        if (sorted.size() == 2)
            return noteName(sorted[0]) + " " + intervalName(sorted[1] - sorted[0]);

        // Distinct pitch classes, in ascending order from the bass.
        std::array<bool, 12> present{};
        for (const auto n : sorted)
            present[(size_t) (n % 12)] = true;

        const int bass = sorted[0] % 12;

        // Try the bass first so an inversion is named from what is heard at
        // the bottom, then every other root.
        if (auto named = tryRoot(bass, present); named.isNotEmpty())
            return named;

        for (int root = 0; root < 12; ++root)
            if (present[(size_t) root] && root != bass)
                if (auto named = tryRoot(root, present); named.isNotEmpty())
                    return named + "/" + noteName(bass);

        return juce::String(countPitchClasses(present)) + " notes";
    }

private:
    struct Shape
    {
        const char* suffix;
        std::vector<int> intervals; // semitones above the root
    };

    static const std::vector<Shape>& shapes()
    {
        // Ordered longest first, so a seventh is not reported as a triad that
        // happens to be a subset of it.
        static const std::vector<Shape> table = {{"13#11", {0, 4, 7, 10, 2, 6, 9}},
                                                 {"7alt", {0, 4, 10, 1, 3, 6, 8}},
                                                 {"m11", {0, 3, 7, 10, 2, 5}},
                                                 {"maj9#11", {0, 4, 7, 11, 2, 6}},
                                                 {"9#11", {0, 4, 7, 10, 2, 6}},
                                                 {"13", {0, 4, 7, 10, 2, 9}},
                                                 {"maj13", {0, 4, 7, 11, 2, 9}},
                                                 {"m13", {0, 3, 7, 10, 2, 9}},
                                                 {"13sus4", {0, 5, 7, 10, 2, 9}},
                                                 {"13b9", {0, 4, 7, 10, 1, 9}},
                                                 {"6/9", {0, 4, 7, 9, 2}},
                                                 {"m6/9", {0, 3, 7, 9, 2}},
                                                 {"maj9", {0, 4, 7, 11, 2}},
                                                 {"9", {0, 4, 7, 10, 2}},
                                                 {"m9", {0, 3, 7, 10, 2}},
                                                 {"mMaj9", {0, 3, 7, 11, 2}},
                                                 {"7b9", {0, 4, 7, 10, 1}},
                                                 {"7#9", {0, 4, 7, 10, 3}},
                                                 {"9b5", {0, 4, 6, 10, 2}},
                                                 {"9#5", {0, 4, 8, 10, 2}},
                                                 {"m9b5", {0, 3, 6, 10, 2}},
                                                 {"11", {0, 7, 10, 2, 5}},
                                                 {"maj7#11", {0, 4, 7, 11, 6}},
                                                 {"7#11", {0, 4, 7, 10, 6}},
                                                 {"7b13", {0, 4, 7, 10, 8}},
                                                 {"6", {0, 4, 7, 9}},
                                                 {"m6", {0, 3, 7, 9}},
                                                 {"maj7", {0, 4, 7, 11}},
                                                 {"7", {0, 4, 7, 10}},
                                                 {"m7", {0, 3, 7, 10}},
                                                 {"mMaj7", {0, 3, 7, 11}},
                                                 {"m7b5", {0, 3, 6, 10}},
                                                 {"dim7", {0, 3, 6, 9}},
                                                 {"7sus4", {0, 5, 7, 10}},
                                                 {"7b5", {0, 4, 6, 10}},
                                                 {"7#5", {0, 4, 8, 10}},
                                                 {"maj7#5", {0, 4, 8, 11}},
                                                 {"maj7b5", {0, 4, 6, 11}},
                                                 {"add9", {0, 4, 7, 2}},
                                                 {"madd9", {0, 3, 7, 2}},
                                                 {"add11", {0, 4, 7, 5}},
                                                 {"", {0, 4, 7}},
                                                 {"m", {0, 3, 7}},
                                                 {"dim", {0, 3, 6}},
                                                 {"aug", {0, 4, 8}},
                                                 {"sus4", {0, 5, 7}},
                                                 {"sus2", {0, 2, 7}},
                                                 {"quartal", {0, 5, 10}},
                                                 {"5", {0, 7}}};
        return table;
    }

    static juce::String tryRoot(int root, const std::array<bool, 12>& present)
    {
        const int total = countPitchClasses(present);

        for (const auto& shape : shapes())
        {
            if ((int) shape.intervals.size() != total)
                continue;

            bool all = true;
            for (const auto iv : shape.intervals)
                if (!present[(size_t) ((root + iv) % 12)])
                {
                    all = false;
                    break;
                }

            if (all)
                return noteName(root) + shape.suffix;
        }

        return {};
    }

    static int countPitchClasses(const std::array<bool, 12>& p)
    {
        int n = 0;
        for (const auto b : p)
            if (b)
                ++n;
        return n;
    }

    static juce::String noteName(int midiOrPitchClass)
    {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return names[((midiOrPitchClass % 12) + 12) % 12];
    }

    static juce::String octaveOf(int midi) { return juce::String(midi / 12 - 1); }

    static juce::String intervalName(int semitones)
    {
        static const char* names[] = {"unison",  "m2", "M2", "m3", "M3", "P4",
                                      "tritone", "P5", "m6", "M6", "m7", "M7"};
        const int oct = semitones / 12;
        const auto base = juce::String(names[semitones % 12]);
        return oct > 0 ? base + "+" + juce::String(oct) + "oct" : base;
    }
};

} // namespace plugshell
