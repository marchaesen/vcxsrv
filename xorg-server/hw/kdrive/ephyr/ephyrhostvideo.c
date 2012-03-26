/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2007 OpenedHand Ltd 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Dodji Seketeli <dodji@openedhand.com>
 */
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
/*
 * including some server headers (like kdrive-config.h)
 * might define the macro _XSERVER64
 * on 64 bits machines. That macro must _NOT_ be defined for Xlib
 * client code, otherwise bad things happen.
 * So let's undef that macro if necessary.
 */
#ifdef _XSERVER64
#undef _XSERVER64
#endif
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xvlib.h>
#include <X11/extensions/Xvproto.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#define _HAVE_XALLOC_DECLS

#include "hostx.h"
#include "ephyrhostvideo.h"
#include "ephyrlog.h"

#ifndef TRUE
#define TRUE 1
#endif /*TRUE*/
#ifndef FALSE
#define FALSE 0
#endif /*FALSE*/
static XExtensionInfo _xv_info_data;
static XExtensionInfo *xv_info = &_xv_info_data;
static char *xv_extension_name = XvName;
static char *xv_error_string(Display * dpy, int code, XExtCodes * codes,
                             char *buf, int n);
static int xv_close_display(Display * dpy, XExtCodes * codes);
static Bool xv_wire_to_event(Display * dpy, XEvent * host, xEvent *wire);

static XExtensionHooks xv_extension_hooks = {
    NULL,                       /* create_gc */
    NULL,                       /* copy_gc */
    NULL,                       /* flush_gc */
    NULL,                       /* free_gc */
    NULL,                       /* create_font */
    NULL,                       /* free_font */
    xv_close_display,           /* close_display */
    xv_wire_to_event,           /* wire_to_event */
    NULL,                       /* event_to_wire */
    NULL,                       /* error */
    xv_error_string             /* error_string */
};

static char *xv_error_list[] = {
    "BadPort",                  /* XvBadPort     */
    "BadEncoding",              /* XvBadEncoding */
    "BadControl"                /* XvBadControl  */
};

#define XvCheckExtension(dpy, i, val) \
  XextCheckExtension(dpy, i, xv_extension_name, val)
