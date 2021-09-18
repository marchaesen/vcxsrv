/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "ac_exp_param.h"
#include "ac_rtld.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "tgsi/tgsi_from_mesa.h"
#include "tgsi/tgsi_strings.h"
#include "util/u_memory.h"

static const char scratch_rsrc_dword0_symbol[] = "SCRATCH_RSRC_DWORD0";

static const char scratch_rsrc_dword1_symbol[] = "SCRATCH_RSRC_DWORD1";

static void si_dump_shader_key(const struct si_shader *shader, FILE *f);

/** Whether the shader runs as a combination of multiple API shaders */
bool si_is_multi_part_shader(struct si_shader *shader)
{
   if (shader->selector->screen->info.chip_class <= GFX8)
      return false;

   return shader->key.as_ls || shader->key.as_es ||
          shader->selector->info.stage == MESA_SHADER_TESS_CTRL ||
          shader->selector->info.stage == MESA_SHADER_GEOMETRY;
}

/** Whether the shader runs on a merged HW stage (LSHS or ESGS) */
bool si_is_merged_shader(struct si_shader *shader)
{
   return shader->key.as_ngg || si_is_multi_part_shader(shader);
}

/**
 * Returns a unique index for a per-patch semantic name and index. The index
 * must be less than 32, so that a 32-bit bitmask of used inputs or outputs
 * can be calculated.
 */
unsigned si_shader_io_get_unique_index_patch(unsigned semantic)
{
   switch (semantic) {
   case VARYING_SLOT_TESS_LEVEL_OUTER:
      return 0;
   case VARYING_SLOT_TESS_LEVEL_INNER:
      return 1;
   default:
      if (semantic >= VARYING_SLOT_PATCH0 && semantic < VARYING_SLOT_PATCH0 + 30)
         return 2 + (semantic - VARYING_SLOT_PATCH0);

      assert(!"invalid semantic");
      return 0;
   }
}

/**
 * Returns a unique index for a semantic name and index. The index must be
 * less than 64, so that a 64-bit bitmask of used inputs or outputs can be
 * calculated.
 */
unsigned si_shader_io_get_unique_index(unsigned semantic, bool is_varying)
{
   switch (semantic) {
   case VARYING_SLOT_POS:
      return 0;
   default:
      /* Since some shader stages use the highest used IO index
       * to determine the size to allocate for inputs/outputs
       * (in LDS, tess and GS rings). GENERIC should be placed right
       * after POSITION to make that size as small as possible.
       */
      if (semantic >= VARYING_SLOT_VAR0 && semantic <= VARYING_SLOT_VAR31)
         return 1 + (semantic - VARYING_SLOT_VAR0); /* 1..32 */

      /* Put 16-bit GLES varyings after 32-bit varyings. They can use the same indices as
       * legacy desktop GL varyings because they are mutually exclusive.
       */
      if (semantic >= VARYING_SLOT_VAR0_16BIT && semantic <= VARYING_SLOT_VAR15_16BIT)
         return 33 + (semantic - VARYING_SLOT_VAR0_16BIT); /* 33..48 */

      assert(!"invalid generic index");
      return 0;

   /* Legacy desktop GL varyings. */
   case VARYING_SLOT_FOGC:
      return 33;
   case VARYING_SLOT_COL0:
      return 34;
   case VARYING_SLOT_COL1:
      return 35;
   case VARYING_SLOT_BFC0:
      /* If it's a varying, COLOR and BCOLOR alias. */
      if (is_varying)
         return 34;
      else
         return 36;
   case VARYING_SLOT_BFC1:
      if (is_varying)
         return 35;
      else
         return 37;
   case VARYING_SLOT_TEX0:
   case VARYING_SLOT_TEX1:
   case VARYING_SLOT_TEX2:
   case VARYING_SLOT_TEX3:
   case VARYING_SLOT_TEX4:
   case VARYING_SLOT_TEX5:
   case VARYING_SLOT_TEX6:
   case VARYING_SLOT_TEX7:
      return 38 + (semantic - VARYING_SLOT_TEX0);
   case VARYING_SLOT_CLIP_VERTEX:
      return 46;

   /* Varyings present in both GLES and desktop GL must start at 49 after 16-bit varyings. */
   case VARYING_SLOT_CLIP_DIST0:
      return 49;
   case VARYING_SLOT_CLIP_DIST1:
      return 50;
   case VARYING_SLOT_PSIZ:
      return 51;

   /* These can't be written by LS, HS, and ES. */
   case VARYING_SLOT_LAYER:
      return 52;
   case VARYING_SLOT_VIEWPORT:
      return 53;
   case VARYING_SLOT_PRIMITIVE_ID:
      return 54;
   }
}

static void si_dump_streamout(struct pipe_stream_output_info *so)
{
   unsigned i;

   if (so->num_outputs)
      fprintf(stderr, "STREAMOUT\n");

   for (i = 0; i < so->num_outputs; i++) {
      unsigned mask = ((1 << so->output[i].num_components) - 1) << so->output[i].start_component;
      fprintf(stderr, "  %i: BUF%i[%i..%i] <- OUT[%i].%s%s%s%s\n", i, so->output[i].output_buffer,
              so->output[i].dst_offset, so->output[i].dst_offset + so->output[i].num_components - 1,
              so->output[i].register_index, mask & 1 ? "x" : "", mask & 2 ? "y" : "",
              mask & 4 ? "z" : "", mask & 8 ? "w" : "");
   }
}

static void declare_streamout_params(struct si_shader_context *ctx,
                                     struct pipe_stream_output_info *so)
{
   if (ctx->screen->use_ngg_streamout) {
      if (ctx->stage == MESA_SHADER_TESS_EVAL)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
      return;
   }

   /* Streamout SGPRs. */
   if (so->num_outputs) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.streamout_config);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.streamout_write_index);
   } else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
   }

   /* A streamout buffer offset is loaded if the stride is non-zero. */
   for (int i = 0; i < 4; i++) {
      if (!so->stride[i])
         continue;

      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.streamout_offset[i]);
   }
}

unsigned si_get_max_workgroup_size(const struct si_shader *shader)
{
   switch (shader->selector->info.stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      return shader->key.as_ngg ? 128 : 0;

   case MESA_SHADER_TESS_CTRL:
      /* Return this so that LLVM doesn't remove s_barrier
       * instructions on chips where we use s_barrier. */
      return shader->selector->screen->info.chip_class >= GFX7 ? 128 : 0;

   case MESA_SHADER_GEOMETRY:
      return shader->selector->screen->info.chip_class >= GFX9 ? 128 : 0;

   case MESA_SHADER_COMPUTE:
      break; /* see below */

   default:
      return 0;
   }

   /* Compile a variable block size using the maximum variable size. */
   if (shader->selector->info.base.workgroup_size_variable)
      return SI_MAX_VARIABLE_THREADS_PER_BLOCK;

   uint16_t *local_size = shader->selector->info.base.workgroup_size;
   unsigned max_work_group_size = (uint32_t)local_size[0] *
                                  (uint32_t)local_size[1] *
                                  (uint32_t)local_size[2];
   assert(max_work_group_size);
   return max_work_group_size;
}

static void declare_const_and_shader_buffers(struct si_shader_context *ctx, bool assign_params)
{
   enum ac_arg_type const_shader_buf_type;

   if (ctx->shader->selector->info.base.num_ubos == 1 &&
       ctx->shader->selector->info.base.num_ssbos == 0)
      const_shader_buf_type = AC_ARG_CONST_FLOAT_PTR;
   else
      const_shader_buf_type = AC_ARG_CONST_DESC_PTR;

   ac_add_arg(
      &ctx->args, AC_ARG_SGPR, 1, const_shader_buf_type,
      assign_params ? &ctx->const_and_shader_buffers : &ctx->other_const_and_shader_buffers);
}

static void declare_samplers_and_images(struct si_shader_context *ctx, bool assign_params)
{
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_IMAGE_PTR,
              assign_params ? &ctx->samplers_and_images : &ctx->other_samplers_and_images);
}

static void declare_per_stage_desc_pointers(struct si_shader_context *ctx, bool assign_params)
{
   declare_const_and_shader_buffers(ctx, assign_params);
   declare_samplers_and_images(ctx, assign_params);
}

static void declare_global_desc_pointers(struct si_shader_context *ctx)
{
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR, &ctx->internal_bindings);
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_IMAGE_PTR,
              &ctx->bindless_samplers_and_images);
}

static void declare_vs_specific_input_sgprs(struct si_shader_context *ctx)
{
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->vs_state_bits);
   if (!ctx->shader->is_gs_copy_shader) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.base_vertex);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.draw_id);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.start_instance);
   }
}

static void declare_vb_descriptor_input_sgprs(struct si_shader_context *ctx)
{
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR, &ctx->args.vertex_buffers);

   unsigned num_vbos_in_user_sgprs = ctx->shader->selector->num_vbos_in_user_sgprs;
   if (num_vbos_in_user_sgprs) {
      unsigned user_sgprs = ctx->args.num_sgprs_used;

      if (si_is_merged_shader(ctx->shader))
         user_sgprs -= 8;
      assert(user_sgprs <= SI_SGPR_VS_VB_DESCRIPTOR_FIRST);

      /* Declare unused SGPRs to align VB descriptors to 4 SGPRs (hw requirement). */
      for (unsigned i = user_sgprs; i < SI_SGPR_VS_VB_DESCRIPTOR_FIRST; i++)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); /* unused */

      assert(num_vbos_in_user_sgprs <= ARRAY_SIZE(ctx->vb_descriptors));
      for (unsigned i = 0; i < num_vbos_in_user_sgprs; i++)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 4, AC_ARG_INT, &ctx->vb_descriptors[i]);
   }
}

static void declare_vs_input_vgprs(struct si_shader_context *ctx, unsigned *num_prolog_vgprs)
{
   struct si_shader *shader = ctx->shader;

   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.vertex_id);
   if (shader->key.as_ls) {
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.vs_rel_patch_id);
      if (ctx->screen->info.chip_class >= GFX10) {
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.instance_id);
      } else {
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.instance_id);
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
      }
   } else if (ctx->screen->info.chip_class >= GFX10) {
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT,
                 &ctx->args.vs_prim_id); /* user vgpr or PrimID (legacy) */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.instance_id);
   } else {
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.instance_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.vs_prim_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
   }

   if (!shader->is_gs_copy_shader) {
      /* Vertex load indices. */
      if (shader->selector->info.num_inputs) {
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->vertex_index0);
         for (unsigned i = 1; i < shader->selector->info.num_inputs; i++)
            ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, NULL);
      }
      *num_prolog_vgprs += shader->selector->info.num_inputs;
   }
}

