/* X11Application.m -- subclass of NSApplication to multiplex events
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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "quartzCommon.h"

#import "X11Application.h"

#include "darwin.h"
#include "quartz.h"
#include "darwinEvents.h"
#include "quartzKeyboard.h"
#include <X11/extensions/applewmconst.h>
#include "micmap.h"
#include "exglobals.h"

#include <mach/mach.h>
#include <unistd.h>
#include <AvailabilityMacros.h>

#include <pthread.h>

#include <Xplugin.h>

// pbproxy/pbproxy.h
extern int
xpbproxy_run(void);

#define DEFAULTS_FILE X11LIBDIR "/X11/xserver/Xquartz.plist"

#ifndef XSERVER_VERSION
#define XSERVER_VERSION "?"
#endif

#ifdef HAVE_LIBDISPATCH
#include <dispatch/dispatch.h>

static dispatch_queue_t eventTranslationQueue;
#endif

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef CF_RETURNS_RETAINED
#if __has_feature(attribute_cf_returns_retained)
#define CF_RETURNS_RETAINED __attribute__((cf_returns_retained))
#else
#define CF_RETURNS_RETAINED
#endif
#endif

extern Bool noTestExtensions;
extern Bool noRenderExtension;
extern BOOL serverRunning;

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
static TISInputSourceRef last_key_layout;
#else
static KeyboardLayoutRef last_key_layout;
#endif

/* This preference is only tested on Lion or later as it's not relevant to
 * earlier OS versions.
 */
Bool XQuartzScrollInDeviceDirection = FALSE;

extern int darwinFakeButtons;

/* Store the mouse location while in the background, and update X11's pointer
 * location when we become the foreground application
 */
static NSPoint bgMouseLocation;
static BOOL bgMouseLocationUpdated = FALSE;

X11Application *X11App;

CFStringRef app_prefs_domain_cfstr = NULL;

#define ALL_KEY_MASKS (NSShiftKeyMask | NSControlKeyMask | \
                       NSAlternateKeyMask | NSCommandKeyMask)

@interface X11Application (Private)
- (void) sendX11NSEvent:(NSEvent *)e;
@end

@implementation X11Application

typedef struct message_struct message;
struct message_struct {
    mach_msg_header_t hdr;
    SEL selector;
    NSObject *arg;
};

static mach_port_t _port;

/* Quartz mode initialization routine. This is often dynamically loaded
   but is statically linked into this X server. */
Bool
QuartzModeBundleInit(void);

static void
init_ports(void)
{
    kern_return_t r;
    NSPort *p;

    if (_port != MACH_PORT_NULL) return;

    r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &_port);
    if (r != KERN_SUCCESS) return;

    p = [NSMachPort portWithMachPort:_port];
    [p setDelegate:NSApp];
    [p scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:
     NSDefaultRunLoopMode];
}

static void
message_kit_thread(SEL selector, NSObject *arg)
{
    message msg;
    kern_return_t r;

    msg.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0);
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_remote_port = _port;
    msg.hdr.msgh_local_port = MACH_PORT_NULL;
    msg.hdr.msgh_reserved = 0;
    msg.hdr.msgh_id = 0;

    msg.selector = selector;
    msg.arg = [arg retain];

    r = mach_msg(&msg.hdr, MACH_SEND_MSG, msg.hdr.msgh_size,
                 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
    if (r != KERN_SUCCESS)
        ErrorF("%s: mach_msg failed: %x\n", __FUNCTION__, r);
}

- (void) handleMachMessage:(void *)_msg
{
    message *msg = _msg;

    [self performSelector:msg->selector withObject:msg->arg];
    [msg->arg release];
}

- (void) set_controller:obj
{
    if (_controller == nil) _controller = [obj retain];
}

- (void) dealloc
{
    if (_controller != nil) [_controller release];

    if (_port != MACH_PORT_NULL)
        mach_port_deallocate(mach_task_self(), _port);

    [super dealloc];
}

- (void) orderFrontStandardAboutPanel: (id) sender
{
    NSMutableDictionary *dict;
    NSDictionary *infoDict;
    NSString *tem;

    dict = [NSMutableDictionary dictionaryWithCapacity:3];
    infoDict = [[NSBundle mainBundle] infoDictionary];

    [dict setObject: NSLocalizedString(@"The X Window System", @"About panel")
             forKey:@"ApplicationName"];

    tem = [infoDict objectForKey:@"CFBundleShortVersionString"];

    [dict setObject:[NSString stringWithFormat:@"XQuartz %@", tem]
             forKey:@"ApplicationVersion"];

    [dict setObject:[NSString stringWithFormat:@"xorg-server %s",
                     XSERVER_VERSION]
     forKey:@"Version"];

    [self orderFrontStandardAboutPanelWithOptions: dict];
}

- (void) activateX:(OSX_BOOL)state
{
    if (_x_active == state)
        return;

    DEBUG_LOG("state=%d, _x_active=%d, \n", state, _x_active);
    if (state) {
        if (bgMouseLocationUpdated) {
            DarwinSendPointerEvents(darwinPointer, MotionNotify, 0,
                                    bgMouseLocation.x, bgMouseLocation.y,
                                    0.0, 0.0);
            bgMouseLocationUpdated = FALSE;
        }
        DarwinSendDDXEvent(kXquartzActivate, 0);
    }
    else {

        if (darwin_all_modifier_flags)
            DarwinUpdateModKeys(0);

        DarwinInputReleaseButtonsAndKeys(darwinKeyboard);
        DarwinInputReleaseButtonsAndKeys(darwinPointer);
        DarwinInputReleaseButtonsAndKeys(darwinTabletCursor);
        DarwinInputReleaseButtonsAndKeys(darwinTabletStylus);
        DarwinInputReleaseButtonsAndKeys(darwinTabletEraser);

        DarwinSendDDXEvent(kXquartzDeactivate, 0);
    }

    _x_active = state;
}

- (void) became_key:(NSWindow *)win
{
    [self activateX:NO];
}

