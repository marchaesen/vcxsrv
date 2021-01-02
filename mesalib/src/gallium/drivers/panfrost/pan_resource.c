/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 * Copyright (C) 2014-2017 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
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
 * Authors (Collabora):
 *   Tomeu Vizoso <tomeu.vizoso@collabora.com>
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include <xf86drm.h>
#include <fcntl.h>
#include "drm-uapi/drm_fourcc.h"

#include "frontend/winsys_handle.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"
#include "util/u_transfer_helper.h"
#include "util/u_gen_mipmap.h"
#include "util/u_drm.h"

#include "pan_bo.h"
#include "pan_context.h"
#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_util.h"
#include "pan_tiling.h"
#include "decode.h"
#include "panfrost-quirks.h"

bool
pan_render_condition_check(struct pipe_context *pctx)
{
	struct panfrost_context *ctx = pan_context(pctx);

	if (!ctx->cond_query)
		return true;

	union pipe_query_result res = { 0 };
	bool wait =
		ctx->cond_mode != PIPE_RENDER_COND_NO_WAIT &&
		ctx->cond_mode != PIPE_RENDER_COND_BY_REGION_NO_WAIT;

	if (pctx->get_query_result(pctx, (struct pipe_query *) ctx->cond_query, wait, &res))
			return (bool)res.u64 != ctx->cond_cond;

	return true;
}

static struct pipe_resource *
panfrost_resource_from_handle(struct pipe_screen *pscreen,
                              const struct pipe_resource *templat,
                              struct winsys_handle *whandle,
                              unsigned usage)
{
        struct panfrost_device *dev = pan_device(pscreen);
        struct panfrost_resource *rsc;
        struct pipe_resource *prsc;

        assert(whandle->type == WINSYS_HANDLE_TYPE_FD);

        rsc = rzalloc(pscreen, struct panfrost_resource);
        if (!rsc)
                return NULL;

        prsc = &rsc->base;

        *prsc = *templat;

        pipe_reference_init(&prsc->reference, 1);
        prsc->screen = pscreen;

        rsc->bo = panfrost_bo_import(dev, whandle->handle);
        rsc->internal_format = templat->format;
        rsc->modifier = (whandle->modifier == DRM_FORMAT_MOD_INVALID) ?
                DRM_FORMAT_MOD_LINEAR : whandle->modifier;
        rsc->modifier_constant = true;
        rsc->slices[0].line_stride = whandle->stride;
        rsc->slices[0].row_stride = whandle->stride;

        if (rsc->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED ||
            drm_is_afbc(rsc->modifier)) {
                unsigned tile_h = panfrost_block_dim(rsc->modifier, false, 0);

                if (util_format_is_compressed(rsc->internal_format))
                        tile_h >>= 2;

                rsc->slices[0].row_stride *= tile_h;
        }

        rsc->slices[0].offset = whandle->offset;
        rsc->slices[0].initialized = true;
        panfrost_resource_set_damage_region(NULL, &rsc->base, 0, NULL);

        if (dev->quirks & IS_BIFROST &&
            templat->bind & PIPE_BIND_RENDER_TARGET) {
                unsigned size = panfrost_compute_checksum_size(
                                        &rsc->slices[0], templat->width0, templat->height0);
                rsc->slices[0].checksum_bo = panfrost_bo_create(dev, size, 0);
                rsc->checksummed = true;
        }

        if (drm_is_afbc(whandle->modifier)) {
                rsc->slices[0].header_size =
                        panfrost_afbc_header_size(templat->width0, templat->height0);
        }

        if (dev->ro) {
                rsc->scanout =
                        renderonly_create_gpu_import_for_resource(prsc, dev->ro, NULL);
                /* failure is expected in some cases.. */
        }

        return prsc;
}

static bool
panfrost_resource_get_handle(struct pipe_screen *pscreen,
                             struct pipe_context *ctx,
                             struct pipe_resource *pt,
                             struct winsys_handle *handle,
                             unsigned usage)
{
        struct panfrost_device *dev = pan_device(pscreen);
        struct panfrost_resource *rsrc = (struct panfrost_resource *) pt;
        struct renderonly_scanout *scanout = rsrc->scanout;

        handle->modifier = rsrc->modifier;
        rsrc->modifier_constant = true;

        if (handle->type == WINSYS_HANDLE_TYPE_SHARED) {
                return false;
        } else if (handle->type == WINSYS_HANDLE_TYPE_KMS) {
                if (renderonly_get_handle(scanout, handle))
                        return true;

                handle->handle = rsrc->bo->gem_handle;
                handle->stride = rsrc->slices[0].line_stride;
                handle->offset = rsrc->slices[0].offset;
                return TRUE;
        } else if (handle->type == WINSYS_HANDLE_TYPE_FD) {
                if (scanout) {
                        struct drm_prime_handle args = {
                                .handle = scanout->handle,
                                .flags = DRM_CLOEXEC,
                        };

                        int ret = drmIoctl(dev->ro->kms_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
                        if (ret == -1)
                                return false;

                        handle->stride = scanout->stride;
                        handle->handle = args.fd;

                        return true;
                } else {
                        int fd = panfrost_bo_export(rsrc->bo);

                        if (fd < 0)
                                return false;

                        handle->handle = fd;
                        handle->stride = rsrc->slices[0].line_stride;
                        handle->offset = rsrc->slices[0].offset;
                        return true;
                }
        }

        return false;
}

static void
panfrost_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        /* TODO */
}