static void declare_vs_blit_inputs(struct si_shader_context *ctx, unsigned vs_blit_property)
{
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->vs_blit_inputs); /* i16 x1, y1 */
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);                 /* i16 x1, y1 */
   ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL);               /* depth */

   if (vs_blit_property == SI_VS_BLIT_SGPRS_POS_COLOR) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* color0 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* color1 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* color2 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* color3 */
   } else if (vs_blit_property == SI_VS_BLIT_SGPRS_POS_TEXCOORD) {
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.x1 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.y1 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.x2 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.y2 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.z */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_FLOAT, NULL); /* texcoord.w */
   }
}

static void declare_tes_input_vgprs(struct si_shader_context *ctx)
{
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.tes_u);
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.tes_v);
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tes_rel_patch_id);
   ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tes_patch_id);
}

enum
{
   /* Convenient merged shader definitions. */
   SI_SHADER_MERGED_VERTEX_TESSCTRL = MESA_ALL_SHADER_STAGES,
   SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY,
};

void si_add_arg_checked(struct ac_shader_args *args, enum ac_arg_regfile file, unsigned registers,
                        enum ac_arg_type type, struct ac_arg *arg, unsigned idx)
{
   assert(args->arg_count == idx);
   ac_add_arg(args, file, registers, type, arg);
}

void si_init_shader_args(struct si_shader_context *ctx, bool ngg_cull_shader)
{
   struct si_shader *shader = ctx->shader;
   unsigned i, num_returns, num_return_sgprs;
   unsigned num_prolog_vgprs = 0;
   unsigned stage = ctx->stage;

   memset(&ctx->args, 0, sizeof(ctx->args));

   /* Set MERGED shaders. */
   if (ctx->screen->info.chip_class >= GFX9) {
      if (shader->key.as_ls || stage == MESA_SHADER_TESS_CTRL)
         stage = SI_SHADER_MERGED_VERTEX_TESSCTRL; /* LS or HS */
      else if (shader->key.as_es || shader->key.as_ngg || stage == MESA_SHADER_GEOMETRY)
         stage = SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY;
   }

   switch (stage) {
   case MESA_SHADER_VERTEX:
      declare_global_desc_pointers(ctx);

      if (shader->selector->info.base.vs.blit_sgprs_amd) {
         declare_vs_blit_inputs(ctx, shader->selector->info.base.vs.blit_sgprs_amd);

         /* VGPRs */
         declare_vs_input_vgprs(ctx, &num_prolog_vgprs);
         break;
      }

      declare_per_stage_desc_pointers(ctx, true);
      declare_vs_specific_input_sgprs(ctx);
      if (!shader->is_gs_copy_shader)
         declare_vb_descriptor_input_sgprs(ctx);

      if (shader->key.as_es) {
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.es2gs_offset);
      } else if (shader->key.as_ls) {
         /* no extra parameters */
      } else {
         /* The locations of the other parameters are assigned dynamically. */
         declare_streamout_params(ctx, &shader->selector->so);
      }

      /* VGPRs */
      declare_vs_input_vgprs(ctx, &num_prolog_vgprs);
      break;

   case MESA_SHADER_TESS_CTRL: /* GFX6-GFX8 */
      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, true);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_offsets);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->vs_state_bits);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tcs_factor_offset);

      /* VGPRs */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tcs_patch_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tcs_rel_ids);

      /* param_tcs_offchip_offset and param_tcs_factor_offset are
       * placed after the user SGPRs.
       */
      for (i = 0; i < GFX6_TCS_NUM_USER_SGPR + 2; i++)
         ac_add_return(&ctx->args, AC_ARG_SGPR);
      for (i = 0; i < 11; i++)
         ac_add_return(&ctx->args, AC_ARG_VGPR);
      break;

   case SI_SHADER_MERGED_VERTEX_TESSCTRL:
      /* Merged stages have 8 system SGPRs at the beginning. */
      /* SPI_SHADER_USER_DATA_ADDR_LO/HI_HS */
      declare_per_stage_desc_pointers(ctx, ctx->stage == MESA_SHADER_TESS_CTRL);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.merged_wave_info);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tcs_factor_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.scratch_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); /* unused */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); /* unused */

      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, ctx->stage == MESA_SHADER_VERTEX);
      declare_vs_specific_input_sgprs(ctx);

      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_offsets);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_out_lds_layout);
      if (ctx->stage == MESA_SHADER_VERTEX)
         declare_vb_descriptor_input_sgprs(ctx);

      /* VGPRs (first TCS, then VS) */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tcs_patch_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.tcs_rel_ids);

      if (ctx->stage == MESA_SHADER_VERTEX) {
         declare_vs_input_vgprs(ctx, &num_prolog_vgprs);

         /* LS return values are inputs to the TCS main shader part. */
         for (i = 0; i < 8 + GFX9_TCS_NUM_USER_SGPR; i++)
            ac_add_return(&ctx->args, AC_ARG_SGPR);
         for (i = 0; i < 2; i++)
            ac_add_return(&ctx->args, AC_ARG_VGPR);

         /* VS outputs passed via VGPRs to TCS. */
         if (shader->key.opt.same_patch_vertices) {
            unsigned num_outputs = util_last_bit64(shader->selector->outputs_written);
            for (i = 0; i < num_outputs * 4; i++)
               ac_add_return(&ctx->args, AC_ARG_VGPR);
         }
      } else {
         /* TCS inputs are passed via VGPRs from VS. */
         if (shader->key.opt.same_patch_vertices) {
            unsigned num_inputs = util_last_bit64(shader->previous_stage_sel->outputs_written);
            for (i = 0; i < num_inputs * 4; i++)
               ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL);
         }

         /* TCS return values are inputs to the TCS epilog.
          *
          * param_tcs_offchip_offset, param_tcs_factor_offset,
          * param_tcs_offchip_layout, and internal_bindings
          * should be passed to the epilog.
          */
         for (i = 0; i <= 8 + GFX9_SGPR_TCS_OUT_LAYOUT; i++)
            ac_add_return(&ctx->args, AC_ARG_SGPR);
         for (i = 0; i < 11; i++)
            ac_add_return(&ctx->args, AC_ARG_VGPR);
      }
      break;

   case SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY:
      /* Merged stages have 8 system SGPRs at the beginning. */
      /* SPI_SHADER_USER_DATA_ADDR_LO/HI_GS */
      declare_per_stage_desc_pointers(ctx, ctx->stage == MESA_SHADER_GEOMETRY);

      if (ctx->shader->key.as_ngg)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.gs_tg_info);
      else
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.gs2vs_offset);

      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.merged_wave_info);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.scratch_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR,
                 &ctx->small_prim_cull_info); /* SPI_SHADER_PGM_LO_GS << 8 */
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT,
                 NULL); /* unused (SPI_SHADER_PGM_LO/HI_GS >> 24) */

      declare_global_desc_pointers(ctx);
      if (ctx->stage != MESA_SHADER_VERTEX || !shader->selector->info.base.vs.blit_sgprs_amd) {
         declare_per_stage_desc_pointers(
            ctx, (ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL));
      }

      if (ctx->stage == MESA_SHADER_VERTEX) {
         if (shader->selector->info.base.vs.blit_sgprs_amd)
            declare_vs_blit_inputs(ctx, shader->selector->info.base.vs.blit_sgprs_amd);
         else
            declare_vs_specific_input_sgprs(ctx);
      } else {
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->vs_state_bits);

         if (ctx->stage == MESA_SHADER_TESS_EVAL) {
            ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
            ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tes_offchip_addr);
         }
      }

      if (ctx->stage == MESA_SHADER_VERTEX)
         declare_vb_descriptor_input_sgprs(ctx);

      /* VGPRs (first GS, then VS/TES) */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[0]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[1]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_prim_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_invocation_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[2]);

      if (ctx->stage == MESA_SHADER_VERTEX) {
         declare_vs_input_vgprs(ctx, &num_prolog_vgprs);
      } else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
         declare_tes_input_vgprs(ctx);
      }

      if ((ctx->shader->key.as_es || ngg_cull_shader) &&
          (ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL)) {
         unsigned num_user_sgprs, num_vgprs;

         if (ctx->stage == MESA_SHADER_VERTEX && ngg_cull_shader) {
            /* For the NGG cull shader, add 1 SGPR to hold
             * the vertex buffer pointer.
             */
            num_user_sgprs = GFX9_VSGS_NUM_USER_SGPR + 1;

            if (shader->selector->num_vbos_in_user_sgprs) {
               assert(num_user_sgprs <= SI_SGPR_VS_VB_DESCRIPTOR_FIRST);
               num_user_sgprs =
                  SI_SGPR_VS_VB_DESCRIPTOR_FIRST + shader->selector->num_vbos_in_user_sgprs * 4;
            }
         } else if (ctx->stage == MESA_SHADER_TESS_EVAL && ngg_cull_shader) {
            num_user_sgprs = GFX9_TESGS_NUM_USER_SGPR;
         } else {
            num_user_sgprs = SI_NUM_VS_STATE_RESOURCE_SGPRS;
         }

         /* The NGG cull shader has to return all 9 VGPRs.
          *
          * The normal merged ESGS shader only has to return the 5 VGPRs
          * for the GS stage.
          */
         num_vgprs = ngg_cull_shader ? 9 : 5;

         /* ES return values are inputs to GS. */
         for (i = 0; i < 8 + num_user_sgprs; i++)
            ac_add_return(&ctx->args, AC_ARG_SGPR);
         for (i = 0; i < num_vgprs; i++)
            ac_add_return(&ctx->args, AC_ARG_VGPR);
      }
      break;

   case MESA_SHADER_TESS_EVAL:
      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, true);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->vs_state_bits);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tcs_offchip_layout);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->tes_offchip_addr);

      if (shader->key.as_es) {
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.es2gs_offset);
      } else {
         declare_streamout_params(ctx, &shader->selector->so);
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tess_offchip_offset);
      }

      /* VGPRs */
      declare_tes_input_vgprs(ctx);
      break;

   case MESA_SHADER_GEOMETRY:
      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, true);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.gs2vs_offset);
      ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.gs_wave_id);

      /* VGPRs */
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[0]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[1]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_prim_id);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[2]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[3]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[4]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_vtx_offset[5]);
      ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.gs_invocation_id);
      break;

   case MESA_SHADER_FRAGMENT:
      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, true);
      si_add_arg_checked(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL, SI_PARAM_ALPHA_REF);
      si_add_arg_checked(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.prim_mask,
                         SI_PARAM_PRIM_MASK);

      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.persp_sample,
                         SI_PARAM_PERSP_SAMPLE);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.persp_center,
                         SI_PARAM_PERSP_CENTER);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.persp_centroid,
                         SI_PARAM_PERSP_CENTROID);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 3, AC_ARG_INT, NULL, SI_PARAM_PERSP_PULL_MODEL);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.linear_sample,
                         SI_PARAM_LINEAR_SAMPLE);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.linear_center,
                         SI_PARAM_LINEAR_CENTER);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 2, AC_ARG_INT, &ctx->args.linear_centroid,
                         SI_PARAM_LINEAR_CENTROID);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 3, AC_ARG_FLOAT, NULL, SI_PARAM_LINE_STIPPLE_TEX);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.frag_pos[0],
                         SI_PARAM_POS_X_FLOAT);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.frag_pos[1],
                         SI_PARAM_POS_Y_FLOAT);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.frag_pos[2],
                         SI_PARAM_POS_Z_FLOAT);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.frag_pos[3],
                         SI_PARAM_POS_W_FLOAT);
      shader->info.face_vgpr_index = ctx->args.num_vgprs_used;
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.front_face,
                         SI_PARAM_FRONT_FACE);
      shader->info.ancillary_vgpr_index = ctx->args.num_vgprs_used;
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.ancillary,
                         SI_PARAM_ANCILLARY);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &ctx->args.sample_coverage,
                         SI_PARAM_SAMPLE_COVERAGE);
      si_add_arg_checked(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->pos_fixed_pt,
                         SI_PARAM_POS_FIXED_PT);

      /* Color inputs from the prolog. */
      if (shader->selector->info.colors_read) {
         unsigned num_color_elements = util_bitcount(shader->selector->info.colors_read);

         for (i = 0; i < num_color_elements; i++)
            ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL);

         num_prolog_vgprs += num_color_elements;
      }

      /* Outputs for the epilog. */
      num_return_sgprs = SI_SGPR_ALPHA_REF + 1;
      num_returns = num_return_sgprs + util_bitcount(shader->selector->info.colors_written) * 4 +
                    shader->selector->info.writes_z + shader->selector->info.writes_stencil +
                    shader->selector->info.writes_samplemask + 1 /* SampleMaskIn */;

      num_returns = MAX2(num_returns, num_return_sgprs + PS_EPILOG_SAMPLEMASK_MIN_LOC + 1);

      for (i = 0; i < num_return_sgprs; i++)
         ac_add_return(&ctx->args, AC_ARG_SGPR);
      for (; i < num_returns; i++)
         ac_add_return(&ctx->args, AC_ARG_VGPR);
      break;

   case MESA_SHADER_COMPUTE:
      declare_global_desc_pointers(ctx);
      declare_per_stage_desc_pointers(ctx, true);
      if (shader->selector->info.uses_grid_size)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 3, AC_ARG_INT, &ctx->args.num_work_groups);
      if (shader->selector->info.uses_variable_block_size)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->block_size);

      unsigned cs_user_data_dwords =
         shader->selector->info.base.cs.user_data_components_amd;
      if (cs_user_data_dwords) {
         ac_add_arg(&ctx->args, AC_ARG_SGPR, cs_user_data_dwords, AC_ARG_INT, &ctx->cs_user_data);
      }

      /* Some descriptors can be in user SGPRs. */
      /* Shader buffers in user SGPRs. */
      for (unsigned i = 0; i < shader->selector->cs_num_shaderbufs_in_user_sgprs; i++) {
         while (ctx->args.num_sgprs_used % 4 != 0)
            ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);

         ac_add_arg(&ctx->args, AC_ARG_SGPR, 4, AC_ARG_INT, &ctx->cs_shaderbuf[i]);
      }
      /* Images in user SGPRs. */
      for (unsigned i = 0; i < shader->selector->cs_num_images_in_user_sgprs; i++) {
         unsigned num_sgprs = shader->selector->info.base.image_buffers & (1 << i) ? 4 : 8;

         while (ctx->args.num_sgprs_used % num_sgprs != 0)
            ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);

         ac_add_arg(&ctx->args, AC_ARG_SGPR, num_sgprs, AC_ARG_INT, &ctx->cs_image[i]);
      }

      /* Hardware SGPRs. */
      for (i = 0; i < 3; i++) {
         if (shader->selector->info.uses_block_id[i]) {
            ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.workgroup_ids[i]);
         }
      }
      if (shader->selector->info.uses_subgroup_info)
         ac_add_arg(&ctx->args, AC_ARG_SGPR, 1, AC_ARG_INT, &ctx->args.tg_size);

      /* Hardware VGPRs. */
      if (!ctx->screen->info.has_graphics && ctx->screen->info.family >= CHIP_ALDEBARAN)
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 1, AC_ARG_INT, &ctx->args.local_invocation_ids);
      else
         ac_add_arg(&ctx->args, AC_ARG_VGPR, 3, AC_ARG_INT, &ctx->args.local_invocation_ids);
      break;
   default:
      assert(0 && "unimplemented shader");
      return;
   }

   shader->info.num_input_sgprs = ctx->args.num_sgprs_used;
   shader->info.num_input_vgprs = ctx->args.num_vgprs_used;

   assert(shader->info.num_input_vgprs >= num_prolog_vgprs);
   shader->info.num_input_vgprs -= num_prolog_vgprs;
}

