/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "ac_nir_to_llvm.h"
#include "ac_nir.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_deref.h"
#include "compiler/nir_types.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "tgsi/tgsi_from_mesa.h"

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

         if (semantic == SYSTEM_VALUE_PRIMITIVE_ID)
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
         info->uses_vmem_return_type_sampler_or_bvh = true;
         break;
      default:
         info->uses_vmem_return_type_other = true;
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
      bool is_image = strstr(intr_name, "image_deref");
      bool is_bindless_image = strstr(intr_name, "bindless_image");

      /* Gather the types of used VMEM instructions that return something. */
      if (nir_intrinsic_infos[intr->intrinsic].has_dest) {
         switch (intr->intrinsic) {
         case nir_intrinsic_load_ubo:
            if (!nir_src_is_const(intr->src[1]))
               info->uses_vmem_return_type_other = true;
            break;

         case nir_intrinsic_load_barycentric_at_sample: /* This loads sample positions. */
         case nir_intrinsic_load_tess_level_outer: /* TES input read from memory */
         case nir_intrinsic_load_tess_level_inner: /* TES input read from memory */
            info->uses_vmem_return_type_other = true;
            break;

         case nir_intrinsic_load_input:
         case nir_intrinsic_load_input_vertex:
         case nir_intrinsic_load_per_vertex_input:
            if (nir->info.stage == MESA_SHADER_VERTEX ||
                nir->info.stage == MESA_SHADER_TESS_EVAL)
               info->uses_vmem_return_type_other = true;
            break;

         default:
            if (is_image ||
                is_bindless_image ||
                is_ssbo ||
                strstr(intr_name, "global") ||
                strstr(intr_name, "scratch"))
               info->uses_vmem_return_type_other = true;
            break;
         }
      }

      if (is_bindless_image)
         info->uses_bindless_images = true;

      if (strstr(intr_name, "image_atomic") ||
          strstr(intr_name, "image_store") ||
          strstr(intr_name, "image_deref_atomic") ||
          strstr(intr_name, "image_deref_store") ||
          strstr(intr_name, "ssbo_atomic") ||
          intr->intrinsic == nir_intrinsic_store_ssbo)
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

   info->base = nir->info;
   info->stage = nir->info.stage;

   if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
      if (info->base.tess.primitive_mode == GL_ISOLINES)
         info->base.tess.primitive_mode = GL_LINES;
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
      info->tessfactors_are_def_in_all_invocs = ac_are_tessfactors_def_in_all_invocs(nir);
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

   /* Trim output read masks based on write masks. */
   for (unsigned i = 0; i < info->num_outputs; i++)
      info->output_readmask[i] &= info->output_usagemask[i];
}

static bool si_alu_to_scalar_filter(const nir_instr *instr, const void *data)
{
   struct si_screen *sscreen = (struct si_screen *)data;

   if (sscreen->options.fp16 &&
       instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      if (alu->dest.dest.is_ssa &&
          alu->dest.dest.ssa.bit_size == 16 &&
          alu->dest.dest.ssa.num_components == 2)
         return false;
   }

   return true;
}

