/*
 * Copyright © 2014 Intel Corporation
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include "xwayland.h"

#include <linux/input.h>

#include <sys/mman.h>
#include <xkbsrv.h>
#include <xserver-properties.h>
#include <inpututils.h>

static void
xwl_pointer_control(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
}

static int
xwl_pointer_proc(DeviceIntPtr device, int what)
{
#define NBUTTONS 10
#define NAXES 4
    BYTE map[NBUTTONS + 1];
    int i = 0;
    Atom btn_labels[NBUTTONS] = { 0 };
    Atom axes_labels[NAXES] = { 0 };

    switch (what) {
    case DEVICE_INIT:
        device->public.on = FALSE;

        for (i = 1; i <= NBUTTONS; i++)
            map[i] = i;

        btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
        btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
        btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
        btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
        btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
        btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
        btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);
        /* don't know about the rest */

        axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
        axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);
        axes_labels[2] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_HWHEEL);
        axes_labels[3] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_WHEEL);

        if (!InitValuatorClassDeviceStruct(device, NAXES, btn_labels,
                                           GetMotionHistorySize(), Absolute))
            return BadValue;

        /* Valuators */
        InitValuatorAxisStruct(device, 0, axes_labels[0],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);
        InitValuatorAxisStruct(device, 1, axes_labels[1],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);
        InitValuatorAxisStruct(device, 2, axes_labels[2],
                               NO_AXIS_LIMITS, NO_AXIS_LIMITS, 0, 0, 0, Relative);
        InitValuatorAxisStruct(device, 3, axes_labels[3],
                               NO_AXIS_LIMITS, NO_AXIS_LIMITS, 0, 0, 0, Relative);

        SetScrollValuator(device, 2, SCROLL_TYPE_HORIZONTAL, 1.0, SCROLL_FLAG_NONE);
        SetScrollValuator(device, 3, SCROLL_TYPE_VERTICAL, 1.0, SCROLL_FLAG_PREFERRED);

        if (!InitPtrFeedbackClassDeviceStruct(device, xwl_pointer_control))
            return BadValue;

        if (!InitButtonClassDeviceStruct(device, 3, btn_labels, map))
            return BadValue;

        return Success;

    case DEVICE_ON:
        device->public.on = TRUE;
        return Success;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
        device->public.on = FALSE;
        return Success;
    }

    return BadMatch;

#undef NBUTTONS
#undef NAXES
}

static void
xwl_keyboard_control(DeviceIntPtr device, KeybdCtrl *ctrl)
{
}

static int
xwl_keyboard_proc(DeviceIntPtr device, int what)
{
    struct xwl_seat *xwl_seat = device->public.devicePrivate;
    int len;

    switch (what) {
    case DEVICE_INIT:
        device->public.on = FALSE;
        if (xwl_seat->keymap)
            len = strnlen(xwl_seat->keymap, xwl_seat->keymap_size);
        else
            len = 0;
        if (!InitKeyboardDeviceStructFromString(device, xwl_seat->keymap,
                                                len,
                                                NULL, xwl_keyboard_control))
            return BadValue;

        return Success;
    case DEVICE_ON:
        device->public.on = TRUE;
        return Success;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
        device->public.on = FALSE;
        return Success;
    }

    return BadMatch;
}

