/* X11Controller.m -- connect the IB ui, also the NSApp delegate
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

#include "sanitizedCarbon.h"
#include <AvailabilityMacros.h>

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "quartzCommon.h"

#import "X11Controller.h"
#import "X11Application.h"

#include "opaque.h"
#include "darwin.h"
#include "darwinEvents.h"
#include "quartz.h"
#include "quartzKeyboard.h"
#include <X11/extensions/applewmconst.h>
#include "applewmExt.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <asl.h>
#include <stdlib.h>

extern aslclient aslc;
extern char *bundle_id_prefix;

@implementation X11Controller

- (void) awakeFromNib
{
    X11Application *xapp = NSApp;
    NSArray *array;

    /* Point X11Application at ourself. */
    [xapp set_controller:self];

    array = [xapp prefs_get_array:@PREFS_APPSMENU];
    if (array != nil) {
        int count;

        /* convert from [TITLE1 COMMAND1 TITLE2 COMMAND2 ...]
           to [[TITLE1 COMMAND1] [TITLE2 COMMAND2] ...] format. */

        count = [array count];
        if (count > 0
            && ![[array objectAtIndex:0] isKindOfClass:[NSArray class]]) {
            int i;
            NSMutableArray *copy, *sub;

            copy = [NSMutableArray arrayWithCapacity:(count / 2)];

            for (i = 0; i < count / 2; i++) {
                sub = [[NSMutableArray alloc] initWithCapacity:3];
                [sub addObject:[array objectAtIndex:i * 2]];
                [sub addObject:[array objectAtIndex:i * 2 + 1]];
                [sub addObject:@""];
                [copy addObject:sub];
                [sub release];
            }

            array = copy;
        }

        [self set_apps_menu:array];
    }

    [[NSNotificationCenter defaultCenter]
     addObserver: self
        selector: @selector(apps_table_done:)
            name: NSWindowWillCloseNotification
          object: [apps_table window]];

    // Setup data about our Windows menu
    if (window_separator) {
        [[window_separator menu] removeItem:window_separator];
        window_separator = nil;
    }

    windows_menu_start = [[X11App windowsMenu] numberOfItems];
}

- (void) item_selected:sender
{
    [NSApp activateIgnoringOtherApps:YES];

    DarwinSendDDXEvent(kXquartzControllerNotify, 2,
                       AppleWMWindowMenuItem, [sender tag]);
}

- (void) remove_window_menu
{
    NSMenu *menu;
    int count, i;

    /* Work backwards so we don't mess up the indices */
    menu = [X11App windowsMenu];
    count = [menu numberOfItems];
    for (i = count - 1; i >= windows_menu_start; i--)
        [menu removeItemAtIndex:i];

    count = [dock_menu indexOfItem:dock_window_separator];
    for (i = 0; i < count; i++)
        [dock_menu removeItemAtIndex:0];
}

- (void) install_window_menu:(NSArray *)list
{
    NSMenu *menu;
    NSMenuItem *item;
    int first, count, i;

    menu = [X11App windowsMenu];
    first = windows_menu_start + 1;
    count = [list count];

    // Push a Separator
    if (count) {
        [menu addItem:[NSMenuItem separatorItem]];
    }

    for (i = 0; i < count; i++) {
        NSString *name, *shortcut;

        name = [[list objectAtIndex:i] objectAtIndex:0];
        shortcut = [[list objectAtIndex:i] objectAtIndex:1];

        if (windowItemModMask == 0 || windowItemModMask == -1)
            shortcut = @"";

        item =
            (NSMenuItem *)[menu addItemWithTitle:name action:
                           @selector
                           (item_selected:) keyEquivalent:shortcut];
        [item setKeyEquivalentModifierMask:(NSUInteger)windowItemModMask];
        [item setTarget:self];
        [item setTag:i];
        [item setEnabled:YES];

        item = (NSMenuItem *)[dock_menu  insertItemWithTitle:name
                                                      action:@selector
                              (item_selected:) keyEquivalent:shortcut
                                                     atIndex:i];
        [item setKeyEquivalentModifierMask:(NSUInteger)windowItemModMask];
        [item setTarget:self];
        [item setTag:i];
        [item setEnabled:YES];
    }

    if (checked_window_item >= 0 && checked_window_item < count) {
        item = (NSMenuItem *)[menu itemAtIndex:first + checked_window_item];
        [item setState:NSOnState];
        item = (NSMenuItem *)[dock_menu itemAtIndex:checked_window_item];
        [item setState:NSOnState];
    }
}

