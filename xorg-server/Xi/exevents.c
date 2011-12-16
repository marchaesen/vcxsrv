/************************************************************

Copyright 1989, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1989 by Hewlett-Packard Company, Palo Alto, California.

			All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Hewlett-Packard not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

HEWLETT-PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
HEWLETT-PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

/********************************************************************
 *
 *  Routines to register and initialize extension input devices.
 *  This also contains ProcessOtherEvent, the routine called from DDX
 *  to route extension events.
 *
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "inputstr.h"
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/geproto.h>
#include "windowstr.h"
#include "miscstruct.h"
#include "region.h"
#include "exevents.h"
#include "extnsionst.h"
#include "exglobals.h"
#include "dixevents.h"	/* DeliverFocusedEvent */
#include "dixgrabs.h"	/* CreateGrab() */
#include "scrnintstr.h"
#include "listdev.h" /* for CopySwapXXXClass */
#include "xace.h"
#include "xiquerydevice.h" /* For List*Info */
#include "eventconvert.h"
#include "eventstr.h"
#include "inpututils.h"

#include <X11/extensions/XKBproto.h>
#include "xkbsrv.h"

#define WID(w) ((w) ? ((w)->drawable.id) : 0)
#define AllModifiersMask ( \
	ShiftMask | LockMask | ControlMask | Mod1Mask | Mod2Mask | \
	Mod3Mask | Mod4Mask | Mod5Mask )
#define AllButtonsMask ( \
	Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask )

Bool ShouldFreeInputMasks(WindowPtr /* pWin */ ,
				 Bool	/* ignoreSelectedEvents */
    );
static Bool MakeInputMasks(WindowPtr	/* pWin */
    );

/*
 * Only let the given client know of core events which will affect its
 * interpretation of input events, if the client's ClientPointer (or the
 * paired keyboard) is the current device.
 */
int
XIShouldNotify(ClientPtr client, DeviceIntPtr dev)
{
    DeviceIntPtr current_ptr = PickPointer(client);
    DeviceIntPtr current_kbd = GetMaster(current_ptr, KEYBOARD_OR_FLOAT);

    if (dev == current_kbd || dev == current_ptr)
        return 1;

    return 0;
}

Bool
IsPointerEvent(InternalEvent* event)
{
    switch(event->any.type)
    {
        case ET_ButtonPress:
        case ET_ButtonRelease:
        case ET_Motion:
            /* XXX: enter/leave ?? */
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

/**
 * @return the device matching the deviceid of the device set in the event, or
 * NULL if the event is not an XInput event.
 */
DeviceIntPtr
XIGetDevice(xEvent* xE)
{
    DeviceIntPtr pDev = NULL;

    if (xE->u.u.type == DeviceButtonPress ||
        xE->u.u.type == DeviceButtonRelease ||
        xE->u.u.type == DeviceMotionNotify ||
        xE->u.u.type == ProximityIn ||
        xE->u.u.type == ProximityOut ||
        xE->u.u.type == DevicePropertyNotify)
    {
        int rc;
        int id;

        id = ((deviceKeyButtonPointer*)xE)->deviceid & ~MORE_EVENTS;

        rc = dixLookupDevice(&pDev, id, serverClient, DixUnknownAccess);
        if (rc != Success)
            ErrorF("[dix] XIGetDevice failed on XACE restrictions (%d)\n", rc);
    }
    return pDev;
}


/**
 * Copy the device->key into master->key and send a mapping notify to the
 * clients if appropriate.
 * master->key needs to be allocated by the caller.
 *
 * Device is the slave device. If it is attached to a master device, we may
 * need to send a mapping notify to the client because it causes the MD
 * to change state.
 *
 * Mapping notify needs to be sent in the following cases:
 *      - different slave device on same master
 *      - different master
 *
 * XXX: They way how the code is we also send a map notify if the slave device
 * stays the same, but the master changes. This isn't really necessary though.
 *
 * XXX: this gives you funny behaviour with the ClientPointer. When a
 * MappingNotify is sent to the client, the client usually responds with a
 * GetKeyboardMapping. This will retrieve the ClientPointer's keyboard
 * mapping, regardless of which keyboard sent the last mapping notify request.
 * So depending on the CP setting, your keyboard may change layout in each
 * app...
 *
 * This code is basically the old SwitchCoreKeyboard.
 */

void
CopyKeyClass(DeviceIntPtr device, DeviceIntPtr master)
{
    KeyClassPtr mk = master->key;

    if (device == master)
        return;

    mk->sourceid = device->id;


    if (!XkbCopyDeviceKeymap(master, device))
        FatalError("Couldn't pivot keymap from device to core!\n");
}

/**
 * Copies the feedback classes from device "from" into device "to". Classes
 * are duplicated (not just flipping the pointers). All feedback classes are
 * linked lists, the full list is duplicated.
 */
static void
DeepCopyFeedbackClasses(DeviceIntPtr from, DeviceIntPtr to)
{
    ClassesPtr classes;


    if (from->intfeed)
    {
        IntegerFeedbackPtr *i, it;

        if (!to->intfeed)
        {
            classes = to->unused_classes;
            to->intfeed = classes->intfeed;
            classes->intfeed = NULL;
        }

        i = &to->intfeed;
        for (it = from->intfeed; it; it = it->next)
        {
            if (!(*i))
            {
                *i = calloc(1, sizeof(IntegerFeedbackClassRec));
                if (!(*i))
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*i)->CtrlProc = it->CtrlProc;
            (*i)->ctrl     = it->ctrl;

            i = &(*i)->next;
        }
    } else if (to->intfeed && !from->intfeed)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->intfeed = to->intfeed;
        to->intfeed      = NULL;
    }

    if (from->stringfeed)
    {
        StringFeedbackPtr *s, it;

        if (!to->stringfeed)
        {
            classes = to->unused_classes;
            to->stringfeed = classes->stringfeed;
            classes->stringfeed = NULL;
        }

        s = &to->stringfeed;
        for (it = from->stringfeed; it; it = it->next)
        {
            if (!(*s))
            {
                *s = calloc(1, sizeof(StringFeedbackClassRec));
                if (!(*s))
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*s)->CtrlProc = it->CtrlProc;
            (*s)->ctrl     = it->ctrl;

            s = &(*s)->next;
        }
    } else if (to->stringfeed && !from->stringfeed)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->stringfeed = to->stringfeed;
        to->stringfeed      = NULL;
    }

    if (from->bell)
    {
        BellFeedbackPtr *b, it;

        if (!to->bell)
        {
            classes = to->unused_classes;
            to->bell = classes->bell;
            classes->bell = NULL;
        }

        b = &to->bell;
        for (it = from->bell; it; it = it->next)
        {
            if (!(*b))
            {
                *b = calloc(1, sizeof(BellFeedbackClassRec));
                if (!(*b))
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*b)->BellProc = it->BellProc;
            (*b)->CtrlProc = it->CtrlProc;
            (*b)->ctrl     = it->ctrl;

            b = &(*b)->next;
        }
    } else if (to->bell && !from->bell)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->bell = to->bell;
        to->bell      = NULL;
    }

    if (from->leds)
    {
        LedFeedbackPtr *l, it;

        if (!to->leds)
        {
            classes = to->unused_classes;
            to->leds = classes->leds;
            classes->leds = NULL;
        }

        l = &to->leds;
        for (it = from->leds; it; it = it->next)
        {
            if (!(*l))
            {
                *l = calloc(1, sizeof(LedFeedbackClassRec));
                if (!(*l))
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*l)->CtrlProc = it->CtrlProc;
            (*l)->ctrl     = it->ctrl;
            if ((*l)->xkb_sli)
                XkbFreeSrvLedInfo((*l)->xkb_sli);
            (*l)->xkb_sli = XkbCopySrvLedInfo(from, it->xkb_sli, NULL, *l);

            l = &(*l)->next;
        }
    } else if (to->leds && !from->leds)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->leds = to->leds;
        to->leds      = NULL;
    }
}

