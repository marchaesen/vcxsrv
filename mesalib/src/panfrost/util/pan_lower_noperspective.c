/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_ir.h"

/* Mali only provides instructions to fetch varyings with either flat or
 * perspective-correct interpolation. This pass lowers noperspective varyings
 * to perspective-correct varyings by multiplying by W in the VS and dividing
 * by W in the FS.
 *
 * This pass needs to lower noperspective varyings in the VS, however Vulkan
 * and OpenGL do not require interpolation qualifiers to match between stages.
 * Only the qualifiers in the fragment shader matter. To handle this, we load
 * a bitfield of noperspective varyings in the linked FS from the
 * 'noperspective_varyings_pan' sysval in the VS. If the FS qualifiers are
 * known at compile-time (for example, with monolithic pipelines in vulkan),
 * this may be lowered to a constant.
 *
 * This pass is expected to run after nir_lower_io_to_temporaries and
 * nir_lower_io, so each IO location must have at most one read or write.
 * These properties are preserved.
 *
 * This pass is expected to run after nir_lower_viewport_transform, so
 * gl_Position.w is actually 1 / gl_Position.w. This is because
 * nir_lower_viewport_transform may clamp large W values, and we need to use
 * the clamped value here. */

static nir_intrinsic_instr *
find_pos_store(nir_function_impl *impl)
{
   /* nir_lower_io_to_temporaries ensures all stores are in the exit block */
   nir_block *block = nir_impl_last_block(impl);
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic != nir_intrinsic_store_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      if (sem.location == VARYING_SLOT_POS)
         return intrin;
   }

   return NULL;
}

static bool
is_noperspective_load(nir_intrinsic_instr* intrin)
{
   if (intrin->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_intrinsic_instr *bary_instr = nir_src_as_intrinsic(intrin->src[0]);
   assert(bary_instr);

   return nir_intrinsic_interp_mode(bary_instr) == INTERP_MODE_NOPERSPECTIVE;
}

static bool
has_noperspective_load(nir_function_impl *impl)
{
   /* nir_lower_io_to_temporaries ersures all loads are in the first block */
   nir_block *block = nir_start_block(impl);
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (is_noperspective_load(intrin))
         return true;
   }
   return false;
}

/**
 * Returns a bitfield of VS outputs where it is known at compile-time that
 * noperspective interpolation may be used at runtime. Similar to the
 * noperspective_varyings_pan sysval, this bitfield only covers user varyings
 * (starting at VARYING_SLOT_VAR0).
 *
 * Precomputed because struct outputs may be split into multiple store_output
 * intrinsics. If any struct members are integers, then the whole struct
 * cannot be noperspective.
 */
static uint32_t
get_maybe_noperspective_outputs(nir_function_impl *impl)
{
   uint32_t used_outputs = 0;
   uint32_t integer_outputs = 0;

   /* nir_lower_io_to_temporaries ensures all stores are in the exit block */
   nir_block *block = nir_impl_last_block(impl);
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic != nir_intrinsic_store_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
      if (sem.location < VARYING_SLOT_VAR0)
         continue;

      uint32_t location_bit = BITFIELD_BIT(sem.location - VARYING_SLOT_VAR0);
      used_outputs |= location_bit;

      nir_alu_type type = nir_intrinsic_src_type(intrin);
      nir_alu_type base_type = nir_alu_type_get_base_type(type);

      if (base_type == nir_type_int ||
          base_type == nir_type_uint ||
          base_type == nir_type_bool)
         integer_outputs |= location_bit;
   }

   /* From the Vulkan 1.1.301 spec:
    *
    *    "Output attributes of integer or unsigned integer type must always be
    *    flat shaded."
    *
    * From the OpenGL 4.6 spec:
    *
    *    "Implementations need not support interpolation of output values of
    *    integer or unsigned integer type, as all such attributes must be flat
    *    shaded."
    *
    * So we can assume varyings that contain integers are never noperspective.
    */
   return used_outputs & ~integer_outputs;
}

static bool
is_maybe_noperspective_output(unsigned location,
                              uint32_t maybe_noperspective_outputs)
{
   return location >= VARYING_SLOT_VAR0 &&
      maybe_noperspective_outputs & BITFIELD_BIT(location - VARYING_SLOT_VAR0);
}

static nir_def *
is_noperspective_output(nir_builder *b, unsigned location,
                        nir_def *noperspective_outputs)
{
   if (location < VARYING_SLOT_VAR0)
      return nir_imm_bool(b, false);
   uint32_t bit = BITFIELD_BIT(location - VARYING_SLOT_VAR0);
   return nir_i2b(b, nir_iand_imm(b, noperspective_outputs, bit));
}

struct lower_noperspective_vs_state {
   nir_def *pos_w;
   uint32_t maybe_noperspective_outputs;
   nir_def *noperspective_outputs;
};