- (void) remove_apps_menu
{
    NSMenu *menu;
    NSMenuItem *item;
    int i;

    if (apps == nil || apps_separator == nil) return;

    menu = [apps_separator menu];

    if (menu != nil) {
        for (i = [menu numberOfItems] - 1; i >= 0; i--) {
            item = (NSMenuItem *)[menu itemAtIndex:i];
            if ([item tag] != 0)
                [menu removeItemAtIndex:i];
        }
    }

    if (dock_apps_menu != nil) {
        for (i = [dock_apps_menu numberOfItems] - 1; i >= 0; i--) {
            item = (NSMenuItem *)[dock_apps_menu itemAtIndex:i];
            if ([item tag] != 0)
                [dock_apps_menu removeItemAtIndex:i];
        }
    }

    [apps release];
    apps = nil;
}

- (void) prepend_apps_item:(NSArray *)list index:(int)i menu:(NSMenu *)menu
{
    NSString *title, *shortcut = @"";
    NSArray *group;
    NSMenuItem *item;

    group = [list objectAtIndex:i];
    title = [group objectAtIndex:0];
    if ([group count] >= 3)
        shortcut = [group objectAtIndex:2];

    if ([title length] != 0) {
        item = (NSMenuItem *)[menu insertItemWithTitle:title
                                                action:@selector (
                                  app_selected:)
                              keyEquivalent:shortcut atIndex:0];
        [item setTarget:self];
        [item setEnabled:YES];
    }
    else {
        item = (NSMenuItem *)[NSMenuItem separatorItem];
        [menu insertItem:item atIndex:0];
    }

    [item setTag:i + 1];                  /* can't be zero, so add one */
}

- (void) install_apps_menu:(NSArray *)list
{
    NSMenu *menu;
    int i, count;

    count = [list count];

    if (count == 0 || apps_separator == nil) return;

    menu = [apps_separator menu];

    for (i = count - 1; i >= 0; i--) {
        if (menu != nil)
            [self prepend_apps_item:list index:i menu:menu];
        if (dock_apps_menu != nil)
            [self prepend_apps_item:list index:i menu:dock_apps_menu];
    }

    apps = [list retain];
}

- (void) set_window_menu:(NSArray *)list
{
    [self remove_window_menu];
    [self install_window_menu:list];

    DarwinSendDDXEvent(kXquartzControllerNotify, 1,
                       AppleWMWindowMenuNotify);
}

- (void) set_window_menu_check:(NSNumber *)nn
{
    NSMenu *menu;
    NSMenuItem *item;
    int first, count;
    int n = [nn intValue];

    menu = [X11App windowsMenu];
    first = windows_menu_start + 1;
    count = [menu numberOfItems] - first;

    if (checked_window_item >= 0 && checked_window_item < count) {
        item = (NSMenuItem *)[menu itemAtIndex:first + checked_window_item];
        [item setState:NSOffState];
        item = (NSMenuItem *)[dock_menu itemAtIndex:checked_window_item];
        [item setState:NSOffState];
    }
    if (n >= 0 && n < count) {
        item = (NSMenuItem *)[menu itemAtIndex:first + n];
        [item setState:NSOnState];
        item = (NSMenuItem *)[dock_menu itemAtIndex:n];
        [item setState:NSOnState];
    }
    checked_window_item = n;
}

- (void) set_apps_menu:(NSArray *)list
{
    [self remove_apps_menu];
    [self install_apps_menu:list];
}

#ifdef XQUARTZ_SPARKLE
- (void) setup_sparkle
{
    if (check_for_updates_item)
        return;  // already did it...

    NSMenu *menu = [x11_about_item menu];

    check_for_updates_item =
        [menu insertItemWithTitle:NSLocalizedString(
             @"Check for X11 Updates...",
             @"Check for X11 Updates...")
         action:@selector (
             checkForUpdates:)
         keyEquivalent:@""
         atIndex:1];
    [check_for_updates_item setTarget:[SUUpdater sharedUpdater]];
    [check_for_updates_item setEnabled:YES];

    // Set X11Controller as the delegate for the updater.
    [[SUUpdater sharedUpdater] setDelegate:self];
}

