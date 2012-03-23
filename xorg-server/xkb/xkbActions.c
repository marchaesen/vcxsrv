/************************************************************
Copyright (c) 1993 by Silicon Graphics Computer Systems, Inc.

Permission to use, copy, modify, and distribute this
software and its documentation for any purpose and without
fee is hereby granted, provided that the above copyright
notice appear in all copies and that both that copyright
notice and this permission notice appear in supporting
documentation, and that the name of Silicon Graphics not be 
used in advertising or publicity pertaining to distribution 
of the software without specific prior written permission.
Silicon Graphics makes no representation about the suitability 
of this software for any purpose. It is provided "as is"
without any express or implied warranty.

SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS 
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL 
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, 
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
THE USE OR PERFORMANCE OF THIS SOFTWARE.

********************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdio.h>
#include <math.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include "misc.h"
#include "inputstr.h"
#include "exevents.h"
#include "eventstr.h"
#include <xkbsrv.h>
#include "xkb.h"
#include <ctype.h>
#include "mi.h"
#include "mipointer.h"
#include "inpututils.h"
#define EXTENSION_EVENT_BASE 64

DevPrivateKeyRec xkbDevicePrivateKeyRec;

void XkbFakeDeviceButton(DeviceIntPtr dev, Bool press, int button);
static void XkbFakePointerMotion(DeviceIntPtr dev, unsigned flags, int x,
                                 int y);

void
xkbUnwrapProc(DeviceIntPtr device, DeviceHandleProc proc, pointer data)
{
    xkbDeviceInfoPtr xkbPrivPtr = XKBDEVICEINFO(device);
    ProcessInputProc backupproc;

    if (xkbPrivPtr->unwrapProc)
        xkbPrivPtr->unwrapProc = NULL;

    UNWRAP_PROCESS_INPUT_PROC(device, xkbPrivPtr, backupproc);
    proc(device, data);
    COND_WRAP_PROCESS_INPUT_PROC(device, xkbPrivPtr, backupproc, xkbUnwrapProc);
}

Bool
XkbInitPrivates(void)
{
    return dixRegisterPrivateKey(&xkbDevicePrivateKeyRec, PRIVATE_DEVICE,
                                 sizeof(xkbDeviceInfoRec));
}

void
XkbSetExtension(DeviceIntPtr device, ProcessInputProc proc)
{
    xkbDeviceInfoPtr xkbPrivPtr = XKBDEVICEINFO(device);

    WRAP_PROCESS_INPUT_PROC(device, xkbPrivPtr, proc, xkbUnwrapProc);
}

/***====================================================================***/

static XkbAction
_FixUpAction(XkbDescPtr xkb, XkbAction *act)
{
    static XkbAction fake;

    if (XkbIsPtrAction(act) &&
        (!(xkb->ctrls->enabled_ctrls & XkbMouseKeysMask))) {
        fake.type = XkbSA_NoAction;
        return fake;
    }
    if (xkb->ctrls->enabled_ctrls & XkbStickyKeysMask) {
        if (act->any.type == XkbSA_SetMods) {
            fake.mods.type = XkbSA_LatchMods;
            fake.mods.mask = act->mods.mask;
            if (XkbAX_NeedOption(xkb->ctrls, XkbAX_LatchToLockMask))
                fake.mods.flags = XkbSA_ClearLocks | XkbSA_LatchToLock;
            else
                fake.mods.flags = XkbSA_ClearLocks;
            return fake;
        }
        if (act->any.type == XkbSA_SetGroup) {
            fake.group.type = XkbSA_LatchGroup;
            if (XkbAX_NeedOption(xkb->ctrls, XkbAX_LatchToLockMask))
                fake.group.flags = XkbSA_ClearLocks | XkbSA_LatchToLock;
            else
                fake.group.flags = XkbSA_ClearLocks;
            XkbSASetGroup(&fake.group, XkbSAGroup(&act->group));
            return fake;
        }
    }
    return *act;
}

static XkbAction
XkbGetKeyAction(XkbSrvInfoPtr xkbi, XkbStatePtr xkbState, CARD8 key)
{
    int effectiveGroup;
    int col;
    XkbDescPtr xkb;
    XkbKeyTypePtr type;
    XkbAction *pActs;
    static XkbAction fake;

    xkb = xkbi->desc;
    if (!XkbKeyHasActions(xkb, key) || !XkbKeycodeInRange(xkb, key)) {
        fake.type = XkbSA_NoAction;
        return fake;
    }
    pActs = XkbKeyActionsPtr(xkb, key);
    col = 0;

    effectiveGroup = XkbGetEffectiveGroup(xkbi, xkbState, key);
    if (effectiveGroup != XkbGroup1Index)
        col += (effectiveGroup * XkbKeyGroupsWidth(xkb, key));

    type = XkbKeyKeyType(xkb, key, effectiveGroup);
    if (type->map != NULL) {
        register unsigned i, mods;
        register XkbKTMapEntryPtr entry;

        mods = xkbState->mods & type->mods.mask;
        for (entry = type->map, i = 0; i < type->map_count; i++, entry++) {
            if ((entry->active) && (entry->mods.mask == mods)) {
                col += entry->level;
                break;
            }
        }
    }
    if (pActs[col].any.type == XkbSA_NoAction)
        return pActs[col];
    fake = _FixUpAction(xkb, &pActs[col]);
    return fake;
}

static XkbAction
XkbGetButtonAction(DeviceIntPtr kbd, DeviceIntPtr dev, int button)
{
    XkbAction fake;

    if ((dev->button) && (dev->button->xkb_acts)) {
        if (dev->button->xkb_acts[button - 1].any.type != XkbSA_NoAction) {
            fake = _FixUpAction(kbd->key->xkbInfo->desc,
                                &dev->button->xkb_acts[button - 1]);
            return fake;
        }
    }
    fake.any.type = XkbSA_NoAction;
    return fake;
}

/***====================================================================***/

#define	SYNTHETIC_KEYCODE	1
#define	BTN_ACT_FLAG		0x100

