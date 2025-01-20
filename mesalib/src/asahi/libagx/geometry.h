/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"

#ifndef __OPENCL_VERSION__
#include "util/bitscan.h"
#define libagx_popcount(x)   util_bitcount64(x)
#define libagx_sub_sat(x, y) ((x >= y) ? (x - y) : 0)
#else
#define libagx_popcount(x)   popcount(x)
#define libagx_sub_sat(x, y) sub_sat(x, y)
#endif

#ifndef LIBAGX_GEOMETRY_H
#define LIBAGX_GEOMETRY_H

#define MAX_SO_BUFFERS     4
#define MAX_VERTEX_STREAMS 4

/* Packed geometry state buffer */
struct agx_geometry_state {
   /* Heap to allocate from. */
   DEVICE(uchar) heap;
   uint32_t heap_bottom, heap_size;
} PACKED;
static_assert(sizeof(struct agx_geometry_state) == 4 * 4);

#ifdef __OPENCL_VERSION__
static inline global void *
agx_heap_alloc_nonatomic(global struct agx_geometry_state *heap, size_t size)
{
   global void *out = heap->heap + heap->heap_bottom;
   heap->heap_bottom += size;
   return out;
}
#endif

struct agx_ia_state {
   /* Index buffer if present. */
   uint64_t index_buffer;

   /* Size of the bound index buffer for bounds checking */
   uint32_t index_buffer_range_el;

   /* Number of vertices per instance. Written by CPU for direct draw, indirect
    * setup kernel for indirect. This is used for VS->GS and VS->TCS indexing.
    */
   uint32_t verts_per_instance;
} PACKED;
static_assert(sizeof(struct agx_ia_state) == 4 * 4);

static inline uint64_t
libagx_index_buffer(uint64_t index_buffer, uint size_el, uint offset_el,
                    uint elsize_B, uint64_t zero_sink)
{
   if (offset_el < size_el)
      return index_buffer + (offset_el * elsize_B);
   else
      return zero_sink;
}

static inline uint
libagx_index_buffer_range_el(uint size_el, uint offset_el)
{
   return libagx_sub_sat(size_el, offset_el);
}

struct agx_geometry_params {
   /* Persistent (cross-draw) geometry state */
   DEVICE(struct agx_geometry_state) state;

   /* Address of associated indirect draw buffer */
   DEVICE(uint) indirect_desc;

   /* Address of count buffer. For an indirect draw, this will be written by the
    * indirect setup kernel.
    */
   DEVICE(uint) count_buffer;

