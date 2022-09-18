/*
 * Copyright © 2019 Valve Corporation.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include "radv_shader_args.h"
#include "radv_private.h"
#include "radv_shader.h"

static void
set_loc(struct radv_userdata_info *ud_info, uint8_t *sgpr_idx, uint8_t num_sgprs)
{
   ud_info->sgpr_idx = *sgpr_idx;
   ud_info->num_sgprs = num_sgprs;
   *sgpr_idx += num_sgprs;
}

static void
set_loc_shader(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx, uint8_t num_sgprs)
{
   struct radv_userdata_info *ud_info = &args->user_sgprs_locs.shader_data[idx];
   assert(ud_info);

   set_loc(ud_info, sgpr_idx, num_sgprs);
}

static void
set_loc_shader_ptr(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx)
{
   bool use_32bit_pointers = idx != AC_UD_SCRATCH_RING_OFFSETS &&
                             idx != AC_UD_CS_TASK_RING_OFFSETS && idx != AC_UD_CS_SBT_DESCRIPTORS &&
                             idx != AC_UD_CS_RAY_LAUNCH_SIZE_ADDR;

   set_loc_shader(args, idx, sgpr_idx, use_32bit_pointers ? 1 : 2);
}

static void
set_loc_desc(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx)
{
   struct radv_userdata_locations *locs = &args->user_sgprs_locs;
   struct radv_userdata_info *ud_info = &locs->descriptor_sets[idx];
   assert(ud_info);

   set_loc(ud_info, sgpr_idx, 1);

   locs->descriptor_sets_enabled |= 1u << idx;
}

struct user_sgpr_info {
   uint64_t inline_push_constant_mask;
   bool inlined_all_push_consts;
   bool indirect_all_descriptor_sets;
   uint8_t remaining_sgprs;
};

static uint8_t
count_vs_user_sgprs(const struct radv_shader_info *info)
{
   uint8_t count = 1; /* vertex offset */

   if (info->vs.vb_desc_usage_mask)
      count++;
   if (info->vs.needs_draw_id)
      count++;
   if (info->vs.needs_base_instance)
      count++;

   return count;
}

static uint8_t
count_tes_user_sgprs(const struct radv_pipeline_key *key)
{
   unsigned count = 0;

   if (key->dynamic_patch_control_points)
      count++; /* tes_num_patches */

   return count;
}

static uint8_t
count_ms_user_sgprs(const struct radv_shader_info *info)
{
   uint8_t count = 1 + 3; /* firstTask + num_work_groups[3] */

   if (info->vs.needs_draw_id)
      count++;
   if (info->ms.has_task)
      count++;

   return count;
}

static unsigned
count_ngg_sgprs(const struct radv_shader_info *info, bool has_ngg_query)
{
   unsigned count = 0;

   if (has_ngg_query)
      count += 1; /* ngg_query_state */
   if (info->has_ngg_culling)
      count += 5; /* ngg_culling_settings + 4x ngg_viewport_* */

   return count;
}

static void
allocate_inline_push_consts(const struct radv_shader_info *info,
                            struct user_sgpr_info *user_sgpr_info)
{
   uint8_t remaining_sgprs = user_sgpr_info->remaining_sgprs;

   if (!info->inline_push_constant_mask)
      return;

   uint64_t mask = info->inline_push_constant_mask;
   uint8_t num_push_consts = util_bitcount64(mask);

   /* Disable the default push constants path if all constants can be inlined and if shaders don't
    * use dynamic descriptors.
    */
   if (num_push_consts <= MIN2(remaining_sgprs + 1, AC_MAX_INLINE_PUSH_CONSTS) &&
       info->can_inline_all_push_constants && !info->loads_dynamic_offsets) {
      user_sgpr_info->inlined_all_push_consts = true;
      remaining_sgprs++;
   } else {
      /* Clamp to the maximum number of allowed inlined push constants. */
      while (num_push_consts > MIN2(remaining_sgprs, AC_MAX_INLINE_PUSH_CONSTS_WITH_INDIRECT)) {
         num_push_consts--;
         mask &= ~BITFIELD64_BIT(util_last_bit64(mask) - 1);
      }
   }