/* For the UMR disassembler. */
#define DEBUGGER_END_OF_CODE_MARKER 0xbf9f0000 /* invalid instruction */
#define DEBUGGER_NUM_MARKERS        5

static bool si_shader_binary_open(struct si_screen *screen, struct si_shader *shader,
                                  struct ac_rtld_binary *rtld)
{
   const struct si_shader_selector *sel = shader->selector;
   const char *part_elfs[5];
   size_t part_sizes[5];
   unsigned num_parts = 0;

#define add_part(shader_or_part)                                                                   \
   if (shader_or_part) {                                                                           \
      part_elfs[num_parts] = (shader_or_part)->binary.elf_buffer;                                  \
      part_sizes[num_parts] = (shader_or_part)->binary.elf_size;                                   \
      num_parts++;                                                                                 \
   }

   add_part(shader->prolog);
   add_part(shader->previous_stage);
   add_part(shader->prolog2);
   add_part(shader);
   add_part(shader->epilog);

#undef add_part

   struct ac_rtld_symbol lds_symbols[2];
   unsigned num_lds_symbols = 0;

   if (sel && screen->info.chip_class >= GFX9 && !shader->is_gs_copy_shader &&
       (sel->info.stage == MESA_SHADER_GEOMETRY || shader->key.as_ngg)) {
      struct ac_rtld_symbol *sym = &lds_symbols[num_lds_symbols++];
      sym->name = "esgs_ring";
      sym->size = shader->gs_info.esgs_ring_size * 4;
      sym->align = 64 * 1024;
   }

   if (shader->key.as_ngg && sel->info.stage == MESA_SHADER_GEOMETRY) {
      struct ac_rtld_symbol *sym = &lds_symbols[num_lds_symbols++];
      sym->name = "ngg_emit";
      sym->size = shader->ngg.ngg_emit_size * 4;
      sym->align = 4;
   }

   bool ok = ac_rtld_open(
      rtld, (struct ac_rtld_open_info){.info = &screen->info,
                                       .options =
                                          {
                                             .halt_at_entry = screen->options.halt_shaders,
                                          },
                                       .shader_type = sel->info.stage,
                                       .wave_size = si_get_shader_wave_size(shader),
                                       .num_parts = num_parts,
                                       .elf_ptrs = part_elfs,
                                       .elf_sizes = part_sizes,
                                       .num_shared_lds_symbols = num_lds_symbols,
                                       .shared_lds_symbols = lds_symbols});

   if (rtld->lds_size > 0) {
      unsigned alloc_granularity = screen->info.chip_class >= GFX7 ? 512 : 256;
      shader->config.lds_size = align(rtld->lds_size, alloc_granularity) / alloc_granularity;
   }

   return ok;
}

static unsigned si_get_shader_binary_size(struct si_screen *screen, struct si_shader *shader)
{
   struct ac_rtld_binary rtld;
   si_shader_binary_open(screen, shader, &rtld);
   uint64_t size = rtld.exec_size;
   ac_rtld_close(&rtld);
   return size;
}

static bool si_get_external_symbol(void *data, const char *name, uint64_t *value)
{
   uint64_t *scratch_va = data;

   if (!strcmp(scratch_rsrc_dword0_symbol, name)) {
      *value = (uint32_t)*scratch_va;
      return true;
   }
   if (!strcmp(scratch_rsrc_dword1_symbol, name)) {
      /* Enable scratch coalescing. */
      *value = S_008F04_BASE_ADDRESS_HI(*scratch_va >> 32) | S_008F04_SWIZZLE_ENABLE(1);
      return true;
   }

   return false;
}

bool si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader,
                             uint64_t scratch_va)
{
   struct ac_rtld_binary binary;
   if (!si_shader_binary_open(sscreen, shader, &binary))
      return false;

   si_resource_reference(&shader->bo, NULL);
   shader->bo = si_aligned_buffer_create(
      &sscreen->b,
      (sscreen->info.cpdma_prefetch_writes_memory ? 0 : SI_RESOURCE_FLAG_READ_ONLY) |
      SI_RESOURCE_FLAG_DRIVER_INTERNAL | SI_RESOURCE_FLAG_32BIT,
      PIPE_USAGE_IMMUTABLE, align(binary.rx_size, SI_CPDMA_ALIGNMENT), 256);
   if (!shader->bo)
      return false;

   /* Upload. */
   struct ac_rtld_upload_info u = {};
   u.binary = &binary;
   u.get_external_symbol = si_get_external_symbol;
   u.cb_data = &scratch_va;
   u.rx_va = shader->bo->gpu_address;
   u.rx_ptr = sscreen->ws->buffer_map(sscreen->ws,
      shader->bo->buf, NULL,
      PIPE_MAP_READ_WRITE | PIPE_MAP_UNSYNCHRONIZED | RADEON_MAP_TEMPORARY);
   if (!u.rx_ptr)
      return false;

   int size = ac_rtld_upload(&u);

   if (sscreen->debug_flags & DBG(SQTT)) {
      /* Remember the uploaded code */
      shader->binary.uploaded_code_size = size;
      shader->binary.uploaded_code = malloc(size);
      memcpy(shader->binary.uploaded_code, u.rx_ptr, size);
   }

   sscreen->ws->buffer_unmap(sscreen->ws, shader->bo->buf);
   ac_rtld_close(&binary);

   return size >= 0;
}

