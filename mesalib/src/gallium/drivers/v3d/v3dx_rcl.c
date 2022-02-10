/*
 * Copyright © 2017 Broadcom
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

#include "util/format/u_format.h"
#include "v3d_context.h"
#include "broadcom/common/v3d_tiling.h"
#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"

#define PIPE_CLEAR_COLOR_BUFFERS (PIPE_CLEAR_COLOR0 |                   \
                                  PIPE_CLEAR_COLOR1 |                   \
                                  PIPE_CLEAR_COLOR2 |                   \
                                  PIPE_CLEAR_COLOR3)                    \

#define PIPE_FIRST_COLOR_BUFFER_BIT (ffs(PIPE_CLEAR_COLOR0) - 1)

/* The HW queues up the load until the tile coordinates show up, but can only
 * track one at a time.  If we need to do more than one load, then we need to
 * flush out the previous load by emitting the tile coordinates and doing a
 * dummy store.
 */
static void
flush_last_load(struct v3d_cl *cl)
{
        if (V3D_VERSION >= 40)
                return;

        cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);
        cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
                store.buffer_to_store = NONE;
        }
}

static void
load_general(struct v3d_cl *cl, struct pipe_surface *psurf, int buffer,
             int layer, uint32_t pipe_bit, uint32_t *loads_pending)
{
        struct v3d_surface *surf = v3d_surface(psurf);
        bool separate_stencil = surf->separate_stencil && buffer == STENCIL;
        if (separate_stencil) {
                psurf = surf->separate_stencil;
                surf = v3d_surface(psurf);
        }

        struct v3d_resource *rsc = v3d_resource(psurf->texture);

        uint32_t layer_offset =
                v3d_layer_offset(&rsc->base, psurf->u.tex.level,
                                 psurf->u.tex.first_layer + layer);
        cl_emit(cl, LOAD_TILE_BUFFER_GENERAL, load) {
                load.buffer_to_load = buffer;
                load.address = cl_address(rsc->bo, layer_offset);

#if V3D_VERSION >= 40
                load.memory_format = surf->tiling;
                if (separate_stencil)
                        load.input_image_format = V3D_OUTPUT_IMAGE_FORMAT_S8;
                else
                        load.input_image_format = surf->format;
                load.r_b_swap = surf->swap_rb;
                load.force_alpha_1 = util_format_has_alpha1(psurf->format);
                if (surf->tiling == V3D_TILING_UIF_NO_XOR ||
                    surf->tiling == V3D_TILING_UIF_XOR) {
                        load.height_in_ub_or_stride =
                                surf->padded_height_of_output_image_in_uif_blocks;
                } else if (surf->tiling == V3D_TILING_RASTER) {
                        struct v3d_resource_slice *slice =
                                &rsc->slices[psurf->u.tex.level];
                        load.height_in_ub_or_stride = slice->stride;
                }

                if (psurf->texture->nr_samples > 1)
                        load.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
                else
                        load.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;

#else /* V3D_VERSION < 40 */
                /* Can't do raw ZSTENCIL loads -- need to load/store them to
                 * separate buffers for Z and stencil.
                 */
                assert(buffer != ZSTENCIL);
                load.raw_mode = true;
                load.padded_height_of_output_image_in_uif_blocks =
                        surf->padded_height_of_output_image_in_uif_blocks;
#endif /* V3D_VERSION < 40 */
        }

        *loads_pending &= ~pipe_bit;
        if (*loads_pending)
                flush_last_load(cl);
}

