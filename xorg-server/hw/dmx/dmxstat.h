/*
 * Copyright 2002 Red Hat Inc., Durham, North Carolina.
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
 * Interface for statistic gathering interface. \see dmxstat.c */

#ifndef _DMXSTAT_H_
#define _DMXSTAT_H_

#define DMX_STAT_LENGTH     10  /**< number of events for moving average */
#define DMX_STAT_INTERVAL 1000  /**< msec between printouts */
#define DMX_STAT_BINS        3  /**< number of bins */
#define DMX_STAT_BIN0    10000  /**< us for bin[0] */
#define DMX_STAT_BINMULT   100  /**< multiplier for next bin[] */

extern int dmxStatInterval;         /**< Only for dmxstat.c and dmxsync.c */
extern void dmxStatActivate(const char *interval, const char *displays);
extern DMXStatInfo *dmxStatAlloc(void);
extern void dmxStatFree(DMXStatInfo *);
extern void dmxStatInit(void);
extern void dmxStatSync(DMXScreenInfo * dmxScreen,
                        struct timeval *stop, struct timeval *start,
                        unsigned long pending);

#endif