#define XvGetReq(name, req) \
        WORD64ALIGN\
        if ((dpy->bufptr + SIZEOF(xv##name##Req)) > dpy->bufmax)\
                _XFlush(dpy);\
        req = (xv##name##Req *)(dpy->last_req = dpy->bufptr);\
        req->reqType = info->codes->major_opcode;\
        req->xvReqType = xv_##name; \
        req->length = (SIZEOF(xv##name##Req))>>2;\
        dpy->bufptr += SIZEOF(xv##name##Req);\
        dpy->request++

static
XEXT_GENERATE_CLOSE_DISPLAY(xv_close_display, xv_info)

static
XEXT_GENERATE_FIND_DISPLAY(xv_find_display, xv_info,
                           xv_extension_name,
                           &xv_extension_hooks, XvNumEvents, NULL)

static
XEXT_GENERATE_ERROR_STRING(xv_error_string, xv_extension_name,
                           XvNumErrors, xv_error_list)

struct _EphyrHostXVAdaptorArray {
    XvAdaptorInfo *adaptors;
    unsigned int nb_adaptors;
};

/*heavily copied from libx11*/
#define BUFSIZE 2048
static void
ephyrHostXVLogXErrorEvent(Display * a_display,
                          XErrorEvent * a_err_event, FILE * a_fp)
{
    char buffer[BUFSIZ];
    char mesg[BUFSIZ];
    char number[32];
    const char *mtype = "XlibMessage";
    register _XExtension *ext = (_XExtension *) NULL;
    _XExtension *bext = (_XExtension *) NULL;
    Display *dpy = a_display;

    XGetErrorText(dpy, a_err_event->error_code, buffer, BUFSIZ);
    XGetErrorDatabaseText(dpy, mtype, "XError", "X Error", mesg, BUFSIZ);
    (void) fprintf(a_fp, "%s:  %s\n  ", mesg, buffer);
    XGetErrorDatabaseText(dpy, mtype, "MajorCode", "Request Major code %d",
                          mesg, BUFSIZ);
    (void) fprintf(a_fp, mesg, a_err_event->request_code);
    if (a_err_event->request_code < 128) {
        snprintf(number, sizeof(number), "%d", a_err_event->request_code);
        XGetErrorDatabaseText(dpy, "XRequest", number, "", buffer, BUFSIZ);
    }
    else {
        for (ext = dpy->ext_procs;
             ext && (ext->codes.major_opcode != a_err_event->request_code);
             ext = ext->next);
        if (ext)
            strcpy(buffer, ext->name);
        else
            buffer[0] = '\0';
    }
    (void) fprintf(a_fp, " (%s)\n", buffer);
    if (a_err_event->request_code >= 128) {
        XGetErrorDatabaseText(dpy, mtype, "MinorCode", "Request Minor code %d",
                              mesg, BUFSIZ);
        fputs("  ", a_fp);
        (void) fprintf(a_fp, mesg, a_err_event->minor_code);
        if (ext) {
            snprintf(mesg, sizeof(mesg), "%s.%d",
                     ext->name, a_err_event->minor_code);
            XGetErrorDatabaseText(dpy, "XRequest", mesg, "", buffer, BUFSIZ);
            (void) fprintf(a_fp, " (%s)", buffer);
        }
        fputs("\n", a_fp);
    }
    if (a_err_event->error_code >= 128) {
        /* kludge, try to find the extension that caused it */
        buffer[0] = '\0';
        for (ext = dpy->ext_procs; ext; ext = ext->next) {
            if (ext->error_string)
                (*ext->error_string) (dpy, a_err_event->error_code, &ext->codes,
                                      buffer, BUFSIZ);
            if (buffer[0]) {
                bext = ext;
                break;
            }
            if (ext->codes.first_error &&
                ext->codes.first_error < (int) a_err_event->error_code &&
                (!bext || ext->codes.first_error > bext->codes.first_error))
                bext = ext;
        }
        if (bext)
            snprintf(buffer, sizeof(buffer), "%s.%d", bext->name,
                     a_err_event->error_code - bext->codes.first_error);
        else
            strcpy(buffer, "Value");
        XGetErrorDatabaseText(dpy, mtype, buffer, "", mesg, BUFSIZ);
        if (mesg[0]) {
            fputs("  ", a_fp);
            (void) fprintf(a_fp, mesg, a_err_event->resourceid);
            fputs("\n", a_fp);
        }
        /* let extensions try to print the values */
        for (ext = dpy->ext_procs; ext; ext = ext->next) {
            if (ext->error_values)
                (*ext->error_values) (dpy, a_err_event, a_fp);
        }
    }
    else if ((a_err_event->error_code == BadWindow) ||
             (a_err_event->error_code == BadPixmap) ||
             (a_err_event->error_code == BadCursor) ||
             (a_err_event->error_code == BadFont) ||
             (a_err_event->error_code == BadDrawable) ||
             (a_err_event->error_code == BadColor) ||
             (a_err_event->error_code == BadGC) ||
             (a_err_event->error_code == BadIDChoice) ||
             (a_err_event->error_code == BadValue) ||
             (a_err_event->error_code == BadAtom)) {
        if (a_err_event->error_code == BadValue)
            XGetErrorDatabaseText(dpy, mtype, "Value", "Value 0x%x",
                                  mesg, BUFSIZ);
        else if (a_err_event->error_code == BadAtom)
            XGetErrorDatabaseText(dpy, mtype, "AtomID", "AtomID 0x%x",
                                  mesg, BUFSIZ);
        else
            XGetErrorDatabaseText(dpy, mtype, "ResourceID", "ResourceID 0x%x",
                                  mesg, BUFSIZ);
        fputs("  ", a_fp);
        (void) fprintf(a_fp, mesg, a_err_event->resourceid);
        fputs("\n", a_fp);
    }
    XGetErrorDatabaseText(dpy, mtype, "ErrorSerial", "Error Serial #%d",
                          mesg, BUFSIZ);
    fputs("  ", a_fp);
    (void) fprintf(a_fp, mesg, a_err_event->serial);
    XGetErrorDatabaseText(dpy, mtype, "CurrentSerial", "Current Serial #%d",
                          mesg, BUFSIZ);
    fputs("\n  ", a_fp);
    (void) fprintf(a_fp, mesg, dpy->request);
    fputs("\n", a_fp);
}

static int
ephyrHostXVErrorHandler(Display * a_display, XErrorEvent * a_error_event)
{
    EPHYR_LOG_ERROR("got an error from the host xserver:\n");
    ephyrHostXVLogXErrorEvent(a_display, a_error_event, stderr);
    return Success;
}

void
ephyrHostXVInit(void)
{
    static Bool s_initialized;

    if (s_initialized)
        return;
    XSetErrorHandler(ephyrHostXVErrorHandler);
    s_initialized = TRUE;
}

Bool
ephyrHostXVQueryAdaptors(EphyrHostXVAdaptorArray ** a_adaptors)
{
    EphyrHostXVAdaptorArray *result = NULL;
    int ret = 0;
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_adaptors, FALSE);

    EPHYR_LOG("enter\n");

    result = calloc(1, sizeof(EphyrHostXVAdaptorArray));
    if (!result)
        goto out;

    ret = XvQueryAdaptors(hostx_get_display(),
                          DefaultRootWindow(hostx_get_display()),
                          &result->nb_adaptors, &result->adaptors);
    if (ret != Success) {
        EPHYR_LOG_ERROR("failed to query host adaptors: %d\n", ret);
        goto out;
    }
    *a_adaptors = result;
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

void
ephyrHostXVAdaptorArrayDelete(EphyrHostXVAdaptorArray * a_adaptors)
{
    if (!a_adaptors)
        return;
    if (a_adaptors->adaptors) {
        XvFreeAdaptorInfo(a_adaptors->adaptors);
        a_adaptors->adaptors = NULL;
        a_adaptors->nb_adaptors = 0;
    }
    XFree(a_adaptors);
}

int
ephyrHostXVAdaptorArrayGetSize(const EphyrHostXVAdaptorArray * a_this)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, -1);
    return a_this->nb_adaptors;
}

EphyrHostXVAdaptor *
ephyrHostXVAdaptorArrayAt(const EphyrHostXVAdaptorArray * a_this, int a_index)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, NULL);

    if (a_index >= a_this->nb_adaptors)
        return NULL;
    return (EphyrHostXVAdaptor *) & a_this->adaptors[a_index];
}

char
ephyrHostXVAdaptorGetType(const EphyrHostXVAdaptor * a_this)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, -1);
    return ((XvAdaptorInfo *) a_this)->type;
}

const char *
ephyrHostXVAdaptorGetName(const EphyrHostXVAdaptor * a_this)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, NULL);

    return ((XvAdaptorInfo *) a_this)->name;
}