- (void) sendEvent:(NSEvent *)e
{
    OSX_BOOL for_appkit, for_x;

    /* By default pass down the responder chain and to X. */
    for_appkit = YES;
    for_x = YES;

    switch ([e type]) {
    case NSLeftMouseDown:
    case NSRightMouseDown:
    case NSOtherMouseDown:
    case NSLeftMouseUp:
    case NSRightMouseUp:
    case NSOtherMouseUp:
        if ([e window] != nil) {
            /* Pointer event has an (AppKit) window. Probably something for the kit. */
            for_x = NO;
            if (_x_active) [self activateX:NO];
        }
        else if ([self modalWindow] == nil) {
            /* Must be an X window. Tell appkit it doesn't have focus. */
            for_appkit = NO;

            if ([self isActive]) {
                [self deactivate];
                if (!_x_active && quartzProcs->IsX11Window([e windowNumber]))
                    [self activateX:YES];
            }
        }

        /* We want to force sending to appkit if we're over the menu bar */
        if (!for_appkit) {
            NSPoint NSlocation = [e locationInWindow];
            NSWindow *window = [e window];
            NSRect NSframe, NSvisibleFrame;
            CGRect CGframe, CGvisibleFrame;
            CGPoint CGlocation;

            if (window != nil) {
                NSRect frame = [window frame];
                NSlocation.x += frame.origin.x;
                NSlocation.y += frame.origin.y;
            }

            NSframe = [[NSScreen mainScreen] frame];
            NSvisibleFrame = [[NSScreen mainScreen] visibleFrame];

            CGframe = CGRectMake(NSframe.origin.x, NSframe.origin.y,
                                 NSframe.size.width, NSframe.size.height);
            CGvisibleFrame = CGRectMake(NSvisibleFrame.origin.x,
                                        NSvisibleFrame.origin.y,
                                        NSvisibleFrame.size.width,
                                        NSvisibleFrame.size.height);
            CGlocation = CGPointMake(NSlocation.x, NSlocation.y);

            if (CGRectContainsPoint(CGframe, CGlocation) &&
                !CGRectContainsPoint(CGvisibleFrame, CGlocation))
                for_appkit = YES;
        }

        break;

    case NSKeyDown:
    case NSKeyUp:

        if (_x_active) {
            static BOOL do_swallow = NO;
            static int swallow_keycode;

            if ([e type] == NSKeyDown) {
                /* Before that though, see if there are any global
                 * shortcuts bound to it. */

                if (darwinAppKitModMask &[e modifierFlags]) {
                    /* Override to force sending to Appkit */
                    swallow_keycode = [e keyCode];
                    do_swallow = YES;
                    for_x = NO;
#if XPLUGIN_VERSION >= 1
                }
                else if (XQuartzEnableKeyEquivalents &&
                         xp_is_symbolic_hotkey_event([e eventRef])) {
                    swallow_keycode = [e keyCode];
                    do_swallow = YES;
                    for_x = NO;
#endif
                }
                else if (XQuartzEnableKeyEquivalents &&
                         [[self mainMenu] performKeyEquivalent:e]) {
                    swallow_keycode = [e keyCode];
                    do_swallow = YES;
                    for_appkit = NO;
                    for_x = NO;
                }
                else if (!XQuartzIsRootless
                         && ([e modifierFlags] & ALL_KEY_MASKS) ==
                         (NSCommandKeyMask | NSAlternateKeyMask)
                         && ([e keyCode] == 0 /*a*/ || [e keyCode] ==
                             53 /*Esc*/)) {
                    /* We have this here to force processing fullscreen
                     * toggle even if XQuartzEnableKeyEquivalents is disabled */
                    swallow_keycode = [e keyCode];
                    do_swallow = YES;
                    for_x = NO;
                    for_appkit = NO;
                    DarwinSendDDXEvent(kXquartzToggleFullscreen, 0);
                }
                else {
                    /* No kit window is focused, so send it to X. */
                    for_appkit = NO;
                }
            }
            else {       /* KeyUp */
                /* If we saw a key equivalent on the down, don't pass
                 * the up through to X. */
                if (do_swallow && [e keyCode] == swallow_keycode) {
                    do_swallow = NO;
                    for_x = NO;
                }
            }
        }
        else {       /* !_x_active */
            for_x = NO;
        }
        break;

    case NSFlagsChanged:
        /* Don't tell X11 about modifiers changing while it's not active */
        if (!_x_active)
            for_x = NO;
        break;

    case NSAppKitDefined:
        switch ([e subtype]) {
            static BOOL x_was_active = NO;

        case NSApplicationActivatedEventType:
            for_x = NO;
            if ([e window] == nil && x_was_active) {
                BOOL order_all_windows = YES, workspaces, ok;
                for_appkit = NO;

                /* FIXME: This is a hack to avoid passing the event to AppKit which
                 *        would result in it raising one of its windows.
                 */
                _appFlags._active = YES;

                [self set_front_process:nil];

                /* Get the Spaces preference for SwitchOnActivate */
                (void)CFPreferencesAppSynchronize(CFSTR("com.apple.dock"));
                workspaces =
                    CFPreferencesGetAppBooleanValue(CFSTR("workspaces"),
                                                    CFSTR(
                                                        "com.apple.dock"),
                                                    &ok);
                if (!ok)
                    workspaces = NO;

                if (workspaces) {
                    (void)CFPreferencesAppSynchronize(CFSTR(
                                                          ".GlobalPreferences"));
                    order_all_windows =
                        CFPreferencesGetAppBooleanValue(CFSTR(
                                                            "AppleSpacesSwitchOnActivate"),
                                                        CFSTR(
                                                            ".GlobalPreferences"),
                                                        &ok);
                    if (!ok)
                        order_all_windows = YES;
                }

                /* TODO: In the workspaces && !AppleSpacesSwitchOnActivate case, the windows are ordered
                 *       correctly, but we need to activate the top window on this space if there is
                 *       none active.
                 *
                 *       If there are no active windows, and there are minimized windows, we should
                 *       be restoring one of them.
                 */
                if ([e data2] & 0x10) {         // 0x10 (bfCPSOrderAllWindowsForward) is set when we use cmd-tab or the dock icon
                    DarwinSendDDXEvent(kXquartzBringAllToFront, 1,
                                       order_all_windows);
                }
            }
            break;

        case 18:         /* ApplicationDidReactivate */
            if (XQuartzFullscreenVisible) for_appkit = NO;
            break;

        case NSApplicationDeactivatedEventType:
            for_x = NO;

            x_was_active = _x_active;
            if (_x_active)
                [self activateX:NO];
            break;
        }
        break;

    default:
        break;          /* for gcc */
    }

    if (for_appkit) [super sendEvent:e];

    if (for_x) {
#ifdef HAVE_LIBDISPATCH
        dispatch_async(eventTranslationQueue, ^{
                           [self sendX11NSEvent:e];
                       });
#else
        [self sendX11NSEvent:e];
#endif
    }
}

- (void) set_window_menu:(NSArray *)list
{
    [_controller set_window_menu:list];
}

- (void) set_window_menu_check:(NSNumber *)n
{
    [_controller set_window_menu_check:n];
}

- (void) set_apps_menu:(NSArray *)list
{
    [_controller set_apps_menu:list];
}

- (void) set_front_process:unused
{
    [NSApp activateIgnoringOtherApps:YES];

    if ([self modalWindow] == nil)
        [self activateX:YES];
}

- (void) set_can_quit:(NSNumber *)state
{
    [_controller set_can_quit:[state boolValue]];
}

- (void) server_ready:unused
{
    [_controller server_ready];
}

- (void) show_hide_menubar:(NSNumber *)state
{
    /* Also shows/hides the dock */
    if ([state boolValue])
        SetSystemUIMode(kUIModeNormal, 0);
    else
        SetSystemUIMode(kUIModeAllHidden,
                        XQuartzFullscreenMenu ? kUIOptionAutoShowMenuBar : 0);                   // kUIModeAllSuppressed or kUIOptionAutoShowMenuBar can be used to allow "mouse-activation"
}

