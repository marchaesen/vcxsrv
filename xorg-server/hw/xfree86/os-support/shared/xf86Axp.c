/*
 * Copyright (c) 2000 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86Axp.h"

axpParams xf86AXPParams[] = {
    {SYS_NONE, 0, 0, 0}
    ,
    {TSUNAMI, 0, 0, 0}
    ,
    {LCA, 1 << 24, 0xf8000000, 1UL << 32}
    ,
    {APECS, 1 << 24, 0xf8000000, 1UL << 32}
    ,
    {T2, 0, 0xFC000000, 1UL << 31}
    ,
    {T2_GAMMA, 0, 0xFC000000, 1UL << 31}
    ,
    {CIA, 0, 0xE0000000, 1UL << 34}
    ,
    {MCPCIA, 0, 0xf8000000, 1UL << 31}
    ,
    {JENSEN, 0, 0xE000000, 1UL << 32}
    ,
    {POLARIS, 0, 0, 0}
    ,
    {PYXIS, 0, 0, 0}
    ,
    {PYXIS_CIA, 0, 0xE0000000, 1UL << 34}
    ,
    {IRONGATE, 0, 0, 0}
};
