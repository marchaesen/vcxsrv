/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "shaders/tessellator.h"
#include "geometry.h"

/* Compatible with util/u_math.h */
static inline uint
util_logbase2_ceil(uint n)
{
   if (n <= 1)
      return 0;
   else
      return 32 - clz(n - 1);
}

/* Swap the two non-provoking vertices third vert in odd triangles. This
 * generates a vertex ID list with a consistent winding order.
 *
 * With prim and flatshade_first, the map : [0, 1, 2] -> [0, 1, 2] is its own
 * inverse. This lets us reuse it for both vertex fetch and transform feedback.
 */
uint
libagx_map_vertex_in_tri_strip(uint prim, uint vert, bool flatshade_first)
{
   unsigned pv = flatshade_first ? 0 : 2;

   bool even = (prim & 1) == 0;
   bool provoking = vert == pv;

   return (provoking || even) ? vert : ((3 - pv) - vert);
}

uint64_t
libagx_xfb_vertex_address(global struct agx_geometry_params *p, uint base_index,
                          uint vert, uint buffer, uint stride,
                          uint output_offset)
{
   uint index = base_index + vert;
   uint xfb_offset = (index * stride) + output_offset;

   return (uintptr_t)(p->xfb_base[buffer]) + xfb_offset;
}

uint
libagx_vertex_id_for_line_loop(uint prim, uint vert, uint num_prims)
{
   /* (0, 1), (1, 2), (2, 0) */
   if (prim == (num_prims - 1) && vert == 1)
      return 0;
   else
      return prim + vert;
}

uint
libagx_vertex_id_for_line_class(enum mesa_prim mode, uint prim, uint vert,
                                uint num_prims)
{
   /* Line list, line strip, or line loop */
   if (mode == MESA_PRIM_LINE_LOOP && prim == (num_prims - 1) && vert == 1)
      return 0;

   if (mode == MESA_PRIM_LINES)
      prim *= 2;

   return prim + vert;
}

uint
libagx_vertex_id_for_tri_fan(uint prim, uint vert, bool flatshade_first)
{
   /* Vulkan spec section 20.1.7 gives (i + 1, i + 2, 0) for a provoking
    * first. OpenGL instead wants (0, i + 1, i + 2) with a provoking last.
    * Piglit clipflat expects us to switch between these orders depending on
    * provoking vertex, to avoid trivializing the fan.
    *
    * Rotate accordingly.
    */
   if (flatshade_first) {
      vert = (vert == 2) ? 0 : (vert + 1);
   }

   /* The simpler form assuming last is provoking. */
   return (vert == 0) ? 0 : prim + vert;
}

uint
libagx_vertex_id_for_tri_class(enum mesa_prim mode, uint prim, uint vert,
                               bool flatshade_first)
{
   if (flatshade_first && mode == MESA_PRIM_TRIANGLE_FAN) {
      vert = vert + 1;
      vert = (vert == 3) ? 0 : vert;
   }

   if (mode == MESA_PRIM_TRIANGLE_FAN && vert == 0)
      return 0;

   if (mode == MESA_PRIM_TRIANGLES)
      prim *= 3;

   /* Triangle list, triangle strip, or triangle fan */
   if (mode == MESA_PRIM_TRIANGLE_STRIP) {
      unsigned pv = flatshade_first ? 0 : 2;

      bool even = (prim & 1) == 0;
      bool provoking = vert == pv;

      vert = ((provoking || even) ? vert : ((3 - pv) - vert));
   }

   return prim + vert;
}

uint
libagx_vertex_id_for_line_adj_class(enum mesa_prim mode, uint prim, uint vert)
{
   /* Line list adj or line strip adj */
   if (mode == MESA_PRIM_LINES_ADJACENCY)
      prim *= 4;

   return prim + vert;
}