   /* Address of the primitives generated counters */
   DEVICE(uint) prims_generated_counter[MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_prims_generated_counter[MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_overflow[MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_any_overflow;

   /* Pointers to transform feedback buffer offsets in bytes */
   DEVICE(uint) xfb_offs_ptrs[MAX_SO_BUFFERS];

   /* Output index buffer, allocated by pre-GS. */
   DEVICE(uint) output_index_buffer;

   /* Address of transform feedback buffer in general, supplied by the CPU. */
   DEVICE(uchar) xfb_base_original[MAX_SO_BUFFERS];

   /* Address of transform feedback for the current primitive. Written by pre-GS
    * program.
    */
   DEVICE(uchar) xfb_base[MAX_SO_BUFFERS];

   /* Address and present mask for the input to the geometry shader. These will
    * reflect the vertex shader for VS->GS or instead the tessellation
    * evaluation shader for TES->GS.
    */
   uint64_t input_buffer;
   uint64_t input_mask;

   /* Location-indexed mask of flat outputs, used for lowering GL edge flags. */
   uint64_t flat_outputs;

   uint32_t xfb_size[MAX_SO_BUFFERS];

   /* Number of primitives emitted by transform feedback per stream. Written by
    * the pre-GS program.
    */
   uint32_t xfb_prims[MAX_VERTEX_STREAMS];

   /* Within an indirect GS draw, the grids used to dispatch the VS/GS written
    * out by the GS indirect setup kernel or the CPU for a direct draw.
    */
   uint32_t vs_grid[3];
   uint32_t gs_grid[3];

   /* Number of input primitives across all instances, calculated by the CPU for
    * a direct draw or the GS indirect setup kernel for an indirect draw.
    */
   uint32_t input_primitives;

   /* Number of input primitives per instance, rounded up to a power-of-two and
    * with the base-2 log taken. This is used to partition the output vertex IDs
    * efficiently.
    */
   uint32_t primitives_log2;

   /* Number of bytes output by the GS count shader per input primitive (may be
    * 0), written by CPU and consumed by indirect draw setup shader for
    * allocating counts.
    */
   uint32_t count_buffer_stride;

   /* Dynamic input topology. Must be compatible with the geometry shader's
    * layout() declared input class.
    */
   uint32_t input_topology;
} PACKED;
static_assert(sizeof(struct agx_geometry_params) == 82 * 4);

/* TCS shared memory layout:
 *
 *    vec4 vs_outputs[VERTICES_IN_INPUT_PATCH][TOTAL_VERTEX_OUTPUTS];
 *
 * TODO: compact.
 */
static inline uint
libagx_tcs_in_offs_el(uint vtx, gl_varying_slot location,
                      uint64_t crosslane_vs_out_mask)
{
   uint base = vtx * libagx_popcount(crosslane_vs_out_mask);
   uint offs = libagx_popcount(crosslane_vs_out_mask &
                               (((uint64_t)(1) << location) - 1));

   return base + offs;
}

static inline uint
libagx_tcs_in_offs(uint vtx, gl_varying_slot location,
                   uint64_t crosslane_vs_out_mask)
{
   return libagx_tcs_in_offs_el(vtx, location, crosslane_vs_out_mask) * 16;
}

static inline uint
libagx_tcs_in_size(uint32_t vertices_in_patch, uint64_t crosslane_vs_out_mask)
{
   return vertices_in_patch * libagx_popcount(crosslane_vs_out_mask) * 16;
}

/*
 * TCS out buffer layout, per-patch:
 *
 *    float tess_level_outer[4];
 *    float tess_level_inner[2];
 *    vec4 patch_out[MAX_PATCH_OUTPUTS];
 *    vec4 vtx_out[OUT_PATCH_SIZE][TOTAL_VERTEX_OUTPUTS];
 *
 * Vertex out are compacted based on the mask of written out. Patch
 * out are used as-is.
 *
 * Bounding boxes are ignored.
 */
static inline uint
libagx_tcs_out_offs_el(uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                       uint64_t vtx_out_mask)
{
   uint off = 0;
   if (location == VARYING_SLOT_TESS_LEVEL_OUTER)
      return off;

   off += 4;
   if (location == VARYING_SLOT_TESS_LEVEL_INNER)
      return off;

   off += 2;
   if (location >= VARYING_SLOT_PATCH0)
      return off + (4 * (location - VARYING_SLOT_PATCH0));

   /* Anything else is a per-vtx output */
   off += 4 * nr_patch_out;
   off += 4 * vtx_id * libagx_popcount(vtx_out_mask);

   uint idx = libagx_popcount(vtx_out_mask & (((uint64_t)(1) << location) - 1));
   return off + (4 * idx);
}

static inline uint
libagx_tcs_out_offs(uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                    uint64_t vtx_out_mask)
{
   return libagx_tcs_out_offs_el(vtx_id, location, nr_patch_out, vtx_out_mask) *
          4;
}

static inline uint
libagx_tcs_out_stride_el(uint nr_patch_out, uint out_patch_size,
                         uint64_t vtx_out_mask)
{
   return libagx_tcs_out_offs_el(out_patch_size, 0, nr_patch_out, vtx_out_mask);
}

static inline uint
libagx_tcs_out_stride(uint nr_patch_out, uint out_patch_size,
                      uint64_t vtx_out_mask)
{
   return libagx_tcs_out_stride_el(nr_patch_out, out_patch_size, vtx_out_mask) *
          4;
}

/* In a tess eval shader, stride for hw vertex ID */
#define LIBAGX_TES_PATCH_ID_STRIDE 8192

static uint
libagx_compact_prim(enum mesa_prim prim)
{
   static_assert(MESA_PRIM_QUAD_STRIP == MESA_PRIM_QUADS + 1);
   static_assert(MESA_PRIM_POLYGON == MESA_PRIM_QUADS + 2);

#ifndef __OPENCL_VERSION__
   assert(prim != MESA_PRIM_QUADS);
   assert(prim != MESA_PRIM_QUAD_STRIP);
   assert(prim != MESA_PRIM_POLYGON);
   assert(prim != MESA_PRIM_PATCHES);
#endif

   return (prim >= MESA_PRIM_QUADS) ? (prim - 3) : prim;
}

static enum mesa_prim
libagx_uncompact_prim(uint packed)
{
   return (packed >= MESA_PRIM_QUADS) ? (packed + 3) : packed;
}

#endif
