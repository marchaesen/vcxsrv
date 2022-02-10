/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

#include "si_shader.h"
#include "util/mesa-sha1.h"


struct si_shader_profile {
   uint32_t sha1[SHA1_DIGEST_LENGTH32];
   uint32_t options;
};

static struct si_shader_profile profiles[] =
{
   {
      /* Plot3D */
      {0x485320cd, 0x87a9ba05, 0x24a60e4f, 0x25aa19f7, 0xf5287451},
      SI_PROFILE_VS_NO_BINNING,
   },
   {
      /* Viewperf/Energy isn't affected by the discard bug. */
      {0x17118671, 0xd0102e0c, 0x947f3592, 0xb2057e7b, 0x4da5d9b0},
      SI_PROFILE_IGNORE_LLVM_DISCARD_BUG,
   },
   {
      /* Viewperf/Medical */
      {0x4dce4331, 0x38f778d5, 0x1b75a717, 0x3e454fb9, 0xeb1527f0},
      SI_PROFILE_PS_NO_BINNING,
   },
   {
      /* Viewperf/Medical, a shader with a divergent loop doesn't benefit from Wave32,
       * probably due to interpolation performance.
       */
      {0x29f0f4a0, 0x0672258d, 0x47ccdcfd, 0x31e67dcc, 0xdcb1fda8},
      SI_PROFILE_WAVE64,
   },
};

static unsigned get_inst_tessfactor_writemask(nir_intrinsic_instr *intrin)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return 0;

   unsigned writemask = nir_intrinsic_write_mask(intrin) << nir_intrinsic_component(intrin);
   unsigned location = nir_intrinsic_io_semantics(intrin).location;

   if (location == VARYING_SLOT_TESS_LEVEL_OUTER)
      return writemask << 4;
   else if (location == VARYING_SLOT_TESS_LEVEL_INNER)
      return writemask;

   return 0;
}

static void scan_tess_ctrl(nir_cf_node *cf_node, unsigned *upper_block_tf_writemask,
                           unsigned *cond_block_tf_writemask,
                           bool *tessfactors_are_def_in_all_invocs, bool is_nested_cf)
{
   switch (cf_node->type) {
   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(cf_node);
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == nir_intrinsic_control_barrier) {

            /* If we find a barrier in nested control flow put this in the
             * too hard basket. In GLSL this is not possible but it is in
             * SPIR-V.
             */
            if (is_nested_cf) {
               *tessfactors_are_def_in_all_invocs = false;
               return;
            }

            /* The following case must be prevented:
             *    gl_TessLevelInner = ...;
             *    barrier();
             *    if (gl_InvocationID == 1)
             *       gl_TessLevelInner = ...;
             *
             * If you consider disjoint code segments separated by barriers, each
             * such segment that writes tess factor channels should write the same
             * channels in all codepaths within that segment.
             */
            if (*upper_block_tf_writemask || *cond_block_tf_writemask) {
               /* Accumulate the result: */
               *tessfactors_are_def_in_all_invocs &=
                  !(*cond_block_tf_writemask & ~(*upper_block_tf_writemask));

               /* Analyze the next code segment from scratch. */
               *upper_block_tf_writemask = 0;
               *cond_block_tf_writemask = 0;
            }
         } else
            *upper_block_tf_writemask |= get_inst_tessfactor_writemask(intrin);
      }

      break;
   }
   case nir_cf_node_if: {
      unsigned then_tessfactor_writemask = 0;
      unsigned else_tessfactor_writemask = 0;

      nir_if *if_stmt = nir_cf_node_as_if(cf_node);
      foreach_list_typed(nir_cf_node, nested_node, node, &if_stmt->then_list)
      {
         scan_tess_ctrl(nested_node, &then_tessfactor_writemask, cond_block_tf_writemask,
                        tessfactors_are_def_in_all_invocs, true);
      }

      foreach_list_typed(nir_cf_node, nested_node, node, &if_stmt->else_list)
      {
         scan_tess_ctrl(nested_node, &else_tessfactor_writemask, cond_block_tf_writemask,
                        tessfactors_are_def_in_all_invocs, true);
      }

      if (then_tessfactor_writemask || else_tessfactor_writemask) {
         /* If both statements write the same tess factor channels,
          * we can say that the upper block writes them too.
          */
         *upper_block_tf_writemask |= then_tessfactor_writemask & else_tessfactor_writemask;
         *cond_block_tf_writemask |= then_tessfactor_writemask | else_tessfactor_writemask;
      }

      break;
   }
   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);
      foreach_list_typed(nir_cf_node, nested_node, node, &loop->body)
      {
         scan_tess_ctrl(nested_node, cond_block_tf_writemask, cond_block_tf_writemask,
                        tessfactors_are_def_in_all_invocs, true);
      }

      break;
   }
   default:
      unreachable("unknown cf node type");
   }
}

