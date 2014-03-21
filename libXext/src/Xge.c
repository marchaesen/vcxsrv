/*
 * Copyright © 2007-2008 Peter Hutterer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Peter Hutterer, University of South Australia, NICTA
 */

/*
 * XGE is an extension to re-use a single opcode for multiple events,
 * depending on the extension. XGE allows events >32 bytes.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <X11/extensions/geproto.h>
#include <X11/extensions/ge.h>
#include <X11/Xlibint.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/Xge.h>

/***********************************************************************/
/*                    internal data structures                         */
/***********************************************************************/

typedef struct {
        int     present;
        short   major_version;
        short   minor_version;
} XGEVersionRec;

/* NULL terminated list of registered extensions. */
typedef struct _XGEExtNode {
    int extension;
    XExtensionHooks* hooks;
    struct _XGEExtNode* next;
} XGEExtNode, *XGEExtList;

/* Internal data for GE extension */
typedef struct _XGEData {
    XEvent data;
    XGEVersionRec *vers;
    XGEExtList extensions;
} XGEData;


/* forward declarations */
static XExtDisplayInfo* _xgeFindDisplay(Display*);
static Bool _xgeWireToEvent(Display*, XEvent*, xEvent*);
static Status _xgeEventToWire(Display*, XEvent*, xEvent*);
static int _xgeDpyClose(Display*, XExtCodes*);
static XGEVersionRec* _xgeGetExtensionVersion(Display*,
                                              _Xconst char*,
                                              XExtDisplayInfo*);
static Bool _xgeCheckExtension(Display* dpy, XExtDisplayInfo* info);

/* main extension information data */
static XExtensionInfo *xge_info;
static const char xge_extension_name[] = GE_NAME;
static XExtensionHooks xge_extension_hooks = {
    NULL,	        /* create_gc */
    NULL,	        /* copy_gc */
    NULL,	        /* flush_gc */
    NULL,	        /* free_gc */
    NULL,	        /* create_font */
    NULL,	        /* free_font */
    _xgeDpyClose,	/* close_display */
    _xgeWireToEvent,	/* wire_to_event */
    _xgeEventToWire,	/* event_to_wire */
    NULL,	        /* error */
    NULL,	        /* error_string */
};


static XExtDisplayInfo *_xgeFindDisplay(Display *dpy)
{
    XExtDisplayInfo *dpyinfo;
    if (!xge_info)
    {
        if (!(xge_info = XextCreateExtension()))
            return NULL;
    }
    if (!(dpyinfo = XextFindDisplay (xge_info, dpy)))
    {
        dpyinfo = XextAddDisplay (xge_info,
                                  dpy,
                                  xge_extension_name,
                                  &xge_extension_hooks,
                                  0 /* no events, see below */,
                                  NULL);
        /* dpyinfo->codes is only null if the server claims not to suppport
           XGE. Don't set up the hooks then, so that an XGE event from the
           server doesn't crash our client */
        if (dpyinfo && dpyinfo->codes)
        {
            /* We don't use an extension opcode, so we have to set the handlers
             * directly. If GenericEvent would be > 64, the job would be done by
             * XExtAddDisplay  */
            XESetWireToEvent (dpy,
                              GenericEvent,
                              xge_extension_hooks.wire_to_event);
            XESetEventToWire (dpy,
                              GenericEvent,
                              xge_extension_hooks.event_to_wire);
        }
    }
    return dpyinfo;
}

/*
 * Check extension is set up and internal data fields are filled.
 */
static Bool
_xgeCheckExtInit(Display* dpy, XExtDisplayInfo* info)
{
    LockDisplay(dpy);
    if(!_xgeCheckExtension(dpy, info))
    {
        goto cleanup;
    }

    if (!info->data)
    {
        XGEData* data = (XGEData*)Xmalloc(sizeof(XGEData));
        if (!data) {
            goto cleanup;
        }
        /* get version from server */
        data->vers =
            _xgeGetExtensionVersion(dpy, "Generic Event Extension", info);
        data->extensions = NULL;
        info->data = (XPointer)data;
    }

    UnlockDisplay(dpy);
    return True;

cleanup:
    UnlockDisplay(dpy);
    return False;
}

/* Return 1 if XGE extension exists, 0 otherwise. */
static Bool
_xgeCheckExtension(Display* dpy, XExtDisplayInfo* info)
{
    return XextHasExtension(info);
}


