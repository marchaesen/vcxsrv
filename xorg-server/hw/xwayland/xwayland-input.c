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
#define NAXES 2
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

        if (!InitValuatorClassDeviceStruct(device, 2, btn_labels,
                                           GetMotionHistorySize(), Absolute))
            return BadValue;

        /* Valuators */
        InitValuatorAxisStruct(device, 0, axes_labels[0],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);
        InitValuatorAxisStruct(device, 1, axes_labels[1],
                               0, 0xFFFF, 10000, 0, 10000, Absolute);

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

    xwl_seat->xwl_screen->serial = serial;
    xwl_seat->pointer_enter_serial = serial;

    xwl_seat->focus_window = wl_surface_get_user_data(surface);

    (*pScreen->SetCursorPosition) (dev, pScreen, sx, sy, TRUE);
    CheckMotion(NULL, GetMaster(dev, MASTER_POINTER));

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
            QueuePointerEvents(xwl_seat->pointer, ButtonRelease, i, 0, &mask);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    DeviceIntPtr dev = xwl_seat->pointer;

    xwl_seat->xwl_screen->serial = serial;

    xwl_seat->focus_window = NULL;
    CheckMotion(NULL, GetMaster(dev, MASTER_POINTER));
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
    case BTN_MIDDLE:
        index = 2;
        break;
    case BTN_RIGHT:
        index = 3;
        break;
    default:
        index = button - BTN_LEFT + 1;
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
    int index, count;
    int i, val;
    const int divisor = 10;
    ValuatorMask mask;

    if (time - xwl_seat->scroll_time > 2000) {
        xwl_seat->vertical_scroll = 0;
        xwl_seat->horizontal_scroll = 0;
    }
    xwl_seat->scroll_time = time;

    /* FIXME: Need to do proper smooth scrolling here! */
    switch (axis) {
    case WL_POINTER_AXIS_VERTICAL_SCROLL:
        xwl_seat->vertical_scroll += value / divisor;
        val = wl_fixed_to_int(xwl_seat->vertical_scroll);
        xwl_seat->vertical_scroll -= wl_fixed_from_int(val);

        if (val <= -1)
            index = 4;
        else if (val >= 1)
            index = 5;
        else
            return;
        break;
    case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        xwl_seat->horizontal_scroll += value / divisor;
        val = wl_fixed_to_int(xwl_seat->horizontal_scroll);
        xwl_seat->horizontal_scroll -= wl_fixed_from_int(val);

        if (val <= -1)
            index = 6;
        else if (val >= 1)
            index = 7;
        else
            return;
        break;
    default:
        return;
    }

    valuator_mask_zero(&mask);

    count = abs(val);
    for (i = 0; i < count; i++) {
        QueuePointerEvents(xwl_seat->pointer, ButtonPress, index, 0, &mask);
        QueuePointerEvents(xwl_seat->pointer, ButtonRelease, index, 0, &mask);
    }
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
    ValuatorMask mask;

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

    valuator_mask_zero(&mask);
    QueueKeyboardEvents(xwl_seat->keyboard,
                        state ? KeyPress : KeyRelease, key + 8, &mask);
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
    ValuatorMask mask;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;
    xwl_seat->keyboard_focus = surface;

    wl_array_copy(&xwl_seat->keys, keys);
    valuator_mask_zero(&mask);
    wl_array_for_each(k, &xwl_seat->keys)
        QueueKeyboardEvents(xwl_seat->keyboard, KeyPress, *k + 8, &mask);
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    struct xwl_seat *xwl_seat = data;
    ValuatorMask mask;
    uint32_t *k;

    xwl_seat->xwl_screen->serial = serial;

    valuator_mask_zero(&mask);
    wl_array_for_each(k, &xwl_seat->keys)
        QueueKeyboardEvents(xwl_seat->keyboard, KeyRelease, *k + 8, &mask);

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

    /* We don't need any of this while we have keyboard focus since
       the regular key event processing already takes care of setting
       our internal state correctly. */
    if (xwl_seat->keyboard_focus)
        return;

    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (dev != xwl_seat->keyboard &&
            dev != GetMaster(xwl_seat->keyboard, MASTER_KEYBOARD))
            continue;

        old_state = dev->key->xkbInfo->state;
        new_state = &dev->key->xkbInfo->state;

        new_state->locked_group = group & XkbAllGroupsMask;
        new_state->locked_mods = mods_locked & XkbAllModifiersMask;
        XkbLatchModifiers(dev, XkbAllModifiersMask,
                          mods_latched & XkbAllModifiersMask);

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

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
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

    if (caps & WL_SEAT_CAPABILITY_POINTER && xwl_seat->pointer == NULL) {
        xwl_seat->wl_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(xwl_seat->wl_pointer,
                                &pointer_listener, xwl_seat);
        xwl_seat_set_cursor(xwl_seat);
        xwl_seat->pointer =
            add_device(xwl_seat, "xwayland-pointer", xwl_pointer_proc);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && xwl_seat->pointer) {
        wl_pointer_release(xwl_seat->wl_pointer);
        RemoveDevice(xwl_seat->pointer, FALSE);
        xwl_seat->pointer = NULL;
    }

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD && xwl_seat->keyboard == NULL) {
        xwl_seat->wl_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(xwl_seat->wl_keyboard,
                                 &keyboard_listener, xwl_seat);
        xwl_seat->keyboard =
            add_device(xwl_seat, "xwayland-keyboard", xwl_keyboard_proc);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && xwl_seat->keyboard) {
        wl_keyboard_release(xwl_seat->wl_keyboard);
        RemoveDevice(xwl_seat->keyboard, FALSE);
        xwl_seat->keyboard = NULL;
    }

    xwl_seat->xwl_screen->expecting_event--;
    /* FIXME: Touch ... */
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
create_input_device(struct xwl_screen *xwl_screen, uint32_t id)
{
    struct xwl_seat *xwl_seat;

    xwl_seat = calloc(sizeof *xwl_seat, 1);
    if (xwl_seat == NULL) {
        ErrorF("create_input ENOMEM");
        return;
    }

    xwl_seat->xwl_screen = xwl_screen;
    xorg_list_add(&xwl_seat->link, &xwl_screen->seat_list);

    xwl_seat->seat =
        wl_registry_bind(xwl_screen->registry, id, &wl_seat_interface, 3);
    xwl_seat->id = id;

    xwl_seat->cursor = wl_compositor_create_surface(xwl_screen->compositor);
    wl_seat_add_listener(xwl_seat->seat, &seat_listener, xwl_seat);
    wl_array_init(&xwl_seat->keys);
}