uint
libagx_vertex_id_for_tri_strip_adj(uint prim, uint vert, uint num_prims,
                                   bool flatshade_first)
{
   /* See Vulkan spec section 20.1.11 "Triangle Strips With Adjancency".
    *
    * There are different cases for first/middle/last/only primitives and for
    * odd/even primitives.  Determine which case we're in.
    */
   bool last = prim == (num_prims - 1);
   bool first = prim == 0;
   bool even = (prim & 1) == 0;
   bool even_or_first = even || first;

   /* When the last vertex is provoking, we rotate the primitives
    * accordingly. This seems required for OpenGL.
    */
   if (!flatshade_first && !even_or_first) {
      vert = (vert + 4u) % 6u;
   }

   /* Offsets per the spec. The spec lists 6 cases with 6 offsets. Luckily,
    * there are lots of patterns we can exploit, avoiding a full 6x6 LUT.
    *
    * Here we assume the first vertex is provoking, the Vulkan default.
    */
   uint offsets[6] = {
      0,
      first ? 1 : (even ? -2 : 3),
      even_or_first ? 2 : 4,
      last ? 5 : 6,
      even_or_first ? 4 : 2,
      even_or_first ? 3 : -2,
   };

   /* Ensure NIR can see thru the local array */
   uint offset = 0;
   for (uint i = 1; i < 6; ++i) {
      if (i == vert)
         offset = offsets[i];
   }

   /* Finally add to the base of the primitive */
   return (prim * 2) + offset;
}

uint
libagx_vertex_id_for_tri_adj_class(enum mesa_prim mode, uint prim, uint vert,
                                   uint nr, bool flatshade_first)
{
   /* Tri adj list or tri adj strip */
   if (mode == MESA_PRIM_TRIANGLE_STRIP_ADJACENCY) {
      return libagx_vertex_id_for_tri_strip_adj(prim, vert, nr,
                                                flatshade_first);
   } else {
      return (6 * prim) + vert;
   }
}

uint
libagx_vertex_id_for_topology(enum mesa_prim mode, bool flatshade_first,
                              uint prim, uint vert, uint num_prims)
{
   switch (mode) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      /* Regular primitive: every N vertices defines a primitive */
      return (prim * mesa_vertices_per_prim(mode)) + vert;

   case MESA_PRIM_LINE_LOOP:
      return libagx_vertex_id_for_line_loop(prim, vert, num_prims);

   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      /* (i, i + 1) or (i, ..., i + 3) */
      return prim + vert;

   case MESA_PRIM_TRIANGLE_STRIP: {
      /* Order depends on the provoking vert.
       *
       * First: (0, 1, 2), (1, 3, 2), (2, 3, 4).
       * Last:  (0, 1, 2), (2, 1, 3), (2, 3, 4).
       *
       * Pull the (maybe swapped) vert from the corresponding primitive
       */
      return prim + libagx_map_vertex_in_tri_strip(prim, vert, flatshade_first);
   }

   case MESA_PRIM_TRIANGLE_FAN:
      return libagx_vertex_id_for_tri_fan(prim, vert, flatshade_first);

   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return libagx_vertex_id_for_tri_strip_adj(prim, vert, num_prims,
                                                flatshade_first);

   default:
      return 0;
   }
}

uint
libagx_load_index_buffer_internal(uintptr_t index_buffer,
                                  uint32_t index_buffer_range_el, uint id,
                                  uint index_size)
{
   bool oob = id >= index_buffer_range_el;

   /* If the load would be out-of-bounds, load the first element which is
    * assumed valid. If the application index buffer is empty with robustness2,
    * index_buffer will point to a zero sink where only the first is valid.
    */
   if (oob) {
      id = 0;
   }

   uint el;
   if (index_size == 1) {
      el = ((constant uint8_t *)index_buffer)[id];
   } else if (index_size == 2) {
      el = ((constant uint16_t *)index_buffer)[id];
   } else {
      el = ((constant uint32_t *)index_buffer)[id];
   }

   /* D3D robustness semantics. TODO: Optimize? */
   if (oob) {
      el = 0;
   }

   return el;
}

uint
libagx_load_index_buffer(constant struct agx_ia_state *p, uint id,
                         uint index_size)
{
   return libagx_load_index_buffer_internal(
      p->index_buffer, p->index_buffer_range_el, id, index_size);
}