static int
_XkbFilterSetState(XkbSrvInfoPtr xkbi,
                   XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = ((pAction->mods.mask & XkbSA_ClearLocks) != 0);
        filter->priv = 0;
        filter->filter = _XkbFilterSetState;
        if (pAction->type == XkbSA_SetMods) {
            filter->upAction = *pAction;
            xkbi->setMods = pAction->mods.mask;
        }
        else {
            xkbi->groupChange = XkbSAGroup(&pAction->group);
            if (pAction->group.flags & XkbSA_GroupAbsolute)
                xkbi->groupChange -= xkbi->state.base_group;
            filter->upAction = *pAction;
            XkbSASetGroup(&filter->upAction.group, xkbi->groupChange);
        }
    }
    else if (filter->keycode == keycode) {
        if (filter->upAction.type == XkbSA_SetMods) {
            xkbi->clearMods = filter->upAction.mods.mask;
            if (filter->upAction.mods.flags & XkbSA_ClearLocks) {
                xkbi->state.locked_mods &= ~filter->upAction.mods.mask;
            }
        }
        else {
            if (filter->upAction.group.flags & XkbSA_ClearLocks) {
                xkbi->state.locked_group = 0;
            }
            xkbi->groupChange = -XkbSAGroup(&filter->upAction.group);
        }
        filter->active = 0;
    }
    else {
        filter->upAction.mods.flags &= ~XkbSA_ClearLocks;
        filter->filterOthers = 0;
    }
    return 1;
}

#define	LATCH_KEY_DOWN	1
#define	LATCH_PENDING	2
#define	NO_LATCH	3

static int
_XkbFilterLatchState(XkbSrvInfoPtr xkbi,
                     XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{

    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 1;
        filter->priv = LATCH_KEY_DOWN;
        filter->filter = _XkbFilterLatchState;
        if (pAction->type == XkbSA_LatchMods) {
            filter->upAction = *pAction;
            xkbi->setMods = pAction->mods.mask;
        }
        else {
            xkbi->groupChange = XkbSAGroup(&pAction->group);
            if (pAction->group.flags & XkbSA_GroupAbsolute)
                xkbi->groupChange -= xkbi->state.base_group;
            filter->upAction = *pAction;
            XkbSASetGroup(&filter->upAction.group, xkbi->groupChange);
        }
    }
    else if (pAction && (filter->priv == LATCH_PENDING)) {
        if (((1 << pAction->type) & XkbSA_BreakLatch) != 0) {
            filter->active = 0;
            if (filter->upAction.type == XkbSA_LatchMods)
                xkbi->state.latched_mods &= ~filter->upAction.mods.mask;
            else
                xkbi->state.latched_group -=
                    XkbSAGroup(&filter->upAction.group);
        }
        else if ((pAction->type == filter->upAction.type) &&
                 (pAction->mods.flags == filter->upAction.mods.flags) &&
                 (pAction->mods.mask == filter->upAction.mods.mask)) {
            if (filter->upAction.mods.flags & XkbSA_LatchToLock) {
                XkbControlsPtr ctrls = xkbi->desc->ctrls;

                if (filter->upAction.type == XkbSA_LatchMods)
                    pAction->mods.type = XkbSA_LockMods;
                else
                    pAction->group.type = XkbSA_LockGroup;
                if (XkbAX_NeedFeedback(ctrls, XkbAX_StickyKeysFBMask) &&
                    (ctrls->enabled_ctrls & XkbStickyKeysMask)) {
                    XkbDDXAccessXBeep(xkbi->device, _BEEP_STICKY_LOCK,
                                      XkbStickyKeysMask);
                }
            }
            else {
                if (filter->upAction.type == XkbSA_LatchMods)
                    pAction->mods.type = XkbSA_SetMods;
                else
                    pAction->group.type = XkbSA_SetGroup;
            }
            if (filter->upAction.type == XkbSA_LatchMods)
                xkbi->state.latched_mods &= ~filter->upAction.mods.mask;
            else
                xkbi->state.latched_group -=
                    XkbSAGroup(&filter->upAction.group);
            filter->active = 0;
        }
    }
    else if (filter->keycode == keycode) {      /* release */
        XkbControlsPtr ctrls = xkbi->desc->ctrls;
        int needBeep;
        int beepType = _BEEP_NONE;

        needBeep = ((ctrls->enabled_ctrls & XkbStickyKeysMask) &&
                    XkbAX_NeedFeedback(ctrls, XkbAX_StickyKeysFBMask));
        if (filter->upAction.type == XkbSA_LatchMods) {
            xkbi->clearMods = filter->upAction.mods.mask;
            if ((filter->upAction.mods.flags & XkbSA_ClearLocks) &&
                (xkbi->clearMods & xkbi->state.locked_mods) ==
                xkbi->clearMods) {
                xkbi->state.locked_mods &= ~xkbi->clearMods;
                filter->priv = NO_LATCH;
                beepType = _BEEP_STICKY_UNLOCK;
            }
        }
        else {
            xkbi->groupChange = -XkbSAGroup(&filter->upAction.group);
            if ((filter->upAction.group.flags & XkbSA_ClearLocks) &&
                (xkbi->state.locked_group)) {
                xkbi->state.locked_group = 0;
                filter->priv = NO_LATCH;
                beepType = _BEEP_STICKY_UNLOCK;
            }
        }
        if (filter->priv == NO_LATCH) {
            filter->active = 0;
        }
        else {
            filter->priv = LATCH_PENDING;
            if (filter->upAction.type == XkbSA_LatchMods) {
                xkbi->state.latched_mods |= filter->upAction.mods.mask;
                needBeep = xkbi->state.latched_mods ? needBeep : 0;
                xkbi->state.latched_mods |= filter->upAction.mods.mask;
            }
            else {
                xkbi->state.latched_group +=
                    XkbSAGroup(&filter->upAction.group);
            }
            if (needBeep && (beepType == _BEEP_NONE))
                beepType = _BEEP_STICKY_LATCH;
        }
        if (needBeep && (beepType != _BEEP_NONE))
            XkbDDXAccessXBeep(xkbi->device, beepType, XkbStickyKeysMask);
    }
    else if (filter->priv == LATCH_KEY_DOWN) {
        filter->priv = NO_LATCH;
        filter->filterOthers = 0;
    }
    return 1;
}

static int
_XkbFilterLockState(XkbSrvInfoPtr xkbi,
                    XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    if (pAction && (pAction->type == XkbSA_LockGroup)) {
        if (pAction->group.flags & XkbSA_GroupAbsolute)
            xkbi->state.locked_group = XkbSAGroup(&pAction->group);
        else
            xkbi->state.locked_group += XkbSAGroup(&pAction->group);
        return 1;
    }
    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->priv = xkbi->state.locked_mods & pAction->mods.mask;
        filter->filter = _XkbFilterLockState;
        filter->upAction = *pAction;
        if (!(filter->upAction.mods.flags & XkbSA_LockNoLock))
            xkbi->state.locked_mods |= pAction->mods.mask;
        xkbi->setMods = pAction->mods.mask;
    }
    else if (filter->keycode == keycode) {
        filter->active = 0;
        xkbi->clearMods = filter->upAction.mods.mask;
        if (!(filter->upAction.mods.flags & XkbSA_LockNoUnlock))
            xkbi->state.locked_mods &= ~filter->priv;
    }
    return 1;
}

