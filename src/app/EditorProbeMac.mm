// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include "EditorProbe.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

namespace plugshell
{

namespace
{

NSView* hostViewFor(juce::Component& c)
{
    if (auto* peer = c.getPeer())
        if (void* handle = peer->getNativeHandle())
            return (__bridge NSView*)handle;

    return nil;
}

/** Cocoa measures the screen from the bottom left of the display holding the
    menu bar; JUCE measures from its top left. Everything crossing between the
    two has to be flipped, and forgetting to is the classic way to click
    precisely the wrong place. */
CGFloat screenFlipOrigin()
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    return [screens count] > 0 ? NSMaxY([screens objectAtIndex:0].frame) : 0.0;
}

NSRect componentRectInView(juce::Component& c, NSView* view)
{
    const auto bounds = c.getScreenBounds();
    const NSRect onScreen =
        NSMakeRect((CGFloat)bounds.getX(), screenFlipOrigin() - (CGFloat)bounds.getBottom(),
                   (CGFloat)bounds.getWidth(), (CGFloat)bounds.getHeight());

    const NSRect inWindow = [[view window] convertRectFromScreen:onScreen];
    return [view convertRect:inWindow fromView:nil];
}

NSPoint pointInWindow(juce::Component& c, juce::Point<float> p, NSView* view)
{
    const auto onScreen = c.localPointToGlobal(p);
    const NSPoint cocoa = NSMakePoint((CGFloat)onScreen.x, screenFlipOrigin() - (CGFloat)onScreen.y);
    return [[view window] convertPointFromScreen:cocoa];
}

/** Whether every pixel sampled is the same. A view whose content the GPU draws
    hands back either nothing or a flat fill, and a flat image is never a real
    plugin editor, so this is a sound-enough test for "the cheap method did not
    work, use the expensive one". */
bool looksBlank(NSBitmapImageRep* rep)
{
    if (rep == nil)
        return true;

    const NSInteger w = [rep pixelsWide], h = [rep pixelsHigh];
    if (w < 2 || h < 2)
        return true;

    NSUInteger first[4] = {0, 0, 0, 0};
    [rep getPixel:first atX:0 y:0];

    for (NSInteger i = 0; i < 64; ++i)
    {
        NSUInteger px[4] = {0, 0, 0, 0};
        [rep getPixel:px atX:(w - 1) * (i % 8) / 7 y:(h - 1) * (i / 8) / 7];

        if (px[0] != first[0] || px[1] != first[1] || px[2] != first[2])
            return false;
    }

    return true;
}

bool writePNG(CGImageRef image, const juce::File& destination)
{
    if (image == nullptr)
        return false;

    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (png == nil)
        return false;

    return [png writeToFile:[NSString stringWithUTF8String:destination.getFullPathName().toRawUTF8()]
                 atomically:YES];
}

} // namespace

bool EditorProbe::canUseWindowServer()
{
    if (@available(macOS 10.15, *))
        return CGPreflightScreenCaptureAccess();

    return true;
}

EditorProbe::CaptureResult EditorProbe::capture(juce::Component& component, const juce::File& destination,
                                                CaptureMethod method)
{
    CaptureResult result;

    NSView* view = hostViewFor(component);
    if (view == nil || [view window] == nil)
    {
        result.error = "the component is not on screen";
        return result;
    }

    const NSRect rect = componentRectInView(component, view);
    if (NSIsEmptyRect(rect))
    {
        result.error = "the component has no area on screen";
        return result;
    }

    const auto tryViewCache = [&]() -> NSBitmapImageRep*
    {
        NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:rect];
        if (rep != nil)
            [view cacheDisplayInRect:rect toBitmapImageRep:rep];
        return rep;
    };

    if (method == CaptureMethod::viewCache || method == CaptureMethod::automatic)
    {
        NSBitmapImageRep* rep = tryViewCache();

        if (rep != nil && !(method == CaptureMethod::automatic && looksBlank(rep)))
        {
            NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

            if (png != nil &&
                [png writeToFile:[NSString stringWithUTF8String:destination.getFullPathName().toRawUTF8()]
                      atomically:YES])
            {
                result.ok = true;
                result.used = CaptureMethod::viewCache;
                result.width = (int)[rep pixelsWide];
                result.height = (int)[rep pixelsHigh];
                result.scale = rect.size.width > 0 ? (double)[rep pixelsWide] / rect.size.width : 1.0;
                return result;
            }
        }

        if (method == CaptureMethod::viewCache)
        {
            result.error = "the view returned nothing to draw; it is probably GPU-backed, "
                           "so use the windowServer method";
            return result;
        }
    }

    // Read back what was actually composited. This is the only method that
    // works for an editor drawn by OpenGL or Metal, which most modern synths
    // use for at least their spectrum and wavetable displays.
    if (!canUseWindowServer())
    {
        result.error = "Screen Recording permission is needed to capture a GPU-drawn editor; "
                       "grant it in System Settings > Privacy & Security > Screen Recording";
        return result;
    }

    const CGWindowID windowID = (CGWindowID)[[view window] windowNumber];
    juce::ignoreUnused(windowID);

    // CGWindowListCreateImage was the way to do this and was made unavailable
    // in macOS 15; ScreenCaptureKit replaces it, asynchronously and with a
    // heavier setup. Not yet wired up, and deliberately an honest failure
    // rather than a silently wrong image.
    result.error = "GPU-drawn editors need the ScreenCaptureKit path, which is not implemented yet "
                   "(CGWindowListCreateImage was removed in macOS 15)";
    result.used = CaptureMethod::windowServer;
    return result;
}

