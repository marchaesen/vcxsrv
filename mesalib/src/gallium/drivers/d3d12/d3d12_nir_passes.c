/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "d3d12_nir_passes.h"
#include "d3d12_compiler.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"
#include "nir_format_convert.h"
#include "program/prog_instruction.h"
#include "dxil_nir.h"

/**
 * Lower Y Flip:
 *
 * We can't do a Y flip simply by negating the viewport height,
 * so we need to lower the flip into the NIR shader.
 */

static nir_ssa_def *
get_state_var(nir_builder *b,
              enum d3d12_state_var var_enum,
              const char *var_name,
              const struct glsl_type *var_type,
              nir_variable **out_var)
{
   const gl_state_index16 tokens[STATE_LENGTH] = { STATE_INTERNAL_DRIVER, var_enum };
   if (*out_var == NULL) {
      nir_variable *var = nir_variable_create(b->shader,
                                              nir_var_uniform,
                                              var_type,
                                              var_name);

      var->num_state_slots = 1;
      var->state_slots = ralloc_array(var, nir_state_slot, 1);
      memcpy(var->state_slots[0].tokens, tokens,
             sizeof(var->state_slots[0].tokens));
      var->data.how_declared = nir_var_hidden;
      b->shader->num_uniforms++;
      *out_var = var;
   }
   return nir_load_var(b, *out_var);
}

static void
lower_pos_write(nir_builder *b, struct nir_instr *instr, nir_variable **flip)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_POS)
      return;

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *pos = nir_ssa_for_src(b, intr->src[1], 4);
   nir_ssa_def *flip_y = get_state_var(b, D3D12_STATE_VAR_Y_FLIP, "d3d12_FlipY",
                                       glsl_float_type(), flip);
   nir_ssa_def *def = nir_vec4(b,
                               nir_channel(b, pos, 0),
                               nir_fmul(b, nir_channel(b, pos, 1), flip_y),
                               nir_channel(b, pos, 2),
                               nir_channel(b, pos, 3));
   nir_instr_rewrite_src(&intr->instr, intr->src + 1, nir_src_for_ssa(def));
}

void
d3d12_lower_yflip(nir_shader *nir)
{
   nir_variable *flip = NULL;

   if (nir->info.stage != MESA_SHADER_VERTEX &&
       nir->info.stage != MESA_SHADER_GEOMETRY)
      return;

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               lower_pos_write(&b, instr, &flip);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}

static void
lower_load_face(nir_builder *b, struct nir_instr *instr, nir_variable *var)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_front_face)
      return;

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *load = nir_load_var(b, var);

   nir_ssa_def_rewrite_uses(&intr->dest.ssa, load);
   nir_instr_remove(instr);
}

void
d3d12_forward_front_face(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   nir_variable *var = nir_variable_create(nir, nir_var_shader_in,
                                           glsl_bool_type(),
                                           "gl_FrontFacing");
   var->data.location = VARYING_SLOT_VAR12;
   var->data.interpolation = INTERP_MODE_FLAT;


   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               lower_load_face(&b, instr, var);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}

static void
lower_pos_read(nir_builder *b, struct nir_instr *instr,
               nir_variable **depth_transform_var)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_deref)
      return;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_in ||
       var->data.location != VARYING_SLOT_POS)
      return;

   b->cursor = nir_after_instr(instr);

   nir_ssa_def *pos = nir_instr_ssa_def(instr);
   nir_ssa_def *depth = nir_channel(b, pos, 2);

   assert(depth_transform_var);
   nir_ssa_def *depth_transform = get_state_var(b, D3D12_STATE_VAR_DEPTH_TRANSFORM,
                                                "d3d12_DepthTransform",
                                                glsl_vec_type(2),
                                                depth_transform_var);
   depth = nir_fmad(b, depth, nir_channel(b, depth_transform, 0),
                              nir_channel(b, depth_transform, 1));

   pos = nir_vector_insert_imm(b, pos, depth, 2);

   assert(intr->dest.is_ssa);
   nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, pos,
                                  pos->parent_instr);
}

void
d3d12_lower_depth_range(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);
   nir_variable *depth_transform = NULL;
   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               lower_pos_read(&b, instr, &depth_transform);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}

static bool
is_color_output(nir_variable *var)
{
   return (var->data.mode == nir_var_shader_out &&
           (var->data.location == FRAG_RESULT_COLOR ||
            var->data.location >= FRAG_RESULT_DATA0));
}