#define	ISO_KEY_DOWN		0
#define	NO_ISO_LOCK		1

static int
_XkbFilterISOLock(XkbSrvInfoPtr xkbi,
                  XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{

    if (filter->keycode == 0) { /* initial press */
        CARD8 flags = pAction->iso.flags;

        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 1;
        filter->priv = ISO_KEY_DOWN;
        filter->upAction = *pAction;
        filter->filter = _XkbFilterISOLock;
        if (flags & XkbSA_ISODfltIsGroup) {
            xkbi->groupChange = XkbSAGroup(&pAction->iso);
            xkbi->setMods = 0;
        }
        else {
            xkbi->setMods = pAction->iso.mask;
            xkbi->groupChange = 0;
        }
        if ((!(flags & XkbSA_ISONoAffectMods)) && (xkbi->state.base_mods)) {
            filter->priv = NO_ISO_LOCK;
            xkbi->state.locked_mods ^= xkbi->state.base_mods;
        }
        if ((!(flags & XkbSA_ISONoAffectGroup)) && (xkbi->state.base_group)) {
/* 6/22/93 (ef) -- lock groups if group key is down first */
        }
        if (!(flags & XkbSA_ISONoAffectPtr)) {
/* 6/22/93 (ef) -- lock mouse buttons if they're down */
        }
    }
    else if (filter->keycode == keycode) {
        CARD8 flags = filter->upAction.iso.flags;

        if (flags & XkbSA_ISODfltIsGroup) {
            xkbi->groupChange = -XkbSAGroup(&filter->upAction.iso);
            xkbi->clearMods = 0;
            if (filter->priv == ISO_KEY_DOWN)
                xkbi->state.locked_group += XkbSAGroup(&filter->upAction.iso);
        }
        else {
            xkbi->clearMods = filter->upAction.iso.mask;
            xkbi->groupChange = 0;
            if (filter->priv == ISO_KEY_DOWN)
                xkbi->state.locked_mods ^= filter->upAction.iso.mask;
        }
        filter->active = 0;
    }
    else if (pAction) {
        CARD8 flags = filter->upAction.iso.flags;

        switch (pAction->type) {
        case XkbSA_SetMods:
        case XkbSA_LatchMods:
            if (!(flags & XkbSA_ISONoAffectMods)) {
                pAction->type = XkbSA_LockMods;
                filter->priv = NO_ISO_LOCK;
            }
            break;
        case XkbSA_SetGroup:
        case XkbSA_LatchGroup:
            if (!(flags & XkbSA_ISONoAffectGroup)) {
                pAction->type = XkbSA_LockGroup;
                filter->priv = NO_ISO_LOCK;
            }
            break;
        case XkbSA_PtrBtn:
            if (!(flags & XkbSA_ISONoAffectPtr)) {
                pAction->type = XkbSA_LockPtrBtn;
                filter->priv = NO_ISO_LOCK;
            }
            break;
        case XkbSA_SetControls:
            if (!(flags & XkbSA_ISONoAffectCtrls)) {
                pAction->type = XkbSA_LockControls;
                filter->priv = NO_ISO_LOCK;
            }
            break;
        }
    }
    return 1;
}

static CARD32
_XkbPtrAccelExpire(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XkbSrvInfoPtr xkbi = (XkbSrvInfoPtr) arg;
    XkbControlsPtr ctrls = xkbi->desc->ctrls;
    int dx, dy;

    if (xkbi->mouseKey == 0)
        return 0;

    if (xkbi->mouseKeysAccel) {
        if ((xkbi->mouseKeysCounter) < ctrls->mk_time_to_max) {
            double step;

            xkbi->mouseKeysCounter++;
            step = xkbi->mouseKeysCurveFactor *
                pow((double) xkbi->mouseKeysCounter, xkbi->mouseKeysCurve);
            if (xkbi->mouseKeysDX < 0)
                dx = floor(((double) xkbi->mouseKeysDX) * step);
            else
                dx = ceil(((double) xkbi->mouseKeysDX) * step);
            if (xkbi->mouseKeysDY < 0)
                dy = floor(((double) xkbi->mouseKeysDY) * step);
            else
                dy = ceil(((double) xkbi->mouseKeysDY) * step);
        }
        else {
            dx = xkbi->mouseKeysDX * ctrls->mk_max_speed;
            dy = xkbi->mouseKeysDY * ctrls->mk_max_speed;
        }
        if (xkbi->mouseKeysFlags & XkbSA_MoveAbsoluteX)
            dx = xkbi->mouseKeysDX;
        if (xkbi->mouseKeysFlags & XkbSA_MoveAbsoluteY)
            dy = xkbi->mouseKeysDY;
    }
    else {
        dx = xkbi->mouseKeysDX;
        dy = xkbi->mouseKeysDY;
    }
    XkbFakePointerMotion(xkbi->device, xkbi->mouseKeysFlags, dx, dy);
    return xkbi->desc->ctrls->mk_interval;
}

static int
_XkbFilterPointerMove(XkbSrvInfoPtr xkbi,
                      XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    int x, y;
    Bool accel;

    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->priv = 0;
        filter->filter = _XkbFilterPointerMove;
        filter->upAction = *pAction;
        xkbi->mouseKeysCounter = 0;
        xkbi->mouseKey = keycode;
        accel = ((pAction->ptr.flags & XkbSA_NoAcceleration) == 0);
        x = XkbPtrActionX(&pAction->ptr);
        y = XkbPtrActionY(&pAction->ptr);
        XkbFakePointerMotion(xkbi->device, pAction->ptr.flags, x, y);
        AccessXCancelRepeatKey(xkbi, keycode);
        xkbi->mouseKeysAccel = accel &&
            (xkbi->desc->ctrls->enabled_ctrls & XkbMouseKeysAccelMask);
        xkbi->mouseKeysFlags = pAction->ptr.flags;
        xkbi->mouseKeysDX = XkbPtrActionX(&pAction->ptr);
        xkbi->mouseKeysDY = XkbPtrActionY(&pAction->ptr);
        xkbi->mouseKeyTimer = TimerSet(xkbi->mouseKeyTimer, 0,
                                       xkbi->desc->ctrls->mk_delay,
                                       _XkbPtrAccelExpire, (pointer) xkbi);
    }
    else if (filter->keycode == keycode) {
        filter->active = 0;
        if (xkbi->mouseKey == keycode) {
            xkbi->mouseKey = 0;
            xkbi->mouseKeyTimer = TimerSet(xkbi->mouseKeyTimer, 0, 0,
                                           NULL, NULL);
        }
    }
    return 0;
}