static void
store_general(struct v3d_job *job,
              struct v3d_cl *cl, struct pipe_surface *psurf,
              int layer, int buffer, int pipe_bit,
              uint32_t *stores_pending, bool general_color_clear,
              bool resolve_4x)
{
        struct v3d_surface *surf = v3d_surface(psurf);
        bool separate_stencil = surf->separate_stencil && buffer == STENCIL;
        if (separate_stencil) {
                psurf = surf->separate_stencil;
                surf = v3d_surface(psurf);
        }

        *stores_pending &= ~pipe_bit;
        bool last_store = !(*stores_pending);

        struct v3d_resource *rsc = v3d_resource(psurf->texture);

        rsc->writes++;

        uint32_t layer_offset =
                v3d_layer_offset(&rsc->base, psurf->u.tex.level,
                                 psurf->u.tex.first_layer + layer);
        cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
                store.buffer_to_store = buffer;
                store.address = cl_address(rsc->bo, layer_offset);

#if V3D_VERSION >= 40
                store.clear_buffer_being_stored = false;

                if (separate_stencil)
                        store.output_image_format = V3D_OUTPUT_IMAGE_FORMAT_S8;
                else
                        store.output_image_format = surf->format;

                store.r_b_swap = surf->swap_rb;
                store.memory_format = surf->tiling;

                if (surf->tiling == V3D_TILING_UIF_NO_XOR ||
                    surf->tiling == V3D_TILING_UIF_XOR) {
                        store.height_in_ub_or_stride =
                                surf->padded_height_of_output_image_in_uif_blocks;
                } else if (surf->tiling == V3D_TILING_RASTER) {
                        struct v3d_resource_slice *slice =
                                &rsc->slices[psurf->u.tex.level];
                        store.height_in_ub_or_stride = slice->stride;
                }

                assert(!resolve_4x || job->bbuf);
                if (psurf->texture->nr_samples > 1)
                        store.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
                else if (resolve_4x && job->bbuf->texture->nr_samples > 1)
                        store.decimate_mode = V3D_DECIMATE_MODE_4X;
                else
                        store.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;

#else /* V3D_VERSION < 40 */
                /* Can't do raw ZSTENCIL stores -- need to load/store them to
                 * separate buffers for Z and stencil.
                 */
                assert(buffer != ZSTENCIL);
                store.raw_mode = true;
                if (!last_store) {
                        store.disable_color_buffers_clear_on_write = true;
                        store.disable_z_buffer_clear_on_write = true;
                        store.disable_stencil_buffer_clear_on_write = true;
                } else {
                        store.disable_color_buffers_clear_on_write =
                                !(((pipe_bit & PIPE_CLEAR_COLOR_BUFFERS) &&
                                   general_color_clear &&
                                   (job->clear & pipe_bit)));
                        store.disable_z_buffer_clear_on_write =
                                !(job->clear & PIPE_CLEAR_DEPTH);
                        store.disable_stencil_buffer_clear_on_write =
                                !(job->clear & PIPE_CLEAR_STENCIL);
                }
                store.padded_height_of_output_image_in_uif_blocks =
                        surf->padded_height_of_output_image_in_uif_blocks;
#endif /* V3D_VERSION < 40 */
        }

        /* There must be a TILE_COORDINATES_IMPLICIT between each store. */
        if (V3D_VERSION < 40 && !last_store) {
                cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);
        }
}

static int
zs_buffer_from_pipe_bits(int pipe_clear_bits)
{
        switch (pipe_clear_bits & PIPE_CLEAR_DEPTHSTENCIL) {
        case PIPE_CLEAR_DEPTHSTENCIL:
                return ZSTENCIL;
        case PIPE_CLEAR_DEPTH:
                return Z;
        case PIPE_CLEAR_STENCIL:
                return STENCIL;
        default:
                return NONE;
        }
}