static struct pipe_surface *
panfrost_create_surface(struct pipe_context *pipe,
                        struct pipe_resource *pt,
                        const struct pipe_surface *surf_tmpl)
{
        struct pipe_surface *ps = NULL;

        ps = rzalloc(pipe, struct pipe_surface);

        if (ps) {
                pipe_reference_init(&ps->reference, 1);
                pipe_resource_reference(&ps->texture, pt);
                ps->context = pipe;
                ps->format = surf_tmpl->format;

                if (pt->target != PIPE_BUFFER) {
                        assert(surf_tmpl->u.tex.level <= pt->last_level);
                        ps->width = u_minify(pt->width0, surf_tmpl->u.tex.level);
                        ps->height = u_minify(pt->height0, surf_tmpl->u.tex.level);
                        ps->nr_samples = surf_tmpl->nr_samples;
                        ps->u.tex.level = surf_tmpl->u.tex.level;
                        ps->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
                        ps->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
                } else {
                        /* setting width as number of elements should get us correct renderbuffer width */
                        ps->width = surf_tmpl->u.buf.last_element - surf_tmpl->u.buf.first_element + 1;
                        ps->height = pt->height0;
                        ps->u.buf.first_element = surf_tmpl->u.buf.first_element;
                        ps->u.buf.last_element = surf_tmpl->u.buf.last_element;
                        assert(ps->u.buf.first_element <= ps->u.buf.last_element);
                        assert(ps->u.buf.last_element < ps->width);
                }
        }

        return ps;
}

static void
panfrost_surface_destroy(struct pipe_context *pipe,
                         struct pipe_surface *surf)
{
        assert(surf->texture);
        pipe_resource_reference(&surf->texture, NULL);
        ralloc_free(surf);
}

