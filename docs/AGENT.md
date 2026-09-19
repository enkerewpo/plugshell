<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (C) 2026 wheatfox <wheatfox17@icloud.com> -->

# Driving plugshell from another program

Start the host with a control socket:

```sh
plugshell --serve            # 127.0.0.1:8767
plugshell --serve 9000       # or a port of your choosing
plugshell --load /Library/Audio/Plug-Ins/VST3/Pigments.vst3 --serve
```

One JSON object per line in, one per line out. It is drivable from a shell,
which is the point:

```sh
echo '{"op":"state"}' | nc 127.0.0.1 8767
```

Every response carries `ok`. A failure carries `error` and nothing else.

Loopback only, and no authentication. That is the right trade for a developer
tool and it does mean the port is as trusted as any other local process.

---

## Permissions

Two of these operations need macOS permissions, and **without them they fail
quietly in the most misleading way possible** — the call reports success and
nothing happens. Settings has a row for each, or:

```sh
echo '{"op":"permissions","request":true}' | nc 127.0.0.1 8767
```

| Permission | Needed for | Why |
|---|---|---|
| Screen Recording | `capture`, on editors that draw with the GPU | reading back what the window server composited |
| Accessibility | `click`, `drag`, `move`, `scroll` | macOS gates synthetic input separately from observation |

macOS shows each prompt exactly once. After a refusal the only route is System
Settings, which the Settings row falls through to.

Permissions attach to an application's **code signature**, and an ad-hoc
signature has none — so an ad-hoc build has to be re-granted after every
rebuild. The build signs with the first codesigning identity in the keychain
for this reason; see [BUILD.md](BUILD.md).

---

## Operations

### Host

| op | takes | gives |
|---|---|---|
| `state` | — | what is loaded, editor size and screen rectangle, parameter and program counts |
| `permissions` | `request` | whether each permission is granted |
| `plugins` | — | the indexed plugin list |
| `load` | `path` | — |
| `unload` | — | — |

`state` reports `editorScreenX/Y/W/H` so a caller can check its own arithmetic
rather than estimating the editor's position from a screenshot.

### Parameters

| op | takes | gives |
|---|---|---|
| `params` | `search`, `offset`, `limit` | `total`, `matched`, and a page of parameters |
| `set` | `index` or `name`, `value` (0–1) | the value and its text after the change |
| `programs` | — | program names |
| `program` | `index` | the new current program |

`params` is paged and searchable rather than a dump, because the counts are
larger than they look — Pigments publishes 4446 parameters. The question a
caller actually has is almost always "which one is the filter cutoff", so:

```sh
echo '{"op":"params","search":"Cutoff","limit":5}' | nc 127.0.0.1 8767
```

`set` goes through the host-facing gesture calls rather than writing the value
directly, so a plugin that only repaints its editor on a gesture does not take
the change and carry on drawing the old position.

### Audio

| op | takes | gives |
|---|---|---|
| `render` | `path`, `durationSec`, `note`, `velocity`, `noteOffSec`, `sampleRate`, `blockSize`, `input` | frames written, `peak`, `rms`, and how long it took |
| `note` | `note`, `velocity`, `durationMs`, `on`, `allOff` | what was sent |
| `transport` | `bpm`, `numerator`, `denominator`, `playing`, `rewind` | the tempo, metre and position now |
| `output` | `gain` or `db` | the master gain, and the current output levels |

There are two ways to make a plugin produce sound, and they answer different
questions.

`render` is offline: the device is detached, the plugin is driven as fast as
the machine allows, and the result is a file. It is the one to use for
measuring, because nothing about it depends on wall-clock time or on an audio
device existing. It reports `peak` and `rms` so that a render which came out
silent is reported as silent rather than as a successful write of nothing.

`note` plays through the audio device, in real time, and returns immediately.
With `durationMs` the note releases itself; without one it stays down until
`{"op":"note","note":60,"on":false}` or `{"op":"note","allOff":true}`. Use it
when the thing being tested is the live path -- the meter, the analysers, a
plugin's response to being played while its editor is watched.

`allOff` sends All Notes Off and All Sound Off on every channel, not just
note-offs for what this host is tracking, so it also stops a latched
arpeggiator or a sequencer the plugin is running by itself.

`output` is the master fader, applied after the plugin and before the device.
It does not affect `render`, which reports the plugin's own output: a
measurement that moved because someone had turned the monitors down would be
worse than useless. Levels come back whether or not anything was set, so it
doubles as a level read:

