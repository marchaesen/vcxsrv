/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <limits.h>
#include "util/u_memory.h"
#include "util/u_math.h"
#include "util/u_rect.h"
#include "util/u_surface.h"
#include "util/u_pack_color.h"
#include "util/u_string.h"
#include "util/u_thread.h"
#include "util/u_memset.h"
#include "util/os_time.h"

#include "lp_scene_queue.h"
#include "lp_context.h"
#include "lp_debug.h"
#include "lp_fence.h"
#include "lp_perf.h"
#include "lp_query.h"
#include "lp_rast.h"
#include "lp_rast_priv.h"
#include "gallivm/lp_bld_format.h"
#include "gallivm/lp_bld_debug.h"
#include "lp_scene.h"
#include "lp_tex_sample.h"


#ifdef DEBUG
int jit_line = 0;
const struct lp_rast_state *jit_state = NULL;
const struct lp_rasterizer_task *jit_task = NULL;
#endif

const float lp_sample_pos_4x[4][2] = { { 0.375, 0.125 },
                                       { 0.875, 0.375 },
                                       { 0.125, 0.625 },
                                       { 0.625, 0.875 } };

/**
 * Begin rasterizing a scene.
 * Called once per scene by one thread.
 */
static void
lp_rast_begin( struct lp_rasterizer *rast,
               struct lp_scene *scene )
{
   rast->curr_scene = scene;

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   lp_scene_begin_rasterization( scene );
   lp_scene_bin_iter_begin( scene );
}


static void
lp_rast_end( struct lp_rasterizer *rast )
{
   lp_scene_end_rasterization( rast->curr_scene );

   rast->curr_scene = NULL;
}


/**
 * Beginning rasterization of a tile.
 * \param x  window X position of the tile, in pixels
 * \param y  window Y position of the tile, in pixels
 */
static void
lp_rast_tile_begin(struct lp_rasterizer_task *task,
                   const struct cmd_bin *bin,
                   int x, int y)
{
   unsigned i;
   struct lp_scene *scene = task->scene;

   LP_DBG(DEBUG_RAST, "%s %d,%d\n", __FUNCTION__, x, y);

   task->bin = bin;
   task->x = x * TILE_SIZE;
   task->y = y * TILE_SIZE;
   task->width = TILE_SIZE + x * TILE_SIZE > task->scene->fb.width ?
                    task->scene->fb.width - x * TILE_SIZE : TILE_SIZE;
   task->height = TILE_SIZE + y * TILE_SIZE > task->scene->fb.height ?
                    task->scene->fb.height - y * TILE_SIZE : TILE_SIZE;

   task->thread_data.vis_counter = 0;
   task->thread_data.ps_invocations = 0;

   for (i = 0; i < task->scene->fb.nr_cbufs; i++) {
      if (task->scene->fb.cbufs[i]) {
         task->color_tiles[i] = scene->cbufs[i].map +
                                scene->cbufs[i].stride * task->y +
                                scene->cbufs[i].format_bytes * task->x;
      }
   }
   if (task->scene->fb.zsbuf) {
      task->depth_tile = scene->zsbuf.map +
                         scene->zsbuf.stride * task->y +
                         scene->zsbuf.format_bytes * task->x;
   }
}


/**
 * Clear the rasterizer's current color tile.
 * This is a bin command called during bin processing.
 * Clear commands always clear all bound layers.
 */
static void
lp_rast_clear_color(struct lp_rasterizer_task *task,
                    const union lp_rast_cmd_arg arg)
{
   const struct lp_scene *scene = task->scene;
   unsigned cbuf = arg.clear_rb->cbuf;
   union util_color uc;
   enum pipe_format format;

   /* we never bin clear commands for non-existing buffers */
   assert(cbuf < scene->fb.nr_cbufs);
   assert(scene->fb.cbufs[cbuf]);

   format = scene->fb.cbufs[cbuf]->format;
   uc = arg.clear_rb->color_val;

   /*
    * this is pretty rough since we have target format (bunch of bytes...) here.
    * dump it as raw 4 dwords.
    */
   LP_DBG(DEBUG_RAST, "%s clear value (target format %d) raw 0x%x,0x%x,0x%x,0x%x\n",
          __FUNCTION__, format, uc.ui[0], uc.ui[1], uc.ui[2], uc.ui[3]);

   for (unsigned s = 0; s < scene->cbufs[cbuf].nr_samples; s++) {
      void *map = (char *)scene->cbufs[cbuf].map + scene->cbufs[cbuf].sample_stride * s;
      util_fill_box(map,
                    format,
                    scene->cbufs[cbuf].stride,
                    scene->cbufs[cbuf].layer_stride,
                    task->x,
                    task->y,
                    0,
                    task->width,
                    task->height,
                    scene->fb_max_layer + 1,
                    &uc);
   }

   /* this will increase for each rb which probably doesn't mean much */
   LP_COUNT(nr_color_tile_clear);
}


/**
 * Clear the rasterizer's current z/stencil tile.
 * This is a bin command called during bin processing.
 * Clear commands always clear all bound layers.
 */