static void
DeepCopyKeyboardClasses(DeviceIntPtr from, DeviceIntPtr to)
{
    ClassesPtr classes;

    /* XkbInitDevice (->XkbInitIndicatorMap->XkbFindSrvLedInfo) relies on the
     * kbdfeed to be set up properly, so let's do the feedback classes first.
     */
    if (from->kbdfeed)
    {
        KbdFeedbackPtr *k, it;

        if (!to->kbdfeed)
        {
            classes = to->unused_classes;

            to->kbdfeed = classes->kbdfeed;
            if (!to->kbdfeed)
                InitKeyboardDeviceStruct(to, NULL, NULL, NULL);
            classes->kbdfeed = NULL;
        }

        k = &to->kbdfeed;
        for(it = from->kbdfeed; it; it = it->next)
        {
            if (!(*k))
            {
                *k = calloc(1, sizeof(KbdFeedbackClassRec));
                if (!*k)
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*k)->BellProc = it->BellProc;
            (*k)->CtrlProc = it->CtrlProc;
            (*k)->ctrl     = it->ctrl;
            if ((*k)->xkb_sli)
                XkbFreeSrvLedInfo((*k)->xkb_sli);
            (*k)->xkb_sli = XkbCopySrvLedInfo(from, it->xkb_sli, *k, NULL);

            k = &(*k)->next;
        }
    } else if (to->kbdfeed && !from->kbdfeed)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->kbdfeed = to->kbdfeed;
        to->kbdfeed      = NULL;
    }

    if (from->key)
    {
        if (!to->key)
        {
            classes = to->unused_classes;
            to->key = classes->key;
            if (!to->key)
                InitKeyboardDeviceStruct(to, NULL, NULL, NULL);
            else
                classes->key = NULL;
        }

        CopyKeyClass(from, to);
    } else if (to->key && !from->key)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->key = to->key;
        to->key      = NULL;
    }

    /* If a SrvLedInfoPtr's flags are XkbSLI_IsDefault, the names and maps
     * pointer point into the xkbInfo->desc struct.  XkbCopySrvLedInfo
     * didn't update the pointers so we need to do it manually here.
     */
    if (to->kbdfeed)
    {
        KbdFeedbackPtr k;

        for (k = to->kbdfeed; k; k = k->next)
        {
            if (!k->xkb_sli)
                continue;
            if (k->xkb_sli->flags & XkbSLI_IsDefault)
            {
                k->xkb_sli->names = to->key->xkbInfo->desc->names->indicators;
                k->xkb_sli->maps = to->key->xkbInfo->desc->indicators->maps;
            }
        }
    }

    /* We can't just copy over the focus class. When an app sets the focus,
     * it'll do so on the master device. Copying the SDs focus means losing
     * the focus.
     * So we only copy the focus class if the device didn't have one,
     * otherwise we leave it as it is.
     */
    if (from->focus)
    {
        if (!to->focus)
        {
            WindowPtr *oldTrace;

            classes = to->unused_classes;
            to->focus = classes->focus;
            if (!to->focus)
            {
                to->focus = calloc(1, sizeof(FocusClassRec));
                if (!to->focus)
                    FatalError("[Xi] no memory for class shift.\n");
            } else
                classes->focus = NULL;

            oldTrace = to->focus->trace;
            memcpy(to->focus, from->focus, sizeof(FocusClassRec));
            to->focus->trace = realloc(oldTrace,
                                  to->focus->traceSize * sizeof(WindowPtr));
            if (!to->focus->trace && to->focus->traceSize)
                FatalError("[Xi] no memory for trace.\n");
            memcpy(to->focus->trace, from->focus->trace,
                    from->focus->traceSize * sizeof(WindowPtr));
            to->focus->sourceid = from->id;
        }
    } else if (to->focus)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->focus = to->focus;
        to->focus      = NULL;
    }

}

static void
DeepCopyPointerClasses(DeviceIntPtr from, DeviceIntPtr to)
{
    ClassesPtr classes;

    /* Feedback classes must be copied first */
    if (from->ptrfeed)
    {
        PtrFeedbackPtr *p, it;
        if (!to->ptrfeed)
        {
            classes = to->unused_classes;
            to->ptrfeed = classes->ptrfeed;
            classes->ptrfeed = NULL;
        }

        p = &to->ptrfeed;
        for (it = from->ptrfeed; it; it = it->next)
        {
            if (!(*p))
            {
                *p = calloc(1, sizeof(PtrFeedbackClassRec));
                if (!*p)
                {
                    ErrorF("[Xi] Cannot alloc memory for class copy.");
                    return;
                }
            }
            (*p)->CtrlProc = it->CtrlProc;
            (*p)->ctrl     = it->ctrl;

            p = &(*p)->next;
        }
    } else if (to->ptrfeed && !from->ptrfeed)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->ptrfeed = to->ptrfeed;
        to->ptrfeed      = NULL;
    }

    if (from->valuator)
    {
        ValuatorClassPtr v;

        if (!to->valuator)
        {
            classes = to->unused_classes;
            to->valuator = classes->valuator;
            if (to->valuator)
                classes->valuator = NULL;
        }

        v = AllocValuatorClass(to->valuator, from->valuator->numAxes);

        if (!v)
            FatalError("[Xi] no memory for class shift.\n");

        to->valuator = v;
        memcpy(v->axes, from->valuator->axes, v->numAxes * sizeof(AxisInfo));

        v->sourceid = from->id;
    } else if (to->valuator && !from->valuator)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->valuator = to->valuator;
        to->valuator      = NULL;
    }

    if (from->button)
    {
        if (!to->button)
        {
            classes = to->unused_classes;
            to->button = classes->button;
            if (!to->button)
            {
                to->button = calloc(1, sizeof(ButtonClassRec));
                if (!to->button)
                    FatalError("[Xi] no memory for class shift.\n");
            } else
                classes->button = NULL;
        }

        if (from->button->xkb_acts)
        {
            if (!to->button->xkb_acts)
            {
                to->button->xkb_acts = calloc(1, sizeof(XkbAction));
                if (!to->button->xkb_acts)
                    FatalError("[Xi] not enough memory for xkb_acts.\n");
            }
            memcpy(to->button->xkb_acts, from->button->xkb_acts,
                    sizeof(XkbAction));
        } else
            free(to->button->xkb_acts);

         memcpy(to->button->labels, from->button->labels,
                from->button->numButtons * sizeof(Atom));
        to->button->sourceid = from->id;
    } else if (to->button && !from->button)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->button = to->button;
        to->button      = NULL;
    }

    if (from->proximity)
    {
        if (!to->proximity)
        {
            classes = to->unused_classes;
            to->proximity = classes->proximity;
            if (!to->proximity)
            {
                to->proximity = calloc(1, sizeof(ProximityClassRec));
                if (!to->proximity)
                    FatalError("[Xi] no memory for class shift.\n");
            } else
                classes->proximity = NULL;
        }
        memcpy(to->proximity, from->proximity, sizeof(ProximityClassRec));
        to->proximity->sourceid = from->id;
    } else if (to->proximity)
    {
        ClassesPtr classes;
        classes = to->unused_classes;
        classes->proximity = to->proximity;
        to->proximity      = NULL;
    }
}

/**
 * Copies the CONTENT of the classes of device from into the classes in device
 * to. From and to are identical after finishing.
 *
 * If to does not have classes from currenly has, the classes are stored in
 * to's devPrivates system. Later, we recover it again from there if needed.
 * Saves a few memory allocations.
 */
void
DeepCopyDeviceClasses(DeviceIntPtr from, DeviceIntPtr to, DeviceChangedEvent *dce)
{
    /* generic feedback classes, not tied to pointer and/or keyboard */
    DeepCopyFeedbackClasses(from, to);

    if ((dce->flags & DEVCHANGE_KEYBOARD_EVENT))
        DeepCopyKeyboardClasses(from, to);
    if ((dce->flags & DEVCHANGE_POINTER_EVENT))
        DeepCopyPointerClasses(from, to);
}


/**
 * Send an XI2 DeviceChangedEvent to all interested clients.
 */
void
XISendDeviceChangedEvent(DeviceIntPtr device, DeviceChangedEvent *dce)
{
    xXIDeviceChangedEvent *dcce;
    int rc;

    rc = EventToXI2((InternalEvent*)dce, (xEvent**)&dcce);
    if (rc != Success)
    {
        ErrorF("[Xi] event conversion from DCE failed with code %d\n", rc);
        return;
    }

    /* we don't actually swap if there's a NullClient, swapping is done
     * later when event is delivered. */
    SendEventToAllWindows(device, XI_DeviceChangedMask, (xEvent*)dcce, 1);
    free(dcce);
}

