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