static void
lp_rast_clear_zstencil(struct lp_rasterizer_task *task,
                       const union lp_rast_cmd_arg arg)
{
   const struct lp_scene *scene = task->scene;
   uint64_t clear_value64 = arg.clear_zstencil.value;
   uint64_t clear_mask64 = arg.clear_zstencil.mask;
   uint32_t clear_value = (uint32_t) clear_value64;
   uint32_t clear_mask = (uint32_t) clear_mask64;
   const unsigned height = task->height;
   const unsigned width = task->width;
   const unsigned dst_stride = scene->zsbuf.stride;
   uint8_t *dst;
   unsigned i, j;
   unsigned block_size;

   LP_DBG(DEBUG_RAST, "%s: value=0x%08x, mask=0x%08x\n",
           __FUNCTION__, clear_value, clear_mask);

   /*
    * Clear the area of the depth/depth buffer matching this tile.
    */

   if (scene->fb.zsbuf) {
      unsigned layer;

      for (unsigned s = 0; s < scene->zsbuf.nr_samples; s++) {
         uint8_t *dst_layer = task->depth_tile + (s * scene->zsbuf.sample_stride);
         block_size = util_format_get_blocksize(scene->fb.zsbuf->format);

         clear_value &= clear_mask;

         for (layer = 0; layer <= scene->fb_max_layer; layer++) {
            dst = dst_layer;

            switch (block_size) {
            case 1:
               assert(clear_mask == 0xff);
               for (i = 0; i < height; i++) {
                  uint8_t *row = (uint8_t *)dst;
                  memset(row, (uint8_t) clear_value, width);
                  dst += dst_stride;
               }
               break;
            case 2:
               if (clear_mask == 0xffff) {
                  for (i = 0; i < height; i++) {
                     uint16_t *row = (uint16_t *)dst;
                     for (j = 0; j < width; j++)
                        *row++ = (uint16_t) clear_value;
                     dst += dst_stride;
                  }
               }
               else {
                  for (i = 0; i < height; i++) {
                     uint16_t *row = (uint16_t *)dst;
                     for (j = 0; j < width; j++) {
                        uint16_t tmp = ~clear_mask & *row;
                        *row++ = clear_value | tmp;
                     }
                     dst += dst_stride;
                  }
               }
               break;
            case 4:
               if (clear_mask == 0xffffffff) {
                  for (i = 0; i < height; i++) {
                     util_memset32(dst, clear_value, width);
                     dst += dst_stride;
                  }
               }
               else {
                  for (i = 0; i < height; i++) {
                     uint32_t *row = (uint32_t *)dst;
                     for (j = 0; j < width; j++) {
                        uint32_t tmp = ~clear_mask & *row;
                        *row++ = clear_value | tmp;
                     }
                     dst += dst_stride;
                  }
               }
               break;
            case 8:
               clear_value64 &= clear_mask64;
               if (clear_mask64 == 0xffffffffffULL) {
                  for (i = 0; i < height; i++) {
                     util_memset64(dst, clear_value64, width);
                     dst += dst_stride;
                  }
               }
               else {
                  for (i = 0; i < height; i++) {
                     uint64_t *row = (uint64_t *)dst;
                     for (j = 0; j < width; j++) {
                        uint64_t tmp = ~clear_mask64 & *row;
                        *row++ = clear_value64 | tmp;
                     }
                     dst += dst_stride;
                  }
               }
               break;

            default:
               assert(0);
               break;
            }
            dst_layer += scene->zsbuf.layer_stride;
         }
      }
   }
}



/**
 * Run the shader on all blocks in a tile.  This is used when a tile is
 * completely contained inside a triangle.
 * This is a bin command called during bin processing.
 */
static void
lp_rast_shade_tile(struct lp_rasterizer_task *task,
                   const union lp_rast_cmd_arg arg)
{
   const struct lp_scene *scene = task->scene;
   const struct lp_rast_shader_inputs *inputs = arg.shade_tile;
   const struct lp_rast_state *state;
   struct lp_fragment_shader_variant *variant;
   const unsigned tile_x = task->x, tile_y = task->y;
   unsigned x, y;

   if (inputs->disable) {
      /* This command was partially binned and has been disabled */
      return;
   }

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   state = task->state;
   assert(state);
   if (!state) {
      return;
   }
   variant = state->variant;

   /* render the whole 64x64 tile in 4x4 chunks */
   for (y = 0; y < task->height; y += 4){
      for (x = 0; x < task->width; x += 4) {
         uint8_t *color[PIPE_MAX_COLOR_BUFS];
         unsigned stride[PIPE_MAX_COLOR_BUFS];
         unsigned sample_stride[PIPE_MAX_COLOR_BUFS];
         uint8_t *depth = NULL;
         unsigned depth_stride = 0;
         unsigned depth_sample_stride = 0;
         unsigned i;

         /* color buffer */
         for (i = 0; i < scene->fb.nr_cbufs; i++){
            if (scene->fb.cbufs[i]) {
               stride[i] = scene->cbufs[i].stride;
               sample_stride[i] = scene->cbufs[i].sample_stride;
               color[i] = lp_rast_get_color_block_pointer(task, i, tile_x + x,
                                                          tile_y + y, inputs->layer + inputs->view_index);
            }
            else {
               stride[i] = 0;
               sample_stride[i] = 0;
               color[i] = NULL;
            }
         }

         /* depth buffer */
         if (scene->zsbuf.map) {
            depth = lp_rast_get_depth_block_pointer(task, tile_x + x,
                                                    tile_y + y, inputs->layer + inputs->view_index);
            depth_stride = scene->zsbuf.stride;
            depth_sample_stride = scene->zsbuf.sample_stride;
         }

         uint64_t mask = 0;
         for (unsigned i = 0; i < scene->fb_max_samples; i++)
            mask |= (uint64_t)(0xffff) << (16 * i);

         /* Propagate non-interpolated raster state. */
         task->thread_data.raster_state.viewport_index = inputs->viewport_index;
         task->thread_data.raster_state.view_index = inputs->view_index;

         /* run shader on 4x4 block */
         BEGIN_JIT_CALL(state, task);
         variant->jit_function[RAST_WHOLE]( &state->jit_context,
                                            tile_x + x, tile_y + y,
                                            inputs->frontfacing,
                                            GET_A0(inputs),
                                            GET_DADX(inputs),
                                            GET_DADY(inputs),
                                            color,
                                            depth,
                                            mask,
                                            &task->thread_data,
                                            stride,
                                            depth_stride,
                                            sample_stride,
                                            depth_sample_stride);
         END_JIT_CALL();
      }
   }
}


