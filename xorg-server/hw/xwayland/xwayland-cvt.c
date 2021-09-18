/*
 * Copyright 2005-2006 Luc Verhaegen.
 * Copyright © 2021 Red Hat, Inc.
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
 */

#include <xwayland-config.h>

#include <string.h>
#include <randrstr.h>
#include <libxcvt/libxcvt.h>

#include "xwayland-cvt.h"

RRModePtr
xwayland_cvt(int hdisplay, int vdisplay, float vrefresh, Bool reduced,
             Bool interlaced)
{
    struct libxcvt_mode_info *libxcvt_mode_info;
    char name[128];
    xRRModeInfo modeinfo;

    libxcvt_mode_info =
        libxcvt_gen_mode_info(hdisplay, vdisplay, vrefresh, reduced, interlaced);

    memset(&modeinfo, 0, sizeof modeinfo);
    modeinfo.width      = libxcvt_mode_info->hdisplay;
    modeinfo.height     = libxcvt_mode_info->vdisplay;
    modeinfo.dotClock   = libxcvt_mode_info->dot_clock * 1000.0;
    modeinfo.hSyncStart = libxcvt_mode_info->hsync_start;
    modeinfo.hSyncEnd   = libxcvt_mode_info->hsync_end;
    modeinfo.hTotal     = libxcvt_mode_info->htotal;
    modeinfo.vSyncStart = libxcvt_mode_info->vsync_start;
    modeinfo.vSyncEnd   = libxcvt_mode_info->vsync_end;
    modeinfo.vTotal     = libxcvt_mode_info->vtotal;
    modeinfo.modeFlags  = libxcvt_mode_info->mode_flags;

    free(libxcvt_mode_info);

    snprintf(name, sizeof name, "%dx%d",
             modeinfo.width, modeinfo.height);
    modeinfo.nameLength = strlen(name);

    return RRModeGet(&modeinfo, name);
}
