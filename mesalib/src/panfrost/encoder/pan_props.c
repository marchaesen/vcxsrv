/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <xf86drm.h>

#include "util/u_math.h"
#include "util/macros.h"
#include "drm-uapi/panfrost_drm.h"
#include "pan_encoder.h"

/* Abstraction over the raw drm_panfrost_get_param ioctl for fetching
 * information about devices */

static __u64
panfrost_query_raw(
                int fd,
                enum drm_panfrost_param param,
                bool required,
                unsigned default_value)
{
        struct drm_panfrost_get_param get_param = {0,};
        ASSERTED int ret;

        get_param.param = DRM_PANFROST_PARAM_GPU_PROD_ID;
        ret = drmIoctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);

        if (ret) {
                assert(!required);
                return default_value;
        }

        return get_param.value;
}

unsigned
panfrost_query_gpu_version(int fd)
{
        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_PROD_ID, true, 0);
}

unsigned
panfrost_query_core_count(int fd)
{
        /* On older kernels, worst-case to 16 cores */

        unsigned mask = panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_SHADER_PRESENT, false, 0xffff);

        return util_bitcount(mask);
}

unsigned
panfrost_query_thread_tls_alloc(int fd)
{
        /* On older kernels, we worst-case to 1024 threads, the architectural
         * maximum for Midgard */

        return panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_THREAD_TLS_ALLOC, false, 1024);
}

/* Given a GPU ID like 0x860, return a prettified model name */

const char *
panfrost_model_name(unsigned gpu_id)
{
        switch (gpu_id) {
        case 0x600: return "Mali T600 (Panfrost)";
        case 0x620: return "Mali T620 (Panfrost)";
        case 0x720: return "Mali T720 (Panfrost)";
        case 0x820: return "Mali T820 (Panfrost)";
        case 0x830: return "Mali T830 (Panfrost)";
        case 0x750: return "Mali T760 (Panfrost)";
        case 0x860: return "Mali T860 (Panfrost)";
        case 0x880: return "Mali T880 (Panfrost)";
        default:
                    unreachable("Invalid GPU ID");
        }
}