/**
 * Run the shader on all blocks in a tile.  This is used when a tile is
 * completely contained inside a triangle, and the shader is opaque.
 * This is a bin command called during bin processing.
 */
static void
lp_rast_shade_tile_opaque(struct lp_rasterizer_task *task,
                          const union lp_rast_cmd_arg arg)
{
   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   assert(task->state);
   if (!task->state) {
      return;
   }

   lp_rast_shade_tile(task, arg);
}


/**
 * Compute shading for a 4x4 block of pixels inside a triangle.
 * This is a bin command called during bin processing.
 * \param x  X position of quad in window coords
 * \param y  Y position of quad in window coords
 */
void
lp_rast_shade_quads_mask_sample(struct lp_rasterizer_task *task,
                                const struct lp_rast_shader_inputs *inputs,
                                unsigned x, unsigned y,
                                uint64_t mask)
{
   const struct lp_rast_state *state = task->state;
   struct lp_fragment_shader_variant *variant = state->variant;
   const struct lp_scene *scene = task->scene;
   uint8_t *color[PIPE_MAX_COLOR_BUFS];
   unsigned stride[PIPE_MAX_COLOR_BUFS];
   unsigned sample_stride[PIPE_MAX_COLOR_BUFS];
   uint8_t *depth = NULL;
   unsigned depth_stride = 0;
   unsigned depth_sample_stride = 0;
   unsigned i;

   assert(state);

   /* Sanity checks */
   assert(x < scene->tiles_x * TILE_SIZE);
   assert(y < scene->tiles_y * TILE_SIZE);
   assert(x % TILE_VECTOR_WIDTH == 0);
   assert(y % TILE_VECTOR_HEIGHT == 0);

   assert((x % 4) == 0);
   assert((y % 4) == 0);

   /* color buffer */
   for (i = 0; i < scene->fb.nr_cbufs; i++) {
      if (scene->fb.cbufs[i]) {
         stride[i] = scene->cbufs[i].stride;
         sample_stride[i] = scene->cbufs[i].sample_stride;
         color[i] = lp_rast_get_color_block_pointer(task, i, x, y,
                                                    inputs->layer + inputs->view_index);
      }
      else {
         stride[i] = 0;
         sample_stride[i] = 0;
         color[i] = NULL;
      }
   }

   /* depth buffer */
   if (scene->zsbuf.map) {
      depth_stride = scene->zsbuf.stride;
      depth_sample_stride = scene->zsbuf.sample_stride;
      depth = lp_rast_get_depth_block_pointer(task, x, y, inputs->layer + inputs->view_index);
   }

   assert(lp_check_alignment(state->jit_context.u8_blend_color, 16));

   /*
    * The rasterizer may produce fragments outside our
    * allocated 4x4 blocks hence need to filter them out here.
    */
   if ((x % TILE_SIZE) < task->width && (y % TILE_SIZE) < task->height) {
      /* Propagate non-interpolated raster state. */
      task->thread_data.raster_state.viewport_index = inputs->viewport_index;
      task->thread_data.raster_state.view_index = inputs->view_index;

      /* run shader on 4x4 block */
      BEGIN_JIT_CALL(state, task);
      variant->jit_function[RAST_EDGE_TEST](&state->jit_context,
                                            x, y,
                                            inputs->frontfacing,
                                            GET_A0(inputs),
                                            GET_DADX(inputs),
                                            GET_DADY(inputs),
                                            color,
                                            depth,
                                            mask,
                                            &task->thread_data,
                                            stride,
                                            depth_stride,
                                            sample_stride,
                                            depth_sample_stride);
      END_JIT_CALL();
   }
}

void
lp_rast_shade_quads_mask(struct lp_rasterizer_task *task,
                         const struct lp_rast_shader_inputs *inputs,
                         unsigned x, unsigned y,
                         unsigned mask)
{
   uint64_t new_mask = 0;
   for (unsigned i = 0; i < task->scene->fb_max_samples; i++)
      new_mask |= ((uint64_t)mask) << (16 * i);
   lp_rast_shade_quads_mask_sample(task, inputs, x, y, new_mask);
}

/**
 * Directly copy pixels from a texture to the destination color buffer.
 * This is a bin command called during bin processing.
 */
