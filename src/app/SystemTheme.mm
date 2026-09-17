// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 wheatfox <wheatfox17@icloud.com>

#import <Cocoa/Cocoa.h>

namespace plugshell::theme
{

/** Asked of AppKit rather than cached, because the user can change it while
    the application is running and the only thing that makes "automatic" worth
    offering is that it follows. */
bool systemPrefersDark()
{
    NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
}

} // namespace plugshell::theme