static struct pipe_resource *
panfrost_create_scanout_res(struct pipe_screen *screen,
                            const struct pipe_resource *template,
                            uint64_t modifier)
{
        struct panfrost_device *dev = pan_device(screen);
        struct renderonly_scanout *scanout;
        struct winsys_handle handle;
        struct pipe_resource *res;
        struct pipe_resource scanout_templat = *template;

        /* Tiled formats need to be tile aligned */
        if (modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                scanout_templat.width0 = ALIGN_POT(template->width0, 16);
                scanout_templat.height0 = ALIGN_POT(template->height0, 16);
        }

        /* AFBC formats need a header. Thankfully we don't care about the
         * stride so we can just use wonky dimensions as long as the right
         * number of bytes are allocated at the end of the day... this implies
         * that stride/pitch is invalid for AFBC buffers */

        if (drm_is_afbc(modifier)) {
                /* Space for the header. We need to keep vaguely similar
                 * dimensions because... reasons... to allocate with renderonly
                 * as a dumb buffer. To do so, after the usual 16x16 alignment,
                 * we add on extra rows for the header. The order of operations
                 * matters here, the extra rows of padding can in fact be
                 * needed and missing them can lead to faults. */

                unsigned header_size = panfrost_afbc_header_size(
                                template->width0, template->height0);

                unsigned pitch = ALIGN_POT(template->width0, 16) *
                        util_format_get_blocksize(template->format);

                unsigned header_rows =
                        DIV_ROUND_UP(header_size, pitch);

                scanout_templat.width0 = ALIGN_POT(template->width0, 16);
                scanout_templat.height0 = ALIGN_POT(template->height0, 16) + header_rows;
        }

        scanout = renderonly_scanout_for_resource(&scanout_templat,
                        dev->ro, &handle);
        if (!scanout)
                return NULL;

        assert(handle.type == WINSYS_HANDLE_TYPE_FD);
        handle.modifier = modifier;
        res = screen->resource_from_handle(screen, template, &handle,
                                           PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
        close(handle.handle);
        if (!res)
                return NULL;

        struct panfrost_resource *pres = pan_resource(res);

        pres->scanout = scanout;

        return res;
}

/* Setup the mip tree given a particular modifier, possibly with checksumming */

static void
panfrost_setup_slices(struct panfrost_device *dev,
                      struct panfrost_resource *pres,
                      size_t *bo_size)
{
        struct pipe_resource *res = &pres->base;
        unsigned width = res->width0;
        unsigned height = res->height0;
        unsigned depth = res->depth0;
        unsigned bytes_per_pixel = util_format_get_blocksize(pres->internal_format);

        /* Z32_S8X24 variants are actually stored in 2 planes (one per
         * component), we have to adjust the bytes_per_pixel value accordingly.
         */
        if (pres->internal_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
            pres->internal_format == PIPE_FORMAT_X32_S8X24_UINT)
                bytes_per_pixel = 4;

        /* MSAA is implemented as a 3D texture with z corresponding to the
         * sample #, horrifyingly enough */

        bool msaa = res->nr_samples > 1;

        if (msaa) {
                assert(depth == 1);
                depth = res->nr_samples;
        }

        assert(depth > 0);

        /* Tiled operates blockwise; linear is packed. Also, anything
         * we render to has to be tile-aligned. Maybe not strictly
         * necessary, but we're not *that* pressed for memory and it
         * makes code a lot simpler */

        bool renderable = res->bind &
                          (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL) &&
                          res->target != PIPE_BUFFER;
        bool afbc = drm_is_afbc(pres->modifier);
        bool tiled = pres->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
        bool linear = pres->modifier == DRM_FORMAT_MOD_LINEAR;
        bool should_align = renderable || tiled || afbc;

        unsigned offset = 0;
        unsigned size_2d = 0;
        unsigned tile_h = 1, tile_w = 1, tile_shift = 0;

        if (tiled || afbc) {
                tile_w = panfrost_block_dim(pres->modifier, true, 0);
                tile_h = panfrost_block_dim(pres->modifier, false, 0);
                if (util_format_is_compressed(pres->internal_format))
                        tile_shift = 2;
        }

        for (unsigned l = 0; l <= res->last_level; ++l) {
                struct panfrost_slice *slice = &pres->slices[l];

                unsigned effective_width = width;
                unsigned effective_height = height;
                unsigned effective_depth = depth;

                if (should_align) {
                        effective_width = ALIGN_POT(effective_width, tile_w) >> tile_shift;
                        effective_height = ALIGN_POT(effective_height, tile_h);

                        /* We don't need to align depth */
                }

                /* Align levels to cache-line as a performance improvement for
                 * linear/tiled and as a requirement for AFBC */

                offset = ALIGN_POT(offset, 64);

                slice->offset = offset;

                /* Compute the would-be stride */
                unsigned stride = bytes_per_pixel * effective_width;

                /* On Bifrost, pixel lines have to be aligned on 64 bytes otherwise
                 * we end up with DATA_INVALID faults. That doesn't seem to be
                 * mandatory on Midgard, but we keep the alignment for performance.
                 */
                if (linear)
                        stride = ALIGN_POT(stride, 64);

                slice->line_stride = stride;
                slice->row_stride = stride * (tile_h >> tile_shift);

                unsigned slice_one_size = slice->line_stride * effective_height;
                unsigned slice_full_size = slice_one_size * effective_depth;

                slice->size0 = slice_one_size;

                /* Report 2D size for 3D texturing */

                if (l == 0)
                        size_2d = slice_one_size;

                /* Compute AFBC sizes if necessary */
                if (afbc) {
                        slice->header_size =
                                panfrost_afbc_header_size(width, height);

                        offset += slice->header_size;
                }

                offset += slice_full_size;

                /* Add a checksum region if necessary */
                if (pres->checksummed) {
                        slice->checksum_offset = offset;

                        unsigned size = panfrost_compute_checksum_size(
                                                slice, width, height);

                        offset += size;
                }

                width = u_minify(width, 1);
                height = u_minify(height, 1);

                /* Don't mipmap the sample count */
                if (!msaa)
                        depth = u_minify(depth, 1);
        }

        assert(res->array_size);

        if (res->target != PIPE_TEXTURE_3D) {
                /* Arrays and cubemaps have the entire miptree duplicated */

                pres->cubemap_stride = ALIGN_POT(offset, 64);
                if (bo_size)
                        *bo_size = ALIGN_POT(pres->cubemap_stride * res->array_size, 4096);
        } else {
                /* 3D strides across the 2D layers */
                assert(res->array_size == 1);

                pres->cubemap_stride = size_2d;
                if (bo_size)
                        *bo_size = ALIGN_POT(offset, 4096);
        }
}

/* Based on the usage, determine if it makes sense to use u-inteleaved tiling.
 * We only have routines to tile 2D textures of sane bpps. On the hardware
 * level, not all usages are valid for tiling. Finally, if the app is hinting
 * that the contents frequently change, tiling will be a loss.
 *
 * On platforms where it is supported, AFBC is even better. */

static bool
panfrost_should_afbc(struct panfrost_device *dev, const struct panfrost_resource *pres)
{
        /* AFBC resources may be rendered to, textured from, or shared across
         * processes, but may not be used as e.g buffers */
        const unsigned valid_binding =
                PIPE_BIND_DEPTH_STENCIL |
                PIPE_BIND_RENDER_TARGET |
                PIPE_BIND_BLENDABLE |
                PIPE_BIND_SAMPLER_VIEW |
                PIPE_BIND_DISPLAY_TARGET |
                PIPE_BIND_SCANOUT |
                PIPE_BIND_SHARED;

        if (pres->base.bind & ~valid_binding)
                return false;

        /* AFBC introduced with Mali T760 */
        if (dev->quirks & MIDGARD_NO_AFBC)
                return false;

        /* AFBC<-->staging is expensive */
        if (pres->base.usage == PIPE_USAGE_STREAM)
                return false;

        /* Only a small selection of formats are AFBC'able */
        if (!panfrost_format_supports_afbc(pres->internal_format))
                return false;

        /* AFBC does not support layered (GLES3 style) multisampling. Use
         * EXT_multisampled_render_to_texture instead */
        if (pres->base.nr_samples > 1)
                return false;

        /* TODO: Is AFBC of 3D textures possible? */
        if ((pres->base.target != PIPE_TEXTURE_2D) && (pres->base.target != PIPE_TEXTURE_RECT))
                return false;

        /* For one tile, AFBC is a loss compared to u-interleaved */
        if (pres->base.width0 <= 16 && pres->base.height0 <= 16)
                return false;

        /* Otherwise, we'd prefer AFBC as it is dramatically more efficient
         * than linear or usually even u-interleaved */
        return true;
}

static bool
panfrost_should_tile(struct panfrost_device *dev, const struct panfrost_resource *pres)
{
        const unsigned valid_binding =
                PIPE_BIND_DEPTH_STENCIL |
                PIPE_BIND_RENDER_TARGET |
                PIPE_BIND_BLENDABLE |
                PIPE_BIND_SAMPLER_VIEW |
                PIPE_BIND_DISPLAY_TARGET |
                PIPE_BIND_SCANOUT |
                PIPE_BIND_SHARED;

        unsigned bpp = util_format_get_blocksizebits(pres->internal_format);

        bool is_sane_bpp =
                bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32 ||
                bpp == 64 || bpp == 128;

        bool is_2d = (pres->base.target == PIPE_TEXTURE_2D)
                || (pres->base.target == PIPE_TEXTURE_RECT);

        bool can_tile = is_2d && is_sane_bpp && ((pres->base.bind & ~valid_binding) == 0);

        return can_tile && (pres->base.usage != PIPE_USAGE_STREAM);
}

static uint64_t
panfrost_best_modifier(struct panfrost_device *dev,
                const struct panfrost_resource *pres)
{
        if (panfrost_should_afbc(dev, pres)) {
                uint64_t afbc =
                        AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                        AFBC_FORMAT_MOD_SPARSE;

                if (panfrost_afbc_can_ytr(pres->base.format))
                        afbc |= AFBC_FORMAT_MOD_YTR;

                return DRM_FORMAT_MOD_ARM_AFBC(afbc);
        } else if (panfrost_should_tile(dev, pres))
                return DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
        else
                return DRM_FORMAT_MOD_LINEAR;
}

static void
panfrost_resource_setup(struct panfrost_device *dev, struct panfrost_resource *pres,
                        size_t *bo_size, uint64_t modifier)
{
        pres->modifier = (modifier != DRM_FORMAT_MOD_INVALID) ? modifier :
                panfrost_best_modifier(dev, pres);
        pres->checksummed = (pres->base.bind & PIPE_BIND_RENDER_TARGET);

        /* We can only switch tiled->linear if the resource isn't already
         * linear and if we control the modifier */
        pres->modifier_constant = !((pres->modifier != DRM_FORMAT_MOD_LINEAR)
                        && (modifier == DRM_FORMAT_MOD_INVALID));

        panfrost_setup_slices(dev, pres, bo_size);
}

void
panfrost_resource_set_damage_region(struct pipe_screen *screen,
                                    struct pipe_resource *res,
                                    unsigned int nrects,
                                    const struct pipe_box *rects)
{
        struct panfrost_resource *pres = pan_resource(res);
        struct pipe_scissor_state *damage_extent = &pres->damage.extent;
        unsigned int i;

        if (pres->damage.inverted_rects)
                ralloc_free(pres->damage.inverted_rects);

        memset(&pres->damage, 0, sizeof(pres->damage));

        pres->damage.inverted_rects =
                pan_subtract_damage(pres,
                        res->width0, res->height0,
                        nrects, rects, &pres->damage.inverted_len);

        /* Track the damage extent: the quad including all damage regions. Will
         * be used restrict the rendering area */

        damage_extent->minx = 0xffff;
        damage_extent->miny = 0xffff;

        for (i = 0; i < nrects; i++) {
                int x = rects[i].x, w = rects[i].width, h = rects[i].height;
                int y = res->height0 - (rects[i].y + h);

                damage_extent->minx = MIN2(damage_extent->minx, x);
                damage_extent->miny = MIN2(damage_extent->miny, y);
                damage_extent->maxx = MAX2(damage_extent->maxx,
                                           MIN2(x + w, res->width0));
                damage_extent->maxy = MAX2(damage_extent->maxy,
                                           MIN2(y + h, res->height0));
        }

        if (nrects == 0) {
                damage_extent->minx = 0;
                damage_extent->miny = 0;
                damage_extent->maxx = res->width0;
                damage_extent->maxy = res->height0;
        }

}

static struct pipe_resource *
panfrost_resource_create_with_modifier(struct pipe_screen *screen,
                         const struct pipe_resource *template,
                         uint64_t modifier)
{
        struct panfrost_device *dev = pan_device(screen);

        /* Make sure we're familiar */
        switch (template->target) {
        case PIPE_BUFFER:
        case PIPE_TEXTURE_1D:
        case PIPE_TEXTURE_2D:
        case PIPE_TEXTURE_3D:
        case PIPE_TEXTURE_CUBE:
        case PIPE_TEXTURE_RECT:
        case PIPE_TEXTURE_1D_ARRAY:
        case PIPE_TEXTURE_2D_ARRAY:
                break;
        default:
                unreachable("Unknown texture target\n");
        }

        if (dev->ro && (template->bind &
            (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)))
                return panfrost_create_scanout_res(screen, template, modifier);

        struct panfrost_resource *so = rzalloc(screen, struct panfrost_resource);
        so->base = *template;
        so->base.screen = screen;
        so->internal_format = template->format;

        pipe_reference_init(&so->base.reference, 1);

        util_range_init(&so->valid_buffer_range);

        size_t bo_size;
        panfrost_resource_setup(dev, so, &bo_size, modifier);

        /* We create a BO immediately but don't bother mapping, since we don't
         * care to map e.g. FBOs which the CPU probably won't touch */
        so->bo = panfrost_bo_create(dev, bo_size, PAN_BO_DELAY_MMAP);

        panfrost_resource_set_damage_region(NULL, &so->base, 0, NULL);

        if (template->bind & PIPE_BIND_INDEX_BUFFER)
                so->index_cache = rzalloc(so, struct panfrost_minmax_cache);

        return (struct pipe_resource *)so;
}

/* Default is to create a resource as don't care */

static struct pipe_resource *
panfrost_resource_create(struct pipe_screen *screen,
                         const struct pipe_resource *template)
{
        return panfrost_resource_create_with_modifier(screen, template,
                        DRM_FORMAT_MOD_INVALID);
}

/* If no modifier is specified, we'll choose. Otherwise, the order of
 * preference is compressed, tiled, linear. */

static struct pipe_resource *
panfrost_resource_create_with_modifiers(struct pipe_screen *screen,
                         const struct pipe_resource *template,
                         const uint64_t *modifiers, int count)
{
        for (unsigned i = 0; i < PAN_MODIFIER_COUNT; ++i) {
                if (drm_find_modifier(pan_best_modifiers[i], modifiers, count)) {
                        return panfrost_resource_create_with_modifier(screen, template,
                                        pan_best_modifiers[i]);
                }
        }

        /* If we didn't find one, app specified invalid */
        assert(count == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID);
        return panfrost_resource_create(screen, template);
}

static void
panfrost_resource_destroy(struct pipe_screen *screen,
                          struct pipe_resource *pt)
{
        struct panfrost_device *dev = pan_device(screen);
        struct panfrost_resource *rsrc = (struct panfrost_resource *) pt;

        if (rsrc->scanout)
                renderonly_scanout_destroy(rsrc->scanout, dev->ro);

        if (rsrc->bo)
                panfrost_bo_unreference(rsrc->bo);

        if (rsrc->slices[0].checksum_bo)
                panfrost_bo_unreference(rsrc->slices[0].checksum_bo);

        util_range_destroy(&rsrc->valid_buffer_range);
        ralloc_free(rsrc);
}

/* Most of the time we can do CPU-side transfers, but sometimes we need to use
 * the 3D pipe for this. Let's wrap u_blitter to blit to/from staging textures.
 * Code adapted from freedreno */

static struct panfrost_resource *
pan_alloc_staging(struct panfrost_context *ctx, struct panfrost_resource *rsc,
		unsigned level, const struct pipe_box *box)
{
        struct pipe_context *pctx = &ctx->base;
        struct pipe_resource tmpl = rsc->base;

        tmpl.width0  = box->width;
        tmpl.height0 = box->height;
        /* for array textures, box->depth is the array_size, otherwise
         * for 3d textures, it is the depth:
         */
        if (tmpl.array_size > 1) {
                if (tmpl.target == PIPE_TEXTURE_CUBE)
                        tmpl.target = PIPE_TEXTURE_2D_ARRAY;
                tmpl.array_size = box->depth;
                tmpl.depth0 = 1;
        } else {
                tmpl.array_size = 1;
                tmpl.depth0 = box->depth;
        }
        tmpl.last_level = 0;
        tmpl.bind |= PIPE_BIND_LINEAR;

        struct pipe_resource *pstaging =
                pctx->screen->resource_create(pctx->screen, &tmpl);
        if (!pstaging)
                return NULL;

        return pan_resource(pstaging);
}

static enum pipe_format
pan_blit_format(enum pipe_format fmt)
{
        const struct util_format_description *desc;
        desc = util_format_description(fmt);

        /* This must be an emulated format (using u_transfer_helper) as if it
         * was real RGTC we wouldn't have used AFBC and needed a blit. */
        if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC)
                fmt = PIPE_FORMAT_R8G8B8A8_UNORM;

        return fmt;
}

