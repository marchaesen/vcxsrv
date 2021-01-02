/*
 * Copyright (c) 2019 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "etnaviv_nir.h"

/* io related lowering
 * run after lower_int_to_float because it adds i2f/f2i ops
 */
void
etna_lower_io(nir_shader *shader, struct etna_shader_variant *v)
{
   nir_foreach_function(function, shader) {
      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               switch (intr->intrinsic) {
               case nir_intrinsic_load_front_face: {
                  /* HW front_face is 0.0/1.0, not 0/~0u for bool
                   * lower with a comparison with 0
                   */
                  intr->dest.ssa.bit_size = 32;

                  b.cursor = nir_after_instr(instr);

                  nir_ssa_def *ssa = nir_ine(&b, &intr->dest.ssa, nir_imm_int(&b, 0));
                  if (v->key.front_ccw)
                     nir_instr_as_alu(ssa->parent_instr)->op = nir_op_ieq;

                  nir_ssa_def_rewrite_uses_after(&intr->dest.ssa,
                                                 nir_src_for_ssa(ssa),
                                                 ssa->parent_instr);
               } break;
               case nir_intrinsic_store_deref: {
                  nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
                  if (shader->info.stage != MESA_SHADER_FRAGMENT || !v->key.frag_rb_swap)
                     break;

                  assert(deref->deref_type == nir_deref_type_var);

                  if (deref->var->data.location != FRAG_RESULT_COLOR &&
                      deref->var->data.location != FRAG_RESULT_DATA0)
                      break;

                  b.cursor = nir_before_instr(instr);

                  nir_ssa_def *ssa = nir_mov(&b, intr->src[1].ssa);
                  nir_alu_instr *alu = nir_instr_as_alu(ssa->parent_instr);
                  alu->src[0].swizzle[0] = 2;
                  alu->src[0].swizzle[2] = 0;
                  nir_instr_rewrite_src(instr, &intr->src[1], nir_src_for_ssa(ssa));
               } break;
               case nir_intrinsic_load_uniform: {
                  /* convert indirect load_uniform to load_ubo when possible
                   * this is required on HALTI5+ because address register is not implemented
                   * address register loads also arent done optimally
                   */
                  if (v->shader->specs->halti < 2 || nir_src_is_const(intr->src[0]))
                     break;

                  nir_intrinsic_instr *load_ubo =
                     nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ubo);
                  load_ubo->num_components = intr->num_components;
                  nir_intrinsic_set_align(load_ubo, intr->dest.ssa.bit_size / 8, 0);
                  nir_ssa_dest_init(&load_ubo->instr, &load_ubo->dest,
                                    load_ubo->num_components, 32, NULL);

                  b.cursor = nir_before_instr(instr);
                  load_ubo->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
                  load_ubo->src[1] = nir_src_for_ssa(nir_iadd(&b,
                     nir_imul(&b, intr->src[0].ssa, nir_imm_int(&b, 16)),
                     nir_imm_int(&b, nir_intrinsic_base(intr) * 16)));
                  nir_builder_instr_insert(&b, &load_ubo->instr);
                  nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                                             nir_src_for_ssa(&load_ubo->dest.ssa));
                  nir_instr_remove(&intr->instr);
               } break;
               case nir_intrinsic_load_ubo: {
                  nir_const_value *idx = nir_src_as_const_value(intr->src[0]);
                  assert(idx);
                  /* offset index by 1, index 0 is used for converted load_uniform */
                  b.cursor = nir_before_instr(instr);
                  nir_instr_rewrite_src(instr, &intr->src[0],
                                        nir_src_for_ssa(nir_imm_int(&b, idx[0].u32 + 1)));
               } break;
               case nir_intrinsic_load_vertex_id:
               case nir_intrinsic_load_instance_id:
                  /* detect use of vertex_id/instance_id */
                  v->vs_id_in_reg = v->infile.num_reg;
                  break;
               default:
                  break;
               }
            }

            if (instr->type != nir_instr_type_tex)
               continue;

            nir_tex_instr *tex = nir_instr_as_tex(instr);
            nir_src *coord = NULL;
            nir_src *lod_bias = NULL;
            unsigned lod_bias_idx;

            assert(tex->sampler_index == tex->texture_index);

            for (unsigned i = 0; i < tex->num_srcs; i++) {
               switch (tex->src[i].src_type) {
               case nir_tex_src_coord:
                  coord = &tex->src[i].src;
                  break;
               case nir_tex_src_bias:
               case nir_tex_src_lod:
                  assert(!lod_bias);
                  lod_bias = &tex->src[i].src;
                  lod_bias_idx = i;
                  break;
               case nir_tex_src_comparator:
                  break;
               default:
                  assert(0);
                  break;
               }
            }

            if (tex->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
               /* use a dummy load_uniform here to represent texcoord scale */
               b.cursor = nir_before_instr(instr);
               nir_intrinsic_instr *load =
                  nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_uniform);
               nir_intrinsic_set_base(load, ~tex->sampler_index);
               load->num_components = 2;
               load->src[0] = nir_src_for_ssa(nir_imm_float(&b, 0.0f));
               nir_ssa_dest_init(&load->instr, &load->dest, 2, 32, NULL);
               nir_intrinsic_set_dest_type(load, nir_type_float);

               nir_builder_instr_insert(&b, &load->instr);

               nir_ssa_def *new_coord = nir_fmul(&b, coord->ssa, &load->dest.ssa);
               nir_instr_rewrite_src(&tex->instr, coord, nir_src_for_ssa(new_coord));
            }

            /* pre HALTI5 needs texture sources in a single source */

            if (!lod_bias || v->shader->specs->halti >= 5)
               continue;

            assert(coord && lod_bias && tex->coord_components < 4);

            nir_alu_instr *vec = nir_alu_instr_create(shader, nir_op_vec4);
            for (unsigned i = 0; i < tex->coord_components; i++) {
               vec->src[i].src = nir_src_for_ssa(coord->ssa);
               vec->src[i].swizzle[0] = i;
            }
            for (unsigned i = tex->coord_components; i < 4; i++)
               vec->src[i].src = nir_src_for_ssa(lod_bias->ssa);

            vec->dest.write_mask = 0xf;
            nir_ssa_dest_init(&vec->instr, &vec->dest.dest, 4, 32, NULL);

            nir_tex_instr_remove_src(tex, lod_bias_idx);
            nir_instr_rewrite_src(&tex->instr, coord, nir_src_for_ssa(&vec->dest.dest.ssa));
            tex->coord_components = 4;

            nir_instr_insert_before(&tex->instr, &vec->instr);
         }
      }
   }
}

