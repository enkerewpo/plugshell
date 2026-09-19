<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (C) 2026 wheatfox <wheatfox17@icloud.com> -->

# Surviving a plugin that crashes

A plugin loaded into the host's own process shares its fate. This is not a
matter of being careful: a segmentation fault is not an exception, no handler
catches it, and the process is gone. It happened here, on this machine, with a
released commercial plugin:

```
Thread 4 CRASHED: com.apple.audio.IOThread.client
0  MiniMeters  0x11de70f14                              <- null dereference
2  MiniMeters  Clap::ProcessAdapter::process
4  plugshell   VST3PluginInstance::processAudio
6  plugshell   AudioProcessorPlayer::audioDeviceIOCallback
```

Everything below frame 4 is ours and blameless. The host died because the
plugin did, seven minutes into a session, on the audio thread.

The fix is the one every host that makes this promise has arrived at: **the
plugin runs in a different process**. Bitwig calls it plugin crash protection,
Reaper calls it bridging, and they both mean the same thing.

---

## What this rules in and out

| Approach | Survives a crash at load | Survives a crash while playing | Survives a hang |
|---|---|---|---|
| Trial-load in a subprocess first | yes | no | partly |
| Watchdog on the message thread | no | no | yes |
| **Separate process** | **yes** | **yes** | **yes** |

Only the third is a real answer, and the first two are worth building only as
steps towards it.

---

## Shape

```
  plugshell (host)                      plugshell-host (one per plugin)
  ┌────────────────────────┐            ┌────────────────────────────┐
  │ plugin list, strip,    │            │ the plugin instance        │
  │ analysers, RPC socket  │            │ its editor window          │
  │                        │            │                            │
  │ audio device ──────────┼──[shm]────►│ processBlock               │
  │                    ◄───┼──[shm]─────┤                            │
  │ control ───────────────┼──[pipe]───►│ parameters, presets, state │
  └────────────────────────┘            └────────────────────────────┘
```

Two channels, because they have nothing in common.

**Audio** is a fixed-size shared memory region holding two ring buffers, in and
out, plus a control block. The audio thread on each side writes with a single
release store and reads with a single acquire load. No locks, no allocation, no
system calls in the steady state. This is the part that must not be clever.

**Control** is the JSON protocol that already exists for the agent surface,
over a pipe rather than a socket. Parameters, programs, state, editor
geometry — everything that is not a sample. It is allowed to block, allocate
and be slow, because nothing on the audio thread waits for it.

---

## The awkward part: the editor

An editor belongs to its instance, so it lives in the child process, and no
public macOS interface lets a view cross a process boundary into another
application's window.

### What one window would take, and why it is not what ships

The one mechanism that makes it literally one window is CoreAnimation's
cross-process layer hosting: the child publishes its layer tree through a
`CAContext` and hands over a context id, and the host puts a `CALayerHost`
with that id in its own view hierarchy. This is what Safari and Chrome use to
put a sandboxed process's pixels in an unsandboxed window. It was built and it
does not work here.

The measurement, because "it did not work" is not a finding: a `CALayerHost`
renders nothing on this system **even when the context it is given was created
in the same process**. The control was a plain green layer published by the
host to itself and hosted three lines later; a sibling `CALayer` with a
literal background colour, added to the same view, drew correctly. So it is
not the cross-process half, not the layer's frame, and not the view's place in
the hierarchy. The experiment is kept under the session scratchpad, and it is
worth retrying on another macOS version before writing the approach off.

The public-API alternative is an `IOSurface`: the child renders its layer tree
into shared memory with `CARenderer` and the host sets that surface as a
layer's `contents`. That is genuinely one window and needs no private
interface. It costs a render loop in the child, and it does not solve input --
a hosted surface is pixels either way, so events have to be forwarded and
replayed on the other side.

### What ships

A second window, borderless and non-activating, held exactly over the area the
host leaves empty, ordered directly above the host's window by window number.

Two properties do the work. *Non-activating* means clicking a knob does not
make the child the active application: the host watches the keyboard at the
event layer so the computer keys can play the plugin, and a local event
monitor only sees events sent to the active application. With an ordinary
window, touching the plugin killed the computer keyboard until the user
clicked the host's window again.

*Ordered rather than floating* is the difference between the pair behaving
like one window and not. Ownership cannot cross processes; ordering can,
because a window number is a name the window server hands out and both
processes can say it. Floating was the first attempt, and floating is a
property of the whole screen -- so the panel had to be hidden whenever the
host was not in front, and the plugin blinked out of existence every time the
user looked at another window.

What is left over, and is not fixable this way: it does not minimise with the
host, and it is missing from a screenshot of the host's window. Both are
consequences of it being a window, and both go away only with the layer or
surface routes above.

---

## Latency

One process hop, which costs one buffer at worst and nothing at best,
depending on whether the child can be scheduled inside the host's callback
deadline. The child runs at real-time priority and reads a buffer that the host
wrote in the previous callback, so the cost is a fixed one buffer — 5.3ms at
256 samples, against 165ms of Bluetooth on this machine.

Anyone who needs it lower can have the in-process path, which is not going
away: it stays as an option, and it stays the default until the out-of-process
path is as well tested.

---

## What happens when it does crash

1. The child's exit is noticed — the control pipe closes, and there is a pid to
   wait on.
2. The audio side reads silence rather than stale samples, because the control
   block in shared memory has a generation counter the host checks.
3. The host says which plugin died, and offers to start it again.
4. Restarting replays the parameter values the host already holds, because the
   host has been mirroring them all along. Everything the plugin kept to
   itself is lost — which is the honest outcome, and the same one a DAW gives.

Point 4 is where the universal preset format earns its keep a second time: an
operation sequence is exactly what is needed to put a freshly started plugin
back where it was.

---

## Order of work

1. The shared memory transport and its tests — the only part where a mistake
   is a glitch in someone's audio rather than a message on screen.
2. The child executable: load a plugin, process from the ring, answer the
   control channel.
3. The host side: launch, monitor, mirror parameters, restart.
4. The editor, held over the host window's content area.
5. One window for real, by IOSurface, with input forwarded to the child.

Steps one to three are what deliver the guarantee. Four is what makes it
usable. Five is what would make it indistinguishable, and is the only thing
the in-process path still does better.