EphyrHostVideoFormat *
ephyrHostXVAdaptorGetVideoFormats(const EphyrHostXVAdaptor * a_this,
                                  int *a_nb_formats)
{
    EphyrHostVideoFormat *formats = NULL;
    int nb_formats = 0, i = 0;
    XVisualInfo *visual_info, visual_info_template;
    int nb_visual_info;

    EPHYR_RETURN_VAL_IF_FAIL(a_this, NULL);

    nb_formats = ((XvAdaptorInfo *) a_this)->num_formats;
    formats = calloc(nb_formats, sizeof(EphyrHostVideoFormat));
    for (i = 0; i < nb_formats; i++) {
        memset(&visual_info_template, 0, sizeof(visual_info_template));
        visual_info_template.visualid =
            ((XvAdaptorInfo *) a_this)->formats[i].visual_id;
        visual_info = XGetVisualInfo(hostx_get_display(),
                                     VisualIDMask,
                                     &visual_info_template, &nb_visual_info);
        formats[i].depth = ((XvAdaptorInfo *) a_this)->formats[i].depth;
        formats[i].visual_class = visual_info->class;
        XFree(visual_info);
    }
    if (a_nb_formats)
        *a_nb_formats = nb_formats;
    return formats;
}

int
ephyrHostXVAdaptorGetNbPorts(const EphyrHostXVAdaptor * a_this)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, -1);

    return ((XvAdaptorInfo *) a_this)->num_ports;
}

