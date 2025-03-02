/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "ac_gpu_info.h"

#include "nir_builder.h"

#define SPECIAL_MS_OUT_MASK \
   (BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_COUNT) | \
    BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES) | \
    BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE))

#define MS_PRIM_ARG_EXP_MASK \
   (VARYING_BIT_LAYER | \
    VARYING_BIT_VIEWPORT | \
    VARYING_BIT_PRIMITIVE_SHADING_RATE)

#define MS_VERT_ARG_EXP_MASK \
   (VARYING_BIT_CULL_DIST0 | \
    VARYING_BIT_CULL_DIST1 | \
    VARYING_BIT_CLIP_DIST0 | \
    VARYING_BIT_CLIP_DIST1 | \
    VARYING_BIT_PSIZ)

/* LDS layout of Mesh Shader workgroup info. */
enum {
   /* DW0: number of primitives */
   lds_ms_num_prims = 0,
   /* DW1: number of vertices */
   lds_ms_num_vtx = 4,
   /* DW2: workgroup index within the current dispatch */
   lds_ms_wg_index = 8,
   /* DW3: number of API workgroups in flight */
   lds_ms_num_api_waves = 12,
};

/* Potential location for Mesh Shader outputs. */
typedef enum {
   ms_out_mode_lds,
   ms_out_mode_scratch_ring,
   ms_out_mode_attr_ring,
   ms_out_mode_var,
} ms_out_mode;

typedef struct
{
   uint64_t mask; /* Mask of output locations */
   uint32_t addr; /* Base address */
} ms_out_part;

typedef struct
{
   /* Mesh shader LDS layout. For details, see ms_calculate_output_layout. */
   struct {
      uint32_t workgroup_info_addr;
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
      uint32_t indices_addr;
      uint32_t cull_flags_addr;
      uint32_t total_size;
   } lds;

   /* VRAM "mesh shader scratch ring" layout for outputs that don't fit into the LDS.
    * Not to be confused with scratch memory.
    */
   struct {
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
   } scratch_ring;

   /* VRAM attributes ring (supported GPUs only) for all non-position outputs.
    * We don't have to reload attributes from this ring at the end of the shader.
    */
   struct {
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
   } attr_ring;

   /* Outputs without cross-invocation access can be stored in variables. */
   struct {
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
   } var;
} ms_out_mem_layout;

typedef struct
{
   const struct radeon_info *hw_info;
   bool fast_launch_2;
   bool vert_multirow_export;
   bool prim_multirow_export;

   ms_out_mem_layout layout;
   uint64_t per_vertex_outputs;
   uint64_t per_primitive_outputs;
   unsigned vertices_per_prim;

   unsigned wave_size;
   unsigned api_workgroup_size;
   unsigned hw_workgroup_size;

   nir_def *workgroup_index;
   nir_variable *out_variables[VARYING_SLOT_MAX * 4];
   nir_variable *primitive_count_var;
   nir_variable *vertex_count_var;

   ac_nir_prerast_out out;

   /* True if the lowering needs to insert the layer output. */
   bool insert_layer_output;
   /* True if cull flags are used */
   bool uses_cull_flags;

   uint32_t clipdist_enable_mask;
   const uint8_t *vs_output_param_offset;
   bool has_param_exports;

   /* True if the lowering needs to insert shader query. */
   bool has_query;
} lower_ngg_ms_state;

static void
ms_store_prim_indices(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      lower_ngg_ms_state *s)
{
   /* EXT_mesh_shader primitive indices: array of vectors.
    * They don't count as per-primitive outputs, but the array is indexed
    * by the primitive index, so they are practically per-primitive.
    */
   assert(nir_src_is_const(*nir_get_io_offset_src(intrin)));
   assert(nir_src_as_uint(*nir_get_io_offset_src(intrin)) == 0);

   const unsigned component_offset = nir_intrinsic_component(intrin);
   nir_def *store_val = intrin->src[0].ssa;
   assert(store_val->num_components <= 3);

   if (store_val->num_components > s->vertices_per_prim)
      store_val = nir_trim_vector(b, store_val, s->vertices_per_prim);

   if (s->layout.var.prm_attr.mask & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES)) {
      for (unsigned c = 0; c < store_val->num_components; ++c) {
         const unsigned i = VARYING_SLOT_PRIMITIVE_INDICES * 4 + c + component_offset;
         nir_store_var(b, s->out_variables[i], nir_channel(b, store_val, c), 0x1);
      }
      return;
   }

   nir_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
   nir_def *offset = nir_imul_imm(b, arr_index, s->vertices_per_prim);

   /* The max vertex count is 256, so these indices always fit 8 bits.
    * To reduce LDS use, store these as a flat array of 8-bit values.
    */
   nir_store_shared(b, nir_u2u8(b, store_val), offset, .base = s->layout.lds.indices_addr + component_offset);
}

static void
ms_store_cull_flag(nir_builder *b,
                   nir_intrinsic_instr *intrin,
                   lower_ngg_ms_state *s)
{
   /* EXT_mesh_shader cull primitive: per-primitive bool. */
   assert(nir_src_is_const(*nir_get_io_offset_src(intrin)));
   assert(nir_src_as_uint(*nir_get_io_offset_src(intrin)) == 0);
   assert(nir_intrinsic_component(intrin) == 0);
   assert(nir_intrinsic_write_mask(intrin) == 1);

   nir_def *store_val = intrin->src[0].ssa;

   assert(store_val->num_components == 1);
   assert(store_val->bit_size == 1);

   if (s->layout.var.prm_attr.mask & BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE)) {
      nir_store_var(b, s->out_variables[VARYING_SLOT_CULL_PRIMITIVE * 4], nir_b2i32(b, store_val), 0x1);
      return;
   }

   nir_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
   nir_def *offset = nir_imul_imm(b, arr_index, s->vertices_per_prim);

   /* To reduce LDS use, store these as an array of 8-bit values. */
   nir_store_shared(b, nir_b2i8(b, store_val), offset, .base = s->layout.lds.cull_flags_addr);
}

static nir_def *
ms_arrayed_output_base_addr(nir_builder *b,
                            nir_def *arr_index,
                            unsigned mapped_location,
                            unsigned num_arrayed_outputs)
{
   /* Address offset of the array item (vertex or primitive). */
   unsigned arr_index_stride = num_arrayed_outputs * 16u;
   nir_def *arr_index_off = nir_imul_imm(b, arr_index, arr_index_stride);

   /* IO address offset within the vertex or primitive data. */
   unsigned io_offset = mapped_location * 16u;
   nir_def *io_off = nir_imm_int(b, io_offset);

   return nir_iadd_nuw(b, arr_index_off, io_off);
}