static void
lp_rast_blit_tile_to_dest(struct lp_rasterizer_task *task,
                          const union lp_rast_cmd_arg arg)
{
   const struct lp_scene *scene = task->scene;
   const struct lp_rast_shader_inputs *inputs = arg.shade_tile;
   const struct lp_rast_state *state = task->state;
   struct lp_fragment_shader_variant *variant = state->variant;
   const struct lp_jit_texture *texture = &state->jit_context.textures[0];
   const uint8_t *src;
   uint8_t *dst;
   unsigned src_stride;
   unsigned dst_stride;
   struct pipe_surface *cbuf = scene->fb.cbufs[0];
   const unsigned face_slice = cbuf->u.tex.first_layer;
   const unsigned level = cbuf->u.tex.level;
   struct llvmpipe_resource *lpt = llvmpipe_resource(cbuf->texture);
   int src_x, src_y;

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   if (inputs->disable) {
      /* This command was partially binned and has been disabled */
      return;
   }

   dst = llvmpipe_get_texture_image_address(lpt, face_slice, level);

   if (!dst)
      return;

   dst_stride = lpt->row_stride[level];

   src = texture->base;
   src_stride = texture->row_stride[0];

   src_x = util_iround(GET_A0(inputs)[1][0]*texture->width - 0.5f);
   src_y = util_iround(GET_A0(inputs)[1][1]*texture->height - 0.5f);

   src_x = src_x + task->x;
   src_y = src_y + task->y;

   if (0) {
      union util_color uc;
      uc.ui[0] = 0xff0000ff;
      util_fill_rect(dst,
                     cbuf->format,
                     dst_stride,
                     task->x,
                     task->y,
                     task->width,
                     task->height,
                     &uc);
      return;
   }

   if (src_x >= 0 &&
       src_y >= 0 &&
       src_x + task->width <= texture->width &&
       src_y + task->height <= texture->height) {

      if (variant->shader->kind == LP_FS_KIND_BLIT_RGBA ||
          (variant->shader->kind == LP_FS_KIND_BLIT_RGB1 &&
           cbuf->format == PIPE_FORMAT_B8G8R8X8_UNORM)) {
         util_copy_rect(dst,
                        cbuf->format,
                        dst_stride,
                        task->x, task->y,
                        task->width, task->height,
                        src, src_stride,
                        src_x, src_y);
         return;
      }

      if (variant->shader->kind == LP_FS_KIND_BLIT_RGB1) {
         if (cbuf->format == PIPE_FORMAT_B8G8R8A8_UNORM) {
            int x, y;

            dst += task->x * 4;
            src += src_x * 4;
            dst += task->y * dst_stride;
            src += src_y * src_stride;

            for (y = 0; y < task->height; ++y) {
               const uint32_t *src_row = (const uint32_t *)src;
               uint32_t *dst_row = (uint32_t *)dst;

               for (x = 0; x < task->width; ++x) {
                  *dst_row++ = *src_row++ | 0xff000000;
               }
               dst += dst_stride;
               src += src_stride;
            }

            return;
         }
      }

   }

   /*
    * Fall back to the jit shaders.
    */

   lp_rast_shade_tile_opaque(task, arg);
}

static void
lp_rast_blit_tile(struct lp_rasterizer_task *task,
                  const union lp_rast_cmd_arg arg)
{
   /* This kindof just works, but isn't efficient:
    */
   lp_rast_blit_tile_to_dest(task, arg);
}

/**
 * Begin a new occlusion query.
 * This is a bin command put in all bins.
 * Called per thread.
 */
static void
lp_rast_begin_query(struct lp_rasterizer_task *task,
                    const union lp_rast_cmd_arg arg)
{
   struct llvmpipe_query *pq = arg.query_obj;

   switch (pq->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      pq->start[task->thread_index] = task->thread_data.vis_counter;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      pq->start[task->thread_index] = task->thread_data.ps_invocations;
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      pq->start[task->thread_index] = os_time_get_nano();
      break;
   default:
      assert(0);
      break;
   }
}


/**
 * End the current occlusion query.
 * This is a bin command put in all bins.
 * Called per thread.
 */
static void
lp_rast_end_query(struct lp_rasterizer_task *task,
                  const union lp_rast_cmd_arg arg)
{
   struct llvmpipe_query *pq = arg.query_obj;

   switch (pq->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      pq->end[task->thread_index] +=
         task->thread_data.vis_counter - pq->start[task->thread_index];
      pq->start[task->thread_index] = 0;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      pq->end[task->thread_index] = os_time_get_nano();
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS:
      pq->end[task->thread_index] +=
         task->thread_data.ps_invocations - pq->start[task->thread_index];
      pq->start[task->thread_index] = 0;
      break;
   default:
      assert(0);
      break;
   }
}


void
lp_rast_set_state(struct lp_rasterizer_task *task,
                  const union lp_rast_cmd_arg arg)
{
   task->state = arg.state;
}



/**
 * Called when we're done writing to a color tile.
 */
static void
lp_rast_tile_end(struct lp_rasterizer_task *task)
{
   unsigned i;

   for (i = 0; i < task->scene->num_active_queries; ++i) {
      lp_rast_end_query(task, lp_rast_arg_query(task->scene->active_queries[i]));
   }

   /* debug */
   memset(task->color_tiles, 0, sizeof(task->color_tiles));
   task->depth_tile = NULL;

   task->bin = NULL;
}




 

/* Currently have two rendering paths only - the general case triangle
 * path and the super-specialized blit/clear path.
 */
#define TRI   ((LP_RAST_FLAGS_TRI <<1)-1)        /* general case */
#define RECT  ((LP_RAST_FLAGS_RECT<<1)-1)        /* direct rectangle rasterizer */
#define BLIT  ((LP_RAST_FLAGS_BLIT<<1)-1)        /* write direct-to-dest */

