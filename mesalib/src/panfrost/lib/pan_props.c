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
#include "util/hash_table.h"
#include "util/u_thread.h"
#include "drm-uapi/panfrost_drm.h"
#include "pan_encoder.h"
#include "pan_device.h"
#include "panfrost-quirks.h"
#include "pan_bo.h"
#include "pan_texture.h"
#include "wrap.h"
#include "pan_util.h"

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

        get_param.param = param;
        ret = drmIoctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);

        if (ret) {
                assert(!required);
                return default_value;
        }

        return get_param.value;
}

static unsigned
panfrost_query_gpu_version(int fd)
{
#ifndef NDEBUG
        /* In debug builds, allow overriding the GPU ID, for example to run
         * Bifrost shader-db on a Midgard machine. This is a bit less heavy
         * handed than setting up the entirety of drm-shim */
        char *override_version = getenv("PAN_GPU_ID");

        if (override_version)
                return strtol(override_version, NULL, 16);
#endif

        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_PROD_ID, true, 0);
}

static unsigned
panfrost_query_gpu_revision(int fd)
{
        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_REVISION, true, 0);
}

static struct panfrost_tiler_features
panfrost_query_tiler_features(int fd)
{
        /* Default value (2^9 bytes and 8 levels) to match old behaviour */
        uint32_t raw = panfrost_query_raw(fd, DRM_PANFROST_PARAM_TILER_FEATURES,
                        false, 0x809);

        /* Bin size is log2 in the first byte, max levels in the second byte */
        return (struct panfrost_tiler_features) {
                .bin_size = (1 << (raw & BITFIELD_MASK(5))),
                .max_levels = (raw >> 8) & BITFIELD_MASK(4)
        };
}

static unsigned
panfrost_query_core_count(int fd)
{
        /* On older kernels, worst-case to 16 cores */

        unsigned mask = panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_SHADER_PRESENT, false, 0xffff);

        /* Some cores might be absent. For TLS computation purposes, we care
         * about the greatest ID + 1, which equals the core count if all cores
         * are present, but allocates space for absent cores if needed.
         * util_last_bit is defined to return the greatest bit set + 1, which
         * is exactly what we need. */

        return util_last_bit(mask);
}

/* Architectural maximums, since this register may be not implemented
 * by a given chip. G31 is actually 512 instead of 768 but it doesn't
 * really matter. */

static unsigned
panfrost_max_thread_count(unsigned arch)
{
        switch (arch) {
        /* Midgard */
        case 4:
        case 5:
                return 256;

        /* Bifrost, first generation */
        case 6:
                return 384;

        /* Bifrost, second generation (G31 is 512 but it doesn't matter) */
        case 7:
                return 768;

        /* Valhall (for completeness) */
        default:
                return 1024;
        }
}

static unsigned
panfrost_query_thread_tls_alloc(int fd, unsigned major)
{
        unsigned tls = panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_THREAD_TLS_ALLOC, false, 0);

        return (tls > 0) ? tls : panfrost_max_thread_count(major);
}

static uint32_t
panfrost_query_compressed_formats(int fd)
{
        /* If unspecified, assume ASTC/ETC only. Factory default for Juno, and
         * should exist on any Mali configuration. All hardware should report
         * these texture formats but the kernel might not be new enough. */

        uint32_t default_set =
                (1 << MALI_ETC2_RGB8) |
                (1 << MALI_ETC2_R11_UNORM) |
                (1 << MALI_ETC2_RGBA8) |
                (1 << MALI_ETC2_RG11_UNORM) |
                (1 << MALI_ETC2_R11_SNORM) |
                (1 << MALI_ETC2_RG11_SNORM) |
                (1 << MALI_ETC2_RGB8A1) |
                (1 << MALI_ASTC_3D_LDR) |
                (1 << MALI_ASTC_3D_HDR) |
                (1 << MALI_ASTC_2D_LDR) |
                (1 << MALI_ASTC_2D_HDR);

        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_TEXTURE_FEATURES0,
                        false, default_set);
}

/* DRM_PANFROST_PARAM_TEXTURE_FEATURES0 will return a bitmask of supported
 * compressed formats, so we offer a helper to test if a format is supported */

