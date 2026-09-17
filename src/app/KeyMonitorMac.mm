// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include "KeyMonitor.h"

#import <Cocoa/Cocoa.h>

namespace plugshell
{

KeyMonitor::KeyMonitor() = default;

KeyMonitor::~KeyMonitor() { stop(); }

void KeyMonitor::start(Handler h)
{
    stop();
    handler = std::move(h);

    if (!handler)
        return;

    // A local monitor sees events destined for this application only, which is
    // the right scope: a global monitor would need accessibility permission and
    // would watch the user's other applications, which this has no business
    // doing.
    monitor = (__bridge_retained void*)[NSEvent
        addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
                                     handler:^NSEvent*(NSEvent* event) {
                                         if (!handler)
                                             return event;

                                         NSString* chars = [event charactersIgnoringModifiers];
                                         if ([chars length] == 0)
                                             return event;

                                         const int c = (int)[[chars lowercaseString] characterAtIndex:0];
                                         const int code = (int)[event keyCode];
                                         const bool isDown = ([event type] == NSEventTypeKeyDown);
                                         const bool isRepeat = isDown && [event isARepeat];
                                         const bool cmd =
                                             ([event modifierFlags] & NSEventModifierFlagCommand) != 0;

                                         // Returning nil swallows the event; returning it passes it on.
                                         return handler(c, code, isDown, isRepeat, cmd) ? nil : event;
                                     }];
}

bool KeyMonitor::isHeld(int keyCode)
{
    // The combined session state, not the HID state, so a key sent by a script
    // or an accessibility tool counts as held the same as a finger does.
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, (CGKeyCode)keyCode);
}

void KeyMonitor::stop()
{
    if (monitor != nullptr)
    {
        id token = (__bridge_transfer id)monitor;
        [NSEvent removeMonitor:token];
        monitor = nullptr;
    }
    handler = nullptr;
}

} // namespace plugshell