/*
 * Return the ID of the first thread in the workgroup where cond is true, or
 * 1024 if cond is false across the workgroup.
 */
static uint
first_true_thread_in_workgroup(bool cond, local uint *scratch)
{
   barrier(CLK_LOCAL_MEM_FENCE);
   scratch[get_sub_group_id()] = ballot(cond);
   barrier(CLK_LOCAL_MEM_FENCE);

   uint first_group = ctz(ballot(scratch[get_sub_group_local_id()]));
   uint off = ctz(first_group < 32 ? scratch[first_group] : 0);
   return (first_group * 32) + off;
}

/*
 * Allocate memory from the heap (thread-safe). Returns the offset into the
 * heap. The allocation will be word-aligned.
 */
static inline uint
libagx_atomic_alloc(global struct agx_geometry_state *heap, uint size_B)
{
   return atomic_fetch_add((volatile atomic_uint *)(&heap->heap_bottom),
                           align(size_B, 8));
}

/*
 * When unrolling the index buffer for a draw, we translate the old indirect
 * draws to new indirect draws. This routine allocates the new index buffer and
 * sets up most of the new draw descriptor.
 */
static global void *
setup_unroll_for_draw(global struct agx_restart_unroll_params *p,
                      constant uint *in_draw, uint draw, enum mesa_prim mode,
                      uint index_size_B)
{
   /* Determine an upper bound on the memory required for the index buffer.
    * Restarts only decrease the unrolled index buffer size, so the maximum size
    * is the unrolled size when the input has no restarts.
    */
   uint max_prims = u_decomposed_prims_for_vertices(mode, in_draw[0]);
   uint max_verts = max_prims * mesa_vertices_per_prim(mode);
   uint alloc_size = max_verts * index_size_B;

   /* Allocate unrolled index buffer. Atomic since multiple threads may be
    * running to handle multidraw in parallel.
    */
   global struct agx_geometry_state *heap = p->heap;
   uint old_heap_bottom_B = libagx_atomic_alloc(p->heap, alloc_size);

   /* Regardless of the input stride, we use tightly packed output draws */
   global uint *out = &p->out_draws[5 * draw];

   /* Setup most of the descriptor. Count will be determined after unroll. */
   out[1] = in_draw[1];                       /* instance count */
   out[2] = old_heap_bottom_B / index_size_B; /* index offset */
   out[3] = in_draw[3];                       /* index bias */
   out[4] = in_draw[4];                       /* base instance */

   /* Return the index buffer we allocated */
   return (global uchar *)heap->heap + old_heap_bottom_B;
}

