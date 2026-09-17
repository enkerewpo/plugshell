<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Universal presets: distributing operations instead of binaries

A preset today is an opaque binary blob. `.vstpreset`, `.fxp`, or a chunk inside a project file — all of them are a serialisation of one plugin's internal state, written by that plugin, readable only by that plugin.

This has consequences that everyone in the field lives with and few question:

- **You cannot read it.** Nothing in the file tells you what it does.
- **You cannot diff it.** Two versions of a patch differ by a wall of bytes.
- **You cannot version-control it meaningfully.** Git stores it, and tells you nothing.
- **You cannot port it.** A patch for one synth says nothing to another, even when both have an oscillator, a filter and an envelope.
- **You cannot share it without redistributing content.** Passing along a factory preset means passing along the vendor's bytes.
- **It breaks.** A plugin update can invalidate a state format, and there is no recourse because there is nothing to migrate.

## The alternative

Distribute **what was done**, not **what resulted**.

```yaml
format: plugshell-patch/1
target:
  plugin: Pigments
  vendor: Arturia
  format: VST3
  version: ">=6.0"

from: factory-init          # a named starting point, not a blob

operations:
  - set:   { parameter: "Osc 1 Wave", value: 0.750, as_text: "Saw" }
  - set:   { parameter: "Filter 1 Cutoff", value: 0.420, as_text: "1.24 kHz" }
  - set:   { parameter: "Filter 1 Resonance", value: 0.180 }
  - ui:
      why: "wavetable selection is not an automatable parameter"
      click: { x: 412, y: 233 }
      expect:
        region: [400, 220, 160, 44]
        looks_like: "sha256:9f2c…"
```

Every line is readable, diffable and reviewable. The file is a description of an operation, not a copy of anything.

### Why record the text as well as the value

`value: 0.420` is the machine's truth and means nothing to a reader. `as_text: "1.24 kHz"` is the plugin's own rendering of that value, captured at authoring time. Keeping both makes the file legible, and gives a later run something to verify against: if the plugin now renders `0.420` as something else, the parameter's mapping changed and the patch needs attention. A binary preset fails this silently.

### The UI escape hatch

Not every control is a parameter. When a patch needs one that is not, the operation records the click, why it was needed, and what the affected region looked like afterwards. That last part matters: a coordinate alone is fragile — a plugin resize or a skin change invalidates it — but a coordinate plus an expectation can at least *detect* that it has broken rather than silently doing the wrong thing.

Coordinates are stored in the editor's own coordinate space at a recorded editor size, so they can be scaled.

### What portability does and does not mean

A patch is written against one plugin. Cross-plugin portability — "take this Serum patch to Pigments" — is a much harder problem and this format does not solve it. What it makes possible is the groundwork: because operations name what they touch, a mapping between two plugins' vocabularies can be written by hand or learned, and applied to a patch. That is future work, explicitly out of scope for v1.

---

## Legal position

**This is not legal advice.** It is the reasoning behind the design, with sources, so that a user or contributor can evaluate it. Jurisdictions differ and none of this has been tested in court for this specific use.

### Copyright is unlikely to attach to a list of parameter values

Three well-established principles point the same way.

**Facts are not copyrightable.** *Feist Publications v. Rural Telephone Service*, 499 U.S. 340 (1991) holds that facts do not owe their origin to an act of authorship and so are not original. The same case rejected the "sweat of the brow" doctrine outright: effort invested in compiling something does not by itself create copyright. A cutoff frequency of 1.24 kHz is a fact about a configuration.

**Functional elements are filtered out.** The idea/expression dichotomy, from *Baker v. Selden* onward, withholds protection from a system or method as distinct from its description. In software infringement analysis, parameter-list similarities are treated as functionally necessary and filtered out before comparison.

**A compilation gets only thin protection.** Under *Feist*, a collection of facts may be protected, but only in its original selection and arrangement, and only thinly. Copying the facts while not copying the selection and arrangement does not infringe.

Commentary in the audio community reaches the same conclusion from the practical side: a patch author does not create the synthesiser, only chooses settings within it, much as with an EQ or compressor.

### The real constraint is contract, not copyright

This is the part that gets overlooked, and it is where the actual risk lives.

Plugin EULAs are contracts, and a contract can restrict conduct that copyright would permit. Many restrict redistribution of factory content, and preset packs are a real commercial market whose sellers rely on those terms. An argument that "parameter values are uncopyrightable" does not answer "you agreed not to redistribute our factory content."

### What that means for this project

The distinction that matters is **where a patch came from**, not what format it is in.