static const unsigned
rast_flags[] = {
   BLIT,                        /* clear color */
   TRI,                         /* clear zstencil */
   TRI,                         /* triangle_1 */
   TRI,                         /* triangle_2 */
   TRI,                         /* triangle_3 */
   TRI,                         /* triangle_4 */
   TRI,                         /* triangle_5 */
   TRI,                         /* triangle_6 */
   TRI,                         /* triangle_7 */
   TRI,                         /* triangle_8 */
   TRI,                         /* triangle_3_4 */
   TRI,                         /* triangle_3_16 */
   TRI,                         /* triangle_4_16 */
   RECT,                        /* shade_tile */
   RECT,                        /* shade_tile_opaque */
   TRI,                         /* begin_query */
   TRI,                         /* end_query */
   BLIT,                        /* set_state, */
   TRI,                         /* lp_rast_triangle_32_1 */
   TRI,                         /* lp_rast_triangle_32_2 */
   TRI,                         /* lp_rast_triangle_32_3 */
   TRI,                         /* lp_rast_triangle_32_4 */
   TRI,                         /* lp_rast_triangle_32_5 */
   TRI,                         /* lp_rast_triangle_32_6 */
   TRI,                         /* lp_rast_triangle_32_7 */
   TRI,                         /* lp_rast_triangle_32_8 */
   TRI,                         /* lp_rast_triangle_32_3_4 */
   TRI,                         /* lp_rast_triangle_32_3_16 */
   TRI,                         /* lp_rast_triangle_32_4_16 */
   TRI,                         /* lp_rast_triangle_ms_1 */
   TRI,                         /* lp_rast_triangle_ms_2 */
   TRI,                         /* lp_rast_triangle_ms_3 */
   TRI,                         /* lp_rast_triangle_ms_4 */
   TRI,                         /* lp_rast_triangle_ms_5 */
   TRI,                         /* lp_rast_triangle_ms_6 */
   TRI,                         /* lp_rast_triangle_ms_7 */
   TRI,                         /* lp_rast_triangle_ms_8 */
   TRI,                         /* lp_rast_triangle_ms_3_4 */
   TRI,                         /* lp_rast_triangle_ms_3_16 */
   TRI,                         /* lp_rast_triangle_ms_4_16 */

   RECT,                        /* rectangle */
   BLIT,                        /* blit */
};

/* 
 */
static const lp_rast_cmd_func
dispatch_blit[] = {
   lp_rast_clear_color,
   NULL,                        /* clear_zstencil */
   NULL,                        /* triangle_1 */
   NULL,                        /* triangle_2 */
   NULL,                        /* triangle_3 */
   NULL,                        /* triangle_4 */
   NULL,                        /* triangle_5 */
   NULL,                        /* triangle_6 */
   NULL,                        /* triangle_7 */
   NULL,                        /* triangle_8 */
   NULL,                        /* triangle_3_4 */
   NULL,                        /* triangle_3_16 */
   NULL,                        /* triangle_4_16 */
   NULL,                        /* shade_tile */
   NULL,                        /* shade_tile_opaque */
   NULL,                        /* begin_query */
   NULL,                        /* end_query */
   lp_rast_set_state,           /* set_state */
   NULL,                        /* lp_rast_triangle_32_1 */
   NULL,                        /* lp_rast_triangle_32_2 */
   NULL,                        /* lp_rast_triangle_32_3 */
   NULL,                        /* lp_rast_triangle_32_4 */
   NULL,                        /* lp_rast_triangle_32_5 */
   NULL,                        /* lp_rast_triangle_32_6 */
   NULL,                        /* lp_rast_triangle_32_7 */
   NULL,                        /* lp_rast_triangle_32_8 */
   NULL,                        /* lp_rast_triangle_32_3_4 */
   NULL,                        /* lp_rast_triangle_32_3_16 */
   NULL,                        /* lp_rast_triangle_32_4_16 */
   NULL,                        /* lp_rast_triangle_ms_1 */
   NULL,                        /* lp_rast_triangle_ms_2 */
   NULL,                        /* lp_rast_triangle_ms_3 */
   NULL,                        /* lp_rast_triangle_ms_4 */
   NULL,                        /* lp_rast_triangle_ms_5 */
   NULL,                        /* lp_rast_triangle_ms_6 */
   NULL,                        /* lp_rast_triangle_ms_7 */
   NULL,                        /* lp_rast_triangle_ms_8 */
   NULL,                        /* lp_rast_triangle_ms_3_4 */
   NULL,                        /* lp_rast_triangle_ms_3_16 */
   NULL,                        /* lp_rast_triangle_ms_4_16 */

   NULL,                        /* rectangle */
   lp_rast_blit_tile_to_dest,
};



/* Triangle and general case rasterization: Use the SOA llvm shdaers,
 * an active swizzled tile for each color buf, etc.  Don't blit/clear
 * directly to destination surface as we know there are swizzled
 * operations coming.
 */