bool
panfrost_supports_compressed_format(struct panfrost_device *dev, unsigned fmt)
{
        if (MALI_EXTRACT_TYPE(fmt) != MALI_FORMAT_COMPRESSED)
                return true;

        unsigned idx = fmt & ~MALI_FORMAT_COMPRESSED;
        assert(idx < 32);

        return dev->compressed_formats & (1 << idx);
}

/* Given a GPU ID like 0x860, return a prettified model name */

const char *
panfrost_model_name(unsigned gpu_id)
{
        switch (gpu_id) {
        case 0x600: return "Mali-T600 (Panfrost)";
        case 0x620: return "Mali-T620 (Panfrost)";
        case 0x720: return "Mali-T720 (Panfrost)";
        case 0x820: return "Mali-T820 (Panfrost)";
        case 0x830: return "Mali-T830 (Panfrost)";
        case 0x750: return "Mali-T760 (Panfrost)";
        case 0x860: return "Mali-T860 (Panfrost)";
        case 0x880: return "Mali-T880 (Panfrost)";
        case 0x6221: return "Mali-G72 (Panfrost)";
        case 0x7093: return "Mali-G31 (Panfrost)";
        case 0x7212: return "Mali-G52 (Panfrost)";
        case 0x7402: return "Mali-G52 r1 (Panfrost)";
        default:
                    unreachable("Invalid GPU ID");
        }
}

/* Check for AFBC hardware support. AFBC is introduced in v5. Implementations
 * may omit it, signaled as a nonzero value in the AFBC_FEATURES property. */

static bool
panfrost_query_afbc(int fd, unsigned arch)
{
        unsigned reg = panfrost_query_raw(fd,
                                          DRM_PANFROST_PARAM_AFBC_FEATURES,
                                          false, 0);

        return (arch >= 5) && (reg == 0);
}

void
panfrost_open_device(void *memctx, int fd, struct panfrost_device *dev)
{
        dev->fd = fd;
        dev->memctx = memctx;
        dev->gpu_id = panfrost_query_gpu_version(fd);
        dev->arch = pan_arch(dev->gpu_id);
        dev->core_count = panfrost_query_core_count(fd);
        dev->thread_tls_alloc = panfrost_query_thread_tls_alloc(fd, dev->arch);
        dev->kernel_version = drmGetVersion(fd);
        unsigned revision = panfrost_query_gpu_revision(fd);
        dev->quirks = panfrost_get_quirks(dev->gpu_id, revision);
        dev->compressed_formats = panfrost_query_compressed_formats(fd);
        dev->tiler_features = panfrost_query_tiler_features(fd);
        dev->has_afbc = panfrost_query_afbc(fd, dev->arch);

        if (dev->quirks & HAS_SWIZZLES)
                dev->formats = panfrost_pipe_format_v6;
        else
                dev->formats = panfrost_pipe_format_v7;

        util_sparse_array_init(&dev->bo_map, sizeof(struct panfrost_bo), 512);

        pthread_mutex_init(&dev->bo_cache.lock, NULL);
        list_inithead(&dev->bo_cache.lru);

        for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); ++i)
                list_inithead(&dev->bo_cache.buckets[i]);

        /* Initialize pandecode before we start allocating */
        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                pandecode_initialize(!(dev->debug & PAN_DBG_TRACE));

        /* Tiler heap is internally required by the tiler, which can only be
         * active for a single job chain at once, so a single heap can be
         * shared across batches/contextes */

        dev->tiler_heap = panfrost_bo_create(dev, 64 * 1024 * 1024,
                        PAN_BO_INVISIBLE | PAN_BO_GROWABLE, "Tiler heap");

        pthread_mutex_init(&dev->submit_lock, NULL);

        /* Done once on init */
        panfrost_upload_sample_positions(dev);
}

void
panfrost_close_device(struct panfrost_device *dev)
{
        pthread_mutex_destroy(&dev->submit_lock);
        panfrost_bo_unreference(dev->tiler_heap);
        panfrost_bo_cache_evict_all(dev);
        pthread_mutex_destroy(&dev->bo_cache.lock);
        drmFreeVersion(dev->kernel_version);
        util_sparse_array_finish(&dev->bo_map);
        close(dev->fd);
}