void EditorProbe::mouse(juce::Component& component, MouseAction action, juce::Point<float> p,
                        float scrollDelta, juce::ModifierKeys mods)
{
    NSView* view = hostViewFor(component);
    if (view == nil || [view window] == nil)
        return;

    NSWindow* window = [view window];
    const NSPoint location = pointInWindow(component, p, view);

    NSEventModifierFlags flags = 0;
    if (mods.isShiftDown())
        flags |= NSEventModifierFlagShift;
    if (mods.isCommandDown())
        flags |= NSEventModifierFlagCommand;
    if (mods.isAltDown())
        flags |= NSEventModifierFlagOption;
    if (mods.isCtrlDown())
        flags |= NSEventModifierFlagControl;

    if (action == MouseAction::scroll)
    {
        CGEventRef scroll =
            CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, (int32_t)scrollDelta);
        if (scroll == nullptr)
            return;

        // CGEvent measures from the top left, the same way JUCE does, so this
        // one does not get flipped.
        const auto onScreen = component.localPointToGlobal(p);
        CGEventSetLocation(scroll, CGPointMake((CGFloat)onScreen.x, (CGFloat)onScreen.y));
        CGEventSetFlags(scroll, (CGEventFlags)flags);

        if (NSEvent* asNS = [NSEvent eventWithCGEvent:scroll])
            [NSApp postEvent:asNS atStart:NO];

        CFRelease(scroll);
        return;
    }

    static NSInteger eventNumber = 0;

    NSEventType type = NSEventTypeMouseMoved;
    switch (action)
    {
    case MouseAction::down:
        type = NSEventTypeLeftMouseDown;
        break;
    case MouseAction::drag:
        type = NSEventTypeLeftMouseDragged;
        break;
    case MouseAction::up:
        type = NSEventTypeLeftMouseUp;
        break;
    case MouseAction::move:
    case MouseAction::scroll:
        type = NSEventTypeMouseMoved;
        break;
    }

    NSEvent* event = [NSEvent mouseEventWithType:type
                                        location:location
                                   modifierFlags:flags
                                       timestamp:[[NSProcessInfo processInfo] systemUptime]
                                    windowNumber:[window windowNumber]
                                         context:nil
                                     eventNumber:++eventNumber
                                      clickCount:action == MouseAction::move ? 0 : 1
                                        pressure:action == MouseAction::down ? 1.0f : 0.0f];

    if (event != nil)
        [NSApp postEvent:event atStart:NO];
}

void EditorProbe::drag(juce::Component& component, juce::Point<float> from, juce::Point<float> to, int steps,
                       juce::ModifierKeys mods)
{
    mouse(component, MouseAction::move, from, 0.0f, mods);
    mouse(component, MouseAction::down, from, 0.0f, mods);

    // Intermediate points matter. A control that accumulates the movement of a
    // drag -- which is how most knobs work, because a knob has no position on
    // screen to jump to -- lands somewhere quite different when given one
    // large step instead of the same distance travelled in small ones.
    for (int i = 1; i <= juce::jmax(1, steps); ++i)
    {
        const float t = (float)i / (float)juce::jmax(1, steps);
        mouse(component, MouseAction::drag, from + (to - from) * t, 0.0f, mods);
    }

    mouse(component, MouseAction::up, to, 0.0f, mods);
}

} // namespace plugshell