static const lp_rast_cmd_func
dispatch_tri[] = {
   lp_rast_clear_color,
   lp_rast_clear_zstencil,
   lp_rast_triangle_1,
   lp_rast_triangle_2,
   lp_rast_triangle_3,
   lp_rast_triangle_4,
   lp_rast_triangle_5,
   lp_rast_triangle_6,
   lp_rast_triangle_7,
   lp_rast_triangle_8,
   lp_rast_triangle_3_4,
   lp_rast_triangle_3_16,
   lp_rast_triangle_4_16,
   lp_rast_shade_tile,
   lp_rast_shade_tile_opaque,
   lp_rast_begin_query,
   lp_rast_end_query,
   lp_rast_set_state,
   lp_rast_triangle_32_1,
   lp_rast_triangle_32_2,
   lp_rast_triangle_32_3,
   lp_rast_triangle_32_4,
   lp_rast_triangle_32_5,
   lp_rast_triangle_32_6,
   lp_rast_triangle_32_7,
   lp_rast_triangle_32_8,
   lp_rast_triangle_32_3_4,
   lp_rast_triangle_32_3_16,
   lp_rast_triangle_32_4_16,
   lp_rast_triangle_ms_1,
   lp_rast_triangle_ms_2,
   lp_rast_triangle_ms_3,
   lp_rast_triangle_ms_4,
   lp_rast_triangle_ms_5,
   lp_rast_triangle_ms_6,
   lp_rast_triangle_ms_7,
   lp_rast_triangle_ms_8,
   lp_rast_triangle_ms_3_4,
   lp_rast_triangle_ms_3_16,
   lp_rast_triangle_ms_4_16,
   lp_rast_rectangle,
   lp_rast_blit_tile,
};


/* Debug rasterization with most fastpaths disabled.
 */
static const lp_rast_cmd_func
dispatch_tri_debug[] =
{
   lp_rast_clear_color,
   lp_rast_clear_zstencil,
   lp_rast_triangle_1,
   lp_rast_triangle_2,
   lp_rast_triangle_3,
   lp_rast_triangle_4,
   lp_rast_triangle_5,
   lp_rast_triangle_6,
   lp_rast_triangle_7,
   lp_rast_triangle_8,
   lp_rast_triangle_3_4,
   lp_rast_triangle_3_16,
   lp_rast_triangle_4_16,
   lp_rast_shade_tile,
   lp_rast_shade_tile,
   lp_rast_begin_query,
   lp_rast_end_query,
   lp_rast_set_state,
   lp_rast_triangle_32_1,
   lp_rast_triangle_32_2,
   lp_rast_triangle_32_3,
   lp_rast_triangle_32_4,
   lp_rast_triangle_32_5,
   lp_rast_triangle_32_6,
   lp_rast_triangle_32_7,
   lp_rast_triangle_32_8,
   lp_rast_triangle_32_3_4,
   lp_rast_triangle_32_3_16,
   lp_rast_triangle_32_4_16,
   lp_rast_triangle_ms_1,
   lp_rast_triangle_ms_2,
   lp_rast_triangle_ms_3,
   lp_rast_triangle_ms_4,
   lp_rast_triangle_ms_5,
   lp_rast_triangle_ms_6,
   lp_rast_triangle_ms_7,
   lp_rast_triangle_ms_8,
   lp_rast_triangle_ms_3_4,
   lp_rast_triangle_ms_3_16,
   lp_rast_triangle_ms_4_16,

   lp_rast_rectangle,
   lp_rast_shade_tile,
};

struct lp_bin_info
lp_characterize_bin(const struct cmd_bin *bin)
{
   struct cmd_block *block;
   struct lp_bin_info info;
   unsigned andflags = ~0;
   unsigned k, j = 0;

   STATIC_ASSERT(ARRAY_SIZE(rast_flags) == LP_RAST_OP_MAX);

   for (block = bin->head; block; block = block->next) {
      for (k = 0; k < block->count; k++, j++) {
         andflags &= rast_flags[block->cmd[k]];
      }
   }
   
   info.type = andflags;
   info.count = j;

   return info;
}


static void
blit_rasterize_bin(struct lp_rasterizer_task *task,
                   const struct cmd_bin *bin)
{
   const struct cmd_block *block;
   unsigned k;

   STATIC_ASSERT(ARRAY_SIZE(dispatch_blit) == LP_RAST_OP_MAX);

   if (0) debug_printf("%s\n", __FUNCTION__);
   for (block = bin->head; block; block = block->next) {
      for (k = 0; k < block->count; k++) {
         dispatch_blit[block->cmd[k]]( task, block->arg[k] );
      }
   }
}

static void
tri_rasterize_bin(struct lp_rasterizer_task *task,
                  const struct cmd_bin *bin,
                  int x, int y)
{
   const struct cmd_block *block;
   unsigned k;

   STATIC_ASSERT(ARRAY_SIZE(dispatch_tri) == LP_RAST_OP_MAX);

   for (block = bin->head; block; block = block->next) {
      for (k = 0; k < block->count; k++) {
         dispatch_tri[block->cmd[k]]( task, block->arg[k] );
      }
   }
}

static void
debug_rasterize_bin(struct lp_rasterizer_task *task,
                  const struct cmd_bin *bin)
{
   const struct cmd_block *block;
   unsigned k;

   STATIC_ASSERT(ARRAY_SIZE(dispatch_tri_debug) == LP_RAST_OP_MAX);

   for (block = bin->head; block; block = block->next) {
      for (k = 0; k < block->count; k++) {
         dispatch_tri_debug[block->cmd[k]]( task, block->arg[k] );
      }
   }
}


/**
 * Rasterize commands for a single bin.
 * \param x, y  position of the bin's tile in the framebuffer
 * Must be called between lp_rast_begin() and lp_rast_end().
 * Called per thread.
 */