static void
etna_lower_alu_impl(nir_function_impl *impl, bool has_new_transcendentals)
{
   nir_shader *shader = impl->function->shader;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* in a seperate loop so we can apply the multiple-uniform logic to the new fmul */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu = nir_instr_as_alu(instr);
         /* multiply sin/cos src by constant
          * TODO: do this earlier (but it breaks const_prop opt)
          */
         if (alu->op == nir_op_fsin || alu->op == nir_op_fcos) {
            b.cursor = nir_before_instr(instr);

            nir_ssa_def *imm = has_new_transcendentals ?
               nir_imm_float(&b, 1.0 / M_PI) :
               nir_imm_float(&b, 2.0 / M_PI);

            nir_instr_rewrite_src(instr, &alu->src[0].src,
               nir_src_for_ssa(nir_fmul(&b, alu->src[0].src.ssa, imm)));
         }

         /* change transcendental ops to vec2 and insert vec1 mul for the result
          * TODO: do this earlier (but it breaks with optimizations)
          */
         if (has_new_transcendentals && (
             alu->op == nir_op_fdiv || alu->op == nir_op_flog2 ||
             alu->op == nir_op_fsin || alu->op == nir_op_fcos)) {
            nir_ssa_def *ssa = &alu->dest.dest.ssa;

            assert(ssa->num_components == 1);

            nir_alu_instr *mul = nir_alu_instr_create(shader, nir_op_fmul);
            mul->src[0].src = mul->src[1].src = nir_src_for_ssa(ssa);
            mul->src[1].swizzle[0] = 1;

            mul->dest.write_mask = 1;
            nir_ssa_dest_init(&mul->instr, &mul->dest.dest, 1, 32, NULL);

            ssa->num_components = 2;

            mul->dest.saturate = alu->dest.saturate;
            alu->dest.saturate = 0;

            nir_instr_insert_after(instr, &mul->instr);

            nir_ssa_def_rewrite_uses_after(ssa, nir_src_for_ssa(&mul->dest.dest.ssa), &mul->instr);
         }
      }
   }
}

void
etna_lower_alu(nir_shader *shader, bool has_new_transcendentals)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         etna_lower_alu_impl(function->impl, has_new_transcendentals);
   }
}