static int
xwl_touch_proc(DeviceIntPtr device, int what)
{
#define NTOUCHPOINTS 20
#define NBUTTONS 1
#define NAXES 2
    struct xwl_seat *xwl_seat = device->public.devicePrivate;
    Atom btn_labels[NBUTTONS] = { 0 };
    Atom axes_labels[NAXES] = { 0 };
    BYTE map[NBUTTONS + 1] = { 0 };

    switch (what) {
    case DEVICE_INIT:
        device->public.on = FALSE;

        axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_POSITION_X);
        axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_POSITION_Y);

        if (!InitValuatorClassDeviceStruct(device, NAXES, axes_labels,
                                           GetMotionHistorySize(), Absolute))
            return BadValue;

        if (!InitButtonClassDeviceStruct(device, NBUTTONS, btn_labels, map))
            return BadValue;

        if (!InitTouchClassDeviceStruct(device, NTOUCHPOINTS,
                                        XIDirectTouch, NAXES))
            return BadValue;

        /* Valuators */
        /* FIXME: devices might be mapped to a single wl_output */
        InitValuatorAxisStruct(device, 0, axes_labels[0],
                               0, xwl_seat->xwl_screen->width,
                               10000, 0, 10000, Absolute);
        InitValuatorAxisStruct(device, 1, axes_labels[1],
                               0, xwl_seat->xwl_screen->height,
                               10000, 0, 10000, Absolute);
        return Success;

    case DEVICE_ON:
        device->public.on = TRUE;
        return Success;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
        device->public.on = FALSE;
        return Success;
    }

    return BadMatch;
#undef NAXES
#undef NBUTTONS
#undef NTOUCHPOINTS
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface,
                     wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev = xwl_seat->pointer;
    int i;
    int sx = wl_fixed_to_int(sx_w);
    int sy = wl_fixed_to_int(sy_w);
    ScreenPtr pScreen = xwl_seat->xwl_screen->screen;
    ValuatorMask mask;

    /* There's a race here where if we create and then immediately
     * destroy a surface, we might end up in a state where the Wayland
     * compositor sends us an event for a surface that doesn't exist.
     *
     * Don't process enter events in this case.
     */
    if (surface == NULL)
        return;

    xwl_seat->xwl_screen->serial = serial;
    xwl_seat->pointer_enter_serial = serial;

    xwl_seat->focus_window = wl_surface_get_user_data(surface);

    (*pScreen->SetCursorPosition) (dev, pScreen, sx, sy, TRUE);
    CheckMotion(NULL, GetMaster(dev, POINTER_OR_FLOAT));

    /* Ideally, X clients shouldn't see these button releases.  When
     * the pointer leaves a window with buttons down, it means that
     * the wayland compositor has grabbed the pointer.  The button
     * release event is consumed by whatever grab in the compositor
     * and won't be sent to clients (the X server is a client).
     * However, we need to reset X's idea of which buttons are up and
     * down, and they're all up (by definition) when the pointer
     * enters a window.  We should figure out a way to swallow these
     * events, perhaps using an X grab whenever the pointer is not in
     * any X window, but for now just send the events. */
    valuator_mask_zero(&mask);
    for (i = 0; i < dev->button->numButtons; i++)
        if (BitIsOn(dev->button->down, i))
            QueuePointerEvents(dev, ButtonRelease, i, 0, &mask);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev = xwl_seat->pointer;

    xwl_seat->xwl_screen->serial = serial;

    xwl_seat->focus_window = NULL;
    CheckMotion(NULL, GetMaster(dev, POINTER_OR_FLOAT));
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
                      uint32_t time, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_seat *xwl_seat = data;
    int32_t dx, dy;
    int sx = wl_fixed_to_int(sx_w);
    int sy = wl_fixed_to_int(sy_w);
    ValuatorMask mask;

    if (!xwl_seat->focus_window)
        return;

    dx = xwl_seat->focus_window->window->drawable.x;
    dy = xwl_seat->focus_window->window->drawable.y;

    valuator_mask_zero(&mask);
    valuator_mask_set(&mask, 0, dx + sx);
    valuator_mask_set(&mask, 1, dy + sy);

    QueuePointerEvents(xwl_seat->pointer, MotionNotify, 0,
                       POINTER_ABSOLUTE | POINTER_SCREEN, &mask);
}