// Sent immediately before installing the specified update.
- (void)updater:(SUUpdater *)updater willInstallUpdate:(SUAppcastItem *)
   update
{
    //[self set_can_quit:YES];
}

#endif

- (void) launch_client:(NSString *)filename
{
    int child1, child2 = 0;
    int status;
    const char *newargv[4];
    char buf[128];
    char *s;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
    int stdout_pipe[2];
    int stderr_pipe[2];
#endif

    newargv[0] = [X11App prefs_get_string:@PREFS_LOGIN_SHELL default:"/bin/sh"];
    newargv[1] = "-c";
    newargv[2] = [filename UTF8String];
    newargv[3] = NULL;

    s = getenv("DISPLAY");
    if (s == NULL || s[0] == 0) {
        snprintf(buf, sizeof(buf), ":%s", display);
        setenv("DISPLAY", buf, TRUE);
    }

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
    if (&asl_log_descriptor) {
        char *asl_sender;
        aslmsg amsg = asl_new(ASL_TYPE_MSG);
        assert(amsg);

        asprintf(&asl_sender, "%s.%s", bundle_id_prefix, newargv[2]);
        assert(asl_sender);
        for(s = asl_sender + strlen(bundle_id_prefix) + 1; *s; s++) {
            if(! ((*s >= 'a' && *s <= 'z') ||
                  (*s >= 'A' && *s <= 'Z') ||
                  (*s >= '0' && *s <= '9'))) {
                *s = '_';
            }
        }

        (void)asl_set(amsg, ASL_KEY_SENDER, asl_sender);
        free(asl_sender);

        assert(0 == pipe(stdout_pipe));
        fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);

        assert(0 == pipe(stderr_pipe));
        fcntl(stderr_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

        asl_log_descriptor(aslc, amsg, ASL_LEVEL_INFO, stdout_pipe[0], ASL_LOG_DESCRIPTOR_READ);
        asl_log_descriptor(aslc, amsg, ASL_LEVEL_NOTICE, stderr_pipe[0], ASL_LOG_DESCRIPTOR_READ);

        asl_free(amsg);
    }
#endif

    /* Do the fork-twice trick to avoid having to reap zombies */
    child1 = fork();
    switch (child1) {
    case -1:                                    /* error */
        break;

    case 0:                                     /* child1 */
        child2 = fork();

        switch (child2) {
            int max_files, i;

        case -1:                                    /* error */
            _exit(1);

        case 0:                                     /* child2 */
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
            if (&asl_log_descriptor) {
                /* Replace our stdout/stderr */
                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stderr_pipe[1], STDERR_FILENO);
            }
#endif

            /* close all open files except for standard streams */
            max_files = sysconf(_SC_OPEN_MAX);
            for (i = 3; i < max_files; i++)
                close(i);

            /* ensure stdin is on /dev/null */
            close(0);
            open("/dev/null", O_RDONLY);

            execvp(newargv[0], (char * *const)newargv);
            _exit(2);

        default:                                    /* parent (child1) */
            _exit(0);
        }
        break;

    default:                                    /* parent */
        waitpid(child1, &status, 0);
    }

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
    if (&asl_log_descriptor) {
        /* Close the write ends of the pipe */
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
    }
#endif
}

- (void) app_selected:sender
{
    int tag;
    NSString *item;

    tag = [sender tag] - 1;
    if (apps == nil || tag < 0 || tag >= [apps count])
        return;

    item = [[apps objectAtIndex:tag] objectAtIndex:1];

    [self launch_client:item];
}

- (IBAction) apps_table_show:sender
{
    NSArray *columns;
    NSMutableArray *oldapps = nil;

    if (table_apps != nil)
        oldapps = table_apps;

    table_apps = [[NSMutableArray alloc] initWithCapacity:1];
    if (apps != nil)
        [table_apps addObjectsFromArray:apps];

    columns = [apps_table tableColumns];
    [[columns objectAtIndex:0] setIdentifier:@"0"];
    [[columns objectAtIndex:1] setIdentifier:@"1"];
    [[columns objectAtIndex:2] setIdentifier:@"2"];

    [apps_table setDataSource:self];
    [apps_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
     byExtendingSelection:NO];

    [[apps_table window] makeKeyAndOrderFront:sender];
    [apps_table reloadData];
    if (oldapps != nil)
        [oldapps release];
}

