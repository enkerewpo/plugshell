#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>
"""Measure what a plugin's controls actually do to its sound.

A parameter list tells you a control's name, its range, and the number it
currently holds. It does not tell you the thing anyone actually wants to know,
which is what moving it does. That is measurable: move it, render, listen to
the difference.

Two modes.

  One parameter, in detail -- sweep it across its range and show how the sound
  moves with it:

      tools/sweep.py --param "F1 Cutoff"

  Every parameter, coarsely -- render each at its extremes and report which
  ones change the sound at all. On a synth that publishes thousands of
  parameters this is the difference between a list and a map:

      tools/sweep.py --survey

Needs a running host with a plugin loaded:

    plugshell --load /path/to/Some.vst3 --serve
"""

import argparse
import json
import math
import socket
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile


class Host:
    """One request per connection, which is what the host expects."""

    def __init__(self, port: int, timeout: float = 60.0):
        self.port = port
        self.timeout = timeout

    def __call__(self, **request):
        with socket.create_connection(("127.0.0.1", self.port), self.timeout) as s:
            s.settimeout(self.timeout)
            s.sendall((json.dumps(request) + "\n").encode())

            chunks = []
            while not (chunks and chunks[-1].endswith(b"\n")):
                got = s.recv(65536)
                if not got:
                    break
                chunks.append(got)

        reply = json.loads(b"".join(chunks).decode())
        if not reply.get("ok"):
            raise RuntimeError(f"{request.get('op')}: {reply.get('error')}")
        return reply


def describe(path: Path, window: tuple[float, float] | None = None) -> dict:
    """Reduce a rendered note to a few numbers that track what an ear tracks.

    Loudness and brightness between them account for most of what people mean
    when they say a sound changed, and the centroid in particular is the
    closest single number to perceived brightness.

    Measured over the sustained part of the note by default, not the whole
    file. Including the attack and the release tail averages the timbre
    together with a transient and a decay into silence, and when the sound is
    quiet the silence dominates -- so a filter sweep that plainly changes the
    sound can come back looking like it changed nothing.
    """
    rate, data = wavfile.read(path)

    x = data.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)

    if window is not None:
        a = int(max(0.0, window[0]) * rate)
        b = int(min(len(x) / rate, window[1]) * rate)
        if b - a > 256:
            x = x[a:b]

    # Integer formats arrive scaled to their width.
    if np.issubdtype(data.dtype, np.integer):
        x /= float(np.iinfo(data.dtype).max)

    rms = float(np.sqrt(np.mean(x**2)))

    window = np.hanning(len(x))
    spectrum = np.abs(np.fft.rfft(x * window))
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)

    total = spectrum.sum()
    if total <= 0:
        return {"rms": rms, "peak": float(np.max(np.abs(x))), "centroid": 0.0, "rolloff": 0.0}

    centroid = float((freqs * spectrum).sum() / total)

    cumulative = np.cumsum(spectrum)
    rolloff = float(freqs[np.searchsorted(cumulative, 0.85 * total)])

    return {
        "rms": rms,
        "peak": float(np.max(np.abs(x))),
        "centroid": centroid,
        "rolloff": rolloff,
        "spectrum": spectrum,
        "freqs": freqs,
    }


def render_at(host: Host, index: int, value: float, wav: Path, args) -> dict:
    note_off = args.duration * 0.6
    host(op="set", index=index, value=value)
    host(
        op="render",
        path=str(wav),
        note=args.note,
        durationSec=args.duration,
        noteOffSec=note_off,
    )
    # From after the attack to just before the release: the part of the note
    # that is the timbre rather than the envelope.
    return describe(wav, window=(0.15, note_off * 0.95))


def bar(fraction: float, width: int = 28) -> str:
    filled = int(round(max(0.0, min(1.0, fraction)) * width))
    return "#" * filled + "." * (width - filled)