void
xwl_seat_destroy(struct xwl_seat *xwl_seat)
{
    RemoveDevice(xwl_seat->pointer, FALSE);
    RemoveDevice(xwl_seat->keyboard, FALSE);
    wl_seat_destroy(xwl_seat->seat);
    wl_surface_destroy(xwl_seat->cursor);
    wl_array_release(&xwl_seat->keys);
    free(xwl_seat);
}

static void
input_handler(void *data, struct wl_registry *registry, uint32_t id,
              const char *interface, uint32_t version)
{
    struct xwl_screen *xwl_screen = data;

    if (strcmp(interface, "wl_seat") == 0 && version >= 3) {
        create_input_device(xwl_screen, id);
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

static WindowPtr
xwl_xy_to_window(ScreenPtr screen, SpritePtr sprite, int x, int y)
{
    struct xwl_seat *xwl_seat = NULL;
    DeviceIntPtr device;

    for (device = inputInfo.devices; device; device = device->next) {
        if (device->deviceProc == xwl_pointer_proc &&
            device->spriteInfo->sprite == sprite) {
            xwl_seat = device->public.devicePrivate;
            break;
        }
    }

    if (xwl_seat == NULL) {
        /* XTEST device */
        sprite->spriteTraceGood = 1;
        return sprite->spriteTrace[0];
    }

    if (xwl_seat->focus_window) {
        sprite->spriteTraceGood = 2;
        sprite->spriteTrace[1] = xwl_seat->focus_window->window;
        return miSpriteTrace(sprite, x, y);
    }
    else {
        sprite->spriteTraceGood = 1;
        return sprite->spriteTrace[0];
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

    xwl_screen->XYToWindow = pScreen->XYToWindow;
    pScreen->XYToWindow = xwl_xy_to_window;

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