static void
lower_uint_color_write(nir_builder *b, struct nir_instr *instr, bool is_signed)
{
   const unsigned NUM_BITS = 8;
   const unsigned bits[4] = { NUM_BITS, NUM_BITS, NUM_BITS, NUM_BITS };

   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (!is_color_output(var))
      return;

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *col = nir_ssa_for_src(b, intr->src[1], intr->num_components);
   nir_ssa_def *def = is_signed ? nir_format_float_to_snorm(b, col, bits) :
                                  nir_format_float_to_unorm(b, col, bits);
   if (is_signed)
      def = nir_bcsel(b, nir_ilt(b, def, nir_imm_int(b, 0)),
                      nir_iadd(b, def, nir_imm_int(b, 1 << NUM_BITS)),
                      def);
   nir_instr_rewrite_src(&intr->instr, intr->src + 1, nir_src_for_ssa(def));
}

void
d3d12_lower_uint_cast(nir_shader *nir, bool is_signed)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT)
      return;

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               lower_uint_color_write(&b, instr, is_signed);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}

static bool
lower_load_first_vertex(nir_builder *b, nir_instr *instr, nir_variable **first_vertex)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_load_first_vertex)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *load = get_state_var(b, D3D12_STATE_VAR_FIRST_VERTEX, "d3d12_FirstVertex",
                                     glsl_uint_type(), first_vertex);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, load);
   nir_instr_remove(instr);

   return true;
}

bool
d3d12_lower_load_first_vertex(struct nir_shader *nir)
{
   nir_variable *first_vertex = NULL;
   bool progress = false;

   if (nir->info.stage != MESA_SHADER_VERTEX)
      return false;

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               progress |= lower_load_first_vertex(&b, instr, &first_vertex);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
   return progress;
}

static void
invert_depth(nir_builder *b, struct nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_POS)
      return;

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *pos = nir_ssa_for_src(b, intr->src[1], 4);
   nir_ssa_def *def = nir_vec4(b,
                               nir_channel(b, pos, 0),
                               nir_channel(b, pos, 1),
                               nir_fneg(b, nir_channel(b, pos, 2)),
                               nir_channel(b, pos, 3));
   nir_instr_rewrite_src(&intr->instr, intr->src + 1, nir_src_for_ssa(def));
}

/* In OpenGL the windows space depth value z_w is evaluated according to "s * z_d + b"
 * with  "s + (far - near) / 2" (depth clip:minus_one_to_one) [OpenGL 3.3, 2.13.1].
 * When we switch the far and near value to satisfy DirectX requirements we have
 * to compensate by inverting "z_d' = -z_d" with this lowering pass.
 */
void
d3d12_nir_invert_depth(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX &&
       shader->info.stage != MESA_SHADER_GEOMETRY)
      return;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               invert_depth(&b, instr);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}


/**
 * Lower State Vars:
 *
 * All uniforms related to internal D3D12 variables are
 * condensed into a UBO that is appended at the end of the
 * current ones.
 */

static unsigned
get_state_var_offset(struct d3d12_shader *shader, enum d3d12_state_var var)
{
   for (unsigned i = 0; i < shader->num_state_vars; ++i) {
      if (shader->state_vars[i].var == var)
         return shader->state_vars[i].offset;
   }

   unsigned offset = shader->state_vars_size;
   shader->state_vars[shader->num_state_vars].offset = offset;
   shader->state_vars[shader->num_state_vars].var = var;
   shader->state_vars_size += 4; /* Use 4-words slots no matter the variable size */
   shader->num_state_vars++;

   return offset;
}

static bool
lower_instr(nir_intrinsic_instr *instr, nir_builder *b,
            struct d3d12_shader *shader, unsigned binding)
{
   nir_variable *variable = NULL;
   nir_deref_instr *deref = NULL;

   b->cursor = nir_before_instr(&instr->instr);

   if (instr->intrinsic == nir_intrinsic_load_uniform) {
      nir_foreach_variable_with_modes(var, b->shader, nir_var_uniform) {
         if (var->data.driver_location == nir_intrinsic_base(instr)) {
            variable = var;
            break;
         }
      }
   } else if (instr->intrinsic == nir_intrinsic_load_deref) {
      deref = nir_src_as_deref(instr->src[0]);
      variable = nir_intrinsic_get_var(instr, 0);
   }

   if (variable == NULL ||
       variable->num_state_slots != 1 ||
       variable->state_slots[0].tokens[0] != STATE_INTERNAL_DRIVER)
      return false;

