/* X11Application.h -- subclass of NSApplication to multiplex events
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

#ifndef X11APPLICATION_H
#define X11APPLICATION_H 1

#if __OBJC__

#import "X11Controller.h"

@interface X11Application : NSApplication {
    X11Controller *_controller;

    unsigned int _x_active : 1;
}

- (void)set_controller:controller;
- (void)set_window_menu:(NSArray *)list;

- (CFPropertyListRef)prefs_get_copy:(NSString *)key CF_RETURNS_RETAINED;
- (int)prefs_get_integer:(NSString *)key default:(int)def;
- (const char *)prefs_get_string:(NSString *)key default:(const char *)def;
- (float)prefs_get_float:(NSString *)key default:(float)def;
- (int)prefs_get_boolean:(NSString *)key default:(int)def;
- (NSURL *)prefs_copy_url:(NSString *)key default:(NSURL *)def
   NS_RETURNS_RETAINED;
- (NSArray *)prefs_get_array:(NSString *)key;
- (void)prefs_set_integer:(NSString *)key value:(int)value;
- (void)prefs_set_float:(NSString *)key value:(float)value;
- (void)prefs_set_boolean:(NSString *)key value:(int)value;
- (void)prefs_set_array:(NSString *)key value:(NSArray *)value;
- (void)prefs_set_string:(NSString *)key value:(NSString *)value;
- (void)prefs_synchronize;

- (X11Controller *)controller;
- (OSX_BOOL)x_active;
@end

extern X11Application * X11App;

#endif /* __OBJC__ */

void
X11ApplicationSetWindowMenu(int nitems, const char **items,
                            const char *shortcuts);
void
X11ApplicationSetWindowMenuCheck(int idx);
void
X11ApplicationSetFrontProcess(void);
void
X11ApplicationSetCanQuit(int state);
void
X11ApplicationServerReady(void);
void
X11ApplicationShowHideMenubar(int state);
void
X11ApplicationLaunchClient(const char *cmd);

Bool
X11ApplicationCanEnterRandR(void);

void
X11ApplicationFatalError(const char *f, va_list args) __printflike(1, 0);

void
X11ApplicationMain(int argc, char **argv, char **envp);

#define PREFS_APPSMENU                "apps_menu"
#define PREFS_FAKEBUTTONS             "enable_fake_buttons"
#define PREFS_KEYEQUIVS               "enable_key_equivalents"
#define PREFS_FULLSCREEN_HOTKEYS      "fullscreen_hotkeys"
#define PREFS_FULLSCREEN_MENU         "fullscreen_menu"
#define PREFS_SYNC_KEYMAP             "sync_keymap"
#define PREFS_DEPTH                   "depth"
#define PREFS_NO_AUTH                 "no_auth"
#define PREFS_NO_TCP                  "nolisten_tcp"
#define PREFS_DONE_XINIT_CHECK        "done_xinit_check"
#define PREFS_NO_QUIT_ALERT           "no_quit_alert"
#define PREFS_NO_RANDR_ALERT          "no_randr_alert"
#define PREFS_OPTION_SENDS_ALT        "option_sends_alt"
#define PREFS_FAKE_BUTTON2            "fake_button2"
#define PREFS_FAKE_BUTTON3            "fake_button3"
#define PREFS_APPKIT_MODIFIERS        "appkit_modifiers"
#define PREFS_WINDOW_ITEM_MODIFIERS   "window_item_modifiers"
#define PREFS_ROOTLESS                "rootless"
#define PREFS_RENDER_EXTENSION        "enable_render_extension"
#define PREFS_TEST_EXTENSIONS         "enable_test_extensions"
#define PREFS_XP_OPTIONS              "xp_options"
#define PREFS_LOGIN_SHELL             "login_shell"
#define PREFS_UPDATE_FEED             "update_feed"
#define PREFS_CLICK_THROUGH           "wm_click_through"
#define PREFS_FFM                     "wm_ffm"
#define PREFS_FOCUS_ON_NEW_WINDOW     "wm_focus_on_new_window"

#define PREFS_SCROLL_IN_DEV_DIRECTION "scroll_in_device_direction"
extern Bool XQuartzScrollInDeviceDirection;

#define PREFS_SYNC_PB                "sync_pasteboard"
#define PREFS_SYNC_PB_TO_CLIPBOARD   "sync_pasteboard_to_clipboard"
#define PREFS_SYNC_PB_TO_PRIMARY     "sync_pasteboard_to_primary"
#define PREFS_SYNC_CLIPBOARD_TO_PB   "sync_clipboard_to_pasteboard"
#define PREFS_SYNC_PRIMARY_ON_SELECT "sync_primary_on_select"

#endif /* X11APPLICATION_H */
