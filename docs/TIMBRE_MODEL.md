<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (C) 2026 wheatfox <wheatfox17@icloud.com> -->

# Audio to synthesizer parameters

A survey of what exists, what is unsolved, and where a host like this one
actually changes the problem.

The question this answers: given a recording, can a model recover a
synthesizer patch that reproduces its timbre — and if so, which plugin, which
preset, which parameter values.

---

## It is a real research field, and it is active

The task has a name in the literature: **Automatic Synthesizer Programming**
(ASP), also called *synthesizer inversion* or *sound matching*. Work runs from
2018 to the present and has not slowed down.

| Year | Work | Synth used | Method |
|---|---|---|---|
| 2018 | [InverSynth](https://arxiv.org/abs/1812.06349) | own FM/subtractive architecture | CNN regression from spectrogram |
| 2021 | [SerumRNN](https://arxiv.org/abs/2104.03876) | **Serum** (effects only) | RNN producing a *step-by-step* effect sequence |
| 2022 | [Sound2Synth](https://arxiv.org/abs/2205.03043) | Dexed (DX7 FM) | multi-modal network over several audio representations |
| 2024 | [DiffMoog](https://arxiv.org/abs/2401.12570) | own differentiable modular synth | gradient descent through the synth |
| 2024 | [Sound Matching with Audio Spectrogram Transformers](https://arxiv.org/abs/2407.16643) | own synth | pretrained AST as the encoder |
| 2025 | [Neural Proxies for Sound Synthesizers](https://arxiv.org/abs/2509.07635) | **arbitrary, black-box** | learn a differentiable stand-in for the synth |
| 2026 | [INSTRUMENTAL](https://arxiv.org/abs/2603.15905) | 28-param subtractive | CMA-ES, derivative-free |
| 2026 | [DDSynth-RL](https://arxiv.org/abs/2608.03032) | Dexed | masked discrete diffusion + GRPO on rendered-audio reward |

Commercially it has already shipped: [PresetLab.ai](https://presetlab.ai/)
sells AI preset generation for u-he Diva and Serum 2. There are hobby
attempts too, such as [antithesis](https://github.com/michael-jan/antithesis).

So the answer to "has anyone built it" is yes, repeatedly, and one company is
charging for it.

---

## The two hard problems, both named in the literature

DDSynth-RL states them more plainly than anyone else, and every other paper is
working around one or both.

**1. The mapping is one-to-many.** Distinct parameter configurations produce
perceptually identical sounds. A loss computed in parameter space therefore
punishes correct answers: two patches that sound the same can be far apart
numerically. This is why parameter-space supervision plateaus, and why the
recent work moved to losses computed on *rendered audio*.

**2. The synth is not differentiable.** A commercial plugin is a black box with
no gradients, so audio-domain loss cannot be backpropagated through it. Three
answers exist, and they are the three interesting branches of the field:

- **Approximate it.** Neural Proxies (2025) trains a network to map presets
  into a pretrained audio-embedding space, producing a differentiable stand-in
  for a synth it never needs to differentiate. This is the only line of work
  that explicitly targets *arbitrary black-box* synthesizers.
- **Do not use gradients.** INSTRUMENTAL (2026) uses CMA-ES and reports that
  it **beats gradient descent** on this landscape, which is non-convex enough
  that a derivative-free method wins outright. It also reports that more
  parameters do not monotonically improve matching.
- **Use the renderer as an environment.** DDSynth-RL (2026) fine-tunes with
  RL rewards computed from actually rendering the candidate parameters.

Note what the last two have in common: **they need to render audio from
parameter vectors, many thousands of times, automatically.** That is a hosting
problem, not a machine-learning problem.

---

## Where this host changes the problem

Every result above is on one synth at a time, and almost always on Dexed — an
open-source DX7 clone — or on a synth the authors wrote themselves. The reason
is not lack of ambition. It is that the experimental loop requires
programmatically setting a parameter vector and rendering the result, and for
commercial plugins that infrastructure has been the obstacle.

That is precisely what this project is. Three things follow.

**Training data is free and unlimited.** The supervision problem that dominates
most audio ML does not exist here: a random parameter vector *is* its own
label, because rendering it gives the matching audio. A host that can render
(parameters → audio) without a GUI and without a human is a dataset generator
that never runs out. This is the single strongest argument for building the
model on top of a host rather than the other way round.

**The parameter vector is not the whole patch.** Every paper listed assumes a
synth's state is a vector of numbers. For Serum, Pigments, or anything with a
wavetable selector, a sample slot, a modulation matrix drawn by dragging, or a
preset browser, it is not — and those controls are exactly the ones this host
exists to reach. No published work addresses recovering non-parameter state,
because no published work could reach it. This is the genuinely novel part and
it is also the hardest.

**Retrieval is a better first target than regression.** "Which of my plugins,
and which of its presets, is closest to this sound" is a nearest-neighbour
query in an embedding space, and it is tractable now: render every preset in
the user's own library, embed, index. It gives a useful answer on day one and
it is the correct initialisation for the optimisation step — INSTRUMENTAL
finds that spectral initialisation beats random starts, and a retrieved preset
is a far better start than a spectral guess.

---

## What a realistic pipeline looks like

```
recording
   ↓  source separation (HTDemucs or similar)         ← off the shelf
stem
   ↓  note/segment detection, pick a monophonic region ← off the shelf
timbre sample
   ↓  audio embedding (CLAP / AST / pretrained)        ← off the shelf
embedding
   ↓  nearest neighbour over a locally rendered preset index  ← plugshell renders
candidate plugin + preset
   ↓  derivative-free refinement (CMA-ES) on the exposed parameters,
      audio-domain perceptual loss, rendering each candidate   ← plugshell renders
parameter vector
   ↓  emit as an operation sequence                    ← universal preset format
patch
```

Every step marked "off the shelf" is somebody else's solved problem. The two
marked "plugshell renders" are the ones this repository has to provide, and
they are the same capability — headless render of a parameter vector —
which is also what the agent control surface needs anyway.

The ceiling is set by the first step. Separation artifacts are timbral
artifacts, so a stem pulled out of a dense mix carries damage that the matcher
will faithfully try to reproduce. Expect this to work well on exposed leads and
badly on a pad buried under a full arrangement, and do not report a matching
loss without saying which of the two it was measured on.

---

## The part that is dangerous

This is worth stating rather than leaving implied, because it changes design
decisions and it changes them now, not later.

A working version of this makes "reproduce this record's sound design" a batch
job. Sound design is craft that people are paid for, and preset libraries are a
product that people sell. A tool that takes a commercial track and emits a
patch is doing automated reverse-engineering of both, at a speed no person can
match.

Three things are worth being precise about.

**The capability does not depend on this project.** Parameter recovery from
audio has been demonstrated repeatedly with the authors' own synths, and is
sold commercially today. Not building it here removes nothing from the world.
What a general host adds is *breadth* — arbitrary plugins rather than Dexed —
and reach into non-parameter controls. Breadth is the dangerous part, so
breadth is where the restraint belongs.

**The legal position is not the same as the ethical one.** The analysis in
[UNIVERSAL_PRESET.md](UNIVERSAL_PRESET.md) applies: parameter values are facts
about a functional system, and *Feist* and *Baker v. Selden* both point away
from their being copyrightable. Recovering them from a recording one has the
right to listen to is, most likely, lawful. That is not an argument that every
use of it is legitimate, and a tool that only asks the first question is a
tool that will be used badly.

**Scale is the control surface that matters.** The difference between a
musician working out how a sound was made — something people have always done
by ear, and a normal way to learn — and industrial extraction is throughput.
So the design decisions that bite are about throughput, not about capability:

- The existing rule stands: a patch records **what the user did**, not a scrape
  of a library. Bulk extraction of an installed preset collection is not a
  feature this host provides.
- Rendered audio and embeddings derived from the user's licensed plugins stay
  **local**. A model trained on one person's own library is a personal tool; a
  distributed model trained on someone's commercial synth is a derivative
  product with an obvious rights holder.
- Patches **declare their origin**, including when they were derived from a
  recording, so a patch that came out of a matcher is not indistinguishable
  from one a person made.
- No shipped "match any track" model. The pipeline can exist; the pre-trained
  weights over a commercial library are the thing not to distribute.

None of these prevent misuse by someone determined. They do decide whether the
easy path through this tool is the legitimate one, and that is the part
actually within a designer's control.

---

## Concrete next steps in this repository

Nothing here needs a model to start being useful.

1. **Headless render** — parameter vector in, audio out, no GUI, deterministic.
   Required by the agent surface regardless, and it is the whole data pipeline.
2. **Preset index** — walk the user's factory banks, render a fixed probe note
   per preset, store embeddings. This alone answers "which preset sounds
   closest to this" without any training.
3. **Parameter sweep capture** — vary one parameter, render, and record what
   changed in the audio *and* in the editor image. This is the dataset for
   "what does this control actually do", which is the question the user of a
   synth is really asking.
4. Only then, matching.

Step 3 is the one to be most interested in. It produces something the
literature does not have and a musician does want: a description of what each
control does to the sound, derived by measurement rather than by reading a
manual.