```sh
echo '{"op":"note","note":60,"velocity":0.9}' | nc 127.0.0.1 8767
sleep 1
echo '{"op":"output"}' | nc 127.0.0.1 8767
#   {"peakDb": -14.14, "rmsL": 0.1337, "clipped": false, ...}
```

`peak` holds and decays over about 1.7 s, and `rms` is integrated over 300 ms,
so a reading taken immediately after a note-on describes the attack and one
taken a second later describes the body.

`transport` supplies the playhead a plugin reads for tempo. Without it,
anything clock-synchronised has nothing to sync to, and plugins that assume
120 BPM produce output that is wrong without reporting an error.

### The editor

| op | takes | gives |
|---|---|---|
| `capture` | `path` | size, scale, and which method worked |
| `move` / `click` | `at: [x, y]` | — |
| `drag` | `at`, `to`, `steps` | — |
| `scroll` | `at`, `delta` | — |

Coordinates are in the **editor's own space**, not the screen's and not the
window's. Pass `"normalised": true` to give them as fractions of the editor
instead, which is the more useful form when the position came from an image.

```sh
echo '{"op":"drag","at":[0.65,0.13],"to":[0.65,0.30],"normalised":true}' | nc 127.0.0.1 8767
```

Drags interpolate over `steps` intermediate points. This is not cosmetic: a
knob accumulates movement rather than jumping to a position, so one large step
lands somewhere different from the same distance travelled gradually.

### Saying who you are

```sh
rpc '{"op":"identify","agent":"Claude Code"}'
```

The host shows a badge in its header while a program is driving it: a pulsing
dot, the caller's name, and what it is doing right now -- "Claude Code is
setting LFO1 Rate", "Claude Code is looking at the editor". The activity is
named from the operation and its own arguments, not from anything the caller
says about itself: a badge where a program can write its own description of
what it is doing to the user's machine is worse than no badge. It is not branding. A window that loads plugins and
turns knobs while nobody is touching the machine reads as an application doing
things by itself, and the first instinct on seeing that is to reach for the
mouse -- which is the one thing that makes the session worse.

Every request refreshes it, so `identify` is a courtesy rather than a
handshake: a caller that never names itself still gets a badge reading "An
agent is driving", because the question it answers is whether something is
driving, not who. The badge fades about two seconds after the last request. An
agent does not disconnect, it stops sending, and a light that stayed on would
be claiming otherwise.

`state` reports `agent` and `agentSecondsSinceRequest` for a caller that wants
to know what the user can see.

### Patches

A patch is a recording of what was done to a plugin, replayable against a
fresh instance of it. The format and the reasoning behind distributing
operations rather than binary state are in
[UNIVERSAL_PRESET.md](UNIVERSAL_PRESET.md).

| op | takes | gives |
|---|---|---|
| `record` | `action`: `start`, `stop`, `status` | whether recording, and how many operations so far |
| `patch` | `path` (optional) | writes the patch, or returns it inline when no path is given |
| `replay` | `path` or `patch`, `force` | how many operations applied, and every problem found |

```sh
rpc '{"op":"record","action":"start"}'
rpc '{"op":"set","index":3,"value":0.72}'
rpc '{"op":"click","at":[0.45,0.40],"normalised":true,"why":"not a published parameter"}'
rpc '{"op":"record","action":"stop"}'
rpc '{"op":"patch","path":"/tmp/probe.plugshell-patch.json"}'
```

Three things are worth knowing about what gets recorded.

**A `set` you asked for is written down when you ask for it.** Changes made by
hand in the editor are found by diffing the parameter set ten times a second
instead, because there is no other moment to notice them. Both end up as the
same kind of operation.

**A pointer operation owns what moves just after it.** Those changes are
attached to it as an expectation rather than written down as separate `set`s
— which is what lets replay tell a click that landed from one that missed,
without comparing a single pixel. The claim lasts about a third of a second;
anything you set after that is your own decision and is recorded as one.

**`why` is worth filling in.** A patch full of unexplained coordinates is a
macro recording. The field is where a recorder says which control it needed
and why no parameter would do.

**Pass a `region` for anything that moves no parameter.** A tab, a page, a
name chosen from the plugin's own list — those leave nothing to read back, so
the host photographs the rectangle you name and stores a coarse signature of
how it looked. Replay photographs the same rectangle and reports how far off
it is.