void si_nir_opts(struct si_screen *sscreen, struct nir_shader *nir, bool first)
{
   bool progress;

   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_lower_alu_to_scalar, si_alu_to_scalar_filter, sscreen);
   NIR_PASS_V(nir, nir_lower_phis_to_scalar, false);

   do {
      progress = false;
      bool lower_alu_to_scalar = false;
      bool lower_phis_to_scalar = false;

      if (first) {
         NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
         NIR_PASS(lower_alu_to_scalar, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      }
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      NIR_PASS(lower_alu_to_scalar, nir, nir_opt_trivial_continues);
      /* (Constant) copy propagation is needed for txf with offsets. */
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(lower_phis_to_scalar, nir, nir_opt_if, true);
      NIR_PASS(progress, nir, nir_opt_dead_cf);

      if (lower_alu_to_scalar)
         NIR_PASS_V(nir, nir_lower_alu_to_scalar, si_alu_to_scalar_filter, sscreen);
      if (lower_phis_to_scalar)
         NIR_PASS_V(nir, nir_lower_phis_to_scalar, false);
      progress |= lower_alu_to_scalar | lower_phis_to_scalar;

      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      /* Needed for algebraic lowering */
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      if (!nir->info.flrp_lowered) {
         unsigned lower_flrp = (nir->options->lower_flrp16 ? 16 : 0) |
                               (nir->options->lower_flrp32 ? 32 : 0) |
                               (nir->options->lower_flrp64 ? 64 : 0);
         assert(lower_flrp);
         bool lower_flrp_progress = false;

         NIR_PASS(lower_flrp_progress, nir, nir_lower_flrp, lower_flrp, false /* always_precise */);
         if (lower_flrp_progress) {
            NIR_PASS(progress, nir, nir_opt_constant_folding);
            progress = true;
         }

         /* Nothing should rematerialize any flrps, so we only
          * need to do this lowering once.
          */
         nir->info.flrp_lowered = true;
      }

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll);
      }

      if (nir->info.stage == MESA_SHADER_FRAGMENT)
         NIR_PASS_V(nir, nir_opt_move_discards_to_top);

      if (sscreen->options.fp16)
         NIR_PASS(progress, nir, nir_opt_vectorize, NULL, NULL);
   } while (progress);

   NIR_PASS_V(nir, nir_lower_var_copies);
}

void si_nir_late_opts(nir_shader *nir)
{
   bool more_late_algebraic = true;
   while (more_late_algebraic) {
      more_late_algebraic = false;
      NIR_PASS(more_late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS_V(nir, nir_opt_constant_folding);
      NIR_PASS_V(nir, nir_copy_prop);
      NIR_PASS_V(nir, nir_opt_dce);
      NIR_PASS_V(nir, nir_opt_cse);
   }
}

static void si_late_optimize_16bit_samplers(struct si_screen *sscreen, nir_shader *nir)
{
   /* Optimize and fix types of image_sample sources and destinations.
    *
    * The image_sample constraints are:
    *   nir_tex_src_coord:       has_a16 ? select 16 or 32 : 32
    *   nir_tex_src_comparator:  32
    *   nir_tex_src_offset:      32
    *   nir_tex_src_bias:        32
    *   nir_tex_src_lod:         match coord
    *   nir_tex_src_min_lod:     match coord
    *   nir_tex_src_ms_index:    match coord
    *   nir_tex_src_ddx:         has_g16 && coord == 32 ? select 16 or 32 : match coord
    *   nir_tex_src_ddy:         match ddy
    *
    * coord and ddx are selected optimally. The types of the rest are legalized
    * based on those two.
    */
   /* TODO: The constraints can't represent the ddx constraint. */
   /*bool has_g16 = sscreen->info.chip_class >= GFX10 && LLVM_VERSION_MAJOR >= 12;*/
   bool has_g16 = false;
   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_comparator]   = {true, 32},
      [nir_tex_src_offset]       = {true, 32},
      [nir_tex_src_bias]         = {true, 32},
      [nir_tex_src_lod]          = {true, 0, nir_tex_src_coord},
      [nir_tex_src_min_lod]      = {true, 0, nir_tex_src_coord},
      [nir_tex_src_ms_index]     = {true, 0, nir_tex_src_coord},
      [nir_tex_src_ddx]          = {!has_g16, 0, nir_tex_src_coord},
      [nir_tex_src_ddy]          = {true, 0, has_g16 ? nir_tex_src_ddx : nir_tex_src_coord},
   };
   bool changed = false;

   NIR_PASS(changed, nir, nir_fold_16bit_sampler_conversions,
            (1 << nir_tex_src_coord) |
            (has_g16 ? 1 << nir_tex_src_ddx : 0));
   NIR_PASS(changed, nir, nir_legalize_16bit_sampler_srcs, tex_constraints);

   if (changed) {
      si_nir_opts(sscreen, nir, false);
      si_nir_late_opts(nir);
   }
}