static void
v3d_rcl_emit_loads(struct v3d_job *job, struct v3d_cl *cl, int layer)
{
        /* When blitting, no color or zs buffer is loaded; instead the blit
         * source buffer is loaded for the aspects that we are going to blit.
         */
        assert(!job->bbuf || job->load == 0);
        assert(!job->bbuf || job->nr_cbufs <= 1);
        assert(!job->bbuf || V3D_VERSION >= 40);

        uint32_t loads_pending = job->bbuf ? job->store : job->load;

        for (int i = 0; i < job->nr_cbufs; i++) {
                uint32_t bit = PIPE_CLEAR_COLOR0 << i;
                if (!(loads_pending & bit))
                        continue;

                struct pipe_surface *psurf = job->bbuf ? job->bbuf : job->cbufs[i];
                assert(!job->bbuf || i == 0);

                if (!psurf || (V3D_VERSION < 40 &&
                               psurf->texture->nr_samples <= 1)) {
                        continue;
                }

                load_general(cl, psurf, RENDER_TARGET_0 + i, layer,
                             bit, &loads_pending);
        }

        if ((loads_pending & PIPE_CLEAR_DEPTHSTENCIL) &&
            (V3D_VERSION >= 40 ||
             (job->zsbuf && job->zsbuf->texture->nr_samples > 1))) {
                struct pipe_surface *src = job->bbuf ? job->bbuf : job->zsbuf;
                struct v3d_resource *rsc = v3d_resource(src->texture);

                if (rsc->separate_stencil &&
                    (loads_pending & PIPE_CLEAR_STENCIL)) {
                        load_general(cl, src,
                                     STENCIL, layer,
                                     PIPE_CLEAR_STENCIL,
                                     &loads_pending);
                }

                if (loads_pending & PIPE_CLEAR_DEPTHSTENCIL) {
                        load_general(cl, src,
                                     zs_buffer_from_pipe_bits(loads_pending),
                                     layer,
                                     loads_pending & PIPE_CLEAR_DEPTHSTENCIL,
                                     &loads_pending);
                }
        }

#if V3D_VERSION < 40
        /* The initial reload will be queued until we get the
         * tile coordinates.
         */
        if (loads_pending) {
                cl_emit(cl, RELOAD_TILE_COLOR_BUFFER, load) {
                        load.disable_color_buffer_load =
                                (~loads_pending &
                                 PIPE_CLEAR_COLOR_BUFFERS) >>
                                PIPE_FIRST_COLOR_BUFFER_BIT;
                        load.enable_z_load =
                                loads_pending & PIPE_CLEAR_DEPTH;
                        load.enable_stencil_load =
                                loads_pending & PIPE_CLEAR_STENCIL;
                }
        }
#else /* V3D_VERSION >= 40 */
        assert(!loads_pending);
        cl_emit(cl, END_OF_LOADS, end);
#endif
}

static void
v3d_rcl_emit_stores(struct v3d_job *job, struct v3d_cl *cl, int layer)
{
#if V3D_VERSION < 40
        UNUSED bool needs_color_clear = job->clear & PIPE_CLEAR_COLOR_BUFFERS;
        UNUSED bool needs_z_clear = job->clear & PIPE_CLEAR_DEPTH;
        UNUSED bool needs_s_clear = job->clear & PIPE_CLEAR_STENCIL;

        /* For clearing color in a TLB general on V3D 3.3:
         *
         * - NONE buffer store clears all TLB color buffers.
         * - color buffer store clears just the TLB color buffer being stored.
         * - Z/S buffers store may not clear the TLB color buffer.
         *
         * And on V3D 4.1, we only have one flag for "clear the buffer being
         * stored" in the general packet, and a separate packet to clear all
         * color TLB buffers.
         *
         * As a result, we only bother flagging TLB color clears in a general
         * packet when we don't have to emit a separate packet to clear all
         * TLB color buffers.
         */
        bool general_color_clear = (needs_color_clear &&
                                    (job->clear & PIPE_CLEAR_COLOR_BUFFERS) ==
                                    (job->store & PIPE_CLEAR_COLOR_BUFFERS));
#else
        bool general_color_clear = false;
#endif

        uint32_t stores_pending = job->store;

        /* For V3D 4.1, use general stores for all TLB stores.
         *
         * For V3D 3.3, we only use general stores to do raw stores for any
         * MSAA surfaces.  These output UIF tiled images where each 4x MSAA
         * pixel is a 2x2 quad, and the format will be that of the
         * internal_type/internal_bpp, rather than the format from GL's
         * perspective.  Non-MSAA surfaces will use
         * STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED.
         */
        assert(!job->bbuf || job->nr_cbufs <= 1);
        for (int i = 0; i < job->nr_cbufs; i++) {
                uint32_t bit = PIPE_CLEAR_COLOR0 << i;
                if (!(job->store & bit))
                        continue;

                struct pipe_surface *psurf = job->cbufs[i];
                if (!psurf ||
                    (V3D_VERSION < 40 && psurf->texture->nr_samples <= 1)) {
                        continue;
                }

                store_general(job, cl, psurf, layer, RENDER_TARGET_0 + i, bit,
                              &stores_pending, general_color_clear, job->bbuf);
        }

        if (job->store & PIPE_CLEAR_DEPTHSTENCIL && job->zsbuf &&
            !(V3D_VERSION < 40 && job->zsbuf->texture->nr_samples <= 1)) {
                struct v3d_resource *rsc = v3d_resource(job->zsbuf->texture);
                if (rsc->separate_stencil) {
                        if (job->store & PIPE_CLEAR_DEPTH) {
                                store_general(job, cl, job->zsbuf, layer,
                                              Z, PIPE_CLEAR_DEPTH,
                                              &stores_pending,
                                              general_color_clear,
                                              false);
                        }

                        if (job->store & PIPE_CLEAR_STENCIL) {
                                store_general(job, cl, job->zsbuf, layer,
                                              STENCIL, PIPE_CLEAR_STENCIL,
                                              &stores_pending,
                                              general_color_clear,
                                              false);
                        }
                } else {
                        store_general(job, cl, job->zsbuf, layer,
                                      zs_buffer_from_pipe_bits(job->store),
                                      job->store & PIPE_CLEAR_DEPTHSTENCIL,
                                      &stores_pending, general_color_clear,
                                      false);
                }
        }

#if V3D_VERSION < 40
        if (stores_pending) {
                cl_emit(cl, STORE_MULTI_SAMPLE_RESOLVED_TILE_COLOR_BUFFER_EXTENDED, store) {

                        store.disable_color_buffer_write =
                                (~stores_pending >>
                                 PIPE_FIRST_COLOR_BUFFER_BIT) & 0xf;
                        store.enable_z_write = stores_pending & PIPE_CLEAR_DEPTH;
                        store.enable_stencil_write = stores_pending & PIPE_CLEAR_STENCIL;

                        /* Note that when set this will clear all of the color
                         * buffers.
                         */
                        store.disable_color_buffers_clear_on_write =
                                !needs_color_clear;
                        store.disable_z_buffer_clear_on_write =
                                !needs_z_clear;
                        store.disable_stencil_buffer_clear_on_write =
                                !needs_s_clear;
                };
        } else if (needs_color_clear && !general_color_clear) {
                /* If we didn't do our color clears in the general packet,
                 * then emit a packet to clear all the TLB color buffers now.
                 */
                cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
                        store.buffer_to_store = NONE;
                }
        }