int
ephyrHostXVAdaptorGetFirstPortID(const EphyrHostXVAdaptor * a_this)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this, -1);

    return ((XvAdaptorInfo *) a_this)->base_id;
}

Bool
ephyrHostXVAdaptorHasPutVideo(const EphyrHostXVAdaptor * a_this, Bool *a_result)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this && a_result, FALSE);

    if ((((XvAdaptorInfo *) a_this)->type & (XvVideoMask | XvInputMask)) ==
        (XvVideoMask | XvInputMask))
        *a_result = TRUE;
    else
        *a_result = FALSE;
    return TRUE;
}

Bool
ephyrHostXVAdaptorHasGetVideo(const EphyrHostXVAdaptor * a_this, Bool *a_result)
{
    if ((((XvAdaptorInfo *) a_this)->type & (XvVideoMask | XvOutputMask)) ==
        (XvVideoMask | XvOutputMask))
        *a_result = TRUE;
    else
        *a_result = FALSE;
    return TRUE;
}

Bool
ephyrHostXVAdaptorHasPutStill(const EphyrHostXVAdaptor * a_this, Bool *a_result)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this && a_result, FALSE);

    if ((((XvAdaptorInfo *) a_this)->type & (XvStillMask | XvInputMask)) ==
        (XvStillMask | XvInputMask))
        *a_result = TRUE;
    else
        *a_result = FALSE;
    return TRUE;
}

Bool
ephyrHostXVAdaptorHasGetStill(const EphyrHostXVAdaptor * a_this, Bool *a_result)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this && a_result, FALSE);

    if ((((XvAdaptorInfo *) a_this)->type & (XvStillMask | XvOutputMask)) ==
        (XvStillMask | XvOutputMask))
        *a_result = TRUE;
    else
        *a_result = FALSE;
    return TRUE;
}

Bool
ephyrHostXVAdaptorHasPutImage(const EphyrHostXVAdaptor * a_this, Bool *a_result)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_this && a_result, FALSE);

    if ((((XvAdaptorInfo *) a_this)->type & (XvImageMask | XvInputMask)) ==
        (XvImageMask | XvInputMask))
        *a_result = TRUE;
    else
        *a_result = FALSE;
    return TRUE;
}

Bool
ephyrHostXVQueryEncodings(int a_port_id,
                          EphyrHostEncoding ** a_encodings,
                          unsigned int *a_num_encodings)
{
    EphyrHostEncoding *encodings = NULL;
    XvEncodingInfo *encoding_info = NULL;
    unsigned int num_encodings = 0, i;
    int ret = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_encodings && a_num_encodings, FALSE);

    ret = XvQueryEncodings(hostx_get_display(),
                           a_port_id, &num_encodings, &encoding_info);
    if (num_encodings && encoding_info) {
        encodings = calloc(num_encodings, sizeof(EphyrHostEncoding));
        for (i = 0; i < num_encodings; i++) {
            encodings[i].id = encoding_info[i].encoding_id;
            encodings[i].name = strdup(encoding_info[i].name);
            encodings[i].width = encoding_info[i].width;
            encodings[i].height = encoding_info[i].height;
            encodings[i].rate.numerator = encoding_info[i].rate.numerator;
            encodings[i].rate.denominator = encoding_info[i].rate.denominator;
        }
    }
    if (encoding_info) {
        XvFreeEncodingInfo(encoding_info);
        encoding_info = NULL;
    }
    *a_encodings = encodings;
    *a_num_encodings = num_encodings;

    if (ret != Success)
        return FALSE;
    return TRUE;
}