```sh
rpc '{"op":"click","at":[0.28,0.07],"normalised":true,
      "why":"the FX page is not a parameter",
      "region":[0.11,0.0,0.36,0.09]}'
```

`region` follows `normalised` like `at` does.

**And collapse a run of them into what it was aiming at.** Clicking through a
plugin's own list — wavetables, filter types, LFO shapes — records a *path*:
"click this arrow three times". That is a fact about the list as it stood.
Reorder it, or start somewhere else, and three clicks land somewhere else with
nothing to say so.

```sh
rpc '{"op":"record","action":"mark","region":[0.335,0.135,0.115,0.035],
      "normalised":true,"why":"the OSC A wavetable this patch wants"}'
```

`mark` replaces the run of identical gestures just recorded with a single
operation carrying a picture of where they arrived, and a step budget.
Replay then *searches*: it repeats the gesture until the region matches, and
says how far it had to go — or that it never got there.

```
从 init:        sought ["3 of 16 steps"]   looked ["0.0 away"]   problems []
一格之后开始:    sought ["2 of 16 steps"]   looked ["0.0 away"]   problems []
越过目标开始:    sought ["16 of 16 steps"]  looked ["46.7 away"]
                 problems ["never reached what it was aiming at in 16 steps"]
```

Different step counts, same destination — which is the difference between a
patch and a macro. With none given, a box around
the point is used, so an operation is never recorded with no evidence at all.
Replay reports every measurement under `looked`, and anything past the
tolerance as a problem:

```json
"looked":   ["click at 252.9,47.3: 0.0 away"]
"problems": ["click at 252.9,400.0 did not leave the editor looking as
              recorded (3.4 of 255 average brightness away)"]
```

Replay refuses a patch recorded against a different plugin rather than
clicking where a knob used to be. `"force": true` overrides that; a version
difference is reported as a problem rather than refused.

Parameters are addressed **by index and checked by name**, not the other way
round. A name looks like the more durable identifier and is not one for every
plugin: Serum renames its effect-slot parameters according to what is loaded
in the slot, so three operations recorded as `VerbPDly`, `VerbDamp` and
`VerbWdth` were found under those names elsewhere and replayed onto the wrong
controls -- with every value landing and every operation reporting success.
The mismatch is now reported either way.

### The sound check, and why it is the important one

`learn` renders one note and stores its peak and RMS in the patch. `replay`
renders the same note and says whether it matched:

```json
"sound": { "recordedPeak": 0.463, "recordedRms": 0.190,
           "peak": 1.383, "rms": 0.126, "reproduced": false }
```

This catches the failure nothing else can see: a patch whose operations all
applied, whose values all read back correctly, and which reproduces nothing.
That happens whenever a plugin keeps part of its voice outside its parameter
list. Serum is the worked example -- a factory preset differs from init in
exactly 64 automatable parameters, and the wavetable those oscillators play is
not one of them, so a perfectly applied patch plays a saw where the preset
played a wavetable. Without this check the host reported a clean success.

Pass `"probe": false` to `record`/`learn` to skip the measurement.

---

## Capture, and why it has two methods

`capture` tries the cheap method first and falls back.

**View cache** asks the view to draw itself into a bitmap. It needs no
permission, and it returns nothing for content the GPU draws.

**Window server** reads back what was actually composited, via
ScreenCaptureKit. It is correct for every kind of view and needs Screen
Recording.

The fallback is not a rare path. A plugin can be perfectly readable through the
view cache while it is starting up and opaque to it seconds later, once it
finishes initialising and moves its drawing onto the GPU. Pigments does exactly
this: a capture four seconds after loading succeeds, and the same capture at
seven seconds does not. Anything measuring capture support needs to measure it
on a settled editor, or it will measure the startup window and conclude the
wrong thing.

The response says which method produced the image, so a caller can tell.

---

## A worked sequence

Find a control, read it, change it by parameter, and check:

```sh
rpc() { printf '%s\n' "$1" | nc -w 5 127.0.0.1 8767; }

rpc '{"op":"load","path":"/Library/Audio/Plug-Ins/VST3/Pigments.vst3"}'
rpc '{"op":"params","search":"F1 Cutoff","limit":1}'
rpc '{"op":"set","index":546,"value":0.35}'
rpc '{"op":"capture","path":"/tmp/after.png"}'
```

Prefer `set` over `drag` wherever the control is a published parameter: it is
addressed by name, it survives a layout change, and it can be verified by
reading the value back. Pointer operations are for the controls a plugin did
not publish, and they are the fragile ones.