static void
pan_blit_from_staging(struct pipe_context *pctx, struct panfrost_transfer *trans)
{
        struct pipe_resource *dst = trans->base.resource;
        struct pipe_blit_info blit = {0};

        blit.dst.resource = dst;
        blit.dst.format   = pan_blit_format(dst->format);
        blit.dst.level    = trans->base.level;
        blit.dst.box      = trans->base.box;
        blit.src.resource = trans->staging.rsrc;
        blit.src.format   = pan_blit_format(trans->staging.rsrc->format);
        blit.src.level    = 0;
        blit.src.box      = trans->staging.box;
        blit.mask = util_format_get_mask(blit.src.format);
        blit.filter = PIPE_TEX_FILTER_NEAREST;

        panfrost_blit(pctx, &blit);
}

static void
pan_blit_to_staging(struct pipe_context *pctx, struct panfrost_transfer *trans)
{
        struct pipe_resource *src = trans->base.resource;
        struct pipe_blit_info blit = {0};

        blit.src.resource = src;
        blit.src.format   = pan_blit_format(src->format);
        blit.src.level    = trans->base.level;
        blit.src.box      = trans->base.box;
        blit.dst.resource = trans->staging.rsrc;
        blit.dst.format   = pan_blit_format(trans->staging.rsrc->format);
        blit.dst.level    = 0;
        blit.dst.box      = trans->staging.box;
        blit.mask = util_format_get_mask(blit.dst.format);
        blit.filter = PIPE_TEX_FILTER_NEAREST;

        panfrost_blit(pctx, &blit);
}