def sweep_one(host: Host, args, tmp: Path):
    found = host(op="params", search=args.param, limit=4)["params"]
    if not found:
        sys.exit(f"No parameter matching {args.param!r}")

    if len(found) > 1 and not any(p["name"].lower() == args.param.lower() for p in found):
        print("Matched several; using the first:")
        for p in found:
            print(f"    {p['index']:>5}  {p['name']}")
        print()

    p = next((q for q in found if q["name"].lower() == args.param.lower()), found[0])
    original = p["value"]

    print(f"{p['name']}  (index {p['index']})")
    print()

    rows = []
    for i in range(args.steps):
        value = i / (args.steps - 1) if args.steps > 1 else 0.0
        m = render_at(host, p["index"], value, tmp / "sweep.wav", args)
        text = host(op="params", search=p["name"], limit=1)["params"][0]["text"]
        rows.append((value, text, m))

    host(op="set", index=p["index"], value=original)

    centroids = [m["centroid"] for _, _, m in rows]
    levels = [20 * math.log10(m["rms"]) if m["rms"] > 0 else -120.0 for _, _, m in rows]

    # In octaves and decibels, because a ratio is what the ear hears and what
    # makes the two dimensions comparable at all. Ninety hertz of centroid
    # means something quite different at 200Hz than at 8kHz.
    c_lo, c_hi = max(min(centroids), 1.0), max(max(centroids), 1.0)
    octaves = math.log2(c_hi / c_lo)
    db_range = max(levels) - min(levels)

    # Track whichever dimension actually moved. Drawing a bar for a range of
    # nine hertz turns measurement noise into a trend line, which is worse
    # than drawing nothing.
    if octaves < 0.15 and db_range < 1.0:
        tracking = None
    elif octaves * 6.0 >= db_range:
        tracking = ("brightness", centroids)
    else:
        tracking = ("level", levels)

    header = f"  {'value':>6}  {'shown as':>12}  {'brightness':>11}  {'level':>8}"
    print(header + (f"   {tracking[0]}" if tracking else ""))
    print(f"  {'-' * 6}  {'-' * 12}  {'-' * 11}  {'-' * 8}")

    for (value, text, _), c, db in zip(rows, centroids, levels):
        line = f"  {value:>6.2f}  {text[:12]:>12}  {c:>9.0f}Hz  {db:>7.1f}dB"
        if tracking:
            series = tracking[1]
            lo, hi = min(series), max(series)
            v = c if tracking[0] == "brightness" else db
            line += "   " + bar((v - lo) / (hi - lo) if hi > lo else 0.0)
        print(line)

    print()
    if tracking is None:
        print("  Nothing measurable changed. Either this control does nothing on its")
        print("  own, or it needs something else switched on before it does.")
        return

    print(f"  Brightness  {c_lo:>7.0f}Hz -> {c_hi:>7.0f}Hz   ({octaves:+.2f} octaves)")
    print(f"  Level       {min(levels):>7.1f}dB -> {max(levels):>7.1f}dB   ({db_range:.1f}dB range)")

    if octaves < 0.15 <= db_range / 6.0:
        print()
        print("  This one works as an attenuator here, not as a tone control: it")
        print("  changes how loud the sound is without much changing its colour.")


def survey(host: Host, args, tmp: Path):
    """Which parameters do anything at all, at the plugin's current settings.

    The qualification matters and is not a caveat to skip: a filter's resonance
    does nothing measurable while the filter is bypassed, and this will
    faithfully report that it does nothing. What comes out is a map of what is
    live in the patch on screen, which is the more useful thing anyway.
    """
    everything = host(op="params", limit=1)
    total = everything["total"]
    limit = args.limit or total

    print(f"{total} parameters published; testing {min(limit, total)}.")
    print("Rendering each at both extremes -- this takes a while.\n")

    movers = []
    checked = 0
    offset = 0

    while checked < min(limit, total):
        page = host(op="params", offset=offset, limit=200)["params"]
        if not page:
            break
        offset += len(page)

        for p in page:
            if checked >= limit:
                break
            checked += 1

            original = p["value"]
            try:
                low = render_at(host, p["index"], 0.0, tmp / "a.wav", args)
                high = render_at(host, p["index"], 1.0, tmp / "b.wav", args)
            except RuntimeError:
                continue
            finally:
                host(op="set", index=p["index"], value=original)

            d_bright = abs(high["centroid"] - low["centroid"])
            d_level = abs(high["rms"] - low["rms"])

            if d_bright > 5.0 or d_level > 1e-4:
                movers.append((d_bright, d_level, p))

            if checked % 50 == 0:
                print(f"  {checked}/{min(limit, total)}  ({len(movers)} live)", flush=True)

    movers.sort(key=lambda m: -(m[0] + m[1] * 20000))

    print(f"\n{len(movers)} of {checked} parameters changed the sound.\n")
    print(f"  {'index':>6}  {'name':<34}  {'brightness':>11}  {'level':>8}")
    print(f"  {'-' * 6}  {'-' * 34}  {'-' * 11}  {'-' * 8}")

    for d_bright, d_level, p in movers[: args.top]:
        print(f"  {p['index']:>6}  {p['name'][:34]:<34}  {d_bright:>9.0f}Hz  {d_level:>8.4f}")

    if len(movers) > args.top:
        print(f"  ... and {len(movers) - args.top} more")

    print("\nThe rest did nothing at the patch's current settings, which usually")
    print("means they belong to a section that is switched off rather than that")
    print("they are inert.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--param", help="parameter to sweep, by name")
    ap.add_argument("--survey", action="store_true", help="test every parameter at its extremes")
    ap.add_argument("--steps", type=int, default=9)
    ap.add_argument("--note", type=int, default=60)
    ap.add_argument("--duration", type=float, default=1.5)
    ap.add_argument("--port", type=int, default=8767)
    ap.add_argument("--limit", type=int, default=0, help="survey only the first N parameters")
    ap.add_argument("--top", type=int, default=40, help="how many movers to list")
    args = ap.parse_args()

    if not args.param and not args.survey:
        ap.error("give --param NAME or --survey")

    host = Host(args.port)

    state = host(op="state")
    if not state.get("loaded"):
        sys.exit("No plugin is loaded. Start the host with --load.")

    print(f"{state['name']}\n")

    with tempfile.TemporaryDirectory(prefix="plugshell-sweep-") as d:
        tmp = Path(d)
        if args.survey:
            survey(host, args, tmp)
        else:
            sweep_one(host, args, tmp)


if __name__ == "__main__":
    main()
