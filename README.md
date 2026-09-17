<p align="center"><img src="assets/hero.webp" alt="plugshell hosting Arturia Pigments with its four analysers open" width="100%"></p>

# plugshell

**An agent-operable plugin host.**

plugshell loads VST3 plugins and exposes them to software over a local socket:
parameters, presets, offline rendering, an image of the plugin's editor, and
synthetic input into that editor. It is built for a caller that is a program
rather than a person.

**Status:** early. Everything listed under [What works](#what-works) is
implemented and in use; everything under [Roadmap](#roadmap) is not.

**Platforms:** macOS today. Editor capture and synthetic input are the only
platform-specific components, and both sit behind a single interface,
`EditorProbe`. Windows and Linux require a second implementation of that
interface rather than a second application.

---

## Quickstart

### Install

Download the disk image from
[Releases](https://github.com/enkerewpo/plugshell/releases), drag
`plugshell.app` onto Applications, and open it.

The first launch is blocked. macOS cannot verify an application that is not
signed with an Apple Developer ID. To allow it, once:

> **System Settings → Privacy & Security**, scroll to the message about
> plugshell, click **Open Anyway**, confirm.

The Control-click → Open method described in older instructions was removed
from macOS and no longer works.

### Or build from source

```sh
git clone https://github.com/enkerewpo/plugshell.git
cd plugshell
make deps && make build
open build/src/app/plugshell_app_artefacts/RelWithDebInfo/plugshell.app
```

Requires the Xcode command line tools, CMake and Ninja. See
[docs/BUILD.md](docs/BUILD.md). A local build is not subject to the Gatekeeper
step above.

### Permissions

Two features require macOS permission, and both fail silently without it: the
operation reports success and nothing happens.

| Permission | Required for |
|---|---|
| Screen Recording | reading a plugin's editor as an image |
| Accessibility | clicking and dragging inside that editor |

**Settings** shows the current state of each. All other functionality —
loading plugins, parameters, the keyboard, the analysers, offline rendering —
works without them. macOS applies Screen Recording only at launch, so quit and
reopen after granting it.

---

## What works

**VST3 hosting, non-blocking.** Plugins and their editors open without
suspending the calling program, which continues to observe and drive the host
while the window is open. The window follows the plugin's own size, including
when the plugin resizes itself, and scrolls an editor larger than the display
rather than clipping it.

**Parameter access.** Read and write by index, paged and searchable. Parameter
counts are large in practice — Pigments publishes 4446 — so search is the
primary access path.

**Editor capture.** The editor is captured through the window server, which
works for GPU-drawn editors. This makes a plugin's own interface available to
a caller as an image.

**Synthetic input.** Clicks, drags and scrolls delivered in the editor's
coordinate space. This reaches controls a plugin does not publish as
parameters: wavetable selectors, modulation matrices operated by dragging,
preset browsers.

**Offline rendering, faster than real time.** A parameter vector in, audio
out, with no audio device and no wall-clock wait. Two seconds of Pigments
renders in 53 ms.

**Computer-keyboard MIDI.** Two octaves in the tracker layout, with the held
chord named — inversions, alterations and slash chords included.

**Output analysis.** Waveform with oscilloscope triggering, spectrum at two
FFT resolutions, spectrogram, and a polar stereo plot with correlation.
Available as an inline strip or over the full window.

**Master output.** A fader and a two-channel meter in one control: RMS bars,
peak marks that hold and decay, and a clip indication, with the level applied
after the plugin and before the device. Offline rendering is deliberately
unaffected by it.

**Tempo and transport.** A playhead supplying tempo, time signature and
position. Without one, tempo-synchronised delays, arpeggiators and LFOs have
no reference; many plugins assume 120 BPM and produce output that is wrong
without reporting an error.

---

## Agent control

```sh
plugshell --serve                 # 127.0.0.1:8767
```

The protocol is newline-delimited JSON over a loopback socket. It is
shell-drivable by design: the intended caller is an agent with a terminal, so
there is no client library to install and no bindings to generate.

```sh
rpc() { printf '%s\n' "$1" | nc -w 5 127.0.0.1 8767; }

rpc '{"op":"load","path":"/Library/Audio/Plug-Ins/VST3/Pigments.vst3"}'
rpc '{"op":"params","search":"Cutoff","limit":5}'
rpc '{"op":"set","index":546,"value":0.35}'
rpc '{"op":"render","path":"/tmp/probe.wav","note":60,"durationSec":2}'
rpc '{"op":"capture","path":"/tmp/editor.png"}'
rpc '{"op":"drag","at":[0.65,0.13],"to":[0.65,0.30],"normalised":true}'
```

### With Claude Code or Codex

Start the host, then state the task.

```sh
plugshell --load /Library/Audio/Plug-Ins/VST3/Pigments.vst3 --serve &
claude
```

> Find the filter cutoff on port 8767, sweep it across its range rendering two
> seconds at each step, and tell me where the sound stops getting brighter and
> starts only getting quieter.

`tools/sweep.py` implements that pattern — set, render, measure, repeat — and
serves as a worked example:

```sh
tools/sweep.py --param "F1 Cutoff"   # one control, in detail
tools/sweep.py --survey              # every control, at its extremes
```

The survey reports which of a plugin's published parameters measurably change
the output in the current patch, which a parameter list alone does not answer.

### Operations

| Area | Operations |
|---|---|
| Host | `state` `plugins` `load` `unload` `permissions` |
| Parameters | `params` `set` `programs` `program` |
| Audio | `render` `note` `transport` `output` |
| Editor | `capture` `move` `click` `drag` `scroll` |

Full protocol, coordinate conventions, and the two capture methods:
[docs/AGENT.md](docs/AGENT.md).

---

## Motivation

A plugin publishes only the controls its developer chose to expose as
parameters. The remainder of its interface — the selected wavetable, the
routing in a modulation matrix, the visible editor page — exists as pixels and
responds only to a mouse. No host can address those controls, so any workflow
that depends on one is not programmable.

A parameter value is also a number without context. `osc1_wt_pos = 0.42`
carries no information about the resulting waveform. Most of what a person
uses to operate a plugin is visual, and none of it has been available to
software.

The consequence is that an agent can adjust the numbers a plugin publishes and
has no access to anything else. plugshell addresses that gap.

### Applications

- **Assisted sound design.** An agent opens a synth, reads the editor, changes
  a control, renders, and iterates — including controls that are not
  parameters.
- **Measuring what a control does**, rather than what value it holds: move,
  render, compare. See [docs/TIMBRE_MODEL.md](docs/TIMBRE_MODEL.md).
- **Patches as operation sequences**, which can be read, diffed and reasoned
  about. See [docs/UNIVERSAL_PRESET.md](docs/UNIVERSAL_PRESET.md).
- **Regression testing for plugin developers.** Drive the editor, capture it,
  diff the image across builds.
- **Accessibility.** Custom-drawn editors are invisible to screen readers; a
  structured description of one is a starting point.

---

## Related work

Plugin hosting is well established. Programmatic hosting is less common.
Agent-facing hosting with editor access does not appear to exist.

| Project | Parameters | Presets | Opens editor | Screenshot | Synthetic input | Agent protocol |
|---|---|---|---|---|---|---|
| [pedalboard](https://spotify.github.io/pedalboard/) | yes | no | no | no | no | no |
| [DawDreamer](http://dirt.design/DawDreamer/) | yes | `.fxp`, `.vstpreset` | yes, **blocking** | no | no | no |
| [Carla](https://github.com/falkTX/Carla) | yes | yes | yes | no | no | no |
| [ableton-mcp-extended](https://github.com/uisato/ableton-mcp-extended) | via Live API | — | no | no | no | yes |
| **plugshell** | yes | yes | yes, **non-blocking** | **yes** | **yes** | yes |

DawDreamer is the closest prior work. Its editor call is documented as
blocking — it "will pause Python execution until you close the editor window" —
because it is intended for a human adjustment made mid-script. That suits its
purpose and does not suit an automated caller.

---

## Development

```sh
make deps            # JUCE, as a submodule
make build           # configure and compile
make check           # formatting and SPDX headers, the same checks CI runs
make format          # rewrite sources with clang-format
make dmg             # a distributable disk image
make signing-status  # what this machine can and cannot sign
make uninstall       # remove the app and everything it wrote
```

The build signs with the first codesigning identity in the keychain. macOS
binds Screen Recording and Accessibility to an application's signature, and an
ad-hoc signature has no stable identity; without a consistent one, every
rebuild is treated as a different application and both permissions must be
granted again.

Two conventions:

- **The RPC surface is the test harness.** Most behaviour here is verified
  through it rather than by inspecting the window. `{"op":"state"}` answers
  questions a screenshot only appears to answer.
- **Every source file carries an SPDX header**, enforced by `make check`.

See [docs/BUILD.md](docs/BUILD.md), [CONTRIBUTING.md](CONTRIBUTING.md), and
[docs/DISTRIBUTION.md](docs/DISTRIBUTION.md).

---

## Roadmap

1. **Out-of-process hosting.** A plugin loaded into the host's process shares
   its fate, and a segmentation fault cannot be caught as an exception. This
   has occurred here with a released commercial plugin, on the audio thread.
   Design: [docs/OUT_OF_PROCESS.md](docs/OUT_OF_PROCESS.md).
2. **Universal preset.** Recording and replaying operation sequences, bound to
   a plugin version and verified during replay.
3. **Preset index.** Render every preset in a library, embed the results, and
   answer nearest-neighbour queries against a target sound.
4. **Interface understanding.** Reading an editor well enough to identify its
   controls, so an agent can operate a plugin it has no prior description of.

---

## Licence

AGPL-3.0-or-later. JUCE 9's modules are AGPLv3.

Icons from [Lucide](https://lucide.dev) (ISC). The Claude mark is Anthropic's
and the Codex name is OpenAI's; both appear above to identify supported
callers, and neither implies endorsement.

**Trademarks.** VST is a trademark of Steinberg Media Technologies GmbH, used
here in plain text to state compatibility, as Steinberg's guidelines permit,
and not as part of this project's name.

Copyright © 2026 wheatfox &lt;wheatfox17@icloud.com&gt;