static void si_shader_dump_disassembly(struct si_screen *screen,
                                       const struct si_shader_binary *binary,
                                       gl_shader_stage stage, unsigned wave_size,
                                       struct pipe_debug_callback *debug, const char *name,
                                       FILE *file)
{
   struct ac_rtld_binary rtld_binary;

   if (!ac_rtld_open(&rtld_binary, (struct ac_rtld_open_info){
                                      .info = &screen->info,
                                      .shader_type = stage,
                                      .wave_size = wave_size,
                                      .num_parts = 1,
                                      .elf_ptrs = &binary->elf_buffer,
                                      .elf_sizes = &binary->elf_size}))
      return;

   const char *disasm;
   size_t nbytes;

   if (!ac_rtld_get_section_by_name(&rtld_binary, ".AMDGPU.disasm", &disasm, &nbytes))
      goto out;

   if (nbytes > INT_MAX)
      goto out;

   if (debug && debug->debug_message) {
      /* Very long debug messages are cut off, so send the
       * disassembly one line at a time. This causes more
       * overhead, but on the plus side it simplifies
       * parsing of resulting logs.
       */
      pipe_debug_message(debug, SHADER_INFO, "Shader Disassembly Begin");

      uint64_t line = 0;
      while (line < nbytes) {
         int count = nbytes - line;
         const char *nl = memchr(disasm + line, '\n', nbytes - line);
         if (nl)
            count = nl - (disasm + line);

         if (count) {
            pipe_debug_message(debug, SHADER_INFO, "%.*s", count, disasm + line);
         }

         line += count + 1;
      }

      pipe_debug_message(debug, SHADER_INFO, "Shader Disassembly End");
   }

   if (file) {
      fprintf(file, "Shader %s disassembly:\n", name);
      fprintf(file, "%*s", (int)nbytes, disasm);
   }

out:
   ac_rtld_close(&rtld_binary);
}

static void si_calculate_max_simd_waves(struct si_shader *shader)
{
   struct si_screen *sscreen = shader->selector->screen;
   struct ac_shader_config *conf = &shader->config;
   unsigned num_inputs = shader->selector->info.num_inputs;
   unsigned lds_increment = sscreen->info.chip_class >= GFX7 ? 512 : 256;
   unsigned lds_per_wave = 0;
   unsigned max_simd_waves;

   max_simd_waves = sscreen->info.max_wave64_per_simd;

   /* Compute LDS usage for PS. */
   switch (shader->selector->info.stage) {
   case MESA_SHADER_FRAGMENT:
      /* The minimum usage per wave is (num_inputs * 48). The maximum
       * usage is (num_inputs * 48 * 16).
       * We can get anything in between and it varies between waves.
       *
       * The 48 bytes per input for a single primitive is equal to
       * 4 bytes/component * 4 components/input * 3 points.
       *
       * Other stages don't know the size at compile time or don't
       * allocate LDS per wave, but instead they do it per thread group.
       */
      lds_per_wave = conf->lds_size * lds_increment + align(num_inputs * 48, lds_increment);
      break;
   case MESA_SHADER_COMPUTE: {
         unsigned max_workgroup_size = si_get_max_workgroup_size(shader);
         lds_per_wave = (conf->lds_size * lds_increment) /
                        DIV_ROUND_UP(max_workgroup_size, sscreen->compute_wave_size);
      }
      break;
   default:;
   }

   /* Compute the per-SIMD wave counts. */
   if (conf->num_sgprs) {
      max_simd_waves =
         MIN2(max_simd_waves, sscreen->info.num_physical_sgprs_per_simd / conf->num_sgprs);
   }

   if (conf->num_vgprs) {
      /* Always print wave limits as Wave64, so that we can compare
       * Wave32 and Wave64 with shader-db fairly. */
      unsigned max_vgprs = sscreen->info.num_physical_wave64_vgprs_per_simd;
      max_simd_waves = MIN2(max_simd_waves, max_vgprs / conf->num_vgprs);
   }

   unsigned max_lds_per_simd = sscreen->info.lds_size_per_workgroup / 4;
   if (lds_per_wave)
      max_simd_waves = MIN2(max_simd_waves, max_lds_per_simd / lds_per_wave);

   shader->info.max_simd_waves = max_simd_waves;
}

void si_shader_dump_stats_for_shader_db(struct si_screen *screen, struct si_shader *shader,
                                        struct pipe_debug_callback *debug)
{
   const struct ac_shader_config *conf = &shader->config;

   if (screen->options.debug_disassembly)
      si_shader_dump_disassembly(screen, &shader->binary, shader->selector->info.stage,
                                 si_get_shader_wave_size(shader), debug, "main", NULL);

   pipe_debug_message(debug, SHADER_INFO,
                      "Shader Stats: SGPRS: %d VGPRS: %d Code Size: %d "
                      "LDS: %d Scratch: %d Max Waves: %d Spilled SGPRs: %d "
                      "Spilled VGPRs: %d PrivMem VGPRs: %d",
                      conf->num_sgprs, conf->num_vgprs, si_get_shader_binary_size(screen, shader),
                      conf->lds_size, conf->scratch_bytes_per_wave, shader->info.max_simd_waves,
                      conf->spilled_sgprs, conf->spilled_vgprs, shader->info.private_mem_vgprs);
}

static void si_shader_dump_stats(struct si_screen *sscreen, struct si_shader *shader, FILE *file,
                                 bool check_debug_option)
{
   const struct ac_shader_config *conf = &shader->config;

   if (!check_debug_option || si_can_dump_shader(sscreen, shader->selector->info.stage)) {
      if (shader->selector->info.stage == MESA_SHADER_FRAGMENT) {
         fprintf(file,
                 "*** SHADER CONFIG ***\n"
                 "SPI_PS_INPUT_ADDR = 0x%04x\n"
                 "SPI_PS_INPUT_ENA  = 0x%04x\n",
                 conf->spi_ps_input_addr, conf->spi_ps_input_ena);
      }

      fprintf(file,
              "*** SHADER STATS ***\n"
              "SGPRS: %d\n"
              "VGPRS: %d\n"
              "Spilled SGPRs: %d\n"
              "Spilled VGPRs: %d\n"
              "Private memory VGPRs: %d\n"
              "Code Size: %d bytes\n"
              "LDS: %d blocks\n"
              "Scratch: %d bytes per wave\n"
              "Max Waves: %d\n"
              "********************\n\n\n",
              conf->num_sgprs, conf->num_vgprs, conf->spilled_sgprs, conf->spilled_vgprs,
              shader->info.private_mem_vgprs, si_get_shader_binary_size(sscreen, shader),
              conf->lds_size, conf->scratch_bytes_per_wave, shader->info.max_simd_waves);
   }
}

const char *si_get_shader_name(const struct si_shader *shader)
{
   switch (shader->selector->info.stage) {
   case MESA_SHADER_VERTEX:
      if (shader->key.as_es)
         return "Vertex Shader as ES";
      else if (shader->key.as_ls)
         return "Vertex Shader as LS";
      else if (shader->key.as_ngg)
         return "Vertex Shader as ESGS";
      else
         return "Vertex Shader as VS";
   case MESA_SHADER_TESS_CTRL:
      return "Tessellation Control Shader";
   case MESA_SHADER_TESS_EVAL:
      if (shader->key.as_es)
         return "Tessellation Evaluation Shader as ES";
      else if (shader->key.as_ngg)
         return "Tessellation Evaluation Shader as ESGS";
      else
         return "Tessellation Evaluation Shader as VS";
   case MESA_SHADER_GEOMETRY:
      if (shader->is_gs_copy_shader)
         return "GS Copy Shader as VS";
      else
         return "Geometry Shader";
   case MESA_SHADER_FRAGMENT:
      return "Pixel Shader";
   case MESA_SHADER_COMPUTE:
      return "Compute Shader";
   default:
      return "Unknown Shader";
   }
}

void si_shader_dump(struct si_screen *sscreen, struct si_shader *shader,
                    struct pipe_debug_callback *debug, FILE *file, bool check_debug_option)
{
   gl_shader_stage stage = shader->selector->info.stage;

   if (!check_debug_option || si_can_dump_shader(sscreen, stage))
      si_dump_shader_key(shader, file);

   if (!check_debug_option && shader->binary.llvm_ir_string) {
      if (shader->previous_stage && shader->previous_stage->binary.llvm_ir_string) {
         fprintf(file, "\n%s - previous stage - LLVM IR:\n\n", si_get_shader_name(shader));
         fprintf(file, "%s\n", shader->previous_stage->binary.llvm_ir_string);
      }

      fprintf(file, "\n%s - main shader part - LLVM IR:\n\n", si_get_shader_name(shader));
      fprintf(file, "%s\n", shader->binary.llvm_ir_string);
   }

   if (!check_debug_option ||
       (si_can_dump_shader(sscreen, stage) && !(sscreen->debug_flags & DBG(NO_ASM)))) {
      unsigned wave_size = si_get_shader_wave_size(shader);

      fprintf(file, "\n%s:\n", si_get_shader_name(shader));

      if (shader->prolog)
         si_shader_dump_disassembly(sscreen, &shader->prolog->binary, stage, wave_size, debug,
                                    "prolog", file);
      if (shader->previous_stage)
         si_shader_dump_disassembly(sscreen, &shader->previous_stage->binary, stage,
                                    wave_size, debug, "previous stage", file);
      if (shader->prolog2)
         si_shader_dump_disassembly(sscreen, &shader->prolog2->binary, stage, wave_size,
                                    debug, "prolog2", file);

      si_shader_dump_disassembly(sscreen, &shader->binary, stage, wave_size, debug, "main",
                                 file);

      if (shader->epilog)
         si_shader_dump_disassembly(sscreen, &shader->epilog->binary, stage, wave_size, debug,
                                    "epilog", file);
      fprintf(file, "\n");
   }

   si_shader_dump_stats(sscreen, shader, file, check_debug_option);
}

static void si_dump_shader_key_vs(const struct si_shader_key *key,
                                  const struct si_vs_prolog_bits *prolog, const char *prefix,
                                  FILE *f)
{
   fprintf(f, "  %s.instance_divisor_is_one = %u\n", prefix, prolog->instance_divisor_is_one);
   fprintf(f, "  %s.instance_divisor_is_fetched = %u\n", prefix,
           prolog->instance_divisor_is_fetched);
   fprintf(f, "  %s.ls_vgpr_fix = %u\n", prefix, prolog->ls_vgpr_fix);

   fprintf(f, "  mono.vs.fetch_opencode = %x\n", key->mono.vs_fetch_opencode);
   fprintf(f, "  mono.vs.fix_fetch = {");
   for (int i = 0; i < SI_MAX_ATTRIBS; i++) {
      union si_vs_fix_fetch fix = key->mono.vs_fix_fetch[i];
      if (i)
         fprintf(f, ", ");
      if (!fix.bits)
         fprintf(f, "0");
      else
         fprintf(f, "%u.%u.%u.%u", fix.u.reverse, fix.u.log_size, fix.u.num_channels_m1,
                 fix.u.format);
   }
   fprintf(f, "}\n");
}