static int
_XkbFilterPointerBtn(XkbSrvInfoPtr xkbi,
                     XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    if (filter->keycode == 0) { /* initial press */
        int button = pAction->btn.button;

        if (button == XkbSA_UseDfltButton)
            button = xkbi->desc->ctrls->mk_dflt_btn;

        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->priv = 0;
        filter->filter = _XkbFilterPointerBtn;
        filter->upAction = *pAction;
        filter->upAction.btn.button = button;
        switch (pAction->type) {
        case XkbSA_LockPtrBtn:
            if (((xkbi->lockedPtrButtons & (1 << button)) == 0) &&
                ((pAction->btn.flags & XkbSA_LockNoLock) == 0)) {
                xkbi->lockedPtrButtons |= (1 << button);
                AccessXCancelRepeatKey(xkbi, keycode);
                XkbFakeDeviceButton(xkbi->device, 1, button);
                filter->upAction.type = XkbSA_NoAction;
            }
            break;
        case XkbSA_PtrBtn:
        {
            register int i, nClicks;

            AccessXCancelRepeatKey(xkbi, keycode);
            if (pAction->btn.count > 0) {
                nClicks = pAction->btn.count;
                for (i = 0; i < nClicks; i++) {
                    XkbFakeDeviceButton(xkbi->device, 1, button);
                    XkbFakeDeviceButton(xkbi->device, 0, button);
                }
                filter->upAction.type = XkbSA_NoAction;
            }
            else
                XkbFakeDeviceButton(xkbi->device, 1, button);
        }
            break;
        case XkbSA_SetPtrDflt:
        {
            XkbControlsPtr ctrls = xkbi->desc->ctrls;
            XkbControlsRec old;
            xkbControlsNotify cn;

            old = *ctrls;
            AccessXCancelRepeatKey(xkbi, keycode);
            switch (pAction->dflt.affect) {
            case XkbSA_AffectDfltBtn:
                if (pAction->dflt.flags & XkbSA_DfltBtnAbsolute)
                    ctrls->mk_dflt_btn = XkbSAPtrDfltValue(&pAction->dflt);
                else {
                    ctrls->mk_dflt_btn += XkbSAPtrDfltValue(&pAction->dflt);
                    if (ctrls->mk_dflt_btn > 5)
                        ctrls->mk_dflt_btn = 5;
                    else if (ctrls->mk_dflt_btn < 1)
                        ctrls->mk_dflt_btn = 1;
                }
                break;
            default:
                ErrorF
                    ("Attempt to change unknown pointer default (%d) ignored\n",
                     pAction->dflt.affect);
                break;
            }
            if (XkbComputeControlsNotify(xkbi->device,
                                         &old, xkbi->desc->ctrls, &cn, FALSE)) {
                cn.keycode = keycode;
                /* XXX: what about DeviceKeyPress? */
                cn.eventType = KeyPress;
                cn.requestMajor = 0;
                cn.requestMinor = 0;
                XkbSendControlsNotify(xkbi->device, &cn);
            }
        }
            break;
        }
    }
    else if (filter->keycode == keycode) {
        int button = filter->upAction.btn.button;

        switch (filter->upAction.type) {
        case XkbSA_LockPtrBtn:
            if (((filter->upAction.btn.flags & XkbSA_LockNoUnlock) != 0) ||
                ((xkbi->lockedPtrButtons & (1 << button)) == 0)) {
                break;
            }
            xkbi->lockedPtrButtons &= ~(1 << button);

            if (IsMaster(xkbi->device)) {
                XkbMergeLockedPtrBtns(xkbi->device);
                /* One SD still has lock set, don't post event */
                if ((xkbi->lockedPtrButtons & (1 << button)) != 0)
                    break;
            }

            /* fallthrough */
        case XkbSA_PtrBtn:
            XkbFakeDeviceButton(xkbi->device, 0, button);
            break;
        }
        filter->active = 0;
    }
    return 0;
}

static int
_XkbFilterControls(XkbSrvInfoPtr xkbi,
                   XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    XkbControlsRec old;
    XkbControlsPtr ctrls;
    DeviceIntPtr kbd;
    unsigned int change;
    XkbEventCauseRec cause;

    kbd = xkbi->device;
    ctrls = xkbi->desc->ctrls;
    old = *ctrls;
    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        change = XkbActionCtrls(&pAction->ctrls);
        filter->priv = change;
        filter->filter = _XkbFilterControls;
        filter->upAction = *pAction;

        if (pAction->type == XkbSA_LockControls) {
            filter->priv = (ctrls->enabled_ctrls & change);
            change &= ~ctrls->enabled_ctrls;
        }

        if (change) {
            xkbControlsNotify cn;
            XkbSrvLedInfoPtr sli;

            ctrls->enabled_ctrls |= change;
            if (XkbComputeControlsNotify(kbd, &old, ctrls, &cn, FALSE)) {
                cn.keycode = keycode;
                /* XXX: what about DeviceKeyPress? */
                cn.eventType = KeyPress;
                cn.requestMajor = 0;
                cn.requestMinor = 0;
                XkbSendControlsNotify(kbd, &cn);
            }

            XkbSetCauseKey(&cause, keycode, KeyPress);

            /* If sticky keys were disabled, clear all locks and latches */
            if ((old.enabled_ctrls & XkbStickyKeysMask) &&
                (!(ctrls->enabled_ctrls & XkbStickyKeysMask))) {
                XkbClearAllLatchesAndLocks(kbd, xkbi, FALSE, &cause);
            }
            sli = XkbFindSrvLedInfo(kbd, XkbDfltXIClass, XkbDfltXIId, 0);
            XkbUpdateIndicators(kbd, sli->usesControls, TRUE, NULL, &cause);
            if (XkbAX_NeedFeedback(ctrls, XkbAX_FeatureFBMask))
                XkbDDXAccessXBeep(kbd, _BEEP_FEATURE_ON, change);
        }
    }
    else if (filter->keycode == keycode) {
        change = filter->priv;
        if (change) {
            xkbControlsNotify cn;
            XkbSrvLedInfoPtr sli;

            ctrls->enabled_ctrls &= ~change;
            if (XkbComputeControlsNotify(kbd, &old, ctrls, &cn, FALSE)) {
                cn.keycode = keycode;
                cn.eventType = KeyRelease;
                cn.requestMajor = 0;
                cn.requestMinor = 0;
                XkbSendControlsNotify(kbd, &cn);
            }

            XkbSetCauseKey(&cause, keycode, KeyRelease);
            /* If sticky keys were disabled, clear all locks and latches */
            if ((old.enabled_ctrls & XkbStickyKeysMask) &&
                (!(ctrls->enabled_ctrls & XkbStickyKeysMask))) {
                XkbClearAllLatchesAndLocks(kbd, xkbi, FALSE, &cause);
            }
            sli = XkbFindSrvLedInfo(kbd, XkbDfltXIClass, XkbDfltXIId, 0);
            XkbUpdateIndicators(kbd, sli->usesControls, TRUE, NULL, &cause);
            if (XkbAX_NeedFeedback(ctrls, XkbAX_FeatureFBMask))
                XkbDDXAccessXBeep(kbd, _BEEP_FEATURE_OFF, change);
        }
        filter->keycode = 0;
        filter->active = 0;
    }
    return 1;
}

