/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "geometry.h"
#include "tessellator.h"
#include <agx_pack.h>

uint
libagx_tcs_patch_vertices_in(constant struct libagx_tess_args *p)
{
   return p->input_patch_size;
}

uint
libagx_tes_patch_vertices_in(constant struct libagx_tess_args *p)
{
   return p->output_patch_size;
}

uint
libagx_tcs_unrolled_id(constant struct libagx_tess_args *p, uint3 wg_id)
{
   return (wg_id.y * p->patches_per_instance) + wg_id.x;
}

uint64_t
libagx_tes_buffer(constant struct libagx_tess_args *p)
{
   return p->tes_buffer;
}

/*
 * Helper to lower indexing for a tess eval shader ran as a compute shader. This
 * handles the tess+geom case. This is simpler than the general input assembly
 * lowering, as we know:
 *
 * 1. the index buffer is U32
 * 2. the index is in bounds
 *
 * Therefore we do a simple load. No bounds checking needed.
 */
uint32_t
libagx_load_tes_index(constant struct libagx_tess_args *p, uint32_t index)
{
   return p->index_buffer[index];
}

ushort
libagx_tcs_in_offset(uint vtx, gl_varying_slot location,
                     uint64_t crosslane_vs_out_mask)
{
   return libagx_tcs_in_offs(vtx, location, crosslane_vs_out_mask);
}

uintptr_t
libagx_tcs_out_address(constant struct libagx_tess_args *p, uint patch_id,
                       uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                       uint out_patch_size, uint64_t vtx_out_mask)
{
   uint stride =
      libagx_tcs_out_stride(nr_patch_out, out_patch_size, vtx_out_mask);

   uint offs =
      libagx_tcs_out_offs(vtx_id, location, nr_patch_out, vtx_out_mask);

   return (uintptr_t)(p->tcs_buffer) + (patch_id * stride) + offs;
}

static uint
libagx_tes_unrolled_patch_id(uint raw_id)
{
   return raw_id / LIBAGX_TES_PATCH_ID_STRIDE;
}

uint
libagx_tes_patch_id(constant struct libagx_tess_args *p, uint raw_id)
{
   return libagx_tes_unrolled_patch_id(raw_id) % p->patches_per_instance;
}

static uint
tes_vertex_id_in_patch(uint raw_id)
{
   return raw_id % LIBAGX_TES_PATCH_ID_STRIDE;
}

float2
libagx_load_tess_coord(constant struct libagx_tess_args *p, uint raw_id)
{
   uint patch = libagx_tes_unrolled_patch_id(raw_id);
   uint vtx = tes_vertex_id_in_patch(raw_id);

   global struct libagx_tess_point *t =
      &p->patch_coord_buffer[p->coord_allocs[patch] + vtx];

   /* Written weirdly because NIR struggles with loads of structs */
   return *((global float2 *)t);
}

uintptr_t
libagx_tes_in_address(constant struct libagx_tess_args *p, uint raw_id,
                      uint vtx_id, gl_varying_slot location)
{
   uint patch = libagx_tes_unrolled_patch_id(raw_id);

   return libagx_tcs_out_address(p, patch, vtx_id, location,
                                 p->tcs_patch_constants, p->output_patch_size,
                                 p->tcs_per_vertex_outputs);
}

float4
libagx_tess_level_outer_default(constant struct libagx_tess_args *p)
{
   return (
      float4)(p->tess_level_outer_default[0], p->tess_level_outer_default[1],
              p->tess_level_outer_default[2], p->tess_level_outer_default[3]);
}

float2
libagx_tess_level_inner_default(constant struct libagx_tess_args *p)
{
   return (float2)(p->tess_level_inner_default[0],
                   p->tess_level_inner_default[1]);
}

void
libagx_tess_setup_indirect(global struct libagx_tess_args *p, bool with_counts,
                           bool point_mode)
{
   uint count = p->indirect[0], instance_count = p->indirect[1];
   unsigned in_patches = count / p->input_patch_size;

   /* TCS invocation counter increments once per-patch */
   if (p->tcs_statistic) {
      *(p->tcs_statistic) += in_patches;
   }

   size_t draw_stride =
      ((!with_counts && point_mode) ? 4 : 6) * sizeof(uint32_t);

   unsigned unrolled_patches = in_patches * instance_count;

   uint32_t alloc = 0;
   uint32_t tcs_out_offs = alloc;
   alloc += unrolled_patches * p->tcs_stride_el * 4;

   uint32_t patch_coord_offs = alloc;
   alloc += unrolled_patches * 4;

   uint32_t count_offs = alloc;
   if (with_counts)
      alloc += unrolled_patches * sizeof(uint32_t);

   uint vb_offs = alloc;
   uint vb_size = libagx_tcs_in_size(count * instance_count, p->vertex_outputs);
   alloc += vb_size;

   /* Allocate all patch calculations in one go */
   global uchar *blob = p->heap->heap + p->heap->heap_bottom;
   p->heap->heap_bottom += alloc;

   p->tcs_buffer = (global float *)(blob + tcs_out_offs);
   p->patches_per_instance = in_patches;
   p->coord_allocs = (global uint *)(blob + patch_coord_offs);
   p->nr_patches = unrolled_patches;

   *(p->vertex_output_buffer_ptr) = (uintptr_t)(blob + vb_offs);

   if (with_counts) {
      p->counts = (global uint32_t *)(blob + count_offs);
   } else {
#if 0
      /* Arrange so we return after all generated draws. agx_pack would be nicer
       * here but designated initializers lead to scratch access...
       */
      global uint32_t *ret =
         (global uint32_t *)(blob + draw_offs +
                             (draw_stride * unrolled_patches));

      *ret = (AGX_VDM_BLOCK_TYPE_BARRIER << 29) | /* with return */ (1u << 27);
#endif
      /* TODO */
   }

   /* VS grid size */
   p->grids[0] = count;
   p->grids[1] = instance_count;
   p->grids[2] = 1;

   /* VS workgroup size */
   p->grids[3] = 64;
   p->grids[4] = 1;
   p->grids[5] = 1;

   /* TCS grid size */
   p->grids[6] = in_patches * p->output_patch_size;
   p->grids[7] = instance_count;
   p->grids[8] = 1;

   /* TCS workgroup size */
   p->grids[9] = p->output_patch_size;
   p->grids[10] = 1;
   p->grids[11] = 1;

   /* Tess grid size */
   p->grids[12] = unrolled_patches;
   p->grids[13] = 1;
   p->grids[14] = 1;

   /* Tess workgroup size */
   p->grids[15] = 64;
   p->grids[16] = 1;
   p->grids[17] = 1;
}
