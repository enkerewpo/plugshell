<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Contributing

The project is at design stage. The most useful contributions right now are
evidence and critique rather than code.

## Most wanted

- **Results from the phase-1 spike on plugins we do not have.** Which capture
  and input strategy worked, which did not, and what the plugin renders with.
  Negative results are as valuable as positive ones and are harder to come by.
- **Prior art the survey in the README missed.**
- **Design critique**, particularly of the RPC boundary and of the open
  question in [docs/SPIKE.md](docs/SPIKE.md) about out-of-process hosting.

## Before writing code

Open an issue first. Phases 2 onward depend on what the spike finds, and work
that lands on an invalidated assumption is wasted.

## Requirements for a pull request

- `make check` passes: formatting and SPDX headers
- Every new source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`
- Commits explain why, not only what
- Claims about plugin behaviour say which plugin, which version, and which macOS

## Licence

Contributions are accepted under AGPL-3.0-or-later. This follows from the JUCE
Framework, whose modules are dual-licensed under AGPLv3 and a commercial
licence. The Steinberg VST3 SDK has been MIT-licensed since version 3.8 and
imposes no such requirement.