static void
update_ms_output_info(const nir_io_semantics io_sem,
                      const nir_src *base_offset_src,
                      const uint32_t write_mask,
                      const unsigned component_offset,
                      const unsigned bit_size,
                      const ms_out_part *out,
                      lower_ngg_ms_state *s)
{
   const uint32_t components_mask = write_mask << component_offset;

   /* 64-bit outputs should have already been lowered to 32-bit. */
   assert(bit_size <= 32);
   assert(components_mask <= 0xf);

   /* When the base offset is constant, only mark the components of the current slot as used.
    * Otherwise, mark the components of all possibly affected slots as used.
    */
   const unsigned base_off_start = nir_src_is_const(*base_offset_src) ? nir_src_as_uint(*base_offset_src) : 0;
   const unsigned num_slots = nir_src_is_const(*base_offset_src) ? 1 : io_sem.num_slots;

   for (unsigned base_off = base_off_start; base_off < num_slots; ++base_off) {
      ac_nir_prerast_per_output_info *info = &s->out.infos[io_sem.location + base_off];
      info->components_mask |= components_mask;

      if (!io_sem.no_sysval_output)
         info->as_sysval_mask |= components_mask;
      if (!io_sem.no_varying)
         info->as_varying_mask |= components_mask;
   }
}

static const ms_out_part *
ms_get_out_layout_part(unsigned location,
                       shader_info *info,
                       ms_out_mode *out_mode,
                       lower_ngg_ms_state *s)
{
   uint64_t mask = BITFIELD64_BIT(location);

   if (info->per_primitive_outputs & mask) {
      if (mask & s->layout.lds.prm_attr.mask) {
         *out_mode = ms_out_mode_lds;
         return &s->layout.lds.prm_attr;
      } else if (mask & s->layout.scratch_ring.prm_attr.mask) {
         *out_mode = ms_out_mode_scratch_ring;
         return &s->layout.scratch_ring.prm_attr;
      } else if (mask & s->layout.attr_ring.prm_attr.mask) {
         *out_mode = ms_out_mode_attr_ring;
         return &s->layout.attr_ring.prm_attr;
      } else if (mask & s->layout.var.prm_attr.mask) {
         *out_mode = ms_out_mode_var;
         return &s->layout.var.prm_attr;
      }
   } else {
      if (mask & s->layout.lds.vtx_attr.mask) {
         *out_mode = ms_out_mode_lds;
         return &s->layout.lds.vtx_attr;
      } else if (mask & s->layout.scratch_ring.vtx_attr.mask) {
         *out_mode = ms_out_mode_scratch_ring;
         return &s->layout.scratch_ring.vtx_attr;
      } else if (mask & s->layout.attr_ring.vtx_attr.mask) {
         *out_mode = ms_out_mode_attr_ring;
         return &s->layout.attr_ring.vtx_attr;
      } else if (mask & s->layout.var.vtx_attr.mask) {
         *out_mode = ms_out_mode_var;
         return &s->layout.var.vtx_attr;
      }
   }

   unreachable("Couldn't figure out mesh shader output mode.");
}

static void
ms_store_arrayed_output(nir_builder *b,
                        nir_src *base_off_src,
                        nir_def *store_val,
                        nir_def *arr_index,
                        const nir_io_semantics io_sem,
                        const unsigned component_offset,
                        const unsigned write_mask,
                        lower_ngg_ms_state *s)
{
   ms_out_mode out_mode;
   const ms_out_part *out = ms_get_out_layout_part(io_sem.location, &b->shader->info, &out_mode, s);
   update_ms_output_info(io_sem, base_off_src, write_mask, component_offset, store_val->bit_size, out, s);

   bool hi_16b = io_sem.high_16bits;
   bool lo_16b = !hi_16b && store_val->bit_size == 16;

   unsigned mapped_location = util_bitcount64(out->mask & u_bit_consecutive64(0, io_sem.location));
   unsigned num_outputs = util_bitcount64(out->mask);
   unsigned const_off = out->addr + component_offset * 4 + (hi_16b ? 2 : 0);

   nir_def *base_addr = ms_arrayed_output_base_addr(b, arr_index, mapped_location, num_outputs);
   nir_def *base_offset = base_off_src->ssa;
   nir_def *base_addr_off = nir_imul_imm(b, base_offset, 16u);
   nir_def *addr = nir_iadd_nuw(b, base_addr, base_addr_off);

   if (out_mode == ms_out_mode_lds) {
      nir_store_shared(b, store_val, addr, .base = const_off,
                     .write_mask = write_mask, .align_mul = 16,
                     .align_offset = const_off % 16);
   } else if (out_mode == ms_out_mode_scratch_ring) {
      nir_def *ring = nir_load_ring_mesh_scratch_amd(b);
      nir_def *off = nir_load_ring_mesh_scratch_offset_amd(b);
      nir_def *zero = nir_imm_int(b, 0);
      nir_store_buffer_amd(b, store_val, ring, addr, off, zero,
                           .base = const_off,
                           .write_mask = write_mask,
                           .memory_modes = nir_var_shader_out,
                           .access = ACCESS_COHERENT);
   } else if (out_mode == ms_out_mode_attr_ring) {
      /* Store params straight to the attribute ring.
       * Even though the access pattern may not be the most optimal,
       * this is still much better than reserving LDS and losing waves.
       * (Also much better than storing and reloading from the scratch ring.)
       */
      unsigned param_offset = s->vs_output_param_offset[io_sem.location];
      nir_def *ring = nir_load_ring_attr_amd(b);
      nir_def *soffset = nir_load_ring_attr_offset_amd(b);
      nir_store_buffer_amd(b, store_val, ring, base_addr_off, soffset, arr_index,
                           .base = const_off + param_offset * 16,
                           .write_mask = write_mask,
                           .memory_modes = nir_var_shader_out,
                           .access = ACCESS_COHERENT | ACCESS_IS_SWIZZLED_AMD,
                           .align_mul = 16, .align_offset = const_off % 16u);
   } else if (out_mode == ms_out_mode_var) {
      unsigned write_mask_32 = write_mask;
      if (store_val->bit_size > 32) {
         /* Split 64-bit store values to 32-bit components. */
         store_val = nir_bitcast_vector(b, store_val, 32);
         /* Widen the write mask so it is in 32-bit components. */
         write_mask_32 = util_widen_mask(write_mask, store_val->bit_size / 32);
      }

      u_foreach_bit(comp, write_mask_32) {
         unsigned idx = io_sem.location * 4 + comp + component_offset;
         nir_def *val = nir_channel(b, store_val, comp);
         nir_def *v = nir_load_var(b, s->out_variables[idx]);

         if (lo_16b) {
            nir_def *var_hi = nir_unpack_32_2x16_split_y(b, v);
            val = nir_pack_32_2x16_split(b, val, var_hi);
         } else if (hi_16b) {
            nir_def *var_lo = nir_unpack_32_2x16_split_x(b, v);
            val = nir_pack_32_2x16_split(b, var_lo, val);
         }

         nir_store_var(b, s->out_variables[idx], val, 0x1);
      }
   } else {
      unreachable("Invalid MS output mode for store");
   }
}

static void
ms_store_arrayed_output_intrin(nir_builder *b,
                               nir_intrinsic_instr *intrin,
                               lower_ngg_ms_state *s)
{
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   if (io_sem.location == VARYING_SLOT_PRIMITIVE_INDICES) {
      ms_store_prim_indices(b, intrin, s);
      return;
   } else if (io_sem.location == VARYING_SLOT_CULL_PRIMITIVE) {
      ms_store_cull_flag(b, intrin, s);
      return;
   }

   unsigned component_offset = nir_intrinsic_component(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);

   nir_def *store_val = intrin->src[0].ssa;
   nir_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
   nir_src *base_off_src = nir_get_io_offset_src(intrin);

   if (store_val->bit_size < 32) {
      /* Split 16-bit output stores to ensure each 16-bit component is stored
       * in the correct location, without overwriting the other 16 bits there.
       */
      u_foreach_bit(c, write_mask) {
         nir_def *store_component = nir_channel(b, store_val, c);
         ms_store_arrayed_output(b, base_off_src, store_component, arr_index, io_sem, c + component_offset, 1, s);
      }
   } else {
      ms_store_arrayed_output(b, base_off_src, store_val, arr_index, io_sem, component_offset, write_mask, s);
   }
}

