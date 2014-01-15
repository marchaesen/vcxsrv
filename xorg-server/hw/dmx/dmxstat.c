/*
 * Copyright 2002, 2003 Red Hat Inc., Durham, North Carolina.
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
 *
 * The DMX server code is written to call #dmxSync() whenever an XSync()
 * might be necessary.  However, since XSync() requires a two way
 * communication with the other X server, eliminating unnecessary
 * XSync() calls is a key performance optimization.  Support for this
 * optimization is provided in \a dmxsync.c.  This file provides routines
 * that evaluate this optimization by counting the number of XSync()
 * calls and monitoring their latency.  This functionality can be turned
 * on using the -stat command-line parameter. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxstat.h"
#include "dmxlog.h"
#include <X11/Xos.h>            /* For sys/time.h */

/** Used to compute a running average of value. */
typedef struct _DMXStatAvg {
    int pos;
    int count;
    unsigned long value[DMX_STAT_LENGTH];
} DMXStatAvg;

/** Statistical information about XSync calls. */
struct _DMXStatInfo {
    unsigned long syncCount;
    unsigned long oldSyncCount;

    DMXStatAvg usec;
    DMXStatAvg pending;

    unsigned long bins[DMX_STAT_BINS];
};

/* Interval in mS between statistic message log entries. */
int dmxStatInterval;
static int dmxStatDisplays;
static OsTimerPtr dmxStatTimer;

/** Return the number of microseconds as an unsigned long.
 * Unfortunately, this is only useful for intervals < about 4 sec.  */
static unsigned long
usec(struct timeval *stop, struct timeval *start)
{
    return (stop->tv_sec - start->tv_sec) * 1000000
        + stop->tv_usec - start->tv_usec;
}

static unsigned long
avg(DMXStatAvg * data, unsigned long *max)
{
    unsigned long sum;
    int i;

    *max = 0;
    if (!data->count)
        return 0;

    for (i = 0, sum = 0; i < data->count; i++) {
        if (data->value[i] > *max)
            *max = data->value[i];
        sum += data->value[i];
    }
    return sum / data->count;
}

/** Turn on XSync statistic gathering and printing.  Print every \a
 * interval seconds, with lines for the first \a displays.  If \a
 * interval is NULL, 1 will be used.  If \a displays is NULL, 0 will be
 * used (meaning a line for every display will be printed).  Note that
 * this function takes string arguments because it will usually be
 * called from #ddxProcessArgument in \a dmxinit.c. */
void
dmxStatActivate(const char *interval, const char *displays)
{
    dmxStatInterval = (interval ? atoi(interval) : 1) * 1000;
    dmxStatDisplays = (displays ? atoi(displays) : 0);

    if (dmxStatInterval < 1000)
        dmxStatInterval = 1000;
    if (dmxStatDisplays < 0)
        dmxStatDisplays = 0;
}

/** Allocate a \a DMXStatInfo structure. */
DMXStatInfo *
dmxStatAlloc(void)
{
    DMXStatInfo *pt = calloc(1, sizeof(*pt));

    return pt;
}

/** Free the memory used by a \a DMXStatInfo structure. */
void
dmxStatFree(DMXStatInfo * pt)
{
    free(pt);
}

static void
dmxStatValue(DMXStatAvg * data, unsigned long value)
{
    if (data->count != DMX_STAT_LENGTH)
        ++data->count;
    if (data->pos >= DMX_STAT_LENGTH - 1)
        data->pos = 0;
    data->value[data->pos++] = value;
}

/** Note that a XSync() was just done on \a dmxScreen with the \a start
 * and \a stop times (from gettimeofday()) and the number of
 * pending-but-not-yet-processed XSync requests.  This routine is called
 * from #dmxDoSync in \a dmxsync.c */
void
dmxStatSync(DMXScreenInfo * dmxScreen,
            struct timeval *stop, struct timeval *start, unsigned long pending)
{
    DMXStatInfo *s = dmxScreen->stat;
    unsigned long elapsed = usec(stop, start);
    unsigned long thresh;
    int i;

    ++s->syncCount;
    dmxStatValue(&s->usec, elapsed);
    dmxStatValue(&s->pending, pending);

    for (i = 0, thresh = DMX_STAT_BIN0; i < DMX_STAT_BINS - 1; i++) {
        if (elapsed < thresh) {
            ++s->bins[i];
            break;
        }
        thresh *= DMX_STAT_BINMULT;
    }
    if (i == DMX_STAT_BINS - 1)
        ++s->bins[i];
}

/* Actually do the work of printing out the human-readable message. */
static CARD32
dmxStatCallback(OsTimerPtr timer, CARD32 t, void *arg)
{
    int i, j;
    static int header = 0;
    int limit = dmxNumScreens;

    if (!dmxNumScreens) {
        header = 0;
        return DMX_STAT_INTERVAL;
    }

    if (!header++ || !(header % 10)) {
        dmxLog(dmxDebug,
               " S SyncCount  Sync/s avSync mxSync avPend mxPend | "
               "<10ms   <1s   >1s\n");
    }

    if (dmxStatDisplays && dmxStatDisplays < limit)
        limit = dmxStatDisplays;
    for (i = 0; i < limit; i++) {
        DMXScreenInfo *dmxScreen = &dmxScreens[i];
        DMXStatInfo *s = dmxScreen->stat;
        unsigned long aSync, mSync;
        unsigned long aPend, mPend;

        if (!s)
            continue;

        aSync = avg(&s->usec, &mSync);
        aPend = avg(&s->pending, &mPend);
        dmxLog(dmxDebug, "%2d %9lu %7lu %6lu %6lu %6lu %6lu |", i,      /* S */
               s->syncCount,    /* SyncCount */
               (s->syncCount - s->oldSyncCount) * 1000 / dmxStatInterval,       /* Sync/s */
               aSync,           /* us/Sync */
               mSync,           /* max/Sync */
               aPend,           /* avgPend */
               mPend);          /* maxPend */
        for (j = 0; j < DMX_STAT_BINS; j++)
            dmxLogCont(dmxDebug, " %5lu", s->bins[j]);
        dmxLogCont(dmxDebug, "\n");

        /* Reset/clear */
        s->oldSyncCount = s->syncCount;
        for (j = 0; j < DMX_STAT_BINS; j++)
            s->bins[j] = 0;
    }
    return DMX_STAT_INTERVAL;   /* Place on queue again */
}

/** Try to initialize the statistic gathering and printing routines.
 * Initialization only takes place if #dmxStatActivate has already been
 * called.  We don't need the same generation protection that we used in
 * dmxSyncInit because our timer is always on a queue -- hence, server
 * generation will always free it. */
void
dmxStatInit(void)
{
    if (dmxStatInterval)
        dmxStatTimer = TimerSet(NULL, 0,
                                dmxStatInterval, dmxStatCallback, NULL);
}
