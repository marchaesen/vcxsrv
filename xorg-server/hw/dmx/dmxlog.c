/*
 * Copyright 2001 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * This file encapsulated all of the logging functions that are used by
 * DMX for informational, warning, and error messages. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxlog.h"
#include "dmxinput.h"
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>

static dmxLogLevel dmxCurrentLogLevel = dmxDebug;

/** Set the default level for logging to #dmxLogLevel.  Returns the
 * previous log level. */
dmxLogLevel
dmxSetLogLevel(dmxLogLevel newLevel)
{
    dmxLogLevel oldLevel = dmxCurrentLogLevel;

    if (newLevel > dmxFatal)
        newLevel = dmxFatal;
    dmxCurrentLogLevel = newLevel;
    return oldLevel;
}

/** Returns the log level set by #dmxLogLevel. */
dmxLogLevel
dmxGetLogLevel(void)
{
    return dmxCurrentLogLevel;
}

#ifdef DMX_LOG_STANDALONE
/* When using this file as part of a stand-alone (i.e., non-X-Server
 * program, then the ultimate output routines have to be defined.  */

/** Provide an ErrorF function when used stand-alone. */
void
ErrorF(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);     /* RATS: We assume the format string
                                         * is trusted, since it is always
                                         * from a log message in our code. */
    va_end(args);
}

/** Provide an VFatalError function when used stand-alone. */
static void
VFatalError(const char *format, va_list args) _X_ATTRIBUTE_PRINTF(1, 0) _X_NORETURN;
static void
VFatalError(const char *format, va_list args)
{
    vfprintf(stderr, format, args);     /* RATS: We assume the format string
                                         * is trusted, since it is always
                                         * from a log message in our code. */
    exit(1);
}

/** Provide an VErrorF function when used stand-alone. */
void
VErrorF(const char *format, va_list args)
{
    vfprintf(stderr, format, args);     /* RATS: We assume the format string
                                         * is trusted, since it is always
                                         * from a log message in our code. */
}
#else
/** This function was removed between XFree86 4.3.0 and XFree86 4.4.0. */
extern void AbortServer(void) _X_NORETURN;
static void
VFatalError(const char *format, va_list args) _X_ATTRIBUTE_PRINTF(1, 0) _X_NORETURN;
static void
VFatalError(const char *format, va_list args)
{
    VErrorF(format, args);
    ErrorF("\n");
    AbortServer();
 /*NOTREACHED*/}
#endif

/* Prints a consistent header for each line. */
static void
dmxHeader(dmxLogLevel logLevel, DMXInputInfo * dmxInput,
          DMXScreenInfo * dmxScreen)
{
    const char *type = "??";

    switch (logLevel) {
    case dmxDebug:
        type = "..";
        break;
    case dmxInfo:
        type = "II";
        break;
    case dmxWarning:
        type = "**";
        break;
    case dmxError:
        type = "!!";
        break;
    case dmxFatal:
        type = "Fatal Error";
        break;
    }

    if (dmxInput && dmxScreen) {
        ErrorF("(%s) dmx[i%d/%s;o%d/%s]: ", type,
               dmxInput->inputIdx, dmxInput->name,
               dmxScreen->index, dmxScreen->name);
    }
    else if (dmxScreen) {
        ErrorF("(%s) dmx[o%d/%s]: ", type, dmxScreen->index, dmxScreen->name);
    }
    else if (dmxInput) {
        const char *pt = strchr(dmxInput->name, ',');
        int len = (pt ? (size_t) (pt - dmxInput->name)
                   : strlen(dmxInput->name));

        ErrorF("(%s) dmx[i%d/%*.*s]: ", type,
               dmxInput->inputIdx, len, len, dmxInput->name);
    }
    else {
        ErrorF("(%s) dmx: ", type);
    }
}

/* Prints the error message with the appropriate low-level X output
 * routine. */