void
ephyrHostEncodingsDelete(EphyrHostEncoding * a_encodings, int a_num_encodings)
{
    int i = 0;

    if (!a_encodings)
        return;
    for (i = 0; i < a_num_encodings; i++) {
        free(a_encodings[i].name);
        a_encodings[i].name = NULL;
    }
    free(a_encodings);
}

void
ephyrHostAttributesDelete(EphyrHostAttribute * a_attributes)
{
    if (!a_attributes)
        return;
    XFree(a_attributes);
}

Bool
ephyrHostXVQueryPortAttributes(int a_port_id,
                               EphyrHostAttribute ** a_attributes,
                               int *a_num_attributes)
{
    EPHYR_RETURN_VAL_IF_FAIL(a_attributes && a_num_attributes, FALSE);

    *a_attributes =
        (EphyrHostAttribute *) XvQueryPortAttributes(hostx_get_display(),
                                                     a_port_id,
                                                     a_num_attributes);

    return TRUE;
}

Bool
ephyrHostXVQueryImageFormats(int a_port_id,
                             EphyrHostImageFormat ** a_formats,
                             int *a_num_format)
{
    XvImageFormatValues *result = NULL;

    EPHYR_RETURN_VAL_IF_FAIL(a_formats && a_num_format, FALSE);

    result = XvListImageFormats(hostx_get_display(), a_port_id, a_num_format);
    *a_formats = (EphyrHostImageFormat *) result;
    return TRUE;

}

Bool
ephyrHostXVSetPortAttribute(int a_port_id, int a_atom, int a_attr_value)
{
    int res = Success;

    EPHYR_LOG("atom,name,value: (%d,%s,%d)\n",
              a_atom, XGetAtomName(hostx_get_display(), a_atom), a_attr_value);

    res = XvSetPortAttribute(hostx_get_display(),
                             a_port_id, a_atom, a_attr_value);
    if (res != Success) {
        EPHYR_LOG_ERROR("XvSetPortAttribute() failed: %d\n", res);
        return FALSE;
    }
    XFlush(hostx_get_display());
    EPHYR_LOG("leave\n");

    return TRUE;
}

Bool
ephyrHostXVGetPortAttribute(int a_port_id, int a_atom, int *a_attr_value)
{
    int res = Success;
    Bool ret = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_attr_value, FALSE);

    EPHYR_LOG("enter, a_port_id: %d, a_atomid: %d, attr_name: %s\n",
              a_port_id, a_atom, XGetAtomName(hostx_get_display(), a_atom));

    res = XvGetPortAttribute(hostx_get_display(),
                             a_port_id, a_atom, a_attr_value);
    if (res != Success) {
        EPHYR_LOG_ERROR("XvGetPortAttribute() failed: %d \n", res);
        goto out;
    }
    EPHYR_LOG("atom,value: (%d, %d)\n", a_atom, *a_attr_value);

    ret = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return ret;
}

Bool
ephyrHostXVQueryBestSize(int a_port_id,
                         Bool a_motion,
                         unsigned int a_frame_w,
                         unsigned int a_frame_h,
                         unsigned int a_drw_w,
                         unsigned int a_drw_h,
                         unsigned int *a_actual_w, unsigned int *a_actual_h)
{
    int res = 0;
    Bool is_ok = FALSE;

    EPHYR_RETURN_VAL_IF_FAIL(a_actual_w && a_actual_h, FALSE);

    EPHYR_LOG("enter: frame (%dx%d), drw (%dx%d)\n",
              a_frame_w, a_frame_h, a_drw_w, a_drw_h);

    res = XvQueryBestSize(hostx_get_display(),
                          a_port_id,
                          a_motion,
                          a_frame_w, a_frame_h,
                          a_drw_w, a_drw_h, a_actual_w, a_actual_h);
    if (res != Success) {
        EPHYR_LOG_ERROR("XvQueryBestSize() failed: %d\n", res);
        goto out;
    }
    XSync(hostx_get_display(), FALSE);

    EPHYR_LOG("actual (%dx%d)\n", *a_actual_w, *a_actual_h);
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}