- (void) launch_client:(NSString *)cmd
{
    (void)[_controller application:self openFile:cmd];
}

/* user preferences */

/* Note that these functions only work for arrays whose elements
   can be toll-free-bridged between NS and CF worlds. */

static const void *
cfretain(CFAllocatorRef a, const void *b)
{
    return CFRetain(b);
}

static void
cfrelease(CFAllocatorRef a, const void *b)
{
    CFRelease(b);
}

CF_RETURNS_RETAINED
static CFMutableArrayRef
nsarray_to_cfarray(NSArray *in)
{
    CFMutableArrayRef out;
    CFArrayCallBacks cb;
    NSObject *ns;
    const CFTypeRef *cf;
    int i, count;

    memset(&cb, 0, sizeof(cb));
    cb.version = 0;
    cb.retain = cfretain;
    cb.release = cfrelease;

    count = [in count];
    out = CFArrayCreateMutable(NULL, count, &cb);

    for (i = 0; i < count; i++) {
        ns = [in objectAtIndex:i];

        if ([ns isKindOfClass:[NSArray class]])
            cf = (CFTypeRef)nsarray_to_cfarray((NSArray *)ns);
        else
            cf = CFRetain((CFTypeRef)ns);

        CFArrayAppendValue(out, cf);
        CFRelease(cf);
    }

    return out;
}

static NSMutableArray *
cfarray_to_nsarray(CFArrayRef in)
{
    NSMutableArray *out;
    const CFTypeRef *cf;
    NSObject *ns;
    int i, count;

    count = CFArrayGetCount(in);
    out = [[NSMutableArray alloc] initWithCapacity:count];

    for (i = 0; i < count; i++) {
        cf = CFArrayGetValueAtIndex(in, i);

        if (CFGetTypeID(cf) == CFArrayGetTypeID())
            ns = cfarray_to_nsarray((CFArrayRef)cf);
        else
            ns = [(id) cf retain];

        [out addObject:ns];
        [ns release];
    }

    return out;
}

- (CFPropertyListRef) prefs_get_copy:(NSString *)key
{
    CFPropertyListRef value;

    value = CFPreferencesCopyAppValue((CFStringRef)key,
                                      app_prefs_domain_cfstr);

    if (value == NULL) {
        static CFDictionaryRef defaults;

        if (defaults == NULL) {
            CFStringRef error = NULL;
            CFDataRef data;
            CFURLRef url;
            SInt32 error_code;

            url = (CFURLCreateFromFileSystemRepresentation
                       (NULL, (unsigned char *)DEFAULTS_FILE,
                       strlen(DEFAULTS_FILE), false));
            if (CFURLCreateDataAndPropertiesFromResource(NULL, url, &data,
                                                         NULL, NULL,
                                                         &error_code)) {
                defaults = (CFPropertyListCreateFromXMLData
                                (NULL, data,
                                kCFPropertyListMutableContainersAndLeaves,
                                &error));
                if (error != NULL) CFRelease(error);
                CFRelease(data);
            }
            CFRelease(url);

            if (defaults != NULL) {
                NSMutableArray *apps, *elt;
                int count, i;
                NSString *name, *nname;

                /* Localize the names in the default apps menu. */

                apps =
                    [(NSDictionary *) defaults objectForKey:@PREFS_APPSMENU];
                if (apps != nil) {
                    count = [apps count];
                    for (i = 0; i < count; i++) {
                        elt = [apps objectAtIndex:i];
                        if (elt != nil &&
                            [elt isKindOfClass:[NSArray class]]) {
                            name = [elt objectAtIndex:0];
                            if (name != nil) {
                                nname = NSLocalizedString(name, nil);
                                if (nname != nil && nname != name)
                                    [elt replaceObjectAtIndex:0 withObject:
                                     nname];
                            }
                        }
                    }
                }
            }
        }

        if (defaults != NULL) value = CFDictionaryGetValue(defaults, key);
        if (value != NULL) CFRetain(value);
    }

    return value;
}

- (int) prefs_get_integer:(NSString *)key default:(int)def
{
    CFPropertyListRef value;
    int ret;

    value = [self prefs_get_copy:key];

    if (value != NULL && CFGetTypeID(value) == CFNumberGetTypeID())
        CFNumberGetValue(value, kCFNumberIntType, &ret);
    else if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID())
        ret = CFStringGetIntValue(value);
    else
        ret = def;

    if (value != NULL) CFRelease(value);

    return ret;
}

- (const char *) prefs_get_string:(NSString *)key default:(const char *)def
{
    CFPropertyListRef value;
    const char *ret = NULL;

    value = [self prefs_get_copy:key];

    if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
        NSString *s = (NSString *)value;

        ret = [s UTF8String];
    }

    if (value != NULL) CFRelease(value);

    return ret != NULL ? ret : def;
}

- (NSURL *) prefs_copy_url:(NSString *)key default:(NSURL *)def
{
    CFPropertyListRef value;
    NSURL *ret = NULL;

    value = [self prefs_get_copy:key];

    if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID()) {
        NSString *s = (NSString *)value;

        ret = [NSURL URLWithString:s];
        [ret retain];
    }

    if (value != NULL) CFRelease(value);

    return ret != NULL ? ret : def;
}

- (float) prefs_get_float:(NSString *)key default:(float)def
{
    CFPropertyListRef value;
    float ret = def;

    value = [self prefs_get_copy:key];

    if (value != NULL
        && CFGetTypeID(value) == CFNumberGetTypeID()
        && CFNumberIsFloatType(value))
        CFNumberGetValue(value, kCFNumberFloatType, &ret);
    else if (value != NULL && CFGetTypeID(value) == CFStringGetTypeID())
        ret = CFStringGetDoubleValue(value);

    if (value != NULL) CFRelease(value);

    return ret;
}

- (int) prefs_get_boolean:(NSString *)key default:(int)def
{
    CFPropertyListRef value;
    int ret = def;

    value = [self prefs_get_copy:key];

    if (value != NULL) {
        if (CFGetTypeID(value) == CFNumberGetTypeID())
            CFNumberGetValue(value, kCFNumberIntType, &ret);
        else if (CFGetTypeID(value) == CFBooleanGetTypeID())
            ret = CFBooleanGetValue(value);
        else if (CFGetTypeID(value) == CFStringGetTypeID()) {
            const char *tem = [(NSString *) value UTF8String];
            if (strcasecmp(tem, "true") == 0 || strcasecmp(tem, "yes") == 0)
                ret = YES;
            else
                ret = NO;
        }

        CFRelease(value);
    }
    return ret;
}