static void
rasterize_bin(struct lp_rasterizer_task *task,
              const struct cmd_bin *bin, int x, int y )
{
   struct lp_bin_info info = lp_characterize_bin(bin);

   lp_rast_tile_begin( task, bin, x, y );

   if (LP_DEBUG & DEBUG_NO_FASTPATH)
      debug_rasterize_bin(task, bin);
   else if (info.type & LP_RAST_FLAGS_BLIT)
      blit_rasterize_bin(task, bin);
   else if (task->scene->permit_linear_rasterizer &&
            !(LP_PERF & PERF_NO_RAST_LINEAR) &&
            (info.type & LP_RAST_FLAGS_RECT))
      lp_linear_rasterize_bin(task, bin);
   else
      tri_rasterize_bin(task, bin, x, y);

   lp_rast_tile_end(task);

#ifdef DEBUG
   /* Debug/Perf flags:
    */
   if (bin->head->count == 1) {
      if (bin->head->cmd[0] == LP_RAST_OP_BLIT)
         LP_COUNT(nr_pure_blit_64);
      else if (bin->head->cmd[0] == LP_RAST_OP_SHADE_TILE_OPAQUE)
         LP_COUNT(nr_pure_shade_opaque_64);
      else if (bin->head->cmd[0] == LP_RAST_OP_SHADE_TILE)
         LP_COUNT(nr_pure_shade_64);
   }
#endif
}


/* An empty bin is one that just loads the contents of the tile and
 * stores them again unchanged.  This typically happens when bins have
 * been flushed for some reason in the middle of a frame, or when
 * incremental updates are being made to a render target.
 * 
 * Try to avoid doing pointless work in this case.
 */
static boolean
is_empty_bin( const struct cmd_bin *bin )
{
   return bin->head == NULL;
}


/**
 * Rasterize/execute all bins within a scene.
 * Called per thread.
 */
static void
rasterize_scene(struct lp_rasterizer_task *task,
                struct lp_scene *scene)
{
   task->scene = scene;

   /* Clear the cache tags. This should not always be necessary but
      simpler for now. */
#if LP_USE_TEXTURE_CACHE
   memset(task->thread_data.cache->cache_tags, 0,
          sizeof(task->thread_data.cache->cache_tags));
#if LP_BUILD_FORMAT_CACHE_DEBUG
   task->thread_data.cache->cache_access_total = 0;
   task->thread_data.cache->cache_access_miss = 0;
#endif
#endif

   if (!task->rast->no_rast) {
      /* loop over scene bins, rasterize each */
      {
         struct cmd_bin *bin;
         int i, j;

         assert(scene);
         while ((bin = lp_scene_bin_iter_next(scene, &i, &j))) {
            if (!is_empty_bin( bin ))
               rasterize_bin(task, bin, i, j);
         }
      }
   }


#if LP_BUILD_FORMAT_CACHE_DEBUG
   {
      uint64_t total, miss;
      total = task->thread_data.cache->cache_access_total;
      miss = task->thread_data.cache->cache_access_miss;
      if (total) {
         debug_printf("thread %d cache access %llu miss %llu hit rate %f\n",
                 task->thread_index, (long long unsigned)total,
                 (long long unsigned)miss,
                 (float)(total - miss)/(float)total);
      }
   }
#endif

   if (scene->fence) {
      lp_fence_signal(scene->fence);
   }

   task->scene = NULL;
}


/**
 * Called by setup module when it has something for us to render.
 */
void
lp_rast_queue_scene( struct lp_rasterizer *rast,
                     struct lp_scene *scene)
{
   LP_DBG(DEBUG_SETUP, "%s\n", __FUNCTION__);

   if (rast->num_threads == 0) {
      /* no threading */
      unsigned fpstate = util_fpstate_get();

      /* Make sure that denorms are treated like zeros. This is 
       * the behavior required by D3D10. OpenGL doesn't care.
       */
      util_fpstate_set_denorms_to_zero(fpstate);

      lp_rast_begin( rast, scene );

      rasterize_scene( &rast->tasks[0], scene );

      lp_rast_end( rast );

      util_fpstate_set(fpstate);

      rast->curr_scene = NULL;
   }
   else {
      /* threaded rendering! */
      unsigned i;

      lp_scene_enqueue( rast->full_scenes, scene );

      /* signal the threads that there's work to do */
      for (i = 0; i < rast->num_threads; i++) {
         pipe_semaphore_signal(&rast->tasks[i].work_ready);
      }
   }

   LP_DBG(DEBUG_SETUP, "%s done \n", __FUNCTION__);
}


void
lp_rast_finish( struct lp_rasterizer *rast )
{
   if (rast->num_threads == 0) {
      /* nothing to do */
   }
   else {
      int i;

      /* wait for work to complete */
      for (i = 0; i < rast->num_threads; i++) {
         pipe_semaphore_wait(&rast->tasks[i].work_done);
      }
   }
}


/**
 * This is the thread's main entrypoint.
 * It's a simple loop:
 *   1. wait for work
 *   2. do work
 *   3. signal that we're done
 */
