/*
 * Copyright (c) 2016-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef _SVGA_MKSSTATS_H
#define _SVGA_MKSSTATS_H

#include "svga_winsys.h"

#ifdef VMX86_STATS
#define SVGA_STATS_COUNT_INC(_sws, _stat)                    \
   _sws->stats_inc(_sws, _stat);

#define SVGA_STATS_TIME_PUSH(_sws, _stat)                    \
   struct svga_winsys_stats_timeframe timeFrame;             \
   _sws->stats_time_push(_sws, _stat, &timeFrame);

#define SVGA_STATS_TIME_POP(_sws)                            \
   _sws->stats_time_pop(_sws);

#else

#define SVGA_STATS_COUNT_INC(_sws, _stat)
#define SVGA_STATS_TIME_PUSH(_sws, _stat)
#define SVGA_STATS_TIME_POP(_sws)

#endif
#endif /* _SVGA_MKSSTATS_H */