static int
_XkbFilterActionMessage(XkbSrvInfoPtr xkbi,
                        XkbFilterPtr filter,
                        unsigned keycode, XkbAction *pAction)
{
    XkbMessageAction *pMsg;
    DeviceIntPtr kbd;

    kbd = xkbi->device;
    if (filter->keycode == 0) { /* initial press */
        pMsg = &pAction->msg;
        if ((pMsg->flags & XkbSA_MessageOnRelease) ||
            ((pMsg->flags & XkbSA_MessageGenKeyEvent) == 0)) {
            filter->keycode = keycode;
            filter->active = 1;
            filter->filterOthers = 0;
            filter->priv = 0;
            filter->filter = _XkbFilterActionMessage;
            filter->upAction = *pAction;
        }
        if (pMsg->flags & XkbSA_MessageOnPress) {
            xkbActionMessage msg;

            msg.keycode = keycode;
            msg.press = 1;
            msg.keyEventFollows =
                ((pMsg->flags & XkbSA_MessageGenKeyEvent) != 0);
            memcpy((char *) msg.message, (char *) pMsg->message,
                   XkbActionMessageLength);
            XkbSendActionMessage(kbd, &msg);
        }
        return ((pAction->msg.flags & XkbSA_MessageGenKeyEvent) != 0);
    }
    else if (filter->keycode == keycode) {
        pMsg = &filter->upAction.msg;
        if (pMsg->flags & XkbSA_MessageOnRelease) {
            xkbActionMessage msg;

            msg.keycode = keycode;
            msg.press = 0;
            msg.keyEventFollows =
                ((pMsg->flags & XkbSA_MessageGenKeyEvent) != 0);
            memcpy((char *) msg.message, (char *) pMsg->message,
                   XkbActionMessageLength);
            XkbSendActionMessage(kbd, &msg);
        }
        filter->keycode = 0;
        filter->active = 0;
        return ((pMsg->flags & XkbSA_MessageGenKeyEvent) != 0);
    }
    return 1;
}

static int
_XkbFilterRedirectKey(XkbSrvInfoPtr xkbi,
                      XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    DeviceEvent ev;
    int x, y;
    XkbStateRec old, old_prev;
    unsigned mods, mask;
    xkbDeviceInfoPtr xkbPrivPtr = XKBDEVICEINFO(xkbi->device);
    ProcessInputProc backupproc;

    /* never actually used uninitialised, but gcc isn't smart enough
     * to work that out. */
    memset(&old, 0, sizeof(old));
    memset(&old_prev, 0, sizeof(old_prev));
    memset(&ev, 0, sizeof(ev));

    if ((filter->keycode != 0) && (filter->keycode != keycode))
        return 1;

    GetSpritePosition(xkbi->device, &x, &y);
    ev.header = ET_Internal;
    ev.length = sizeof(DeviceEvent);
    ev.time = GetTimeInMillis();
    ev.root_x = x;
    ev.root_y = y;
    /* redirect actions do not work across devices, therefore the following is
     * correct: */
    ev.deviceid = xkbi->device->id;
    /* filter->priv must be set up by the caller for the initial press. */
    ev.sourceid = filter->priv;

    if (filter->keycode == 0) { /* initial press */
        if ((pAction->redirect.new_key < xkbi->desc->min_key_code) ||
            (pAction->redirect.new_key > xkbi->desc->max_key_code)) {
            return 1;
        }
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->filter = _XkbFilterRedirectKey;
        filter->upAction = *pAction;

        ev.type = ET_KeyPress;
        ev.detail.key = pAction->redirect.new_key;

        mask = XkbSARedirectVModsMask(&pAction->redirect);
        mods = XkbSARedirectVMods(&pAction->redirect);
        if (mask)
            XkbVirtualModsToReal(xkbi->desc, mask, &mask);
        if (mods)
            XkbVirtualModsToReal(xkbi->desc, mods, &mods);
        mask |= pAction->redirect.mods_mask;
        mods |= pAction->redirect.mods;

        if (mask || mods) {
            old = xkbi->state;
            old_prev = xkbi->prev_state;
            xkbi->state.base_mods &= ~mask;
            xkbi->state.base_mods |= (mods & mask);
            xkbi->state.latched_mods &= ~mask;
            xkbi->state.latched_mods |= (mods & mask);
            xkbi->state.locked_mods &= ~mask;
            xkbi->state.locked_mods |= (mods & mask);
            XkbComputeDerivedState(xkbi);
            xkbi->prev_state = xkbi->state;
        }

        UNWRAP_PROCESS_INPUT_PROC(xkbi->device, xkbPrivPtr, backupproc);
        xkbi->device->public.processInputProc((InternalEvent *) &ev,
                                              xkbi->device);
        COND_WRAP_PROCESS_INPUT_PROC(xkbi->device, xkbPrivPtr, backupproc,
                                     xkbUnwrapProc);

        if (mask || mods) {
            xkbi->state = old;
            xkbi->prev_state = old_prev;
        }
    }
    else if (filter->keycode == keycode) {

        ev.type = ET_KeyRelease;
        ev.detail.key = filter->upAction.redirect.new_key;

        mask = XkbSARedirectVModsMask(&filter->upAction.redirect);
        mods = XkbSARedirectVMods(&filter->upAction.redirect);
        if (mask)
            XkbVirtualModsToReal(xkbi->desc, mask, &mask);
        if (mods)
            XkbVirtualModsToReal(xkbi->desc, mods, &mods);
        mask |= filter->upAction.redirect.mods_mask;
        mods |= filter->upAction.redirect.mods;

        if (mask || mods) {
            old = xkbi->state;
            old_prev = xkbi->prev_state;
            xkbi->state.base_mods &= ~mask;
            xkbi->state.base_mods |= (mods & mask);
            xkbi->state.latched_mods &= ~mask;
            xkbi->state.latched_mods |= (mods & mask);
            xkbi->state.locked_mods &= ~mask;
            xkbi->state.locked_mods |= (mods & mask);
            XkbComputeDerivedState(xkbi);
            xkbi->prev_state = xkbi->state;
        }

        UNWRAP_PROCESS_INPUT_PROC(xkbi->device, xkbPrivPtr, backupproc);
        xkbi->device->public.processInputProc((InternalEvent *) &ev,
                                              xkbi->device);
        COND_WRAP_PROCESS_INPUT_PROC(xkbi->device, xkbPrivPtr, backupproc,
                                     xkbUnwrapProc);

        if (mask || mods) {
            xkbi->state = old;
            xkbi->prev_state = old_prev;
        }

        filter->keycode = 0;
        filter->active = 0;
    }
    return 0;
}

