<p align="center"><img src="assets/hero.webp" alt="plugshell hosting Arturia Pigments with its four analysers open" width="100%"></p>

# plugshell

**A host that lets an agent work an audio plugin.**

**Agent control** is the point. Not only reading a plugin's parameters — seeing
its editor, clicking the controls that are not parameters, rendering it to
audio, and describing a patch as a sequence of operations rather than an opaque
binary blob. Everything a person can do with a plugin, an agent can do.

> **Status: early, and it runs.** Everything under [What works](#what-works) is
> built and has been used; everything under [Roadmap](#roadmap) is not.
>
> **macOS first, not macOS only.** Editor capture and synthetic input are the
> two parts that are genuinely platform work, and they are written against a
> small shim -- `EditorProbe` -- with the rest of the application above it.
> Windows and Linux mean a second implementation of that shim, not a second
> application.

---

## Quickstart

### Install

Download the disk image from
[Releases](https://github.com/enkerewpo/plugshell/releases), drag
`plugshell.app` onto Applications, and open it.

**The first launch is blocked.** macOS cannot verify an application that is not
signed with an Apple Developer ID — a paid membership rather than a security
property. To allow it, once:

> **System Settings → Privacy & Security**, scroll down to the message about
> plugshell, click **Open Anyway**, confirm.

The Control-click → Open shortcut older instructions mention was removed from
macOS and no longer works.

### Or build it

No Gatekeeper step, because nothing was downloaded:

```sh
git clone https://github.com/enkerewpo/plugshell.git
cd plugshell
make deps && make build
open build/src/app/plugshell_app_artefacts/RelWithDebInfo/plugshell.app
```

Needs the Xcode command line tools, CMake and Ninja. See
[docs/BUILD.md](docs/BUILD.md).

### Permissions

Two features need macOS permission, and **both fail silently without it** — the
operation reports success and nothing happens:

| Permission | Needed for |
|---|---|
| Screen Recording | reading a plugin's editor as an image |
| Accessibility | clicking and dragging inside that editor |

**Settings** has a row for each showing whether it is granted. Everything else —
loading plugins, parameters, the keyboard, the analysers, offline rendering —
works without them. macOS applies Screen Recording only at launch, so quit and
reopen after allowing it.

---

## What works

**Hosts VST3 plugins and their editors, without blocking.** The window stays
open, observable and driveable while the calling program keeps working — which
is where every programmatic host before it stops. It follows the plugin's own
size, including when the plugin resizes itself, and scrolls an editor larger
than the display rather than clipping it.

**Reads and writes parameters.** Paged and searchable, because the counts are
larger than they look: Pigments publishes 4446, and the question a caller
actually has is "which one is the filter cutoff".

**Captures the editor as an image**, through the window server, so it works for
editors drawn by the GPU — which is most modern synths. This is the part no
other host does, and it is what puts a plugin's own interface in front of an
agent at all.

**Puts synthetic input into the editor.** Clicks, drags and scrolls in the
editor's own coordinates: the escape hatch for every control a plugin chose not
to publish — wavetable selectors, modulation matrices drawn by dragging, preset
browsers.

**Renders offline, faster than real time.** Two seconds of Pigments in 53
milliseconds. A parameter vector in, audio out, no device and no waiting.

**Plays from the computer keyboard**, two octaves in the tracker layout, naming
the chord as you hold it — inversions, alterations and slash chords included.

**Shows what the output is doing**: waveform with oscilloscope triggering,
spectrum at two FFT resolutions, spectrogram, and a polar stereo plot with
correlation. In the strip as a glance, or over the whole window to study.

**Provides a tempo and transport**, because a plugin without a playhead has to
guess — and several guess 120 and carry on, which is worse than failing.

---

## Agent control

```sh
plugshell --serve                 # 127.0.0.1:8767
```

Newline-delimited JSON over a loopback socket, drivable from a shell. That is
deliberate: the intended caller is an agent with a terminal, not an application
compiled against a client library. There is nothing to install on the agent's
side and nothing to generate — it already knows how to run `nc`.

```sh
rpc() { printf '%s\n' "$1" | nc -w 5 127.0.0.1 8767; }

rpc '{"op":"load","path":"/Library/Audio/Plug-Ins/VST3/Pigments.vst3"}'
rpc '{"op":"params","search":"Cutoff","limit":5}'
rpc '{"op":"set","index":546,"value":0.35}'
rpc '{"op":"render","path":"/tmp/probe.wav","note":60,"durationSec":2}'
rpc '{"op":"capture","path":"/tmp/editor.png"}'
rpc '{"op":"drag","at":[0.65,0.13],"to":[0.65,0.30],"normalised":true}'
```

### With Claude Code, or Codex

Start the host, then ask for the work in words.

```sh
plugshell --load /Library/Audio/Plug-Ins/VST3/Pigments.vst3 --serve &
claude
```

> Find the filter cutoff on port 8767, sweep it across its range rendering two
> seconds at each step, and tell me where the sound stops getting brighter and
> starts only getting quieter.

`tools/sweep.py` is that pattern written down — set, render, measure, repeat —
and a worked example to read before writing your own:

```sh
tools/sweep.py --param "F1 Cutoff"   # one control, in detail
tools/sweep.py --survey              # every control, at its extremes
```

The survey answers something a parameter list cannot: of 4446 published
parameters, which ones actually change the sound in this patch. That is the
difference between a list and a map.

### The surface

| Area | Operations |
|---|---|
| Host | `state` `plugins` `load` `unload` `permissions` |
| Parameters | `params` `set` `programs` `program` |
| Audio | `render` `transport` |
| Editor | `capture` `move` `click` `drag` `scroll` |

Full protocol, the coordinate conventions, and why capture has two methods:
[docs/AGENT.md](docs/AGENT.md).

---

## Why

**A plugin publishes the controls it chose to publish.** Everything else —
which wavetable is selected, what the modulation matrix routes where, which
page of the editor is showing — exists only as pixels and only answers to a
mouse. If a control is not a parameter, no host can touch it, and any workflow
needing it stops being programmable.

**And a parameter value is a number without context.** Knowing that
`osc1_wt_pos = 0.42` says nothing about what the wavetable looks like there.
What a person uses to work a plugin is mostly visual, and none of it has been
available to an agent.

One consequence: an agent can adjust the numbers a plugin chooses to publish,
and is blind and powerless for everything else. Closing that is what this is
for.

### What that makes possible

- **Assisted sound design.** An agent opens a synth, looks at the editor,
  changes a control, listens, and iterates — including controls that are not
  parameters.
- **Measuring what a control does** — not what number it holds. Move it,
  render, compare: [docs/TIMBRE_MODEL.md](docs/TIMBRE_MODEL.md).
- **Patches as operations**, which can be read, diffed and reasoned about:
  [docs/UNIVERSAL_PRESET.md](docs/UNIVERSAL_PRESET.md).
- **Regression testing for plugin developers.** Drive the editor, capture it,
  diff the image across builds.
- **Accessibility.** Custom-drawn editors are invisible to screen readers; a
  structured description of one is a starting point.

---

## Related work

Plugin hosting is well-trodden. Programmatic hosting is less so. Agent-facing
hosting with editor access does not appear to exist.

| Project | Parameters | Presets | Opens editor | Screenshot | Synthetic input | Agent protocol |
|---|---|---|---|---|---|---|
| [pedalboard](https://spotify.github.io/pedalboard/) | yes | no | no | no | no | no |
| [DawDreamer](http://dirt.design/DawDreamer/) | yes | `.fxp`, `.vstpreset` | yes, **blocking** | no | no | no |
| [Carla](https://github.com/falkTX/Carla) | yes | yes | yes | no | no | no |
| [ableton-mcp-extended](https://github.com/uisato/ableton-mcp-extended) | via Live API | — | no | no | no | yes |
| **plugshell** | yes | yes | yes, **non-blocking** | **yes** | **yes** | yes |

**DawDreamer is the closest prior work.** Its editor call is documented as
blocking — it "will pause Python execution until you close the editor window" —
because it is built for a human to make an adjustment mid-script. That is right
for its purpose and wrong for an automated caller.

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
attaches Screen Recording and Accessibility to an application's **signature**,
and an ad-hoc signature has no stable identity — so without this, every rebuild
looks like a different application and both permissions need granting again.

Two habits worth keeping:

- **The RPC surface is the test harness.** Most behaviour here was verified
  through it rather than by looking at the window, and that is deliberate.
  `{"op":"state"}` answers questions a screenshot only appears to: more than
  one bug in this repository's history was a window showing the right thing for
  the wrong reason.
- **Every source file carries an SPDX header**, checked by `make check`.

More: [docs/BUILD.md](docs/BUILD.md), [CONTRIBUTING.md](CONTRIBUTING.md),
[docs/DISTRIBUTION.md](docs/DISTRIBUTION.md).

---

## Roadmap

1. **Out-of-process hosting.** A plugin loaded into the host's own process
   shares its fate, and a segmentation fault is not an exception that can be
   caught. This has already happened here, with a released commercial plugin,
   on the audio thread. The fix is the one every host making this promise
   arrived at: [docs/OUT_OF_PROCESS.md](docs/OUT_OF_PROCESS.md).
2. **Universal preset.** Recording and replaying operation sequences, bound to
   a plugin version and verified as they replay.
3. **Preset index.** Render every preset in a library, embed it, and answer
   "which of these sounds closest to this".
4. **Interface understanding.** Reading an editor well enough to name its
   controls, so an agent can work a plugin nobody told it about.

---

## Licence

AGPL-3.0-or-later. JUCE 9's modules are AGPLv3, so this is too.

Icons from [Lucide](https://lucide.dev) (ISC). The Claude mark belongs to
Anthropic and the Codex name to OpenAI; both appear above to say what this is
built to be driven by, and neither implies endorsement.

**Trademarks.** VST is a trademark of Steinberg Media Technologies GmbH, used
here in plain text to state compatibility — which Steinberg's guidelines permit
— and not as part of this project's name.

Copyright © 2026 wheatfox &lt;wheatfox17@icloud.com&gt;
