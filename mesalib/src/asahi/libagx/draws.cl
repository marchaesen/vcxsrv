/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "asahi/lib/agx_abi.h"
#include "compiler/libcl/libcl_vk.h"
#include "agx_pack.h"
#include "geometry.h"
#include "libagx_dgc.h"

/*
 * To implement drawIndirectCount generically, we dispatch a kernel to
 * clone-and-patch the indirect buffer, predicating out draws as appropriate.
 */
KERNEL(32)
libagx_predicate_indirect(global uint32_t *out, constant uint32_t *in,
                          constant uint32_t *draw_count, uint32_t stride_el,
                          uint indexed__2)
{
   uint draw = cl_global_id.x;
   uint words = indexed__2 ? 5 : 4;
   bool enabled = draw < *draw_count;
   out += draw * words;
   in += draw * stride_el;

   /* Copy enabled draws, zero predicated draws. */
   for (uint i = 0; i < words; ++i) {
      out[i] = enabled ? in[i] : 0;
   }
}

/*
 * Indexing/offseting is in software if necessary so we strip all
 * indexing/offset information.
 */
KERNEL(1)
libagx_draw_without_adj(global VkDrawIndirectCommand *out,
                        global VkDrawIndirectCommand *in,
                        global struct agx_ia_state *ia, uint64_t index_buffer,
                        uint64_t index_buffer_range_el, int index_size_B,
                        enum mesa_prim prim)
{
   *out = (VkDrawIndirectCommand){
      .vertexCount = libagx_remap_adj_count(in->vertexCount, prim),
      .instanceCount = in->instanceCount,
   };

   /* TODO: Deduplicate */
   if (index_size_B) {
      uint offs = in->firstVertex;

      ia->index_buffer = libagx_index_buffer(
         index_buffer, index_buffer_range_el, offs, index_size_B);

      ia->index_buffer_range_el =
         libagx_index_buffer_range_el(index_buffer_range_el, offs);
   }
}

/* Precondition: len must be < the group size */
static void
libagx_memcpy_small(global uchar *dst, constant uchar *src, uint len, uint tid)
{
   if (tid < len) {
      dst[tid] = src[tid];
   }
}

static void
libagx_memcpy_aligned_uint4(global uint *dst, constant uint *src, uint len,
                            uint tid, uint group_size)
{
   for (uint i = tid; i < len; i += group_size) {
      vstore4(vload4(i, src), i, dst);
   }
}

static void
libagx_memcpy_to_aligned(global uint *dst, constant uchar *src, uint len,
                         uint tid, uint group_size)
{
   /* Copy a few bytes at the start */
   uint start_unaligned = ((uintptr_t)src) & 3;
   if (start_unaligned) {
      uint need = 4 - start_unaligned;
      libagx_memcpy_small((global uchar *)dst, src, need, tid);
      src += need;
      len -= need;
   }

   /* Copy a few bytes at the end */
   uint end_unaligned = len & 0xf;
   len -= end_unaligned;
   libagx_memcpy_small(((global uchar *)dst) + len, src + len, end_unaligned,
                       tid);

   /* Now both src and dst are word-aligned, and len is 16-aligned */
   libagx_memcpy_aligned_uint4(dst, (constant uint *)src, len / 16, tid,
                               group_size);
}

/* Precondition: len must be < the group size */
static void
libagx_memset_small(global uchar *dst, uchar b, int len, uint tid)
{
   if (tid < len) {
      dst[tid] = b;
   }
}

/*
 * AGX does not implement robustBufferAccess2 semantics for
 * index buffers, where out-of-bounds indices read as zero. When we
 * dynamically detect index buffer overread (this if-statement), we need
 * to clone the index buffer and zero-extend it to get robustness.
 *
 * We do this dynamically (generating a VDM draw to consume the result) to avoid
 * expensive allocations & memcpys in the happy path where no out-of-bounds
 * access occurs. Otherwise we could use a hardware indirect draw, rather than
 * generating VDM words directly in shader.
 *
 * TODO: Handle multiple draws in parallel.
 */
KERNEL(32)
libagx_draw_robust_index(global uint32_t *vdm,
                         global struct agx_geometry_state *heap,
                         constant VkDrawIndexedIndirectCommand *cmd,
                         uint64_t in_buf_ptr, uint32_t in_buf_range_B,
                         ushort restart, enum agx_primitive topology,
                         enum agx_index_size index_size__3)
{
   uint tid = get_sub_group_local_id();
   bool first = tid == 0;
   enum agx_index_size index_size = index_size__3;

   struct agx_draw draw = agx_draw_indexed(
      cmd->indexCount, cmd->instanceCount, cmd->firstIndex, cmd->vertexOffset,
      cmd->firstInstance, in_buf_ptr, in_buf_range_B, index_size, restart);

   if (agx_indices_to_B(cmd->firstIndex, index_size) >= in_buf_range_B) {
      /* If the entire draw is out-of-bounds, skip it. We handle this specially
       * for both performance and to avoid integer wrapping issues in the main
       * code path (where cmd->firstIndex could get treated as a negative).
       */
      draw.index_buffer = AGX_ZERO_PAGE_ADDRESS;
      draw.index_buffer_range_B = 4;
      draw.start = 0;
   } else if (agx_direct_draw_overreads_indices(draw)) {
      constant void *in_buf = (constant void *)agx_draw_index_buffer(draw);
      uint in_size_el = agx_draw_index_range_el(draw);
      uint in_size_B = agx_indices_to_B(in_size_el, index_size);

      /* After a small number of zeroes at the end, extra zeroes cannot change
       * rendering since they will duplicate the same degenerate primitive many
       * times. Therefore we clamp the number of zeroes we need to extend with.
       * This makes the memset constant time.
       */
      draw.b.count[0] = min(draw.b.count[0], in_size_el + 32);

      uint out_size_el = draw.b.count[0];
      uint out_size_B = agx_indices_to_B(out_size_el, index_size);

      /* Allocate memory for the shadow index buffer */
      global uchar *padded;
      if (first) {
         padded = agx_heap_alloc_nonatomic(heap, out_size_B);
      }
      padded = (global uchar *)sub_group_broadcast((uintptr_t)padded, 0);

      draw.index_buffer = (uintptr_t)padded;
      draw.index_buffer_range_B = out_size_B;
      draw.start = 0;

      /* Clone the index buffer. The destination is aligned as a post-condition
       * of agx_heap_alloc_nonatomic.
       */
      libagx_memcpy_to_aligned((global uint *)padded, in_buf, in_size_B, tid,
                               32);

      /* Extend with up to 32 zeroes with a small memset */
      libagx_memset_small(padded + in_size_B, 0, out_size_B - in_size_B, tid);
   }

   if (first) {
      agx_vdm_draw(vdm, 0, draw, topology);
   }
}