static void si_dump_shader_key(const struct si_shader *shader, FILE *f)
{
   const struct si_shader_key *key = &shader->key;
   gl_shader_stage stage = shader->selector->info.stage;

   fprintf(f, "SHADER KEY\n");

   switch (stage) {
   case MESA_SHADER_VERTEX:
      si_dump_shader_key_vs(key, &key->part.vs.prolog, "part.vs.prolog", f);
      fprintf(f, "  as_es = %u\n", key->as_es);
      fprintf(f, "  as_ls = %u\n", key->as_ls);
      fprintf(f, "  as_ngg = %u\n", key->as_ngg);
      fprintf(f, "  mono.u.vs_export_prim_id = %u\n", key->mono.u.vs_export_prim_id);
      break;

   case MESA_SHADER_TESS_CTRL:
      if (shader->selector->screen->info.chip_class >= GFX9) {
         si_dump_shader_key_vs(key, &key->part.tcs.ls_prolog, "part.tcs.ls_prolog", f);
      }
      fprintf(f, "  part.tcs.epilog.prim_mode = %u\n", key->part.tcs.epilog.prim_mode);
      fprintf(f, "  mono.u.ff_tcs_inputs_to_copy = 0x%" PRIx64 "\n",
              key->mono.u.ff_tcs_inputs_to_copy);
      fprintf(f, "  opt.prefer_mono = %u\n", key->opt.prefer_mono);
      fprintf(f, "  opt.same_patch_vertices = %u\n", key->opt.same_patch_vertices);
      break;

   case MESA_SHADER_TESS_EVAL:
      fprintf(f, "  as_es = %u\n", key->as_es);
      fprintf(f, "  as_ngg = %u\n", key->as_ngg);
      fprintf(f, "  mono.u.vs_export_prim_id = %u\n", key->mono.u.vs_export_prim_id);
      break;

   case MESA_SHADER_GEOMETRY:
      if (shader->is_gs_copy_shader)
         break;

      if (shader->selector->screen->info.chip_class >= GFX9 &&
          key->part.gs.es->info.stage == MESA_SHADER_VERTEX) {
         si_dump_shader_key_vs(key, &key->part.gs.vs_prolog, "part.gs.vs_prolog", f);
      }
      fprintf(f, "  part.gs.prolog.tri_strip_adj_fix = %u\n",
              key->part.gs.prolog.tri_strip_adj_fix);
      fprintf(f, "  as_ngg = %u\n", key->as_ngg);
      break;

   case MESA_SHADER_COMPUTE:
      break;

   case MESA_SHADER_FRAGMENT:
      fprintf(f, "  part.ps.prolog.color_two_side = %u\n", key->part.ps.prolog.color_two_side);
      fprintf(f, "  part.ps.prolog.flatshade_colors = %u\n", key->part.ps.prolog.flatshade_colors);
      fprintf(f, "  part.ps.prolog.poly_stipple = %u\n", key->part.ps.prolog.poly_stipple);
      fprintf(f, "  part.ps.prolog.force_persp_sample_interp = %u\n",
              key->part.ps.prolog.force_persp_sample_interp);
      fprintf(f, "  part.ps.prolog.force_linear_sample_interp = %u\n",
              key->part.ps.prolog.force_linear_sample_interp);
      fprintf(f, "  part.ps.prolog.force_persp_center_interp = %u\n",
              key->part.ps.prolog.force_persp_center_interp);
      fprintf(f, "  part.ps.prolog.force_linear_center_interp = %u\n",
              key->part.ps.prolog.force_linear_center_interp);
      fprintf(f, "  part.ps.prolog.bc_optimize_for_persp = %u\n",
              key->part.ps.prolog.bc_optimize_for_persp);
      fprintf(f, "  part.ps.prolog.bc_optimize_for_linear = %u\n",
              key->part.ps.prolog.bc_optimize_for_linear);
      fprintf(f, "  part.ps.prolog.samplemask_log_ps_iter = %u\n",
              key->part.ps.prolog.samplemask_log_ps_iter);
      fprintf(f, "  part.ps.epilog.spi_shader_col_format = 0x%x\n",
              key->part.ps.epilog.spi_shader_col_format);
      fprintf(f, "  part.ps.epilog.color_is_int8 = 0x%X\n", key->part.ps.epilog.color_is_int8);
      fprintf(f, "  part.ps.epilog.color_is_int10 = 0x%X\n", key->part.ps.epilog.color_is_int10);
      fprintf(f, "  part.ps.epilog.last_cbuf = %u\n", key->part.ps.epilog.last_cbuf);
      fprintf(f, "  part.ps.epilog.alpha_func = %u\n", key->part.ps.epilog.alpha_func);
      fprintf(f, "  part.ps.epilog.alpha_to_one = %u\n", key->part.ps.epilog.alpha_to_one);
      fprintf(f, "  part.ps.epilog.poly_line_smoothing = %u\n",
              key->part.ps.epilog.poly_line_smoothing);
      fprintf(f, "  part.ps.epilog.clamp_color = %u\n", key->part.ps.epilog.clamp_color);
      fprintf(f, "  mono.u.ps.interpolate_at_sample_force_center = %u\n",
              key->mono.u.ps.interpolate_at_sample_force_center);
      fprintf(f, "  mono.u.ps.fbfetch_msaa = %u\n", key->mono.u.ps.fbfetch_msaa);
      fprintf(f, "  mono.u.ps.fbfetch_is_1D = %u\n", key->mono.u.ps.fbfetch_is_1D);
      fprintf(f, "  mono.u.ps.fbfetch_layered = %u\n", key->mono.u.ps.fbfetch_layered);
      break;

   default:
      assert(0);
   }

   if ((stage == MESA_SHADER_GEOMETRY || stage == MESA_SHADER_TESS_EVAL ||
        stage == MESA_SHADER_VERTEX) &&
       !key->as_es && !key->as_ls) {
      fprintf(f, "  opt.kill_outputs = 0x%" PRIx64 "\n", key->opt.kill_outputs);
      fprintf(f, "  opt.kill_pointsize = 0x%x\n", key->opt.kill_pointsize);
      fprintf(f, "  opt.kill_clip_distances = 0x%x\n", key->opt.kill_clip_distances);
      if (stage != MESA_SHADER_GEOMETRY)
         fprintf(f, "  opt.ngg_culling = 0x%x\n", key->opt.ngg_culling);
   }

   fprintf(f, "  opt.prefer_mono = %u\n", key->opt.prefer_mono);
   fprintf(f, "  opt.inline_uniforms = %u (0x%x, 0x%x, 0x%x, 0x%x)\n",
           key->opt.inline_uniforms,
           key->opt.inlined_uniform_values[0],
           key->opt.inlined_uniform_values[1],
           key->opt.inlined_uniform_values[2],
           key->opt.inlined_uniform_values[3]);
}

bool si_vs_needs_prolog(const struct si_shader_selector *sel,
                        const struct si_vs_prolog_bits *prolog_key,
                        const struct si_shader_key *key, bool ngg_cull_shader)
{
   /* VGPR initialization fixup for Vega10 and Raven is always done in the
    * VS prolog. */
   return sel->vs_needs_prolog || prolog_key->ls_vgpr_fix ||
          /* The 2nd VS prolog loads input VGPRs from LDS */
          (key->opt.ngg_culling && !ngg_cull_shader) ||
          /* The 1st VS prolog generates input VGPRs for fast launch. */
          (ngg_cull_shader && key->opt.ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_ALL);
}

/**
 * Compute the VS prolog key, which contains all the information needed to
 * build the VS prolog function, and set shader->info bits where needed.
 *
 * \param info             Shader info of the vertex shader.
 * \param num_input_sgprs  Number of input SGPRs for the vertex shader.
 * \param has_old_  Whether the preceding shader part is the NGG cull shader.
 * \param prolog_key       Key of the VS prolog
 * \param shader_out       The vertex shader, or the next shader if merging LS+HS or ES+GS.
 * \param key              Output shader part key.
 */
void si_get_vs_prolog_key(const struct si_shader_info *info, unsigned num_input_sgprs,
                          bool ngg_cull_shader, const struct si_vs_prolog_bits *prolog_key,
                          struct si_shader *shader_out, union si_shader_part_key *key)
{
   memset(key, 0, sizeof(*key));
   key->vs_prolog.states = *prolog_key;
   key->vs_prolog.num_input_sgprs = num_input_sgprs;
   key->vs_prolog.num_inputs = info->num_inputs;
   key->vs_prolog.as_ls = shader_out->key.as_ls;
   key->vs_prolog.as_es = shader_out->key.as_es;
   key->vs_prolog.as_ngg = shader_out->key.as_ngg;

   if (ngg_cull_shader) {
      key->vs_prolog.gs_fast_launch_tri_list =
         !!(shader_out->key.opt.ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_TRI_LIST);
      key->vs_prolog.gs_fast_launch_tri_strip =
         !!(shader_out->key.opt.ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP);
      key->vs_prolog.gs_fast_launch_index_size_packed =
         SI_GET_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(shader_out->key.opt.ngg_culling);
   } else if (shader_out->key.opt.ngg_culling) {
      key->vs_prolog.load_vgprs_after_culling = 1;
   }

   if (shader_out->selector->info.stage == MESA_SHADER_TESS_CTRL) {
      key->vs_prolog.as_ls = 1;
      key->vs_prolog.num_merged_next_stage_vgprs = 2;
   } else if (shader_out->selector->info.stage == MESA_SHADER_GEOMETRY) {
      key->vs_prolog.as_es = 1;
      key->vs_prolog.num_merged_next_stage_vgprs = 5;
   } else if (shader_out->key.as_ngg) {
      key->vs_prolog.num_merged_next_stage_vgprs = 5;
   }

   /* Only one of these combinations can be set. as_ngg can be set with as_es. */
   assert(key->vs_prolog.as_ls + key->vs_prolog.as_ngg +
          (key->vs_prolog.as_es && !key->vs_prolog.as_ngg) <= 1);

   /* Enable loading the InstanceID VGPR. */
   uint16_t input_mask = u_bit_consecutive(0, info->num_inputs);

   if ((key->vs_prolog.states.instance_divisor_is_one |
        key->vs_prolog.states.instance_divisor_is_fetched) &
       input_mask)
      shader_out->info.uses_instanceid = true;
}