static void
ChangeMasterDeviceClasses(DeviceIntPtr device, DeviceChangedEvent *dce)
{
    DeviceIntPtr slave;
    int rc;

    /* For now, we don't have devices that change physically. */
    if (!IsMaster(device))
        return;

    rc = dixLookupDevice(&slave, dce->sourceid, serverClient, DixReadAccess);

    if (rc != Success)
        return; /* Device has disappeared */

    if (IsMaster(slave))
        return;

    if (IsFloating(slave))
        return; /* set floating since the event */

    if (GetMaster(slave, MASTER_ATTACHED)->id != dce->masterid)
        return; /* not our slave anymore, don't care */

    /* FIXME: we probably need to send a DCE for the new slave now */

    device->public.devicePrivate = slave->public.devicePrivate;

    /* FIXME: the classes may have changed since we generated the event. */
    DeepCopyDeviceClasses(slave, device, dce);
    dce->deviceid = device->id;
    XISendDeviceChangedEvent(device, dce);
}

/**
 * Add state and motionMask to the filter for this event. The protocol
 * supports some extra masks for motion when a button is down:
 * ButtonXMotionMask and the DeviceButtonMotionMask to trigger only when at
 * least one button (or that specific button is down). These masks need to
 * be added to the filters for core/XI motion events.
 *
 * @param device The device to update the mask for
 * @param state The current button state mask
 * @param motion_mask The motion mask (DeviceButtonMotionMask or 0)
 */
static void
UpdateDeviceMotionMask(DeviceIntPtr device, unsigned short state,
                       Mask motion_mask)
{
    Mask mask;

    mask = DevicePointerMotionMask | state | motion_mask;
    SetMaskForEvent(device->id, mask, DeviceMotionNotify);
    mask = PointerMotionMask | state | motion_mask;
    SetMaskForEvent(device->id, mask, MotionNotify);
}

static void
IncreaseButtonCount(DeviceIntPtr dev, int key, CARD8 *buttons_down,
                    Mask *motion_mask, unsigned short *state)
{
    if (dev->valuator)
        dev->valuator->motionHintWindow = NullWindow;

    (*buttons_down)++;
    *motion_mask = DeviceButtonMotionMask;
    if (dev->button->map[key] <= 5)
        *state |= (Button1Mask >> 1) << dev->button->map[key];
}

static void
DecreaseButtonCount(DeviceIntPtr dev, int key, CARD8 *buttons_down,
                    Mask *motion_mask, unsigned short *state)
{
    if (dev->valuator)
        dev->valuator->motionHintWindow = NullWindow;

    if (*buttons_down >= 1 && !--(*buttons_down))
        *motion_mask = 0;
    if (dev->button->map[key] <= 5)
        *state &= ~((Button1Mask >> 1) << dev->button->map[key]);
}

/**
 * Update the device state according to the data in the event.
 *
 * return values are
 *   DEFAULT ... process as normal
 *   DONT_PROCESS ... return immediately from caller
 */
#define DEFAULT 0
#define DONT_PROCESS 1
int
UpdateDeviceState(DeviceIntPtr device, DeviceEvent* event)
{
    int i;
    int key = 0,
        last_valuator;

    KeyClassPtr k       = NULL;
    ButtonClassPtr b    = NULL;
    ValuatorClassPtr v  = NULL;

    /* This event is always the first we get, before the actual events with
     * the data. However, the way how the DDX is set up, "device" will
     * actually be the slave device that caused the event.
     */
    switch(event->type)
    {
        case ET_DeviceChanged:
            ChangeMasterDeviceClasses(device, (DeviceChangedEvent*)event);
            return DONT_PROCESS; /* event has been sent already */
        case ET_Motion:
        case ET_ButtonPress:
        case ET_ButtonRelease:
        case ET_KeyPress:
        case ET_KeyRelease:
        case ET_ProximityIn:
        case ET_ProximityOut:
            break;
        default:
            /* other events don't update the device */
            return DEFAULT;
    }

    k = device->key;
    v = device->valuator;
    b = device->button;

    key = event->detail.key;

    /* Update device axis */
    /* Check valuators first */
    last_valuator = -1;
    for (i = 0; i < MAX_VALUATORS; i++)
    {
        if (BitIsOn(&event->valuators.mask, i))
        {
            if (!v)
            {
                ErrorF("[Xi] Valuators reported for non-valuator device '%s'. "
                        "Ignoring event.\n", device->name);
                return DONT_PROCESS;
            } else if (v->numAxes < i)
            {
                ErrorF("[Xi] Too many valuators reported for device '%s'. "
                        "Ignoring event.\n", device->name);
                return DONT_PROCESS;
            }
            last_valuator = i;
        }
    }

    for (i = 0; i <= last_valuator && i < v->numAxes; i++)
    {
        /* XXX: Relative/Absolute mode */
        if (BitIsOn(&event->valuators.mask, i))
            v->axisVal[i] = event->valuators.data[i];
    }

    if (event->type == ET_KeyPress) {
        if (!k)
            return DONT_PROCESS;

	/* don't allow ddx to generate multiple downs, but repeats are okay */
	if (key_is_down(device, key, KEY_PROCESSED) && !event->key_repeat)
	    return DONT_PROCESS;

	if (device->valuator)
	    device->valuator->motionHintWindow = NullWindow;
	set_key_down(device, key, KEY_PROCESSED);
    } else if (event->type == ET_KeyRelease) {
        if (!k)
            return DONT_PROCESS;

	if (!key_is_down(device, key, KEY_PROCESSED))	/* guard against duplicates */
	    return DONT_PROCESS;
	if (device->valuator)
	    device->valuator->motionHintWindow = NullWindow;
	set_key_up(device, key, KEY_PROCESSED);
    } else if (event->type == ET_ButtonPress) {
        if (!b)
            return DONT_PROCESS;

        if (button_is_down(device, key, BUTTON_PROCESSED))
            return DONT_PROCESS;

        set_button_down(device, key, BUTTON_PROCESSED);

        if (!b->map[key])
            return DONT_PROCESS;

        IncreaseButtonCount(device, key, &b->buttonsDown, &b->motionMask, &b->state);
        UpdateDeviceMotionMask(device, b->state, b->motionMask);
    } else if (event->type == ET_ButtonRelease) {
        if (!b)
            return DONT_PROCESS;

        if (!button_is_down(device, key, BUTTON_PROCESSED))
            return DONT_PROCESS;
        if (IsMaster(device)) {
            DeviceIntPtr sd;

            /*
             * Leave the button down if any slave has the
             * button still down. Note that this depends on the
             * event being delivered through the slave first
             */
            for (sd = inputInfo.devices; sd; sd = sd->next) {
                if (IsMaster(sd) || GetMaster(sd, MASTER_POINTER) != device)
                    continue;
                if (!sd->button)
                    continue;
                for (i = 1; i <= sd->button->numButtons; i++)
                    if (sd->button->map[i] == key &&
                        button_is_down(sd, i, BUTTON_PROCESSED))
                        return DONT_PROCESS;
            }
        }
        set_button_up(device, key, BUTTON_PROCESSED);
        if (!b->map[key])
            return DONT_PROCESS;

        DecreaseButtonCount(device, key, &b->buttonsDown, &b->motionMask, &b->state);
        UpdateDeviceMotionMask(device,  b->state, b->motionMask);
    } else if (event->type == ET_ProximityIn)
	device->proximity->in_proximity = TRUE;
    else if (event->type == ET_ProximityOut)
	device->proximity->in_proximity = FALSE;

    return DEFAULT;
}

/**
 * Main device event processing function.
 * Called from when processing the events from the event queue.
 *
 */
