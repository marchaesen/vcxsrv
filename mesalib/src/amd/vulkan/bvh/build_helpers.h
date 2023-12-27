/*
 * Copyright © 2022 Konstantin Seurer
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

#ifndef BVH_BUILD_HELPERS_H
#define BVH_BUILD_HELPERS_H

#include "bvh.h"

#define VK_FORMAT_UNDEFINED                  0
#define VK_FORMAT_R4G4_UNORM_PACK8           1
#define VK_FORMAT_R4G4B4A4_UNORM_PACK16      2
#define VK_FORMAT_B4G4R4A4_UNORM_PACK16      3
#define VK_FORMAT_R5G6B5_UNORM_PACK16        4
#define VK_FORMAT_B5G6R5_UNORM_PACK16        5
#define VK_FORMAT_R5G5B5A1_UNORM_PACK16      6
#define VK_FORMAT_B5G5R5A1_UNORM_PACK16      7
#define VK_FORMAT_A1R5G5B5_UNORM_PACK16      8
#define VK_FORMAT_R8_UNORM                   9
#define VK_FORMAT_R8_SNORM                   10
#define VK_FORMAT_R8_USCALED                 11
#define VK_FORMAT_R8_SSCALED                 12
#define VK_FORMAT_R8_UINT                    13
#define VK_FORMAT_R8_SINT                    14
#define VK_FORMAT_R8_SRGB                    15
#define VK_FORMAT_R8G8_UNORM                 16
#define VK_FORMAT_R8G8_SNORM                 17
#define VK_FORMAT_R8G8_USCALED               18
#define VK_FORMAT_R8G8_SSCALED               19
#define VK_FORMAT_R8G8_UINT                  20
#define VK_FORMAT_R8G8_SINT                  21
#define VK_FORMAT_R8G8_SRGB                  22
#define VK_FORMAT_R8G8B8_UNORM               23
#define VK_FORMAT_R8G8B8_SNORM               24
#define VK_FORMAT_R8G8B8_USCALED             25
#define VK_FORMAT_R8G8B8_SSCALED             26
#define VK_FORMAT_R8G8B8_UINT                27
#define VK_FORMAT_R8G8B8_SINT                28
#define VK_FORMAT_R8G8B8_SRGB                29
#define VK_FORMAT_B8G8R8_UNORM               30
#define VK_FORMAT_B8G8R8_SNORM               31
#define VK_FORMAT_B8G8R8_USCALED             32
#define VK_FORMAT_B8G8R8_SSCALED             33
#define VK_FORMAT_B8G8R8_UINT                34
#define VK_FORMAT_B8G8R8_SINT                35
#define VK_FORMAT_B8G8R8_SRGB                36
#define VK_FORMAT_R8G8B8A8_UNORM             37
#define VK_FORMAT_R8G8B8A8_SNORM             38
#define VK_FORMAT_R8G8B8A8_USCALED           39
#define VK_FORMAT_R8G8B8A8_SSCALED           40
#define VK_FORMAT_R8G8B8A8_UINT              41
#define VK_FORMAT_R8G8B8A8_SINT              42
#define VK_FORMAT_R8G8B8A8_SRGB              43
#define VK_FORMAT_B8G8R8A8_UNORM             44
#define VK_FORMAT_B8G8R8A8_SNORM             45
#define VK_FORMAT_B8G8R8A8_USCALED           46
#define VK_FORMAT_B8G8R8A8_SSCALED           47
#define VK_FORMAT_B8G8R8A8_UINT              48
#define VK_FORMAT_B8G8R8A8_SINT              49
#define VK_FORMAT_B8G8R8A8_SRGB              50
#define VK_FORMAT_A8B8G8R8_UNORM_PACK32      51
#define VK_FORMAT_A8B8G8R8_SNORM_PACK32      52
#define VK_FORMAT_A8B8G8R8_USCALED_PACK32    53
#define VK_FORMAT_A8B8G8R8_SSCALED_PACK32    54
#define VK_FORMAT_A8B8G8R8_UINT_PACK32       55
#define VK_FORMAT_A8B8G8R8_SINT_PACK32       56
#define VK_FORMAT_A8B8G8R8_SRGB_PACK32       57
#define VK_FORMAT_A2R10G10B10_UNORM_PACK32   58
#define VK_FORMAT_A2R10G10B10_SNORM_PACK32   59
#define VK_FORMAT_A2R10G10B10_USCALED_PACK32 60
#define VK_FORMAT_A2R10G10B10_SSCALED_PACK32 61
#define VK_FORMAT_A2R10G10B10_UINT_PACK32    62
#define VK_FORMAT_A2R10G10B10_SINT_PACK32    63
#define VK_FORMAT_A2B10G10R10_UNORM_PACK32   64
#define VK_FORMAT_A2B10G10R10_SNORM_PACK32   65
#define VK_FORMAT_A2B10G10R10_USCALED_PACK32 66
#define VK_FORMAT_A2B10G10R10_SSCALED_PACK32 67
#define VK_FORMAT_A2B10G10R10_UINT_PACK32    68
#define VK_FORMAT_A2B10G10R10_SINT_PACK32    69
#define VK_FORMAT_R16_UNORM                  70
#define VK_FORMAT_R16_SNORM                  71
#define VK_FORMAT_R16_USCALED                72
#define VK_FORMAT_R16_SSCALED                73
#define VK_FORMAT_R16_UINT                   74
#define VK_FORMAT_R16_SINT                   75
#define VK_FORMAT_R16_SFLOAT                 76
#define VK_FORMAT_R16G16_UNORM               77
#define VK_FORMAT_R16G16_SNORM               78
#define VK_FORMAT_R16G16_USCALED             79
#define VK_FORMAT_R16G16_SSCALED             80
#define VK_FORMAT_R16G16_UINT                81
#define VK_FORMAT_R16G16_SINT                82
#define VK_FORMAT_R16G16_SFLOAT              83
#define VK_FORMAT_R16G16B16_UNORM            84
#define VK_FORMAT_R16G16B16_SNORM            85
#define VK_FORMAT_R16G16B16_USCALED          86
#define VK_FORMAT_R16G16B16_SSCALED          87
#define VK_FORMAT_R16G16B16_UINT             88
#define VK_FORMAT_R16G16B16_SINT             89
#define VK_FORMAT_R16G16B16_SFLOAT           90
#define VK_FORMAT_R16G16B16A16_UNORM         91
#define VK_FORMAT_R16G16B16A16_SNORM         92
#define VK_FORMAT_R16G16B16A16_USCALED       93
#define VK_FORMAT_R16G16B16A16_SSCALED       94
#define VK_FORMAT_R16G16B16A16_UINT          95
#define VK_FORMAT_R16G16B16A16_SINT          96
#define VK_FORMAT_R16G16B16A16_SFLOAT        97
#define VK_FORMAT_R32_UINT                   98
#define VK_FORMAT_R32_SINT                   99
#define VK_FORMAT_R32_SFLOAT                 100
#define VK_FORMAT_R32G32_UINT                101
#define VK_FORMAT_R32G32_SINT                102
#define VK_FORMAT_R32G32_SFLOAT              103
#define VK_FORMAT_R32G32B32_UINT             104
#define VK_FORMAT_R32G32B32_SINT             105
#define VK_FORMAT_R32G32B32_SFLOAT           106
#define VK_FORMAT_R32G32B32A32_UINT          107
#define VK_FORMAT_R32G32B32A32_SINT          108
#define VK_FORMAT_R32G32B32A32_SFLOAT        109
#define VK_FORMAT_R64_UINT                   110
#define VK_FORMAT_R64_SINT                   111
#define VK_FORMAT_R64_SFLOAT                 112
#define VK_FORMAT_R64G64_UINT                113
#define VK_FORMAT_R64G64_SINT                114
#define VK_FORMAT_R64G64_SFLOAT              115
#define VK_FORMAT_R64G64B64_UINT             116
#define VK_FORMAT_R64G64B64_SINT             117
#define VK_FORMAT_R64G64B64_SFLOAT           118
#define VK_FORMAT_R64G64B64A64_UINT          119
#define VK_FORMAT_R64G64B64A64_SINT          120
#define VK_FORMAT_R64G64B64A64_SFLOAT        121

#define VK_INDEX_TYPE_UINT16    0
#define VK_INDEX_TYPE_UINT32    1
#define VK_INDEX_TYPE_NONE_KHR  1000165000
#define VK_INDEX_TYPE_UINT8_EXT 1000265000

#define VK_GEOMETRY_TYPE_TRIANGLES_KHR 0
#define VK_GEOMETRY_TYPE_AABBS_KHR     1

#define VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR 1
#define VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR         2
#define VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR                 4
#define VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR              8

#define TYPE(type, align)                                                                                              \
   layout(buffer_reference, buffer_reference_align = align, scalar) buffer type##_ref                                  \
   {                                                                                                                   \
      type value;                                                                                                      \
   };

#define REF(type)  type##_ref
#define VOID_REF   uint64_t
#define NULL       0
#define DEREF(var) var.value

#define SIZEOF(type) uint32_t(uint64_t(REF(type)(uint64_t(0)) + 1))

#define OFFSET(ptr, offset) (uint64_t(ptr) + offset)

#define INFINITY (1.0 / 0.0)
#define NAN      (0.0 / 0.0)

#define INDEX(type, ptr, index) REF(type)(OFFSET(ptr, (index)*SIZEOF(type)))

TYPE(int8_t, 1);
TYPE(uint8_t, 1);
TYPE(int16_t, 2);
TYPE(uint16_t, 2);
TYPE(int32_t, 4);
TYPE(uint32_t, 4);
TYPE(int64_t, 8);
TYPE(uint64_t, 8);

TYPE(float, 4);

TYPE(vec2, 4);
TYPE(vec3, 4);
TYPE(vec4, 4);

TYPE(uvec4, 16);

TYPE(VOID_REF, 8);

/* copied from u_math.h */
uint32_t
align(uint32_t value, uint32_t alignment)
{
   return (value + alignment - 1) & ~(alignment - 1);
}

