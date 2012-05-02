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
 * This header is included by all files that need to use the DMX logging
 * facilities. */

#ifndef _DMXLOG_H_
#define _DMXLOG_H_

/** Logging levels -- output is tunable with #dmxSetLogLevel. */
typedef enum {
    dmxDebug,                   /**< Usually verbose debugging info */
    dmxInfo,                    /**< Non-warning information */
    dmxWarning,                 /**< A warning that may indicate DMX
                                 * will not function as the user
                                 * intends. */
    dmxError,                   /**< A non-fatal error that probably
                                 * indicates DMX will not function as
                                 * desired.*/
    dmxFatal                    /**< A fatal error that will cause DMX
                                 * to shut down. */
} dmxLogLevel;

/* Logging functions used by Xserver/hw/dmx routines. */
extern dmxLogLevel dmxSetLogLevel(dmxLogLevel newLevel);
extern dmxLogLevel dmxGetLogLevel(void);
extern void dmxLog(dmxLogLevel logLevel, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogCont(dmxLogLevel logLevel, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern const char *dmxEventName(int type);

#ifndef DMX_LOG_STANDALONE
extern void dmxLogOutput(DMXScreenInfo * dmxScreen, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogOutputCont(DMXScreenInfo * dmxScreen, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogOutputWarning(DMXScreenInfo * dmxScreen, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogInput(DMXInputInfo * dmxInput, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogInputCont(DMXInputInfo * dmxInput, const char *format,
                             ...) _X_ATTRIBUTE_PRINTF(2, 3);
extern void dmxLogArgs(dmxLogLevel logLevel, int argc, char **argv);
extern void dmxLogVisual(DMXScreenInfo * dmxScreen, XVisualInfo * vi,
                         int defaultVisual);
extern const char *dmxXInputEventName(int type);
#endif

#endif