- (IBAction) apps_table_done:sender
{
    [apps_table deselectAll:sender];    /* flush edits? */

    [self remove_apps_menu];
    [self install_apps_menu:table_apps];

    [NSApp prefs_set_array:@PREFS_APPSMENU value:table_apps];
    [NSApp prefs_synchronize];

    [[apps_table window] orderOut:sender];

    [table_apps release];
    table_apps = nil;
}

- (IBAction) apps_table_new:sender
{
    NSMutableArray *item;

    int row = [apps_table selectedRow], i;

    if (row < 0) row = 0;
    else row = row + 1;

    i = row;
    if (i > [table_apps count])
        return;                         /* avoid exceptions */

    [apps_table deselectAll:sender];

    item = [[NSMutableArray alloc] initWithCapacity:3];
    [item addObject:@""];
    [item addObject:@""];
    [item addObject:@""];

    [table_apps insertObject:item atIndex:i];
    [item release];

    [apps_table reloadData];
    [apps_table selectRowIndexes:[NSIndexSet indexSetWithIndex:row]
     byExtendingSelection:NO];
}

- (IBAction) apps_table_duplicate:sender
{
    int row = [apps_table selectedRow], i;
    NSObject *item;

    if (row < 0) {
        [self apps_table_new:sender];
        return;
    }

    i = row;
    if (i > [table_apps count] - 1) return;                             /* avoid exceptions */

    [apps_table deselectAll:sender];

    item = [[table_apps objectAtIndex:i] mutableCopy];
    [table_apps insertObject:item atIndex:i];
    [item release];

    [apps_table reloadData];
    [apps_table selectRowIndexes:[NSIndexSet indexSetWithIndex:row +
                                  1] byExtendingSelection:NO];
}

- (IBAction) apps_table_delete:sender
{
    int row = [apps_table selectedRow];

    if (row >= 0) {
        int i = row;

        if (i > [table_apps count] - 1) return;                 /* avoid exceptions */

        [apps_table deselectAll:sender];

        [table_apps removeObjectAtIndex:i];
    }

    [apps_table reloadData];

    row = MIN(row, [table_apps count] - 1);
    if (row >= 0)
        [apps_table selectRowIndexes:[NSIndexSet indexSetWithIndex:row]
         byExtendingSelection:NO];
}

- (NSInteger) numberOfRowsInTableView:(NSTableView *)tableView
{
    if (table_apps == nil) return 0;

    return [table_apps count];
}

- (id)             tableView:(NSTableView *)tableView
   objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSArray *item;
    int col;

    if (table_apps == nil) return nil;

    col = [[tableColumn identifier] intValue];

    item = [table_apps objectAtIndex:row];
    if ([item count] > col)
        return [item objectAtIndex:col];
    else
        return @"";
}

- (void) tableView:(NSTableView *)tableView setObjectValue:(id)object
    forTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSMutableArray *item;
    int col;

    if (table_apps == nil) return;

    col = [[tableColumn identifier] intValue];

    item = [table_apps objectAtIndex:row];
    [item replaceObjectAtIndex:col withObject:object];
}

- (void) hide_window:sender
{
    if ([X11App x_active])
        DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMHideWindow);
    else
        NSBeep();                       /* FIXME: something here */
}

- (IBAction)bring_to_front:sender
{
    DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMBringAllToFront);
}

- (IBAction)close_window:sender
{
    if ([X11App x_active])
        DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMCloseWindow);
    else
        [[NSApp keyWindow] performClose:sender];
}

- (IBAction)minimize_window:sender
{
    if ([X11App x_active])
        DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMMinimizeWindow);
    else
        [[NSApp keyWindow] performMiniaturize:sender];
}

- (IBAction)zoom_window:sender
{
    if ([X11App x_active])
        DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMZoomWindow);
    else
        [[NSApp keyWindow] performZoom:sender];
}

- (IBAction) next_window:sender
{
    DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMNextWindow);
}

- (IBAction) previous_window:sender
{
    DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMPreviousWindow);
}