static void *
panfrost_ptr_map(struct pipe_context *pctx,
                      struct pipe_resource *resource,
                      unsigned level,
                      unsigned usage,  /* a combination of PIPE_MAP_x */
                      const struct pipe_box *box,
                      struct pipe_transfer **out_transfer)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_device *dev = pan_device(pctx->screen);
        struct panfrost_resource *rsrc = pan_resource(resource);
        int bytes_per_pixel = util_format_get_blocksize(rsrc->internal_format);
        struct panfrost_bo *bo = rsrc->bo;

        /* Can't map tiled/compressed directly */
        if ((usage & PIPE_MAP_DIRECTLY) && rsrc->modifier != DRM_FORMAT_MOD_LINEAR)
                return NULL;

        struct panfrost_transfer *transfer = rzalloc(pctx, struct panfrost_transfer);
        transfer->base.level = level;
        transfer->base.usage = usage;
        transfer->base.box = *box;

        pipe_resource_reference(&transfer->base.resource, resource);
        *out_transfer = &transfer->base;

        /* We don't have s/w routines for AFBC, so use a staging texture */
        if (drm_is_afbc(rsrc->modifier)) {
                struct panfrost_resource *staging = pan_alloc_staging(ctx, rsrc, level, box);
                transfer->base.stride = staging->slices[0].line_stride;
                transfer->base.layer_stride = transfer->base.stride * box->height;

                transfer->staging.rsrc = &staging->base;

                transfer->staging.box = *box;
                transfer->staging.box.x = 0;
                transfer->staging.box.y = 0;
                transfer->staging.box.z = 0;

                assert(transfer->staging.rsrc != NULL);

                /* TODO: Eliminate this flush. It's only there to determine if
                 * we're initialized or not, when the initialization could come
                 * from a pending batch XXX */
                panfrost_flush_batches_accessing_bo(ctx, rsrc->bo, true);

                if ((usage & PIPE_MAP_READ) && rsrc->slices[level].initialized) {
                        pan_blit_to_staging(pctx, transfer);
                        panfrost_flush_batches_accessing_bo(ctx, staging->bo, true);
                        panfrost_bo_wait(staging->bo, INT64_MAX, false);
                }

                panfrost_bo_mmap(staging->bo);
                return staging->bo->ptr.cpu;
        }

        /* If we haven't already mmaped, now's the time */
        panfrost_bo_mmap(bo);

        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                pandecode_inject_mmap(bo->ptr.gpu, bo->ptr.cpu, bo->size, NULL);

        bool create_new_bo = usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE;
        bool copy_resource = false;

        if (!create_new_bo &&
            !(usage & PIPE_MAP_UNSYNCHRONIZED) &&
            (usage & PIPE_MAP_WRITE) &&
            !(resource->target == PIPE_BUFFER
              && !util_ranges_intersect(&rsrc->valid_buffer_range, box->x, box->x + box->width)) &&
            panfrost_pending_batches_access_bo(ctx, bo)) {

                /* When a resource to be modified is already being used by a
                 * pending batch, it is often faster to copy the whole BO than
                 * to flush and split the frame in two.
                 */

                panfrost_flush_batches_accessing_bo(ctx, bo, false);
                panfrost_bo_wait(bo, INT64_MAX, false);

                create_new_bo = true;
                copy_resource = true;
        }

        if (create_new_bo) {
                /* If the BO is used by one of the pending batches or if it's
                 * not ready yet (still accessed by one of the already flushed
                 * batches), we try to allocate a new one to avoid waiting.
                 */
                if (panfrost_pending_batches_access_bo(ctx, bo) ||
                    !panfrost_bo_wait(bo, 0, true)) {
                        /* We want the BO to be MMAPed. */
                        uint32_t flags = bo->flags & ~PAN_BO_DELAY_MMAP;
                        struct panfrost_bo *newbo = NULL;

                        /* When the BO has been imported/exported, we can't
                         * replace it by another one, otherwise the
                         * importer/exporter wouldn't see the change we're
                         * doing to it.
                         */
                        if (!(bo->flags & PAN_BO_SHARED))
                                newbo = panfrost_bo_create(dev, bo->size,
                                                           flags);

                        if (newbo) {
                                if (copy_resource)
                                        memcpy(newbo->ptr.cpu, rsrc->bo->ptr.cpu, bo->size);

                                panfrost_bo_unreference(bo);
                                rsrc->bo = newbo;
                                bo = newbo;
                        } else {
                                /* Allocation failed or was impossible, let's
                                 * fall back on a flush+wait.
                                 */
                                panfrost_flush_batches_accessing_bo(ctx, bo, true);
                                panfrost_bo_wait(bo, INT64_MAX, true);
                        }
                }
        } else if ((usage & PIPE_MAP_WRITE)
                   && resource->target == PIPE_BUFFER
                   && !util_ranges_intersect(&rsrc->valid_buffer_range, box->x, box->x + box->width)) {
                /* No flush for writes to uninitialized */
        } else if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
                if (usage & PIPE_MAP_WRITE) {
                        panfrost_flush_batches_accessing_bo(ctx, bo, true);
                        panfrost_bo_wait(bo, INT64_MAX, true);
                } else if (usage & PIPE_MAP_READ) {
                        panfrost_flush_batches_accessing_bo(ctx, bo, false);
                        panfrost_bo_wait(bo, INT64_MAX, false);
                }
        }

        if (rsrc->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                transfer->base.stride = box->width * bytes_per_pixel;
                transfer->base.layer_stride = transfer->base.stride * box->height;
                transfer->map = ralloc_size(transfer, transfer->base.layer_stride * box->depth);
                assert(box->depth == 1);

                if ((usage & PIPE_MAP_READ) && rsrc->slices[level].initialized) {
                        panfrost_load_tiled_image(
                                        transfer->map,
                                        bo->ptr.cpu + rsrc->slices[level].offset,
                                        box->x, box->y, box->width, box->height,
                                        transfer->base.stride,
                                        rsrc->slices[level].line_stride,
                                        rsrc->internal_format);
                }

                return transfer->map;
        } else {
                assert (rsrc->modifier == DRM_FORMAT_MOD_LINEAR);

                /* Direct, persistent writes create holes in time for
                 * caching... I don't know if this is actually possible but we
                 * should still get it right */

                unsigned dpw = PIPE_MAP_DIRECTLY | PIPE_MAP_WRITE | PIPE_MAP_PERSISTENT;

                if ((usage & dpw) == dpw && rsrc->index_cache)
                        return NULL;

                transfer->base.stride = rsrc->slices[level].line_stride;
                transfer->base.layer_stride = panfrost_get_layer_stride(
                                rsrc->slices, rsrc->base.target == PIPE_TEXTURE_3D,
                                rsrc->cubemap_stride, level);

                /* By mapping direct-write, we're implicitly already
                 * initialized (maybe), so be conservative */

                if (usage & PIPE_MAP_WRITE) {
                        rsrc->slices[level].initialized = true;
                        panfrost_minmax_cache_invalidate(rsrc->index_cache, &transfer->base);
                }

                return bo->ptr.cpu
                       + rsrc->slices[level].offset
                       + transfer->base.box.z * transfer->base.layer_stride
                       + transfer->base.box.y * rsrc->slices[level].line_stride
                       + transfer->base.box.x * bytes_per_pixel;
        }
}