static bool are_tessfactors_def_in_all_invocs(const struct nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_TESS_CTRL);

   /* The pass works as follows:
    * If all codepaths write tess factors, we can say that all
    * invocations define tess factors.
    *
    * Each tess factor channel is tracked separately.
    */
   unsigned main_block_tf_writemask = 0; /* if main block writes tess factors */
   unsigned cond_block_tf_writemask = 0; /* if cond block writes tess factors */

   /* Initial value = true. Here the pass will accumulate results from
    * multiple segments surrounded by barriers. If tess factors aren't
    * written at all, it's a shader bug and we don't care if this will be
    * true.
    */
   bool tessfactors_are_def_in_all_invocs = true;

   nir_foreach_function (function, nir) {
      if (function->impl) {
         foreach_list_typed(nir_cf_node, node, node, &function->impl->body)
         {
            scan_tess_ctrl(node, &main_block_tf_writemask, &cond_block_tf_writemask,
                           &tessfactors_are_def_in_all_invocs, false);
         }
      }
   }

   /* Accumulate the result for the last code segment separated by a
    * barrier.
    */
   if (main_block_tf_writemask || cond_block_tf_writemask) {
      tessfactors_are_def_in_all_invocs &= !(cond_block_tf_writemask & ~main_block_tf_writemask);
   }

   return tessfactors_are_def_in_all_invocs;
}

static const nir_src *get_texture_src(nir_tex_instr *instr, nir_tex_src_type type)
{
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      if (instr->src[i].src_type == type)
         return &instr->src[i].src;
   }
   return NULL;
}