   user_sgpr_info->remaining_sgprs = remaining_sgprs - util_bitcount64(mask);
   user_sgpr_info->inline_push_constant_mask = mask;
}

static void
allocate_user_sgprs(enum amd_gfx_level gfx_level, const struct radv_shader_info *info,
                    struct radv_shader_args *args, gl_shader_stage stage, bool has_previous_stage,
                    gl_shader_stage previous_stage, bool needs_view_index, bool has_ngg_query,
                    const struct radv_pipeline_key *key,
                    struct user_sgpr_info *user_sgpr_info)
{
   uint8_t user_sgpr_count = 0;

   memset(user_sgpr_info, 0, sizeof(struct user_sgpr_info));

   /* 2 user sgprs will always be allocated for scratch/rings */
   user_sgpr_count += 2;

   if (stage == MESA_SHADER_TASK)
      user_sgpr_count += 2; /* task descriptors */

   /* prolog inputs */
   if (info->vs.has_prolog)
      user_sgpr_count += 2;

   switch (stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      if (info->cs.uses_sbt)
         user_sgpr_count += 2;
      if (info->cs.uses_grid_size)
         user_sgpr_count += args->load_grid_size_from_user_sgpr ? 3 : 2;
      if (info->cs.uses_ray_launch_size)
         user_sgpr_count += 2;
      if (info->vs.needs_draw_id)
         user_sgpr_count += 1;
      if (stage == MESA_SHADER_TASK)
         user_sgpr_count += 4; /* ring_entry, 2x ib_addr, ib_stride */
      break;
   case MESA_SHADER_FRAGMENT:
      /* epilog continue PC */
      if (info->ps.has_epilog)
         user_sgpr_count += 1;
      break;
   case MESA_SHADER_VERTEX:
      if (!args->is_gs_copy_shader)
         user_sgpr_count += count_vs_user_sgprs(info);
      break;
   case MESA_SHADER_TESS_CTRL:
      if (has_previous_stage) {
         if (previous_stage == MESA_SHADER_VERTEX)
            user_sgpr_count += count_vs_user_sgprs(info);
      }
      if (key->dynamic_patch_control_points)
         user_sgpr_count += 1; /* tcs_offchip_layout */
      break;
   case MESA_SHADER_TESS_EVAL:
      count_tes_user_sgprs(key);
      break;
   case MESA_SHADER_GEOMETRY:
      if (has_previous_stage) {
         if (info->is_ngg)
            user_sgpr_count += count_ngg_sgprs(info, has_ngg_query);

         if (previous_stage == MESA_SHADER_VERTEX) {
            user_sgpr_count += count_vs_user_sgprs(info);
         } else if (previous_stage == MESA_SHADER_TESS_EVAL) {
            user_sgpr_count += count_tes_user_sgprs(key);
         } else if (previous_stage == MESA_SHADER_MESH) {
            user_sgpr_count += count_ms_user_sgprs(info);
         }
      }
      break;
   default:
      break;
   }

   if (needs_view_index)
      user_sgpr_count++;

   if (info->force_vrs_per_vertex)
      user_sgpr_count++;

   if (info->loads_push_constants)
      user_sgpr_count++;

   if (info->so.num_outputs)
      user_sgpr_count++;

   uint32_t available_sgprs =
      gfx_level >= GFX9 && stage != MESA_SHADER_COMPUTE && stage != MESA_SHADER_TASK ? 32 : 16;
   uint32_t remaining_sgprs = available_sgprs - user_sgpr_count;
   uint32_t num_desc_set = util_bitcount(info->desc_set_used_mask);

   if (remaining_sgprs < num_desc_set) {
      user_sgpr_info->indirect_all_descriptor_sets = true;
      user_sgpr_info->remaining_sgprs = remaining_sgprs - 1;
   } else {
      user_sgpr_info->remaining_sgprs = remaining_sgprs - num_desc_set;
   }

