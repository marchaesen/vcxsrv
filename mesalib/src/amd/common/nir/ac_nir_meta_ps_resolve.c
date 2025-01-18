/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir_meta.h"
#include "ac_nir_helpers.h"
#include "nir_builder.h"
#include "compiler/aco_interface.h"

static nir_def *
build_tex_load_ms(nir_builder *b, unsigned num_components, unsigned bit_size,
                  nir_deref_instr *tex_deref, nir_def *coord, nir_def *sample_index)
{
   nir_tex_src srcs[] = {
      nir_tex_src_for_ssa(nir_tex_src_coord, coord),
      nir_tex_src_for_ssa(nir_tex_src_ms_index, sample_index),
   };
   nir_def *result = nir_build_tex_deref_instr(b, nir_texop_txf_ms, tex_deref, tex_deref,
                                               ARRAY_SIZE(srcs), srcs);

   nir_tex_instr *tex = nir_instr_as_tex(result->parent_instr);

   assert(bit_size == 32 || bit_size == 16);
   if (bit_size == 16) {
      tex->dest_type = nir_type_float16;
      tex->def.bit_size = 16;
   }
   return nir_trim_vector(b, result, num_components);
}

nir_shader *
ac_create_resolve_ps(const struct ac_ps_resolve_options *options,
                     const union ac_ps_resolve_key *key)
{
   if (options->print_key) {
      fprintf(stderr, "Internal shader: resolve_ps\n");
      fprintf(stderr, "   key.use_aco = %u\n", key->use_aco);
      fprintf(stderr, "   key.src_is_array = %u\n", key->src_is_array);
      fprintf(stderr, "   key.log_samples = %u\n", key->log_samples);
      fprintf(stderr, "   key.last_src_channel = %u\n", key->last_src_channel);
      fprintf(stderr, "   key.x_clamp_to_edge = %u\n", key->x_clamp_to_edge);
      fprintf(stderr, "   key.y_clamp_to_edge = %u\n", key->y_clamp_to_edge);
      fprintf(stderr, "   key.d16 = %u\n", key->d16);
      fprintf(stderr, "   key.a16 = %u\n", key->a16);
      fprintf(stderr, "\n");
   }

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options->nir_options, "ac_resolve_ps");
   b.shader->info.use_aco_amd = options->use_aco ||
                                (key->use_aco && aco_is_gpu_supported(options->info));
   BITSET_SET(b.shader->info.textures_used, 1);

   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_MS, /*shadow*/ false, /*is_array*/ key->src_is_array,
                        GLSL_TYPE_FLOAT);
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "samp0");
   sampler->data.binding = 0;

   nir_deref_instr *deref = nir_build_deref_var(&b, sampler);
   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *baryc = nir_load_barycentric_pixel(&b, 32, .interp_mode = INTERP_MODE_SMOOTH);
   nir_def *coord = nir_load_interpolated_input(&b, 2 + key->src_is_array, 32, baryc, zero,
                                                .dest_type = nir_type_float32,
                                                .io_semantics = (nir_io_semantics){
                                                                .location = VARYING_SLOT_VAR0,
                                                                .num_slots = 1});

   /* Nearest filtering floors and then converts to integer, and then
    * applies clamp to edge as clamp(coord, 0, dim - 1).
    */
   coord = nir_vector_insert_imm(&b, coord, nir_ffloor(&b, nir_channel(&b, coord, 0)), 0);
   coord = nir_vector_insert_imm(&b, coord, nir_ffloor(&b, nir_channel(&b, coord, 1)), 1);
   coord = nir_f2iN(&b, coord, key->a16 ? 16 : 32);

   /* Clamp to edge only for X and Y because Z can't be out of bounds. */
   nir_def *resinfo = NULL;
   for (unsigned chan = 0; chan < 2; chan++) {
      if (chan ? key->y_clamp_to_edge : key->x_clamp_to_edge) {
         if (!resinfo) {
            resinfo = nir_build_tex_deref_instr(&b, nir_texop_txs, deref, deref, 0, NULL);

            if (key->a16) {
               resinfo = nir_umin_imm(&b, resinfo, INT16_MAX);
               resinfo = nir_i2i16(&b, resinfo);
            }
         }

         nir_def *tmp = nir_channel(&b, coord, chan);
         tmp = nir_imax_imm(&b, tmp, 0);
         tmp = nir_imin(&b, tmp, nir_iadd_imm(&b, nir_channel(&b, resinfo, chan), -1));
         coord = nir_vector_insert_imm(&b, coord, tmp, chan);
      }
   }

   /* Use samples_identical if it's supported. */
   bool uses_samples_identical = options->info->gfx_level < GFX11 && !options->no_fmask;
   nir_def *sample0 = NULL;
   nir_if *if_identical = NULL;

   assert(key->last_src_channel <= key->last_dst_channel);

   if (uses_samples_identical) {
      nir_tex_src iden_srcs[] = {
         nir_tex_src_for_ssa(nir_tex_src_coord, coord),
      };
      nir_def *samples_identical =
         nir_build_tex_deref_instr(&b, nir_texop_samples_identical, deref, deref,
                                   ARRAY_SIZE(iden_srcs), iden_srcs);

      /* If all samples are identical, load only sample 0. */
      if_identical = nir_push_if(&b, samples_identical);
      {
         sample0 = build_tex_load_ms(&b, key->last_src_channel + 1, key->d16 ? 16 : 32,
                                     deref, coord, nir_imm_intN_t(&b, 0, coord->bit_size));
      }
      nir_push_else(&b, if_identical);
   }

   /* Insert the sample index into the coordinates. */
   unsigned num_src_coords = 2 + key->src_is_array + 1;
   unsigned num_samples = 1 << key->log_samples;
   nir_def *coord_src[16] = {0};

   for (unsigned i = 0; i < num_samples; i++) {
      coord_src[i] = nir_pad_vector(&b, coord, num_src_coords);
      coord_src[i] = nir_vector_insert_imm(&b, coord_src[i],
                                           nir_imm_intN_t(&b, i, coord->bit_size),
                                           num_src_coords - 1);
   }

   /* We need this because LLVM interleaves coordinate computations with image loads, which breaks
    * VMEM clauses.
    */
   ac_optimization_barrier_vgpr_array(options->info, &b, coord_src, num_samples, num_src_coords);

   nir_def *samples[16] = {0};
   for (unsigned i = 0; i < num_samples; i++) {
      samples[i] = build_tex_load_ms(&b, key->last_src_channel + 1, key->d16 ? 16 : 32,
                                     deref, nir_trim_vector(&b, coord_src[i], num_src_coords - 1),
                                     nir_channel(&b, coord_src[i], num_src_coords - 1));
   }
   nir_def *result = ac_average_samples(&b, samples, num_samples);

   if (uses_samples_identical) {
      nir_pop_if(&b, if_identical);
      result = nir_if_phi(&b, sample0, result);
   }

   result = nir_pad_vector(&b, result, key->last_dst_channel + 1);
   for (unsigned i = key->last_src_channel + 1; i <= key->last_dst_channel; i++) {
      result = nir_vector_insert_imm(&b, result,
                                     nir_imm_floatN_t(&b, i == 3 ? 1 : 0, result->bit_size), i);
   }

   nir_store_output(&b, result, zero,
                    .write_mask = BITFIELD_MASK(key->last_dst_channel + 1),
                    .src_type = key->d16 ? nir_type_float16 : nir_type_float32,
                    .io_semantics = (nir_io_semantics){
                                    .location = FRAG_RESULT_DATA0,
                                    .num_slots = 1});

   return b.shader;
}