static int
_XkbFilterSwitchScreen(XkbSrvInfoPtr xkbi,
                       XkbFilterPtr filter,
                       unsigned keycode, XkbAction *pAction)
{
    DeviceIntPtr dev = xkbi->device;

    if (dev == inputInfo.keyboard)
        return 0;

    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->filter = _XkbFilterSwitchScreen;
        AccessXCancelRepeatKey(xkbi, keycode);
        XkbDDXSwitchScreen(dev, keycode, pAction);
        return 0;
    }
    else if (filter->keycode == keycode) {
        filter->active = 0;
        return 0;
    }
    return 1;
}

static int
_XkbFilterXF86Private(XkbSrvInfoPtr xkbi,
                      XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    DeviceIntPtr dev = xkbi->device;

    if (dev == inputInfo.keyboard)
        return 0;

    if (filter->keycode == 0) { /* initial press */
        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->filter = _XkbFilterXF86Private;
        XkbDDXPrivate(dev, keycode, pAction);
        return 0;
    }
    else if (filter->keycode == keycode) {
        filter->active = 0;
        return 0;
    }
    return 1;
}

static int
_XkbFilterDeviceBtn(XkbSrvInfoPtr xkbi,
                    XkbFilterPtr filter, unsigned keycode, XkbAction *pAction)
{
    if (xkbi->device == inputInfo.keyboard)
        return 0;

    if (filter->keycode == 0) { /* initial press */
        DeviceIntPtr dev;
        int button;

        _XkbLookupButtonDevice(&dev, pAction->devbtn.device, serverClient,
                               DixUnknownAccess, &button);
        if (!dev || !dev->public.on)
            return 1;

        button = pAction->devbtn.button;
        if ((button < 1) || (button > dev->button->numButtons))
            return 1;

        filter->keycode = keycode;
        filter->active = 1;
        filter->filterOthers = 0;
        filter->priv = 0;
        filter->filter = _XkbFilterDeviceBtn;
        filter->upAction = *pAction;
        switch (pAction->type) {
        case XkbSA_LockDeviceBtn:
            if ((pAction->devbtn.flags & XkbSA_LockNoLock) ||
                BitIsOn(dev->button->down, button))
                return 0;
            XkbFakeDeviceButton(dev, TRUE, button);
            filter->upAction.type = XkbSA_NoAction;
            break;
        case XkbSA_DeviceBtn:
            if (pAction->devbtn.count > 0) {
                int nClicks, i;

                nClicks = pAction->btn.count;
                for (i = 0; i < nClicks; i++) {
                    XkbFakeDeviceButton(dev, TRUE, button);
                    XkbFakeDeviceButton(dev, FALSE, button);
                }
                filter->upAction.type = XkbSA_NoAction;
            }
            else
                XkbFakeDeviceButton(dev, TRUE, button);
            break;
        }
    }
    else if (filter->keycode == keycode) {
        DeviceIntPtr dev;
        int button;

        filter->active = 0;
        _XkbLookupButtonDevice(&dev, filter->upAction.devbtn.device,
                               serverClient, DixUnknownAccess, &button);
        if (!dev || !dev->public.on)
            return 1;

        button = filter->upAction.btn.button;
        switch (filter->upAction.type) {
        case XkbSA_LockDeviceBtn:
            if ((filter->upAction.devbtn.flags & XkbSA_LockNoUnlock) ||
                !BitIsOn(dev->button->down, button))
                return 0;
            XkbFakeDeviceButton(dev, FALSE, button);
            break;
        case XkbSA_DeviceBtn:
            XkbFakeDeviceButton(dev, FALSE, button);
            break;
        }
        filter->active = 0;
    }
    return 0;
}

static XkbFilterPtr
_XkbNextFreeFilter(XkbSrvInfoPtr xkbi)
{
    register int i;

    if (xkbi->szFilters == 0) {
        xkbi->szFilters = 4;
        xkbi->filters = calloc(xkbi->szFilters, sizeof(XkbFilterRec));
        /* 6/21/93 (ef) -- XXX! deal with allocation failure */
    }
    for (i = 0; i < xkbi->szFilters; i++) {
        if (!xkbi->filters[i].active) {
            xkbi->filters[i].keycode = 0;
            return &xkbi->filters[i];
        }
    }
    xkbi->szFilters *= 2;
    xkbi->filters = realloc(xkbi->filters,
                            xkbi->szFilters * sizeof(XkbFilterRec));
    /* 6/21/93 (ef) -- XXX! deal with allocation failure */
    memset(&xkbi->filters[xkbi->szFilters / 2], 0,
           (xkbi->szFilters / 2) * sizeof(XkbFilterRec));
    return &xkbi->filters[xkbi->szFilters / 2];
}

static int
_XkbApplyFilters(XkbSrvInfoPtr xkbi, unsigned kc, XkbAction *pAction)
{
    register int i, send;

    send = 1;
    for (i = 0; i < xkbi->szFilters; i++) {
        if ((xkbi->filters[i].active) && (xkbi->filters[i].filter))
            send =
                ((*xkbi->filters[i].filter) (xkbi, &xkbi->filters[i], kc,
                                             pAction)
                 && send);
    }
    return send;
}

