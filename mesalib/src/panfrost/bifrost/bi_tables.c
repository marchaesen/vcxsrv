/*
 * Copyright (C) 2020 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"

unsigned bi_class_props[BI_NUM_CLASSES] = {
        [BI_ADD] 		= BI_MODS | BI_SCHED_ALL | BI_NO_ABS_ABS_FP16_FMA,
        [BI_ATEST] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD,
        [BI_BRANCH] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_CONDITIONAL,
        [BI_CMP] 		= BI_MODS | BI_SCHED_ALL | BI_CONDITIONAL,
        [BI_BLEND] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_SRC,
        [BI_BITWISE] 		= BI_SCHED_ALL,
        [BI_COMBINE] 		= 0,
        [BI_CONVERT] 		= BI_SCHED_ADD | BI_SWIZZLABLE | BI_ROUNDMODE, /* +FMA on G71 */
        [BI_CSEL] 		= BI_SCHED_FMA | BI_CONDITIONAL,
        [BI_DISCARD] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_CONDITIONAL,
        [BI_FMA] 		= BI_ROUNDMODE | BI_SCHED_FMA | BI_MODS,
        [BI_FREXP] 		= BI_SCHED_ALL,
        [BI_IMATH] 		= BI_SCHED_ALL,
        [BI_LOAD] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_LOAD_UNIFORM]	= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_LOAD_ATTR] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_LOAD_VAR] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_LOAD_VAR_ADDRESS] 	= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_LOAD_TILE]		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_MINMAX] 		= BI_SCHED_ADD | BI_NO_ABS_ABS_FP16_FMA | BI_MODS,
        [BI_MOV] 		= BI_SCHED_ALL,
        [BI_FMOV]               = BI_MODS | BI_SCHED_ALL,
        [BI_REDUCE_FMA]         = BI_SCHED_FMA,
        [BI_STORE] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_SRC,
        [BI_STORE_VAR] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_SRC,
        [BI_SPECIAL_ADD]	= BI_SCHED_ADD | BI_SCHED_SLOW,
        [BI_SPECIAL_FMA]	= BI_SCHED_FMA | BI_SCHED_SLOW,
        [BI_TABLE]              = BI_SCHED_ADD,
        [BI_SELECT]             = BI_SCHED_ALL | BI_SWIZZLABLE,
        [BI_TEXS] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_TEXC] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_SRC | BI_DATA_REG_DEST,
        [BI_TEXC_DUAL] 		= BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_VECTOR | BI_DATA_REG_DEST,
        [BI_ROUND] 		= BI_ROUNDMODE | BI_SCHED_ALL,
        [BI_IMUL]       = BI_SCHED_FMA,
        [BI_ZS_EMIT]            = BI_SCHED_HI_LATENCY | BI_SCHED_ADD | BI_DATA_REG_DEST,
};