struct nir_shader *si_get_nir_shader(struct si_shader_selector *sel,
                                     const struct si_shader_key *key,
                                     bool *free_nir)
{
   nir_shader *nir;
   *free_nir = false;

   if (sel->nir) {
      nir = sel->nir;
   } else if (sel->nir_binary) {
      struct pipe_screen *screen = &sel->screen->b;
      const void *options = screen->get_compiler_options(screen, PIPE_SHADER_IR_NIR,
                                                         pipe_shader_type_from_mesa(sel->info.stage));

      struct blob_reader blob_reader;
      blob_reader_init(&blob_reader, sel->nir_binary, sel->nir_size);
      *free_nir = true;
      nir = nir_deserialize(NULL, options, &blob_reader);
   } else {
      return NULL;
   }

   if (key && key->opt.inline_uniforms) {
      assert(*free_nir);

      /* Most places use shader information from the default variant, not
       * the optimized variant. These are the things that the driver looks at
       * in optimized variants and the list of things that we need to do.
       *
       * The driver takes into account these things if they suddenly disappear
       * from the shader code:
       * - Register usage and code size decrease (obvious)
       * - Eliminated PS system values are disabled by LLVM
       *   (FragCoord, FrontFace, barycentrics)
       * - VS/TES/GS outputs feeding PS are eliminated if outputs are undef.
       *   (thanks to an LLVM pass in Mesa - TODO: move it to NIR)
       *   The storage for eliminated outputs is also not allocated.
       * - VS/TCS/TES/GS/PS input loads are eliminated (VS relies on DCE in LLVM)
       * - TCS output stores are eliminated
       *
       * TODO: These are things the driver ignores in the final shader code
       * and relies on the default shader info.
       * - Other system values are not eliminated
       * - PS.NUM_INTERP = bitcount64(inputs_read), renumber inputs
       *   to remove holes
       * - uses_discard - if it changed to false
       * - writes_memory - if it changed to false
       * - VS->TCS, VS->GS, TES->GS output stores for the former stage are not
       *   eliminated
       * - Eliminated VS/TCS/TES outputs are still allocated. (except when feeding PS)
       *   GS outputs are eliminated except for the temporary LDS.
       *   Clip distances, gl_PointSize, and PS outputs are eliminated based
       *   on current states, so we don't care about the shader code.
       *
       * TODO: Merged shaders don't inline uniforms for the first stage.
       * VS-GS: only GS inlines uniforms; VS-TCS: only TCS; TES-GS: only GS.
       * (key == NULL for the first stage here)
       *
       * TODO: Compute shaders don't support inlinable uniforms, because they
       * don't have shader variants.
       *
       * TODO: The driver uses a linear search to find a shader variant. This
       * can be really slow if we get too many variants due to uniform inlining.
       */
      NIR_PASS_V(nir, nir_inline_uniforms,
                 nir->info.num_inlinable_uniforms,
                 key->opt.inlined_uniform_values,
                 nir->info.inlinable_uniform_dw_offsets);

      si_nir_opts(sel->screen, nir, true);
      si_nir_late_opts(nir);

      /* This must be done again. */
      NIR_PASS_V(nir, nir_io_add_const_offset_to_base, nir_var_shader_in |
                                                       nir_var_shader_out);
   }

   return nir;
}

bool si_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                       struct si_shader *shader, struct pipe_debug_callback *debug)
{
   struct si_shader_selector *sel = shader->selector;
   bool free_nir;
   struct nir_shader *nir = si_get_nir_shader(sel, &shader->key, &free_nir);

   /* Dump NIR before doing NIR->LLVM conversion in case the
    * conversion fails. */
   if (si_can_dump_shader(sscreen, sel->info.stage) &&
       !(sscreen->debug_flags & DBG(NO_NIR))) {
      nir_print_shader(nir, stderr);
      si_dump_streamout(&sel->so);
   }

   /* Initialize vs_output_ps_input_cntl to default. */
   for (unsigned i = 0; i < ARRAY_SIZE(shader->info.vs_output_ps_input_cntl); i++)
      shader->info.vs_output_ps_input_cntl[i] = SI_PS_INPUT_CNTL_UNUSED;
   shader->info.vs_output_ps_input_cntl[VARYING_SLOT_COL0] = SI_PS_INPUT_CNTL_UNUSED_COLOR0;

   shader->info.uses_instanceid = sel->info.uses_instanceid;

   /* TODO: ACO could compile non-monolithic shaders here (starting
    * with PS and NGG VS), but monolithic shaders should be compiled
    * by LLVM due to more complicated compilation.
    */
   if (!si_llvm_compile_shader(sscreen, compiler, shader, debug, nir, free_nir))
      return false;

   /* Compute vs_output_ps_input_cntl. */
   if ((sel->info.stage == MESA_SHADER_VERTEX ||
        sel->info.stage == MESA_SHADER_TESS_EVAL ||
        sel->info.stage == MESA_SHADER_GEOMETRY) &&
       !shader->key.as_ls && !shader->key.as_es) {
      ubyte *vs_output_param_offset = shader->info.vs_output_param_offset;

      if (sel->info.stage == MESA_SHADER_GEOMETRY && !shader->key.as_ngg)
         vs_output_param_offset = sel->gs_copy_shader->info.vs_output_param_offset;

      /* VS and TES should also set primitive ID output if it's used. */
      unsigned num_outputs_with_prim_id = sel->info.num_outputs +
                                          shader->key.mono.u.vs_export_prim_id;

      for (unsigned i = 0; i < num_outputs_with_prim_id; i++) {
         unsigned semantic = sel->info.output_semantic[i];
         unsigned offset = vs_output_param_offset[i];
         unsigned ps_input_cntl;

         if (offset <= AC_EXP_PARAM_OFFSET_31) {
            /* The input is loaded from parameter memory. */
            ps_input_cntl = S_028644_OFFSET(offset);
         } else {
            /* The input is a DEFAULT_VAL constant. */
            assert(offset >= AC_EXP_PARAM_DEFAULT_VAL_0000 &&
                   offset <= AC_EXP_PARAM_DEFAULT_VAL_1111);
            offset -= AC_EXP_PARAM_DEFAULT_VAL_0000;

            /* OFFSET=0x20 means that DEFAULT_VAL is used. */
            ps_input_cntl = S_028644_OFFSET(0x20) |
                            S_028644_DEFAULT_VAL(offset);
         }

         shader->info.vs_output_ps_input_cntl[semantic] = ps_input_cntl;
      }
   }

   /* Validate SGPR and VGPR usage for compute to detect compiler bugs. */
   if (sel->info.stage == MESA_SHADER_COMPUTE) {
      unsigned wave_size = sscreen->compute_wave_size;
      unsigned max_vgprs =
         sscreen->info.num_physical_wave64_vgprs_per_simd * (wave_size == 32 ? 2 : 1);
      unsigned max_sgprs = sscreen->info.num_physical_sgprs_per_simd;
      unsigned max_sgprs_per_wave = 128;
      unsigned simds_per_tg = 4; /* assuming WGP mode on gfx10 */
      unsigned threads_per_tg = si_get_max_workgroup_size(shader);
      unsigned waves_per_tg = DIV_ROUND_UP(threads_per_tg, wave_size);
      unsigned waves_per_simd = DIV_ROUND_UP(waves_per_tg, simds_per_tg);

      max_vgprs = max_vgprs / waves_per_simd;
      max_sgprs = MIN2(max_sgprs / waves_per_simd, max_sgprs_per_wave);

      if (shader->config.num_sgprs > max_sgprs || shader->config.num_vgprs > max_vgprs) {
         fprintf(stderr,
                 "LLVM failed to compile a shader correctly: "
                 "SGPR:VGPR usage is %u:%u, but the hw limit is %u:%u\n",
                 shader->config.num_sgprs, shader->config.num_vgprs, max_sgprs, max_vgprs);

         /* Just terminate the process, because dependent
          * shaders can hang due to bad input data, but use
          * the env var to allow shader-db to work.
          */
         if (!debug_get_bool_option("SI_PASS_BAD_SHADERS", false))
            abort();
      }
   }

   /* Add the scratch offset to input SGPRs. */
   if (shader->config.scratch_bytes_per_wave && !si_is_merged_shader(shader))
      shader->info.num_input_sgprs += 1; /* scratch byte offset */

   /* Calculate the number of fragment input VGPRs. */
   if (sel->info.stage == MESA_SHADER_FRAGMENT) {
      shader->info.num_input_vgprs = ac_get_fs_input_vgpr_cnt(
         &shader->config, &shader->info.face_vgpr_index, &shader->info.ancillary_vgpr_index);
   }

   si_calculate_max_simd_waves(shader);
   si_shader_dump_stats_for_shader_db(sscreen, shader, debug);
   return true;
}

/**
 * Create, compile and return a shader part (prolog or epilog).
 *
 * \param sscreen	screen
 * \param list		list of shader parts of the same category
 * \param type		shader type
 * \param key		shader part key
 * \param prolog	whether the part being requested is a prolog
 * \param tm		LLVM target machine
 * \param debug		debug callback
 * \param build		the callback responsible for building the main function
 * \return		non-NULL on success
 */
static struct si_shader_part *
si_get_shader_part(struct si_screen *sscreen, struct si_shader_part **list,
                   gl_shader_stage stage, bool prolog, union si_shader_part_key *key,
                   struct ac_llvm_compiler *compiler, struct pipe_debug_callback *debug,
                   void (*build)(struct si_shader_context *, union si_shader_part_key *),
                   const char *name)
{
   struct si_shader_part *result;

   simple_mtx_lock(&sscreen->shader_parts_mutex);

   /* Find existing. */
   for (result = *list; result; result = result->next) {
      if (memcmp(&result->key, key, sizeof(*key)) == 0) {
         simple_mtx_unlock(&sscreen->shader_parts_mutex);
         return result;
      }
   }

   /* Compile a new one. */
   result = CALLOC_STRUCT(si_shader_part);
   result->key = *key;

   struct si_shader_selector sel = {};
   sel.screen = sscreen;

   struct si_shader shader = {};
   shader.selector = &sel;

   switch (stage) {
   case MESA_SHADER_VERTEX:
      shader.key.as_ls = key->vs_prolog.as_ls;
      shader.key.as_es = key->vs_prolog.as_es;
      shader.key.as_ngg = key->vs_prolog.as_ngg;
      shader.key.opt.ngg_culling =
         (key->vs_prolog.gs_fast_launch_tri_list ? SI_NGG_CULL_GS_FAST_LAUNCH_TRI_LIST : 0) |
         (key->vs_prolog.gs_fast_launch_tri_strip ? SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP : 0) |
         SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(key->vs_prolog.gs_fast_launch_index_size_packed);
      break;
   case MESA_SHADER_TESS_CTRL:
      assert(!prolog);
      shader.key.part.tcs.epilog = key->tcs_epilog.states;
      break;
   case MESA_SHADER_GEOMETRY:
      assert(prolog);
      shader.key.as_ngg = key->gs_prolog.as_ngg;
      break;
   case MESA_SHADER_FRAGMENT:
      if (prolog)
         shader.key.part.ps.prolog = key->ps_prolog.states;
      else
         shader.key.part.ps.epilog = key->ps_epilog.states;
      break;
   default:
      unreachable("bad shader part");
   }

   struct si_shader_context ctx;
   si_llvm_context_init(&ctx, sscreen, compiler,
                        si_get_wave_size(sscreen, stage,
                                         shader.key.as_ngg, shader.key.as_es,
                                         shader.key.opt.ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_ALL));
   ctx.shader = &shader;
   ctx.stage = stage;

   build(&ctx, key);

   /* Compile. */
   si_llvm_optimize_module(&ctx);

   if (!si_compile_llvm(sscreen, &result->binary, &result->config, compiler, &ctx.ac, debug,
                        ctx.stage, name, false)) {
      FREE(result);
      result = NULL;
      goto out;
   }

   result->next = *list;
   *list = result;

out:
   si_llvm_dispose(&ctx);
   simple_mtx_unlock(&sscreen->shader_parts_mutex);
   return result;
}