int32_t
to_emulated_float(float f)
{
   int32_t bits = floatBitsToInt(f);
   return f < 0 ? -2147483648 - bits : bits;
}

float
from_emulated_float(int32_t bits)
{
   return intBitsToFloat(bits < 0 ? -2147483648 - bits : bits);
}

TYPE(radv_aabb, 4);

struct key_id_pair {
   uint32_t id;
   uint32_t key;
};
TYPE(key_id_pair, 4);

TYPE(radv_accel_struct_serialization_header, 8);
TYPE(radv_accel_struct_header, 8);
TYPE(radv_bvh_triangle_node, 4);
TYPE(radv_bvh_aabb_node, 4);
TYPE(radv_bvh_instance_node, 8);
TYPE(radv_bvh_box16_node, 4);
TYPE(radv_bvh_box32_node, 4);

TYPE(radv_ir_header, 4);
TYPE(radv_ir_node, 4);
TYPE(radv_ir_box_node, 4);

TYPE(radv_global_sync_data, 4);

uint32_t
id_to_offset(uint32_t id)
{
   return (id & (~7u)) << 3;
}

uint32_t
id_to_type(uint32_t id)
{
   return id & 7u;
}

uint32_t
pack_node_id(uint32_t offset, uint32_t type)
{
   return (offset >> 3) | type;
}