   allocate_inline_push_consts(info, user_sgpr_info);
}

static void
declare_global_input_sgprs(const struct radv_shader_info *info,
                           const struct user_sgpr_info *user_sgpr_info,
                           struct radv_shader_args *args)
{
   /* 1 for each descriptor set */
   if (!user_sgpr_info->indirect_all_descriptor_sets) {
      uint32_t mask = info->desc_set_used_mask;

      while (mask) {
         int i = u_bit_scan(&mask);

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR, &args->descriptor_sets[i]);
      }
   } else {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR_PTR, &args->descriptor_sets[0]);
   }

   if (info->loads_push_constants && !user_sgpr_info->inlined_all_push_consts) {
      /* 1 for push constants and dynamic descriptors */
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR, &args->ac.push_constants);
   }

   for (unsigned i = 0; i < util_bitcount64(user_sgpr_info->inline_push_constant_mask); i++) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.inline_push_consts[i]);
   }
   args->ac.inline_push_const_mask = user_sgpr_info->inline_push_constant_mask;

   if (info->so.num_outputs) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR, &args->streamout_buffers);
   }
}

static void
declare_vs_specific_input_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                                gl_shader_stage stage, bool has_previous_stage,
                                gl_shader_stage previous_stage)
{
   if (info->vs.has_prolog)
      ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_INT, &args->prolog_inputs);

   if (!args->is_gs_copy_shader && (stage == MESA_SHADER_VERTEX ||
                                    (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
      if (info->vs.vb_desc_usage_mask) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR, &args->ac.vertex_buffers);
      }
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.base_vertex);
      if (info->vs.needs_draw_id) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.draw_id);
      }
      if (info->vs.needs_base_instance) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.start_instance);
      }
   }
}

static void
declare_vs_input_vgprs(enum amd_gfx_level gfx_level, const struct radv_shader_info *info,
                       struct radv_shader_args *args, bool merged_vs_tcs)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vertex_id);
   if (!args->is_gs_copy_shader) {
      if (info->vs.as_ls || merged_vs_tcs) {

         if (gfx_level >= GFX11) {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
         } else if (gfx_level >= GFX10) {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_rel_patch_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
         } else {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_rel_patch_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
         }
      } else {
         if (gfx_level >= GFX10) {
            if (info->is_ngg) {
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            } else {
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_prim_id);
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            }
         } else {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_prim_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
         }
      }
   }

   if (info->vs.dynamic_inputs) {
      assert(info->vs.use_per_attribute_vb_descs);
      unsigned num_attributes = util_last_bit(info->vs.input_slot_usage_mask);
      for (unsigned i = 0; i < num_attributes; i++)
         ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_INT, &args->vs_inputs[i]);
      /* Ensure the main shader doesn't use less vgprs than the prolog. The prolog requires one
       * VGPR more than the number of shader arguments in the case of non-trivial divisors on GFX8.
       */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL);
   }
}

static void
declare_streamout_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                        gl_shader_stage stage)
{
   int i;

   /* Streamout SGPRs. */
   if (info->so.num_outputs) {
      assert(stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_config);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_write_index);
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
   }

   /* A streamout buffer offset is loaded if the stride is non-zero. */
   for (i = 0; i < 4; i++) {
      if (!info->so.strides[i])
         continue;

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_offset[i]);
   }
}

static void
declare_tes_input_vgprs(struct radv_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.tes_u);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.tes_v);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tes_rel_patch_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tes_patch_id);
}

static void
declare_ms_input_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.base_vertex);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_INT, &args->ac.num_work_groups);
   if (info->vs.needs_draw_id) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.draw_id);
   }
   if (info->ms.has_task) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.task_ring_entry);
   }
}

static void
declare_ms_input_vgprs(struct radv_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vertex_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* instance_id */
}