/* Retrieve XGE version number from server. */
static XGEVersionRec*
_xgeGetExtensionVersion(Display* dpy,
                            _Xconst char* name,
                            XExtDisplayInfo*info)
{
    xGEQueryVersionReply rep;
    xGEQueryVersionReq *req;
    XGEVersionRec *vers;

    GetReq(GEQueryVersion, req);
    req->reqType = info->codes->major_opcode;
    req->ReqType = X_GEQueryVersion;
    req->majorVersion = GE_MAJOR;
    req->minorVersion = GE_MINOR;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue))
    {
        Xfree(info);
        return NULL;
    }

    vers = (XGEVersionRec*)Xmalloc(sizeof(XGEVersionRec));
    vers->major_version = rep.majorVersion;
    vers->minor_version = rep.minorVersion;
    return vers;
}

/*
 * Display closing routine.
 */

static int
_xgeDpyClose(Display* dpy, XExtCodes* codes)
{
    XExtDisplayInfo *info = _xgeFindDisplay(dpy);

    if (info->data != NULL) {
        XGEData* xge_data = (XGEData*)info->data;

        if (xge_data->extensions)
        {
            XGEExtList current, next;
            current = xge_data->extensions;
            while(current)
            {
                next = current->next;
                Xfree(current);
                current = next;
            }
        }

        XFree(xge_data->vers);
        XFree(xge_data);
    }

    if(!XextRemoveDisplay(xge_info, dpy))
        return 0;

    if (xge_info->ndisplays == 0) {
        XextDestroyExtension(xge_info);
        xge_info = NULL;
    }

    return 1;
}

/*
 * protocol to Xlib event conversion routine.
 */
static Bool
_xgeWireToEvent(Display* dpy, XEvent* re, xEvent *event)
{
    int extension;
    XGEExtList it;
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info || !info->data)
        return False;
    /*
       _xgeCheckExtInit() calls LockDisplay, leading to a SIGABRT.
       Well, I guess we don't need the data we get in CheckExtInit anyway
       if (!_xgeCheckExtInit(dpy, info))
                return False;
     */

    extension = ((xGenericEvent*)event)->extension;

    it = ((XGEData*)info->data)->extensions;
    while(it)
    {
        if (it->extension == extension)
        {
            return (it->hooks->wire_to_event(dpy, re, event));
        }
        it = it->next;
    }

    return False;
}

/*
 * xlib event to protocol conversion routine.
 */
static Status
_xgeEventToWire(Display* dpy, XEvent* re, xEvent* event)
{
    int extension;
    XGEExtList it;
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info || !info->data)
        return 1; /* error! */

    extension = ((XGenericEvent*)re)->extension;

    it = ((XGEData*)info->data)->extensions;
    while(it)
    {
        if (it->extension == extension)
        {
            return (it->hooks->event_to_wire(dpy, re, event));
        }
        it = it->next;
    }

    return Success;
}

/*
 * Extensions need to register callbacks for their events.
 */
Bool
_X_HIDDEN xgeExtRegister(Display* dpy, int offset, XExtensionHooks* callbacks)
{
    XGEExtNode* newExt;
    XGEData* xge_data;

    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return False; /* error! */

    if (!_xgeCheckExtInit(dpy, info))
        return False;

    xge_data = (XGEData*)info->data;

    newExt = (XGEExtNode*)Xmalloc(sizeof(XGEExtNode));
    if (!newExt)
    {
        fprintf(stderr, "xgeExtRegister: Failed to alloc memory.\n");
        return False;
    }

    newExt->extension = offset;
    newExt->hooks = callbacks;
    newExt->next = xge_data->extensions;
    xge_data->extensions = newExt;

    return True;
}

/***********************************************************************/
/*                    Client interfaces                                */
/***********************************************************************/

/* Set event_base and error_base to the matching values for XGE.
 * Note that since XGE doesn't use any errors and events, the actual return
 * value is of limited use.
 */
Bool
XGEQueryExtension(Display* dpy, int* event_base, int* error_base)
{
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!_xgeCheckExtInit(dpy, info))
        return False;

    *event_base = info->codes->first_event;
    *error_base = info->codes->first_error;
    return True;
}

/* Get XGE version number.
 * Doesn't actually get it from server, that should have been done beforehand
 * already
 */
Bool
XGEQueryVersion(Display* dpy,
                int *major_version,
                int *minor_version)
{
    XExtDisplayInfo* info = _xgeFindDisplay(dpy);
    if (!info)
        return False;

    if (!_xgeCheckExtInit(dpy, info))
        return False;

    *major_version = ((XGEData*)info->data)->vers->major_version;
    *minor_version = ((XGEData*)info->data)->vers->minor_version;

    return True;
}