static nir_def *
ms_load_arrayed_output(nir_builder *b,
                       nir_def *arr_index,
                       nir_def *base_offset,
                       unsigned location,
                       unsigned component_offset,
                       unsigned num_components,
                       unsigned load_bit_size,
                       lower_ngg_ms_state *s)
{
   ms_out_mode out_mode;
   const ms_out_part *out = ms_get_out_layout_part(location, &b->shader->info, &out_mode, s);

   unsigned component_addr_off = component_offset * 4;
   unsigned num_outputs = util_bitcount64(out->mask);
   unsigned const_off = out->addr + component_offset * 4;

   /* Use compacted location instead of the original semantic location. */
   unsigned mapped_location = util_bitcount64(out->mask & u_bit_consecutive64(0, location));

   nir_def *base_addr = ms_arrayed_output_base_addr(b, arr_index, mapped_location, num_outputs);
   nir_def *base_addr_off = nir_imul_imm(b, base_offset, 16);
   nir_def *addr = nir_iadd_nuw(b, base_addr, base_addr_off);

   if (out_mode == ms_out_mode_lds) {
      return nir_load_shared(b, num_components, load_bit_size, addr, .align_mul = 16,
                             .align_offset = component_addr_off % 16,
                             .base = const_off);
   } else if (out_mode == ms_out_mode_scratch_ring) {
      nir_def *ring = nir_load_ring_mesh_scratch_amd(b);
      nir_def *off = nir_load_ring_mesh_scratch_offset_amd(b);
      nir_def *zero = nir_imm_int(b, 0);
      return nir_load_buffer_amd(b, num_components, load_bit_size, ring, addr, off, zero,
                                 .base = const_off,
                                 .memory_modes = nir_var_shader_out,
                                 .access = ACCESS_COHERENT);
   } else if (out_mode == ms_out_mode_var) {
      assert(load_bit_size == 32);
      nir_def *arr[8] = {0};
      for (unsigned comp = 0; comp < num_components; ++comp) {
         unsigned idx = location * 4 + comp + component_addr_off;
         arr[comp] = nir_load_var(b, s->out_variables[idx]);
      }
      return nir_vec(b, arr, num_components);
   } else {
      unreachable("Invalid MS output mode for load");
   }
}

static nir_def *
lower_ms_load_workgroup_index(nir_builder *b,
                              UNUSED nir_intrinsic_instr *intrin,
                              lower_ngg_ms_state *s)
{
   return s->workgroup_index;
}

static nir_def *
lower_ms_set_vertex_and_primitive_count(nir_builder *b,
                                        nir_intrinsic_instr *intrin,
                                        lower_ngg_ms_state *s)
{
   /* If either the number of vertices or primitives is zero, set both of them to zero. */
   nir_def *num_vtx = nir_read_first_invocation(b, intrin->src[0].ssa);
   nir_def *num_prm = nir_read_first_invocation(b, intrin->src[1].ssa);
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *is_either_zero = nir_ieq(b, nir_umin(b, num_vtx, num_prm), zero);
   num_vtx = nir_bcsel(b, is_either_zero, zero, num_vtx);
   num_prm = nir_bcsel(b, is_either_zero, zero, num_prm);

   nir_store_var(b, s->vertex_count_var, num_vtx, 0x1);
   nir_store_var(b, s->primitive_count_var, num_prm, 0x1);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static nir_def *
update_ms_barrier(nir_builder *b,
                         nir_intrinsic_instr *intrin,
                         lower_ngg_ms_state *s)
{
   /* Output loads and stores are lowered to shared memory access,
    * so we have to update the barriers to also reflect this.
    */
   unsigned mem_modes = nir_intrinsic_memory_modes(intrin);
   if (mem_modes & nir_var_shader_out)
      mem_modes |= nir_var_mem_shared;
   else
      return NULL;

   nir_intrinsic_set_memory_modes(intrin, mem_modes);

   return NIR_LOWER_INSTR_PROGRESS;
}

static nir_def *
lower_ms_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_ms_state *s = (lower_ngg_ms_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return NULL;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_primitive_output:
      ms_store_arrayed_output_intrin(b, intrin, s);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   case nir_intrinsic_barrier:
      return update_ms_barrier(b, intrin, s);
   case nir_intrinsic_load_workgroup_index:
      return lower_ms_load_workgroup_index(b, intrin, s);
   case nir_intrinsic_set_vertex_and_primitive_count:
      return lower_ms_set_vertex_and_primitive_count(b, intrin, s);
   default:
      unreachable("Not a lowerable mesh shader intrinsic.");
   }
}

static bool
filter_ms_intrinsic(const nir_instr *instr,
                    UNUSED const void *s)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_per_primitive_output ||
          intrin->intrinsic == nir_intrinsic_barrier ||
          intrin->intrinsic == nir_intrinsic_load_workgroup_index ||
          intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count;
}

static void
lower_ms_intrinsics(nir_shader *shader, lower_ngg_ms_state *s)
{
   nir_shader_lower_instructions(shader, filter_ms_intrinsic, lower_ms_intrinsic, s);
}

static void
ms_emit_arrayed_outputs(nir_builder *b,
                        nir_def *invocation_index,
                        uint64_t mask,
                        lower_ngg_ms_state *s)
{
   nir_def *zero = nir_imm_int(b, 0);

   u_foreach_bit64(slot, mask) {
      /* Should not occur here, handled separately. */
      assert(slot != VARYING_SLOT_PRIMITIVE_COUNT && slot != VARYING_SLOT_PRIMITIVE_INDICES);

      unsigned component_mask = s->out.infos[slot].components_mask;

      while (component_mask) {
         int start_comp = 0, num_components = 1;
         u_bit_scan_consecutive_range(&component_mask, &start_comp, &num_components);

         nir_def *load =
            ms_load_arrayed_output(b, invocation_index, zero, slot, start_comp,
                                   num_components, 32, s);

         for (int i = 0; i < num_components; i++)
            s->out.outputs[slot][start_comp + i] = nir_channel(b, load, i);
      }
   }
}

static void
ms_create_same_invocation_vars(nir_builder *b, lower_ngg_ms_state *s)
{
   /* Initialize NIR variables for same-invocation outputs. */
   uint64_t same_invocation_output_mask = s->layout.var.prm_attr.mask | s->layout.var.vtx_attr.mask;

   u_foreach_bit64(slot, same_invocation_output_mask) {
      for (unsigned comp = 0; comp < 4; ++comp) {
         unsigned idx = slot * 4 + comp;
         s->out_variables[idx] = nir_local_variable_create(b->impl, glsl_uint_type(), "ms_var_output");
      }
   }
}