/**
 * Multiply all noperspective varying stores by gl_Position.w
 */
static bool
lower_noperspective_vs(nir_builder *b, nir_intrinsic_instr *intrin,
                       void *data)
{
   struct lower_noperspective_vs_state *state = data;

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;
   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

   if (!is_maybe_noperspective_output(sem.location,
                                      state->maybe_noperspective_outputs))
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *is_noperspective =
      is_noperspective_output(b, sem.location, state->noperspective_outputs);

   nir_def *old_value = intrin->src[0].ssa;
   nir_def *pos_w = state->pos_w;
   if (old_value->bit_size == 16)
      pos_w = nir_f2f16(b, pos_w);
   nir_def *noperspective_value = nir_fmul(b, old_value, pos_w);
   nir_def *new_value =
      nir_bcsel(b, is_noperspective, noperspective_value, old_value);

   nir_src_rewrite(&intrin->src[0], new_value);

   return true;
}

/**
 * Multiply all noperspective varying loads by gl_FragCoord.w
 */
static bool
lower_noperspective_fs(nir_builder *b, nir_intrinsic_instr *intrin,
                       void *data)
{
   if (!is_noperspective_load(intrin))
      return false;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *bary = intrin->src[0].ssa;
   nir_def *fragcoord_w = nir_load_frag_coord_zw_pan(b, bary, .component = 3);
   if (intrin->def.bit_size == 16)
      fragcoord_w = nir_f2f16(b, fragcoord_w);

   nir_def *new_value = nir_fmul(b, &intrin->def, fragcoord_w);
   nir_def_rewrite_uses_after(&intrin->def, new_value, new_value->parent_instr);

   return true;
}

/**
 * Move all stores to output variables that occur before the specified
 * instruction in the same block to after the specified instruction.
 */
static void
move_output_stores_after(nir_instr *after)
{
   nir_cursor cursor = nir_after_instr(after);
   nir_block *block = nir_cursor_current_block(cursor);
   nir_foreach_instr_safe(instr, block) {
      if (instr == after)
         break;

      if (instr->type != nir_instr_type_intrinsic)
         continue;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic == nir_intrinsic_store_output)
         nir_instr_move(cursor, instr);
   }
}

bool
pan_nir_lower_noperspective_vs(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   if (!(shader->info.outputs_written & VARYING_BIT_POS))
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   uint32_t maybe_noperspective_outputs = get_maybe_noperspective_outputs(impl);
   if (!maybe_noperspective_outputs)
      return false;

   nir_intrinsic_instr *pos_store = find_pos_store(impl);
   assert(pos_store);
   assert(nir_intrinsic_write_mask(pos_store) & BITFIELD_BIT(3));

   nir_builder b = nir_builder_at(nir_after_instr(&pos_store->instr));

   /* This is after nir_lower_viewport_transform, so stored W is 1/W */
   nir_def *pos_w_recip = nir_channel(&b, pos_store->src[0].ssa, 3);
   nir_def *pos_w = nir_frcp(&b, pos_w_recip);

   /* Reorder stores to ensure pos_w def is available */
   move_output_stores_after(pos_w->parent_instr);

   nir_def *noperspective_outputs = nir_load_noperspective_varyings_pan(&b);
   struct lower_noperspective_vs_state state = {
      .pos_w = pos_w,
      .maybe_noperspective_outputs = maybe_noperspective_outputs,
      .noperspective_outputs = noperspective_outputs,
   };
   nir_shader_intrinsics_pass(shader, lower_noperspective_vs,
                              nir_metadata_control_flow |
                              nir_metadata_loop_analysis,
                              (void *)&state);

   return true;
}

bool
pan_nir_lower_noperspective_fs(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   if (!has_noperspective_load(impl))
      return false;

   nir_shader_intrinsics_pass(shader, lower_noperspective_fs,
                              nir_metadata_control_flow, NULL);

   return true;
}

static bool
lower_static_noperspective(nir_builder *b, nir_intrinsic_instr *intrin,
                           void *data)
{
   uint32_t *noperspective_varyings = data;

   if (intrin->intrinsic != nir_intrinsic_load_noperspective_varyings_pan)
      return false;

   b->cursor = nir_after_instr(&intrin->instr);
   nir_def *val = nir_imm_int(b, *noperspective_varyings);
   nir_def_replace(&intrin->def, val);

   return true;
}

/**
 * Lower loads from the noperspective_varyings_pan sysval to a constant.
 */
bool
pan_nir_lower_static_noperspective(nir_shader *shader,
                                   uint32_t noperspective_varyings)
{
   return nir_shader_intrinsics_pass(shader, lower_static_noperspective,
                                     nir_metadata_control_flow,
                                     (void *)&noperspective_varyings);
}