- (IBAction) enable_fullscreen_changed:sender
{
    XQuartzRootlessDefault = ![enable_fullscreen intValue];

    [enable_fullscreen_menu setEnabled:!XQuartzRootlessDefault];
    [enable_fullscreen_menu_text setTextColor:XQuartzRootlessDefault ?[
         NSColor disabledControlTextColor] : [NSColor controlTextColor]];

    DarwinSendDDXEvent(kXquartzSetRootless, 1, XQuartzRootlessDefault);

    [NSApp prefs_set_boolean:@PREFS_ROOTLESS value:XQuartzRootlessDefault];
    [NSApp prefs_synchronize];
}

- (IBAction) toggle_fullscreen:sender
{
    DarwinSendDDXEvent(kXquartzToggleFullscreen, 0);
}

- (void) set_can_quit:(OSX_BOOL)state
{
    can_quit = state;
}

- (IBAction)prefs_changed:sender
{
    if (!sender)
        return;

    if (sender == fake_buttons) {
        darwinFakeButtons = [fake_buttons intValue];
        [NSApp prefs_set_boolean:@PREFS_FAKEBUTTONS value:darwinFakeButtons];
    }
    else if (sender == enable_keyequivs) {
        XQuartzEnableKeyEquivalents = [enable_keyequivs intValue];
        [NSApp prefs_set_boolean:@PREFS_KEYEQUIVS value:
         XQuartzEnableKeyEquivalents];
    }
    else if (sender == sync_keymap) {
        darwinSyncKeymap = [sync_keymap intValue];
        [NSApp prefs_set_boolean:@PREFS_SYNC_KEYMAP value:darwinSyncKeymap];
    }
    else if (sender == enable_fullscreen_menu) {
        XQuartzFullscreenMenu = [enable_fullscreen_menu intValue];
        [NSApp prefs_set_boolean:@PREFS_FULLSCREEN_MENU value:
         XQuartzFullscreenMenu];
    }
    else if (sender == option_sends_alt) {
        BOOL prev_opt_sends_alt = XQuartzOptionSendsAlt;

        XQuartzOptionSendsAlt = [option_sends_alt intValue];
        [NSApp prefs_set_boolean:@PREFS_OPTION_SENDS_ALT value:
         XQuartzOptionSendsAlt];

        if (prev_opt_sends_alt != XQuartzOptionSendsAlt)
            QuartsResyncKeymap(TRUE);
    }
    else if (sender == click_through) {
        [NSApp prefs_set_boolean:@PREFS_CLICK_THROUGH value:[click_through
                                                             intValue]];
    }
    else if (sender == focus_follows_mouse) {
        [NSApp prefs_set_boolean:@PREFS_FFM value:[focus_follows_mouse
                                                   intValue]];
    }
    else if (sender == focus_on_new_window) {
        [NSApp prefs_set_boolean:@PREFS_FOCUS_ON_NEW_WINDOW value:[
             focus_on_new_window intValue]];
    }
    else if (sender == enable_auth) {
        [NSApp prefs_set_boolean:@PREFS_NO_AUTH value:![enable_auth intValue]
        ];
    }
    else if (sender == enable_tcp) {
        [NSApp prefs_set_boolean:@PREFS_NO_TCP value:![enable_tcp intValue]];
    }
    else if (sender == depth) {
        [NSApp prefs_set_integer:@PREFS_DEPTH value:[depth selectedTag]];
    }
    else if (sender == sync_pasteboard) {
        BOOL pbproxy_active = [sync_pasteboard intValue];
        [NSApp prefs_set_boolean:@PREFS_SYNC_PB value:pbproxy_active];

        [sync_pasteboard_to_clipboard setEnabled:pbproxy_active];
        [sync_pasteboard_to_primary setEnabled:pbproxy_active];
        [sync_clipboard_to_pasteboard setEnabled:pbproxy_active];
        [sync_primary_immediately setEnabled:pbproxy_active];

        // setEnabled doesn't do this...
        [sync_text1 setTextColor:pbproxy_active ?[NSColor controlTextColor] :
         [NSColor disabledControlTextColor]];
        [sync_text2 setTextColor:pbproxy_active ?[NSColor controlTextColor] :
         [NSColor disabledControlTextColor]];
    }
    else if (sender == sync_pasteboard_to_clipboard) {
        [NSApp prefs_set_boolean:@PREFS_SYNC_PB_TO_CLIPBOARD value:[
             sync_pasteboard_to_clipboard intValue]];
    }
    else if (sender == sync_pasteboard_to_primary) {
        [NSApp prefs_set_boolean:@PREFS_SYNC_PB_TO_PRIMARY value:[
             sync_pasteboard_to_primary intValue]];
    }
    else if (sender == sync_clipboard_to_pasteboard) {
        [NSApp prefs_set_boolean:@PREFS_SYNC_CLIPBOARD_TO_PB value:[
             sync_clipboard_to_pasteboard intValue]];
    }
    else if (sender == sync_primary_immediately) {
        [NSApp prefs_set_boolean:@PREFS_SYNC_PRIMARY_ON_SELECT value:[
             sync_primary_immediately intValue]];
    }
    else if (sender == scroll_in_device_direction) {
        XQuartzScrollInDeviceDirection =
            [scroll_in_device_direction intValue];
        [NSApp prefs_set_boolean:@PREFS_SCROLL_IN_DEV_DIRECTION value:
         XQuartzScrollInDeviceDirection];
    }

    [NSApp prefs_synchronize];

    DarwinSendDDXEvent(kXquartzReloadPreferences, 0);
}