| Case | Position |
|---|---|
| A user authors a patch and shares the operations | Their own work. Not a redistribution of anything. |
| A patch derived from a factory or commercial preset | Likely a breach of the source plugin's EULA regardless of the format it is expressed in. Reformatting someone's content does not launder it. |
| Reading a plugin's factory presets locally, for your own use | Ordinary use of software you have licensed. |
| Bulk-converting a commercial preset library into shareable operations | The clearest problem case. Do not build this. |

### Design decisions that follow

These are constraints on the software, not disclaimers.

1. **Record what the user does.** The authoring path is "observe a session and write down the operations", not "read a preset file and decompile it".
2. **No bulk extraction feature.** plugshell will not ship a "convert this preset library to patches" command. The capability to read a preset locally is legitimate and useful; industrialising it into a redistribution pipeline is not, and the absence of that button is a deliberate design decision.
3. **Patches declare their origin.** A `from:` field states the starting point. `from: factory-init` is a named initial state, not vendor content. A patch that began from a factory preset must say so, and tooling should warn before sharing it.
4. **The format specification is separate from this implementation.** The schema is intended to be reusable by anyone, under a permissive licence, independent of this implementation's AGPL. A format that only one copyleft tool can read is not a universal format.

### Sources

- [*Feist Publications, Inc. v. Rural Telephone Service Co.*, 499 U.S. 340 (1991)](https://supreme.justia.com/cases/federal/us/499/340/) — facts uncopyrightable; "sweat of the brow" rejected; thin protection for compilations
- [Functionality and expression in computer programs (Berkeley Law)](https://www.law.berkeley.edu/wp-content/uploads/2017/07/FUNCTIONALITY-AND-EXPRESSION-IN-COMPUTER-PROGRAMS.pdf) — filtration of functional elements, including parameter lists
- [Copyright cases index (BitLaw)](https://www.bitlaw.com/source/cases/copyright/index.html) — *Baker v. Selden* and the idea/expression line
- [End-user license agreement (overview)](https://en.wikipedia.org/wiki/End-user_license_agreement) — contractual restrictions independent of copyright

---

## Status

Design stage. The format above is a sketch and will change once phase 1 reports whether UI operations can be recorded and replayed at all — a patch format with an escape hatch that does not work is a patch format that only covers automatable parameters, which is a smaller and much less interesting thing.

---

## Replay: what a patch is anchored to

A patch is only worth distributing if replaying it lands in the same place it
was recorded from. Three things decide that.

### The base state is the plugin's own default

Replay starts from a freshly instantiated plugin and nothing else. That state
is determinate, it is the same on every machine, and — the reason it matters
here — it is **not** anybody's data: it is whatever the plugin constructs
itself as. A patch that started from a factory preset would have to name that
preset, and a patch that started from a saved state would have to carry it,
which is the binary blob this format exists to avoid.

So every patch is a path from the default, and the first operations in a
recording are usually the ones that undo the defaults the author did not want.
That is slightly wasteful and entirely portable.

### Coordinates are the editor's own

Pointer operations address the editor in its own coordinate space, the one the
plugin laid its controls out in — not the screen, not the window. The host
already knows where the editor is and at what scale it is drawn, so it does
the conversion, and a patch does not care where the window was when it was
recorded, what display it was on, or whether the host scaled the editor down
to fit.

Fractions of the editor are accepted as well, which is the more useful form
when the operation was derived from an image: an agent reasoning about a
screenshot knows a knob's position as a proportion of the picture, and that
proportion stays correct if the plugin is opened at a different size.

### The binding is to a plugin version, and it is checked

A control's position is a fact about one version of one plugin's editor. The
patch therefore records the plugin identifier and version it was made against,
and replay refuses, loudly, when they do not match — rather than clicking
where a knob used to be.

Parameter operations are the exception and should be preferred wherever a
control is a published parameter: a parameter is addressed by name and
identifier, survives a layout change, and can be verified after the fact by
reading the value back. Pointer operations are the escape hatch for everything
the plugin did not publish, and they are inherently more fragile. A recorder
should reach for a parameter first and a coordinate only when it must.

### Verification is what makes replay trustworthy

After each operation the host can compare the parameter set against what the
recording expected. Most editor actions, even on controls that are not
themselves parameters, move something that is — so a click that missed is
usually detectable without looking at a single pixel. Where nothing observable
changed, the recorded editor image is the fallback comparison.

This is the difference between a patch format and a macro recorder: a macro
replays blindly, and this can tell you it went wrong.
