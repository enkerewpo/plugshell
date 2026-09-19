// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

/**
    The patch file format, round-tripped.

    A patch is meant to be written by one program and read by another, so what
    is worth testing is not that the struct holds its fields but that they
    survive the journey through the file -- including the parts a careless
    serialiser drops: the expectation attached to a pointer operation, and the
    text a value was recorded as.
*/

#include "../src/app/Patch.h"

#include <iostream>

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    if (condition)
        return;

    std::cout << "FAIL: " << what << std::endl;
    ++failures;
}

plugshell::patch::Patch authored()
{
    plugshell::patch::Patch p;
    p.target.plugin = "Pigments";
    p.target.vendor = "Arturia";
    p.target.format = "VST3";
    p.target.version = "6.0.1";
    p.target.parameterCount = 4446;
    p.editorSize = {1280, 780};

    plugshell::patch::Operation cutoff;
    cutoff.kind = plugshell::patch::Operation::Kind::set;
    cutoff.change.index = 546;
    cutoff.change.name = "Filter 1 Cutoff";
    cutoff.change.value = 0.42f;
    cutoff.change.asText = "1.24 kHz";
    p.operations.push_back(cutoff);

    plugshell::patch::Operation wavetable;
    wavetable.kind = plugshell::patch::Operation::Kind::click;
    wavetable.at = {412.0f, 233.0f};
    wavetable.why = "wavetable selection is not an automatable parameter";

    plugshell::patch::Change moved;
    moved.index = 12;
    moved.name = "Osc 1 Wave";
    moved.value = 0.75f;
    moved.asText = "Saw";
    wavetable.expect.push_back(moved);

    wavetable.look.region = {400, 220, 160, 44};
    wavetable.look.cells.assign(plugshell::patch::Look::cellCount, 0);
    for (size_t i = 0; i < wavetable.look.cells.size(); ++i)
        wavetable.look.cells[i] = (juce::uint8) (i % 256);

    p.operations.push_back(wavetable);

    plugshell::patch::Operation page;
    page.kind = plugshell::patch::Operation::Kind::click;
    page.at = {250.0f, 47.0f};
    page.why = "the FX page is not a parameter, and moves none";
    page.look.region = {99, 0, 324, 66};
    page.look.cells.assign(plugshell::patch::Look::cellCount, 128);
    p.operations.push_back(page);

    plugshell::patch::Operation sweep;
    sweep.kind = plugshell::patch::Operation::Kind::drag;
    sweep.at = {100.0f, 200.0f};
    sweep.to = {100.0f, 260.0f};
    p.operations.push_back(sweep);

    return p;
}
} // namespace

int main()
{
    const auto original = authored();

    juce::String error;
    const auto text = original.toJSON();
    const auto back = plugshell::patch::Patch::fromJSON(text, error);

    check(error.isEmpty(), "a patch this program wrote parses back without complaint");
    check(back.target.plugin == "Pigments" && back.target.vendor == "Arturia", "the target survives");
    check(back.target.version == "6.0.1", "the version survives, which is what replay refuses on");
    check(back.editorSize == juce::Point<int>(1280, 780), "the editor size survives");
    check(back.operations.size() == original.operations.size(), "every operation survives");

    if (back.operations.size() == 4)
    {
        const auto& set = back.operations[0];
        check(set.kind == plugshell::patch::Operation::Kind::set, "a set stays a set");
        check(set.change.name == "Filter 1 Cutoff", "the parameter is named");
        check(std::abs(set.change.value - 0.42f) < 1.0e-6f, "the value survives");
        check(set.change.asText == "1.24 kHz", "the text survives, which is what makes it readable");

        const auto& click = back.operations[1];
        check(click.kind == plugshell::patch::Operation::Kind::click, "a click stays a click");
        check(click.at == juce::Point<float>(412.0f, 233.0f), "the coordinate survives");
        check(click.why.startsWith("wavetable"), "the reason for reaching past a parameter survives");
        check(click.expect.size() == 1, "what the click moved survives");

        if (click.expect.size() == 1)
            check(click.expect[0].name == "Osc 1 Wave" && click.expect[0].asText == "Saw",
                  "and survives in full, because it is what replay verifies against");

        check(click.look.recorded(), "what the region looked like survives");
        check(click.look.region == juce::Rectangle<int>(400, 220, 160, 44), "and where it was");
        check(click.look.cells == original.operations[1].look.cells,
              "and every one of its cells, which is what the comparison is made of");

        const auto& page = back.operations[2];
        check(page.look.recorded() && page.expect.empty(),
              "an operation that moves no parameter still carries evidence");

        const auto& drag = back.operations[3];
        check(drag.kind == plugshell::patch::Operation::Kind::drag, "a drag stays a drag");
        check(drag.to == juce::Point<float>(100.0f, 260.0f), "a drag keeps both ends");

        // The measure the replay check is made of.
        check(plugshell::patch::Look::distance(page.look.cells, page.look.cells) == 0.0,
              "a region compared with itself is zero away");

        auto shifted = page.look.cells;
        for (auto& c : shifted)
            c = (juce::uint8) juce::jlimit(0, 255, (int) c + 10);

        check(std::abs(plugshell::patch::Look::distance(page.look.cells, shifted) - 10.0) < 0.01,
              "and ten brighter everywhere is ten away");
    }

    // The sound a patch is supposed to make, which is the check that catches
    // a patch whose every operation applied and which reproduces nothing.
    plugshell::patch::Patch withSound = original;
    withSound.sound.measured = true;
    withSound.sound.peak = 0.463;
    withSound.sound.rms = 0.190;

    juce::String soundError;
    const auto heard = plugshell::patch::Patch::fromJSON(withSound.toJSON(), soundError);
    check(heard.sound.measured && std::abs(heard.sound.rms - 0.190) < 1.0e-9,
          "the recorded sound survives the file");

    plugshell::patch::Sound different;
    different.measured = true;
    different.peak = 1.383;
    different.rms = 0.126;
    check(heard.sound.differenceFrom(different) > 0.10,
          "and a reproduction that is nothing like it is reported as nothing like it");
    check(heard.sound.differenceFrom(heard.sound) < 1.0e-9, "while an identical one is not");

    // A file from somewhere else, refused rather than half-read.
    juce::String other;
    plugshell::patch::Patch::fromJSON("{\"format\":\"something-else/3\"}", other);
    check(other.isNotEmpty(), "a file that is not a patch is refused by name");

    std::cout << (failures == 0 ? "patch: all checks passed" : "patch: FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