static void scan_io_usage(struct si_shader_info *info, nir_intrinsic_instr *intr,
                          bool is_input)
{
   unsigned interp = INTERP_MODE_FLAT; /* load_input uses flat shading */

   if (intr->intrinsic == nir_intrinsic_load_interpolated_input) {
      nir_intrinsic_instr *baryc = nir_instr_as_intrinsic(intr->src[0].ssa->parent_instr);

      if (baryc) {
         if (nir_intrinsic_infos[baryc->intrinsic].index_map[NIR_INTRINSIC_INTERP_MODE] > 0)
            interp = nir_intrinsic_interp_mode(baryc);
         else
            unreachable("unknown barycentric intrinsic");
      } else {
         unreachable("unknown barycentric expression");
      }
   }

   unsigned mask, bit_size;
   bool is_output_load;

   if (nir_intrinsic_has_write_mask(intr)) {
      mask = nir_intrinsic_write_mask(intr); /* store */
      bit_size = nir_src_bit_size(intr->src[0]);
      is_output_load = false;
   } else {
      mask = nir_ssa_def_components_read(&intr->dest.ssa); /* load */
      bit_size = intr->dest.ssa.bit_size;
      is_output_load = !is_input;
   }
   assert(bit_size != 64 && !(mask & ~0xf) && "64-bit IO should have been lowered");

   /* Convert the 16-bit component mask to a 32-bit component mask except for VS inputs
    * where the mask is untyped.
    */
   if (bit_size == 16 && !is_input) {
      unsigned new_mask = 0;
      for (unsigned i = 0; i < 4; i++) {
         if (mask & (1 << i))
            new_mask |= 0x1 << (i / 2);
      }
      mask = new_mask;
   }

   mask <<= nir_intrinsic_component(intr);

   nir_src offset = *nir_get_io_offset_src(intr);
   bool indirect = !nir_src_is_const(offset);
   if (!indirect)
      assert(nir_src_as_uint(offset) == 0);

   unsigned semantic = 0;
   /* VS doesn't have semantics. */
   if (info->stage != MESA_SHADER_VERTEX || !is_input)
      semantic = nir_intrinsic_io_semantics(intr).location;

   if (info->stage == MESA_SHADER_FRAGMENT && !is_input) {
      /* Never use FRAG_RESULT_COLOR directly. */
      if (semantic == FRAG_RESULT_COLOR)
         semantic = FRAG_RESULT_DATA0;
      semantic += nir_intrinsic_io_semantics(intr).dual_source_blend_index;
   }

   unsigned driver_location = nir_intrinsic_base(intr);
   unsigned num_slots = indirect ? nir_intrinsic_io_semantics(intr).num_slots : 1;

   if (is_input) {
      assert(driver_location + num_slots <= ARRAY_SIZE(info->input));

      for (unsigned i = 0; i < num_slots; i++) {
         unsigned loc = driver_location + i;

         info->input[loc].semantic = semantic + i;

         if (semantic == VARYING_SLOT_PRIMITIVE_ID)
            info->input[loc].interpolate = INTERP_MODE_FLAT;
         else
            info->input[loc].interpolate = interp;

         if (mask) {
            info->input[loc].usage_mask |= mask;
            if (bit_size == 16) {
               if (nir_intrinsic_io_semantics(intr).high_16bits)
                  info->input[loc].fp16_lo_hi_valid |= 0x2;
               else
                  info->input[loc].fp16_lo_hi_valid |= 0x1;
            }
            info->num_inputs = MAX2(info->num_inputs, loc + 1);
         }
      }
   } else {
      /* Outputs. */
      assert(driver_location + num_slots <= ARRAY_SIZE(info->output_usagemask));

      for (unsigned i = 0; i < num_slots; i++) {
         unsigned loc = driver_location + i;

         info->output_semantic[loc] = semantic + i;

         if (is_output_load) {
            /* Output loads have only a few things that we need to track. */
            info->output_readmask[loc] |= mask;
         } else if (mask) {
            /* Output stores. */
            unsigned gs_streams = (uint32_t)nir_intrinsic_io_semantics(intr).gs_streams <<
                                  (nir_intrinsic_component(intr) * 2);
            unsigned new_mask = mask & ~info->output_usagemask[loc];

            for (unsigned i = 0; i < 4; i++) {
               unsigned stream = (gs_streams >> (i * 2)) & 0x3;

               if (new_mask & (1 << i)) {
                  info->output_streams[loc] |= stream << (i * 2);
                  info->num_stream_output_components[stream]++;
               }
            }

            if (nir_intrinsic_has_src_type(intr))
               info->output_type[loc] = nir_intrinsic_src_type(intr);
            else if (nir_intrinsic_has_dest_type(intr))
               info->output_type[loc] = nir_intrinsic_dest_type(intr);
            else
               info->output_type[loc] = nir_type_float32;

            info->output_usagemask[loc] |= mask;
            info->num_outputs = MAX2(info->num_outputs, loc + 1);

            if (info->stage == MESA_SHADER_FRAGMENT &&
                semantic >= FRAG_RESULT_DATA0 && semantic <= FRAG_RESULT_DATA7) {
               unsigned index = semantic - FRAG_RESULT_DATA0;

               if (nir_intrinsic_src_type(intr) == nir_type_float16)
                  info->output_color_types |= SI_TYPE_FLOAT16 << (index * 2);
               else if (nir_intrinsic_src_type(intr) == nir_type_int16)
                  info->output_color_types |= SI_TYPE_INT16 << (index * 2);
               else if (nir_intrinsic_src_type(intr) == nir_type_uint16)
                  info->output_color_types |= SI_TYPE_UINT16 << (index * 2);
            }
         }
      }
   }
}

static bool is_bindless_handle_indirect(nir_instr *src)
{
   /* Check if the bindless handle comes from indirect load_ubo. */
   if (src->type == nir_instr_type_intrinsic &&
       nir_instr_as_intrinsic(src)->intrinsic == nir_intrinsic_load_ubo) {
      if (!nir_src_is_const(nir_instr_as_intrinsic(src)->src[0]))
         return true;
   } else {
      /* Some other instruction. Return the worst-case result. */
      return true;
   }
   return false;
}