static void
dmxMessage(dmxLogLevel logLevel, const char *format, va_list args) _X_ATTRIBUTE_PRINTF(2, 0);
static void
dmxMessage(dmxLogLevel logLevel, const char *format, va_list args)
{
    if (logLevel == dmxFatal || logLevel >= dmxCurrentLogLevel) {
        if (logLevel == dmxFatal)
            VFatalError(format, args);
        else
            VErrorF(format, args);
    }
}

/** Log the specified message at the specified \a logLevel.  \a format
 * can be a printf-like format expression. */
void
dmxLog(dmxLogLevel logLevel, const char *format, ...)
{
    va_list args;

    dmxHeader(logLevel, NULL, NULL);
    va_start(args, format);
    dmxMessage(logLevel, format, args);
    va_end(args);
}

/** Continue a log message without printing the message prefix. */
void
dmxLogCont(dmxLogLevel logLevel, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    dmxMessage(logLevel, format, args);
    va_end(args);
}

#ifndef DMX_LOG_STANDALONE
/** Log an informational message (at level #dmxInfo) related to ouput.
 * The message prefix will contain backend information from \a
 * dmxScreen. */
void
dmxLogOutput(DMXScreenInfo * dmxScreen, const char *format, ...)
{
    va_list args;

    dmxHeader(dmxInfo, NULL, dmxScreen);
    va_start(args, format);
    dmxMessage(dmxInfo, format, args);
    va_end(args);
}

/** Continue a message related to output without printing the message
 * prefix. */
void
dmxLogOutputCont(DMXScreenInfo * dmxScreen, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    dmxMessage(dmxInfo, format, args);
    va_end(args);
}

/** Log a warning message (at level #dmxWarning) related to output.
 * The message prefix will contain backend information from \a
 * dmxScreen. */
void
dmxLogOutputWarning(DMXScreenInfo * dmxScreen, const char *format, ...)
{
    va_list args;

    dmxHeader(dmxWarning, NULL, dmxScreen);
    va_start(args, format);
    dmxMessage(dmxWarning, format, args);
    va_end(args);
}

/** Log an informational message (at level #dmxInfo) related to input.
 * The message prefix will contain information from \a dmxInput. */
void
dmxLogInput(DMXInputInfo * dmxInput, const char *format, ...)
{
    va_list args;

    dmxHeader(dmxInfo, dmxInput, NULL);
    va_start(args, format);
    dmxMessage(dmxInfo, format, args);
    va_end(args);
}

/** Continue a message related to input without printing the message
 * prefix. */
void
dmxLogInputCont(DMXInputInfo * dmxInput, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    dmxMessage(dmxInfo, format, args);
    va_end(args);
}

/** Print \a argc messages, each describing an element in \a argv.  This
 * is maingly for debugging purposes. */
void
dmxLogArgs(dmxLogLevel logLevel, int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++)
        dmxLog(logLevel, "   Arg[%d] = \"%s\"\n", i, argv[i]);
}

/** Print messages at level #dmxInfo describing the visuals in \a vi. */
void
dmxLogVisual(DMXScreenInfo * dmxScreen, XVisualInfo * vi, int defaultVisual)
{
    const char *class = "Unknown";

    switch (vi->class) {
    case StaticGray:
        class = "StaticGray ";
        break;
    case GrayScale:
        class = "GrayScale  ";
        break;
    case StaticColor:
        class = "StaticColor";
        break;
    case PseudoColor:
        class = "PseudoColor";
        break;
    case TrueColor:
        class = "TrueColor  ";
        break;
    case DirectColor:
        class = "DirectColor";
        break;
    }
#define VisualLogFormat "0x%02lx %s %2db %db/rgb %3d 0x%04lx 0x%04lx 0x%04lx%s\n"

    if (dmxScreen) {
        dmxLogOutput(dmxScreen,
                     VisualLogFormat,
                     vi->visualid, class, vi->depth, vi->bits_per_rgb,
                     vi->colormap_size,
                     vi->red_mask, vi->green_mask, vi->blue_mask,
                     defaultVisual ? " *" : "");
    }
    else {
        dmxLog(dmxInfo,
               "  " VisualLogFormat,
               vi->visualid, class, vi->depth, vi->bits_per_rgb,
               vi->colormap_size,
               vi->red_mask, vi->green_mask, vi->blue_mask,
               defaultVisual ? " *" : "");
    }
}