static void
ms_emit_legacy_workgroup_index(nir_builder *b, lower_ngg_ms_state *s)
{
   /* Workgroup ID should have been lowered to workgroup index. */
   assert(!BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_WORKGROUP_ID));

   /* No need to do anything if the shader doesn't use the workgroup index. */
   if (!BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_WORKGROUP_INDEX))
      return;

   b->cursor = nir_before_impl(b->impl);

   /* Legacy fast launch mode (FAST_LAUNCH=1):
    *
    * The HW doesn't support a proper workgroup index for vertex processing stages,
    * so we use the vertex ID which is equivalent to the index of the current workgroup
    * within the current dispatch.
    *
    * Due to the register programming of mesh shaders, this value is only filled for
    * the first invocation of the first wave. To let other waves know, we use LDS.
    */
   nir_def *workgroup_index = nir_load_vertex_id_zero_base(b);

   if (s->api_workgroup_size <= s->wave_size) {
      /* API workgroup is small, so we don't need to use LDS. */
      s->workgroup_index = nir_read_first_invocation(b, workgroup_index);
      return;
   }

   unsigned workgroup_index_lds_addr = s->layout.lds.workgroup_info_addr + lds_ms_wg_index;

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *dont_care = nir_undef(b, 1, 32);
   nir_def *loaded_workgroup_index = NULL;

   /* Use elect to make sure only 1 invocation uses LDS. */
   nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
   {
      nir_def *wave_id = nir_load_subgroup_id(b);
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, wave_id, 0));
      {
         nir_store_shared(b, workgroup_index, zero, .base = workgroup_index_lds_addr);
         nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                               .memory_scope = SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
      }
      nir_push_else(b, if_wave_0);
      {
         nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                               .memory_scope = SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
         loaded_workgroup_index = nir_load_shared(b, 1, 32, zero, .base = workgroup_index_lds_addr);
      }
      nir_pop_if(b, if_wave_0);

      workgroup_index = nir_if_phi(b, workgroup_index, loaded_workgroup_index);
   }
   nir_pop_if(b, if_elected);

   workgroup_index = nir_if_phi(b, workgroup_index, dont_care);
   s->workgroup_index = nir_read_first_invocation(b, workgroup_index);
}

static void
set_ms_final_output_counts(nir_builder *b,
                           lower_ngg_ms_state *s,
                           nir_def **out_num_prm,
                           nir_def **out_num_vtx)
{
   /* The spec allows the numbers to be divergent, and in that case we need to
    * use the values from the first invocation. Also the HW requires us to set
    * both to 0 if either was 0.
    *
    * These are already done by the lowering.
    */
   nir_def *num_prm = nir_load_var(b, s->primitive_count_var);
   nir_def *num_vtx = nir_load_var(b, s->vertex_count_var);

   if (s->hw_workgroup_size <= s->wave_size) {
      /* Single-wave mesh shader workgroup. */
      ac_nir_ngg_alloc_vertices_and_primitives(b, num_vtx, num_prm, false);

      *out_num_prm = num_prm;
      *out_num_vtx = num_vtx;
      return;
   }

   /* Multi-wave mesh shader workgroup:
    * We need to use LDS to distribute the correct values to the other waves.
    *
    * TODO:
    * If we can prove that the values are workgroup-uniform, we can skip this
    * and just use whatever the current wave has. However, NIR divergence analysis
    * currently doesn't support this.
    */

   nir_def *zero = nir_imm_int(b, 0);

   nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
   {
      nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
      {
         nir_store_shared(b, nir_vec2(b, num_prm, num_vtx), zero,
                          .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
      }
      nir_pop_if(b, if_elected);

      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                            .memory_scope = SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_mem_shared);

      ac_nir_ngg_alloc_vertices_and_primitives(b, num_vtx, num_prm, false);
   }
   nir_push_else(b, if_wave_0);
   {
      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                            .memory_scope = SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_mem_shared);

      nir_def *prm_vtx = NULL;
      nir_def *dont_care_2x32 = nir_undef(b, 2, 32);
      nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
      {
         prm_vtx = nir_load_shared(b, 2, 32, zero,
                                   .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
      }
      nir_pop_if(b, if_elected);

      prm_vtx = nir_if_phi(b, prm_vtx, dont_care_2x32);
      num_prm = nir_read_first_invocation(b, nir_channel(b, prm_vtx, 0));
      num_vtx = nir_read_first_invocation(b, nir_channel(b, prm_vtx, 1));

      nir_store_var(b, s->primitive_count_var, num_prm, 0x1);
      nir_store_var(b, s->vertex_count_var, num_vtx, 0x1);
   }
   nir_pop_if(b, if_wave_0);

   *out_num_prm = nir_load_var(b, s->primitive_count_var);
   *out_num_vtx = nir_load_var(b, s->vertex_count_var);
}

static void
ms_emit_attribute_ring_output_stores(nir_builder *b, const uint64_t outputs_mask,
                                     nir_def *idx, lower_ngg_ms_state *s)
{
   if (!outputs_mask)
      return;

   nir_def *ring = nir_load_ring_attr_amd(b);
   nir_def *off = nir_load_ring_attr_offset_amd(b);
   nir_def *zero = nir_imm_int(b, 0);

   u_foreach_bit64 (slot, outputs_mask) {
      if (s->vs_output_param_offset[slot] > AC_EXP_PARAM_OFFSET_31)
         continue;

      nir_def *soffset = nir_iadd_imm(b, off, s->vs_output_param_offset[slot] * 16 * 32);
      nir_def *store_val = nir_undef(b, 4, 32);
      unsigned store_val_components = 0;
      for (unsigned c = 0; c < 4; ++c) {
         if (s->out.outputs[slot][c]) {
            store_val = nir_vector_insert_imm(b, store_val, s->out.outputs[slot][c], c);
            store_val_components = c + 1;
         }
      }

      store_val = nir_trim_vector(b, store_val, store_val_components);
      nir_store_buffer_amd(b, store_val, ring, zero, soffset, idx,
                           .memory_modes = nir_var_shader_out,
                           .access = ACCESS_COHERENT | ACCESS_IS_SWIZZLED_AMD,
                           .align_mul = 16, .align_offset = 0);
   }
}

static nir_def *
ms_prim_exp_arg_ch1(nir_builder *b, nir_def *invocation_index, nir_def *num_vtx, lower_ngg_ms_state *s)
{
   /* Primitive connectivity data: describes which vertices the primitive uses. */
   nir_def *prim_idx_addr = nir_imul_imm(b, invocation_index, s->vertices_per_prim);
   nir_def *indices_loaded = NULL;
   nir_def *cull_flag = NULL;

   if (s->layout.var.prm_attr.mask & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES)) {
      nir_def *indices[3] = {0};
      for (unsigned c = 0; c < s->vertices_per_prim; ++c)
         indices[c] = nir_load_var(b, s->out_variables[VARYING_SLOT_PRIMITIVE_INDICES * 4 + c]);
      indices_loaded = nir_vec(b, indices, s->vertices_per_prim);
   } else {
      indices_loaded = nir_load_shared(b, s->vertices_per_prim, 8, prim_idx_addr, .base = s->layout.lds.indices_addr);
      indices_loaded = nir_u2u32(b, indices_loaded);
   }

   if (s->uses_cull_flags) {
      nir_def *loaded_cull_flag = NULL;
      if (s->layout.var.prm_attr.mask & BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE))
         loaded_cull_flag = nir_load_var(b, s->out_variables[VARYING_SLOT_CULL_PRIMITIVE * 4]);
      else
         loaded_cull_flag = nir_u2u32(b, nir_load_shared(b, 1, 8, prim_idx_addr, .base = s->layout.lds.cull_flags_addr));

      cull_flag = nir_i2b(b, loaded_cull_flag);
   }

   nir_def *indices[3];
   nir_def *max_vtx_idx = nir_iadd_imm(b, num_vtx, -1u);

   for (unsigned i = 0; i < s->vertices_per_prim; ++i) {
      indices[i] = nir_channel(b, indices_loaded, i);
      indices[i] = nir_umin(b, indices[i], max_vtx_idx);
   }

   return ac_nir_pack_ngg_prim_exp_arg(b, s->vertices_per_prim, indices, cull_flag, s->hw_info->gfx_level);
}

