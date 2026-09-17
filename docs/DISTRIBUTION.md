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

You can still distribute. macOS does not require anyone's permission to hand
out software, and `make dmg` still builds an image. What you cannot do is hand
out an image that opens without a detour, and the detours are getting narrower.

Note also that an expired membership is not the same as never having had one.
A Developer ID certificate issued while a membership was active keeps working
until the certificate itself expires, years later, so builds can go on being
signed. But **notarisation requires an active membership**, and so does
creating a certificate in the first place. Letting a membership lapse with no
certificate in hand leaves nothing behind.

Three honest options, in the order worth considering them.

**Ship source.** This is the recommended answer for this project, not a
consolation prize. It is an AGPL project, building it is `make deps && make
build`, and its audience — people wiring a plugin host to an agent — already
has a terminal open. A locally built application is signed ad-hoc, which is
enough for macOS to run it, and Gatekeeper never enters the picture because
nothing was downloaded. It also happens to be what the licence requires anyone
redistributing binaries to offer regardless.

**A Homebrew tap of your own.** This used to be the good answer and is now a
qualified one. Homebrew is
[removing `--no-quarantine`](https://github.com/Homebrew/brew/issues/20755),
and the main cask repository
[began disabling casks that fail Gatekeeper on 1 September 2026](https://github.com/orgs/Homebrew/discussions/6334)
— 387 of 7624 casks, about five per cent. A tap you host yourself is not
subject to the main repository's policy, so this still works, but it is now a
route that is being closed rather than one being kept open, and anything
written about it before late 2025 describes a world that no longer exists.

**An unsigned image with instructions.** Works, and asks the most of the user.
The route on current macOS is to open the app, let it be blocked, then go to
System Settings > Privacy & Security, where an *Open Anyway* button appears for
about an hour after the attempt. The older Control-click > Open shortcut was
removed in a recent macOS release, so any instructions that mention it are out
of date and will waste your users' time.

Note the direction of all of this. Every bypass except building from source has
narrowed in the last two years, and none of them has widened. Plans that depend
on one of them should expect to be revisited.

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