static Bool
xv_wire_to_event(Display * dpy, XEvent * host, xEvent *wire)
{
    XExtDisplayInfo *info = xv_find_display(dpy);
    XvEvent *re = (XvEvent *) host;
    xvEvent *event = (xvEvent *) wire;

    XvCheckExtension(dpy, info, False);

    switch ((event->u.u.type & 0x7F) - info->codes->first_event) {
    case XvVideoNotify:
        re->xvvideo.type = event->u.u.type & 0x7f;
        re->xvvideo.serial = _XSetLastRequestRead(dpy, (xGenericReply *) event);
        re->xvvideo.send_event = ((event->u.u.type & 0x80) != 0);
        re->xvvideo.display = dpy;
        re->xvvideo.time = event->u.videoNotify.time;
        re->xvvideo.reason = event->u.videoNotify.reason;
        re->xvvideo.drawable = event->u.videoNotify.drawable;
        re->xvvideo.port_id = event->u.videoNotify.port;
        break;
    case XvPortNotify:
        re->xvport.type = event->u.u.type & 0x7f;
        re->xvport.serial = _XSetLastRequestRead(dpy, (xGenericReply *) event);
        re->xvport.send_event = ((event->u.u.type & 0x80) != 0);
        re->xvport.display = dpy;
        re->xvport.time = event->u.portNotify.time;
        re->xvport.port_id = event->u.portNotify.port;
        re->xvport.attribute = event->u.portNotify.attribute;
        re->xvport.value = event->u.portNotify.value;
        break;
    default:
        return False;
    }

    return True;
}

Bool
ephyrHostXVQueryImageAttributes(int a_port_id,
                                int a_image_id /*image fourcc code */ ,
                                unsigned short *a_width,
                                unsigned short *a_height,
                                int *a_image_size,
                                int *a_pitches, int *a_offsets)
{
    Display *dpy = hostx_get_display();
    Bool ret = FALSE;
    XExtDisplayInfo *info = xv_find_display(dpy);
    xvQueryImageAttributesReq *req = NULL;
    xvQueryImageAttributesReply rep;

    EPHYR_RETURN_VAL_IF_FAIL(a_width, FALSE);
    EPHYR_RETURN_VAL_IF_FAIL(a_height, FALSE);
    EPHYR_RETURN_VAL_IF_FAIL(a_image_size, FALSE);

    XvCheckExtension(dpy, info, FALSE);

    LockDisplay(dpy);

    XvGetReq(QueryImageAttributes, req);
    req->id = a_image_id;
    req->port = a_port_id;
    req->width = *a_width;
    req->height = *a_height;
    /*
     * read the reply
     */
    if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
        EPHYR_LOG_ERROR("QeryImageAttribute req failed\n");
        goto out;
    }
    if (a_pitches && a_offsets) {
        _XRead(dpy, (char *) a_pitches, rep.num_planes << 2);
        _XRead(dpy, (char *) a_offsets, rep.num_planes << 2);
    }
    else {
        _XEatData(dpy, rep.length << 2);
    }
    *a_width = rep.width;
    *a_height = rep.height;
    *a_image_size = rep.data_size;

    ret = TRUE;

 out:
    UnlockDisplay(dpy);
    SyncHandle();
    return ret;
}

Bool
ephyrHostGetAtom(const char *a_name, Bool a_create_if_not_exists, int *a_atom)
{
    int atom = None;

    EPHYR_RETURN_VAL_IF_FAIL(a_atom, FALSE);

    atom = XInternAtom(hostx_get_display(), a_name, a_create_if_not_exists);
    if (atom == None) {
        return FALSE;
    }
    *a_atom = atom;
    return TRUE;
}

char *
ephyrHostGetAtomName(int a_atom)
{
    return XGetAtomName(hostx_get_display(), a_atom);
}