static nir_def *
ms_prim_exp_arg_ch2(nir_builder *b, uint64_t outputs_mask, lower_ngg_ms_state *s)
{
   nir_def *prim_exp_arg_ch2 = NULL;

   if (outputs_mask) {
      /* When layer, viewport etc. are per-primitive, they need to be encoded in
       * the primitive export instruction's second channel. The encoding is:
       *
       * --- GFX10.3 ---
       * bits 31..30: VRS rate Y
       * bits 29..28: VRS rate X
       * bits 23..20: viewport
       * bits 19..17: layer
       *
       * --- GFX11 ---
       * bits 31..28: VRS rate enum
       * bits 23..20: viewport
       * bits 12..00: layer
       */
      prim_exp_arg_ch2 = nir_imm_int(b, 0);

      if (outputs_mask & VARYING_BIT_LAYER) {
         nir_def *layer =
            nir_ishl_imm(b, s->out.outputs[VARYING_SLOT_LAYER][0], s->hw_info->gfx_level >= GFX11 ? 0 : 17);
         prim_exp_arg_ch2 = nir_ior(b, prim_exp_arg_ch2, layer);
      }

      if (outputs_mask & VARYING_BIT_VIEWPORT) {
         nir_def *view = nir_ishl_imm(b, s->out.outputs[VARYING_SLOT_VIEWPORT][0], 20);
         prim_exp_arg_ch2 = nir_ior(b, prim_exp_arg_ch2, view);
      }

      if (outputs_mask & VARYING_BIT_PRIMITIVE_SHADING_RATE) {
         nir_def *rate = s->out.outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE][0];
         prim_exp_arg_ch2 = nir_ior(b, prim_exp_arg_ch2, rate);
      }
   }

   return prim_exp_arg_ch2;
}

static void
ms_prim_gen_query(nir_builder *b,
                  nir_def *invocation_index,
                  nir_def *num_prm,
                  lower_ngg_ms_state *s)
{
   if (!s->has_query)
      return;

   nir_if *if_invocation_index_zero = nir_push_if(b, nir_ieq_imm(b, invocation_index, 0));
   {
      nir_if *if_shader_query = nir_push_if(b, nir_load_prim_gen_query_enabled_amd(b));
      {
         nir_atomic_add_gen_prim_count_amd(b, num_prm, .stream_id = 0);
      }
      nir_pop_if(b, if_shader_query);
   }
   nir_pop_if(b, if_invocation_index_zero);
}

static void
ms_invocation_query(nir_builder *b,
                    nir_def *invocation_index,
                    lower_ngg_ms_state *s)
{
   if (!s->has_query)
      return;

   nir_if *if_invocation_index_zero = nir_push_if(b, nir_ieq_imm(b, invocation_index, 0));
   {
      nir_if *if_pipeline_query = nir_push_if(b, nir_load_pipeline_stat_query_enabled_amd(b));
      {
         nir_atomic_add_shader_invocation_count_amd(b, nir_imm_int(b, s->api_workgroup_size));
      }
      nir_pop_if(b, if_pipeline_query);
   }
   nir_pop_if(b, if_invocation_index_zero);
}

static void
emit_ms_vertex(nir_builder *b, nir_def *index, nir_def *row, bool exports, bool parameters,
               uint64_t per_vertex_outputs, lower_ngg_ms_state *s)
{
   ms_emit_arrayed_outputs(b, index, per_vertex_outputs, s);

   if (exports) {
      ac_nir_export_position(b, s->hw_info->gfx_level, s->clipdist_enable_mask,
                             !s->has_param_exports, false, true,
                             s->per_vertex_outputs | VARYING_BIT_POS, &s->out, row);
   }

   if (parameters) {
      /* Export generic attributes when there is no attribute ring. */
      if (s->has_param_exports && !s->hw_info->has_attr_ring) {
         ac_nir_export_parameters(b, s->vs_output_param_offset, per_vertex_outputs, 0, &s->out);
      }

      /* Also store special outputs to the attribute ring so PS can load them. */
      if (s->hw_info->has_attr_ring && (per_vertex_outputs & MS_VERT_ARG_EXP_MASK))
         ms_emit_attribute_ring_output_stores(b, per_vertex_outputs & MS_VERT_ARG_EXP_MASK, index, s);
   }
}

static void
emit_ms_primitive(nir_builder *b, nir_def *index, nir_def *row, bool exports, bool parameters,
                  uint64_t per_primitive_outputs, lower_ngg_ms_state *s)
{
   ms_emit_arrayed_outputs(b, index, per_primitive_outputs, s);

   /* Insert layer output store if the pipeline uses multiview but the API shader doesn't write it. */
   if (s->insert_layer_output) {
      s->out.outputs[VARYING_SLOT_LAYER][0] = nir_load_view_index(b);
      s->out.infos[VARYING_SLOT_LAYER].as_sysval_mask |= 1;
   }

   if (exports) {
      const uint64_t outputs_mask = per_primitive_outputs & MS_PRIM_ARG_EXP_MASK;
      nir_def *num_vtx = nir_load_var(b, s->vertex_count_var);
      nir_def *prim_exp_arg_ch1 = ms_prim_exp_arg_ch1(b, index, num_vtx, s);
      nir_def *prim_exp_arg_ch2 = ms_prim_exp_arg_ch2(b, outputs_mask, s);

      nir_def *prim_exp_arg = prim_exp_arg_ch2 ?
         nir_vec2(b, prim_exp_arg_ch1, prim_exp_arg_ch2) : prim_exp_arg_ch1;

      ac_nir_export_primitive(b, prim_exp_arg, row);
   }

   if (parameters) {
      /* Export generic attributes when there is no attribute ring. */
      if (s->has_param_exports && !s->hw_info->has_attr_ring) {
         ac_nir_export_parameters(b, s->vs_output_param_offset, per_primitive_outputs, 0, &s->out);
      }

      /* Also store special outputs to the attribute ring so PS can load them. */
      if (s->hw_info->has_attr_ring)
         ms_emit_attribute_ring_output_stores(b, per_primitive_outputs & MS_PRIM_ARG_EXP_MASK, index, s);
   }
}