static bool si_get_vs_prolog(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                             struct si_shader *shader, struct pipe_debug_callback *debug,
                             struct si_shader *main_part, const struct si_vs_prolog_bits *key)
{
   struct si_shader_selector *vs = main_part->selector;

   if (!si_vs_needs_prolog(vs, key, &shader->key, false))
      return true;

   /* Get the prolog. */
   union si_shader_part_key prolog_key;
   si_get_vs_prolog_key(&vs->info, main_part->info.num_input_sgprs, false, key, shader,
                        &prolog_key);

   shader->prolog =
      si_get_shader_part(sscreen, &sscreen->vs_prologs, MESA_SHADER_VERTEX, true, &prolog_key,
                         compiler, debug, si_llvm_build_vs_prolog, "Vertex Shader Prolog");
   return shader->prolog != NULL;
}

/**
 * Select and compile (or reuse) vertex shader parts (prolog & epilog).
 */
static bool si_shader_select_vs_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                      struct si_shader *shader, struct pipe_debug_callback *debug)
{
   return si_get_vs_prolog(sscreen, compiler, shader, debug, shader, &shader->key.part.vs.prolog);
}

/**
 * Select and compile (or reuse) TCS parts (epilog).
 */
static bool si_shader_select_tcs_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                       struct si_shader *shader, struct pipe_debug_callback *debug)
{
   if (sscreen->info.chip_class >= GFX9) {
      struct si_shader *ls_main_part = shader->key.part.tcs.ls->main_shader_part_ls;

      if (!si_get_vs_prolog(sscreen, compiler, shader, debug, ls_main_part,
                            &shader->key.part.tcs.ls_prolog))
         return false;

      shader->previous_stage = ls_main_part;
   }

   /* Get the epilog. */
   union si_shader_part_key epilog_key;
   memset(&epilog_key, 0, sizeof(epilog_key));
   epilog_key.tcs_epilog.states = shader->key.part.tcs.epilog;

   shader->epilog = si_get_shader_part(sscreen, &sscreen->tcs_epilogs, MESA_SHADER_TESS_CTRL, false,
                                       &epilog_key, compiler, debug, si_llvm_build_tcs_epilog,
                                       "Tessellation Control Shader Epilog");
   return shader->epilog != NULL;
}

/**
 * Select and compile (or reuse) GS parts (prolog).
 */
static bool si_shader_select_gs_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                      struct si_shader *shader, struct pipe_debug_callback *debug)
{
   if (sscreen->info.chip_class >= GFX9) {
      struct si_shader *es_main_part;

      if (shader->key.as_ngg)
         es_main_part = shader->key.part.gs.es->main_shader_part_ngg_es;
      else
         es_main_part = shader->key.part.gs.es->main_shader_part_es;

      if (shader->key.part.gs.es->info.stage == MESA_SHADER_VERTEX &&
          !si_get_vs_prolog(sscreen, compiler, shader, debug, es_main_part,
                            &shader->key.part.gs.vs_prolog))
         return false;

      shader->previous_stage = es_main_part;
   }

   if (!shader->key.part.gs.prolog.tri_strip_adj_fix)
      return true;

   union si_shader_part_key prolog_key;
   memset(&prolog_key, 0, sizeof(prolog_key));
   prolog_key.gs_prolog.states = shader->key.part.gs.prolog;
   prolog_key.gs_prolog.as_ngg = shader->key.as_ngg;

   shader->prolog2 =
      si_get_shader_part(sscreen, &sscreen->gs_prologs, MESA_SHADER_GEOMETRY, true, &prolog_key,
                         compiler, debug, si_llvm_build_gs_prolog, "Geometry Shader Prolog");
   return shader->prolog2 != NULL;
}

/**
 * Compute the PS prolog key, which contains all the information needed to
 * build the PS prolog function, and set related bits in shader->config.
 */
void si_get_ps_prolog_key(struct si_shader *shader, union si_shader_part_key *key,
                          bool separate_prolog)
{
   struct si_shader_info *info = &shader->selector->info;

   memset(key, 0, sizeof(*key));
   key->ps_prolog.states = shader->key.part.ps.prolog;
   key->ps_prolog.colors_read = info->colors_read;
   key->ps_prolog.num_input_sgprs = shader->info.num_input_sgprs;
   key->ps_prolog.num_input_vgprs = shader->info.num_input_vgprs;
   key->ps_prolog.wqm =
      info->base.fs.needs_quad_helper_invocations &&
      (key->ps_prolog.colors_read || key->ps_prolog.states.force_persp_sample_interp ||
       key->ps_prolog.states.force_linear_sample_interp ||
       key->ps_prolog.states.force_persp_center_interp ||
       key->ps_prolog.states.force_linear_center_interp ||
       key->ps_prolog.states.bc_optimize_for_persp || key->ps_prolog.states.bc_optimize_for_linear);
   key->ps_prolog.ancillary_vgpr_index = shader->info.ancillary_vgpr_index;

   if (info->colors_read) {
      ubyte *color = shader->selector->color_attr_index;

      if (shader->key.part.ps.prolog.color_two_side) {
         /* BCOLORs are stored after the last input. */
         key->ps_prolog.num_interp_inputs = info->num_inputs;
         key->ps_prolog.face_vgpr_index = shader->info.face_vgpr_index;
         if (separate_prolog)
            shader->config.spi_ps_input_ena |= S_0286CC_FRONT_FACE_ENA(1);
      }

      for (unsigned i = 0; i < 2; i++) {
         unsigned interp = info->color_interpolate[i];
         unsigned location = info->color_interpolate_loc[i];

         if (!(info->colors_read & (0xf << i * 4)))
            continue;

         key->ps_prolog.color_attr_index[i] = color[i];

         if (shader->key.part.ps.prolog.flatshade_colors && interp == INTERP_MODE_COLOR)
            interp = INTERP_MODE_FLAT;

         switch (interp) {
         case INTERP_MODE_FLAT:
            key->ps_prolog.color_interp_vgpr_index[i] = -1;
            break;
         case INTERP_MODE_SMOOTH:
         case INTERP_MODE_COLOR:
            /* Force the interpolation location for colors here. */
            if (shader->key.part.ps.prolog.force_persp_sample_interp)
               location = TGSI_INTERPOLATE_LOC_SAMPLE;
            if (shader->key.part.ps.prolog.force_persp_center_interp)
               location = TGSI_INTERPOLATE_LOC_CENTER;

            switch (location) {
            case TGSI_INTERPOLATE_LOC_SAMPLE:
               key->ps_prolog.color_interp_vgpr_index[i] = 0;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
               }
               break;
            case TGSI_INTERPOLATE_LOC_CENTER:
               key->ps_prolog.color_interp_vgpr_index[i] = 2;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
               }
               break;
            case TGSI_INTERPOLATE_LOC_CENTROID:
               key->ps_prolog.color_interp_vgpr_index[i] = 4;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTROID_ENA(1);
               }
               break;
            default:
               assert(0);
            }
            break;
         case INTERP_MODE_NOPERSPECTIVE:
            /* Force the interpolation location for colors here. */
            if (shader->key.part.ps.prolog.force_linear_sample_interp)
               location = TGSI_INTERPOLATE_LOC_SAMPLE;
            if (shader->key.part.ps.prolog.force_linear_center_interp)
               location = TGSI_INTERPOLATE_LOC_CENTER;

            /* The VGPR assignment for non-monolithic shaders
             * works because InitialPSInputAddr is set on the
             * main shader and PERSP_PULL_MODEL is never used.
             */
            switch (location) {
            case TGSI_INTERPOLATE_LOC_SAMPLE:
               key->ps_prolog.color_interp_vgpr_index[i] = separate_prolog ? 6 : 9;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_SAMPLE_ENA(1);
               }
               break;
            case TGSI_INTERPOLATE_LOC_CENTER:
               key->ps_prolog.color_interp_vgpr_index[i] = separate_prolog ? 8 : 11;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
               }
               break;
            case TGSI_INTERPOLATE_LOC_CENTROID:
               key->ps_prolog.color_interp_vgpr_index[i] = separate_prolog ? 10 : 13;
               if (separate_prolog) {
                  shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTROID_ENA(1);
               }
               break;
            default:
               assert(0);
            }
            break;
         default:
            assert(0);
         }
      }
   }
}

/**
 * Check whether a PS prolog is required based on the key.
 */
bool si_need_ps_prolog(const union si_shader_part_key *key)
{
   return key->ps_prolog.colors_read || key->ps_prolog.states.force_persp_sample_interp ||
          key->ps_prolog.states.force_linear_sample_interp ||
          key->ps_prolog.states.force_persp_center_interp ||
          key->ps_prolog.states.force_linear_center_interp ||
          key->ps_prolog.states.bc_optimize_for_persp ||
          key->ps_prolog.states.bc_optimize_for_linear || key->ps_prolog.states.poly_stipple ||
          key->ps_prolog.states.samplemask_log_ps_iter;
}

/**
 * Compute the PS epilog key, which contains all the information needed to
 * build the PS epilog function.
 */
void si_get_ps_epilog_key(struct si_shader *shader, union si_shader_part_key *key)
{
   struct si_shader_info *info = &shader->selector->info;
   memset(key, 0, sizeof(*key));
   key->ps_epilog.colors_written = info->colors_written;
   key->ps_epilog.color_types = info->output_color_types;
   key->ps_epilog.writes_z = info->writes_z;
   key->ps_epilog.writes_stencil = info->writes_stencil;
   key->ps_epilog.writes_samplemask = info->writes_samplemask;
   key->ps_epilog.states = shader->key.part.ps.epilog;
}

/**
 * Select and compile (or reuse) pixel shader parts (prolog & epilog).
 */
