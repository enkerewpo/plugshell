// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 plugshell contributors
//
// macOS implementation of EditorSurface.
//
// Unimplemented by design: the strategies declared in EditorSurface are
// candidates, not decisions. Phase 1 fills this in one strategy at a time and
// records which plugins each one works for.

#include "core/EditorSurface.h"

namespace plugshell
{

// TODO(phase-1): implement capture via juce::createSnapshotOfNativeWindow,
// then NSView caching, then ScreenCaptureKit as the fallback that always
// works but costs a permission prompt.

// TODO(phase-1): implement input via synthetic NSEvent posted to the editor
// view, falling back to session-level CGEvent.

} // namespace plugshell