void
ProcessOtherEvent(InternalEvent *ev, DeviceIntPtr device)
{
    GrabPtr grab;
    Bool deactivateDeviceGrab = FALSE;
    int key = 0, rootX, rootY;
    ButtonClassPtr b;
    int ret = 0;
    int corestate;
    DeviceIntPtr mouse = NULL, kbd = NULL;
    DeviceEvent *event = &ev->device_event;

    verify_internal_event(ev);

    if (ev->any.type == ET_RawKeyPress ||
        ev->any.type == ET_RawKeyRelease ||
        ev->any.type == ET_RawButtonPress ||
        ev->any.type == ET_RawButtonRelease ||
        ev->any.type == ET_RawMotion)
    {
        DeliverRawEvent(&ev->raw_event, device);
        return;
    }

    if (IsPointerDevice(device))
    {
        kbd = GetMaster(device, KEYBOARD_OR_FLOAT);
        mouse = device;
        if (!kbd->key) /* can happen with floating SDs */
            kbd = NULL;
    } else
    {
        mouse = GetMaster(device, POINTER_OR_FLOAT);
        kbd = device;
        if (!mouse->valuator || !mouse->button) /* may be float. SDs */
            mouse = NULL;
    }

    corestate = event_get_corestate(mouse, kbd);
    event_set_state(mouse, kbd, event);

    ret = UpdateDeviceState(device, event);
    if (ret == DONT_PROCESS)
        return;

    b = device->button;

    if (IsMaster(device) || IsFloating(device))
        CheckMotion(event, device);

    switch (event->type)
    {
        case ET_Motion:
        case ET_ButtonPress:
        case ET_ButtonRelease:
        case ET_KeyPress:
        case ET_KeyRelease:
        case ET_ProximityIn:
        case ET_ProximityOut:
            GetSpritePosition(device, &rootX, &rootY);
            event->root_x = rootX;
            event->root_y = rootY;
            NoticeEventTime((InternalEvent*)event);
            event->corestate = corestate;
            key = event->detail.key;
            break;
        default:
            break;
    }

    if (DeviceEventCallback && !syncEvents.playingEvents) {
	DeviceEventInfoRec eventinfo;
	SpritePtr pSprite = device->spriteInfo->sprite;

	/* see comment in EnqueueEvents regarding the next three lines */
	if (ev->any.type == ET_Motion)
	    ev->device_event.root = pSprite->hotPhys.pScreen->root->drawable.id;

	eventinfo.device = device;
	eventinfo.event = ev;
	CallCallbacks(&DeviceEventCallback, (pointer) & eventinfo);
    }

    grab = device->deviceGrab.grab;

    switch(event->type)
    {
        case ET_KeyPress:
            if (!grab && CheckDeviceGrabs(device, event, 0))
                return;
            break;
        case ET_KeyRelease:
            if (grab && device->deviceGrab.fromPassiveGrab &&
                (key == device->deviceGrab.activatingKey) &&
                GrabIsKeyboardGrab(device->deviceGrab.grab))
                deactivateDeviceGrab = TRUE;
            break;
        case ET_ButtonPress:
            if (b->map[key] == 0) /* there's no button 0 */
                return;
            event->detail.button = b->map[key];
            if (!grab && CheckDeviceGrabs(device, event, 0))
            {
                /* if a passive grab was activated, the event has been sent
                 * already */
                return;
            }
            break;
        case ET_ButtonRelease:
            if (b->map[key] == 0) /* there's no button 0 */
                return;
            event->detail.button = b->map[key];
            if (grab && !b->buttonsDown &&
                device->deviceGrab.fromPassiveGrab &&
                GrabIsPointerGrab(device->deviceGrab.grab))
                deactivateDeviceGrab = TRUE;
        default:
            break;
    }


    if (grab)
        DeliverGrabbedEvent((InternalEvent*)event, device, deactivateDeviceGrab);
    else if (device->focus && !IsPointerEvent(ev))
        DeliverFocusedEvent(device, (InternalEvent*)event,
                            GetSpriteWindow(device));
    else
        DeliverDeviceEvents(GetSpriteWindow(device), (InternalEvent*)event,
                            NullGrab, NullWindow, device);

    if (deactivateDeviceGrab == TRUE)
	(*device->deviceGrab.DeactivateGrab) (device);
    event->detail.key = key;
}

int
InitProximityClassDeviceStruct(DeviceIntPtr dev)
{
    ProximityClassPtr proxc;

    proxc = (ProximityClassPtr) malloc(sizeof(ProximityClassRec));
    if (!proxc)
	return FALSE;
    proxc->sourceid = dev->id;
    proxc->in_proximity = TRUE;
    dev->proximity = proxc;
    return TRUE;
}

/**
 * Initialise the device's valuators. The memory must already be allocated,
 * this function merely inits the matching axis (specified through axnum) to
 * sane values.
 *
 * It is a condition that (minval < maxval).
 *
 * @see InitValuatorClassDeviceStruct
 */
Bool
InitValuatorAxisStruct(DeviceIntPtr dev, int axnum, Atom label, int minval, int maxval,
		       int resolution, int min_res, int max_res, int mode)
{
    AxisInfoPtr ax;

    if (!dev || !dev->valuator || (minval > maxval && mode == Absolute))
        return FALSE;
    if (axnum >= dev->valuator->numAxes)
        return FALSE;

    ax = dev->valuator->axes + axnum;

    ax->min_value = minval;
    ax->max_value = maxval;
    ax->resolution = resolution;
    ax->min_resolution = min_res;
    ax->max_resolution = max_res;
    ax->label = label;
    ax->mode = mode;

    if (mode & OutOfProximity)
        dev->proximity->in_proximity = FALSE;

    return SetScrollValuator(dev, axnum, SCROLL_TYPE_NONE, 0, SCROLL_FLAG_NONE);
}

/**
 * Set the given axis number as a scrolling valuator.
 */
Bool
SetScrollValuator(DeviceIntPtr dev, int axnum, enum ScrollType type, double increment, int flags)
{
    AxisInfoPtr ax;
    int *current_ax;
    InternalEvent dce;
    DeviceIntPtr master;

    if (!dev || !dev->valuator || axnum >= dev->valuator->numAxes)
        return FALSE;

    switch (type)
    {
        case SCROLL_TYPE_VERTICAL:
            current_ax = &dev->valuator->v_scroll_axis;
            break;
        case SCROLL_TYPE_HORIZONTAL:
            current_ax = &dev->valuator->h_scroll_axis;
            break;
        case SCROLL_TYPE_NONE:
            ax = &dev->valuator->axes[axnum];
            ax->scroll.type = type;
            return TRUE;
        default:
            return FALSE;
    }

    if (increment == 0.0)
        return FALSE;

    if (*current_ax != -1 && axnum != *current_ax)
    {
        ax = &dev->valuator->axes[*current_ax];
        if (ax->scroll.type == type &&
            (flags & SCROLL_FLAG_PREFERRED) && (ax->scroll.flags & SCROLL_FLAG_PREFERRED))
            return FALSE;
    }
    *current_ax = axnum;

    ax = &dev->valuator->axes[axnum];
    ax->scroll.type = type;
    ax->scroll.increment = increment;
    ax->scroll.flags = flags;

    master = GetMaster(dev, MASTER_ATTACHED);
    CreateClassesChangedEvent(&dce, master, dev, DEVCHANGE_POINTER_EVENT | DEVCHANGE_DEVICE_CHANGE);
    XISendDeviceChangedEvent(dev, &dce.changed_event);

    /* if the current slave is us, update the master. If not, we'll update
     * whenever the next slave switch happens anyway. CMDC sends the event
     * for us */
    if (master && master->lastSlave == dev)
        ChangeMasterDeviceClasses(master, &dce.changed_event);

    return TRUE;
}

static void
FixDeviceStateNotify(DeviceIntPtr dev, deviceStateNotify * ev, KeyClassPtr k,
		     ButtonClassPtr b, ValuatorClassPtr v, int first)
{
    ev->type = DeviceStateNotify;
    ev->deviceid = dev->id;
    ev->time = currentTime.milliseconds;
    ev->classes_reported = 0;
    ev->num_keys = 0;
    ev->num_buttons = 0;
    ev->num_valuators = 0;

    if (b) {
	ev->classes_reported |= (1 << ButtonClass);
	ev->num_buttons = b->numButtons;
	memcpy((char*)ev->buttons, (char*)b->down, 4);
    } else if (k) {
	ev->classes_reported |= (1 << KeyClass);
	ev->num_keys = k->xkbInfo->desc->max_key_code -
                       k->xkbInfo->desc->min_key_code;
	memmove((char *)&ev->keys[0], (char *)k->down, 4);
    }
    if (v) {
	int nval = v->numAxes - first;

	ev->classes_reported |= (1 << ValuatorClass);
	ev->classes_reported |= valuator_get_mode(dev, 0) << ModeBitsShift;
	ev->num_valuators = nval < 3 ? nval : 3;
	switch (ev->num_valuators) {
	case 3:
	    ev->valuator2 = v->axisVal[first + 2];
	case 2:
	    ev->valuator1 = v->axisVal[first + 1];
	case 1:
	    ev->valuator0 = v->axisVal[first];
	    break;
	}
    }
}

static void
FixDeviceValuator(DeviceIntPtr dev, deviceValuator * ev, ValuatorClassPtr v,
		  int first)
{
    int nval = v->numAxes - first;

    ev->type = DeviceValuator;
    ev->deviceid = dev->id;
    ev->num_valuators = nval < 3 ? nval : 3;
    ev->first_valuator = first;
    switch (ev->num_valuators) {
    case 3:
	ev->valuator2 = v->axisVal[first + 2];
    case 2:
	ev->valuator1 = v->axisVal[first + 1];
    case 1:
	ev->valuator0 = v->axisVal[first];
	break;
    }
    first += ev->num_valuators;
}