static void
pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                      uint32_t time, uint32_t button, uint32_t state)
{
    struct xwl_seat *xwl_seat = data;
    int index;
    ValuatorMask mask;

    xwl_seat->xwl_screen->serial = serial;

    switch (button) {
    case BTN_LEFT:
        index = 1;
        break;
    case BTN_MIDDLE:
        index = 2;
        break;
    case BTN_RIGHT:
        index = 3;
        break;
    default:
        /* Skip indexes 4-7: they are used for vertical and horizontal scroll.
           The rest of the buttons go in order: BTN_SIDE becomes 8, etc. */
        index = 8 + button - BTN_SIDE;
        break;
    }

    valuator_mask_zero(&mask);
    QueuePointerEvents(xwl_seat->pointer,
                       state ? ButtonPress : ButtonRelease, index, 0, &mask);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    struct xwl_seat *xwl_seat = data;
    int index;
    const int divisor = 10;
    ValuatorMask mask;

    switch (axis) {
    case WL_POINTER_AXIS_VERTICAL_SCROLL:
        index = 3;
        break;
    case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        index = 2;
        break;
    default:
        return;
    }

    valuator_mask_zero(&mask);
    valuator_mask_set_double(&mask, index, wl_fixed_to_double(value) / divisor);
    QueuePointerEvents(xwl_seat->pointer, MotionNotify, 0, POINTER_RELATIVE, &mask);
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_handle_enter,
    pointer_handle_leave,
    pointer_handle_motion,
    pointer_handle_button,
    pointer_handle_axis,
};

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                    uint32_t time, uint32_t key, uint32_t state)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k, *end;

    xwl_seat->xwl_screen->serial = serial;

    end = (uint32_t *) ((char *) xwl_seat->keys.data + xwl_seat->keys.size);
    for (k = xwl_seat->keys.data; k < end; k++) {
        if (*k == key)
            *k = *--end;
    }
    xwl_seat->keys.size = (char *) end - (char *) xwl_seat->keys.data;
    if (state) {
        k = wl_array_add(&xwl_seat->keys, sizeof *k);
        *k = key;
    }

    QueueKeyboardEvents(xwl_seat->keyboard,
                        state ? KeyPress : KeyRelease, key + 8);
}

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr master;
    XkbDescPtr xkb;
    XkbChangesRec changes = { 0 };

    if (xwl_seat->keymap)
        munmap(xwl_seat->keymap, xwl_seat->keymap_size);

    xwl_seat->keymap_size = size;
    xwl_seat->keymap = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (xwl_seat->keymap == MAP_FAILED) {
        xwl_seat->keymap_size = 0;
        xwl_seat->keymap = NULL;
        goto out;
    }

    xkb = XkbCompileKeymapFromString(xwl_seat->keyboard, xwl_seat->keymap,
                                     strnlen(xwl_seat->keymap,
                                             xwl_seat->keymap_size));
    if (!xkb)
        goto out;

    XkbUpdateDescActions(xkb, xkb->min_key_code, XkbNumKeys(xkb), &changes);

    if (xwl_seat->keyboard->key)
        /* Keep the current controls */
        XkbCopyControls(xkb, xwl_seat->keyboard->key->xkbInfo->desc);

    XkbDeviceApplyKeymap(xwl_seat->keyboard, xkb);

    master = GetMaster(xwl_seat->keyboard, MASTER_KEYBOARD);
    if (master && master->lastSlave == xwl_seat->keyboard)
        XkbDeviceApplyKeymap(master, xkb);

    XkbFreeKeyboard(xkb, XkbAllComponentsMask, TRUE);

 out:
    close(fd);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial,
                      struct wl_surface *surface, struct wl_array *keys)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;
    xwl_seat->keyboard_focus = surface;

    wl_array_copy(&xwl_seat->keys, keys);
    wl_array_for_each(k, &xwl_seat->keys)
        QueueKeyboardEvents(xwl_seat->keyboard, KeyPress, *k + 8);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;

    wl_array_for_each(k, &xwl_seat->keys)
        QueueKeyboardEvents(xwl_seat->keyboard, KeyRelease, *k + 8);

    xwl_seat->keyboard_focus = NULL;
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev;
    XkbStateRec old_state, *new_state;
    xkbStateNotify sn;
    CARD16 changed;

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (dev != xwl_seat->keyboard &&
            dev != GetMaster(xwl_seat->keyboard, MASTER_KEYBOARD))
            continue;

        old_state = dev->key->xkbInfo->state;
        new_state = &dev->key->xkbInfo->state;

        if (!xwl_seat->keyboard_focus) {
            new_state->locked_mods = mods_locked & XkbAllModifiersMask;
            XkbLatchModifiers(dev, XkbAllModifiersMask,
                              mods_latched & XkbAllModifiersMask);
        }
        new_state->locked_group = group & XkbAllGroupsMask;

        XkbComputeDerivedState(dev->key->xkbInfo);

        changed = XkbStateChangedFlags(&old_state, new_state);
        if (!changed)
            continue;

        sn.keycode = 0;
        sn.eventType = 0;
        sn.requestMajor = XkbReqCode;
        sn.requestMinor = X_kbLatchLockState;   /* close enough */
        sn.changed = changed;
        XkbSendStateNotify(dev, &sn);
    }
}