   enum d3d12_state_var var = variable->state_slots[0].tokens[1];
   nir_ssa_def *ubo_idx = nir_imm_int(b, binding);
   nir_ssa_def *ubo_offset =  nir_imm_int(b, get_state_var_offset(shader, var) * 4);
   nir_ssa_def *load =
      nir_load_ubo(b, instr->num_components, instr->dest.ssa.bit_size,
                   ubo_idx, ubo_offset,
                   .align_mul = instr->dest.ssa.bit_size / 8,
                   .align_offset = 0,
                   .range_base = 0,
                   .range = ~0,
                   );

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, load);

   /* Remove the old load_* instruction and any parent derefs */
   nir_instr_remove(&instr->instr);
   for (nir_deref_instr *d = deref; d; d = nir_deref_instr_parent(d)) {
      /* If anyone is using this deref, leave it alone */
      assert(d->dest.is_ssa);
      if (!list_is_empty(&d->dest.ssa.uses))
         break;

      nir_instr_remove(&d->instr);
   }

   return true;
}

bool
d3d12_lower_state_vars(nir_shader *nir, struct d3d12_shader *shader)
{
   bool progress = false;

   /* The state var UBO is added after all the other UBOs if it already
    * exists it will be replaced by using the same binding.
    * In the event there are no other UBO's, use binding slot 1 to
    * be consistent with other non-default UBO's */
   unsigned binding = MAX2(nir->info.num_ubos, 1);

   nir_foreach_variable_with_modes_safe(var, nir, nir_var_uniform) {
      if (var->num_state_slots == 1 &&
          var->state_slots[0].tokens[0] == STATE_INTERNAL_DRIVER) {
         if (var->data.mode == nir_var_mem_ubo) {
            binding = var->data.binding;
         }
      }
   }

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_instr(nir_instr_as_intrinsic(instr),
                                          &builder,
                                          shader,
                                          binding);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   if (progress) {
      assert(shader->num_state_vars > 0);

      shader->state_vars_used = true;

      /* Remove state variables */
      nir_foreach_variable_with_modes_safe(var, nir, nir_var_uniform) {
         if (var->num_state_slots == 1 &&
             var->state_slots[0].tokens[0] == STATE_INTERNAL_DRIVER) {
            exec_node_remove(&var->node);
            nir->num_uniforms--;
         }
      }

      const gl_state_index16 tokens[STATE_LENGTH] = { STATE_INTERNAL_DRIVER };
      const struct glsl_type *type = glsl_array_type(glsl_vec4_type(),
                                                     shader->state_vars_size / 4, 0);
      nir_variable *ubo = nir_variable_create(nir, nir_var_mem_ubo, type,
                                                  "d3d12_state_vars");
      if (binding >= nir->info.num_ubos)
         nir->info.num_ubos = binding + 1;
      ubo->data.binding = binding;
      ubo->num_state_slots = 1;
      ubo->state_slots = ralloc_array(ubo, nir_state_slot, 1);
      memcpy(ubo->state_slots[0].tokens, tokens,
              sizeof(ubo->state_slots[0].tokens));

      struct glsl_struct_field field = {
          .type = type,
          .name = "data",
          .location = -1,
      };
      ubo->interface_type =
              glsl_interface_type(&field, 1, GLSL_INTERFACE_PACKING_STD430,
                                  false, "__d3d12_state_vars_interface");
   }

   return progress;
}

static bool
lower_bool_input_filter(const nir_instr *instr,
                        UNUSED const void *_options)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic == nir_intrinsic_load_front_face)
      return true;

   if (intr->intrinsic == nir_intrinsic_load_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
      nir_variable *var = nir_deref_instr_get_variable(deref);
      return var->data.mode == nir_var_shader_in &&
             glsl_get_base_type(var->type) == GLSL_TYPE_BOOL;
   }

   return false;
}

static nir_ssa_def *
lower_bool_input_impl(nir_builder *b, nir_instr *instr,
                      UNUSED void *_options)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic == nir_intrinsic_load_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
      nir_variable *var = nir_deref_instr_get_variable(deref);

      /* rewrite var->type */
      var->type = glsl_vector_type(GLSL_TYPE_UINT,
                                   glsl_get_vector_elements(var->type));
      deref->type = var->type;
   }

   intr->dest.ssa.bit_size = 32;
   return nir_i2b1(b, &intr->dest.ssa);
}

bool
d3d12_lower_bool_input(struct nir_shader *s)
{
   return nir_shader_lower_instructions(s, lower_bool_input_filter,
                                        lower_bool_input_impl, NULL);
}