void
XkbHandleActions(DeviceIntPtr dev, DeviceIntPtr kbd, DeviceEvent *event)
{
    int key, bit, i;
    XkbSrvInfoPtr xkbi;
    KeyClassPtr keyc;
    int changed, sendEvent;
    Bool genStateNotify;
    XkbAction act;
    XkbFilterPtr filter;
    Bool keyEvent;
    Bool pressEvent;
    ProcessInputProc backupproc;

    xkbDeviceInfoPtr xkbPrivPtr = XKBDEVICEINFO(dev);

    keyc = kbd->key;
    xkbi = keyc->xkbInfo;
    key = event->detail.key;
    /* The state may change, so if we're not in the middle of sending a state
     * notify, prepare for it */
    if ((xkbi->flags & _XkbStateNotifyInProgress) == 0) {
        xkbi->prev_state = xkbi->state;
        xkbi->flags |= _XkbStateNotifyInProgress;
        genStateNotify = TRUE;
    }
    else
        genStateNotify = FALSE;

    xkbi->clearMods = xkbi->setMods = 0;
    xkbi->groupChange = 0;

    sendEvent = 1;
    keyEvent = ((event->type == ET_KeyPress) || (event->type == ET_KeyRelease));
    pressEvent = ((event->type == ET_KeyPress) ||
                  (event->type == ET_ButtonPress));

    if (pressEvent) {
        if (keyEvent)
            act = XkbGetKeyAction(xkbi, &xkbi->state, key);
        else {
            act = XkbGetButtonAction(kbd, dev, key);
            key |= BTN_ACT_FLAG;
        }
        sendEvent = _XkbApplyFilters(xkbi, key, &act);
        if (sendEvent) {
            switch (act.type) {
            case XkbSA_SetMods:
            case XkbSA_SetGroup:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterSetState(xkbi, filter, key, &act);
                break;
            case XkbSA_LatchMods:
            case XkbSA_LatchGroup:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterLatchState(xkbi, filter, key, &act);
                break;
            case XkbSA_LockMods:
            case XkbSA_LockGroup:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterLockState(xkbi, filter, key, &act);
                break;
            case XkbSA_ISOLock:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterISOLock(xkbi, filter, key, &act);
                break;
            case XkbSA_MovePtr:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterPointerMove(xkbi, filter, key, &act);
                break;
            case XkbSA_PtrBtn:
            case XkbSA_LockPtrBtn:
            case XkbSA_SetPtrDflt:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterPointerBtn(xkbi, filter, key, &act);
                break;
            case XkbSA_Terminate:
                sendEvent = XkbDDXTerminateServer(dev, key, &act);
                break;
            case XkbSA_SwitchScreen:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterSwitchScreen(xkbi, filter, key, &act);
                break;
            case XkbSA_SetControls:
            case XkbSA_LockControls:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterControls(xkbi, filter, key, &act);
                break;
            case XkbSA_ActionMessage:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterActionMessage(xkbi, filter, key, &act);
                break;
            case XkbSA_RedirectKey:
                filter = _XkbNextFreeFilter(xkbi);
                /* redirect actions must create a new DeviceEvent.  The
                 * source device id for this event cannot be obtained from
                 * xkbi, so we pass it here explicitly. The field deviceid
                 * equals to xkbi->device->id. */
                filter->priv = event->sourceid;
                sendEvent = _XkbFilterRedirectKey(xkbi, filter, key, &act);
                break;
            case XkbSA_DeviceBtn:
            case XkbSA_LockDeviceBtn:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterDeviceBtn(xkbi, filter, key, &act);
                break;
            case XkbSA_XFree86Private:
                filter = _XkbNextFreeFilter(xkbi);
                sendEvent = _XkbFilterXF86Private(xkbi, filter, key, &act);
                break;
            }
        }
    }
    else {
        if (!keyEvent)
            key |= BTN_ACT_FLAG;
        sendEvent = _XkbApplyFilters(xkbi, key, NULL);
    }

    if (xkbi->groupChange != 0)
        xkbi->state.base_group += xkbi->groupChange;
    if (xkbi->setMods) {
        for (i = 0, bit = 1; xkbi->setMods; i++, bit <<= 1) {
            if (xkbi->setMods & bit) {
                keyc->modifierKeyCount[i]++;
                xkbi->state.base_mods |= bit;
                xkbi->setMods &= ~bit;
            }
        }
    }
    if (xkbi->clearMods) {
        for (i = 0, bit = 1; xkbi->clearMods; i++, bit <<= 1) {
            if (xkbi->clearMods & bit) {
                keyc->modifierKeyCount[i]--;
                if (keyc->modifierKeyCount[i] <= 0) {
                    xkbi->state.base_mods &= ~bit;
                    keyc->modifierKeyCount[i] = 0;
                }
                xkbi->clearMods &= ~bit;
            }
        }
    }

    if (sendEvent) {
        DeviceIntPtr tmpdev;

        if (keyEvent)
            tmpdev = dev;
        else
            tmpdev = GetMaster(dev, POINTER_OR_FLOAT);

        UNWRAP_PROCESS_INPUT_PROC(tmpdev, xkbPrivPtr, backupproc);
        dev->public.processInputProc((InternalEvent *) event, tmpdev);
        COND_WRAP_PROCESS_INPUT_PROC(tmpdev, xkbPrivPtr,
                                     backupproc, xkbUnwrapProc);
    }
    else if (keyEvent) {
        FixKeyState(event, dev);
    }

    XkbComputeDerivedState(xkbi);
    changed = XkbStateChangedFlags(&xkbi->prev_state, &xkbi->state);
    if (genStateNotify) {
        if (changed) {
            xkbStateNotify sn;

            sn.keycode = key;
            sn.eventType = event->type;
            sn.requestMajor = sn.requestMinor = 0;
            sn.changed = changed;
            XkbSendStateNotify(dev, &sn);
        }
        xkbi->flags &= ~_XkbStateNotifyInProgress;
    }
    changed = XkbIndicatorsToUpdate(dev, changed, FALSE);
    if (changed) {
        XkbEventCauseRec cause;

        XkbSetCauseKey(&cause, key, event->type);
        XkbUpdateIndicators(dev, changed, FALSE, NULL, &cause);
    }
    return;
}

int
XkbLatchModifiers(DeviceIntPtr pXDev, CARD8 mask, CARD8 latches)
{
    XkbSrvInfoPtr xkbi;
    XkbFilterPtr filter;
    XkbAction act;
    unsigned clear;

    if (pXDev && pXDev->key && pXDev->key->xkbInfo) {
        xkbi = pXDev->key->xkbInfo;
        clear = (mask & (~latches));
        xkbi->state.latched_mods &= ~clear;
        /* Clear any pending latch to locks.
         */
        act.type = XkbSA_NoAction;
        _XkbApplyFilters(xkbi, SYNTHETIC_KEYCODE, &act);
        act.type = XkbSA_LatchMods;
        act.mods.flags = 0;
        act.mods.mask = mask & latches;
        filter = _XkbNextFreeFilter(xkbi);
        _XkbFilterLatchState(xkbi, filter, SYNTHETIC_KEYCODE, &act);
        _XkbFilterLatchState(xkbi, filter, SYNTHETIC_KEYCODE,
                             (XkbAction *) NULL);
        return Success;
    }
    return BadValue;
}