static int type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void si_nir_lower_color(nir_shader *nir)
{
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);

   nir_builder b;
   nir_builder_init(&b, entrypoint);

   nir_foreach_block (block, entrypoint) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         if (intrin->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         if (!nir_deref_mode_is(deref, nir_var_shader_in))
            continue;

         b.cursor = nir_before_instr(instr);
         nir_variable *var = nir_deref_instr_get_variable(deref);
         nir_ssa_def *def;

         if (var->data.location == VARYING_SLOT_COL0) {
            def = nir_load_color0(&b);
            nir->info.fs.color0_interp = var->data.interpolation;
            nir->info.fs.color0_sample = var->data.sample;
            nir->info.fs.color0_centroid = var->data.centroid;
         } else if (var->data.location == VARYING_SLOT_COL1) {
            def = nir_load_color1(&b);
            nir->info.fs.color1_interp = var->data.interpolation;
            nir->info.fs.color1_sample = var->data.sample;
            nir->info.fs.color1_centroid = var->data.centroid;
         } else {
            continue;
         }

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, def);
         nir_instr_remove(instr);
      }
   }
}

static void si_lower_io(struct nir_shader *nir)
{
   /* HW supports indirect indexing for: | Enabled in driver
    * -------------------------------------------------------
    * TCS inputs                         | Yes
    * TES inputs                         | Yes
    * GS inputs                          | No
    * -------------------------------------------------------
    * VS outputs before TCS              | No
    * TCS outputs                        | Yes
    * VS/TES outputs before GS           | No
    */
   bool has_indirect_inputs = nir->info.stage == MESA_SHADER_TESS_CTRL ||
                              nir->info.stage == MESA_SHADER_TESS_EVAL;
   bool has_indirect_outputs = nir->info.stage == MESA_SHADER_TESS_CTRL;

   if (!has_indirect_inputs || !has_indirect_outputs) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir),
                 !has_indirect_outputs, !has_indirect_inputs);

      /* Since we're doing nir_lower_io_to_temporaries late, we need
       * to lower all the copy_deref's introduced by
       * lower_io_to_temporaries before calling nir_lower_io.
       */
      NIR_PASS_V(nir, nir_split_var_copies);
      NIR_PASS_V(nir, nir_lower_var_copies);
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   }

   /* The vectorization must be done after nir_lower_io_to_temporaries, because
    * nir_lower_io_to_temporaries after vectorization breaks:
    *    piglit/bin/arb_gpu_shader5-interpolateAtOffset -auto -fbo
    * TODO: It's probably a bug in nir_lower_io_to_temporaries.
    *
    * The vectorizer can only vectorize this:
    *    op src0.x, src1.x
    *    op src0.y, src1.y
    *
    * So it requires that inputs are already vectors and it must be the same
    * vector between instructions. The vectorizer doesn't create vectors
    * from independent scalar sources, so vectorize inputs.
    *
    * TODO: The pass fails this for VS: assert(b.shader->info.stage != MESA_SHADER_VERTEX);
    */
   if (nir->info.stage != MESA_SHADER_VERTEX)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_in);

   /* Vectorize outputs, so that we don't split vectors before storing outputs. */
   /* TODO: The pass fails an assertion for other shader stages. */
   if (nir->info.stage == MESA_SHADER_TESS_CTRL ||
       nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_out);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      si_nir_lower_color(nir);

   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_out | nir_var_shader_in,
              type_size_vec4, nir_lower_io_lower_64bit_to_32);
   nir->info.io_lowered = true;

   /* This pass needs actual constants */
   NIR_PASS_V(nir, nir_opt_constant_folding);
   NIR_PASS_V(nir, nir_io_add_const_offset_to_base, nir_var_shader_in |
                                                    nir_var_shader_out);

   /* Remove dead derefs, so that nir_validate doesn't fail. */
   NIR_PASS_V(nir, nir_opt_dce);

   /* Remove input and output nir_variables, because we don't need them
    * anymore. Also remove uniforms, because those should have been lowered
    * to UBOs already.
    */
   unsigned modes = nir_var_shader_in | nir_var_shader_out | nir_var_uniform;
   nir_foreach_variable_with_modes_safe(var, nir, modes) {
      if (var->data.mode == nir_var_uniform &&
          (glsl_type_get_image_count(var->type) ||
           glsl_type_get_sampler_count(var->type)))
         continue;

      exec_node_remove(&var->node);
   }
}

