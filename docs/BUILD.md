<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Building

## Requirements

macOS on Apple Silicon or Intel. Verified on macOS 27, arm64.

| Tool | Why | Install |
|---|---|---|
| Xcode Command Line Tools | clang, system SDKs | `xcode-select --install` |
| CMake ≥ 3.22 | build system | `brew install cmake` |
| Ninja | build driver | `brew install ninja` |
| clang-format | `make format` | `brew install clang-format` |

Full Xcode is **not** required to build or run. It becomes necessary only for
signing and notarising a distributable `.app` (phase 4).

JUCE is vendored as a git submodule, so no system-wide install is needed.

## Build

```bash
git clone --recurse-submodules https://github.com/enkerewpo/plugshell.git
cd plugshell
make build
```

If the repository was cloned without `--recurse-submodules`:

```bash
make deps
```

## Targets

```
make help           list every target
make build          build everything
make spike          build and run the phase-1 feasibility spike
make format         rewrite sources with clang-format
make check          format and SPDX checks, as CI runs them
make clean          remove the build tree
```

`BUILD_TYPE`, `BUILD_DIR`, `GENERATOR` and `JOBS` can be overridden:

```bash
make build BUILD_TYPE=Debug JOBS=4
```

## Layout

```
src/core/           plugin loading, parameters, presets — platform-neutral
src/platform/macos/ editor capture and event injection
src/rpc/            JSON-RPC surface (phase 3)
spike/              phase-1 feasibility probe, not a product
docs/               design, related work, spike findings
tools/              repository checks
external/JUCE/      submodule
```

## Conventions

Every tracked source carries an SPDX identifier on its first lines. `make
license-check` enforces this and CI runs it. Formatting is `.clang-format`,
enforced by `make format-check`.

## Known issues

Scanning installed plugins in-process can crash the host, because it loads
arbitrary third-party binaries into one address space. See
[SPIKE.md](SPIKE.md), finding F1.

## Code signing and macOS permissions

Screen Recording and Accessibility are required for editor capture and
synthetic input, and macOS attaches both to the application's **code
signature**. An ad-hoc signature — JUCE's default — has no stable identity, so
the system identifies the app by its path and contents instead, and every
rebuild looks like a different application. The permissions then have to be
granted again after every build.

The build therefore signs with the first codesigning identity in the keychain,
which is reported at configure time:

```
-- plugshell: signing with 'Apple Development: ...' so macOS permissions survive rebuilds
```

Any stable identity works, including a self-signed one — it does not have to be
an Apple Developer certificate. Create one in Keychain Access under *Certificate
Assistant > Create a Certificate*, choosing **Code Signing** as the type.

To choose an identity explicitly, or to go back to ad-hoc:

```sh
cmake -S . -B build -DPLUGSHELL_CODESIGN_IDENTITY="Apple Development: ..."
cmake -S . -B build -DPLUGSHELL_CODESIGN_IDENTITY=-      # ad-hoc
```

With no identity available the build still works; only the permissions become
tedious.