- (NSArray *) prefs_get_array:(NSString *)key
{
    NSArray *ret = nil;
    CFPropertyListRef value;

    value = [self prefs_get_copy:key];

    if (value != NULL) {
        if (CFGetTypeID(value) == CFArrayGetTypeID())
            ret = [cfarray_to_nsarray (value)autorelease];

        CFRelease(value);
    }

    return ret;
}

- (void) prefs_set_integer:(NSString *)key value:(int)value
{
    CFNumberRef x;

    x = CFNumberCreate(NULL, kCFNumberIntType, &value);

    CFPreferencesSetValue((CFStringRef)key, (CFTypeRef)x,
                          app_prefs_domain_cfstr,
                          kCFPreferencesCurrentUser,
                          kCFPreferencesAnyHost);

    CFRelease(x);
}

- (void) prefs_set_float:(NSString *)key value:(float)value
{
    CFNumberRef x;

    x = CFNumberCreate(NULL, kCFNumberFloatType, &value);

    CFPreferencesSetValue((CFStringRef)key, (CFTypeRef)x,
                          app_prefs_domain_cfstr,
                          kCFPreferencesCurrentUser,
                          kCFPreferencesAnyHost);

    CFRelease(x);
}

- (void) prefs_set_boolean:(NSString *)key value:(int)value
{
    CFPreferencesSetValue(
        (CFStringRef)key,
        (CFTypeRef)(value ? kCFBooleanTrue
                    : kCFBooleanFalse),
        app_prefs_domain_cfstr,
        kCFPreferencesCurrentUser, kCFPreferencesAnyHost);

}