#else /* V3D_VERSION >= 40 */
        /* If we're emitting an RCL with GL_ARB_framebuffer_no_attachments,
         * we still need to emit some sort of store.
         */
        if (!job->store) {
                cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
                        store.buffer_to_store = NONE;
                }
        }

        assert(!stores_pending);

        /* GFXH-1461/GFXH-1689: The per-buffer store command's clear
         * buffer bit is broken for depth/stencil.  In addition, the
         * clear packet's Z/S bit is broken, but the RTs bit ends up
         * clearing Z/S.
         */
        if (job->clear) {
                cl_emit(cl, CLEAR_TILE_BUFFERS, clear) {
                        clear.clear_z_stencil_buffer = true;
                        clear.clear_all_render_targets = true;
                }
        }
#endif /* V3D_VERSION >= 40 */
}

static void
v3d_rcl_emit_generic_per_tile_list(struct v3d_job *job, int layer)
{
        /* Emit the generic list in our indirect state -- the rcl will just
         * have pointers into it.
         */
        struct v3d_cl *cl = &job->indirect;
        v3d_cl_ensure_space(cl, 200, 1);
        struct v3d_cl_reloc tile_list_start = cl_get_address(cl);

        if (V3D_VERSION >= 40) {
                /* V3D 4.x only requires a single tile coordinates, and
                 * END_OF_LOADS switches us between loading and rendering.
                 */
                cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);
        }

        v3d_rcl_emit_loads(job, cl, layer);

        if (V3D_VERSION < 40) {
                /* Tile Coordinates triggers the last reload and sets where
                 * the stores go. There must be one per store packet.
                 */
                cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);
        }

        /* The binner starts out writing tiles assuming that the initial mode
         * is triangles, so make sure that's the case.
         */
        cl_emit(cl, PRIM_LIST_FORMAT, fmt) {
                fmt.primitive_type = LIST_TRIANGLES;
        }