static void scan_instruction(const struct nir_shader *nir, struct si_shader_info *info,
                             nir_instr *instr)
{
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      const nir_src *handle = get_texture_src(tex, nir_tex_src_texture_handle);

      /* Gather the types of used VMEM instructions that return something. */
      switch (tex->op) {
      case nir_texop_tex:
      case nir_texop_txb:
      case nir_texop_txl:
      case nir_texop_txd:
      case nir_texop_lod:
      case nir_texop_tg4:
         info->uses_vmem_sampler_or_bvh = true;
         break;
      default:
         info->uses_vmem_load_other = true;
         break;
      }

      if (handle) {
         info->uses_bindless_samplers = true;

         if (is_bindless_handle_indirect(handle->ssa->parent_instr))
            info->uses_indirect_descriptor = true;
      } else {
         const nir_src *deref = get_texture_src(tex, nir_tex_src_texture_deref);

         if (nir_deref_instr_has_indirect(nir_src_as_deref(*deref)))
            info->uses_indirect_descriptor = true;
      }
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      const char *intr_name = nir_intrinsic_infos[intr->intrinsic].name;
      bool is_ssbo = strstr(intr_name, "ssbo");
      bool is_image = strstr(intr_name, "image") == intr_name;
      bool is_bindless_image = strstr(intr_name, "bindless_image") == intr_name;

      /* Gather the types of used VMEM instructions that return something. */
      if (nir_intrinsic_infos[intr->intrinsic].has_dest) {
         switch (intr->intrinsic) {
         case nir_intrinsic_load_ubo:
            if (!nir_src_is_const(intr->src[1]))
               info->uses_vmem_load_other = true;
            break;
         case nir_intrinsic_load_constant:
            info->uses_vmem_load_other = true;
            break;

         case nir_intrinsic_load_barycentric_at_sample: /* This loads sample positions. */
         case nir_intrinsic_load_tess_level_outer: /* TES input read from memory */
         case nir_intrinsic_load_tess_level_inner: /* TES input read from memory */
            info->uses_vmem_load_other = true;
            break;

         case nir_intrinsic_load_input:
         case nir_intrinsic_load_input_vertex:
         case nir_intrinsic_load_per_vertex_input:
            if (nir->info.stage == MESA_SHADER_VERTEX ||
                nir->info.stage == MESA_SHADER_TESS_EVAL)
               info->uses_vmem_load_other = true;
            break;

         default:
            if (is_image ||
                is_bindless_image ||
                is_ssbo ||
                (strstr(intr_name, "global") == intr_name ||
                 intr->intrinsic == nir_intrinsic_load_global ||
                 intr->intrinsic == nir_intrinsic_store_global) ||
                strstr(intr_name, "scratch"))
               info->uses_vmem_load_other = true;
            break;
         }
      }

      if (is_bindless_image)
         info->uses_bindless_images = true;

      if (nir_intrinsic_writes_external_memory(intr))
         info->num_memory_stores++;

      if (is_image && nir_deref_instr_has_indirect(nir_src_as_deref(intr->src[0])))
         info->uses_indirect_descriptor = true;

      if (is_bindless_image && is_bindless_handle_indirect(intr->src[0].ssa->parent_instr))
         info->uses_indirect_descriptor = true;

      if (intr->intrinsic != nir_intrinsic_store_ssbo && is_ssbo &&
          !nir_src_is_const(intr->src[0]))
         info->uses_indirect_descriptor = true;

      switch (intr->intrinsic) {
      case nir_intrinsic_store_ssbo:
         if (!nir_src_is_const(intr->src[1]))
            info->uses_indirect_descriptor = true;
         break;
      case nir_intrinsic_load_ubo:
         if (!nir_src_is_const(intr->src[0]))
            info->uses_indirect_descriptor = true;
         break;
      case nir_intrinsic_load_local_invocation_id:
      case nir_intrinsic_load_workgroup_id: {
         unsigned mask = nir_ssa_def_components_read(&intr->dest.ssa);
         while (mask) {
            unsigned i = u_bit_scan(&mask);

            if (intr->intrinsic == nir_intrinsic_load_workgroup_id)
               info->uses_block_id[i] = true;
            else
               info->uses_thread_id[i] = true;
         }
         break;
      }
      case nir_intrinsic_load_color0:
      case nir_intrinsic_load_color1: {
         unsigned index = intr->intrinsic == nir_intrinsic_load_color1;
         uint8_t mask = nir_ssa_def_components_read(&intr->dest.ssa);
         info->colors_read |= mask << (index * 4);

         switch (info->color_interpolate[index]) {
         case INTERP_MODE_SMOOTH:
            if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_SAMPLE)
               info->uses_persp_sample = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTROID)
               info->uses_persp_centroid = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTER)
               info->uses_persp_center = true;
            break;
         case INTERP_MODE_NOPERSPECTIVE:
            if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_SAMPLE)
               info->uses_linear_sample = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTROID)
               info->uses_linear_centroid = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTER)
               info->uses_linear_center = true;
            break;
         case INTERP_MODE_COLOR:
            /* We don't know the final value. This will be FLAT if flatshading is enabled
             * in the rasterizer state, otherwise it will be SMOOTH.
             */
            info->uses_interp_color = true;
            if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_SAMPLE)
               info->uses_persp_sample_color = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTROID)
               info->uses_persp_centroid_color = true;
            else if (info->color_interpolate_loc[index] == TGSI_INTERPOLATE_LOC_CENTER)
               info->uses_persp_center_color = true;
            break;
         }
         break;
      }
      case nir_intrinsic_load_barycentric_at_offset:   /* uses center */
      case nir_intrinsic_load_barycentric_at_sample:   /* uses center */
         if (nir_intrinsic_interp_mode(intr) == INTERP_MODE_FLAT)
            break;

         if (nir_intrinsic_interp_mode(intr) == INTERP_MODE_NOPERSPECTIVE) {
            info->uses_linear_center = true;
         } else {
            info->uses_persp_center = true;
         }
         if (intr->intrinsic == nir_intrinsic_load_barycentric_at_sample)
            info->uses_interp_at_sample = true;
         break;
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_per_vertex_input:
      case nir_intrinsic_load_input_vertex:
      case nir_intrinsic_load_interpolated_input:
         scan_io_usage(info, intr, true);
         break;
      case nir_intrinsic_load_output:
      case nir_intrinsic_load_per_vertex_output:
      case nir_intrinsic_store_output:
      case nir_intrinsic_store_per_vertex_output:
         scan_io_usage(info, intr, false);
         break;
      case nir_intrinsic_load_deref:
      case nir_intrinsic_store_deref:
         /* These can only occur if there is indirect temp indexing. */
         break;
      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
         unreachable("these opcodes should have been lowered");
         break;
      default:
         break;
      }
   }
}