/**
 * Perform "lowering" operations on the NIR that are run once when the shader
 * selector is created.
 */
static void si_lower_nir(struct si_screen *sscreen, struct nir_shader *nir)
{
   /* Perform lowerings (and optimizations) of code.
    *
    * Performance considerations aside, we must:
    * - lower certain ALU operations
    * - ensure constant offsets for texture instructions are folded
    *   and copy-propagated
    */

   static const struct nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0u,
      .lower_txs_cube_array = true,
   };
   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);

   static const struct nir_lower_image_options lower_image_options = {
      .lower_cube_size = true,
   };
   NIR_PASS_V(nir, nir_lower_image, &lower_image_options);

   const nir_lower_subgroups_options subgroups_options = {
      .subgroup_size = 64,
      .ballot_bit_size = 64,
      .ballot_components = 1,
      .lower_to_scalar = true,
      .lower_subgroup_masks = true,
      .lower_vote_trivial = false,
      .lower_vote_eq = true,
   };
   NIR_PASS_V(nir, nir_lower_subgroups, &subgroups_options);

   NIR_PASS_V(nir, nir_lower_discard_or_demote,
              (sscreen->debug_flags & DBG(FS_CORRECT_DERIVS_AFTER_KILL)) ||
               nir->info.is_arb_asm);

   /* Lower load constants to scalar and then clean up the mess */
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_opt_intrinsics);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      if (nir->info.cs.derivative_group == DERIVATIVE_GROUP_QUADS) {
         /* If we are shuffling local_invocation_id for quad derivatives, we
          * need to derive local_invocation_index from local_invocation_id
          * first, so that the value corresponds to the shuffled
          * local_invocation_id.
          */
         nir_lower_compute_system_values_options options = {0};
         options.lower_local_invocation_index = true;
         NIR_PASS_V(nir, nir_lower_compute_system_values, &options);
      }

      nir_opt_cse(nir); /* CSE load_local_invocation_id */
      nir_lower_compute_system_values_options options = {0};
      options.shuffle_local_ids_for_quad_derivatives = true;
      NIR_PASS_V(nir, nir_lower_compute_system_values, &options);
   }

   if (sscreen->b.get_shader_param(&sscreen->b, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_FP16)) {
      NIR_PASS_V(nir, nir_lower_mediump_io,
                 /* TODO: LLVM fails to compile this test if VS inputs are 16-bit:
                  * dEQP-GLES31.functional.shaders.builtin_functions.integer.bitfieldinsert.uvec3_lowp_geometry
                  */
                 (nir->info.stage != MESA_SHADER_VERTEX ? nir_var_shader_in : 0) | nir_var_shader_out,
                 BITFIELD64_BIT(VARYING_SLOT_PNTC) | BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32),
                 true);
   }

   si_nir_opts(sscreen, nir, true);

   /* Lower large variables that are always constant with load_constant
    * intrinsics, which get turned into PC-relative loads from a data
    * section next to the shader.
    *
    * st/mesa calls finalize_nir twice, but we can't call this pass twice.
    */
   bool changed = false;
   if (!nir->constant_data) {
      /* The pass crashes if there are dead temps of lowered IO interface types. */
      NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
      NIR_PASS(changed, nir, nir_opt_large_constants, glsl_get_natural_size_align_bytes, 16);
   }

   changed |= ac_nir_lower_indirect_derefs(nir, sscreen->info.chip_class);
   if (changed)
      si_nir_opts(sscreen, nir, false);

   /* Run late optimizations to fuse ffma and eliminate 16-bit conversions. */
   si_nir_late_opts(nir);

   if (sscreen->b.get_shader_param(&sscreen->b, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_FP16))
      si_late_optimize_16bit_samplers(sscreen, nir);

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
}

char *si_finalize_nir(struct pipe_screen *screen, void *nirptr)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct nir_shader *nir = (struct nir_shader *)nirptr;

   si_lower_io(nir);
   si_lower_nir(sscreen, nir);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (sscreen->options.inline_uniforms)
      nir_find_inlinable_uniforms(nir);

   return NULL;
}