static void
DeliverStateNotifyEvent(DeviceIntPtr dev, WindowPtr win)
{
    int evcount = 1;
    deviceStateNotify *ev, *sev;
    deviceKeyStateNotify *kev;
    deviceButtonStateNotify *bev;

    KeyClassPtr k;
    ButtonClassPtr b;
    ValuatorClassPtr v;
    int nval = 0, nkeys = 0, nbuttons = 0, first = 0;

    if (!(wOtherInputMasks(win)) ||
        !(wOtherInputMasks(win)->inputEvents[dev->id] & DeviceStateNotifyMask))
        return;

    if ((b = dev->button) != NULL) {
        nbuttons = b->numButtons;
        if (nbuttons > 32)
            evcount++;
    }
    if ((k = dev->key) != NULL) {
        nkeys = k->xkbInfo->desc->max_key_code -
            k->xkbInfo->desc->min_key_code;
        if (nkeys > 32)
            evcount++;
        if (nbuttons > 0) {
            evcount++;
        }
    }
    if ((v = dev->valuator) != NULL) {
        nval = v->numAxes;

        if (nval > 3)
            evcount++;
        if (nval > 6) {
            if (!(k && b))
                evcount++;
            if (nval > 9)
                evcount += ((nval - 7) / 3);
        }
    }

    sev = ev = (deviceStateNotify *) malloc(evcount * sizeof(xEvent));
    FixDeviceStateNotify(dev, ev, NULL, NULL, NULL, first);

    if (b != NULL) {
        FixDeviceStateNotify(dev, ev++, NULL, b, v, first);
        first += 3;
        nval -= 3;
        if (nbuttons > 32) {
            (ev - 1)->deviceid |= MORE_EVENTS;
            bev = (deviceButtonStateNotify *) ev++;
            bev->type = DeviceButtonStateNotify;
            bev->deviceid = dev->id;
            memcpy((char*)&bev->buttons[4], (char*)&b->down[4], DOWN_LENGTH - 4);
        }
        if (nval > 0) {
            (ev - 1)->deviceid |= MORE_EVENTS;
            FixDeviceValuator(dev, (deviceValuator *) ev++, v, first);
            first += 3;
            nval -= 3;
        }
    }

    if (k != NULL) {
        FixDeviceStateNotify(dev, ev++, k, NULL, v, first);
        first += 3;
        nval -= 3;
        if (nkeys > 32) {
            (ev - 1)->deviceid |= MORE_EVENTS;
            kev = (deviceKeyStateNotify *) ev++;
            kev->type = DeviceKeyStateNotify;
            kev->deviceid = dev->id;
            memmove((char *)&kev->keys[0], (char *)&k->down[4], 28);
        }
        if (nval > 0) {
            (ev - 1)->deviceid |= MORE_EVENTS;
            FixDeviceValuator(dev, (deviceValuator *) ev++, v, first);
            first += 3;
            nval -= 3;
        }
    }

    while (nval > 0) {
        FixDeviceStateNotify(dev, ev++, NULL, NULL, v, first);
        first += 3;
        nval -= 3;
        if (nval > 0) {
            (ev - 1)->deviceid |= MORE_EVENTS;
            FixDeviceValuator(dev, (deviceValuator *) ev++, v, first);
            first += 3;
            nval -= 3;
        }
    }

    DeliverEventsToWindow(dev, win, (xEvent *) sev, evcount,
                          DeviceStateNotifyMask, NullGrab);
    free(sev);
}

void
DeviceFocusEvent(DeviceIntPtr dev, int type, int mode, int detail,
		 WindowPtr pWin)
{
    deviceFocus event;
    xXIFocusInEvent *xi2event;
    DeviceIntPtr mouse;
    int btlen, len, i;

    mouse = IsFloating(dev) ? dev : GetMaster(dev, MASTER_POINTER);

    /* XI 2 event */
    btlen = (mouse->button) ? bits_to_bytes(mouse->button->numButtons) : 0;
    btlen = bytes_to_int32(btlen);
    len = sizeof(xXIFocusInEvent) + btlen * 4;

    xi2event = calloc(1, len);
    xi2event->type         = GenericEvent;
    xi2event->extension    = IReqCode;
    xi2event->evtype       = type;
    xi2event->length       = bytes_to_int32(len - sizeof(xEvent));
    xi2event->buttons_len  = btlen;
    xi2event->detail       = detail;
    xi2event->time         = currentTime.milliseconds;
    xi2event->deviceid     = dev->id;
    xi2event->sourceid     = dev->id; /* a device doesn't change focus by itself */
    xi2event->mode         = mode;
    xi2event->root_x       = FP1616(mouse->spriteInfo->sprite->hot.x, 0);
    xi2event->root_y       = FP1616(mouse->spriteInfo->sprite->hot.y, 0);

    for (i = 0; mouse && mouse->button && i < mouse->button->numButtons; i++)
        if (BitIsOn(mouse->button->down, i))
            SetBit(&xi2event[1], i);

    if (dev->key)
    {
        xi2event->mods.base_mods = dev->key->xkbInfo->state.base_mods;
        xi2event->mods.latched_mods = dev->key->xkbInfo->state.latched_mods;
        xi2event->mods.locked_mods = dev->key->xkbInfo->state.locked_mods;
        xi2event->mods.effective_mods = dev->key->xkbInfo->state.mods;

        xi2event->group.base_group = dev->key->xkbInfo->state.base_group;
        xi2event->group.latched_group = dev->key->xkbInfo->state.latched_group;
        xi2event->group.locked_group = dev->key->xkbInfo->state.locked_group;
        xi2event->group.effective_group = dev->key->xkbInfo->state.group;
    }

    FixUpEventFromWindow(dev->spriteInfo->sprite, (xEvent*)xi2event, pWin,
                         None, FALSE);

    DeliverEventsToWindow(dev, pWin, (xEvent*)xi2event, 1,
                          GetEventFilter(dev, (xEvent*)xi2event), NullGrab);

    free(xi2event);

    /* XI 1.x event */
    event.deviceid = dev->id;
    event.mode = mode;
    event.type = (type == XI_FocusIn) ? DeviceFocusIn : DeviceFocusOut;
    event.detail = detail;
    event.window = pWin->drawable.id;
    event.time = currentTime.milliseconds;

    DeliverEventsToWindow(dev, pWin, (xEvent *) & event, 1,
				DeviceFocusChangeMask, NullGrab);

    if (event.type == DeviceFocusIn)
        DeliverStateNotifyEvent(dev, pWin);
}

int
CheckGrabValues(ClientPtr client, GrabParameters* param)
{
    if (param->grabtype != CORE &&
        param->grabtype != XI &&
        param->grabtype != XI2)
    {
        ErrorF("[Xi] grabtype is invalid. This is a bug.\n");
        return BadImplementation;
    }

    if ((param->this_device_mode != GrabModeSync) &&
	(param->this_device_mode != GrabModeAsync)) {
	client->errorValue = param->this_device_mode;
	return BadValue;
    }
    if ((param->other_devices_mode != GrabModeSync) &&
	(param->other_devices_mode != GrabModeAsync)) {
	client->errorValue = param->other_devices_mode;
	return BadValue;
    }

    if (param->grabtype != XI2 && (param->modifiers != AnyModifier) &&
        (param->modifiers & ~AllModifiersMask)) {
	client->errorValue = param->modifiers;
	return BadValue;
    }

    if ((param->ownerEvents != xFalse) && (param->ownerEvents != xTrue)) {
	client->errorValue = param->ownerEvents;
	return BadValue;
    }
    return Success;
}

