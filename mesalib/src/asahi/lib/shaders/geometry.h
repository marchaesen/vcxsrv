/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/shader_enums.h"
#include "libagx.h"

#ifndef __OPENCL_VERSION__
#include "util/bitscan.h"
#define CONST(type_)         uint64_t
#define libagx_popcount(x)   util_bitcount64(x)
#define libagx_sub_sat(x, y) ((x >= y) ? (x - y) : 0)
#else
#define CONST(type_)         constant type_ *
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
   GLOBAL(uchar) heap;
   uint32_t heap_bottom, heap_size;
} PACKED;
AGX_STATIC_ASSERT(sizeof(struct agx_geometry_state) == 4 * 4);

struct agx_restart_unroll_params {
   /* Heap to allocate from across draws */
   GLOBAL(struct agx_geometry_state) heap;

   /* Input: index buffer if present. */
   uint64_t index_buffer;

   /* Input: draw count */
   CONST(uint) count;

   /* Input: indirect draw descriptor. Raw pointer since it's strided. */
   uint64_t draws;

   /* Output draw descriptors */
   GLOBAL(uint) out_draws;

   /* Pointer to zero */
   uint64_t zero_sink;

   /* Input: maximum draw count, count is clamped to this */
   uint32_t max_draws;

   /* Primitive restart index */
   uint32_t restart_index;

   /* Input index buffer size in elements */
   uint32_t index_buffer_size_el;

   /* Stride for the draw descriptor array */
   uint32_t draw_stride;

   /* Use first vertex as the provoking vertex for flat shading. We could stick
    * this in the key, but meh, you're already hosed for perf on the unroll
    * path.
    */
   uint32_t flatshade_first;
} PACKED;
AGX_STATIC_ASSERT(sizeof(struct agx_restart_unroll_params) == 17 * 4);

struct agx_gs_setup_indirect_params {
   /* Index buffer if present. */
   uint64_t index_buffer;

   /* Indirect draw descriptor. */
   CONST(uint) draw;

   /* Pointer to be written with allocated vertex buffer */
   GLOBAL(uintptr_t) vertex_buffer;

   /* Output input assembly state */
   GLOBAL(struct agx_ia_state) ia;

   /* Output geometry parameters */
   GLOBAL(struct agx_geometry_params) geom;

   /* Pointer to zero */
   uint64_t zero_sink;

   /* Vertex (TES) output mask for sizing the allocated buffer */
   uint64_t vs_outputs;

   /* The index size (1, 2, 4) or 0 if drawing without an index buffer. */
   uint32_t index_size_B;

   /* Size of the index buffer */
   uint32_t index_buffer_range_el;
} PACKED;
AGX_STATIC_ASSERT(sizeof(struct agx_gs_setup_indirect_params) == 16 * 4);

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
AGX_STATIC_ASSERT(sizeof(struct agx_ia_state) == 4 * 4);

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
   GLOBAL(struct agx_geometry_state) state;

   /* Address of associated indirect draw buffer */
   GLOBAL(uint) indirect_desc;

   /* Address of count buffer. For an indirect draw, this will be written by the
    * indirect setup kernel.
    */
   GLOBAL(uint) count_buffer;

   /* Address of the primitives generated counters */
   GLOBAL(uint) prims_generated_counter[MAX_VERTEX_STREAMS];
   GLOBAL(uint) xfb_prims_generated_counter[MAX_VERTEX_STREAMS];
   GLOBAL(uint) xfb_overflow[MAX_VERTEX_STREAMS];
   GLOBAL(uint) xfb_any_overflow;

   /* Pointers to transform feedback buffer offsets in bytes */
   GLOBAL(uint) xfb_offs_ptrs[MAX_SO_BUFFERS];

   /* Output index buffer, allocated by pre-GS. */
   GLOBAL(uint) output_index_buffer;

   /* Address of transform feedback buffer in general, supplied by the CPU. */
   GLOBAL(uchar) xfb_base_original[MAX_SO_BUFFERS];

   /* Address of transform feedback for the current primitive. Written by pre-GS
    * program.
    */
   GLOBAL(uchar) xfb_base[MAX_SO_BUFFERS];

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
AGX_STATIC_ASSERT(sizeof(struct agx_geometry_params) == 82 * 4);

/* TCS shared memory layout:
 *
 *    vec4 vs_outputs[VERTICES_IN_INPUT_PATCH][TOTAL_VERTEX_OUTPUTS];
 *
 * TODO: compact.
 */
static inline uint
libagx_tcs_in_offs(uint vtx, gl_varying_slot location,
                   uint64_t crosslane_vs_out_mask)
{
   uint base = vtx * libagx_popcount(crosslane_vs_out_mask);
   uint offs = libagx_popcount(crosslane_vs_out_mask &
                               (((uint64_t)(1) << location) - 1));

   return (base + offs) * 16;
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
libagx_tcs_out_offs(uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                    uint64_t vtx_out_mask)
{
   uint off = 0;
   if (location == VARYING_SLOT_TESS_LEVEL_OUTER)
      return off;

   off += 4 * sizeof(float);
   if (location == VARYING_SLOT_TESS_LEVEL_INNER)
      return off;

   off += 2 * sizeof(float);
   if (location >= VARYING_SLOT_PATCH0)
      return off + (16 * (location - VARYING_SLOT_PATCH0));

   /* Anything else is a per-vtx output */
   off += 16 * nr_patch_out;
   off += 16 * vtx_id * libagx_popcount(vtx_out_mask);

   uint idx = libagx_popcount(vtx_out_mask & (((uint64_t)(1) << location) - 1));
   return off + (16 * idx);
}

static inline uint
libagx_tcs_out_stride(uint nr_patch_out, uint out_patch_size,
                      uint64_t vtx_out_mask)
{
   return libagx_tcs_out_offs(out_patch_size, 0, nr_patch_out, vtx_out_mask);
}

/* In a tess eval shader, stride for hw vertex ID */
#define LIBAGX_TES_PATCH_ID_STRIDE 8192

#endif