static void
keyboard_handle_repeat_info (void *data, struct wl_keyboard *keyboard,
                             int32_t rate, int32_t delay)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev;
    XkbControlsPtr ctrl;

    if (rate < 0 || delay < 0) {
	ErrorF("Wrong rate/delay: %d, %d\n", rate, delay);
	return;
    }

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (dev != xwl_seat->keyboard &&
            dev != GetMaster(xwl_seat->keyboard, MASTER_KEYBOARD))
            continue;

	if (rate != 0) {
            ctrl = dev->key->xkbInfo->desc->ctrls;
            ctrl->repeat_delay = delay;
            /* rate is number of keys per second */
            ctrl->repeat_interval = 1000 / rate;

	    XkbSetRepeatKeys(dev, -1, AutoRepeatModeOn);
	} else
	    XkbSetRepeatKeys(dev, -1, AutoRepeatModeOff);
    }
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
    keyboard_handle_repeat_info,
};

static struct xwl_touch *
xwl_seat_lookup_touch(struct xwl_seat *xwl_seat, int32_t id)
{
    struct xwl_touch *xwl_touch, *next_xwl_touch;

    xorg_list_for_each_entry_safe(xwl_touch, next_xwl_touch,
                                  &xwl_seat->touches, link_touch) {
        if (xwl_touch->id == id)
            return xwl_touch;
    }

    return NULL;
}

static void
xwl_touch_send_event(struct xwl_touch *xwl_touch,
                     struct xwl_seat *xwl_seat, int type)
{
    int32_t dx, dy;
    ValuatorMask mask;

    dx = xwl_touch->window->window->drawable.x;
    dy = xwl_touch->window->window->drawable.y;

    valuator_mask_zero(&mask);
    valuator_mask_set(&mask, 0, dx + xwl_touch->x);
    valuator_mask_set(&mask, 1, dy + xwl_touch->y);
    QueueTouchEvents(xwl_seat->touch, type, xwl_touch->id, 0, &mask);
}

static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
                  uint32_t serial, uint32_t time,
                  struct wl_surface *surface,
                  int32_t id, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_seat *xwl_seat = data;
    struct xwl_touch *xwl_touch;

    if (surface == NULL)
        return;

    xwl_touch = calloc(sizeof *xwl_touch, 1);
    if (xwl_touch == NULL) {
        ErrorF("touch_handle_down ENOMEM");
        return;
    }

    xwl_touch->window = wl_surface_get_user_data(surface);
    xwl_touch->id = id;
    xwl_touch->x = wl_fixed_to_int(sx_w);
    xwl_touch->y = wl_fixed_to_int(sy_w);
    xorg_list_add(&xwl_touch->link_touch, &xwl_seat->touches);

    xwl_touch_send_event(xwl_touch, xwl_seat, XI_TouchBegin);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
                uint32_t serial, uint32_t time, int32_t id)
{
    struct xwl_touch *xwl_touch;
    struct xwl_seat *xwl_seat = data;

    xwl_touch = xwl_seat_lookup_touch(xwl_seat, id);

    if (!xwl_touch)
        return;

    xwl_touch_send_event(xwl_touch, xwl_seat, XI_TouchEnd);
    xorg_list_del(&xwl_touch->link_touch);
    free(xwl_touch);
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
                    uint32_t time, int32_t id,
                    wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    struct xwl_seat *xwl_seat = data;
    struct xwl_touch *xwl_touch;

    xwl_touch = xwl_seat_lookup_touch(xwl_seat, id);

    if (!xwl_touch)
        return;

    xwl_touch->x = wl_fixed_to_int(sx_w);
    xwl_touch->y = wl_fixed_to_int(sy_w);
    xwl_touch_send_event(xwl_touch, xwl_seat, XI_TouchUpdate);
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}

