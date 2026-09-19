// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include "AttachedPanel.h"

#import <Cocoa/Cocoa.h>

namespace plugshell::child
{

namespace
{
/** JUCE's screen rectangle to Cocoa's.

    They disagree about which corner is the origin and which way y runs, and
    both measure from the first screen -- so the conversion is one subtraction
    against that screen's height, not against whichever display the rectangle
    happens to be on. */
NSRect toCocoa(juce::Rectangle<int> r)
{
    NSArray<NSScreen*>* screens = [NSScreen screens];

    if ([screens count] == 0)
        return NSMakeRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());

    const CGFloat firstScreenTop = NSMaxY([[screens objectAtIndex:0] frame]);

    return NSMakeRect((CGFloat)r.getX(), firstScreenTop - (CGFloat)(r.getY() + r.getHeight()),
                      (CGFloat)juce::jmax(1, r.getWidth()), (CGFloat)juce::jmax(1, r.getHeight()));
}
} // namespace

AttachedPanel::AttachedPanel()
{
    // Borderless, because the host is drawing the frame this sits inside;
    // non-activating, because clicking the plugin must not take the keyboard
    // away from the host.
    NSPanel* p = [[NSPanel alloc]
        initWithContentRect:NSMakeRect(0, 0, 100, 100)
                  styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                    backing:NSBackingStoreBuffered
                      defer:NO];

    // Ordinary level. It is put above the host's window by ordering against
    // that window's number, in showAbove, rather than by being raised above
    // the whole screen -- see the note in the header for why that difference
    // is the whole design.
    [p setLevel:NSNormalWindowLevel];

    // This process is an accessory with no menu bar, so it is never the
    // active application and a panel that hid on deactivation would never be
    // seen at all.
    [p setHidesOnDeactivate:NO];
    [p setReleasedWhenClosed:NO];
    [p setOpaque:YES];
    [p setHasShadow:NO];
    [p setBackgroundColor:[NSColor blackColor]];

    // Follows the host onto whichever desktop it is on, and stays out of
    // Mission Control, where a lone rectangle of somebody else's plugin would
    // be one window too many.
    [p setCollectionBehavior:(NSWindowCollectionBehaviorMoveToActiveSpace |
                              NSWindowCollectionBehaviorFullScreenAuxiliary |
                              NSWindowCollectionBehaviorTransient)];
    [p setExcludedFromWindowsMenu:YES];

    panel = (__bridge_retained void*)p;
}

AttachedPanel::~AttachedPanel()
{
    if (panel == nullptr)
        return;

    NSPanel* p = (__bridge_transfer NSPanel*)panel;
    panel = nullptr;

    [p orderOut:nil];
    [p close];
}

void* AttachedPanel::contentView() const noexcept
{
    if (panel == nullptr)
        return nullptr;

    return (__bridge void*)[(__bridge NSPanel*)panel contentView];
}

void AttachedPanel::setBounds(juce::Rectangle<int> screenBounds)
{
    if (panel == nullptr)
        return;

    NSPanel* p = (__bridge NSPanel*)panel;
    [p setFrame:[p frameRectForContentRect:toCocoa(screenBounds)] display:YES];
}

void AttachedPanel::showAbove(std::uint32_t hostWindowNumber)
{
    if (panel == nullptr)
        return;

    NSPanel* p = (__bridge NSPanel*)panel;

    // Ordered, never made key. Key is the thing this panel exists not to take.
    //
    // A window number is global to the window server, so this reaches across
    // the process boundary where addChildWindow: cannot.
    if (hostWindowNumber != 0)
        [p orderWindow:NSWindowAbove relativeTo:(NSInteger)hostWindowNumber];
    else
        [p orderFront:nil];
}

void AttachedPanel::hide()
{
    if (panel != nullptr)
        [(__bridge NSPanel*)panel orderOut:nil];
}

} // namespace plugshell::child