void si_nir_scan_shader(const struct nir_shader *nir, struct si_shader_info *info)
{
   nir_function *func;

   memset(info, 0, sizeof(*info));
   info->base = nir->info;
   info->stage = nir->info.stage;

   /* Get options from shader profiles. */
   for (unsigned i = 0; i < ARRAY_SIZE(profiles); i++) {
      if (_mesa_printed_sha1_equal(info->base.source_sha1, profiles[i].sha1)) {
         info->options = profiles[i].options;
         break;
      }
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* post_depth_coverage implies early_fragment_tests */
      info->base.fs.early_fragment_tests |= info->base.fs.post_depth_coverage;

      info->color_interpolate[0] = nir->info.fs.color0_interp;
      info->color_interpolate[1] = nir->info.fs.color1_interp;
      for (unsigned i = 0; i < 2; i++) {
         if (info->color_interpolate[i] == INTERP_MODE_NONE)
            info->color_interpolate[i] = INTERP_MODE_COLOR;
      }

      info->color_interpolate_loc[0] = nir->info.fs.color0_sample ? TGSI_INTERPOLATE_LOC_SAMPLE :
                                       nir->info.fs.color0_centroid ? TGSI_INTERPOLATE_LOC_CENTROID :
                                                                      TGSI_INTERPOLATE_LOC_CENTER;
      info->color_interpolate_loc[1] = nir->info.fs.color1_sample ? TGSI_INTERPOLATE_LOC_SAMPLE :
                                       nir->info.fs.color1_centroid ? TGSI_INTERPOLATE_LOC_CENTROID :
                                                                      TGSI_INTERPOLATE_LOC_CENTER;
      /* Set an invalid value. Will be determined at draw time if needed when the expected
       * conditions are met.
       */
      info->writes_1_if_tex_is_1 = nir->info.writes_memory ? 0 : 0xff;
   }

   info->constbuf0_num_slots = nir->num_uniforms;

   if (nir->info.stage == MESA_SHADER_TESS_CTRL) {
      info->tessfactors_are_def_in_all_invocs = are_tessfactors_def_in_all_invocs(nir);
   }

   info->uses_frontface = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRONT_FACE);
   info->uses_instanceid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   info->uses_base_vertex = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX);
   info->uses_base_instance = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE);
   info->uses_invocationid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INVOCATION_ID);
   info->uses_grid_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_WORKGROUPS);
   info->uses_subgroup_info = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_LOCAL_INVOCATION_INDEX) ||
                              BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SUBGROUP_ID) ||
                              BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_SUBGROUPS);
   info->uses_variable_block_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_WORKGROUP_SIZE);
   info->uses_drawid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
   info->uses_primid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID) ||
                       nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID;
   info->reads_samplemask = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);
   info->reads_tess_factors = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_TESS_LEVEL_INNER) ||
                              BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_TESS_LEVEL_OUTER);
   info->uses_linear_sample = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_LINEAR_SAMPLE);
   info->uses_linear_centroid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_LINEAR_CENTROID);
   info->uses_linear_center = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_LINEAR_PIXEL);
   info->uses_persp_sample = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_PERSP_SAMPLE);
   info->uses_persp_centroid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_PERSP_CENTROID);
   info->uses_persp_center = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      info->writes_z = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
      info->writes_stencil = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);
      info->writes_samplemask = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK);

      info->colors_written = nir->info.outputs_written >> FRAG_RESULT_DATA0;
      if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_COLOR)) {
         info->color0_writes_all_cbufs = true;
         info->colors_written |= 0x1;
      }
      if (nir->info.fs.color_is_dual_source)
         info->colors_written |= 0x2;
   } else {
      info->writes_primid = nir->info.outputs_written & VARYING_BIT_PRIMITIVE_ID;
      info->writes_viewport_index = nir->info.outputs_written & VARYING_BIT_VIEWPORT;
      info->writes_layer = nir->info.outputs_written & VARYING_BIT_LAYER;
      info->writes_psize = nir->info.outputs_written & VARYING_BIT_PSIZ;
      info->writes_clipvertex = nir->info.outputs_written & VARYING_BIT_CLIP_VERTEX;
      info->writes_edgeflag = nir->info.outputs_written & VARYING_BIT_EDGE;
      info->writes_position = nir->info.outputs_written & VARYING_BIT_POS;
   }

   func = (struct nir_function *)exec_list_get_head_const(&nir->functions);
   nir_foreach_block (block, func->impl) {
      nir_foreach_instr (instr, block)
         scan_instruction(nir, info, instr);
   }

   if (info->stage == MESA_SHADER_VERTEX || info->stage == MESA_SHADER_TESS_EVAL) {
      /* Add the PrimitiveID output, but don't increment num_outputs.
       * The driver inserts PrimitiveID only when it's used by the pixel shader,
       * and si_emit_spi_map uses this unconditionally when such a pixel shader is used.
       */
      info->output_semantic[info->num_outputs] = VARYING_SLOT_PRIMITIVE_ID;
      info->output_type[info->num_outputs] = nir_type_uint32;
      info->output_usagemask[info->num_outputs] = 0x1;
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      info->allow_flat_shading = !(info->uses_persp_center || info->uses_persp_centroid ||
                                   info->uses_persp_sample || info->uses_linear_center ||
                                   info->uses_linear_centroid || info->uses_linear_sample ||
                                   info->uses_interp_at_sample || nir->info.writes_memory ||
                                   nir->info.fs.uses_fbfetch_output ||
                                   nir->info.fs.needs_quad_helper_invocations ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_POINT_COORD) ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID) ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS) ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN) ||
                                   BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_HELPER_INVOCATION));

      info->uses_vmem_load_other |= info->base.fs.uses_fbfetch_output;

      /* Add both front and back color inputs. */
      unsigned num_inputs_with_colors = info->num_inputs;
      for (unsigned back = 0; back < 2; back++) {
         for (unsigned i = 0; i < 2; i++) {
            if ((info->colors_read >> (i * 4)) & 0xf) {
               unsigned index = num_inputs_with_colors;

               info->input[index].semantic = (back ? VARYING_SLOT_BFC0 : VARYING_SLOT_COL0) + i;
               info->input[index].interpolate = info->color_interpolate[i];
               info->input[index].usage_mask = info->colors_read >> (i * 4);
               num_inputs_with_colors++;

               /* Back-face color don't increment num_inputs. si_emit_spi_map will use
                * back-face colors conditionally only when they are needed.
                */
               if (!back)
                  info->num_inputs = num_inputs_with_colors;
            }
         }
      }
   }

   info->uses_vmem_load_other |= info->uses_indirect_descriptor;

   /* Trim output read masks based on write masks. */
   for (unsigned i = 0; i < info->num_outputs; i++)
      info->output_readmask[i] &= info->output_usagemask[i];

   info->has_divergent_loop = nir_has_divergent_loop((nir_shader*)nir);
}
