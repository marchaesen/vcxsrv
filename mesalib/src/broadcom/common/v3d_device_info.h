/*
 * Copyright © 2016 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef V3D_CHIP_H
#define V3D_CHIP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Struct for tracking features of the V3D chip across driver and compiler.
 */
struct v3d_device_info {
        /** Simple V3D version: major * 10 + minor */
        uint8_t ver;

        /** V3D revision number */
        uint8_t rev;

        /** V3D compatitiblity revision number */
        uint8_t compat_rev;

        /** Maximum number of performance counters for a given V3D version **/
        uint8_t max_perfcnt;

        /** Size of the VPM, in bytes. */
        int vpm_size;

        /** NSLC * QUPS from the core's IDENT registers. */
        int qpu_count;

        /** If the hw has accumulator registers */
        bool has_accumulators;

        /** Granularity for the Clipper XY Scaling */
        float clipper_xy_granularity;

        /** The Control List Executor (CLE) pre-fetches V3D_CLE_READAHEAD
         *  bytes from the Control List buffer. The usage of these last bytes
         *  should be avoided or the CLE would pre-fetch the data after the
         *  end of the CL buffer, reporting the kernel "MMU error from client
         *  CLE".
         */
        uint32_t cle_readahead;

        /** Minimum size for a buffer storing the Control List Executor (CLE) */
        uint32_t cle_buffer_min_size;
};

typedef int (*v3d_ioctl_fun)(int fd, unsigned long request, void *arg);

bool
v3d_get_device_info(int fd, struct v3d_device_info* devinfo, v3d_ioctl_fun fun);

static inline bool
v3d_device_has_draw_index(const struct v3d_device_info *devinfo)
{
        return devinfo->ver > 71 || (devinfo->ver == 71 && devinfo->rev >= 10);
}

static inline bool
v3d_device_has_unpack_sat(const struct v3d_device_info *devinfo)
{
        return devinfo->ver > 45 || (devinfo->ver == 45 && devinfo->rev >= 7);
}

static inline bool
v3d_device_has_unpack_max0(const struct v3d_device_info *devinfo)
{
        return devinfo->ver > 71 ||
               (devinfo->ver == 71 &&
                (devinfo->rev >= 7 ||
                 (devinfo->rev == 6 && devinfo->compat_rev >= 4)));
}

#endif