static bool si_shader_select_ps_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                      struct si_shader *shader, struct pipe_debug_callback *debug)
{
   union si_shader_part_key prolog_key;
   union si_shader_part_key epilog_key;

   /* Get the prolog. */
   si_get_ps_prolog_key(shader, &prolog_key, true);

   /* The prolog is a no-op if these aren't set. */
   if (si_need_ps_prolog(&prolog_key)) {
      shader->prolog =
         si_get_shader_part(sscreen, &sscreen->ps_prologs, MESA_SHADER_FRAGMENT, true, &prolog_key,
                            compiler, debug, si_llvm_build_ps_prolog, "Fragment Shader Prolog");
      if (!shader->prolog)
         return false;
   }

   /* Get the epilog. */
   si_get_ps_epilog_key(shader, &epilog_key);

   shader->epilog =
      si_get_shader_part(sscreen, &sscreen->ps_epilogs, MESA_SHADER_FRAGMENT, false, &epilog_key,
                         compiler, debug, si_llvm_build_ps_epilog, "Fragment Shader Epilog");
   if (!shader->epilog)
      return false;

   /* Enable POS_FIXED_PT if polygon stippling is enabled. */
   if (shader->key.part.ps.prolog.poly_stipple) {
      shader->config.spi_ps_input_ena |= S_0286CC_POS_FIXED_PT_ENA(1);
      assert(G_0286CC_POS_FIXED_PT_ENA(shader->config.spi_ps_input_addr));
   }

   /* Set up the enable bits for per-sample shading if needed. */
   if (shader->key.part.ps.prolog.force_persp_sample_interp &&
       (G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTER_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
   }
   if (shader->key.part.ps.prolog.force_linear_sample_interp &&
       (G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTER_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_SAMPLE_ENA(1);
   }
   if (shader->key.part.ps.prolog.force_persp_center_interp &&
       (G_0286CC_PERSP_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_SAMPLE_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
   }
   if (shader->key.part.ps.prolog.force_linear_center_interp &&
       (G_0286CC_LINEAR_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_SAMPLE_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
   }

   /* POW_W_FLOAT requires that one of the perspective weights is enabled. */
   if (G_0286CC_POS_W_FLOAT_ENA(shader->config.spi_ps_input_ena) &&
       !(shader->config.spi_ps_input_ena & 0xf)) {
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
      assert(G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_addr));
   }

   /* At least one pair of interpolation weights must be enabled. */
   if (!(shader->config.spi_ps_input_ena & 0x7f)) {
      shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
      assert(G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_addr));
   }

   /* Samplemask fixup requires the sample ID. */
   if (shader->key.part.ps.prolog.samplemask_log_ps_iter) {
      shader->config.spi_ps_input_ena |= S_0286CC_ANCILLARY_ENA(1);
      assert(G_0286CC_ANCILLARY_ENA(shader->config.spi_ps_input_addr));
   }

   /* The sample mask input is always enabled, because the API shader always
    * passes it through to the epilog. Disable it here if it's unused.
    */
   if (!shader->key.part.ps.epilog.poly_line_smoothing && !shader->selector->info.reads_samplemask)
      shader->config.spi_ps_input_ena &= C_0286CC_SAMPLE_COVERAGE_ENA;

   return true;
}

void si_multiwave_lds_size_workaround(struct si_screen *sscreen, unsigned *lds_size)
{
   /* If tessellation is all offchip and on-chip GS isn't used, this
    * workaround is not needed.
    */
   return;

   /* SPI barrier management bug:
    *   Make sure we have at least 4k of LDS in use to avoid the bug.
    *   It applies to workgroup sizes of more than one wavefront.
    */
   if (sscreen->info.family == CHIP_BONAIRE || sscreen->info.family == CHIP_KABINI)
      *lds_size = MAX2(*lds_size, 8);
}

void si_fix_resource_usage(struct si_screen *sscreen, struct si_shader *shader)
{
   unsigned min_sgprs = shader->info.num_input_sgprs + 2; /* VCC */

   shader->config.num_sgprs = MAX2(shader->config.num_sgprs, min_sgprs);

   if (shader->selector->info.stage == MESA_SHADER_COMPUTE &&
       si_get_max_workgroup_size(shader) > sscreen->compute_wave_size) {
      si_multiwave_lds_size_workaround(sscreen, &shader->config.lds_size);
   }
}

bool si_create_shader_variant(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                              struct si_shader *shader, struct pipe_debug_callback *debug)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader *mainp = *si_get_main_shader_part(sel, &shader->key);

   /* LS, ES, VS are compiled on demand if the main part hasn't been
    * compiled for that stage.
    *
    * GS are compiled on demand if the main part hasn't been compiled
    * for the chosen NGG-ness.
    *
    * Vertex shaders are compiled on demand when a vertex fetch
    * workaround must be applied.
    */
   if (shader->is_monolithic) {
      /* Monolithic shader (compiled as a whole, has many variants,
       * may take a long time to compile).
       */
      if (!si_compile_shader(sscreen, compiler, shader, debug))
         return false;
   } else {
      /* The shader consists of several parts:
       *
       * - the middle part is the user shader, it has 1 variant only
       *   and it was compiled during the creation of the shader
       *   selector
       * - the prolog part is inserted at the beginning
       * - the epilog part is inserted at the end
       *
       * The prolog and epilog have many (but simple) variants.
       *
       * Starting with gfx9, geometry and tessellation control
       * shaders also contain the prolog and user shader parts of
       * the previous shader stage.
       */

      if (!mainp)
         return false;

      /* Copy the compiled shader data over. */
      shader->is_binary_shared = true;
      shader->binary = mainp->binary;
      shader->config = mainp->config;
      shader->info.num_input_sgprs = mainp->info.num_input_sgprs;
      shader->info.num_input_vgprs = mainp->info.num_input_vgprs;
      shader->info.face_vgpr_index = mainp->info.face_vgpr_index;
      shader->info.ancillary_vgpr_index = mainp->info.ancillary_vgpr_index;
      memcpy(shader->info.vs_output_ps_input_cntl, mainp->info.vs_output_ps_input_cntl,
             sizeof(mainp->info.vs_output_ps_input_cntl));
      shader->info.uses_instanceid = mainp->info.uses_instanceid;
      shader->info.nr_pos_exports = mainp->info.nr_pos_exports;
      shader->info.nr_param_exports = mainp->info.nr_param_exports;

      /* Select prologs and/or epilogs. */
      switch (sel->info.stage) {
      case MESA_SHADER_VERTEX:
         if (!si_shader_select_vs_parts(sscreen, compiler, shader, debug))
            return false;
         break;
      case MESA_SHADER_TESS_CTRL:
         if (!si_shader_select_tcs_parts(sscreen, compiler, shader, debug))
            return false;
         break;
      case MESA_SHADER_TESS_EVAL:
         break;
      case MESA_SHADER_GEOMETRY:
         if (!si_shader_select_gs_parts(sscreen, compiler, shader, debug))
            return false;
         break;
      case MESA_SHADER_FRAGMENT:
         if (!si_shader_select_ps_parts(sscreen, compiler, shader, debug))
            return false;

         /* Make sure we have at least as many VGPRs as there
          * are allocated inputs.
          */
         shader->config.num_vgprs = MAX2(shader->config.num_vgprs, shader->info.num_input_vgprs);
         break;
      default:;
      }

      /* Update SGPR and VGPR counts. */
      if (shader->prolog) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->prolog->config.num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->prolog->config.num_vgprs);
      }
      if (shader->previous_stage) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->previous_stage->config.num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->previous_stage->config.num_vgprs);
         shader->config.spilled_sgprs =
            MAX2(shader->config.spilled_sgprs, shader->previous_stage->config.spilled_sgprs);
         shader->config.spilled_vgprs =
            MAX2(shader->config.spilled_vgprs, shader->previous_stage->config.spilled_vgprs);
         shader->info.private_mem_vgprs =
            MAX2(shader->info.private_mem_vgprs, shader->previous_stage->info.private_mem_vgprs);
         shader->config.scratch_bytes_per_wave =
            MAX2(shader->config.scratch_bytes_per_wave,
                 shader->previous_stage->config.scratch_bytes_per_wave);
         shader->info.uses_instanceid |= shader->previous_stage->info.uses_instanceid;
      }
      if (shader->prolog2) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->prolog2->config.num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->prolog2->config.num_vgprs);
      }
      if (shader->epilog) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->epilog->config.num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->epilog->config.num_vgprs);
      }
      si_calculate_max_simd_waves(shader);
   }

   if (shader->key.as_ngg) {
      assert(!shader->key.as_es && !shader->key.as_ls);
      if (!gfx10_ngg_calculate_subgroup_info(shader)) {
         fprintf(stderr, "Failed to compute subgroup info\n");
         return false;
      }
   } else if (sscreen->info.chip_class >= GFX9 && sel->info.stage == MESA_SHADER_GEOMETRY) {
      gfx9_get_gs_info(shader->previous_stage_sel, sel, &shader->gs_info);
   }

   shader->uses_vs_state_provoking_vertex =
      sscreen->use_ngg &&
      /* Used to convert triangle strips from GS to triangles. */
      ((sel->info.stage == MESA_SHADER_GEOMETRY &&
        util_rast_prim_is_triangles(sel->info.base.gs.output_primitive)) ||
       (sel->info.stage == MESA_SHADER_VERTEX &&
        /* Used to export PrimitiveID from the correct vertex. */
        (shader->key.mono.u.vs_export_prim_id ||
         /* Used to generate triangle strip vertex IDs for all threads. */
         shader->key.opt.ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP)));

   shader->uses_vs_state_outprim = sscreen->use_ngg &&
                                   /* Only used by streamout in vertex shaders. */
                                   sel->info.stage == MESA_SHADER_VERTEX &&
                                   sel->so.num_outputs;

   if (sel->info.stage == MESA_SHADER_VERTEX) {
      shader->uses_base_instance = sel->info.uses_base_instance ||
                                   shader->key.part.vs.prolog.instance_divisor_is_one ||
                                   shader->key.part.vs.prolog.instance_divisor_is_fetched;
   } else if (sel->info.stage == MESA_SHADER_TESS_CTRL) {
      shader->uses_base_instance = shader->previous_stage_sel &&
                                   (shader->previous_stage_sel->info.uses_base_instance ||
                                    shader->key.part.tcs.ls_prolog.instance_divisor_is_one ||
                                    shader->key.part.tcs.ls_prolog.instance_divisor_is_fetched);
   } else if (sel->info.stage == MESA_SHADER_GEOMETRY) {
      shader->uses_base_instance = shader->previous_stage_sel &&
                                   (shader->previous_stage_sel->info.uses_base_instance ||
                                    shader->key.part.gs.vs_prolog.instance_divisor_is_one ||
                                    shader->key.part.gs.vs_prolog.instance_divisor_is_fetched);
   }

   si_fix_resource_usage(sscreen, shader);
   si_shader_dump(sscreen, shader, debug, stderr, true);

   /* Upload. */
   if (!si_shader_binary_upload(sscreen, shader, 0)) {
      fprintf(stderr, "LLVM failed to upload shader\n");
      return false;
   }

   return true;
}

void si_shader_binary_clean(struct si_shader_binary *binary)
{
   free((void *)binary->elf_buffer);
   binary->elf_buffer = NULL;

   free(binary->llvm_ir_string);
   binary->llvm_ir_string = NULL;

   free(binary->uploaded_code);
   binary->uploaded_code = NULL;
   binary->uploaded_code_size = 0;
}

void si_shader_destroy(struct si_shader *shader)
{
   if (shader->scratch_bo)
      si_resource_reference(&shader->scratch_bo, NULL);

   si_resource_reference(&shader->bo, NULL);

   if (!shader->is_binary_shared)
      si_shader_binary_clean(&shader->binary);

   free(shader->shader_log);
}
