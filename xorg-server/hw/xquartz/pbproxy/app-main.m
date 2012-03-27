/* app-main.m
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

#include "pbproxy.h"
#import "x-selection.h"

#include <pthread.h>
#include <unistd.h> /*for getpid*/
#include <Cocoa/Cocoa.h>

static const char *app_prefs_domain = BUNDLE_ID_PREFIX ".xpbproxy";
CFStringRef app_prefs_domain_cfstr;

/* Stubs */
char *display = NULL;
BOOL serverRunning = YES;
pthread_mutex_t serverRunningMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serverRunningCond = PTHREAD_COND_INITIALIZER;

static void
signal_handler(int sig)
{
    switch (sig) {
    case SIGHUP:
        xpbproxy_prefs_reload = YES;
        break;

    default:
        _exit(EXIT_SUCCESS);
    }
}

void
ErrorF(const char * f, ...)
{
    va_list args;

    va_start(args, f);
    vfprintf(stderr, f, args);
    va_end(args);
}

/* TODO: Have this actually log to ASL */
void
xq_asl_log(int level, const char *subsystem, const char *file,
           const char *function, int line, const char *fmt,
           ...)
{
#ifdef DEBUG
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
#endif
}

int
main(int argc, const char *argv[])
{
    const char *s;
    int i;

#ifdef DEBUG
    ErrorF("pid: %u\n", getpid());
#endif

    xpbproxy_is_standalone = YES;

    if ((s = getenv("X11_PREFS_DOMAIN")))
        app_prefs_domain = s;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--prefs-domain") == 0 && i + 1 < argc) {
            app_prefs_domain = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0) {
            ErrorF(
                "usage: xpbproxy OPTIONS\n"
                "Pasteboard proxying for X11.\n\n"
                "--prefs-domain <domain>   Change the domain used for reading preferences\n"
                "                          (default: %s)\n",
                app_prefs_domain);
            return 0;
        }
        else {
            ErrorF("usage: xpbproxy OPTIONS...\n"
                   "Try 'xpbproxy --help' for more information.\n");
            return 1;
        }
    }

    app_prefs_domain_cfstr = CFStringCreateWithCString(NULL, app_prefs_domain,
                                                       kCFStringEncodingUTF8);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    return xpbproxy_run();
}