void
d3d12_add_missing_dual_src_target(struct nir_shader *s,
                                  unsigned missing_mask)
{
   assert(missing_mask != 0);
   nir_builder b;
   nir_function_impl *impl = nir_shader_get_entrypoint(s);
   nir_builder_init(&b, impl);
   b.cursor = nir_before_cf_list(&impl->body);

   nir_ssa_def *zero = nir_imm_zero(&b, 4, 32);
   for (unsigned i = 0; i < 2; ++i) {

      if (!(missing_mask & (1u << i)))
         continue;

      const char *name = i == 0 ? "gl_FragData[0]" :
                                  "gl_SecondaryFragDataEXT[0]";
      nir_variable *out = nir_variable_create(s, nir_var_shader_out,
                                              glsl_vec4_type(), name);
      out->data.location = FRAG_RESULT_DATA0;
      out->data.driver_location = i;
      out->data.index = i;

      nir_store_var(&b, out, zero, 0xf);
   }
   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

static bool
fix_io_uint_type(struct nir_shader *s, nir_variable_mode modes, int slot)
{
   nir_variable *fixed_var = NULL;
   nir_foreach_variable_with_modes(var, s, modes) {
      if (var->data.location == slot) {
         var->type = glsl_uint_type();
         fixed_var = var;
         break;
      }
   }

   assert(fixed_var);

   nir_foreach_function(function, s) {
      if (function->impl) {
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_deref) {
                  nir_deref_instr *deref = nir_instr_as_deref(instr);
                  if (deref->var == fixed_var)
                     deref->type = fixed_var->type;
               }
            }
         }
      }
   }
   return true;
}

bool
d3d12_fix_io_uint_type(struct nir_shader *s, uint64_t in_mask, uint64_t out_mask)
{
   if (!(s->info.outputs_written & out_mask) &&
       !(s->info.inputs_read & in_mask))
      return false;

   bool progress = false;

   while (in_mask) {
      int slot = u_bit_scan64(&in_mask);
      progress |= (s->info.inputs_read & (1ull << slot)) &&
                  fix_io_uint_type(s, nir_var_shader_in, slot);
   }

   while (out_mask) {
      int slot = u_bit_scan64(&out_mask);
      progress |= (s->info.outputs_written & (1ull << slot)) &&
                  fix_io_uint_type(s, nir_var_shader_out, slot);
   }

   return progress;
}

static bool
lower_load_ubo_packed_filter(const nir_instr *instr,
                             UNUSED const void *_options) {
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   return intr->intrinsic == nir_intrinsic_load_ubo;
}

static nir_ssa_def *
lower_load_ubo_packed_impl(nir_builder *b, nir_instr *instr,
                              UNUSED void *_options) {
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   nir_ssa_def *buffer = intr->src[0].ssa;
   nir_ssa_def *offset = intr->src[1].ssa;

   nir_ssa_def *result =
      build_load_ubo_dxil(b, buffer,
                          offset,
                          nir_dest_num_components(intr->dest),
                          nir_dest_bit_size(intr->dest));
   return result;
}

bool
nir_lower_packed_ubo_loads(nir_shader *nir) {
   return nir_shader_lower_instructions(nir,
                                        lower_load_ubo_packed_filter,
                                        lower_load_ubo_packed_impl,
                                        NULL);
}

void
d3d12_lower_primitive_id(nir_shader *shader)
{
   nir_builder b;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_ssa_def *primitive_id;
   nir_builder_init(&b, impl);

   nir_variable *primitive_id_var = nir_variable_create(shader, nir_var_shader_out,
                                                        glsl_uint_type(), "primitive_id");
   primitive_id_var->data.location = VARYING_SLOT_PRIMITIVE_ID;
   primitive_id_var->data.interpolation = INTERP_MODE_FLAT;

   nir_foreach_block(block, impl) {
      b.cursor = nir_before_block(block);
      primitive_id = nir_load_primitive_id(&b);

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic ||
             nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_emit_vertex)
            continue;

         b.cursor = nir_before_instr(instr);
         nir_store_var(&b, primitive_id_var, primitive_id, 0x1);
      }
   }

   nir_metadata_preserve(impl, nir_metadata_none);
}

static void
lower_triangle_strip_store(nir_builder *b, nir_intrinsic_instr *intr,
                           nir_variable *vertex_count_var,
                           nir_variable **varyings)
{
   /**
    * tmp_varying[slot][min(vertex_count, 2)] = src
    */
   nir_ssa_def *vertex_count = nir_load_var(b, vertex_count_var);
   nir_ssa_def *index = nir_imin(b, vertex_count, nir_imm_int(b, 2));
   nir_variable *var = nir_intrinsic_get_var(intr, 0);

   if (var->data.mode != nir_var_shader_out)
      return;

   nir_deref_instr *deref = nir_build_deref_array(b, nir_build_deref_var(b, varyings[var->data.location]), index);
   nir_ssa_def *value = nir_ssa_for_src(b, intr->src[1], intr->num_components);
   nir_store_deref(b, deref, value, 0xf);
   nir_instr_remove(&intr->instr);
}

