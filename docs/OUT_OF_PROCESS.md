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

An editor belongs to its instance, so it lives in the child process. Two ways
to present that:

**A window of its own.** The child opens a normal window; the host positions it
and keeps it above the main one. Costs nothing to build and is honest about
what is happening — the plugin really is a separate program. This is phase one.

**Embedded in the host's window.** macOS can do it, through a remote layer
hosted in the parent's view hierarchy. It looks seamless and it is the fiddliest
code in the project. Phase two, once the rest works.

Starting with a separate window is not a compromise on the crash guarantee,
which is the point of the exercise; it is a compromise on how the result looks.

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
4. The editor in its own window.
5. The editor embedded.

Steps one to three are what deliver the guarantee. Four is what makes it
usable. Five is what makes it invisible.