static void
declare_ps_input_vgprs(const struct radv_shader_info *info, struct radv_shader_args *args)
{
   unsigned spi_ps_input = info->ps.spi_ps_input;

   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_centroid);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.pull_model);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_centroid);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL); /* line stipple tex */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[0]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[1]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[2]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[3]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.front_face);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.ancillary);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.sample_coverage);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* fixed pt */

   if (args->remap_spi_ps_input) {
      /* LLVM optimizes away unused FS inputs and computes spi_ps_input_addr itself and then
       * communicates the results back via the ELF binary. Mirror what LLVM does by re-mapping the
       * VGPR arguments here.
       */
      unsigned arg_count = 0;
      for (unsigned i = 0, vgpr_arg = 0, vgpr_reg = 0; i < args->ac.arg_count; i++) {
         if (args->ac.args[i].file != AC_ARG_VGPR) {
            arg_count++;
            continue;
         }

         if (!(spi_ps_input & (1 << vgpr_arg))) {
            args->ac.args[i].skip = true;
         } else {
            args->ac.args[i].offset = vgpr_reg;
            vgpr_reg += args->ac.args[i].size;
            arg_count++;
         }
         vgpr_arg++;
      }
   }

   if (info->ps.has_epilog) {
      /* FIXME: Ensure the main shader doesn't have less VGPRs than the epilog */
      for (unsigned i = 0; i < MAX_RTS; i++)
         ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_INT, NULL);
   }
}

static void
declare_ngg_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                  bool has_ngg_query)
{
   if (has_ngg_query)
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_query_state);

   if (info->has_ngg_culling) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_culling_settings);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_viewport_scale[0]);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_viewport_scale[1]);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_viewport_translate[0]);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ngg_viewport_translate[1]);
   }
}

static void
set_global_input_locs(struct radv_shader_args *args, const struct user_sgpr_info *user_sgpr_info,
                      uint8_t *user_sgpr_idx)
{
   if (!user_sgpr_info->indirect_all_descriptor_sets) {
      for (unsigned i = 0; i < ARRAY_SIZE(args->descriptor_sets); i++) {
         if (args->descriptor_sets[i].used)
            set_loc_desc(args, i, user_sgpr_idx);
      }
   } else {
      set_loc_shader_ptr(args, AC_UD_INDIRECT_DESCRIPTOR_SETS, user_sgpr_idx);
   }

   if (args->ac.push_constants.used) {
      set_loc_shader_ptr(args, AC_UD_PUSH_CONSTANTS, user_sgpr_idx);
   }

   if (user_sgpr_info->inline_push_constant_mask) {
      set_loc_shader(args, AC_UD_INLINE_PUSH_CONSTANTS, user_sgpr_idx,
                     util_bitcount64(user_sgpr_info->inline_push_constant_mask));
   }

   if (args->streamout_buffers.used) {
      set_loc_shader_ptr(args, AC_UD_STREAMOUT_BUFFERS, user_sgpr_idx);
   }
}

