<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (C) 2026 wheatfox <wheatfox17@icloud.com> -->

# Shipping it

Short answers first, because the questions have a lot of folklore around them.

**Do you need Apple's permission to distribute outside the App Store?** No.
macOS is not a closed platform and never has been. Developer ID exists
precisely so that software can be shipped directly.

**Does a disk image have to be signed?** Not to build one, and not to install
it. But an unsigned or merely development-signed application is blocked by
Gatekeeper on every machine that did not build it, and recent macOS has removed
the easy way around that. So in practice: the image works, and your users have
to be talked through a detour.

**Is there a free route to an image that just opens?** No. It needs a
*Developer ID Application* certificate, that certificate needs a paid Apple
Developer Program membership, and notarisation needs the same account. This is
the one place where the $99 is not optional.

**If the membership is already paid, is anything left to do?** Yes, one thing:
the certificate is not issued automatically and has to be created. A paid
account with no Developer ID certificate looks exactly like a free one from the
keychain's point of view, which is the confusing part.

---

## What this machine can do right now

```sh
make signing-status
```

At the time of writing it reports an `Apple Development` identity and no
`Developer ID Application`. Those are different things and only the second one
can ship:

| Certificate | Made for | Ships to others |
|---|---|---|
| Apple Development | running your own builds on your own machines | no |
| Apple Distribution | App Store submission | App Store only |
| **Developer ID Application** | direct distribution | **yes** |

Having an Apple Development certificate and provisioning profiles means there
is a developer account. It says nothing about whether a Developer ID
certificate could be created, because one that was never created is absent in
exactly the same way as one that is not allowed.

### Creating the certificate

With a paid membership, the quickest route is Xcode, which handles the
certificate signing request for you:

**Xcode > Settings > Accounts >** select the team **> Manage Certificates >**
the **+** button **> Developer ID Application**.

The longer route, if Xcode is not installed: generate a certificate signing
request in Keychain Access (*Keychain Access > Certificate Assistant > Request a
Certificate From a Certificate Authority*, saved to disk), upload it at
developer.apple.com under *Certificates, Identifiers & Profiles > Certificates >
+ > Developer ID Application*, then download and double-click the result.

Two things to know before doing it. Only the **Account Holder** can create a
Developer ID certificate — for an individual account that is you, but for an
organisation it may not be. And the number an account may hold is limited and
revoking one invalidates everything already signed with it, so create it once
and keep the private key backed up. Losing that key means every future build
is signed by a different identity.

Afterwards `make signing-status` will list it, and `make dmg` will use it.

Note also that the two Apple-issued certificates on this machine whose names
begin with "Developer ID" are Apple's own intermediate authorities, part of the
trust chain. They are not a signing identity, which is why
`security find-identity` lists none.

---

## The full path, when the certificate exists

```sh
export PLUGSHELL_DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)"
export PLUGSHELL_NOTARY_PROFILE="plugshell-notary"
make dmg
```

Once, to store the notarisation credentials in the keychain:

```sh
xcrun notarytool store-credentials plugshell-notary \
    --apple-id you@example.com --team-id TEAMID \
    --password <app-specific-password>
```

The app-specific password is made at appleid.apple.com, not your account
password.

`make dmg` then signs the application with the hardened runtime, builds the
image, signs the image, submits it for notarisation, waits, and staples the
resulting ticket into it. Stapling matters: without it a machine that is
offline the first time it opens the image gets no verdict and blocks.

### The entitlement that is easy to miss

Notarisation requires the hardened runtime, and the hardened runtime refuses to
load code signed by anyone else. For a plugin host that is every plugin it
exists to load — so a correctly notarised build opens perfectly and then cannot
open a single VST3.

`tools/plugshell.entitlements` disables library validation for this reason. It
is not a workaround; it is the documented entitlement for exactly this case.

---

## Without a Developer ID

The image still builds, and `make dmg` says plainly what it is worth. Three
honest options, in the order worth considering them.

**Ship source.** This is an AGPL project and building it is `make deps &&
make build`. Most open-source audio software distributes this way and nobody
finds it strange. It costs nothing and has no Gatekeeper problem, because the
user built it themselves.

**A Homebrew cask.** `brew install --cask` removes the quarantine attribute as
part of installing, which is not a trick — the user asked for the software by
name on their own command line, which is the consent Gatekeeper is trying to
obtain. This is the best unsigned experience available and it is a normal way
to ship developer-facing macOS software.

**An unsigned image with instructions.** Works, and asks the most of the user.
The route on current macOS is to open the app, let it be blocked, then go to
System Settings > Privacy & Security, where an *Open Anyway* button appears for
about an hour after the attempt. The older Control-click > Open shortcut was
removed in a recent macOS release, so any instructions that mention it are out
of date and will waste your users' time.

Do not tell people to run `sudo spctl --master-disable`. It turns off
Gatekeeper for everything they will ever install, to solve a problem with one
application.

---

## Uninstalling

```sh
make uninstall          # or tools/uninstall.sh
```

It lists what it will delete and asks first. It removes the application, the
plugin index cache under Application Support, preferences and saved state.

It also resets the Screen Recording and Accessibility grants with `tccutil`,
because those live in the system's own database and survive deleting the
application — leaving an entry in System Settings pointing at something that no
longer exists.

It does not touch plugins or their presets. The host never writes to them.

---

## A note on signing for development

This is separate from distribution and matters much sooner.

macOS attaches Screen Recording and Accessibility to an application's code
signature. An ad-hoc signature — JUCE's default — has no stable identity, so
the system falls back to identifying the app by path and contents, and every
rebuild is a new application as far as the permission database is concerned.
Both permissions then have to be granted again after every build.

Any stable identity fixes this, including a self-signed one. The build does it
automatically; see [BUILD.md](BUILD.md).