void
ephyrHostFree(void *a_pointer)
{
    if (a_pointer)
        XFree(a_pointer);
}

Bool
ephyrHostXVPutImage(int a_screen_num,
                    int a_port_id,
                    int a_image_id,
                    int a_drw_x,
                    int a_drw_y,
                    int a_drw_w,
                    int a_drw_h,
                    int a_src_x,
                    int a_src_y,
                    int a_src_w,
                    int a_src_h,
                    int a_image_width,
                    int a_image_height,
                    unsigned char *a_buf,
                    EphyrHostBox * a_clip_rects, int a_clip_rect_nums)
{
    Bool is_ok = TRUE;
    XvImage *xv_image = NULL;
    GC gc = 0;
    XGCValues gc_values;
    Display *dpy = hostx_get_display();
    XRectangle *rects = NULL;
    int res = 0;

    EPHYR_RETURN_VAL_IF_FAIL(a_buf, FALSE);

    EPHYR_LOG("enter, num_clip_rects: %d\n", a_clip_rect_nums);

    memset(&gc_values, 0, sizeof(gc_values));
    gc = XCreateGC(dpy, hostx_get_window(a_screen_num), 0L, &gc_values);
    if (!gc) {
        EPHYR_LOG_ERROR("failed to create gc \n");
        goto out;
    }
    xv_image = (XvImage *) XvCreateImage(hostx_get_display(),
                                         a_port_id, a_image_id,
                                         NULL, a_image_width, a_image_height);
    if (!xv_image) {
        EPHYR_LOG_ERROR("failed to create image\n");
        goto out;
    }
    xv_image->data = (char *) a_buf;
    if (a_clip_rect_nums) {
        int i = 0;

        rects = calloc(a_clip_rect_nums, sizeof(XRectangle));
        for (i = 0; i < a_clip_rect_nums; i++) {
            rects[i].x = a_clip_rects[i].x1;
            rects[i].y = a_clip_rects[i].y1;
            rects[i].width = a_clip_rects[i].x2 - a_clip_rects[i].x1;
            rects[i].height = a_clip_rects[i].y2 - a_clip_rects[i].y1;
            EPHYR_LOG("(x,y,w,h): (%d,%d,%d,%d)\n",
                      rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }
        XSetClipRectangles(dpy, gc, 0, 0, rects, a_clip_rect_nums, YXBanded);
        /*this always returns 1 */
    }
    res = XvPutImage(dpy, a_port_id,
                     hostx_get_window(a_screen_num),
                     gc, xv_image,
                     a_src_x, a_src_y, a_src_w, a_src_h,
                     a_drw_x, a_drw_y, a_drw_w, a_drw_h);
    if (res != Success) {
        EPHYR_LOG_ERROR("XvPutImage() failed: %d\n", res);
        goto out;
    }
    is_ok = TRUE;

 out:
    if (xv_image) {
        XFree(xv_image);
        xv_image = NULL;
    }
    if (gc) {
        XFreeGC(dpy, gc);
        gc = NULL;
    }
    free(rects);
    rects = NULL;
    EPHYR_LOG("leave\n");
    return is_ok;
}

Bool
ephyrHostXVPutVideo(int a_screen_num, int a_port_id,
                    int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                    int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h)
{
    Bool is_ok = FALSE;
    int res = FALSE;
    GC gc = 0;
    XGCValues gc_values;
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    gc = XCreateGC(dpy, hostx_get_window(a_screen_num), 0L, &gc_values);
    if (!gc) {
        EPHYR_LOG_ERROR("failed to create gc \n");
        goto out;
    }
    res = XvPutVideo(dpy, a_port_id, hostx_get_window(a_screen_num), gc,
                     a_vid_x, a_vid_y, a_vid_w, a_vid_h,
                     a_drw_x, a_drw_y, a_drw_w, a_drw_h);

    if (res != Success) {
        EPHYR_LOG_ERROR("XvPutVideo() failed: %d\n", res);
        goto out;
    }

    is_ok = TRUE;

 out:
    if (gc) {
        XFreeGC(dpy, gc);
        gc = NULL;
    }
    return is_ok;
}

Bool
ephyrHostXVGetVideo(int a_screen_num, int a_port_id,
                    int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                    int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h)
{
    Bool is_ok = FALSE;
    int res = FALSE;
    GC gc = 0;
    XGCValues gc_values;
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    gc = XCreateGC(dpy, hostx_get_window(a_screen_num), 0L, &gc_values);
    if (!gc) {
        EPHYR_LOG_ERROR("failed to create gc \n");
        goto out;
    }
    res = XvGetVideo(dpy, a_port_id, hostx_get_window(a_screen_num), gc,
                     a_vid_x, a_vid_y, a_vid_w, a_vid_h,
                     a_drw_x, a_drw_y, a_drw_w, a_drw_h);

    if (res != Success) {
        EPHYR_LOG_ERROR("XvGetVideo() failed: %d\n", res);
        goto out;
    }

    is_ok = TRUE;

 out:
    if (gc) {
        XFreeGC(dpy, gc);
        gc = NULL;
    }
    return is_ok;
}

Bool
ephyrHostXVPutStill(int a_screen_num, int a_port_id,
                    int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                    int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h)
{
    Bool is_ok = FALSE;
    int res = FALSE;
    GC gc = 0;
    XGCValues gc_values;
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    gc = XCreateGC(dpy, hostx_get_window(a_screen_num), 0L, &gc_values);
    if (!gc) {
        EPHYR_LOG_ERROR("failed to create gc \n");
        goto out;
    }
    res = XvPutStill(dpy, a_port_id, hostx_get_window(a_screen_num), gc,
                     a_vid_x, a_vid_y, a_vid_w, a_vid_h,
                     a_drw_x, a_drw_y, a_drw_w, a_drw_h);

    if (res != Success) {
        EPHYR_LOG_ERROR("XvPutStill() failed: %d\n", res);
        goto out;
    }

    is_ok = TRUE;

 out:
    if (gc) {
        XFreeGC(dpy, gc);
        gc = NULL;
    }
    return is_ok;
}

Bool
ephyrHostXVGetStill(int a_screen_num, int a_port_id,
                    int a_vid_x, int a_vid_y, int a_vid_w, int a_vid_h,
                    int a_drw_x, int a_drw_y, int a_drw_w, int a_drw_h)
{
    Bool is_ok = FALSE;
    int res = FALSE;
    GC gc = 0;
    XGCValues gc_values;
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    gc = XCreateGC(dpy, hostx_get_window(a_screen_num), 0L, &gc_values);
    if (!gc) {
        EPHYR_LOG_ERROR("failed to create gc \n");
        goto out;
    }
    res = XvGetStill(dpy, a_port_id, hostx_get_window(a_screen_num), gc,
                     a_vid_x, a_vid_y, a_vid_w, a_vid_h,
                     a_drw_x, a_drw_y, a_drw_w, a_drw_h);

    if (res != Success) {
        EPHYR_LOG_ERROR("XvGetStill() failed: %d\n", res);
        goto out;
    }

    is_ok = TRUE;

 out:
    if (gc) {
        XFreeGC(dpy, gc);
        gc = NULL;
    }
    return is_ok;
}

Bool
ephyrHostXVStopVideo(int a_screen_num, int a_port_id)
{
    int ret = 0;
    Bool is_ok = FALSE;
    Display *dpy = hostx_get_display();

    EPHYR_RETURN_VAL_IF_FAIL(dpy, FALSE);

    EPHYR_LOG("enter\n");

    ret = XvStopVideo(dpy, a_port_id, hostx_get_window(a_screen_num));
    if (ret != Success) {
        EPHYR_LOG_ERROR("XvStopVideo() failed: %d \n", ret);
        goto out;
    }
    is_ok = TRUE;

 out:
    EPHYR_LOG("leave\n");
    return is_ok;
}