/** Translate a (normalized) XInput event \a type into a human-readable
 * string. */
const char *
dmxXInputEventName(int type)
{
    switch (type) {
    case XI_DeviceValuator:
        return "XI_DeviceValuator";
    case XI_DeviceKeyPress:
        return "XI_DeviceKeyPress";
    case XI_DeviceKeyRelease:
        return "XI_DeviceKeyRelease";
    case XI_DeviceButtonPress:
        return "XI_DeviceButtonPress";
    case XI_DeviceButtonRelease:
        return "XI_DeviceButtonRelease";
    case XI_DeviceMotionNotify:
        return "XI_DeviceMotionNotify";
    case XI_DeviceFocusIn:
        return "XI_DeviceFocusIn";
    case XI_DeviceFocusOut:
        return "XI_DeviceFocusOut";
    case XI_ProximityIn:
        return "XI_ProximityIn";
    case XI_ProximityOut:
        return "XI_ProximityOut";
    case XI_DeviceStateNotify:
        return "XI_DeviceStateNotify";
    case XI_DeviceMappingNotify:
        return "XI_DeviceMappingNotify";
    case XI_ChangeDeviceNotify:
        return "XI_ChangeDeviceNotify";
    case XI_DeviceKeystateNotify:
        return "XI_DeviceKeystateNotify";
    case XI_DeviceButtonstateNotify:
        return "XI_DeviceButtonstateNotify";
    default:
        return "unknown";
    }
}

#endif

/** Translate an event \a type into a human-readable string. */
const char *
dmxEventName(int type)
{
    switch (type) {
    case KeyPress:
        return "KeyPress";
    case KeyRelease:
        return "KeyRelease";
    case ButtonPress:
        return "ButtonPress";
    case ButtonRelease:
        return "ButtonRelease";
    case MotionNotify:
        return "MotionNotify";
    case EnterNotify:
        return "EnterNotify";
    case LeaveNotify:
        return "LeaveNotify";
    case FocusIn:
        return "FocusIn";
    case FocusOut:
        return "FocusOut";
    case KeymapNotify:
        return "KeymapNotify";
    case Expose:
        return "Expose";
    case GraphicsExpose:
        return "GraphicsExpose";
    case NoExpose:
        return "NoExpose";
    case VisibilityNotify:
        return "VisibilityNotify";
    case CreateNotify:
        return "CreateNotify";
    case DestroyNotify:
        return "DestroyNotify";
    case UnmapNotify:
        return "UnmapNotify";
    case MapNotify:
        return "MapNotify";
    case MapRequest:
        return "MapRequest";
    case ReparentNotify:
        return "ReparentNotify";
    case ConfigureNotify:
        return "ConfigureNotify";
    case ConfigureRequest:
        return "ConfigureRequest";
    case GravityNotify:
        return "GravityNotify";
    case ResizeRequest:
        return "ResizeRequest";
    case CirculateNotify:
        return "CirculateNotify";
    case CirculateRequest:
        return "CirculateRequest";
    case PropertyNotify:
        return "PropertyNotify";
    case SelectionClear:
        return "SelectionClear";
    case SelectionRequest:
        return "SelectionRequest";
    case SelectionNotify:
        return "SelectionNotify";
    case ColormapNotify:
        return "ColormapNotify";
    case ClientMessage:
        return "ClientMessage";
    case MappingNotify:
        return "MappingNotify";
    default:
        return "<unknown>";
    }
}