uint64_t
node_to_addr(uint64_t node)
{
   node &= ~7ul;
   node <<= 19;
   return int64_t(node) >> 16;
}

uint64_t
addr_to_node(uint64_t addr)
{
   return (addr >> 3) & ((1ul << 45) - 1);
}

uint32_t
ir_id_to_offset(uint32_t id)
{
   return id & (~3u);
}

uint32_t
ir_id_to_type(uint32_t id)
{
   return id & 3u;
}

uint32_t
pack_ir_node_id(uint32_t offset, uint32_t type)
{
   return offset | type;
}

uint32_t
ir_type_to_bvh_type(uint32_t type)
{
   switch (type) {
   case radv_ir_node_triangle:
      return radv_bvh_node_triangle;
   case radv_ir_node_internal:
      return radv_bvh_node_box32;
   case radv_ir_node_instance:
      return radv_bvh_node_instance;
   case radv_ir_node_aabb:
      return radv_bvh_node_aabb;
   }
   /* unreachable in valid nodes */
   return RADV_BVH_INVALID_NODE;
}

float
aabb_surface_area(radv_aabb aabb)
{
   vec3 diagonal = aabb.max - aabb.min;
   return 2 * diagonal.x * diagonal.y + 2 * diagonal.y * diagonal.z + 2 * diagonal.x * diagonal.z;
}

/** Compute ceiling of integer quotient of A divided by B.
    From macros.h */
#define DIV_ROUND_UP(A, B) (((A) + (B)-1) / (B))

#ifdef USE_GLOBAL_SYNC