#define UNROLL(INDEX, suffix)                                                  \
   kernel void libagx_unroll_restart_##suffix(                                 \
      global struct agx_restart_unroll_params *p, enum mesa_prim mode,         \
      uint draw, uint tid)                                                     \
   {                                                                           \
      /* For an indirect multidraw, we are dispatched maxDraws times and       \
       * terminate trailing invocations.                                       \
       */                                                                      \
      if (p->count && draw >= *(p->count))                                     \
         return;                                                               \
                                                                               \
      constant uint *in_draw =                                                 \
         (constant uint *)(p->draws + (draw * p->draw_stride));                \
                                                                               \
      uint count = in_draw[0];                                                 \
                                                                               \
      local uintptr_t out_ptr, in_ptr;                                         \
      if (tid == 0) {                                                          \
         out_ptr = (uintptr_t)setup_unroll_for_draw(p, in_draw, draw, mode,    \
                                                    sizeof(INDEX));            \
                                                                               \
         /* Accessed thru local mem because NIR deref is too aggressive */     \
         in_ptr = (uintptr_t)(libagx_index_buffer(                             \
            p->index_buffer, p->index_buffer_size_el, in_draw[2],              \
            sizeof(INDEX), p->zero_sink));                                     \
      }                                                                        \
                                                                               \
      barrier(CLK_LOCAL_MEM_FENCE);                                            \
      global INDEX *out = (global INDEX *)out_ptr;                             \
                                                                               \
      local uint scratch[32];                                                  \
                                                                               \
      uint out_prims = 0;                                                      \
      INDEX restart_idx = p->restart_index;                                    \
      bool flatshade_first = p->flatshade_first;                               \
                                                                               \
      uint needle = 0;                                                         \
      uint per_prim = mesa_vertices_per_prim(mode);                            \
      while (needle < count) {                                                 \
         /* Search for next restart or the end. Lanes load in parallel. */     \
         uint next_restart = needle;                                           \
         for (;;) {                                                            \
            uint idx = next_restart + tid;                                     \
            bool restart =                                                     \
               idx >= count || libagx_load_index_buffer_internal(              \
                                  in_ptr, p->index_buffer_size_el, idx,        \
                                  sizeof(INDEX)) == restart_idx;               \
                                                                               \
            uint next_offs = first_true_thread_in_workgroup(restart, scratch); \
                                                                               \
            next_restart += next_offs;                                         \
            if (next_offs < 1024)                                              \
               break;                                                          \
         }                                                                     \
                                                                               \
         /* Emit up to the next restart. Lanes output in parallel */           \
         uint subcount = next_restart - needle;                                \
         uint subprims = u_decomposed_prims_for_vertices(mode, subcount);      \
         uint out_prims_base = out_prims;                                      \
         for (uint i = tid; i < subprims; i += 1024) {                         \
            for (uint vtx = 0; vtx < per_prim; ++vtx) {                        \
               uint id = libagx_vertex_id_for_topology(mode, flatshade_first,  \
                                                       i, vtx, subprims);      \
               uint offset = needle + id;                                      \
                                                                               \
               out[((out_prims_base + i) * per_prim) + vtx] =                  \
                  libagx_load_index_buffer_internal(                           \
                     in_ptr, p->index_buffer_size_el, offset, sizeof(INDEX));  \
            }                                                                  \
         }                                                                     \
                                                                               \
         out_prims += subprims;                                                \
         needle = next_restart + 1;                                            \
      }                                                                        \
                                                                               \
      if (tid == 0)                                                            \
         p->out_draws[(5 * draw) + 0] = out_prims * per_prim;                  \
   }

UNROLL(uchar, u8)
UNROLL(ushort, u16)
UNROLL(uint, u32)

uint
libagx_setup_xfb_buffer(global struct agx_geometry_params *p, uint i)
{
   global uint *off_ptr = p->xfb_offs_ptrs[i];
   if (!off_ptr)
      return 0;

   uint off = *off_ptr;
   p->xfb_base[i] = p->xfb_base_original[i] + off;
   return off;
}

/*
 * Translate EndPrimitive for LINE_STRIP or TRIANGLE_STRIP output prims into
 * writes into the 32-bit output index buffer. We write the sequence (b, b + 1,
 * b + 2, ..., b + n - 1, -1), where b (base) is the first vertex in the prim, n
 * (count) is the number of verts in the prims, and -1 is the prim restart index
 * used to signal the end of the prim.
 *
 * For points, we write index buffers without restart, just as a sideband to
 * pass data into the vertex shader.
 */
void
libagx_end_primitive(global int *index_buffer, uint total_verts,
                     uint verts_in_prim, uint total_prims,
                     uint invocation_vertex_base, uint invocation_prim_base,
                     uint geometry_base, bool restart)
{
   /* Previous verts/prims are from previous invocations plus earlier
    * prims in this invocation. For the intra-invocation counts, we
    * subtract the count for this prim from the inclusive sum NIR gives us.
    */
   uint previous_verts_in_invoc = (total_verts - verts_in_prim);
   uint previous_verts = invocation_vertex_base + previous_verts_in_invoc;
   uint previous_prims = restart ? invocation_prim_base + (total_prims - 1) : 0;

   /* The indices are encoded as: (unrolled ID * output vertices) + vertex. */
   uint index_base = geometry_base + previous_verts_in_invoc;

   /* Index buffer contains 1 index for each vertex and 1 for each prim */
   global int *out = &index_buffer[previous_verts + previous_prims];

   /* Write out indices for the strip */
   for (uint i = 0; i < verts_in_prim; ++i) {
      out[i] = index_base + i;
   }

   if (restart)
      out[verts_in_prim] = -1;
}