- (IBAction) prefs_show:sender
{
    BOOL pbproxy_active =
        [NSApp prefs_get_boolean:@PREFS_SYNC_PB default:YES];

    // Remove preferences from the GUI which are not supported
    // TODO: Change 1117 to NSAppKitVersionNumber10_7 when it is defined
    if (scroll_in_device_direction && NSAppKitVersionNumber < 1117) {
        [scroll_in_device_direction removeFromSuperview];
        scroll_in_device_direction = nil;
    }
    else {
        [scroll_in_device_direction setIntValue:
         XQuartzScrollInDeviceDirection];
    }

    [fake_buttons setIntValue:darwinFakeButtons];
    [enable_keyequivs setIntValue:XQuartzEnableKeyEquivalents];
    [sync_keymap setIntValue:darwinSyncKeymap];
    [option_sends_alt setIntValue:XQuartzOptionSendsAlt];
    [click_through setIntValue:[NSApp prefs_get_boolean:@PREFS_CLICK_THROUGH
                                default:NO]];
    [focus_follows_mouse setIntValue:[NSApp prefs_get_boolean:@PREFS_FFM
                                      default:NO]];
    [focus_on_new_window setIntValue:[NSApp prefs_get_boolean:
                                      @PREFS_FOCUS_ON_NEW_WINDOW default:YES]
    ];

    [enable_auth setIntValue:![NSApp prefs_get_boolean:@PREFS_NO_AUTH default
                               :NO]];
    [enable_tcp setIntValue:![NSApp prefs_get_boolean:@PREFS_NO_TCP default:
                              NO]];

    [depth selectItemAtIndex:[depth indexOfItemWithTag:[NSApp
                                                        prefs_get_integer:
                                                        @PREFS_DEPTH default:
                                                        -1]]];

    [sync_pasteboard setIntValue:pbproxy_active];
    [sync_pasteboard_to_clipboard setIntValue:[NSApp prefs_get_boolean:
                                               @PREFS_SYNC_PB_TO_CLIPBOARD
                                               default:YES]];
    [sync_pasteboard_to_primary setIntValue:[NSApp prefs_get_boolean:
                                             @PREFS_SYNC_PB_TO_PRIMARY
                                             default:YES]];
    [sync_clipboard_to_pasteboard setIntValue:[NSApp prefs_get_boolean:
                                               @PREFS_SYNC_CLIPBOARD_TO_PB
                                               default:YES]];
    [sync_primary_immediately setIntValue:[NSApp prefs_get_boolean:
                                           @PREFS_SYNC_PRIMARY_ON_SELECT
                                           default:NO]];

    [sync_pasteboard_to_clipboard setEnabled:pbproxy_active];
    [sync_pasteboard_to_primary setEnabled:pbproxy_active];
    [sync_clipboard_to_pasteboard setEnabled:pbproxy_active];
    [sync_primary_immediately setEnabled:pbproxy_active];

    // setEnabled doesn't do this...
    [sync_text1 setTextColor:pbproxy_active ?[NSColor controlTextColor] : [
         NSColor disabledControlTextColor]];
    [sync_text2 setTextColor:pbproxy_active ?[NSColor controlTextColor] : [
         NSColor disabledControlTextColor]];

    [enable_fullscreen setIntValue:!XQuartzRootlessDefault];
    [enable_fullscreen_menu setIntValue:XQuartzFullscreenMenu];
    [enable_fullscreen_menu setEnabled:!XQuartzRootlessDefault];
    [enable_fullscreen_menu_text setTextColor:XQuartzRootlessDefault ?[
         NSColor disabledControlTextColor] : [NSColor controlTextColor]];

    [prefs_panel makeKeyAndOrderFront:sender];
}