static void
emit_ms_outputs(nir_builder *b, nir_def *invocation_index, nir_def *row_start,
                nir_def *count, bool exports, bool parameters, uint64_t mask,
                void (*cb)(nir_builder *, nir_def *, nir_def *, bool, bool,
                           uint64_t, lower_ngg_ms_state *),
                lower_ngg_ms_state *s)
{
   if (cb == &emit_ms_primitive ? s->prim_multirow_export : s->vert_multirow_export) {
      assert(s->hw_workgroup_size % s->wave_size == 0);
      const unsigned num_waves = s->hw_workgroup_size / s->wave_size;

      nir_loop *row_loop = nir_push_loop(b);
      {
         nir_block *preheader = nir_cf_node_as_block(nir_cf_node_prev(&row_loop->cf_node));

         nir_phi_instr *index = nir_phi_instr_create(b->shader);
         nir_phi_instr *row = nir_phi_instr_create(b->shader);
         nir_def_init(&index->instr, &index->def, 1, 32);
         nir_def_init(&row->instr, &row->def, 1, 32);

         nir_phi_instr_add_src(index, preheader, invocation_index);
         nir_phi_instr_add_src(row, preheader, row_start);

         nir_if *if_break = nir_push_if(b, nir_uge(b, &index->def, count));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, if_break);

         cb(b, &index->def, &row->def, exports, parameters, mask, s);

         nir_block *body = nir_cursor_current_block(b->cursor);
         nir_phi_instr_add_src(index, body,
                               nir_iadd_imm(b, &index->def, s->hw_workgroup_size));
         nir_phi_instr_add_src(row, body,
                               nir_iadd_imm(b, &row->def, num_waves));

         nir_instr_insert_before_cf_list(&row_loop->body, &row->instr);
         nir_instr_insert_before_cf_list(&row_loop->body, &index->instr);
      }
      nir_pop_loop(b, row_loop);
   } else {
      nir_def *has_output = nir_ilt(b, invocation_index, count);
      nir_if *if_has_output = nir_push_if(b, has_output);
      {
         cb(b, invocation_index, row_start, exports, parameters, mask, s);
      }
      nir_pop_if(b, if_has_output);
   }
}

static void
emit_ms_finale(nir_builder *b, lower_ngg_ms_state *s)
{
   /* We assume there is always a single end block in the shader. */
   nir_block *last_block = nir_impl_last_block(b->impl);
   b->cursor = nir_after_block(last_block);

   nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_shader_out|nir_var_mem_shared);

   nir_def *num_prm;
   nir_def *num_vtx;

   set_ms_final_output_counts(b, s, &num_prm, &num_vtx);

   nir_def *invocation_index = nir_load_local_invocation_index(b);

   ms_prim_gen_query(b, invocation_index, num_prm, s);

   nir_def *row_start = NULL;
   if (s->fast_launch_2)
      row_start = s->hw_workgroup_size <= s->wave_size ? nir_imm_int(b, 0) : nir_load_subgroup_id(b);

   /* Load vertex/primitive attributes from shared memory and
    * emit store_output intrinsics for them.
    *
    * Contrary to the semantics of the API mesh shader, these are now
    * compliant with NGG HW semantics, meaning that these store the
    * current thread's vertex attributes in a way the HW can export.
    */

   uint64_t per_vertex_outputs =
      s->per_vertex_outputs & ~s->layout.attr_ring.vtx_attr.mask;
   uint64_t per_primitive_outputs =
      s->per_primitive_outputs & ~s->layout.attr_ring.prm_attr.mask & ~SPECIAL_MS_OUT_MASK;

   /* Insert layer output store if the pipeline uses multiview but the API shader doesn't write it. */
   if (s->insert_layer_output) {
      b->shader->info.outputs_written |= VARYING_BIT_LAYER;
      b->shader->info.per_primitive_outputs |= VARYING_BIT_LAYER;
      per_primitive_outputs |= VARYING_BIT_LAYER;
   }

   const bool has_special_param_exports =
      (per_vertex_outputs & MS_VERT_ARG_EXP_MASK) ||
      (per_primitive_outputs & MS_PRIM_ARG_EXP_MASK);
   const bool wait_attr_ring = has_special_param_exports && s->hw_info->has_attr_ring_wait_bug;

   /* Export vertices. */
   if ((per_vertex_outputs & ~VARYING_BIT_POS) || !wait_attr_ring) {
      emit_ms_outputs(b, invocation_index, row_start, num_vtx, !wait_attr_ring, true,
                      per_vertex_outputs, &emit_ms_vertex, s);
   }

   /* Export primitives. */
   if (per_primitive_outputs || !wait_attr_ring) {
      emit_ms_outputs(b, invocation_index, row_start, num_prm, !wait_attr_ring, true,
                      per_primitive_outputs, &emit_ms_primitive, s);
   }

   /* When we need to wait for attribute ring stores, we emit both position and primitive
    * export instructions after a barrier to make sure both per-vertex and per-primitive
    * attribute ring stores are finished before the GPU starts rasterization.
    */
   if (wait_attr_ring) {
      /* Wait for attribute stores to finish. */
      nir_barrier(b, .execution_scope = SCOPE_SUBGROUP,
                     .memory_scope = SCOPE_DEVICE,
                     .memory_semantics = NIR_MEMORY_RELEASE,
                     .memory_modes = nir_var_shader_out);

      /* Position/primitive export only */
      emit_ms_outputs(b, invocation_index, row_start, num_vtx, true, false,
                      per_vertex_outputs, &emit_ms_vertex, s);
      emit_ms_outputs(b, invocation_index, row_start, num_prm, true, false,
                      per_primitive_outputs, &emit_ms_primitive, s);
   }
}