void
libagx_build_gs_draw(global struct agx_geometry_params *p, uint vertices,
                     uint primitives)
{
   global uint *descriptor = p->indirect_desc;
   global struct agx_geometry_state *state = p->state;

   /* Setup the indirect draw descriptor */
   uint indices = vertices + primitives; /* includes restart indices */

   /* Allocate the index buffer */
   uint index_buffer_offset_B = state->heap_bottom;
   p->output_index_buffer =
      (global uint *)(state->heap + index_buffer_offset_B);
   state->heap_bottom += (indices * 4);

   descriptor[0] = indices;                   /* count */
   descriptor[1] = 1;                         /* instance count */
   descriptor[2] = index_buffer_offset_B / 4; /* start */
   descriptor[3] = 0;                         /* index bias */
   descriptor[4] = 0;                         /* start instance */

   if (state->heap_bottom > state->heap_size) {
      global uint *foo = (global uint *)(uintptr_t)0xdeadbeef;
      *foo = 0x1234;
   }
}

void
libagx_gs_setup_indirect(global struct agx_gs_setup_indirect_params *gsi,
                         enum mesa_prim mode, uint local_id)
{
   global struct agx_geometry_params *p = gsi->geom;
   global struct agx_ia_state *ia = gsi->ia;

   /* Determine the (primitives, instances) grid size. */
   uint vertex_count = gsi->draw[0];
   uint instance_count = gsi->draw[1];

   ia->verts_per_instance = vertex_count;

   /* Calculate number of primitives input into the GS */
   uint prim_per_instance = u_decomposed_prims_for_vertices(mode, vertex_count);
   p->input_primitives = prim_per_instance * instance_count;

   /* Invoke VS as (vertices, instances); GS as (primitives, instances) */
   p->vs_grid[0] = vertex_count;
   p->vs_grid[1] = instance_count;

   p->gs_grid[0] = prim_per_instance;
   p->gs_grid[1] = instance_count;

   p->primitives_log2 = util_logbase2_ceil(prim_per_instance);

   /* If indexing is enabled, the third word is the offset into the index buffer
    * in elements. Apply that offset now that we have it. For a hardware
    * indirect draw, the hardware would do this for us, but for software input
    * assembly we need to do it ourselves.
    */
   if (gsi->index_size_B) {
      ia->index_buffer =
         libagx_index_buffer(gsi->index_buffer, gsi->index_buffer_range_el,
                             gsi->draw[2], gsi->index_size_B, gsi->zero_sink);

      ia->index_buffer_range_el =
         libagx_index_buffer_range_el(gsi->index_buffer_range_el, gsi->draw[2]);
   }

   /* We need to allocate VS and GS count buffers, do so now */
   global struct agx_geometry_state *state = p->state;

   uint vertex_buffer_size =
      libagx_tcs_in_size(vertex_count * instance_count, gsi->vs_outputs);

   p->count_buffer = (global uint *)(state->heap + state->heap_bottom);
   state->heap_bottom +=
      align(p->input_primitives * p->count_buffer_stride, 16);

   p->input_buffer = (uintptr_t)(state->heap + state->heap_bottom);
   *(gsi->vertex_buffer) = p->input_buffer;
   state->heap_bottom += align(vertex_buffer_size, 4);

   p->input_mask = gsi->vs_outputs;

   if (state->heap_bottom > state->heap_size) {
      global uint *foo = (global uint *)(uintptr_t)0x1deadbeef;
      *foo = 0x1234;
   }
}

/*
 * Returns (work_group_scan_inclusive_add(x), work_group_sum(x)). Implemented
 * manually with subgroup ops and local memory since Mesa doesn't do those
 * lowerings yet.
 */