static void
set_vs_specific_input_locs(struct radv_shader_args *args, gl_shader_stage stage,
                           bool has_previous_stage, gl_shader_stage previous_stage,
                           uint8_t *user_sgpr_idx)
{
   if (args->prolog_inputs.used)
      set_loc_shader(args, AC_UD_VS_PROLOG_INPUTS, user_sgpr_idx, 2);

   if (!args->is_gs_copy_shader && (stage == MESA_SHADER_VERTEX ||
                                    (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
      if (args->ac.vertex_buffers.used) {
         set_loc_shader_ptr(args, AC_UD_VS_VERTEX_BUFFERS, user_sgpr_idx);
      }

      unsigned vs_num = args->ac.base_vertex.used + args->ac.draw_id.used +
                        args->ac.start_instance.used;
      set_loc_shader(args, AC_UD_VS_BASE_VERTEX_START_INSTANCE, user_sgpr_idx, vs_num);
   }
}

static void
set_ms_input_locs(struct radv_shader_args *args, uint8_t *user_sgpr_idx)
{
   unsigned vs_num =
      args->ac.base_vertex.used + 3 * args->ac.num_work_groups.used + args->ac.draw_id.used;
   set_loc_shader(args, AC_UD_VS_BASE_VERTEX_START_INSTANCE, user_sgpr_idx, vs_num);

   if (args->ac.task_ring_entry.used)
      set_loc_shader(args, AC_UD_TASK_RING_ENTRY, user_sgpr_idx, 1);
}

void
radv_declare_shader_args(enum amd_gfx_level gfx_level, const struct radv_pipeline_key *key,
                         const struct radv_shader_info *info, gl_shader_stage stage,
                         bool has_previous_stage, gl_shader_stage previous_stage,
                         struct radv_shader_args *args)
{
   struct user_sgpr_info user_sgpr_info;
   bool needs_view_index = info->uses_view_index;
   bool has_ngg_query = stage == MESA_SHADER_GEOMETRY || key->primitives_generated_query;

   if (gfx_level >= GFX10 && info->is_ngg && stage != MESA_SHADER_GEOMETRY) {
      /* Handle all NGG shaders as GS to simplify the code here. */
      previous_stage = stage;
      stage = MESA_SHADER_GEOMETRY;
      has_previous_stage = true;
   }

   for (int i = 0; i < MAX_SETS; i++)
      args->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
   for (int i = 0; i < AC_UD_MAX_UD; i++)
      args->user_sgprs_locs.shader_data[i].sgpr_idx = -1;

   allocate_user_sgprs(gfx_level, info, args, stage, has_previous_stage, previous_stage,
                       needs_view_index, has_ngg_query, key, &user_sgpr_info);

   if (args->explicit_scratch_args) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->ring_offsets);
   }
   if (stage == MESA_SHADER_TASK) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->task_ring_offsets);
   }

   /* To ensure prologs match the main VS, VS specific input SGPRs have to be placed before other
    * sgprs.
    */

   switch (stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      declare_global_input_sgprs(info, &user_sgpr_info, args);

      if (info->cs.uses_sbt) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_PTR, &args->ac.sbt_descriptors);
      }

      if (info->cs.uses_grid_size) {
         if (args->load_grid_size_from_user_sgpr)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_INT, &args->ac.num_work_groups);
         else
            ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_PTR, &args->ac.num_work_groups);
      }

      if (info->cs.uses_ray_launch_size) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_PTR, &args->ac.ray_launch_size_addr);
      }

      if (info->vs.needs_draw_id) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.draw_id);
      }

      if (stage == MESA_SHADER_TASK) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.task_ring_entry);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_INT, &args->task_ib_addr);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->task_ib_stride);
      }

      for (int i = 0; i < 3; i++) {
         if (info->cs.uses_block_id[i]) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.workgroup_ids[i]);
         }
      }

      if (info->cs.uses_local_invocation_idx) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tg_size);
      }

      if (args->explicit_scratch_args && gfx_level < GFX11) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      if (gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.local_invocation_ids);
      else
         ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.local_invocation_ids);
      break;
   case MESA_SHADER_VERTEX:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_vs_specific_input_sgprs(info, args, stage, has_previous_stage, previous_stage);

      declare_global_input_sgprs(info, &user_sgpr_info, args);

      if (needs_view_index) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);
      }

      if (info->force_vrs_per_vertex) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.force_vrs_rates);
      }

      if (info->vs.as_es) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.es2gs_offset);
      } else if (info->vs.as_ls) {
         /* no extra parameters */
      } else {
         declare_streamout_sgprs(info, args, stage);
      }

      if (args->explicit_scratch_args) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      declare_vs_input_vgprs(gfx_level, info, args, false);
      break;
   case MESA_SHADER_TESS_CTRL:
      if (has_previous_stage) {
         // First 6 system regs
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.merged_wave_info);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_factor_offset);

         if (gfx_level >= GFX11) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_wave_id);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

         declare_vs_specific_input_sgprs(info, args, stage, has_previous_stage, previous_stage);

         declare_global_input_sgprs(info, &user_sgpr_info, args);

         if (needs_view_index) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);
         }

         if (key->dynamic_patch_control_points) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->tcs_offchip_layout);
         }

         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_rel_ids);

         declare_vs_input_vgprs(gfx_level, info, args, true);
      } else {
         declare_global_input_sgprs(info, &user_sgpr_info, args);

         if (needs_view_index) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);
         }

         if (key->dynamic_patch_control_points) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->tcs_offchip_layout);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_factor_offset);
         if (args->explicit_scratch_args) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_rel_ids);
      }
      break;
   case MESA_SHADER_TESS_EVAL:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_global_input_sgprs(info, &user_sgpr_info, args);

      if (needs_view_index)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);

      if (key->dynamic_patch_control_points)
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->tes_num_patches);

      if (info->tes.as_es) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.es2gs_offset);
      } else {
         declare_streamout_sgprs(info, args, stage);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
      }
      if (args->explicit_scratch_args) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }
      declare_tes_input_vgprs(args);
      break;
   case MESA_SHADER_GEOMETRY:
      if (has_previous_stage) {
         // First 6 system regs
         if (info->is_ngg) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs_tg_info);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs2vs_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.merged_wave_info);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);

         if (gfx_level < GFX11) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

         if (previous_stage == MESA_SHADER_VERTEX) {
            declare_vs_specific_input_sgprs(info, args, stage, has_previous_stage, previous_stage);
         } else if (previous_stage == MESA_SHADER_MESH) {
            declare_ms_input_sgprs(info, args);
         }

         declare_global_input_sgprs(info, &user_sgpr_info, args);

         if (needs_view_index) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);
         }

         if (previous_stage == MESA_SHADER_TESS_EVAL && key->dynamic_patch_control_points)
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->tes_num_patches);

         if (info->force_vrs_per_vertex) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.force_vrs_rates);
         }

         if (info->is_ngg) {
            declare_ngg_sgprs(info, args, has_ngg_query);
         }

         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[1]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_invocation_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[2]);

         if (previous_stage == MESA_SHADER_VERTEX) {
            declare_vs_input_vgprs(gfx_level, info, args, false);
         } else if (previous_stage == MESA_SHADER_TESS_EVAL) {
            declare_tes_input_vgprs(args);
         } else if (previous_stage == MESA_SHADER_MESH) {
            declare_ms_input_vgprs(args);
         }
      } else {
         declare_global_input_sgprs(info, &user_sgpr_info, args);

         if (needs_view_index) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.view_index);
         }

         if (info->force_vrs_per_vertex) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.force_vrs_rates);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs2vs_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs_wave_id);
         if (args->explicit_scratch_args) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[1]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[2]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[3]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[4]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[5]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_invocation_id);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      declare_global_input_sgprs(info, &user_sgpr_info, args);

      if (info->ps.has_epilog) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ps_epilog_pc);
      }

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.prim_mask);
      if (args->explicit_scratch_args && gfx_level < GFX11) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      declare_ps_input_vgprs(info, args);
      break;
   default:
      unreachable("Shader stage not implemented");
   }

   uint8_t user_sgpr_idx = 0;

   set_loc_shader_ptr(args, AC_UD_SCRATCH_RING_OFFSETS, &user_sgpr_idx);
   if (stage == MESA_SHADER_TASK) {
      set_loc_shader_ptr(args, AC_UD_CS_TASK_RING_OFFSETS, &user_sgpr_idx);
   }

   /* For merged shaders the user SGPRs start at 8, with 8 system SGPRs in front (including
    * the rw_buffers at s0/s1. With user SGPR0 = s8, lets restart the count from 0 */
   if (has_previous_stage)
      user_sgpr_idx = 0;

   if (stage == MESA_SHADER_VERTEX || (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))
      set_vs_specific_input_locs(args, stage, has_previous_stage, previous_stage, &user_sgpr_idx);
   else if (has_previous_stage && previous_stage == MESA_SHADER_MESH)
      set_ms_input_locs(args, &user_sgpr_idx);

   set_global_input_locs(args, &user_sgpr_info, &user_sgpr_idx);

   switch (stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      if (args->ac.sbt_descriptors.used) {
         set_loc_shader_ptr(args, AC_UD_CS_SBT_DESCRIPTORS, &user_sgpr_idx);
      }
      if (args->ac.num_work_groups.used) {
         set_loc_shader(args, AC_UD_CS_GRID_SIZE, &user_sgpr_idx,
                        args->load_grid_size_from_user_sgpr ? 3 : 2);
      }
      if (args->ac.ray_launch_size_addr.used) {
         set_loc_shader_ptr(args, AC_UD_CS_RAY_LAUNCH_SIZE_ADDR, &user_sgpr_idx);
      }
      if (args->ac.draw_id.used) {
         set_loc_shader(args, AC_UD_CS_TASK_DRAW_ID, &user_sgpr_idx, 1);
      }
      if (args->ac.task_ring_entry.used) {
         set_loc_shader(args, AC_UD_TASK_RING_ENTRY, &user_sgpr_idx, 1);
      }
      if (args->task_ib_addr.used) {
         assert(args->task_ib_stride.used);
         set_loc_shader(args, AC_UD_CS_TASK_IB, &user_sgpr_idx, 3);
      }
      break;
   case MESA_SHADER_VERTEX:
      if (args->ac.view_index.used)
         set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
      if (args->ac.force_vrs_rates.used)
         set_loc_shader(args, AC_UD_FORCE_VRS_RATES, &user_sgpr_idx, 1);
      break;
   case MESA_SHADER_TESS_CTRL:
      if (args->ac.view_index.used)
         set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);

      if (args->tcs_offchip_layout.used)
         set_loc_shader(args, AC_UD_TCS_OFFCHIP_LAYOUT, &user_sgpr_idx, 1);
      break;
   case MESA_SHADER_TESS_EVAL:
      if (args->ac.view_index.used)
         set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);

      if (args->tes_num_patches.used)
         set_loc_shader(args, AC_UD_TES_NUM_PATCHES, &user_sgpr_idx, 1);
      break;
   case MESA_SHADER_GEOMETRY:
      if (args->ac.view_index.used)
         set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);

      if (args->tes_num_patches.used)
         set_loc_shader(args, AC_UD_TES_NUM_PATCHES, &user_sgpr_idx, 1);

      if (args->ac.force_vrs_rates.used)
         set_loc_shader(args, AC_UD_FORCE_VRS_RATES, &user_sgpr_idx, 1);

      if (args->ngg_query_state.used) {
         set_loc_shader(args, AC_UD_NGG_QUERY_STATE, &user_sgpr_idx, 1);
      }

      if (args->ngg_culling_settings.used) {
         set_loc_shader(args, AC_UD_NGG_CULLING_SETTINGS, &user_sgpr_idx, 1);
      }

      if (args->ngg_viewport_scale[0].used) {
         assert(args->ngg_viewport_scale[1].used &&
                args->ngg_viewport_translate[0].used &&
                args->ngg_viewport_translate[1].used);
         set_loc_shader(args, AC_UD_NGG_VIEWPORT, &user_sgpr_idx, 4);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      if (args->ps_epilog_pc.used)
         set_loc_shader(args, AC_UD_PS_EPILOG_PC, &user_sgpr_idx, 1);
      break;
   default:
      unreachable("Shader stage not implemented");
   }

   args->num_user_sgprs = user_sgpr_idx;
}

void
radv_declare_ps_epilog_args(enum amd_gfx_level gfx_level, const struct radv_ps_epilog_key *key,
                            struct radv_shader_args *args)
{
   unsigned num_inputs = 0;

   ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->ring_offsets);
   if (gfx_level < GFX11)
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);

   /* Declare VGPR arguments for color exports. */
   for (unsigned i = 0; i < MAX_RTS; i++) {
      unsigned col_format = (key->spi_shader_col_format >> (i * 4)) & 0xf;

      if (col_format == V_028714_SPI_SHADER_ZERO)
         continue;

      ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_FLOAT, &args->ps_epilog_inputs[num_inputs]);
      num_inputs++;
   }
}