- (IBAction) quit:sender
{
    DarwinSendDDXEvent(kXquartzQuit, 0);
}

- (IBAction) x11_help:sender
{
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
    AHLookupAnchor((CFStringRef)NSLocalizedString(@"Mac Help",
                                                  no comment),
                   CFSTR("mchlp2276"));
#else
    AHLookupAnchor(CFSTR("com.apple.machelp"), CFSTR("mchlp2276"));
#endif
}

- (OSX_BOOL) validateMenuItem:(NSMenuItem *)item
{
    NSMenu *menu = [item menu];

    if (item == toggle_fullscreen_item)
        return !XQuartzIsRootless;
    else if (menu == [X11App windowsMenu] || menu == dock_menu
             || (menu == [x11_about_item menu] && [item tag] == 42))
        return (AppleWMSelectedEvents() & AppleWMControllerNotifyMask) != 0;
    else
        return TRUE;
}

- (void) applicationDidHide:(NSNotification *)notify
{
    DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMHideAll);

    /* Toggle off fullscreen mode to leave our non-default video
     * mode and hide our guard window.
     */
    if (!XQuartzIsRootless && XQuartzFullscreenVisible) {
        DarwinSendDDXEvent(kXquartzToggleFullscreen, 0);
    }
}

- (void) applicationDidUnhide:(NSNotification *)notify
{
    DarwinSendDDXEvent(kXquartzControllerNotify, 1, AppleWMShowAll);
}

- (NSApplicationTerminateReply) applicationShouldTerminate:sender
{
    NSString *msg;
    NSString *title;

    if (can_quit ||
        [X11App prefs_get_boolean:@PREFS_NO_QUIT_ALERT default:NO])
        return NSTerminateNow;

    /* Make sure we're frontmost. */
    [NSApp activateIgnoringOtherApps:YES];

    title = NSLocalizedString(@"Do you really want to quit X11?",
                              @"Dialog title when quitting");
    msg = NSLocalizedString(
        @"Any open X11 applications will stop immediately, and you will lose any unsaved changes.",
        @"Dialog when quitting");

    /* FIXME: safe to run the alert in here? Or should we return Later
     *        and then run the alert on a timer? It seems to work here, so..
     */

    NSInteger result = NSRunAlertPanel(title, @"%@", NSLocalizedString(@"Quit", @""),
                                       NSLocalizedString(@"Cancel", @""), nil, msg);
    return (result == NSAlertDefaultReturn) ? NSTerminateNow : NSTerminateCancel;
}

- (void) applicationWillTerminate:(NSNotification *)aNotification _X_NORETURN
{
    [X11App prefs_synchronize];

    /* shutdown the X server, it will exit () for us. */
    DarwinSendDDXEvent(kXquartzQuit, 0);

    /* In case it doesn't, exit anyway after 5s. */
    [NSThread sleepForTimeInterval:5.0];

    exit(1);
}

- (void) server_ready
{
    x_list *node;

    finished_launching = YES;

    for (node = pending_apps; node != NULL; node = node->next) {
        NSString *filename = node->data;
        [self launch_client:filename];
        [filename release];
    }

    x_list_free(pending_apps);
    pending_apps = NULL;
}

- (OSX_BOOL) application:(NSApplication *)app openFile:(NSString *)filename
{
    const char *name = [filename UTF8String];

    if (finished_launching)
        [self launch_client:filename];
    else if (name[0] != ':')            /* ignore display names */
        pending_apps = x_list_prepend(pending_apps, [filename retain]);

    /* FIXME: report failures. */
    return YES;
}

@end

void
X11ControllerMain(int argc, char **argv, char **envp)
{
    X11ApplicationMain(argc, argv, envp);
}