static uint2
libagx_work_group_scan_inclusive_add(uint x, local uint *scratch)
{
   uint sg_id = get_sub_group_id();

   /* Partial prefix sum of the subgroup */
   uint sg = sub_group_scan_inclusive_add(x);

   /* Reduction (sum) for the subgroup */
   uint sg_sum = sub_group_broadcast(sg, 31);

   /* Write out all the subgroups sums */
   barrier(CLK_LOCAL_MEM_FENCE);
   scratch[sg_id] = sg_sum;
   barrier(CLK_LOCAL_MEM_FENCE);

   /* Read all the subgroup sums. Thread T in subgroup G reads the sum of all
    * threads in subgroup T.
    */
   uint other_sum = scratch[get_sub_group_local_id()];

   /* Exclusive sum the subgroup sums to get the total before the current group,
    * which can be added to the total for the current group.
    */
   uint other_sums = sub_group_scan_exclusive_add(other_sum);
   uint base = sub_group_broadcast(other_sums, sg_id);
   uint prefix = base + sg;

   /* Reduce the workgroup using the prefix sum we already did */
   uint reduction = sub_group_broadcast(other_sums + other_sum, 31);

   return (uint2)(prefix, reduction);
}

kernel void
libagx_prefix_sum(global uint *buffer, uint len, uint words, uint word)
{
   local uint scratch[32];
   uint tid = get_local_id(0);

   /* Main loop: complete workgroups processing 1024 values at once */
   uint i, count = 0;
   uint len_remainder = len % 1024;
   uint len_rounded_down = len - len_remainder;

   for (i = tid; i < len_rounded_down; i += 1024) {
      global uint *ptr = &buffer[(i * words) + word];
      uint value = *ptr;
      uint2 sums = libagx_work_group_scan_inclusive_add(value, scratch);

      *ptr = count + sums[0];
      count += sums[1];
   }

   /* The last iteration is special since we won't have a full subgroup unless
    * the length is divisible by the subgroup size, and we don't advance count.
    */
   global uint *ptr = &buffer[(i * words) + word];
   uint value = (tid < len_remainder) ? *ptr : 0;
   uint scan = libagx_work_group_scan_inclusive_add(value, scratch)[0];

   if (tid < len_remainder) {
      *ptr = count + scan;
   }
}

kernel void
libagx_prefix_sum_tess(global struct libagx_tess_args *p)
{
   libagx_prefix_sum(p->counts, p->nr_patches, 1 /* words */, 0 /* word */);

   /* After prefix summing, we know the total # of indices, so allocate the
    * index buffer now. Elect a thread for the allocation.
    */
   barrier(CLK_LOCAL_MEM_FENCE);
   if (get_local_id(0) != 0)
      return;

   /* The last element of an inclusive prefix sum is the total sum */
   uint total = p->counts[p->nr_patches - 1];

   /* Allocate 4-byte indices */
   uint32_t elsize_B = sizeof(uint32_t);
   uint32_t size_B = total * elsize_B;
   uint alloc_B = p->heap->heap_bottom;
   p->heap->heap_bottom += size_B;
   p->heap->heap_bottom = align(p->heap->heap_bottom, 8);

   p->index_buffer = (global uint32_t *)(((uintptr_t)p->heap->heap) + alloc_B);

   /* ...and now we can generate the API indexed draw */
   global uint32_t *desc = p->out_draws;

   desc[0] = total;              /* count */
   desc[1] = 1;                  /* instance_count */
   desc[2] = alloc_B / elsize_B; /* start */
   desc[3] = 0;                  /* index_bias */
   desc[4] = 0;                  /* start_instance */
}

uintptr_t
libagx_vertex_output_address(uintptr_t buffer, uint64_t mask, uint vtx,
                             gl_varying_slot location)
{
   return buffer + libagx_tcs_in_offs(vtx, location, mask);
}

uintptr_t
libagx_geometry_input_address(constant struct agx_geometry_params *p, uint vtx,
                              gl_varying_slot location)
{
   return libagx_vertex_output_address(p->input_buffer, p->input_mask, vtx,
                                       location);
}

unsigned
libagx_input_vertices(constant struct agx_ia_state *ia)
{
   return ia->verts_per_instance;
}