static bool
panfrost_should_linear_convert(struct panfrost_resource *prsrc,
                               struct pipe_transfer *transfer)
{
        if (prsrc->modifier_constant)
                return false;

        /* Overwriting the entire resource indicates streaming, for which
         * linear layout is most efficient due to the lack of expensive
         * conversion.
         *
         * For now we just switch to linear after a number of complete
         * overwrites to keep things simple, but we could do better.
         */

        bool entire_overwrite = prsrc->base.last_level == 0
                && transfer->box.width == prsrc->base.width0
                && transfer->box.height == prsrc->base.height0
                && transfer->box.x == 0
                && transfer->box.y == 0;

        if (entire_overwrite)
                ++prsrc->modifier_updates;

        return prsrc->modifier_updates >= LAYOUT_CONVERT_THRESHOLD;
}

static void
panfrost_ptr_unmap(struct pipe_context *pctx,
                        struct pipe_transfer *transfer)
{
        /* Gallium expects writeback here, so we tile */

        struct panfrost_transfer *trans = pan_transfer(transfer);
        struct panfrost_resource *prsrc = (struct panfrost_resource *) transfer->resource;
        struct panfrost_device *dev = pan_device(pctx->screen);

        /* AFBC will use a staging resource. `initialized` will be set when the
         * fragment job is created; this is deferred to prevent useless surface
         * reloads that can cascade into DATA_INVALID_FAULTs due to reading
         * malformed AFBC data if uninitialized */

