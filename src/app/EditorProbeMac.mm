// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#include "EditorProbe.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

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

/** Reads back what the window server composited for one window.

    This replaces CGWindowListCreateImage, which macOS 15 removed, and it is
    needed far more often than it first appeared. A plugin editor can be
    perfectly readable through the view cache while it is starting up and
    become opaque to it seconds later, once the plugin finishes initialising
    and moves its drawing onto the GPU. Pigments does exactly that: a capture
    four seconds after loading succeeds, and the same capture at seven seconds
    does not.

    Both calls are asynchronous and are waited on, with timeouts, because the
    caller wants an image rather than a callback. */
API_AVAILABLE(macos(14.0))
CGImageRef captureWindowComposited(CGWindowID windowID, juce::String& error)
{
    __block SCWindow* target = nil;
    dispatch_semaphore_t found = dispatch_semaphore_create(0);

    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                               onScreenWindowsOnly:NO
                                 completionHandler:^(SCShareableContent* content, NSError* e) {
                                     if (e == nil)
                                         for (SCWindow* w in content.windows)
                                             if (w.windowID == windowID)
                                             {
                                                 target = w;
                                                 break;
                                             }
                                     dispatch_semaphore_signal(found);
                                 }];

    if (dispatch_semaphore_wait(found, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC))) != 0)
    {
        error = "timed out asking the window server what it can share";
        return nullptr;
    }

    if (target == nil)
    {
        error = "the window server does not list this window; Screen Recording permission is "
                "probably not granted";
        return nullptr;
    }

    SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target];

    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.width = (size_t)(target.frame.size.width * 2.0);
    config.height = (size_t)(target.frame.size.height * 2.0);
    config.showsCursor = NO;
    config.captureResolution = SCCaptureResolutionBest;

    __block CGImageRef image = nullptr;
    __block juce::String failure;
    dispatch_semaphore_t shot = dispatch_semaphore_create(0);

    [SCScreenshotManager captureImageWithFilter:filter
                                  configuration:config
                              completionHandler:^(CGImageRef sample, NSError* e) {
                                  if (sample != nullptr)
                                      image = CGImageRetain(sample);
                                  else if (e != nil)
                                      failure = juce::String::fromUTF8([[e localizedDescription] UTF8String]);
                                  dispatch_semaphore_signal(shot);
                              }];

    if (dispatch_semaphore_wait(shot, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC))) != 0)
    {
        error = "timed out waiting for the screenshot";
        return nullptr;
    }

    if (image == nullptr)
        error = failure.isNotEmpty() ? failure : "the window server returned no image";

    return image;
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

bool EditorProbe::hasScreenRecordingPermission()
{
    if (@available(macOS 10.15, *))
        return CGPreflightScreenCaptureAccess();

    return true;
}

bool EditorProbe::hasAccessibilityPermission() { return AXIsProcessTrusted(); }

bool EditorProbe::requestAccessibilityPermission()
{
    NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

bool EditorProbe::requestScreenRecordingPermission()
{
    if (@available(macOS 10.15, *))
        return CGPreflightScreenCaptureAccess() || CGRequestScreenCaptureAccess();

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

    result.used = CaptureMethod::windowServer;

    if (@available(macOS 14.0, *))
    {
        juce::String why;
        CGImageRef whole = captureWindowComposited((CGWindowID)[[view window] windowNumber], why);

        if (whole == nullptr)
        {
            result.error = why;
            return result;
        }

        // The image covers the window's frame in backing pixels, so the crop
        // is scaled, and measured from the top rather than from the Cocoa
        // baseline the rest of this file works in.
        const NSRect frame = [[view window] frame];
        const double scale = frame.size.width > 0 ? (double)CGImageGetWidth(whole) / frame.size.width : 1.0;

        const NSRect inWindow = [view convertRect:rect toView:nil];
        const CGRect crop =
            CGRectMake(inWindow.origin.x * scale, (frame.size.height - NSMaxY(inWindow)) * scale,
                       inWindow.size.width * scale, inWindow.size.height * scale);

        CGImageRef cropped = CGImageCreateWithImageInRect(whole, crop);
        CGImageRef chosen = cropped != nullptr ? cropped : whole;

        result.ok = writePNG(chosen, destination);
        result.width = (int)CGImageGetWidth(chosen);
        result.height = (int)CGImageGetHeight(chosen);
        result.scale = scale;

        if (!result.ok)
            result.error = "could not write the image";

        if (cropped != nullptr)
            CGImageRelease(cropped);
        CGImageRelease(whole);

        return result;
    }

    result.error = "capturing a GPU-drawn editor needs macOS 14 or later";
    return result;
}

bool EditorProbe::useSystemEvents = true;

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

    // Two ways to deliver, because the polite one does not always arrive.
    //
    // Posting into the application's own queue keeps the user's cursor where
    // it was and cannot leak input into another application, which is why it
    // is preferred. But a plugin that reads the pointer position from the
    // window server rather than from the event -- or that only trusts events
    // the system itself generated -- sees nothing, and the host has no way to
    // tell that from the outside.
    //
    // A real system event is indistinguishable from a mouse, at the cost of
    // moving the actual cursor and requiring the window to be in front.
    if (useSystemEvents)
    {
        CGEventType cgType = kCGEventMouseMoved;
        switch (action)
        {
        case MouseAction::down:
            cgType = kCGEventLeftMouseDown;
            break;
        case MouseAction::drag:
            cgType = kCGEventLeftMouseDragged;
            break;
        case MouseAction::up:
            cgType = kCGEventLeftMouseUp;
            break;
        default:
            break;
        }

        // CGEvent measures from the top left, the same way JUCE does.
        const auto onScreen = component.localPointToGlobal(p);
        CGEventRef ev = CGEventCreateMouseEvent(
            nullptr, cgType, CGPointMake((CGFloat)onScreen.x, (CGFloat)onScreen.y), kCGMouseButtonLeft);
        if (ev != nullptr)
        {
            CGEventSetFlags(ev, (CGEventFlags)flags);
            CGEventPost(kCGHIDEventTap, ev);
            CFRelease(ev);
        }

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