static int
thread_function(void *init_data)
{
   struct lp_rasterizer_task *task = (struct lp_rasterizer_task *) init_data;
   struct lp_rasterizer *rast = task->rast;
   boolean debug = false;
   char thread_name[16];
   unsigned fpstate;

   snprintf(thread_name, sizeof thread_name, "llvmpipe-%u", task->thread_index);
   u_thread_setname(thread_name);

   /* Make sure that denorms are treated like zeros. This is 
    * the behavior required by D3D10. OpenGL doesn't care.
    */
   fpstate = util_fpstate_get();
   util_fpstate_set_denorms_to_zero(fpstate);

   while (1) {
      /* wait for work */
      if (debug)
         debug_printf("thread %d waiting for work\n", task->thread_index);
      pipe_semaphore_wait(&task->work_ready);

      if (rast->exit_flag)
         break;

      if (task->thread_index == 0) {
         /* thread[0]:
          *  - get next scene to rasterize
          *  - map the framebuffer surfaces
          */
         lp_rast_begin( rast, 
                        lp_scene_dequeue( rast->full_scenes, TRUE ) );
      }

      /* Wait for all threads to get here so that threads[1+] don't
       * get a null rast->curr_scene pointer.
       */
      util_barrier_wait( &rast->barrier );

      /* do work */
      if (debug)
         debug_printf("thread %d doing work\n", task->thread_index);

      rasterize_scene(task,
                      rast->curr_scene);
      
      /* wait for all threads to finish with this scene */
      util_barrier_wait( &rast->barrier );

      /* XXX: shouldn't be necessary:
       */
      if (task->thread_index == 0) {
         lp_rast_end( rast );
      }

      /* signal done with work */
      if (debug)
         debug_printf("thread %d done working\n", task->thread_index);

      pipe_semaphore_signal(&task->work_done);
   }

#ifdef _WIN32
   pipe_semaphore_signal(&task->work_done);
#endif

   return 0;
}


/**
 * Initialize semaphores and spawn the threads.
 */
static void
create_rast_threads(struct lp_rasterizer *rast)
{
   unsigned i;

   /* NOTE: if num_threads is zero, we won't use any threads */
   for (i = 0; i < rast->num_threads; i++) {
      pipe_semaphore_init(&rast->tasks[i].work_ready, 0);
      pipe_semaphore_init(&rast->tasks[i].work_done, 0);
      rast->threads[i] = u_thread_create(thread_function,
                                            (void *) &rast->tasks[i]);
      if (!rast->threads[i]) {
         rast->num_threads = i; /* previous thread is max */
         break;
      }
   }
}



/**
 * Create new lp_rasterizer.  If num_threads is zero, don't create any
 * new threads, do rendering synchronously.
 * \param num_threads  number of rasterizer threads to create
 */
struct lp_rasterizer *
lp_rast_create( unsigned num_threads )
{
   struct lp_rasterizer *rast;
   unsigned i;

   rast = CALLOC_STRUCT(lp_rasterizer);
   if (!rast) {
      goto no_rast;
   }

   rast->full_scenes = lp_scene_queue_create();
   if (!rast->full_scenes) {
      goto no_full_scenes;
   }

   for (i = 0; i < MAX2(1, num_threads); i++) {
      struct lp_rasterizer_task *task = &rast->tasks[i];
      task->rast = rast;
      task->thread_index = i;
      task->thread_data.cache = align_malloc(sizeof(struct lp_build_format_cache),
                                             16);
      if (!task->thread_data.cache) {
         goto no_thread_data_cache;
      }
   }

   rast->num_threads = num_threads;

   rast->no_rast = debug_get_bool_option("LP_NO_RAST", FALSE);

   create_rast_threads(rast);

   /* for synchronizing rasterization threads */
   if (rast->num_threads > 0) {
      util_barrier_init( &rast->barrier, rast->num_threads );
   }

   memset(lp_dummy_tile, 0, sizeof lp_dummy_tile);

   return rast;

no_thread_data_cache:
   for (i = 0; i < MAX2(1, rast->num_threads); i++) {
      if (rast->tasks[i].thread_data.cache) {
         align_free(rast->tasks[i].thread_data.cache);
      }
   }

   lp_scene_queue_destroy(rast->full_scenes);
no_full_scenes:
   FREE(rast);
no_rast:
   return NULL;
}


/* Shutdown:
 */
void lp_rast_destroy( struct lp_rasterizer *rast )
{
   unsigned i;

   /* Set exit_flag and signal each thread's work_ready semaphore.
    * Each thread will be woken up, notice that the exit_flag is set and
    * break out of its main loop.  The thread will then exit.
    */
   rast->exit_flag = TRUE;
   for (i = 0; i < rast->num_threads; i++) {
      pipe_semaphore_signal(&rast->tasks[i].work_ready);
   }

   /* Wait for threads to terminate before cleaning up per-thread data.
    * We don't actually call pipe_thread_wait to avoid dead lock on Windows
    * per https://bugs.freedesktop.org/show_bug.cgi?id=76252 */
   for (i = 0; i < rast->num_threads; i++) {
#ifdef _WIN32
      /* Threads might already be dead - Windows apparently terminates other threads when
       * returning from main.
       */
      DWORD exit_code = STILL_ACTIVE;
      if (GetExitCodeThread(rast->threads[i], &exit_code) && exit_code == STILL_ACTIVE)
         pipe_semaphore_wait(&rast->tasks[i].work_done);
#else
      thrd_join(rast->threads[i], NULL);
#endif
   }

   /* Clean up per-thread data */
   for (i = 0; i < rast->num_threads; i++) {
      pipe_semaphore_destroy(&rast->tasks[i].work_ready);
      pipe_semaphore_destroy(&rast->tasks[i].work_done);
   }
   for (i = 0; i < MAX2(1, rast->num_threads); i++) {
      align_free(rast->tasks[i].thread_data.cache);
   }

   /* for synchronizing rasterization threads */
   if (rast->num_threads > 0) {
      util_barrier_destroy( &rast->barrier );
   }

   lp_scene_queue_destroy(rast->full_scenes);

   FREE(rast);
}