static void
handle_smaller_ms_api_workgroup(nir_builder *b,
                                lower_ngg_ms_state *s)
{
   if (s->api_workgroup_size >= s->hw_workgroup_size)
      return;

   /* Handle barriers manually when the API workgroup
    * size is less than the HW workgroup size.
    *
    * The problem is that the real workgroup launched on NGG HW
    * will be larger than the size specified by the API, and the
    * extra waves need to keep up with barriers in the API waves.
    *
    * There are 2 different cases:
    * 1. The whole API workgroup fits in a single wave.
    *    We can shrink the barriers to subgroup scope and
    *    don't need to insert any extra ones.
    * 2. The API workgroup occupies multiple waves, but not
    *    all. In this case, we emit code that consumes every
    *    barrier on the extra waves.
    */
   assert(s->hw_workgroup_size % s->wave_size == 0);
   bool scan_barriers = ALIGN(s->api_workgroup_size, s->wave_size) < s->hw_workgroup_size;
   bool can_shrink_barriers = s->api_workgroup_size <= s->wave_size;
   bool need_additional_barriers = scan_barriers && !can_shrink_barriers;

   unsigned api_waves_in_flight_addr = s->layout.lds.workgroup_info_addr + lds_ms_num_api_waves;
   unsigned num_api_waves = DIV_ROUND_UP(s->api_workgroup_size, s->wave_size);

   /* Scan the shader for workgroup barriers. */
   if (scan_barriers) {
      bool has_any_workgroup_barriers = false;

      nir_foreach_block(block, b->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            bool is_workgroup_barrier =
               intrin->intrinsic == nir_intrinsic_barrier &&
               nir_intrinsic_execution_scope(intrin) == SCOPE_WORKGROUP;

            if (!is_workgroup_barrier)
               continue;

            if (can_shrink_barriers) {
               /* Every API invocation runs in the first wave.
                * In this case, we can change the barriers to subgroup scope
                * and avoid adding additional barriers.
                */
               nir_intrinsic_set_memory_scope(intrin, SCOPE_SUBGROUP);
               nir_intrinsic_set_execution_scope(intrin, SCOPE_SUBGROUP);
            } else {
               has_any_workgroup_barriers = true;
            }
         }
      }

      need_additional_barriers &= has_any_workgroup_barriers;
   }

   /* Extract the full control flow of the shader. */
   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_impl(b->impl),
                  nir_after_cf_list(&b->impl->body));
   b->cursor = nir_before_impl(b->impl);

   /* Wrap the shader in an if to ensure that only the necessary amount of lanes run it. */
   nir_def *invocation_index = nir_load_local_invocation_index(b);
   nir_def *zero = nir_imm_int(b, 0);

   if (need_additional_barriers) {
      /* First invocation stores 0 to number of API waves in flight. */
      nir_if *if_first_in_workgroup = nir_push_if(b, nir_ieq_imm(b, invocation_index, 0));
      {
         nir_store_shared(b, nir_imm_int(b, num_api_waves), zero, .base = api_waves_in_flight_addr);
      }
      nir_pop_if(b, if_first_in_workgroup);

      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                            .memory_scope = SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_shader_out | nir_var_mem_shared);
   }

   nir_def *has_api_ms_invocation = nir_ult_imm(b, invocation_index, s->api_workgroup_size);
   nir_if *if_has_api_ms_invocation = nir_push_if(b, has_api_ms_invocation);
   {
      nir_cf_reinsert(&extracted, b->cursor);
      b->cursor = nir_after_cf_list(&if_has_api_ms_invocation->then_list);

      if (need_additional_barriers) {
         /* One invocation in each API wave decrements the number of API waves in flight. */
         nir_if *if_elected_again = nir_push_if(b, nir_elect(b, 1));
         {
            nir_shared_atomic(b, 32, zero, nir_imm_int(b, -1u),
                              .base = api_waves_in_flight_addr,
                              .atomic_op = nir_atomic_op_iadd);
         }
         nir_pop_if(b, if_elected_again);

         nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                               .memory_scope = SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_shader_out | nir_var_mem_shared);
      }

      ms_invocation_query(b, invocation_index, s);
   }
   nir_pop_if(b, if_has_api_ms_invocation);

   if (need_additional_barriers) {
      /* Make sure that waves that don't run any API invocations execute
       * the same amount of barriers as those that do.
       *
       * We do this by executing a barrier until the number of API waves
       * in flight becomes zero.
       */
      nir_def *has_api_ms_ballot = nir_ballot(b, 1, s->wave_size, has_api_ms_invocation);
      nir_def *wave_has_no_api_ms = nir_ieq_imm(b, has_api_ms_ballot, 0);
      nir_if *if_wave_has_no_api_ms = nir_push_if(b, wave_has_no_api_ms);
      {
         nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
         {
            nir_loop *loop = nir_push_loop(b);
            {
               nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                                     .memory_scope = SCOPE_WORKGROUP,
                                     .memory_semantics = NIR_MEMORY_ACQ_REL,
                                     .memory_modes = nir_var_shader_out | nir_var_mem_shared);

               nir_def *loaded = nir_load_shared(b, 1, 32, zero, .base = api_waves_in_flight_addr);
               nir_if *if_break = nir_push_if(b, nir_ieq_imm(b, loaded, 0));
               {
                  nir_jump(b, nir_jump_break);
               }
               nir_pop_if(b, if_break);
            }
            nir_pop_loop(b, loop);
         }
         nir_pop_if(b, if_elected);
      }
      nir_pop_if(b, if_wave_has_no_api_ms);
   }
}

static void
ms_move_output(ms_out_part *from, ms_out_part *to)
{
   uint64_t loc = util_logbase2_64(from->mask);
   uint64_t bit = BITFIELD64_BIT(loc);
   from->mask ^= bit;
   to->mask |= bit;
}

static void
ms_calculate_arrayed_output_layout(ms_out_mem_layout *l,
                                   unsigned max_vertices,
                                   unsigned max_primitives)
{
   uint32_t lds_vtx_attr_size = util_bitcount64(l->lds.vtx_attr.mask) * max_vertices * 16;
   uint32_t lds_prm_attr_size = util_bitcount64(l->lds.prm_attr.mask) * max_primitives * 16;
   l->lds.prm_attr.addr = ALIGN(l->lds.vtx_attr.addr + lds_vtx_attr_size, 16);
   l->lds.total_size = l->lds.prm_attr.addr + lds_prm_attr_size;

   uint32_t scratch_ring_vtx_attr_size =
      util_bitcount64(l->scratch_ring.vtx_attr.mask) * max_vertices * 16;
   l->scratch_ring.prm_attr.addr =
      ALIGN(l->scratch_ring.vtx_attr.addr + scratch_ring_vtx_attr_size, 16);
}