#if V3D_VERSION >= 41
        /* PTB assumes that value to be 0, but hw will not set it. */
        cl_emit(cl, SET_INSTANCEID, set) {
           set.instance_id = 0;
        }
#endif

        cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

        v3d_rcl_emit_stores(job, cl, layer);

#if V3D_VERSION >= 40
        cl_emit(cl, END_OF_TILE_MARKER, end);
#endif

        cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

        cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
                branch.start = tile_list_start;
                branch.end = cl_get_address(cl);
        }
}

#if V3D_VERSION >= 40
static void
v3d_setup_render_target(struct v3d_job *job, int cbuf,
                        uint32_t *rt_bpp, uint32_t *rt_type, uint32_t *rt_clamp)
{
        if (!job->cbufs[cbuf])
                return;

        struct v3d_surface *surf = v3d_surface(job->cbufs[cbuf]);
        *rt_bpp = surf->internal_bpp;
        if (job->bbuf) {
           struct v3d_surface *bsurf = v3d_surface(job->bbuf);
           *rt_bpp = MAX2(*rt_bpp, bsurf->internal_bpp);
        }
        *rt_type = surf->internal_type;
        *rt_clamp = V3D_RENDER_TARGET_CLAMP_NONE;
}

#else /* V3D_VERSION < 40 */

static void
v3d_emit_z_stencil_config(struct v3d_job *job, struct v3d_surface *surf,
                          struct v3d_resource *rsc, bool is_separate_stencil)
{
        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_Z_STENCIL, zs) {
                zs.address = cl_address(rsc->bo, surf->offset);

                if (!is_separate_stencil) {
                        zs.internal_type = surf->internal_type;
                        zs.output_image_format = surf->format;
                } else {
                        zs.z_stencil_id = 1; /* Separate stencil */
                }

                zs.padded_height_of_output_image_in_uif_blocks =
                        surf->padded_height_of_output_image_in_uif_blocks;

                assert(surf->tiling != V3D_TILING_RASTER);
                zs.memory_format = surf->tiling;
        }

        if (job->store & (is_separate_stencil ?
                          PIPE_CLEAR_STENCIL :
                          PIPE_CLEAR_DEPTHSTENCIL)) {
                rsc->writes++;
        }
}
#endif /* V3D_VERSION < 40 */

#define div_round_up(a, b) (((a) + (b) - 1) / b)

static bool
supertile_in_job_scissors(struct v3d_job *job,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   if (job->scissor.disabled || job->scissor.count == 0)
      return true;

   const uint32_t min_x = x * w;
   const uint32_t min_y = y * h;
   const uint32_t max_x = min_x + w - 1;
   const uint32_t max_y = min_y + h - 1;

   for (uint32_t i = 0; i < job->scissor.count; i++) {
           const uint32_t min_s_x = job->scissor.rects[i].min_x;
           const uint32_t min_s_y = job->scissor.rects[i].min_y;
           const uint32_t max_s_x = job->scissor.rects[i].max_x;
           const uint32_t max_s_y = job->scissor.rects[i].max_y;

           if (max_x < min_s_x || min_x > max_s_x ||
               max_y < min_s_y || min_y > max_s_y) {
                   continue;
           }

           return true;
   }

   return false;
}

#if V3D_VERSION >= 40
static inline bool
do_double_initial_tile_clear(const struct v3d_job *job)
{
        /* Our rendering code emits an initial clear per layer, unlike the
         * Vulkan driver, which only executes a single initial clear for all
         * layers. This is because in GL we don't use the
         * 'clear_buffer_being_stored' bit when storing tiles, so each layer
         * needs the iniital clear. This is also why this helper, unlike the
         * Vulkan version, doesn't check the layer count to decide if double
         * clear for double buffer mode is required.
         */
        return job->double_buffer &&
               (job->draw_tiles_x > 1 || job->draw_tiles_y > 1);
}
#endif