        if (trans->staging.rsrc) {
                if (transfer->usage & PIPE_MAP_WRITE) {
                        if (panfrost_should_linear_convert(prsrc, transfer)) {

                                panfrost_bo_unreference(prsrc->bo);
                                if (prsrc->slices[0].checksum_bo)
                                        panfrost_bo_unreference(prsrc->slices[0].checksum_bo);

                                panfrost_resource_setup(dev, prsrc, NULL, DRM_FORMAT_MOD_LINEAR);

                                prsrc->bo = pan_resource(trans->staging.rsrc)->bo;
                                panfrost_bo_reference(prsrc->bo);
                        } else {
                                pan_blit_from_staging(pctx, trans);
                                panfrost_flush_batches_accessing_bo(pan_context(pctx), pan_resource(trans->staging.rsrc)->bo, true);
                        }
                }

                pipe_resource_reference(&trans->staging.rsrc, NULL);
        }

        /* Tiling will occur in software from a staging cpu buffer */
        if (trans->map) {
                struct panfrost_bo *bo = prsrc->bo;

                if (transfer->usage & PIPE_MAP_WRITE) {
                        prsrc->slices[transfer->level].initialized = true;

                        if (prsrc->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
                                assert(transfer->box.depth == 1);

                                if (panfrost_should_linear_convert(prsrc, transfer)) {
                                        size_t bo_size;

                                        panfrost_resource_setup(dev, prsrc, &bo_size, DRM_FORMAT_MOD_LINEAR);
                                        if (bo_size > bo->size) {
                                                panfrost_bo_unreference(bo);
                                                bo = prsrc->bo = panfrost_bo_create(dev, bo_size, 0);
                                                assert(bo);
                                        }

                                        util_copy_rect(
                                                bo->ptr.cpu + prsrc->slices[0].offset,
                                                prsrc->base.format,
                                                prsrc->slices[0].line_stride,
                                                0, 0,
                                                transfer->box.width,
                                                transfer->box.height,
                                                trans->map,
                                                transfer->stride,
                                                0, 0);
                                } else {
                                        panfrost_store_tiled_image(
                                                bo->ptr.cpu + prsrc->slices[transfer->level].offset,
                                                trans->map,
                                                transfer->box.x, transfer->box.y,
                                                transfer->box.width, transfer->box.height,
                                                prsrc->slices[transfer->level].line_stride,
                                                transfer->stride,
                                                prsrc->internal_format);
                                }
                        }
                }
        }