static void
lower_triangle_strip_emit_vertex(nir_builder *b, nir_intrinsic_instr *intr,
                                 nir_variable *vertex_count_var,
                                 nir_variable **varyings,
                                 nir_variable **out_varyings)
{
   // TODO xfb + flat shading + last_pv
   /**
    * if (vertex_count >= 2) {
    *    for (i = 0; i < 3; i++) {
    *       foreach(slot)
    *          out[slot] = tmp_varying[slot][i];
    *       EmitVertex();
    *    }
    *    EndPrimitive();
    *    foreach(slot)
    *       tmp_varying[slot][vertex_count % 2] = tmp_varying[slot][2];
    * }
    * vertex_count++;
    */

   nir_ssa_def *two = nir_imm_int(b, 2);
   nir_ssa_def *vertex_count = nir_load_var(b, vertex_count_var);
   nir_ssa_def *count_cmp = nir_uge(b, vertex_count, two);
   nir_if *count_check = nir_push_if(b, count_cmp);

   for (int j = 0; j < 3; ++j) {
      for (int i = 0; i < VARYING_SLOT_MAX; ++i) {
         if (!varyings[i])
            continue;
         nir_copy_deref(b, nir_build_deref_var(b, out_varyings[i]),
                        nir_build_deref_array_imm(b, nir_build_deref_var(b, varyings[i]), j));
      }
      nir_emit_vertex(b, 0);
   }

   for (int i = 0; i < VARYING_SLOT_MAX; ++i) {
      if (!varyings[i])
         continue;
      nir_copy_deref(b, nir_build_deref_array(b, nir_build_deref_var(b, varyings[i]), nir_umod(b, vertex_count, two)),
                        nir_build_deref_array(b, nir_build_deref_var(b, varyings[i]), two));
   }

   nir_end_primitive(b, .stream_id = 0);

   nir_pop_if(b, count_check);

   vertex_count = nir_iadd(b, vertex_count, nir_imm_int(b, 1));
   nir_store_var(b, vertex_count_var, vertex_count, 0x1);

   nir_instr_remove(&intr->instr);
}

static void
lower_triangle_strip_end_primitive(nir_builder *b, nir_intrinsic_instr *intr,
                                   nir_variable *vertex_count_var)
{
   /**
    * vertex_count = 0;
    */
   nir_store_var(b, vertex_count_var, nir_imm_int(b, 0), 0x1);
   nir_instr_remove(&intr->instr);
}

void
d3d12_lower_triangle_strip(nir_shader *shader)
{
   nir_builder b;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_variable *tmp_vars[VARYING_SLOT_MAX] = {0};
   nir_variable *out_vars[VARYING_SLOT_MAX] = {0};
   nir_builder_init(&b, impl);

   shader->info.gs.vertices_out = (shader->info.gs.vertices_out - 2) * 3;

   nir_variable *vertex_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "vertex_count");

   nir_block *first = nir_start_block(impl);
   b.cursor = nir_before_block(first);
   nir_foreach_variable_with_modes(var, shader, nir_var_shader_out) {
      const struct glsl_type *type = glsl_array_type(var->type, 3, 0);
      tmp_vars[var->data.location] =  nir_local_variable_create(impl, type, "tmp_var");
      out_vars[var->data.location] = var;
   }
   nir_store_var(&b, vertex_count_var, nir_imm_int(&b, 0), 1);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_store_deref:
            b.cursor = nir_before_instr(instr);
            lower_triangle_strip_store(&b, intrin, vertex_count_var, tmp_vars);
            break;
         case nir_intrinsic_emit_vertex_with_counter:
         case nir_intrinsic_emit_vertex:
            b.cursor = nir_before_instr(instr);
            lower_triangle_strip_emit_vertex(&b, intrin, vertex_count_var,
                                             tmp_vars, out_vars);
            break;
         case nir_intrinsic_end_primitive:
         case nir_intrinsic_end_primitive_with_counter:
            b.cursor = nir_before_instr(instr);
            lower_triangle_strip_end_primitive(&b, intrin, vertex_count_var);
            break;
         default:
            break;
         }
      }
   }

   nir_metadata_preserve(impl, nir_metadata_none);
   NIR_PASS_V(shader, nir_lower_var_copies);
}
