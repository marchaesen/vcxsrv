/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "util/mesa-sha1.h"
#include "sid.h"
#include "nir.h"
#include "nir_tcs_info.h"
#include "nir_xfb_info.h"
#include "aco_interface.h"
#include "ac_nir.h"

struct si_shader_profile si_shader_profiles[] =
{
   {
      /* Plot3D */
      {0x38c94662, 0x7b634109, 0x50f8254a, 0x0f4986a9, 0x11e59716, 0x3081e1a2, 0xbb2a0c59, 0xc29e853a},
      SI_PROFILE_VS_NO_BINNING,
   },
   {
      /* Viewperf/Energy */
      {0x3279654e, 0xf51c358d, 0xc526e175, 0xd198eb26, 0x75c36c86, 0xd796398b, 0xc99b5e92, 0xddc31503},
      SI_PROFILE_NO_OPT_UNIFORM_VARYINGS,    /* Uniform propagation regresses performance. */
   },
   {
      /* Viewperf/Medical */
      {0x4a041ad8, 0xe105a058, 0x2e9f7a38, 0xef4d1c2f, 0xb8aee798, 0x821f166b, 0x17b42668, 0xa4d1cc0a},
      SI_PROFILE_GFX9_GFX10_PS_NO_BINNING,
   },
   {
      /* Viewperf/Medical, a shader with a divergent loop doesn't benefit from Wave32,
       * probably due to interpolation performance.
       */
      {0xa9c7e2c2, 0x3e01de01, 0x886cab63, 0x24327678, 0xe247c394, 0x2ecc4bf9, 0xc196d978, 0x2ba7a89c},
      SI_PROFILE_GFX10_WAVE64,
   },
   {
      /* Viewperf/Creo */
      {0x182bd6b3, 0x5e8fba11, 0xa7b74071, 0xc69f6153, 0xc57aef8c, 0x9076492a, 0x53dc83ee, 0x921fb114},
      SI_PROFILE_CLAMP_DIV_BY_ZERO,
   },
};

unsigned si_get_num_shader_profiles(void)
{
   return ARRAY_SIZE(si_shader_profiles);
}

static const nir_src *get_texture_src(nir_tex_instr *instr, nir_tex_src_type type)
{
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      if (instr->src[i].src_type == type)
         return &instr->src[i].src;
   }
   return NULL;
}