static void
emit_render_layer(struct v3d_job *job, uint32_t layer)
{
        uint32_t supertile_w = 1, supertile_h = 1;

        /* If doing multicore binning, we would need to initialize each
         * core's tile list here.
         */
        uint32_t tile_alloc_offset =
                layer * job->draw_tiles_x * job->draw_tiles_y * 64;
        cl_emit(&job->rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, list) {
                list.address = cl_address(job->tile_alloc, tile_alloc_offset);
        }

        cl_emit(&job->rcl, MULTICORE_RENDERING_SUPERTILE_CFG, config) {
                uint32_t frame_w_in_supertiles, frame_h_in_supertiles;
                const uint32_t max_supertiles = 256;

                /* Size up our supertiles until we get under the limit. */
                for (;;) {
                        frame_w_in_supertiles = div_round_up(job->draw_tiles_x,
                                                             supertile_w);
                        frame_h_in_supertiles = div_round_up(job->draw_tiles_y,
                                                             supertile_h);
                        if (frame_w_in_supertiles *
                                frame_h_in_supertiles < max_supertiles) {
                                break;
                        }

                        if (supertile_w < supertile_h)
                                supertile_w++;
                        else
                                supertile_h++;
                }

                config.number_of_bin_tile_lists = 1;
                config.total_frame_width_in_tiles = job->draw_tiles_x;
                config.total_frame_height_in_tiles = job->draw_tiles_y;

                config.supertile_width_in_tiles = supertile_w;
                config.supertile_height_in_tiles = supertile_h;

                config.total_frame_width_in_supertiles = frame_w_in_supertiles;
                config.total_frame_height_in_supertiles = frame_h_in_supertiles;
        }

        /* Start by clearing the tile buffer. */
        cl_emit(&job->rcl, TILE_COORDINATES, coords) {
                coords.tile_column_number = 0;
                coords.tile_row_number = 0;
        }

        /* Emit an initial clear of the tile buffers.  This is necessary
         * for any buffers that should be cleared (since clearing
         * normally happens at the *end* of the generic tile list), but
         * it's also nice to clear everything so the first tile doesn't
         * inherit any contents from some previous frame.
         *
         * Also, implement the GFXH-1742 workaround.  There's a race in
         * the HW between the RCL updating the TLB's internal type/size
         * and thespawning of the QPU instances using the TLB's current
         * internal type/size.  To make sure the QPUs get the right
         * state, we need 1 dummy store in between internal type/size
         * changes on V3D 3.x, and 2 dummy stores on 4.x.
         */
#if V3D_VERSION < 40
        cl_emit(&job->rcl, STORE_TILE_BUFFER_GENERAL, store) {
                store.buffer_to_store = NONE;
        }
#else
        for (int i = 0; i < 2; i++) {
                if (i > 0)
                        cl_emit(&job->rcl, TILE_COORDINATES, coords);
                cl_emit(&job->rcl, END_OF_LOADS, end);
                cl_emit(&job->rcl, STORE_TILE_BUFFER_GENERAL, store) {
                        store.buffer_to_store = NONE;
                }
                if (i == 0 || do_double_initial_tile_clear(job)) {
                        cl_emit(&job->rcl, CLEAR_TILE_BUFFERS, clear) {
                                clear.clear_z_stencil_buffer = true;
                                clear.clear_all_render_targets = true;
                        }
                }
                cl_emit(&job->rcl, END_OF_TILE_MARKER, end);
        }
#endif

        cl_emit(&job->rcl, FLUSH_VCD_CACHE, flush);

        v3d_rcl_emit_generic_per_tile_list(job, layer);

        /* XXX perf: We should expose GL_MESA_tile_raster_order to
         * improve X11 performance, but we should use Morton order
         * otherwise to improve cache locality.
         */
        uint32_t supertile_w_in_pixels = job->tile_width * supertile_w;
        uint32_t supertile_h_in_pixels = job->tile_height * supertile_h;
        uint32_t min_x_supertile = job->draw_min_x / supertile_w_in_pixels;
        uint32_t min_y_supertile = job->draw_min_y / supertile_h_in_pixels;

        uint32_t max_x_supertile = 0;
        uint32_t max_y_supertile = 0;
        if (job->draw_max_x != 0 && job->draw_max_y != 0) {
                max_x_supertile = (job->draw_max_x - 1) / supertile_w_in_pixels;
                max_y_supertile = (job->draw_max_y - 1) / supertile_h_in_pixels;
        }

        for (int y = min_y_supertile; y <= max_y_supertile; y++) {
                for (int x = min_x_supertile; x <= max_x_supertile; x++) {
                        if (supertile_in_job_scissors(job, x, y,
                                                      supertile_w_in_pixels,
                                                      supertile_h_in_pixels)) {
                                cl_emit(&job->rcl, SUPERTILE_COORDINATES, coords) {
                                      coords.column_number_in_supertiles = x;
                                      coords.row_number_in_supertiles = y;
                                }
                        }
                }
        }
}