- (void) prefs_set_array:(NSString *)key value:(NSArray *)value
{
    CFArrayRef cfarray;

    cfarray = nsarray_to_cfarray(value);
    CFPreferencesSetValue((CFStringRef)key,
                          (CFTypeRef)cfarray,
                          app_prefs_domain_cfstr,
                          kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    CFRelease(cfarray);
}

- (void) prefs_set_string:(NSString *)key value:(NSString *)value
{
    CFPreferencesSetValue((CFStringRef)key, (CFTypeRef)value,
                          app_prefs_domain_cfstr, kCFPreferencesCurrentUser,
                          kCFPreferencesAnyHost);
}

- (void) prefs_synchronize
{
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
}

- (void) read_defaults
{
    NSString *nsstr;
    const char *tem;

    XQuartzRootlessDefault = [self prefs_get_boolean:@PREFS_ROOTLESS
                              default               :XQuartzRootlessDefault];
    XQuartzFullscreenMenu = [self prefs_get_boolean:@PREFS_FULLSCREEN_MENU
                             default               :XQuartzFullscreenMenu];
    XQuartzFullscreenDisableHotkeys =
        ![self prefs_get_boolean:@PREFS_FULLSCREEN_HOTKEYS
          default               :!
          XQuartzFullscreenDisableHotkeys];
    darwinFakeButtons = [self prefs_get_boolean:@PREFS_FAKEBUTTONS
                         default               :darwinFakeButtons];
    XQuartzOptionSendsAlt = [self prefs_get_boolean:@PREFS_OPTION_SENDS_ALT
                             default               :XQuartzOptionSendsAlt];

    if (darwinFakeButtons) {
        const char *fake2, *fake3;

        fake2 = [self prefs_get_string:@PREFS_FAKE_BUTTON2 default:NULL];
        fake3 = [self prefs_get_string:@PREFS_FAKE_BUTTON3 default:NULL];

        if (fake2 != NULL) darwinFakeMouse2Mask = DarwinParseModifierList(
                fake2, TRUE);
        if (fake3 != NULL) darwinFakeMouse3Mask = DarwinParseModifierList(
                fake3, TRUE);
    }

    tem = [self prefs_get_string:@PREFS_APPKIT_MODIFIERS default:NULL];
    if (tem != NULL) darwinAppKitModMask = DarwinParseModifierList(tem, TRUE);

    tem = [self prefs_get_string:@PREFS_WINDOW_ITEM_MODIFIERS default:NULL];
    if (tem != NULL) {
        windowItemModMask = DarwinParseModifierList(tem, FALSE);
    }
    else {
        nsstr = NSLocalizedString(@"window item modifiers",
                                  @"window item modifiers");
        if (nsstr != NULL) {
            tem = [nsstr UTF8String];
            if ((tem != NULL) && strcmp(tem, "window item modifiers")) {
                windowItemModMask = DarwinParseModifierList(tem, FALSE);
            }
        }
    }

    XQuartzEnableKeyEquivalents = [self prefs_get_boolean:@PREFS_KEYEQUIVS
                                   default               :
                                   XQuartzEnableKeyEquivalents];

    darwinSyncKeymap = [self prefs_get_boolean:@PREFS_SYNC_KEYMAP
                        default               :darwinSyncKeymap];

    darwinDesiredDepth = [self prefs_get_integer:@PREFS_DEPTH
                          default               :darwinDesiredDepth];

    noTestExtensions = ![self prefs_get_boolean:@PREFS_TEST_EXTENSIONS
                         default               :FALSE];

    noRenderExtension = ![self prefs_get_boolean:@PREFS_RENDER_EXTENSION
                          default               :TRUE];

    XQuartzScrollInDeviceDirection =
        [self prefs_get_boolean:@PREFS_SCROLL_IN_DEV_DIRECTION
         default               :
         XQuartzScrollInDeviceDirection];

#if XQUARTZ_SPARKLE
    NSURL *url = [self prefs_copy_url:@PREFS_UPDATE_FEED default:nil];
    if (url) {
        [[SUUpdater sharedUpdater] setFeedURL:url];
        [url release];
    }
#endif
}

/* This will end up at the end of the responder chain. */
- (void) copy:sender
{
    DarwinSendDDXEvent(kXquartzPasteboardNotify, 1,
                       AppleWMCopyToPasteboard);
}

- (X11Controller *) controller
{
    return _controller;
}

- (OSX_BOOL) x_active
{
    return _x_active;
}

@end

static NSArray *
array_with_strings_and_numbers(int nitems, const char **items,
                               const char *numbers)
{
    NSMutableArray *array, *subarray;
    NSString *string, *number;
    int i;

    /* (Can't autorelease on the X server thread) */

    array = [[NSMutableArray alloc] initWithCapacity:nitems];

    for (i = 0; i < nitems; i++) {
        subarray = [[NSMutableArray alloc] initWithCapacity:2];

        string = [[NSString alloc] initWithUTF8String:items[i]];
        [subarray addObject:string];
        [string release];

        if (numbers[i] != 0) {
            number = [[NSString alloc] initWithFormat:@"%d", numbers[i]];
            [subarray addObject:number];
            [number release];
        }
        else
            [subarray addObject:@""];

        [array addObject:subarray];
        [subarray release];
    }

    return array;
}

void
X11ApplicationSetWindowMenu(int nitems, const char **items,
                            const char *shortcuts)
{
    NSArray *array;
    array = array_with_strings_and_numbers(nitems, items, shortcuts);

    /* Send the array of strings over to the appkit thread */

    message_kit_thread(@selector (set_window_menu:), array);
    [array release];
}

void
X11ApplicationSetWindowMenuCheck(int idx)
{
    NSNumber *n;

    n = [[NSNumber alloc] initWithInt:idx];

    message_kit_thread(@selector (set_window_menu_check:), n);

    [n release];
}

void
X11ApplicationSetFrontProcess(void)
{
    message_kit_thread(@selector (set_front_process:), nil);
}

void
X11ApplicationSetCanQuit(int state)
{
    NSNumber *n;

    n = [[NSNumber alloc] initWithBool:state];

    message_kit_thread(@selector (set_can_quit:), n);

    [n release];
}

void
X11ApplicationServerReady(void)
{
    message_kit_thread(@selector (server_ready:), nil);
}

void
X11ApplicationShowHideMenubar(int state)
{
    NSNumber *n;

    n = [[NSNumber alloc] initWithBool:state];

    message_kit_thread(@selector (show_hide_menubar:), n);

    [n release];
}

void
X11ApplicationLaunchClient(const char *cmd)
{
    NSString *string;

    string = [[NSString alloc] initWithUTF8String:cmd];

    message_kit_thread(@selector (launch_client:), string);

    [string release];
}

/* This is a special function in that it is run from the *SERVER* thread and
 * not the AppKit thread.  We want to block entering a screen-capturing RandR
 * mode until we notify the user about how to get out if the X11 client crashes.
 */
Bool
X11ApplicationCanEnterRandR(void)
{
    NSString *title, *msg;

    if ([X11App prefs_get_boolean:@PREFS_NO_RANDR_ALERT default:NO] ||
        XQuartzShieldingWindowLevel != 0)
        return TRUE;

    title = NSLocalizedString(@"Enter RandR mode?",
                              @"Dialog title when switching to RandR");
    msg = NSLocalizedString(
        @"An application has requested X11 to change the resolution of your display.  X11 will restore the display to its previous state when the requesting application requests to return to the previous state.  Alternatively, you can use the ⌥⌘A key sequence to force X11 to return to the previous state.",
        @"Dialog when switching to RandR");

    if (!XQuartzIsRootless)
        QuartzShowFullscreen(FALSE);

    switch (NSRunAlertPanel(title, @"%@",
                            NSLocalizedString(@"Allow",
                                              @""),
                            NSLocalizedString(@"Cancel",
                                              @""),
                            NSLocalizedString(@"Always Allow", @""), msg)) {
    case NSAlertOtherReturn:
        [X11App prefs_set_boolean:@PREFS_NO_RANDR_ALERT value:YES];
        [X11App prefs_synchronize];

    case NSAlertDefaultReturn:
        return YES;

    default:
        return NO;
    }
}

void
X11ApplicationFatalError(const char *f, va_list args)
{
#ifdef HAVE_LIBDISPATCH
    NSString *title, *msg;
    char *error_msg;

    /* This is called by FatalError() in the server thread just before
     * we would abort.  If the server never got off the ground, We should
     * inform the user of the error rather than letting the ever-so-friendly
     * CrashReporter do it for us.
     *
     * This also has the benefit of forcing user interaction rather than
     * allowing an infinite throttled-restart if the crash occurs before
     * we can drain the launchd socket.
     */

    if (serverRunning) {
        return;
    }

    title = NSLocalizedString(@"The application X11 could not be opened.",
                              @"Dialog title when encountering a fatal error");
    msg = NSLocalizedString(
        @"An error occurred while starting the X11 server: \"%s\"\n\nClick Quit to quit X11. Click Report to see more details or send a report to Apple.",
        @"Dialog when encountering a fatal error");

    vasprintf(&error_msg, f, args);
    msg = [NSString stringWithFormat:msg, error_msg];

    /* We want the AppKit thread to actually service the alert or we will race [NSApp run] and create an
     * 'NSInternalInconsistencyException', reason: 'NSApp with wrong _running count'
     */
    dispatch_sync(dispatch_get_main_queue(), ^{
                      if (NSAlertDefaultReturn ==
                          NSRunAlertPanel (title, @"%@",
                                           NSLocalizedString (@"Quit", @""),
                                           NSLocalizedString (@"Report...", @""),
                                           nil, msg)) {
                          exit (EXIT_FAILURE);
                      }
                  });

    /* fall back to caller to do the abort() in the DIX */
#endif
}

static void
check_xinitrc(void)
{
    char *tem, buf[1024];
    NSString *msg;

    if ([X11App prefs_get_boolean:@PREFS_DONE_XINIT_CHECK default:NO])
        return;

    tem = getenv("HOME");
    if (tem == NULL) goto done;

    snprintf(buf, sizeof(buf), "%s/.xinitrc", tem);
    if (access(buf, F_OK) != 0)
        goto done;

    msg =
        NSLocalizedString(
            @"You have an existing ~/.xinitrc file.\n\n\
                             Windows displayed by X11 applications may not have titlebars, or may look \
                             different to windows displayed by native applications.\n\n\
                             Would you like to move aside the existing file and use the standard X11 \
                             environment the next time you start X11?"                                                                                                                                                                                                                                                                                                                                                                  ,
            @"Startup xinitrc dialog");

    if (NSAlertDefaultReturn ==
        NSRunAlertPanel(nil, @"%@", NSLocalizedString(@"Yes", @""),
                        NSLocalizedString(@"No", @""), nil, msg)) {
        char buf2[1024];
        int i = -1;

        snprintf(buf2, sizeof(buf2), "%s.old", buf);

        for (i = 1; access(buf2, F_OK) == 0; i++)
            snprintf(buf2, sizeof(buf2), "%s.old.%d", buf, i);

        rename(buf, buf2);
    }

done:
    [X11App prefs_set_boolean:@PREFS_DONE_XINIT_CHECK value:YES];
    [X11App prefs_synchronize];
}

static inline pthread_t
create_thread(void *(*func)(void *), void *arg)
{
    pthread_attr_t attr;
    pthread_t tid;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, func, arg);
    pthread_attr_destroy(&attr);

    return tid;
}

static void *
xpbproxy_x_thread(void *args)
{
    xpbproxy_run();

    ErrorF("xpbproxy thread is terminating unexpectedly.\n");
    return NULL;
}