        util_range_add(&prsrc->base, &prsrc->valid_buffer_range,
                       transfer->box.x,
                       transfer->box.x + transfer->box.width);

        panfrost_minmax_cache_invalidate(prsrc->index_cache, transfer);

        /* Derefence the resource */
        pipe_resource_reference(&transfer->resource, NULL);

        /* Transfer itself is RALLOCed at the moment */
        ralloc_free(transfer);
}

static void
panfrost_ptr_flush_region(struct pipe_context *pctx,
                               struct pipe_transfer *transfer,
                               const struct pipe_box *box)
{
        struct panfrost_resource *rsc = pan_resource(transfer->resource);

        if (transfer->resource->target == PIPE_BUFFER) {
                util_range_add(&rsc->base, &rsc->valid_buffer_range,
                               transfer->box.x + box->x,
                               transfer->box.x + box->x + box->width);
        } else {
                unsigned level = transfer->level;
                rsc->slices[level].initialized = true;
        }
}

static void
panfrost_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        /* TODO */
}

static enum pipe_format
panfrost_resource_get_internal_format(struct pipe_resource *rsrc)
{
        struct panfrost_resource *prsrc = (struct panfrost_resource *) rsrc;
        return prsrc->internal_format;
}

static bool
panfrost_generate_mipmap(
        struct pipe_context *pctx,
        struct pipe_resource *prsrc,
        enum pipe_format format,
        unsigned base_level,
        unsigned last_level,
        unsigned first_layer,
        unsigned last_layer)
{
        struct panfrost_resource *rsrc = pan_resource(prsrc);

        /* Generating a mipmap invalidates the written levels, so make that
         * explicit so we don't try to wallpaper them back and end up with
         * u_blitter recursion */

        assert(rsrc->bo);
        for (unsigned l = base_level + 1; l <= last_level; ++l)
                rsrc->slices[l].initialized = false;

        /* Beyond that, we just delegate the hard stuff. */

        bool blit_res = util_gen_mipmap(
                                pctx, prsrc, format,
                                base_level, last_level,
                                first_layer, last_layer,
                                PIPE_TEX_FILTER_LINEAR);

        return blit_res;
}

/* Computes the address to a texture at a particular slice */

mali_ptr
panfrost_get_texture_address(
        struct panfrost_resource *rsrc,
        unsigned level, unsigned face, unsigned sample)
{
        bool is_3d = rsrc->base.target == PIPE_TEXTURE_3D;
        return rsrc->bo->ptr.gpu +
               panfrost_texture_offset(rsrc->slices, is_3d,
                                       rsrc->cubemap_stride,
                                       level, face, sample);
}

static void
panfrost_resource_set_stencil(struct pipe_resource *prsrc,
                              struct pipe_resource *stencil)
{
        pan_resource(prsrc)->separate_stencil = pan_resource(stencil);
}

static struct pipe_resource *
panfrost_resource_get_stencil(struct pipe_resource *prsrc)
{
        return &pan_resource(prsrc)->separate_stencil->base;
}

static const struct u_transfer_vtbl transfer_vtbl = {
        .resource_create          = panfrost_resource_create,
        .resource_destroy         = panfrost_resource_destroy,
        .transfer_map             = panfrost_ptr_map,
        .transfer_unmap           = panfrost_ptr_unmap,
        .transfer_flush_region    = panfrost_ptr_flush_region,
        .get_internal_format      = panfrost_resource_get_internal_format,
        .set_stencil              = panfrost_resource_set_stencil,
        .get_stencil              = panfrost_resource_get_stencil,
};

void
panfrost_resource_screen_init(struct pipe_screen *pscreen)
{
        struct panfrost_device *dev = pan_device(pscreen);

        bool fake_rgtc = !panfrost_supports_compressed_format(dev, MALI_BC4_UNORM);

        pscreen->resource_create_with_modifiers =
                panfrost_resource_create_with_modifiers;
        pscreen->resource_create = u_transfer_helper_resource_create;
        pscreen->resource_destroy = u_transfer_helper_resource_destroy;
        pscreen->resource_from_handle = panfrost_resource_from_handle;
        pscreen->resource_get_handle = panfrost_resource_get_handle;
        pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl,
                                        true, false,
                                        fake_rgtc, true);
}

void
panfrost_resource_context_init(struct pipe_context *pctx)
{
        pctx->transfer_map = u_transfer_helper_transfer_map;
        pctx->transfer_unmap = u_transfer_helper_transfer_unmap;
        pctx->create_surface = panfrost_create_surface;
        pctx->surface_destroy = panfrost_surface_destroy;
        pctx->resource_copy_region = util_resource_copy_region;
        pctx->blit = panfrost_blit;
        pctx->generate_mipmap = panfrost_generate_mipmap;
        pctx->flush_resource = panfrost_flush_resource;
        pctx->invalidate_resource = panfrost_invalidate_resource;
        pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
        pctx->buffer_subdata = u_default_buffer_subdata;
        pctx->texture_subdata = u_default_texture_subdata;
}