static ms_out_mem_layout
ms_calculate_output_layout(const struct radeon_info *hw_info, unsigned api_shared_size,
                           uint64_t per_vertex_output_mask, uint64_t per_primitive_output_mask,
                           uint64_t cross_invocation_output_access, unsigned max_vertices,
                           unsigned max_primitives, unsigned vertices_per_prim)
{
   /* These outputs always need export instructions and can't use the attributes ring. */
   const uint64_t always_export_mask =
      VARYING_BIT_POS | VARYING_BIT_CULL_DIST0 | VARYING_BIT_CULL_DIST1 | VARYING_BIT_CLIP_DIST0 |
      VARYING_BIT_CLIP_DIST1 | VARYING_BIT_PSIZ | VARYING_BIT_VIEWPORT |
      VARYING_BIT_PRIMITIVE_SHADING_RATE | VARYING_BIT_LAYER |
      BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_COUNT) |
      BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES) | BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE);

   const bool use_attr_ring = hw_info->has_attr_ring;
   const uint64_t attr_ring_per_vertex_output_mask =
      use_attr_ring ? per_vertex_output_mask & ~always_export_mask : 0;
   const uint64_t attr_ring_per_primitive_output_mask =
      use_attr_ring ? per_primitive_output_mask & ~always_export_mask : 0;

   const uint64_t lds_per_vertex_output_mask =
      per_vertex_output_mask & ~attr_ring_per_vertex_output_mask & cross_invocation_output_access &
      ~SPECIAL_MS_OUT_MASK;
   const uint64_t lds_per_primitive_output_mask =
      per_primitive_output_mask & ~attr_ring_per_primitive_output_mask &
      cross_invocation_output_access & ~SPECIAL_MS_OUT_MASK;

   const bool cross_invocation_indices =
      cross_invocation_output_access & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES);
   const bool cross_invocation_cull_primitive =
      cross_invocation_output_access & BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE);

   /* Shared memory used by the API shader. */
   ms_out_mem_layout l = { .lds = { .total_size = api_shared_size } };

   /* Use attribute ring for all generic attributes (on GPUs with an attribute ring). */
   l.attr_ring.vtx_attr.mask = attr_ring_per_vertex_output_mask;
   l.attr_ring.prm_attr.mask = attr_ring_per_primitive_output_mask;

   /* Outputs without cross-invocation access can be stored in variables. */
   l.var.vtx_attr.mask =
      per_vertex_output_mask & ~attr_ring_per_vertex_output_mask & ~cross_invocation_output_access;
   l.var.prm_attr.mask = per_primitive_output_mask & ~attr_ring_per_primitive_output_mask &
                         ~cross_invocation_output_access;

   /* Workgroup information, see ms_workgroup_* for the layout. */
   l.lds.workgroup_info_addr = ALIGN(l.lds.total_size, 16);
   l.lds.total_size = l.lds.workgroup_info_addr + 16;

   /* Per-vertex and per-primitive output attributes.
    * Outputs without cross-invocation access are not included here.
    * First, try to put all outputs into LDS (shared memory).
    * If they don't fit, try to move them to VRAM one by one.
    */
   l.lds.vtx_attr.addr = ALIGN(l.lds.total_size, 16);
   l.lds.vtx_attr.mask = lds_per_vertex_output_mask;
   l.lds.prm_attr.mask = lds_per_primitive_output_mask;
   ms_calculate_arrayed_output_layout(&l, max_vertices, max_primitives);

   /* NGG shaders can only address up to 32K LDS memory.
    * The spec requires us to allow the application to use at least up to 28K
    * shared memory. Additionally, we reserve 2K for driver internal use
    * (eg. primitive indices and such, see below).
    *
    * Move the outputs that do not fit LDS, to VRAM.
    * Start with per-primitive attributes, because those are grouped at the end.
    */
   const unsigned usable_lds_kbytes =
      (cross_invocation_cull_primitive || cross_invocation_indices) ? 30 : 31;
   while (l.lds.total_size >= usable_lds_kbytes * 1024) {
      if (l.lds.prm_attr.mask)
         ms_move_output(&l.lds.prm_attr, &l.scratch_ring.prm_attr);
      else if (l.lds.vtx_attr.mask)
         ms_move_output(&l.lds.vtx_attr, &l.scratch_ring.vtx_attr);
      else
         unreachable("API shader uses too much shared memory.");

      ms_calculate_arrayed_output_layout(&l, max_vertices, max_primitives);
   }

   if (cross_invocation_indices) {
      /* Indices: flat array of 8-bit vertex indices for each primitive. */
      l.lds.indices_addr = ALIGN(l.lds.total_size, 16);
      l.lds.total_size = l.lds.indices_addr + max_primitives * vertices_per_prim;
   }

   if (cross_invocation_cull_primitive) {
      /* Cull flags: array of 8-bit cull flags for each primitive, 1=cull, 0=keep. */
      l.lds.cull_flags_addr = ALIGN(l.lds.total_size, 16);
      l.lds.total_size = l.lds.cull_flags_addr + max_primitives;
   }

   /* NGG is only allowed to address up to 32K of LDS. */
   assert(l.lds.total_size <= 32 * 1024);
   return l;
}

bool
ac_nir_lower_ngg_mesh(nir_shader *shader,
                      const struct radeon_info *hw_info,
                      uint32_t clipdist_enable_mask,
                      const uint8_t *vs_output_param_offset,
                      bool has_param_exports,
                      bool *out_needs_scratch_ring,
                      unsigned wave_size,
                      unsigned hw_workgroup_size,
                      bool multiview,
                      bool has_query,
                      bool fast_launch_2)
{
   unsigned vertices_per_prim =
      mesa_vertices_per_prim(shader->info.mesh.primitive_type);

   uint64_t per_vertex_outputs =
      shader->info.outputs_written & ~shader->info.per_primitive_outputs & ~SPECIAL_MS_OUT_MASK;
   uint64_t per_primitive_outputs =
      shader->info.per_primitive_outputs & shader->info.outputs_written;

   /* Whether the shader uses CullPrimitiveEXT */
   bool uses_cull = shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE);
   /* Can't handle indirect register addressing, pretend as if they were cross-invocation. */
   uint64_t cross_invocation_access = shader->info.mesh.ms_cross_invocation_output_access |
                                      shader->info.outputs_accessed_indirectly;

   unsigned max_vertices = shader->info.mesh.max_vertices_out;
   unsigned max_primitives = shader->info.mesh.max_primitives_out;

   ms_out_mem_layout layout = ms_calculate_output_layout(
      hw_info, shader->info.shared_size, per_vertex_outputs, per_primitive_outputs,
      cross_invocation_access, max_vertices, max_primitives, vertices_per_prim);

   shader->info.shared_size = layout.lds.total_size;
   *out_needs_scratch_ring = layout.scratch_ring.vtx_attr.mask || layout.scratch_ring.prm_attr.mask;

   /* The workgroup size that is specified by the API shader may be different
    * from the size of the workgroup that actually runs on the HW, due to the
    * limitations of NGG: max 0/1 vertex and 0/1 primitive per lane is allowed.
    *
    * Therefore, we must make sure that when the API workgroup size is smaller,
    * we don't run the API shader on more HW invocations than is necessary.
    */
   unsigned api_workgroup_size = shader->info.workgroup_size[0] *
                                 shader->info.workgroup_size[1] *
                                 shader->info.workgroup_size[2];

   lower_ngg_ms_state state = {
      .layout = layout,
      .wave_size = wave_size,
      .per_vertex_outputs = per_vertex_outputs,
      .per_primitive_outputs = per_primitive_outputs,
      .vertices_per_prim = vertices_per_prim,
      .api_workgroup_size = api_workgroup_size,
      .hw_workgroup_size = hw_workgroup_size,
      .insert_layer_output = multiview && !(shader->info.outputs_written & VARYING_BIT_LAYER),
      .uses_cull_flags = uses_cull,
      .hw_info = hw_info,
      .fast_launch_2 = fast_launch_2,
      .vert_multirow_export = fast_launch_2 && max_vertices > hw_workgroup_size,
      .prim_multirow_export = fast_launch_2 && max_primitives > hw_workgroup_size,
      .clipdist_enable_mask = clipdist_enable_mask,
      .vs_output_param_offset = vs_output_param_offset,
      .has_param_exports = has_param_exports,
      .has_query = has_query,
   };

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);

   state.vertex_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "vertex_count_var");
   state.primitive_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "primitive_count_var");

   nir_builder builder = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &builder; /* This is to avoid the & */

   handle_smaller_ms_api_workgroup(b, &state);
   if (!fast_launch_2)
      ms_emit_legacy_workgroup_index(b, &state);
   ms_create_same_invocation_vars(b, &state);

   lower_ms_intrinsics(shader, &state);

   emit_ms_finale(b, &state);

   /* Take care of metadata and validation before calling other passes */
   nir_progress(true, impl, nir_metadata_none);
   nir_validate_shader(shader, "after emitting NGG MS");

   /* Cleanup */
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_lower_alu_to_scalar(shader, NULL, NULL);
   nir_lower_phis_to_scalar(shader, true);

   /* Optimize load_local_invocation_index. When the API workgroup is smaller than the HW workgroup,
    * local_invocation_id isn't initialized for all lanes and we can't perform this optimization for
    * all load_local_invocation_index.
    */
   if (fast_launch_2 && api_workgroup_size == hw_workgroup_size &&
       ((shader->info.workgroup_size[0] == 1) + (shader->info.workgroup_size[1] == 1) +
        (shader->info.workgroup_size[2] == 1)) == 2) {
      nir_lower_compute_system_values_options csv_options = {
         .lower_local_invocation_index = true,
      };
      nir_lower_compute_system_values(shader, &csv_options);
   }


   return true;
}