static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
    struct xwl_seat *xwl_seat = data;
    struct xwl_touch *xwl_touch, *next_xwl_touch;

    xorg_list_for_each_entry_safe(xwl_touch, next_xwl_touch,
                                  &xwl_seat->touches, link_touch) {
        /* We can't properly notify of cancellation to the X client
         * once it thinks it has the ownership, send at least a
         * TouchEnd event.
         */
        xwl_touch_send_event(xwl_touch, xwl_seat, XI_TouchEnd);
        xorg_list_del(&xwl_touch->link_touch);
        free(xwl_touch);
    }
}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,
    touch_handle_up,
    touch_handle_motion,
    touch_handle_frame,
    touch_handle_cancel
};

static DeviceIntPtr
add_device(struct xwl_seat *xwl_seat,
           const char *driver, DeviceProc device_proc)
{
    DeviceIntPtr dev = NULL;
    static Atom type_atom;
    char name[32];

    dev = AddInputDevice(serverClient, device_proc, TRUE);
    if (dev == NULL)
        return NULL;

    if (type_atom == None)
        type_atom = MakeAtom(driver, strlen(driver), TRUE);
    snprintf(name, sizeof name, "%s:%d", driver, xwl_seat->id);
    AssignTypeAndName(dev, type_atom, name);
    dev->public.devicePrivate = xwl_seat;
    dev->type = SLAVE;
    dev->spriteInfo->spriteOwner = FALSE;

    return dev;
}

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         enum wl_seat_capability caps)
{
    struct xwl_seat *xwl_seat = data;

    if (caps & WL_SEAT_CAPABILITY_POINTER && xwl_seat->wl_pointer == NULL) {
        xwl_seat->wl_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(xwl_seat->wl_pointer,
                                &pointer_listener, xwl_seat);

        if (xwl_seat->pointer == NULL) {
            xwl_seat_set_cursor(xwl_seat);
            xwl_seat->pointer =
                add_device(xwl_seat, "xwayland-pointer", xwl_pointer_proc);
            ActivateDevice(xwl_seat->pointer, TRUE);
        }
        EnableDevice(xwl_seat->pointer, TRUE);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && xwl_seat->wl_pointer) {
        wl_pointer_release(xwl_seat->wl_pointer);
        xwl_seat->wl_pointer = NULL;

        if (xwl_seat->pointer)
            DisableDevice(xwl_seat->pointer, TRUE);
    }

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD && xwl_seat->wl_keyboard == NULL) {
        xwl_seat->wl_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(xwl_seat->wl_keyboard,
                                 &keyboard_listener, xwl_seat);

        if (xwl_seat->keyboard == NULL) {
            xwl_seat->keyboard =
                add_device(xwl_seat, "xwayland-keyboard", xwl_keyboard_proc);
            ActivateDevice(xwl_seat->keyboard, TRUE);
        }
        EnableDevice(xwl_seat->keyboard, TRUE);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && xwl_seat->wl_keyboard) {
        wl_keyboard_release(xwl_seat->wl_keyboard);
        xwl_seat->wl_keyboard = NULL;

        if (xwl_seat->keyboard)
            DisableDevice(xwl_seat->keyboard, TRUE);
    }

    if (caps & WL_SEAT_CAPABILITY_TOUCH && xwl_seat->wl_touch == NULL) {
        xwl_seat->wl_touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(xwl_seat->wl_touch,
                              &touch_listener, xwl_seat);

        if (xwl_seat->touch)
            EnableDevice(xwl_seat->touch, TRUE);
        else {
            xwl_seat->touch =
                add_device(xwl_seat, "xwayland-touch", xwl_touch_proc);
        }
    } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && xwl_seat->wl_touch) {
        wl_touch_release(xwl_seat->wl_touch);
        xwl_seat->wl_touch = NULL;

        if (xwl_seat->touch)
            DisableDevice(xwl_seat->touch, TRUE);
    }

    xwl_seat->xwl_screen->expecting_event--;
}