void
X11ApplicationMain(int argc, char **argv, char **envp)
{
    NSAutoreleasePool *pool;

#ifdef DEBUG
    while (access("/tmp/x11-block", F_OK) == 0) sleep(1);
#endif

    pool = [[NSAutoreleasePool alloc] init];
    X11App = (X11Application *)[X11Application sharedApplication];
    init_ports();

    app_prefs_domain_cfstr =
        (CFStringRef)[[NSBundle mainBundle] bundleIdentifier];

    if (app_prefs_domain_cfstr == NULL) {
        ErrorF(
            "X11ApplicationMain: Unable to determine bundle identifier.  Your installation of XQuartz may be broken.\n");
        app_prefs_domain_cfstr = CFSTR(BUNDLE_ID_PREFIX ".X11");
    }

    [NSApp read_defaults];
    [NSBundle loadNibNamed:@"main" owner:NSApp];
    [[NSNotificationCenter defaultCenter] addObserver:NSApp
                                             selector:@selector (became_key:)
                                                 name:
     NSWindowDidBecomeKeyNotification object:nil];

    /*
     * The xpr Quartz mode is statically linked into this server.
     * Initialize all the Quartz functions.
     */
    QuartzModeBundleInit();

    /* Calculate the height of the menubar so we can avoid it. */
    aquaMenuBarHeight = [[NSApp mainMenu] menuBarHeight];
#if ! __LP64__
    if (!aquaMenuBarHeight) {
        aquaMenuBarHeight = [NSMenuView menuBarHeight];
    }
#endif
    if (!aquaMenuBarHeight) {
        NSScreen* primaryScreen = [[NSScreen screens] objectAtIndex:0];
        aquaMenuBarHeight = NSHeight([primaryScreen frame]) - NSMaxY([primaryScreen visibleFrame]);
    }

#ifdef HAVE_LIBDISPATCH
    eventTranslationQueue = dispatch_queue_create(
        BUNDLE_ID_PREFIX ".X11.NSEventsToX11EventsQueue", NULL);
    assert(eventTranslationQueue != NULL);
#endif

    /* Set the key layout seed before we start the server */
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
    last_key_layout = TISCopyCurrentKeyboardLayoutInputSource();

    if (!last_key_layout)
        ErrorF(
            "X11ApplicationMain: Unable to determine TISCopyCurrentKeyboardLayoutInputSource() at startup.\n");
#else
    KLGetCurrentKeyboardLayout(&last_key_layout);
    if (!last_key_layout)
        ErrorF(
            "X11ApplicationMain: Unable to determine KLGetCurrentKeyboardLayout() at startup.\n");
#endif

    if (!QuartsResyncKeymap(FALSE)) {
        ErrorF("X11ApplicationMain: Could not build a valid keymap.\n");
    }

    /* Tell the server thread that it can proceed */
    QuartzInitServer(argc, argv, envp);

    /* This must be done after QuartzInitServer because it can result in
     * an mieqEnqueue() - <rdar://problem/6300249>
     */
    check_xinitrc();

    create_thread(xpbproxy_x_thread, NULL);

#if XQUARTZ_SPARKLE
    [[X11App controller] setup_sparkle];
    [[SUUpdater sharedUpdater] resetUpdateCycle];
    //    [[SUUpdater sharedUpdater] checkForUpdates:X11App];
#endif

    [pool release];
    [NSApp run];
    /* not reached */
}

@implementation X11Application (Private)

#ifdef NX_DEVICELCMDKEYMASK
/* This is to workaround a bug in the VNC server where we sometimes see the L
 * modifier and sometimes see no "side"
 */
static inline int
ensure_flag(int flags, int device_independent, int device_dependents,
            int device_dependent_default)
{
    if ((flags & device_independent) &&
        !(flags & device_dependents))
        flags |= device_dependent_default;
    return flags;
}
#endif

#ifdef DEBUG_UNTRUSTED_POINTER_DELTA
static const char *
untrusted_str(NSEvent *e)
{
    switch ([e type]) {
    case NSScrollWheel:
        return "NSScrollWheel";

    case NSTabletPoint:
        return "NSTabletPoint";

    case NSOtherMouseDown:
        return "NSOtherMouseDown";

    case NSOtherMouseUp:
        return "NSOtherMouseUp";

    case NSLeftMouseDown:
        return "NSLeftMouseDown";

    case NSLeftMouseUp:
        return "NSLeftMouseUp";

    default:
        switch ([e subtype]) {
        case NSTabletPointEventSubtype:
            return "NSTabletPointEventSubtype";

        case NSTabletProximityEventSubtype:
            return "NSTabletProximityEventSubtype";

        default:
            return "Other";
        }
    }
}
#endif

extern void
darwinEvents_lock(void);
extern void
darwinEvents_unlock(void);