static void scan_io_usage(const nir_shader *nir, struct si_shader_info *info,
                          nir_intrinsic_instr *intr, bool is_input, bool colors_lowered)
{
   unsigned mask, bit_size;
   bool is_output_load;

   if (nir_intrinsic_has_write_mask(intr)) {
      mask = nir_intrinsic_write_mask(intr); /* store */
      bit_size = nir_src_bit_size(intr->src[0]);
      is_output_load = false;
   } else {
      mask = nir_def_components_read(&intr->def); /* load */
      bit_size = intr->def.bit_size;
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
   if (nir->info.stage != MESA_SHADER_VERTEX || !is_input)
      semantic = nir_intrinsic_io_semantics(intr).location;

   if (nir->info.stage == MESA_SHADER_FRAGMENT && is_input) {
      /* Gather color PS inputs. We can only get here after lowering colors in monolithic
       * shaders. This must match what we do for nir_intrinsic_load_color0/1.
       */
      if (!colors_lowered &&
          (semantic == VARYING_SLOT_COL0 || semantic == VARYING_SLOT_COL1 ||
           semantic == VARYING_SLOT_BFC0 || semantic == VARYING_SLOT_BFC1)) {
         unsigned index = semantic == VARYING_SLOT_COL1 || semantic == VARYING_SLOT_BFC1;
         info->colors_read |= mask << (index * 4);
         return;
      }
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT && !is_input) {
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

         if (mask) {
            info->input[loc].usage_mask |= mask;
            info->num_inputs = MAX2(info->num_inputs, loc + 1);
         }
      }
   } else {
      /* Outputs. */
      assert(driver_location + num_slots <= ARRAY_SIZE(info->output_usagemask));

      for (unsigned i = 0; i < num_slots; i++) {
         unsigned loc = driver_location + i;
         unsigned slot_semantic = semantic + i;

         /* Call the translation functions to validate the semantic (call assertions in them). */
         if (nir->info.stage != MESA_SHADER_FRAGMENT &&
             semantic != VARYING_SLOT_EDGE) {
            if (semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
                semantic == VARYING_SLOT_TESS_LEVEL_OUTER ||
                (semantic >= VARYING_SLOT_PATCH0 && semantic <= VARYING_SLOT_PATCH31)) {
               ac_shader_io_get_unique_index_patch(semantic);
               ac_shader_io_get_unique_index_patch(slot_semantic);
            } else {
               si_shader_io_get_unique_index(semantic);
               si_shader_io_get_unique_index(slot_semantic);
            }
         }

         info->output_semantic[loc] = slot_semantic;

         if (!is_output_load && mask) {
            /* Output stores. */
            unsigned gs_streams = (uint32_t)nir_intrinsic_io_semantics(intr).gs_streams <<
                                  (nir_intrinsic_component(intr) * 2);
            unsigned new_mask = mask & ~info->output_usagemask[loc];

            /* Iterate over all components. */
            for (unsigned i = 0; i < 4; i++) {
               unsigned stream = (gs_streams >> (i * 2)) & 0x3;

               if (new_mask & (1 << i)) {
                  info->output_streams[loc] |= stream << (i * 2);
                  info->num_stream_output_components[stream]++;
               }

               if (nir_intrinsic_has_io_xfb(intr)) {
                  nir_io_xfb xfb = i < 2 ? nir_intrinsic_io_xfb(intr) :
                                           nir_intrinsic_io_xfb2(intr);
                  if (xfb.out[i % 2].num_components) {
                     unsigned stream = (gs_streams >> (i * 2)) & 0x3;
                     info->enabled_streamout_buffer_mask |=
                        BITFIELD_BIT(stream * 4 + xfb.out[i % 2].buffer);
                  }

                  info->output_xfb_writemask[loc] |= nir_instr_xfb_write_mask(intr);
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

            if (nir->info.stage == MESA_SHADER_VERTEX ||
                nir->info.stage == MESA_SHADER_TESS_CTRL ||
                nir->info.stage == MESA_SHADER_TESS_EVAL ||
                nir->info.stage == MESA_SHADER_GEOMETRY) {
               if (slot_semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
                   slot_semantic == VARYING_SLOT_TESS_LEVEL_OUTER) {
                  if (!nir_intrinsic_io_semantics(intr).no_varying) {
                     info->tess_levels_written_for_tes |=
                        BITFIELD_BIT(ac_shader_io_get_unique_index_patch(slot_semantic));
                  }
               } else if (slot_semantic >= VARYING_SLOT_PATCH0 &&
                          slot_semantic < VARYING_SLOT_TESS_MAX) {
                  if (!nir_intrinsic_io_semantics(intr).no_varying) {
                     info->patch_outputs_written_for_tes |=
                        BITFIELD_BIT(ac_shader_io_get_unique_index_patch(slot_semantic));
                  }
               } else if ((slot_semantic <= VARYING_SLOT_VAR31 ||
                           slot_semantic >= VARYING_SLOT_VAR0_16BIT) &&
                          slot_semantic != VARYING_SLOT_EDGE) {
                  uint64_t bit = BITFIELD64_BIT(si_shader_io_get_unique_index(slot_semantic));

                  /* Ignore outputs that are not passed from VS to PS. */
                  if (slot_semantic != VARYING_SLOT_POS &&
                      slot_semantic != VARYING_SLOT_PSIZ &&
                      slot_semantic != VARYING_SLOT_CLIP_VERTEX &&
                      slot_semantic != VARYING_SLOT_LAYER)
                     info->outputs_written_before_ps |= bit;

                  /* LAYER and VIEWPORT have no effect if they don't feed the rasterizer. */
                  if (slot_semantic != VARYING_SLOT_LAYER &&
                      slot_semantic != VARYING_SLOT_VIEWPORT) {
                     info->ls_es_outputs_written |= bit;

                     if (!nir_intrinsic_io_semantics(intr).no_varying)
                        info->tcs_outputs_written_for_tes |= bit;
                  }
               }
            }

            if (nir->info.stage == MESA_SHADER_FRAGMENT &&
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

   if (nir->info.stage == MESA_SHADER_FRAGMENT && !is_input && semantic == FRAG_RESULT_DEPTH) {
      if (nir_def_is_frag_coord_z(intr->src[0].ssa))
         info->output_z_equals_input_z = true;
      else
         info->output_z_is_not_input_z = true;
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

/* TODO: convert to nir_shader_instructions_pass */
static void scan_instruction(const struct nir_shader *nir, struct si_shader_info *info,
                             nir_instr *instr, bool colors_lowered)
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

      info->has_non_uniform_tex_access |=
         tex->texture_non_uniform || tex->sampler_non_uniform;

      info->has_shadow_comparison |= tex->is_shadow;
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

         case nir_intrinsic_load_input:
         case nir_intrinsic_load_input_vertex:
         case nir_intrinsic_load_per_vertex_input:
            if (nir->info.stage == MESA_SHADER_VERTEX ||
                nir->info.stage == MESA_SHADER_TESS_EVAL)
               info->uses_vmem_load_other = true;
            break;

         case nir_intrinsic_load_constant:
         case nir_intrinsic_load_barycentric_at_sample: /* This loads sample positions. */
         case nir_intrinsic_load_buffer_amd:
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

      if (is_image && nir_deref_instr_has_indirect(nir_src_as_deref(intr->src[0])))
         info->uses_indirect_descriptor = true;

      if (is_bindless_image && is_bindless_handle_indirect(intr->src[0].ssa->parent_instr))
         info->uses_indirect_descriptor = true;

      if (intr->intrinsic != nir_intrinsic_store_ssbo && is_ssbo &&
          !nir_src_is_const(intr->src[0]))
         info->uses_indirect_descriptor = true;

      if (nir_intrinsic_has_atomic_op(intr)) {
         if (nir_intrinsic_atomic_op(intr) == nir_atomic_op_ordered_add_gfx12_amd)
            info->uses_atomic_ordered_add = true;
      }

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
         unsigned mask = nir_def_components_read(&intr->def);
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
         uint8_t mask = nir_def_components_read(&intr->def);
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
         if (intr->intrinsic == nir_intrinsic_load_barycentric_at_offset)
            info->uses_interp_at_offset = true;
         if (intr->intrinsic == nir_intrinsic_load_barycentric_at_sample)
            info->uses_interp_at_sample = true;
         break;
      case nir_intrinsic_load_frag_coord:
         info->reads_frag_coord_mask |= nir_def_components_read(&intr->def);
         break;
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_per_vertex_input:
      case nir_intrinsic_load_input_vertex:
      case nir_intrinsic_load_interpolated_input:
         scan_io_usage(nir, info, intr, true, colors_lowered);
         break;
      case nir_intrinsic_load_output:
      case nir_intrinsic_load_per_vertex_output:
      case nir_intrinsic_store_output:
      case nir_intrinsic_store_per_vertex_output:
         scan_io_usage(nir, info, intr, false, colors_lowered);
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
      case nir_intrinsic_ordered_add_loop_gfx12_amd:
         info->uses_atomic_ordered_add = true;
         break;
      default:
         break;
      }
   }
}

void si_nir_scan_shader(struct si_screen *sscreen, struct nir_shader *nir,
                        struct si_shader_info *info, bool colors_lowered)
{
   bool force_use_aco = sscreen->use_aco_shader_type == nir->info.stage;
   for (unsigned i = 0; i < sscreen->num_use_aco_shader_blakes; i++) {
      if (!memcmp(sscreen->use_aco_shader_blakes[i], nir->info.source_blake3,
                  sizeof(blake3_hash))) {
         force_use_aco = true;
         break;
      }
   }

   nir->info.use_aco_amd = aco_is_gpu_supported(&sscreen->info) &&
                           sscreen->info.has_image_opcodes &&
                           (sscreen->use_aco || nir->info.use_aco_amd || force_use_aco ||
                            /* Use ACO for streamout on gfx12 because it's faster. */
                            (sscreen->info.gfx_level >= GFX12 && nir->xfb_info &&
                             nir->xfb_info->output_count));

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* post_depth_coverage implies early_fragment_tests */
      nir->info.fs.early_fragment_tests |= nir->info.fs.post_depth_coverage;
   }

   memset(info, 0, sizeof(*info));
   info->base = nir->info;

   /* Get options from shader profiles. */
   for (unsigned i = 0; i < ARRAY_SIZE(si_shader_profiles); i++) {
      if (_mesa_printed_blake3_equal(nir->info.source_blake3, si_shader_profiles[i].blake3)) {
         info->options = si_shader_profiles[i].options;
         break;
      }
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
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
      nir_tcs_info tcs_info;
      nir_gather_tcs_info(nir, &tcs_info, nir->info.tess._primitive_mode,
                          nir->info.tess.spacing);

      info->tessfactors_are_def_in_all_invocs = tcs_info.all_invocations_define_tess_levels;
   }

   /* tess factors are loaded as input instead of system value */
   info->reads_tess_factors = nir->info.inputs_read &
      (BITFIELD64_BIT(VARYING_SLOT_TESS_LEVEL_INNER) |
       BITFIELD64_BIT(VARYING_SLOT_TESS_LEVEL_OUTER));

   info->uses_frontface = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRONT_FACE) |
                          BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRONT_FACE_FSIGN);
   info->uses_instanceid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   info->uses_base_vertex = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_VERTEX);
   info->uses_base_instance = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE);
   info->uses_invocationid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INVOCATION_ID);
   info->uses_grid_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_WORKGROUPS);
   info->uses_tg_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_SUBGROUPS);
   if (sscreen->info.gfx_level < GFX12) {
      info->uses_tg_size |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_LOCAL_INVOCATION_INDEX) ||
                            BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SUBGROUP_ID) ||
                            si_should_clear_lds(sscreen, nir);
   }
   info->uses_variable_block_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_WORKGROUP_SIZE);
   info->uses_drawid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
   info->uses_primid = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID) ||
                       nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID;
   info->reads_samplemask = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);
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
      if (nir->info.fs.color_is_dual_source)
         info->colors_written |= 0x2;
      if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_COLOR)) {
         info->colors_written |= 0x1;
         info->color0_writes_all_cbufs = info->colors_written == 0x1;

      }
   } else {
      info->writes_primid = nir->info.outputs_written & VARYING_BIT_PRIMITIVE_ID;
      info->writes_viewport_index = nir->info.outputs_written & VARYING_BIT_VIEWPORT;
      info->writes_layer = nir->info.outputs_written & VARYING_BIT_LAYER;
      info->writes_psize = nir->info.outputs_written & VARYING_BIT_PSIZ;
      info->writes_clipvertex = nir->info.outputs_written & VARYING_BIT_CLIP_VERTEX;
      info->writes_edgeflag = nir->info.outputs_written & VARYING_BIT_EDGE;
      info->writes_position = nir->info.outputs_written & VARYING_BIT_POS;
   }

   nir_function_impl *impl = nir_shader_get_entrypoint((nir_shader*)nir);
   nir_foreach_block (block, impl) {
      nir_foreach_instr (instr, block)
         scan_instruction(nir, info, instr, colors_lowered);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      info->num_streamout_components = 0;
      for (unsigned i = 0; i < info->num_outputs; i++)
         info->num_streamout_components += util_bitcount(info->output_xfb_writemask[i]);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL) {
      /* Add the PrimitiveID output, but don't increment num_outputs.
       * The driver inserts PrimitiveID only when it's used by the pixel shader,
       * and si_emit_spi_map uses this unconditionally when such a pixel shader is used.
       */
      info->output_semantic[info->num_outputs] = VARYING_SLOT_PRIMITIVE_ID;
      info->output_type[info->num_outputs] = nir_type_uint32;
      info->output_usagemask[info->num_outputs] = 0x1;
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      info->output_z_equals_input_z &= !info->output_z_is_not_input_z;
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

      info->uses_vmem_load_other |= nir->info.fs.uses_fbfetch_output;

      /* Add both front and back color inputs. */
      unsigned num_inputs_with_colors = info->num_inputs;
      for (unsigned back = 0; back < 2; back++) {
         for (unsigned i = 0; i < 2; i++) {
            if ((info->colors_read >> (i * 4)) & 0xf) {
               unsigned index = num_inputs_with_colors;

               info->input[index].semantic = (back ? VARYING_SLOT_BFC0 : VARYING_SLOT_COL0) + i;
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
   info->has_divergent_loop = nir_has_divergent_loop((nir_shader*)nir);

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      info->num_vs_inputs =
         nir->info.stage == MESA_SHADER_VERTEX && !nir->info.vs.blit_sgprs_amd ? info->num_inputs : 0;
      unsigned num_vbos_in_sgprs = si_num_vbos_in_user_sgprs_inline(sscreen->info.gfx_level);
      info->num_vbos_in_user_sgprs = MIN2(info->num_vs_inputs, num_vbos_in_sgprs);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_TESS_CTRL ||
       nir->info.stage == MESA_SHADER_TESS_EVAL) {
      info->esgs_vertex_stride =
         util_last_bit64(info->ls_es_outputs_written) * 16;

      /* For the ESGS ring in LDS, add 1 dword to reduce LDS bank
       * conflicts, i.e. each vertex will start on a different bank.
       */
      if (sscreen->info.gfx_level >= GFX9) {
         if (info->esgs_vertex_stride)
            info->esgs_vertex_stride += 4;
      } else {
         assert(((info->esgs_vertex_stride / 4) & C_028AAC_ITEMSIZE) == 0);
      }

      info->tcs_inputs_via_temp = nir->info.tess.tcs_same_invocation_inputs_read;
      info->tcs_inputs_via_lds = nir->info.tess.tcs_cross_invocation_inputs_read |
                                 (nir->info.tess.tcs_same_invocation_inputs_read &
                                  nir->info.inputs_read_indirectly);
   }

   if (nir->info.stage == MESA_SHADER_GEOMETRY) {
      info->gsvs_vertex_size = info->num_outputs * 16;
      info->max_gsvs_emit_size = info->gsvs_vertex_size * nir->info.gs.vertices_out;
      info->gs_input_verts_per_prim =
         mesa_vertices_per_prim(nir->info.gs.input_primitive);
   }

   info->clipdist_mask = info->writes_clipvertex ? SI_USER_CLIP_PLANE_MASK :
                         u_bit_consecutive(0, nir->info.clip_distance_array_size);
   info->culldist_mask = u_bit_consecutive(0, nir->info.cull_distance_array_size) <<
                         nir->info.clip_distance_array_size;

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      for (unsigned i = 0; i < info->num_inputs; i++) {
         unsigned semantic = info->input[i].semantic;

         if ((semantic <= VARYING_SLOT_VAR31 || semantic >= VARYING_SLOT_VAR0_16BIT) &&
             semantic != VARYING_SLOT_PNTC) {
            info->inputs_read |= 1ull << si_shader_io_get_unique_index(semantic);
         }
      }

      for (unsigned i = 0; i < 8; i++)
         if (info->colors_written & (1 << i))
            info->colors_written_4bit |= 0xf << (4 * i);

      for (unsigned i = 0; i < info->num_inputs; i++) {
         if (info->input[i].semantic == VARYING_SLOT_COL0)
            info->color_attr_index[0] = i;
         else if (info->input[i].semantic == VARYING_SLOT_COL1)
            info->color_attr_index[1] = i;
      }
   }
}

enum ac_hw_stage
si_select_hw_stage(const gl_shader_stage stage, const union si_shader_key *const key,
                   const enum amd_gfx_level gfx_level)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      if (key->ge.as_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else if (key->ge.as_es)
         return gfx_level >= GFX9 ? AC_HW_LEGACY_GEOMETRY_SHADER : AC_HW_EXPORT_SHADER;
      else if (key->ge.as_ls)
         return gfx_level >= GFX9 ? AC_HW_HULL_SHADER : AC_HW_LOCAL_SHADER;
      else
         return AC_HW_VERTEX_SHADER;
   case MESA_SHADER_TESS_CTRL:
      return AC_HW_HULL_SHADER;
   case MESA_SHADER_GEOMETRY:
      if (key->ge.as_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else
         return AC_HW_LEGACY_GEOMETRY_SHADER;
   case MESA_SHADER_FRAGMENT:
      return AC_HW_PIXEL_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return AC_HW_COMPUTE_SHADER;
   default:
      unreachable("Unsupported HW stage");
   }
}