int
XkbLatchGroup(DeviceIntPtr pXDev, int group)
{
    XkbSrvInfoPtr xkbi;
    XkbFilterPtr filter;
    XkbAction act;

    if (pXDev && pXDev->key && pXDev->key->xkbInfo) {
        xkbi = pXDev->key->xkbInfo;
        act.type = XkbSA_LatchGroup;
        act.group.flags = 0;
        XkbSASetGroup(&act.group, group);
        filter = _XkbNextFreeFilter(xkbi);
        _XkbFilterLatchState(xkbi, filter, SYNTHETIC_KEYCODE, &act);
        _XkbFilterLatchState(xkbi, filter, SYNTHETIC_KEYCODE,
                             (XkbAction *) NULL);
        return Success;
    }
    return BadValue;
}

/***====================================================================***/

void
XkbClearAllLatchesAndLocks(DeviceIntPtr dev,
                           XkbSrvInfoPtr xkbi,
                           Bool genEv, XkbEventCausePtr cause)
{
    XkbStateRec os;
    xkbStateNotify sn;

    sn.changed = 0;
    os = xkbi->state;
    if (os.latched_mods) {      /* clear all latches */
        XkbLatchModifiers(dev, ~0, 0);
        sn.changed |= XkbModifierLatchMask;
    }
    if (os.latched_group) {
        XkbLatchGroup(dev, 0);
        sn.changed |= XkbGroupLatchMask;
    }
    if (os.locked_mods) {
        xkbi->state.locked_mods = 0;
        sn.changed |= XkbModifierLockMask;
    }
    if (os.locked_group) {
        xkbi->state.locked_group = 0;
        sn.changed |= XkbGroupLockMask;
    }
    if (genEv && sn.changed) {
        CARD32 changed;

        XkbComputeDerivedState(xkbi);
        sn.keycode = cause->kc;
        sn.eventType = cause->event;
        sn.requestMajor = cause->mjr;
        sn.requestMinor = cause->mnr;
        sn.changed = XkbStateChangedFlags(&os, &xkbi->state);
        XkbSendStateNotify(dev, &sn);
        changed = XkbIndicatorsToUpdate(dev, sn.changed, FALSE);
        if (changed) {
            XkbUpdateIndicators(dev, changed, TRUE, NULL, cause);
        }
    }
    return;
}

/*
 * The event is injected into the event processing, not the EQ. Thus,
 * ensure that we restore the master after the event sequence to the
 * original set of classes. Otherwise, the master remains on the XTEST
 * classes and drops events that don't fit into the XTEST layout (e.g.
 * events with more than 2 valuators).
 *
 * FIXME: EQ injection in the processing stage is not designed for, so this
 * is a rather awkward hack. The event list returned by GetPointerEvents()
 * and friends is always prefixed with a DCE if the last _posted_ device was
 * different. For normal events, this sequence then resets the master during
 * the processing stage. Since we inject the PointerKey events in the
 * processing stage though, we need to manually reset to restore the
 * previous order, because the events already in the EQ must be sent for the
 * right device.
 * So we post-fix the event list we get from GPE with a DCE back to the
 * previous slave device.
 *
 * First one on drinking island wins!
 */
static void
InjectPointerKeyEvents(DeviceIntPtr dev, int type, int button, int flags,
                       ValuatorMask *mask)
{
    ScreenPtr pScreen;
    InternalEvent *events;
    int nevents, i;
    DeviceIntPtr ptr, mpointer, lastSlave = NULL;
    Bool saveWait;

    if (IsMaster(dev)) {
        mpointer = GetMaster(dev, MASTER_POINTER);
        lastSlave = mpointer->lastSlave;
        ptr = GetXTestDevice(mpointer);
    }
    else if (IsFloating(dev))
        ptr = dev;
    else
        return;

    events = InitEventList(GetMaximumEventsNum() + 1);
    OsBlockSignals();
    pScreen = miPointerGetScreen(ptr);
    saveWait = miPointerSetWaitForUpdate(pScreen, FALSE);
    nevents = GetPointerEvents(events, ptr, type, button, flags, mask);
    if (IsMaster(dev) && (lastSlave && lastSlave != ptr))
        UpdateFromMaster(&events[nevents], lastSlave, DEVCHANGE_POINTER_EVENT,
                         &nevents);
    miPointerSetWaitForUpdate(pScreen, saveWait);
    OsReleaseSignals();

    for (i = 0; i < nevents; i++)
        mieqProcessDeviceEvent(ptr, &events[i], NULL);

    FreeEventList(events, GetMaximumEventsNum());

}

static void
XkbFakePointerMotion(DeviceIntPtr dev, unsigned flags, int x, int y)
{
    ValuatorMask mask;
    int gpe_flags = 0;

    /* ignore attached SDs */
    if (!IsMaster(dev) && !IsFloating(dev))
        return;

    if (flags & XkbSA_MoveAbsoluteX || flags & XkbSA_MoveAbsoluteY)
        gpe_flags = POINTER_ABSOLUTE;
    else
        gpe_flags = POINTER_RELATIVE;

    valuator_mask_set_range(&mask, 0, 2, (int[]) {
                            x, y});

    InjectPointerKeyEvents(dev, MotionNotify, 0, gpe_flags, &mask);
}

void
XkbFakeDeviceButton(DeviceIntPtr dev, Bool press, int button)
{
    DeviceIntPtr ptr;
    int down;

    /* If dev is a slave device, and the SD is attached, do nothing. If we'd
     * post through the attached master pointer we'd get duplicate events.
     *
     * if dev is a master keyboard, post through the XTEST device
     *
     * if dev is a floating slave, post through the device itself.
     */

    if (IsMaster(dev)) {
        DeviceIntPtr mpointer = GetMaster(dev, MASTER_POINTER);

        ptr = GetXTestDevice(mpointer);
    }
    else if (IsFloating(dev))
        ptr = dev;
    else
        return;

    down = button_is_down(ptr, button, BUTTON_PROCESSED);
    if (press == down)
        return;

    InjectPointerKeyEvents(dev, press ? ButtonPress : ButtonRelease,
                           button, 0, NULL);
}