void
v3dX(emit_rcl)(struct v3d_job *job)
{
        /* The RCL list should be empty. */
        assert(!job->rcl.bo);

        v3d_cl_ensure_space_with_branch(&job->rcl, 200 +
                                        MAX2(job->num_layers, 1) * 256 *
                                        cl_packet_length(SUPERTILE_COORDINATES));
        job->submit.rcl_start = job->rcl.bo->offset;
        v3d_job_add_bo(job, job->rcl.bo);

        /* Common config must be the first TILE_RENDERING_MODE_CFG
         * and Z_STENCIL_CLEAR_VALUES must be last.  The ones in between are
         * optional updates to the previous HW state.
         */
        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_COMMON, config) {
#if V3D_VERSION < 40
                config.enable_z_store = job->store & PIPE_CLEAR_DEPTH;
                config.enable_stencil_store = job->store & PIPE_CLEAR_STENCIL;
#else /* V3D_VERSION >= 40 */
                if (job->zsbuf) {
                        struct v3d_surface *surf = v3d_surface(job->zsbuf);
                        config.internal_depth_type = surf->internal_type;
                }
#endif /* V3D_VERSION >= 40 */

                /* XXX: Early D/S clear */

                switch (job->first_ez_state) {
                case V3D_EZ_UNDECIDED:
                case V3D_EZ_LT_LE:
                        config.early_z_disable = false;
                        config.early_z_test_and_update_direction =
                                EARLY_Z_DIRECTION_LT_LE;
                        break;
                case V3D_EZ_GT_GE:
                        config.early_z_disable = false;
                        config.early_z_test_and_update_direction =
                                EARLY_Z_DIRECTION_GT_GE;
                        break;
                case V3D_EZ_DISABLED:
                        config.early_z_disable = true;
                }

                config.image_width_pixels = job->draw_width;
                config.image_height_pixels = job->draw_height;

                config.number_of_render_targets = MAX2(job->nr_cbufs, 1);

                assert(!job->msaa || !job->double_buffer);
                config.multisample_mode_4x = job->msaa;
                config.double_buffer_in_non_ms_mode = job->double_buffer;

                config.maximum_bpp_of_all_render_targets = job->internal_bpp;
        }

        for (int i = 0; i < job->nr_cbufs; i++) {
                struct pipe_surface *psurf = job->cbufs[i];
                if (!psurf)
                        continue;
                struct v3d_surface *surf = v3d_surface(psurf);
                struct v3d_resource *rsc = v3d_resource(psurf->texture);

                UNUSED uint32_t config_pad = 0;
                uint32_t clear_pad = 0;

                /* XXX: Set the pad for raster. */
                if (surf->tiling == V3D_TILING_UIF_NO_XOR ||
                    surf->tiling == V3D_TILING_UIF_XOR) {
                        int uif_block_height = v3d_utile_height(rsc->cpp) * 2;
                        uint32_t implicit_padded_height = (align(job->draw_height, uif_block_height) /
                                                           uif_block_height);
                        if (surf->padded_height_of_output_image_in_uif_blocks -
                            implicit_padded_height < 15) {
                                config_pad = (surf->padded_height_of_output_image_in_uif_blocks -
                                              implicit_padded_height);
                        } else {
                                config_pad = 15;
                                clear_pad = surf->padded_height_of_output_image_in_uif_blocks;
                        }
                }

#if V3D_VERSION < 40
                cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
                        rt.address = cl_address(rsc->bo, surf->offset);
                        rt.internal_type = surf->internal_type;
                        rt.output_image_format = surf->format;
                        rt.memory_format = surf->tiling;
                        rt.internal_bpp = surf->internal_bpp;
                        rt.render_target_number = i;
                        rt.pad = config_pad;

                        if (job->store & PIPE_CLEAR_COLOR0 << i)
                                rsc->writes++;
                }