int
GrabButton(ClientPtr client, DeviceIntPtr dev, DeviceIntPtr modifier_device,
           int button, GrabParameters *param, enum InputLevel grabtype,
	   GrabMask *mask)
{
    WindowPtr pWin, confineTo;
    CursorPtr cursor;
    GrabPtr grab;
    int rc, type = -1;
    Mask access_mode = DixGrabAccess;

    rc = CheckGrabValues(client, param);
    if (rc != Success)
	return rc;
    if (param->confineTo == None)
	confineTo = NullWindow;
    else {
	rc = dixLookupWindow(&confineTo, param->confineTo, client, DixSetAttrAccess);
	if (rc != Success)
	    return rc;
    }
    if (param->cursor == None)
	cursor = NullCursor;
    else {
	rc = dixLookupResourceByType((pointer *)&cursor, param->cursor,
				     RT_CURSOR, client, DixUseAccess);
	if (rc != Success)
	{
	    client->errorValue = param->cursor;
	    return rc;
	}
	access_mode |= DixForceAccess;
    }
    if (param->this_device_mode == GrabModeSync || param->other_devices_mode == GrabModeSync)
	access_mode |= DixFreezeAccess;
    rc = XaceHook(XACE_DEVICE_ACCESS, client, dev, access_mode);
    if (rc != Success)
	return rc;
    rc = dixLookupWindow(&pWin, param->grabWindow, client, DixSetAttrAccess);
    if (rc != Success)
	return rc;

    if (grabtype == XI)
        type = DeviceButtonPress;
    else if (grabtype == XI2)
        type = XI_ButtonPress;

    grab = CreateGrab(client->index, dev, modifier_device, pWin, grabtype,
                      mask, param, type, button, confineTo, cursor);
    if (!grab)
	return BadAlloc;
    return AddPassiveGrabToList(client, grab);
}

/**
 * Grab the given key. If grabtype is XI, the key is a keycode. If
 * grabtype is XI2, the key is a keysym.
 */
int
GrabKey(ClientPtr client, DeviceIntPtr dev, DeviceIntPtr modifier_device,
        int key, GrabParameters *param, enum InputLevel grabtype, GrabMask *mask)
{
    WindowPtr pWin;
    GrabPtr grab;
    KeyClassPtr k = dev->key;
    Mask access_mode = DixGrabAccess;
    int rc, type = -1;

    rc = CheckGrabValues(client, param);
    if (rc != Success)
        return rc;
    if ((dev->id != XIAllDevices && dev->id != XIAllMasterDevices) && k == NULL)
	return BadMatch;
    if (grabtype == XI)
    {
        if ((key > k->xkbInfo->desc->max_key_code ||
                    key < k->xkbInfo->desc->min_key_code)
                && (key != AnyKey)) {
            client->errorValue = key;
            return BadValue;
        }
        type = DeviceKeyPress;
    } else if (grabtype == XI2)
        type = XI_KeyPress;

    rc = dixLookupWindow(&pWin, param->grabWindow, client, DixSetAttrAccess);
    if (rc != Success)
	return rc;
    if (param->this_device_mode == GrabModeSync || param->other_devices_mode == GrabModeSync)
	access_mode |= DixFreezeAccess;
    rc = XaceHook(XACE_DEVICE_ACCESS, client, dev, access_mode);
    if (rc != Success)
	return rc;

    grab = CreateGrab(client->index, dev, modifier_device, pWin, grabtype,
                      mask, param, type, key, NULL, NULL);
    if (!grab)
	return BadAlloc;
    return AddPassiveGrabToList(client, grab);
}

/* Enter/FocusIn grab */
int
GrabWindow(ClientPtr client, DeviceIntPtr dev, int type,
           GrabParameters *param, GrabMask *mask)
{
    WindowPtr pWin;
    CursorPtr cursor;
    GrabPtr grab;
    Mask access_mode = DixGrabAccess;
    int rc;

    rc = CheckGrabValues(client, param);
    if (rc != Success)
        return rc;

    rc = dixLookupWindow(&pWin, param->grabWindow, client, DixSetAttrAccess);
    if (rc != Success)
	return rc;
    if (param->cursor == None)
	cursor = NullCursor;
    else {
	rc = dixLookupResourceByType((pointer *)&cursor, param->cursor,
				     RT_CURSOR, client, DixUseAccess);
	if (rc != Success)
	{
	    client->errorValue = param->cursor;
	    return rc;
	}
	access_mode |= DixForceAccess;
    }
    if (param->this_device_mode == GrabModeSync || param->other_devices_mode == GrabModeSync)
	access_mode |= DixFreezeAccess;
    rc = XaceHook(XACE_DEVICE_ACCESS, client, dev, access_mode);
    if (rc != Success)
	return rc;

    grab = CreateGrab(client->index, dev, dev, pWin, XI2,
                      mask, param, (type == XIGrabtypeEnter) ? XI_Enter : XI_FocusIn,
                      0, NULL, cursor);

    if (!grab)
        return BadAlloc;

    return AddPassiveGrabToList(client, grab);
}

int
SelectForWindow(DeviceIntPtr dev, WindowPtr pWin, ClientPtr client,
		Mask mask, Mask exclusivemasks)
{
    int mskidx = dev->id;
    int i, ret;
    Mask check;
    InputClientsPtr others;

    check = (mask & exclusivemasks);
    if (wOtherInputMasks(pWin)) {
	if (check & wOtherInputMasks(pWin)->inputEvents[mskidx]) {	/* It is illegal for two different
									 * clients to select on any of the
									 * events for maskcheck. However,
									 * it is OK, for some client to
									 * continue selecting on one of those
									 * events.  */
	    for (others = wOtherInputMasks(pWin)->inputClients; others;
		 others = others->next) {
		if (!SameClient(others, client) && (check &
						    others->mask[mskidx]))
		    return BadAccess;
	    }
	}
	for (others = wOtherInputMasks(pWin)->inputClients; others;
	     others = others->next) {
	    if (SameClient(others, client)) {
		check = others->mask[mskidx];
		others->mask[mskidx] = mask;
		if (mask == 0) {
		    for (i = 0; i < EMASKSIZE; i++)
			if (i != mskidx && others->mask[i] != 0)
			    break;
		    if (i == EMASKSIZE) {
			RecalculateDeviceDeliverableEvents(pWin);
			if (ShouldFreeInputMasks(pWin, FALSE))
			    FreeResource(others->resource, RT_NONE);
			return Success;
		    }
		}
		goto maskSet;
	    }
	}
    }
    check = 0;
    if ((ret = AddExtensionClient(pWin, client, mask, mskidx)) != Success)
	return ret;
  maskSet:
    if (dev->valuator)
	if ((dev->valuator->motionHintWindow == pWin) &&
	    (mask & DevicePointerMotionHintMask) &&
	    !(check & DevicePointerMotionHintMask) && !dev->deviceGrab.grab)
	    dev->valuator->motionHintWindow = NullWindow;
    RecalculateDeviceDeliverableEvents(pWin);
    return Success;
}

static void
FreeInputClient(InputClientsPtr *other)
{
    xi2mask_free(&(*other)->xi2mask);
    free(*other);
    *other = NULL;
}

static InputClientsPtr
AllocInputClient(void)
{
    return calloc(1, sizeof(InputClients));
}

int
AddExtensionClient(WindowPtr pWin, ClientPtr client, Mask mask, int mskidx)
{
    InputClientsPtr others;

    if (!pWin->optional && !MakeWindowOptional(pWin))
	return BadAlloc;
    others = AllocInputClient();
    if (!others)
	return BadAlloc;
    if (!pWin->optional->inputMasks && !MakeInputMasks(pWin))
	goto bail;
    others->xi2mask = xi2mask_new();
    if (!others->xi2mask)
        goto bail;
    others->mask[mskidx] = mask;
    others->resource = FakeClientID(client->index);
    others->next = pWin->optional->inputMasks->inputClients;
    pWin->optional->inputMasks->inputClients = others;
    if (!AddResource(others->resource, RT_INPUTCLIENT, (pointer) pWin))
	goto bail;
    return Success;

bail:
    FreeInputClient(&others);
    return BadAlloc;
}

static Bool
MakeInputMasks(WindowPtr pWin)
{
    struct _OtherInputMasks *imasks;

    imasks = calloc(1, sizeof(struct _OtherInputMasks));
    if (!imasks)
	return FALSE;
    imasks->xi2mask = xi2mask_new();
    if (!imasks->xi2mask)
    {
        free(imasks);
        return FALSE;
    }
    pWin->optional->inputMasks = imasks;
    return TRUE;
}

static void
FreeInputMask(OtherInputMasks **imask)
{
    xi2mask_free(&(*imask)->xi2mask);
    free(*imask);
    *imask = NULL;
}

