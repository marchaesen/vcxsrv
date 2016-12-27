/*
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#ifndef AMD_FAMILY_H
#define AMD_FAMILY_H

enum radeon_family {
    CHIP_UNKNOWN = 0,
    CHIP_R300, /* R3xx-based cores. */
    CHIP_R350,
    CHIP_RV350,
    CHIP_RV370,
    CHIP_RV380,
    CHIP_RS400,
    CHIP_RC410,
    CHIP_RS480,
    CHIP_R420,     /* R4xx-based cores. */
    CHIP_R423,
    CHIP_R430,
    CHIP_R480,
    CHIP_R481,
    CHIP_RV410,
    CHIP_RS600,
    CHIP_RS690,
    CHIP_RS740,
    CHIP_RV515,    /* R5xx-based cores. */
    CHIP_R520,
    CHIP_RV530,
    CHIP_R580,
    CHIP_RV560,
    CHIP_RV570,
    CHIP_R600,
    CHIP_RV610,
    CHIP_RV630,
    CHIP_RV670,
    CHIP_RV620,
    CHIP_RV635,
    CHIP_RS780,
    CHIP_RS880,
    CHIP_RV770,
    CHIP_RV730,
    CHIP_RV710,
    CHIP_RV740,
    CHIP_CEDAR,
    CHIP_REDWOOD,
    CHIP_JUNIPER,
    CHIP_CYPRESS,
    CHIP_HEMLOCK,
    CHIP_PALM,
    CHIP_SUMO,
    CHIP_SUMO2,
    CHIP_BARTS,
    CHIP_TURKS,
    CHIP_CAICOS,
    CHIP_CAYMAN,
    CHIP_ARUBA,
    CHIP_TAHITI,
    CHIP_PITCAIRN,
    CHIP_VERDE,
    CHIP_OLAND,
    CHIP_HAINAN,
    CHIP_BONAIRE,
    CHIP_KAVERI,
    CHIP_KABINI,
    CHIP_HAWAII,
    CHIP_MULLINS,
    CHIP_TONGA,
    CHIP_ICELAND,
    CHIP_CARRIZO,
    CHIP_FIJI,
    CHIP_STONEY,
    CHIP_POLARIS10,
    CHIP_POLARIS11,
    CHIP_POLARIS12,
    CHIP_LAST,
};

enum chip_class {
    CLASS_UNKNOWN = 0,
    R300,
    R400,
    R500,
    R600,
    R700,
    EVERGREEN,
    CAYMAN,
    SI,
    CIK,
    VI,
};

#endif