static void
seat_handle_name(void *data, struct wl_seat *seat,
		 const char *name)
{

}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
    seat_handle_name
};

static void
create_input_device(struct xwl_screen *xwl_screen, uint32_t id, uint32_t version)
{
    struct xwl_seat *xwl_seat;

    xwl_seat = calloc(sizeof *xwl_seat, 1);
    if (xwl_seat == NULL) {
        ErrorF("create_input ENOMEM\n");
        return;
    }

    xwl_seat->xwl_screen = xwl_screen;
    xorg_list_add(&xwl_seat->link, &xwl_screen->seat_list);

    xwl_seat->seat =
        wl_registry_bind(xwl_screen->registry, id,
                         &wl_seat_interface, min(version, 4));
    xwl_seat->id = id;

    xwl_seat->cursor = wl_compositor_create_surface(xwl_screen->compositor);
    wl_seat_add_listener(xwl_seat->seat, &seat_listener, xwl_seat);
    wl_array_init(&xwl_seat->keys);

    xorg_list_init(&xwl_seat->touches);
}

void
xwl_seat_destroy(struct xwl_seat *xwl_seat)
{
    struct xwl_touch *xwl_touch, *next_xwl_touch;

    xorg_list_for_each_entry_safe(xwl_touch, next_xwl_touch,
                                  &xwl_seat->touches, link_touch) {
        xorg_list_del(&xwl_touch->link_touch);
        free(xwl_touch);
    }

    wl_seat_destroy(xwl_seat->seat);
    wl_surface_destroy(xwl_seat->cursor);
    if (xwl_seat->cursor_frame_cb)
        wl_callback_destroy(xwl_seat->cursor_frame_cb);
    wl_array_release(&xwl_seat->keys);
    free(xwl_seat);
}

static void
input_handler(void *data, struct wl_registry *registry, uint32_t id,
              const char *interface, uint32_t version)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp(interface, "wl_seat") == 0 && version >= 3) {
        create_input_device(xwl_screen, id, version);
        xwl_screen->expecting_event++;
    }
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener input_listener = {
    input_handler,
    global_remove,
};

Bool
LegalModifier(unsigned int key, DeviceIntPtr pDev)
{
    return TRUE;
}

void
ProcessInputEvents(void)
{
    mieqProcessInputEvents();
}

void
DDXRingBell(int volume, int pitch, int duration)
{
}

void
xwl_seat_clear_touch(struct xwl_seat *xwl_seat, WindowPtr window)
{
    struct xwl_touch *xwl_touch, *next_xwl_touch;

    xorg_list_for_each_entry_safe(xwl_touch, next_xwl_touch,
                                  &xwl_seat->touches, link_touch) {
        if (xwl_touch->window->window == window) {
            xorg_list_del(&xwl_touch->link_touch);
            free(xwl_touch);
        }
    }
}

void
InitInput(int argc, char *argv[])
{
    ScreenPtr pScreen = screenInfo.screens[0];
    struct xwl_screen *xwl_screen = xwl_screen_get(pScreen);

    mieqInit();

    xwl_screen->input_registry = wl_display_get_registry(xwl_screen->display);
    wl_registry_add_listener(xwl_screen->input_registry, &input_listener,
                             xwl_screen);

    xwl_screen->expecting_event = 0;
    wl_display_roundtrip(xwl_screen->display);
    while (xwl_screen->expecting_event)
        wl_display_roundtrip(xwl_screen->display);
}

void
CloseInput(void)
{
    mieqFini();
}