#endif /* V3D_VERSION < 40 */

                cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1,
                        clear) {
                        clear.clear_color_low_32_bits = job->clear_color[i][0];
                        clear.clear_color_next_24_bits = job->clear_color[i][1] & 0xffffff;
                        clear.render_target_number = i;
                };

                if (surf->internal_bpp >= V3D_INTERNAL_BPP_64) {
                        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART2,
                                clear) {
                                clear.clear_color_mid_low_32_bits =
                                        ((job->clear_color[i][1] >> 24) |
                                         (job->clear_color[i][2] << 8));
                                clear.clear_color_mid_high_24_bits =
                                        ((job->clear_color[i][2] >> 24) |
                                         ((job->clear_color[i][3] & 0xffff) << 8));
                                clear.render_target_number = i;
                        };
                }

                if (surf->internal_bpp >= V3D_INTERNAL_BPP_128 || clear_pad) {
                        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART3,
                                clear) {
                                clear.uif_padded_height_in_uif_blocks = clear_pad;
                                clear.clear_color_high_16_bits = job->clear_color[i][3] >> 16;
                                clear.render_target_number = i;
                        };
                }
        }

#if V3D_VERSION >= 40
        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
                v3d_setup_render_target(job, 0,
                                        &rt.render_target_0_internal_bpp,
                                        &rt.render_target_0_internal_type,
                                        &rt.render_target_0_clamp);
                v3d_setup_render_target(job, 1,
                                        &rt.render_target_1_internal_bpp,
                                        &rt.render_target_1_internal_type,
                                        &rt.render_target_1_clamp);
                v3d_setup_render_target(job, 2,
                                        &rt.render_target_2_internal_bpp,
                                        &rt.render_target_2_internal_type,
                                        &rt.render_target_2_clamp);
                v3d_setup_render_target(job, 3,
                                        &rt.render_target_3_internal_bpp,
                                        &rt.render_target_3_internal_type,
                                        &rt.render_target_3_clamp);
        }
#endif

#if V3D_VERSION < 40
        /* TODO: Don't bother emitting if we don't load/clear Z/S. */
        if (job->zsbuf) {
                struct pipe_surface *psurf = job->zsbuf;
                struct v3d_surface *surf = v3d_surface(psurf);
                struct v3d_resource *rsc = v3d_resource(psurf->texture);

                v3d_emit_z_stencil_config(job, surf, rsc, false);

                /* Emit the separate stencil packet if we have a resource for
                 * it.  The HW will only load/store this buffer if the
                 * Z/Stencil config doesn't have stencil in its format.
                 */
                if (surf->separate_stencil) {
                        v3d_emit_z_stencil_config(job,
                                                  v3d_surface(surf->separate_stencil),
                                                  rsc->separate_stencil, true);
                }
        }
#endif /* V3D_VERSION < 40 */

        /* Ends rendering mode config. */
        cl_emit(&job->rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES,
                clear) {
                clear.z_clear_value = job->clear_z;
                clear.stencil_clear_value = job->clear_s;
        };

        /* Always set initial block size before the first branch, which needs
         * to match the value from binning mode config.
         */
        cl_emit(&job->rcl, TILE_LIST_INITIAL_BLOCK_SIZE, init) {
                init.use_auto_chained_tile_lists = true;
                init.size_of_first_block_in_chained_tile_lists =
                        TILE_ALLOCATION_BLOCK_SIZE_64B;
        }

        /* ARB_framebuffer_no_attachments allows rendering to happen even when
         * the framebuffer has no attachments, the idea being that fragment
         * shaders can still do image load/store, ssbo, etc without having to
         * write to actual attachments, so always run at least one iteration
         * of the loop.
         */
        assert(job->num_layers > 0 || (job->load == 0 && job->store == 0));
        for (int layer = 0; layer < MAX2(1, job->num_layers); layer++)
                emit_render_layer(job, layer);

        cl_emit(&job->rcl, END_OF_RENDERING, end);
}