void
RecalculateDeviceDeliverableEvents(WindowPtr pWin)
{
    InputClientsPtr others;
    struct _OtherInputMasks *inputMasks;	/* default: NULL */
    WindowPtr pChild, tmp;
    int i;

    pChild = pWin;
    while (1) {
	if ((inputMasks = wOtherInputMasks(pChild)) != 0) {
            xi2mask_zero(inputMasks->xi2mask, -1);
	    for (others = inputMasks->inputClients; others;
		 others = others->next) {
		for (i = 0; i < EMASKSIZE; i++)
		    inputMasks->inputEvents[i] |= others->mask[i];
                xi2mask_merge(inputMasks->xi2mask, others->xi2mask);
	    }
	    for (i = 0; i < EMASKSIZE; i++)
		inputMasks->deliverableEvents[i] = inputMasks->inputEvents[i];
	    for (tmp = pChild->parent; tmp; tmp = tmp->parent)
		if (wOtherInputMasks(tmp))
		    for (i = 0; i < EMASKSIZE; i++)
			inputMasks->deliverableEvents[i] |=
			    (wOtherInputMasks(tmp)->deliverableEvents[i]
			     & ~inputMasks->
			     dontPropagateMask[i] & PropagateMask[i]);
	}
	if (pChild->firstChild) {
	    pChild = pChild->firstChild;
	    continue;
	}
	while (!pChild->nextSib && (pChild != pWin))
	    pChild = pChild->parent;
	if (pChild == pWin)
	    break;
	pChild = pChild->nextSib;
    }
}

int
InputClientGone(WindowPtr pWin, XID id)
{
    InputClientsPtr other, prev;

    if (!wOtherInputMasks(pWin))
	return Success;
    prev = 0;
    for (other = wOtherInputMasks(pWin)->inputClients; other;
	 other = other->next) {
	if (other->resource == id) {
	    if (prev) {
		prev->next = other->next;
		FreeInputClient(&other);
	    } else if (!(other->next)) {
		if (ShouldFreeInputMasks(pWin, TRUE)) {
		    OtherInputMasks *mask = wOtherInputMasks(pWin);
		    mask->inputClients = other->next;
		    FreeInputMask(&mask);
		    pWin->optional->inputMasks = (OtherInputMasks *) NULL;
		    CheckWindowOptionalNeed(pWin);
		    FreeInputClient(&other);
		} else {
		    other->resource = FakeClientID(0);
		    if (!AddResource(other->resource, RT_INPUTCLIENT,
				     (pointer) pWin))
			return BadAlloc;
		}
	    } else {
		wOtherInputMasks(pWin)->inputClients = other->next;
		FreeInputClient(&other);
	    }
	    RecalculateDeviceDeliverableEvents(pWin);
	    return Success;
	}
	prev = other;
    }
    FatalError("client not on device event list");
}

int
SendEvent(ClientPtr client, DeviceIntPtr d, Window dest, Bool propagate,
	  xEvent * ev, Mask mask, int count)
{
    WindowPtr pWin;
    WindowPtr effectiveFocus = NullWindow;	/* only set if dest==InputFocus */
    WindowPtr spriteWin = GetSpriteWindow(d);

    if (dest == PointerWindow)
	pWin = spriteWin;
    else if (dest == InputFocus) {
	WindowPtr inputFocus;

	if (!d->focus)
	    inputFocus = spriteWin;
	else
	    inputFocus = d->focus->win;

	if (inputFocus == FollowKeyboardWin)
	    inputFocus = inputInfo.keyboard->focus->win;

	if (inputFocus == NoneWin)
	    return Success;

	/* If the input focus is PointerRootWin, send the event to where
	 * the pointer is if possible, then perhaps propogate up to root. */
	if (inputFocus == PointerRootWin)
	    inputFocus = GetCurrentRootWindow(d);

	if (IsParent(inputFocus, spriteWin)) {
	    effectiveFocus = inputFocus;
	    pWin = spriteWin;
	} else
	    effectiveFocus = pWin = inputFocus;
    } else
	dixLookupWindow(&pWin, dest, client, DixSendAccess);
    if (!pWin)
	return BadWindow;
    if ((propagate != xFalse) && (propagate != xTrue)) {
	client->errorValue = propagate;
	return BadValue;
    }
    ev->u.u.type |= 0x80;
    if (propagate) {
	for (; pWin; pWin = pWin->parent) {
	    if (DeliverEventsToWindow(d, pWin, ev, count, mask, NullGrab))
		return Success;
	    if (pWin == effectiveFocus)
		return Success;
	    if (wOtherInputMasks(pWin))
		mask &= ~wOtherInputMasks(pWin)->dontPropagateMask[d->id];
	    if (!mask)
		break;
	}
    } else if (!XaceHook(XACE_SEND_ACCESS, client, NULL, pWin, ev, count))
	DeliverEventsToWindow(d, pWin, ev, count, mask, NullGrab);
    return Success;
}

int
SetButtonMapping(ClientPtr client, DeviceIntPtr dev, int nElts, BYTE * map)
{
    int i;
    ButtonClassPtr b = dev->button;

    if (b == NULL)
	return BadMatch;

    if (nElts != b->numButtons) {
	client->errorValue = nElts;
	return BadValue;
    }
    if (BadDeviceMap(&map[0], nElts, 1, 255, &client->errorValue))
	return BadValue;
    for (i = 0; i < nElts; i++)
	if ((b->map[i + 1] != map[i]) && BitIsOn(b->down, i + 1))
	    return MappingBusy;
    for (i = 0; i < nElts; i++)
	b->map[i + 1] = map[i];
    return Success;
}

int
ChangeKeyMapping(ClientPtr client,
		 DeviceIntPtr dev,
		 unsigned len,
		 int type,
		 KeyCode firstKeyCode,
		 CARD8 keyCodes, CARD8 keySymsPerKeyCode, KeySym * map)
{
    KeySymsRec keysyms;
    KeyClassPtr k = dev->key;

    if (k == NULL)
	return BadMatch;

    if (len != (keyCodes * keySymsPerKeyCode))
	return BadLength;

    if ((firstKeyCode < k->xkbInfo->desc->min_key_code) ||
	(firstKeyCode + keyCodes - 1 > k->xkbInfo->desc->max_key_code)) {
	client->errorValue = firstKeyCode;
	return BadValue;
    }
    if (keySymsPerKeyCode == 0) {
	client->errorValue = 0;
	return BadValue;
    }
    keysyms.minKeyCode = firstKeyCode;
    keysyms.maxKeyCode = firstKeyCode + keyCodes - 1;
    keysyms.mapWidth = keySymsPerKeyCode;
    keysyms.map = map;

    XkbApplyMappingChange(dev, &keysyms, firstKeyCode, keyCodes, NULL,
                          serverClient);

    return Success;
}

static void
DeleteDeviceFromAnyExtEvents(WindowPtr pWin, DeviceIntPtr dev)
{
    WindowPtr parent;

    /* Deactivate any grabs performed on this window, before making
     * any input focus changes.
     * Deactivating a device grab should cause focus events. */

    if (dev->deviceGrab.grab && (dev->deviceGrab.grab->window == pWin))
	(*dev->deviceGrab.DeactivateGrab) (dev);

    /* If the focus window is a root window (ie. has no parent)
     * then don't delete the focus from it. */

    if (dev->focus && (pWin == dev->focus->win) && (pWin->parent != NullWindow)) {
	int focusEventMode = NotifyNormal;

	/* If a grab is in progress, then alter the mode of focus events. */

	if (dev->deviceGrab.grab)
	    focusEventMode = NotifyWhileGrabbed;

	switch (dev->focus->revert) {
	case RevertToNone:
	    if (!ActivateFocusInGrab(dev, pWin, NoneWin))
		DoFocusEvents(dev, pWin, NoneWin, focusEventMode);
	    dev->focus->win = NoneWin;
	    dev->focus->traceGood = 0;
	    break;
	case RevertToParent:
	    parent = pWin;
	    do {
		parent = parent->parent;
		dev->focus->traceGood--;
	    }
	    while (!parent->realized);
	    if (!ActivateFocusInGrab(dev, pWin, parent))
		DoFocusEvents(dev, pWin, parent, focusEventMode);
	    dev->focus->win = parent;
	    dev->focus->revert = RevertToNone;
	    break;
	case RevertToPointerRoot:
	    if (!ActivateFocusInGrab(dev, pWin, PointerRootWin))
		DoFocusEvents(dev, pWin, PointerRootWin, focusEventMode);
	    dev->focus->win = PointerRootWin;
	    dev->focus->traceGood = 0;
	    break;
	case RevertToFollowKeyboard:
            {
                DeviceIntPtr kbd = GetMaster(dev, MASTER_KEYBOARD);
                if (!kbd || (kbd == dev && kbd != inputInfo.keyboard))
                    kbd = inputInfo.keyboard;
	    if (kbd->focus->win) {
		if (!ActivateFocusInGrab(dev, pWin, kbd->focus->win))
		    DoFocusEvents(dev, pWin, kbd->focus->win, focusEventMode);
		dev->focus->win = FollowKeyboardWin;
		dev->focus->traceGood = 0;
	    } else {
                if (!ActivateFocusInGrab(dev, pWin, NoneWin))
                    DoFocusEvents(dev, pWin, NoneWin, focusEventMode);
		dev->focus->win = NoneWin;
		dev->focus->traceGood = 0;
	    }
            }
	    break;
	}
    }

    if (dev->valuator)
	if (dev->valuator->motionHintWindow == pWin)
	    dev->valuator->motionHintWindow = NullWindow;
}