/* There might be more invocations available than tasks to do.
 * In that case, the fetched task index is greater than the
 * counter offset for the next phase. To avoid out-of-bounds
 * accessing, phases will be skipped until the task index is
 * is in-bounds again. */
uint32_t num_tasks_to_skip = 0;
uint32_t phase_index = 0;
bool should_skip = false;
shared uint32_t global_task_index;

shared uint32_t shared_phase_index;

uint32_t
task_count(REF(radv_ir_header) header)
{
   uint32_t phase_index = DEREF(header).sync_data.phase_index;
   return DEREF(header).sync_data.task_counts[phase_index & 1];
}

/* Sets the task count for the next phase. */
void
set_next_task_count(REF(radv_ir_header) header, uint32_t new_count)
{
   uint32_t phase_index = DEREF(header).sync_data.phase_index;
   DEREF(header).sync_data.task_counts[(phase_index + 1) & 1] = new_count;
}

/*
 * This function has two main objectives:
 * Firstly, it partitions pending work among free invocations.
 * Secondly, it guarantees global synchronization between different phases.
 *
 * After every call to fetch_task, a new task index is returned.
 * fetch_task will also set num_tasks_to_skip. Use should_execute_phase
 * to determine if the current phase should be executed or skipped.
 *
 * Since tasks are assigned per-workgroup, there is a possibility of the task index being
 * greater than the total task count.
 */
uint32_t
fetch_task(REF(radv_ir_header) header, bool did_work)
{
   /* Perform a memory + control barrier for all buffer writes for the entire workgroup.
    * This guarantees that once the workgroup leaves the PHASE loop, all invocations have finished
    * and their results are written to memory. */
   controlBarrier(gl_ScopeWorkgroup, gl_ScopeDevice, gl_StorageSemanticsBuffer,
                  gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
   if (gl_LocalInvocationIndex == 0) {
      if (did_work)
         atomicAdd(DEREF(header).sync_data.task_done_counter, 1);
      global_task_index = atomicAdd(DEREF(header).sync_data.task_started_counter, 1);

      do {
         /* Perform a memory barrier to refresh the current phase's end counter, in case
          * another workgroup changed it. */
         memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                       gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);

         /* The first invocation of the first workgroup in a new phase is responsible to initiate the
          * switch to a new phase. It is only possible to switch to a new phase if all tasks of the
          * previous phase have been completed. Switching to a new phase and incrementing the phase
          * end counter in turn notifies all invocations for that phase that it is safe to execute.
          */
         if (global_task_index == DEREF(header).sync_data.current_phase_end_counter &&
             DEREF(header).sync_data.task_done_counter == DEREF(header).sync_data.current_phase_end_counter) {
            if (DEREF(header).sync_data.next_phase_exit_flag != 0) {
               DEREF(header).sync_data.phase_index = TASK_INDEX_INVALID;
               memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                             gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
            } else {
               atomicAdd(DEREF(header).sync_data.phase_index, 1);
               DEREF(header).sync_data.current_phase_start_counter = DEREF(header).sync_data.current_phase_end_counter;
               /* Ensure the changes to the phase index and start/end counter are visible for other
                * workgroup waiting in the loop. */
               memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                             gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
               atomicAdd(DEREF(header).sync_data.current_phase_end_counter,
                         DIV_ROUND_UP(task_count(header), gl_WorkGroupSize.x));
            }
            break;
         }

         /* If other invocations have finished all nodes, break out; there is no work to do */
         if (DEREF(header).sync_data.phase_index == TASK_INDEX_INVALID) {
            break;
         }
      } while (global_task_index >= DEREF(header).sync_data.current_phase_end_counter);

      shared_phase_index = DEREF(header).sync_data.phase_index;
   }

   barrier();
   if (DEREF(header).sync_data.phase_index == TASK_INDEX_INVALID)
      return TASK_INDEX_INVALID;

   num_tasks_to_skip = shared_phase_index - phase_index;

   uint32_t local_task_index = global_task_index - DEREF(header).sync_data.current_phase_start_counter;
   return local_task_index * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
}

bool
should_execute_phase()
{
   if (num_tasks_to_skip > 0) {
      /* Skip to next phase. */
      ++phase_index;
      --num_tasks_to_skip;
      return false;
   }
   return true;
}

#define PHASE(header)                                                                                                  \
   for (; task_index != TASK_INDEX_INVALID && should_execute_phase(); task_index = fetch_task(header, true))
#endif

#endif
