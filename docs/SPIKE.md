<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Phase 1: feasibility spike

The project rests on two mechanisms that are not guaranteed to work. This phase tests them against real plugins before anything is built on top. Its output is a report, including negative results.

## Questions

1. **Capture.** Can a host obtain a rendered image of a plugin's editor?
2. **Input.** Can a host deliver a synthetic click or drag that the plugin acts on?

Both are per-plugin behaviours. A plugin drawing through Metal may not yield its contents to a view-level snapshot; one running its own event handling may ignore a synthesised event. Reasoning about this in the abstract is not useful, so the spike runs against three plugins chosen for different rendering strategies.

### Targets

| Plugin | Why |
|---|---|
| Arturia Pigments | Multi-engine synth, visually dense, likely GPU-rendered |
| Xfer Serum 2 | Wavetable synth with a custom renderer |
| FabFilter Pro-Q 4 | Interactive curve display, unusual interaction model |

### Strategies to try, in order

**Capture**

1. `juce::createSnapshotOfNativeWindow` — cheapest, works on the view's backing store
2. `NSView` caching (`bitmapImageRepForCachingDisplayInRect:`) — same limitation
3. `ScreenCaptureKit` — reliable, but needs the Screen Recording permission and an on-screen window

**Input**

1. Synthetic `NSEvent` posted to the editor's view — precise, may be ignored
2. Session-level `CGEvent` — more likely to land, less precise, affects the real cursor

### Result matrix

To be filled in by the spike. `—` means not yet run.

| Plugin | JUCE snapshot | NSView cache | ScreenCaptureKit | NSEvent | CGEvent |
|---|---|---|---|---|---|
| Pigments | — | — | — | — | — |
| Serum 2 | — | — | — | — | — |
| Pro-Q 4 | — | — | — | — | — |

---

## Findings so far

### F1 — In-process plugin scanning crashes the host

**Status: confirmed, 2026-09-17. Affects the design.**

The first spike build scanned the machine's installed plugins in-process, using `AudioPluginFormatManager` and `KnownPluginList::scanAndAddFile` directly. The run terminated with `SIGSEGV` (exit 139) partway through the Audio Unit set, after loading a vendor shell plugin.

This is not a bug in the spike. Scanning means loading and instantiating third-party binaries, any one of which may be unstable, may conflict with another vendor's framework already resident in the process, or may simply crash. Observed during the same run, before the fault:

```
objc: Class ResizeWindow is implemented in both
  .../WaveShell1-AU 15.5.component/Contents/MacOS/WaveShell1-AU and
  .../Waves/Modules/InnerProcessDictionary.dylib
  One of the duplicates must be removed or renamed.
```

Duplicate Objective-C class registrations across two vendor bundles in one address space. Any host that loads arbitrary plugins in its own process inherits every such conflict.

**Consequence for the design.** Scanning must run out of process, which is why JUCE ships `PluginDirectoryScanner` together with a subprocess coordinator and why every shipping DAW scans this way. This has a second implication that matters more for this project: if a plugin can crash the scanner, it can crash the *host*, and a crash while an agent is driving a session loses that session. Plugin instances should therefore be considered for out-of-process hosting as well, not only scanning.

That is a larger change than it first appears, because the editor has to be captured and driven in whichever process owns it. Phase 1 should record, for each capture and input strategy, whether it still works across a process boundary. If it does not, the design has to choose between crash isolation and editor access, and that trade-off should be made with evidence rather than assumed.

**Action:** deferred to phase 2, tracked as an open design question. The spike continues in-process for now, against an explicit list of target plugins rather than a full scan, since the crash is a property of scanning everything.

---

## How to run

```bash
make deps      # fetch JUCE
make spike     # build and run
```

The spike prints what it finds and writes nothing. Artefacts, when it produces them, go to `spike/artifacts/`, which is git-ignored.