void
DeleteWindowFromAnyExtEvents(WindowPtr pWin, Bool freeResources)
{
    int i;
    DeviceIntPtr dev;
    InputClientsPtr ic;
    struct _OtherInputMasks *inputMasks;

    for (dev = inputInfo.devices; dev; dev = dev->next) {
	DeleteDeviceFromAnyExtEvents(pWin, dev);
    }

    for (dev = inputInfo.off_devices; dev; dev = dev->next)
	DeleteDeviceFromAnyExtEvents(pWin, dev);

    if (freeResources)
	while ((inputMasks = wOtherInputMasks(pWin)) != 0) {
	    ic = inputMasks->inputClients;
	    for (i = 0; i < EMASKSIZE; i++)
		inputMasks->dontPropagateMask[i] = 0;
	    FreeResource(ic->resource, RT_NONE);
	}
}

int
MaybeSendDeviceMotionNotifyHint(deviceKeyButtonPointer * pEvents, Mask mask)
{
    DeviceIntPtr dev;

    dixLookupDevice(&dev, pEvents->deviceid & DEVICE_BITS, serverClient,
		    DixReadAccess);
    if (!dev)
        return 0;

    if (pEvents->type == DeviceMotionNotify) {
	if (mask & DevicePointerMotionHintMask) {
	    if (WID(dev->valuator->motionHintWindow) == pEvents->event) {
		return 1;	/* don't send, but pretend we did */
	    }
	    pEvents->detail = NotifyHint;
	} else {
	    pEvents->detail = NotifyNormal;
	}
    }
    return 0;
}

void
CheckDeviceGrabAndHintWindow(WindowPtr pWin, int type,
			     deviceKeyButtonPointer * xE, GrabPtr grab,
			     ClientPtr client, Mask deliveryMask)
{
    DeviceIntPtr dev;

    dixLookupDevice(&dev, xE->deviceid & DEVICE_BITS, serverClient,
		    DixGrabAccess);
    if (!dev)
        return;

    if (type == DeviceMotionNotify)
	dev->valuator->motionHintWindow = pWin;
    else if ((type == DeviceButtonPress) && (!grab) &&
	     (deliveryMask & DeviceButtonGrabMask)) {
	GrabPtr tempGrab;

	tempGrab = AllocGrab();
	if (!tempGrab)
	    return;

	tempGrab->device = dev;
	tempGrab->resource = client->clientAsMask;
	tempGrab->window = pWin;
	tempGrab->ownerEvents =
	    (deliveryMask & DeviceOwnerGrabButtonMask) ? TRUE : FALSE;
	tempGrab->eventMask = deliveryMask;
	tempGrab->keyboardMode = GrabModeAsync;
	tempGrab->pointerMode = GrabModeAsync;
	tempGrab->confineTo = NullWindow;
	tempGrab->cursor = NullCursor;
	tempGrab->next = NULL;
	(*dev->deviceGrab.ActivateGrab) (dev, tempGrab, currentTime, TRUE);
	FreeGrab(tempGrab);
    }
}

static Mask
DeviceEventMaskForClient(DeviceIntPtr dev, WindowPtr pWin, ClientPtr client)
{
    InputClientsPtr other;

    if (!wOtherInputMasks(pWin))
	return 0;
    for (other = wOtherInputMasks(pWin)->inputClients; other;
	 other = other->next) {
	if (SameClient(other, client))
	    return other->mask[dev->id];
    }
    return 0;
}

void
MaybeStopDeviceHint(DeviceIntPtr dev, ClientPtr client)
{
    WindowPtr pWin;
    GrabPtr grab = dev->deviceGrab.grab;

    pWin = dev->valuator->motionHintWindow;

    if ((grab && SameClient(grab, client) &&
	 ((grab->eventMask & DevicePointerMotionHintMask) ||
	  (grab->ownerEvents &&
	   (DeviceEventMaskForClient(dev, pWin, client) &
	    DevicePointerMotionHintMask)))) ||
	(!grab &&
	 (DeviceEventMaskForClient(dev, pWin, client) &
	  DevicePointerMotionHintMask)))
	dev->valuator->motionHintWindow = NullWindow;
}

int
DeviceEventSuppressForWindow(WindowPtr pWin, ClientPtr client, Mask mask,
			     int maskndx)
{
    struct _OtherInputMasks *inputMasks = wOtherInputMasks(pWin);

    if (mask & ~PropagateMask[maskndx]) {
	client->errorValue = mask;
	return BadValue;
    }

    if (mask == 0) {
	if (inputMasks)
	    inputMasks->dontPropagateMask[maskndx] = mask;
    } else {
	if (!inputMasks)
	    AddExtensionClient(pWin, client, 0, 0);
	inputMasks = wOtherInputMasks(pWin);
	inputMasks->dontPropagateMask[maskndx] = mask;
    }
    RecalculateDeviceDeliverableEvents(pWin);
    if (ShouldFreeInputMasks(pWin, FALSE))
	FreeResource(inputMasks->inputClients->resource, RT_NONE);
    return Success;
}

Bool
ShouldFreeInputMasks(WindowPtr pWin, Bool ignoreSelectedEvents)
{
    int i;
    Mask allInputEventMasks = 0;
    struct _OtherInputMasks *inputMasks = wOtherInputMasks(pWin);

    for (i = 0; i < EMASKSIZE; i++)
	allInputEventMasks |= inputMasks->dontPropagateMask[i];
    if (!ignoreSelectedEvents)
	for (i = 0; i < EMASKSIZE; i++)
	    allInputEventMasks |= inputMasks->inputEvents[i];
    if (allInputEventMasks == 0)
	return TRUE;
    else
	return FALSE;
}

/***********************************************************************
 *
 * Walk through the window tree, finding all clients that want to know
 * about the Event.
 *
 */

static void
FindInterestedChildren(DeviceIntPtr dev, WindowPtr p1, Mask mask,
                       xEvent * ev, int count)
{
    WindowPtr p2;

    while (p1) {
        p2 = p1->firstChild;
        DeliverEventsToWindow(dev, p1, ev, count, mask, NullGrab);
        FindInterestedChildren(dev, p2, mask, ev, count);
        p1 = p1->nextSib;
    }
}

/***********************************************************************
 *
 * Send an event to interested clients in all windows on all screens.
 *
 */

void
SendEventToAllWindows(DeviceIntPtr dev, Mask mask, xEvent * ev, int count)
{
    int i;
    WindowPtr pWin, p1;

    for (i = 0; i < screenInfo.numScreens; i++) {
        pWin = screenInfo.screens[i]->root;
        if (!pWin)
            continue;
        DeliverEventsToWindow(dev, pWin, ev, count, mask, NullGrab);
        p1 = pWin->firstChild;
        FindInterestedChildren(dev, p1, mask, ev, count);
    }
}

/**
 * Set the XI2 mask for the given client on the given window.
 * @param dev The device to set the mask for.
 * @param win The window to set the mask on.
 * @param client The client setting the mask.
 * @param len Number of bytes in mask.
 * @param mask Event mask in the form of (1 << eventtype)
 */
int
XISetEventMask(DeviceIntPtr dev, WindowPtr win, ClientPtr client,
               unsigned int len, unsigned char* mask)
{
    OtherInputMasks *masks;
    InputClientsPtr others = NULL;

    masks = wOtherInputMasks(win);
    if (masks)
    {
	for (others = wOtherInputMasks(win)->inputClients; others;
	     others = others->next) {
	    if (SameClient(others, client)) {
                xi2mask_zero(others->xi2mask, dev->id);
                break;
            }
        }
    }


    if (len && !others)
    {
        if (AddExtensionClient(win, client, 0, 0) != Success)
            return BadAlloc;
        others= wOtherInputMasks(win)->inputClients;
    }

    if (others) {
        xi2mask_zero(others->xi2mask, dev->id);
        len = min(len, xi2mask_mask_size(others->xi2mask));
    }

    if (len) {
        xi2mask_set_one_mask(others->xi2mask, dev->id, mask, len);
    }

    RecalculateDeviceDeliverableEvents(win);

    return Success;
}