- (void) sendX11NSEvent:(NSEvent *)e
{
    NSPoint location = NSZeroPoint;
    int ev_button, ev_type;
    static float pressure = 0.0;       // static so ProximityOut will have the value from the previous tablet event
    static NSPoint tilt;               // static so ProximityOut will have the value from the previous tablet event
    static DeviceIntPtr darwinTabletCurrent = NULL;
    static BOOL needsProximityIn = NO; // Do we do need to handle a pending ProximityIn once we have pressure/tilt?
    DeviceIntPtr pDev;
    int modifierFlags;
    BOOL isMouseOrTabletEvent, isTabletEvent;

    if (!darwinTabletCurrent) {
        /* Ensure that the event system is initialized */
        darwinEvents_lock();
        darwinEvents_unlock();
        assert(darwinTabletStylus);

        tilt = NSZeroPoint;
        darwinTabletCurrent = darwinTabletStylus;
    }

    isMouseOrTabletEvent = [e type] == NSLeftMouseDown ||
                           [e type] == NSOtherMouseDown ||
                           [e type] == NSRightMouseDown ||
                           [e type] == NSLeftMouseUp ||
                           [e type] == NSOtherMouseUp ||
                           [e type] == NSRightMouseUp ||
                           [e type] == NSLeftMouseDragged ||
                           [e type] == NSOtherMouseDragged ||
                           [e type] == NSRightMouseDragged ||
                           [e type] == NSMouseMoved ||
                           [e type] == NSTabletPoint || 
                           [e type] == NSScrollWheel;

    isTabletEvent = ([e type] == NSTabletPoint) ||
                    (isMouseOrTabletEvent &&
                     ([e subtype] == NSTabletPointEventSubtype ||
                      [e subtype] == NSTabletProximityEventSubtype));

    if (isMouseOrTabletEvent) {
        static NSPoint lastpt;
        NSWindow *window = [e window];
        NSRect screen = [[[NSScreen screens] objectAtIndex:0] frame];
        BOOL hasUntrustedPointerDelta;

        // NSEvents for tablets are not consistent wrt deltaXY between events, so we cannot rely on that
        // Thus tablets will be subject to the warp-pointer bug worked around by the delta, but tablets
        // are not normally used in cases where that bug would present itself, so this is a fair tradeoff
        // <rdar://problem/7111003> deltaX and deltaY are incorrect for NSMouseMoved, NSTabletPointEventSubtype
        // http://xquartz.macosforge.org/trac/ticket/288
        hasUntrustedPointerDelta = isTabletEvent;

        // The deltaXY for middle click events also appear erroneous after fast user switching
        // <rdar://problem/7979468> deltaX and deltaY are incorrect for NSOtherMouseDown and NSOtherMouseUp after FUS
        // http://xquartz.macosforge.org/trac/ticket/389
        hasUntrustedPointerDelta |= [e type] == NSOtherMouseDown ||
                                    [e type] == NSOtherMouseUp;

        // The deltaXY for scroll events correspond to the scroll delta, not the pointer delta
        // <rdar://problem/7989690> deltaXY for wheel events are being sent as mouse movement
        hasUntrustedPointerDelta |= [e type] == NSScrollWheel;

#ifdef DEBUG_UNTRUSTED_POINTER_DELTA
        hasUntrustedPointerDelta |= [e type] == NSLeftMouseDown ||
                                    [e type] == NSLeftMouseUp;
#endif

        if (window != nil) {
            NSRect frame = [window frame];
            location = [e locationInWindow];
            location.x += frame.origin.x;
            location.y += frame.origin.y;
            lastpt = location;
        }
        else if (hasUntrustedPointerDelta) {
#ifdef DEBUG_UNTRUSTED_POINTER_DELTA
            ErrorF("--- Begin Event Debug ---\n");
            ErrorF("Event type: %s\n", untrusted_str(e));
            ErrorF("old lastpt: (%0.2f, %0.2f)\n", lastpt.x, lastpt.y);
            ErrorF("     delta: (%0.2f, %0.2f)\n", [e deltaX], -[e deltaY]);
            ErrorF("  location: (%0.2f, %0.2f)\n", lastpt.x + [e deltaX],
                   lastpt.y - [e deltaY]);
            ErrorF("workaround: (%0.2f, %0.2f)\n", [e locationInWindow].x,
                   [e locationInWindow].y);
            ErrorF("--- End Event Debug ---\n");

            location.x = lastpt.x + [e deltaX];
            location.y = lastpt.y - [e deltaY];
            lastpt = [e locationInWindow];
#else
            location = [e locationInWindow];
            lastpt = location;
#endif
        }
        else {
            location.x = lastpt.x + [e deltaX];
            location.y = lastpt.y - [e deltaY];
            lastpt = [e locationInWindow];
        }

        /* Convert coordinate system */
        location.y = (screen.origin.y + screen.size.height) - location.y;
    }

    modifierFlags = [e modifierFlags];

#ifdef NX_DEVICELCMDKEYMASK
    /* This is to workaround a bug in the VNC server where we sometimes see the L
     * modifier and sometimes see no "side"
     */
    modifierFlags = ensure_flag(modifierFlags, NX_CONTROLMASK,
                                NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK,
                                NX_DEVICELCTLKEYMASK);
    modifierFlags = ensure_flag(modifierFlags, NX_SHIFTMASK,
                                NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK, 
                                NX_DEVICELSHIFTKEYMASK);
    modifierFlags = ensure_flag(modifierFlags, NX_COMMANDMASK,
                                NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK,
                                NX_DEVICELCMDKEYMASK);
    modifierFlags = ensure_flag(modifierFlags, NX_ALTERNATEMASK,
                                NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK,
                                NX_DEVICELALTKEYMASK);
#endif

    modifierFlags &= darwin_all_modifier_mask;

    /* We don't receive modifier key events while out of focus, and 3button
     * emulation mucks this up, so we need to check our modifier flag state
     * on every event... ugg
     */

    if (darwin_all_modifier_flags != modifierFlags)
        DarwinUpdateModKeys(modifierFlags);

    switch ([e type]) {
    case NSLeftMouseDown:
        ev_button = 1;
        ev_type = ButtonPress;
        goto handle_mouse;

    case NSOtherMouseDown:
        ev_button = 2;
        ev_type = ButtonPress;
        goto handle_mouse;

    case NSRightMouseDown:
        ev_button = 3;
        ev_type = ButtonPress;
        goto handle_mouse;

    case NSLeftMouseUp:
        ev_button = 1;
        ev_type = ButtonRelease;
        goto handle_mouse;

    case NSOtherMouseUp:
        ev_button = 2;
        ev_type = ButtonRelease;
        goto handle_mouse;

    case NSRightMouseUp:
        ev_button = 3;
        ev_type = ButtonRelease;
        goto handle_mouse;

    case NSLeftMouseDragged:
        ev_button = 1;
        ev_type = MotionNotify;
        goto handle_mouse;

    case NSOtherMouseDragged:
        ev_button = 2;
        ev_type = MotionNotify;
        goto handle_mouse;

    case NSRightMouseDragged:
        ev_button = 3;
        ev_type = MotionNotify;
        goto handle_mouse;

    case NSMouseMoved:
        ev_button = 0;
        ev_type = MotionNotify;
        goto handle_mouse;

    case NSTabletPoint:
        ev_button = 0;
        ev_type = MotionNotify;
        goto handle_mouse;

handle_mouse:
        pDev = darwinPointer;

        /* NSTabletPoint can have no subtype */
        if ([e type] != NSTabletPoint &&
            [e subtype] == NSTabletProximityEventSubtype) {
            switch ([e pointingDeviceType]) {
            case NSEraserPointingDevice:
                darwinTabletCurrent = darwinTabletEraser;
                break;

            case NSPenPointingDevice:
                darwinTabletCurrent = darwinTabletStylus;
                break;

            case NSCursorPointingDevice:
            case NSUnknownPointingDevice:
            default:
                darwinTabletCurrent = darwinTabletCursor;
                break;
            }

            if ([e isEnteringProximity])
                needsProximityIn = YES;
            else
                DarwinSendTabletEvents(darwinTabletCurrent, ProximityOut, 0,
                                       location.x, location.y, pressure,
                                       tilt.x, tilt.y);
            return;
        }

        if ([e type] == NSTabletPoint ||
            [e subtype] == NSTabletPointEventSubtype) {
            pressure = [e pressure];
            tilt = [e tilt];

            pDev = darwinTabletCurrent;

            if (needsProximityIn) {
                DarwinSendTabletEvents(darwinTabletCurrent, ProximityIn, 0,
                                       location.x, location.y, pressure,
                                       tilt.x, tilt.y);

                needsProximityIn = NO;
            }
        }

        if (!XQuartzServerVisible && noTestExtensions) {
#if defined(XPLUGIN_VERSION) && XPLUGIN_VERSION > 0
            /* Older libXplugin (Tiger/"Stock" Leopard) aren't thread safe, so we can't call xp_find_window from the Appkit thread */
            xp_window_id wid = 0;
            xp_error err;

            /* Sigh. Need to check that we're really over one of
             * our windows. (We need to receive pointer events while
             * not in the foreground, but we don't want to receive them
             * when another window is over us or we might show a tooltip)
             */

            err = xp_find_window(location.x, location.y, 0, &wid);

            if (err != XP_Success || (err == XP_Success && wid == 0))
#endif
            {
                bgMouseLocation = location;
                bgMouseLocationUpdated = TRUE;
                return;
            }
        }

        if (bgMouseLocationUpdated) {
            if (!(ev_type == MotionNotify && ev_button == 0)) {
                DarwinSendPointerEvents(darwinPointer, MotionNotify, 0,
                                        location.x, location.y,
                                        0.0, 0.0);
            }
            bgMouseLocationUpdated = FALSE;
        }

        if (pDev == darwinPointer) {
            DarwinSendPointerEvents(pDev, ev_type, ev_button,
                                    location.x, location.y,
                                    [e deltaX], [e deltaY]);
        } else {
            DarwinSendTabletEvents(pDev, ev_type, ev_button,
                                   location.x, location.y, pressure,
                                   tilt.x, tilt.y);
        }

        break;

    case NSTabletProximity:
        switch ([e pointingDeviceType]) {
        case NSEraserPointingDevice:
            darwinTabletCurrent = darwinTabletEraser;
            break;

        case NSPenPointingDevice:
            darwinTabletCurrent = darwinTabletStylus;
            break;

        case NSCursorPointingDevice:
        case NSUnknownPointingDevice:
        default:
            darwinTabletCurrent = darwinTabletCursor;
            break;
        }

        if ([e isEnteringProximity])
            needsProximityIn = YES;
        else
            DarwinSendTabletEvents(darwinTabletCurrent, ProximityOut, 0,
                                   location.x, location.y, pressure,
                                   tilt.x, tilt.y);
        break;

    case NSScrollWheel:
    {
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1050
        float deltaX = [e deltaX];
        float deltaY = [e deltaY];
        BOOL isContinuous = NO;
#else
        CGFloat deltaX = [e deltaX];
        CGFloat deltaY = [e deltaY];
        CGEventRef cge = [e CGEvent];
        BOOL isContinuous =
            CGEventGetIntegerValueField(cge, kCGScrollWheelEventIsContinuous);

#if 0
        /* Scale the scroll value by line height */
        CGEventSourceRef source = CGEventCreateSourceFromEvent(cge);
        if (source) {
            double lineHeight = CGEventSourceGetPixelsPerLine(source);
            CFRelease(source);
            
            /* There's no real reason for the 1/5 ratio here other than that
             * it feels like a good ratio after some testing.
             */
            
            deltaX *= lineHeight / 5.0;
            deltaY *= lineHeight / 5.0;
        }
#endif
#endif
        
#if !defined(XPLUGIN_VERSION) || XPLUGIN_VERSION == 0
        /* If we're in the background, we need to send a MotionNotify event
         * first, since we aren't getting them on background mouse motion
         */
        if (!XQuartzServerVisible && noTestExtensions) {
            bgMouseLocationUpdated = FALSE;
            DarwinSendPointerEvents(darwinPointer, MotionNotify, 0,
                                    location.x, location.y,
                                    0.0, 0.0);
        }
#endif
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
        // TODO: Change 1117 to NSAppKitVersionNumber10_7 when it is defined
        if (NSAppKitVersionNumber >= 1117 &&
            XQuartzScrollInDeviceDirection &&
            [e isDirectionInvertedFromDevice]) {
            deltaX *= -1;
            deltaY *= -1;
        }
#endif
        /* This hack is in place to better deal with "clicky" scroll wheels:
         * http://xquartz.macosforge.org/trac/ticket/562
         */
        if (!isContinuous) {
            static NSTimeInterval lastScrollTime = 0.0;

            /* These store how much extra we have already scrolled.
             * ie, this is how much we ignore on the next event.
             */
            static double deficit_x = 0.0;
            static double deficit_y = 0.0;

            /* If we have past a second since the last scroll, wipe the slate
             * clean
             */
            if ([e timestamp] - lastScrollTime > 1.0) {
                deficit_x = deficit_y = 0.0;
            }
            lastScrollTime = [e timestamp];

            if (deltaX != 0.0) {
                /* If we changed directions, wipe the slate clean */
                if ((deficit_x < 0.0 && deltaX > 0.0) ||
                    (deficit_x > 0.0 && deltaX < 0.0)) {
                    deficit_x = 0.0;
                }

                /* Eat up the deficit, but ensure that something is
                 * always sent 
                 */
                if (fabs(deltaX) > fabs(deficit_x)) {
                    deltaX -= deficit_x;

                    if (deltaX > 0.0) {
                        deficit_x = ceil(deltaX) - deltaX;
                        deltaX = ceil(deltaX);
                    } else {
                        deficit_x = floor(deltaX) - deltaX;
                        deltaX = floor(deltaX);
                    }
                } else {
                    deficit_x -= deltaX;

                    if (deltaX > 0.0) {
                        deltaX = 1.0;
                    } else {
                        deltaX = -1.0;
                    }

                    deficit_x += deltaX;
                }
            }

            if (deltaY != 0.0) {
                /* If we changed directions, wipe the slate clean */
                if ((deficit_y < 0.0 && deltaY > 0.0) ||
                    (deficit_y > 0.0 && deltaY < 0.0)) {
                    deficit_y = 0.0;
                }

                /* Eat up the deficit, but ensure that something is
                 * always sent 
                 */
                if (fabs(deltaY) > fabs(deficit_y)) {
                    deltaY -= deficit_y;

                    if (deltaY > 0.0) {
                        deficit_y = ceil(deltaY) - deltaY;
                        deltaY = ceil(deltaY);
                    } else {
                        deficit_y = floor(deltaY) - deltaY;
                        deltaY = floor(deltaY);
                    }
                } else {
                    deficit_y -= deltaY;

                    if (deltaY > 0.0) {
                        deltaY = 1.0;
                    } else {
                        deltaY = -1.0;
                    }

                    deficit_y += deltaY;
                }
            }
        }

        DarwinSendScrollEvents(deltaX, deltaY);
        break;
    }

    case NSKeyDown:
    case NSKeyUp:
    {
        /* XKB clobbers our keymap at startup, so we need to force it on the first keypress.
         * TODO: Make this less of a kludge.
         */
        static int force_resync_keymap = YES;
        if (force_resync_keymap) {
            DarwinSendDDXEvent(kXquartzReloadKeymap, 0);
            force_resync_keymap = NO;
        }
    }

        if (darwinSyncKeymap) {
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
            TISInputSourceRef key_layout = 
                TISCopyCurrentKeyboardLayoutInputSource();
            TISInputSourceRef clear;
            if (CFEqual(key_layout, last_key_layout)) {
                CFRelease(key_layout);
            }
            else {
                /* Swap/free thread-safely */
                clear = last_key_layout;
                last_key_layout = key_layout;
                CFRelease(clear);
#else
            KeyboardLayoutRef key_layout;
            KLGetCurrentKeyboardLayout(&key_layout);
            if (key_layout != last_key_layout) {
                last_key_layout = key_layout;
#endif
                /* Update keyInfo */
                if (!QuartsResyncKeymap(TRUE)) {
                    ErrorF(
                        "sendX11NSEvent: Could not build a valid keymap.\n");
                }
            }
        }

        ev_type = ([e type] == NSKeyDown) ? KeyPress : KeyRelease;
        DarwinSendKeyboardEvents(ev_type, [e keyCode]);
        break;

    default:
        break;              /* for gcc */
    }
}
@end
