/* X11Controller.h -- connect the IB ui
 *
 * Copyright (c) 2002-2012 Apple Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT
 * HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above
 * copyright holders shall not be used in advertising or otherwise to
 * promote the sale, use or other dealings in this Software without
 * prior written authorization.
 */

#ifndef X11CONTROLLER_H
#define X11CONTROLLER_H 1

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#if __OBJC__

#include "sanitizedCocoa.h"
#include "xpr/x-list.h"

#ifdef XQUARTZ_SPARKLE
#define BOOL OSX_BOOL
#include <Sparkle/SUUpdater.h>
#undef BOOL
#endif

#ifndef NSINTEGER_DEFINED
#if __LP64__ || NS_BUILD_32_LIKE_64
typedef long NSInteger;
typedef unsigned long NSUInteger;
#else
typedef int NSInteger;
typedef unsigned int NSUInteger;
#endif
#endif

@interface X11Controller : NSObject
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1060
    <NSTableViewDataSource>
#endif
{
    IBOutlet NSPanel *prefs_panel;

    IBOutlet NSButton *fake_buttons;
    IBOutlet NSButton *enable_fullscreen;
    IBOutlet NSButton *enable_fullscreen_menu;
    IBOutlet NSTextField *enable_fullscreen_menu_text;
    IBOutlet NSButton *enable_keyequivs;
    IBOutlet NSButton *sync_keymap;
    IBOutlet NSButton *option_sends_alt;
    IBOutlet NSButton *scroll_in_device_direction;
    IBOutlet NSButton *click_through;
    IBOutlet NSButton *focus_follows_mouse;
    IBOutlet NSButton *focus_on_new_window;
    IBOutlet NSButton *enable_auth;
    IBOutlet NSButton *enable_tcp;
    IBOutlet NSButton *sync_pasteboard;
    IBOutlet NSButton *sync_pasteboard_to_clipboard;
    IBOutlet NSButton *sync_pasteboard_to_primary;
    IBOutlet NSButton *sync_clipboard_to_pasteboard;
    IBOutlet NSButton *sync_primary_immediately;
    IBOutlet NSTextField *sync_text1;
    IBOutlet NSTextField *sync_text2;
    IBOutlet NSPopUpButton *depth;

    IBOutlet NSMenuItem *window_separator;
    // window_separator is DEPRECATED due to this radar:
    // <rdar://problem/7088335> NSApplication releases the separator in the Windows menu even though it's an IBOutlet
    // It is kept around for localization compatability and is subject to removal "eventually"
    // If it is !NULL (meaning it is in the nib), it is removed from the menu and released

    IBOutlet NSMenuItem *x11_about_item;
    IBOutlet NSMenuItem *dock_window_separator;
    IBOutlet NSMenuItem *apps_separator;
    IBOutlet NSMenuItem *toggle_fullscreen_item;
#ifdef XQUARTZ_SPARKLE
    NSMenuItem *check_for_updates_item; // Programatically enabled
#endif
    IBOutlet NSMenuItem *copy_menu_item;
    IBOutlet NSMenu *dock_apps_menu;
    IBOutlet NSTableView *apps_table;

    NSArray *apps;
    NSMutableArray *table_apps;

    IBOutlet NSMenu *dock_menu;

    // This is where in the Windows menu we'll start (this will be the index of the separator)
    NSInteger windows_menu_start;

    int checked_window_item;
    x_list *pending_apps;

    OSX_BOOL finished_launching;
    OSX_BOOL can_quit;
}

- (void)set_window_menu:(NSArray *)list;
- (void)set_window_menu_check:(NSNumber *)n;
- (void)set_apps_menu:(NSArray *)list;
#ifdef XQUARTZ_SPARKLE
- (void)setup_sparkle;
- (void)updater:(SUUpdater *)updater willInstallUpdate:(SUAppcastItem *)
   update;
#endif
- (void)set_can_quit:(OSX_BOOL)state;
- (void)server_ready;
- (OSX_BOOL)application:(NSApplication *)app openFile:(NSString *)filename;

- (IBAction)apps_table_show:(id)sender;
- (IBAction)apps_table_done:(id)sender;
- (IBAction)apps_table_new:(id)sender;
- (IBAction)apps_table_duplicate:(id)sender;
- (IBAction)apps_table_delete:(id)sender;
- (IBAction)bring_to_front:(id)sender;
- (IBAction)close_window:(id)sender;
- (IBAction)minimize_window:(id)sender;
- (IBAction)zoom_window:(id)sender;
- (IBAction)next_window:(id)sender;
- (IBAction)previous_window:(id)sender;
- (IBAction)enable_fullscreen_changed:(id)sender;
- (IBAction)toggle_fullscreen:(id)sender;
- (IBAction)prefs_changed:(id)sender;
- (IBAction)prefs_show:(id)sender;
- (IBAction)quit:(id)sender;
- (IBAction)x11_help:(id)sender;

@end

#endif /* __OBJC__ */

void
X11ControllerMain(int argc, char **argv, char **envp);

#endif /* X11CONTROLLER_H */
