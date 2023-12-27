/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gallium/auxiliary/nir/pipe_nir.h"
#define AC_SURFACE_INCLUDE_NIR
#include "ac_surface.h"
#include "si_pipe.h"

#include "nir_format_convert.h"

static void *create_shader_state(struct si_context *sctx, nir_shader *nir)
{
   sctx->b.screen->finalize_nir(sctx->b.screen, (void*)nir);
   return pipe_shader_from_nir(&sctx->b, nir);
}

static nir_def *get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_def *block_ids = nir_channels(b, nir_load_workgroup_id(b), mask);
   nir_def *block_size = nir_channels(b, nir_load_workgroup_size(b), mask);
   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

static void unpack_2x16(nir_builder *b, nir_def *src, nir_def **x, nir_def **y)
{
   *x = nir_iand_imm(b, src, 0xffff);
   *y = nir_ushr_imm(b, src, 16);
}

static void unpack_2x16_signed(nir_builder *b, nir_def *src, nir_def **x, nir_def **y)
{
   *x = nir_i2i32(b, nir_u2u16(b, src));
   *y = nir_ishr_imm(b, src, 16);
}

static nir_def *
deref_ssa(nir_builder *b, nir_variable *var)
{
   return &nir_build_deref_var(b, var)->def;
}

/* Create a NIR compute shader implementing copy_image.
 *
 * This shader can handle 1D and 2D, linear and non-linear images.
 * It expects the source and destination (x,y,z) coords as user_data_amd,
 * packed into 3 SGPRs as 2x16bits per component.
 */
void *si_create_copy_image_cs(struct si_context *sctx, unsigned wg_dim,
                              bool src_is_1d_array, bool dst_is_1d_array)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "copy_image_cs");
   b.shader->info.num_images = 2;

   /* The workgroup size is either 8x8 for normal (non-linear) 2D images,
    * or 64x1 for 1D and linear-2D images.
    */
   b.shader->info.workgroup_size_variable = true;

   b.shader->info.cs.user_data_components_amd = 3;
   nir_def *ids = nir_pad_vector_imm_int(&b, get_global_ids(&b, wg_dim), 0, 3);

   nir_def *coord_src = NULL, *coord_dst = NULL;
   unpack_2x16(&b, nir_trim_vector(&b, nir_load_user_data_amd(&b), 3),
               &coord_src, &coord_dst);

   coord_src = nir_iadd(&b, coord_src, ids);
   coord_dst = nir_iadd(&b, coord_dst, ids);

   /* Coordinates must have 4 channels in NIR. */
   coord_src = nir_pad_vector(&b, coord_src, 4);
   coord_dst = nir_pad_vector(&b, coord_dst, 4);

   static unsigned swizzle_xz[] = {0, 2, 0, 0};

   if (src_is_1d_array)
      coord_src = nir_swizzle(&b, coord_src, swizzle_xz, 4);
   if (dst_is_1d_array)
      coord_dst = nir_swizzle(&b, coord_dst, swizzle_xz, 4);

   const struct glsl_type *src_img_type = glsl_image_type(src_is_1d_array ? GLSL_SAMPLER_DIM_1D
                                                                          : GLSL_SAMPLER_DIM_2D,
                                                          /*is_array*/ true, GLSL_TYPE_FLOAT);
   const struct glsl_type *dst_img_type = glsl_image_type(dst_is_1d_array ? GLSL_SAMPLER_DIM_1D
                                                                          : GLSL_SAMPLER_DIM_2D,
                                                          /*is_array*/ true, GLSL_TYPE_FLOAT);

   nir_variable *img_src = nir_variable_create(b.shader, nir_var_image, src_img_type, "img_src");
   img_src->data.binding = 0;

   nir_variable *img_dst = nir_variable_create(b.shader, nir_var_image, dst_img_type, "img_dst");
   img_dst->data.binding = 1;

   nir_def *undef32 = nir_undef(&b, 1, 32);
   nir_def *zero = nir_imm_int(&b, 0);

   nir_def *data = nir_image_deref_load(&b, /*num_components*/ 4, /*bit_size*/ 32,
      deref_ssa(&b, img_src), coord_src, undef32, zero);

   nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst, undef32, data, zero);

   return create_shader_state(sctx, b.shader);
}

void *si_create_dcc_retile_cs(struct si_context *sctx, struct radeon_surf *surf)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "dcc_retile");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 3;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_def *user_sgprs = nir_load_user_data_amd(&b);

   /* Relative offset from the displayable DCC to the non-displayable DCC in the same buffer. */
   nir_def *src_dcc_offset = nir_channel(&b, user_sgprs, 0);

   nir_def *src_dcc_pitch, *dst_dcc_pitch, *src_dcc_height, *dst_dcc_height;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &src_dcc_pitch, &src_dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 2), &dst_dcc_pitch, &dst_dcc_height);

   /* Get the 2D coordinates. */
   nir_def *coord = get_global_ids(&b, 2);
   nir_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord, nir_imm_ivec2(&b, surf->u.gfx9.color.dcc_block_width,
                                             surf->u.gfx9.color.dcc_block_height));

   nir_def *src_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.dcc_equation,
                                 src_dcc_pitch, src_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   src_offset = nir_iadd(&b, src_offset, src_dcc_offset);
   nir_def *value = nir_load_ssbo(&b, 1, 8, zero, src_offset, .align_mul=1);

   nir_def *dst_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.display_dcc_equation,
                                 dst_dcc_pitch, dst_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   nir_store_ssbo(&b, value, zero, dst_offset, .write_mask=0x1, .align_mul=1);

   return create_shader_state(sctx, b.shader);
}

void *gfx9_create_clear_dcc_msaa_cs(struct si_context *sctx, struct si_texture *tex)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_dcc_msaa");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 2;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_def *user_sgprs = nir_load_user_data_amd(&b);
   nir_def *dcc_pitch, *dcc_height, *clear_value, *pipe_xor;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 0), &dcc_pitch, &dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &clear_value, &pipe_xor);
   clear_value = nir_u2u16(&b, clear_value);

   /* Get the 2D coordinates. */
   nir_def *coord = get_global_ids(&b, 3);
   nir_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord,
                    nir_imm_ivec3(&b, tex->surface.u.gfx9.color.dcc_block_width,
                                      tex->surface.u.gfx9.color.dcc_block_height,
                                      tex->surface.u.gfx9.color.dcc_block_depth));

   nir_def *offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, tex->surface.bpe,
                                 &tex->surface.u.gfx9.color.dcc_equation,
                                 dcc_pitch, dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 tex->buffer.b.b.array_size > 1 ? nir_channel(&b, coord, 2) : zero, /* z */
                                 zero, pipe_xor); /* sample, pipe_xor */

   /* The trick here is that DCC elements for an even and the next odd sample are next to each other
    * in memory, so we only need to compute the address for sample 0 and the next DCC byte is always
    * sample 1. That's why the clear value has 2 bytes - we're clearing 2 samples at the same time.
    */
   nir_store_ssbo(&b, clear_value, zero, offset, .write_mask=0x1, .align_mul=2);

   return create_shader_state(sctx, b.shader);
}

/* Create a compute shader implementing clear_buffer or copy_buffer. */
void *si_create_clear_buffer_rmw_cs(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_buffer_rmw_cs");
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 2;
   b.shader->info.num_ssbos = 1;

   /* address = blockID * 64 + threadID; */
   nir_def *address = get_global_ids(&b, 1);

   /* address = address * 16; (byte offset, loading one vec4 per thread) */
   address = nir_ishl_imm(&b, address, 4);
   
   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *data = nir_load_ssbo(&b, 4, 32, zero, address, .align_mul = 4);

   /* Get user data SGPRs. */
   nir_def *user_sgprs = nir_load_user_data_amd(&b);

   /* data &= inverted_writemask; */
   data = nir_iand(&b, data, nir_channel(&b, user_sgprs, 1));
   /* data |= clear_value_masked; */
   data = nir_ior(&b, data, nir_channel(&b, user_sgprs, 0));

   nir_store_ssbo(&b, data, zero, address,
      .access = SI_COMPUTE_DST_CACHE_POLICY != L2_LRU ? ACCESS_NON_TEMPORAL : 0,
      .align_mul = 4);

   return create_shader_state(sctx, b.shader);
}

/* This is used when TCS is NULL in the VS->TCS->TES chain. In this case,
 * VS passes its outputs to TES directly, so the fixed-function shader only
 * has to write TESSOUTER and TESSINNER.
 */
void *si_create_passthrough_tcs(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR,
                                           PIPE_SHADER_TESS_CTRL);

   unsigned locations[PIPE_MAX_SHADER_OUTPUTS];

   struct si_shader_info *info = &sctx->shader.vs.cso->info;
   for (unsigned i = 0; i < info->num_outputs; i++) {
      locations[i] = info->output_semantic[i];
   }

   nir_shader *tcs =
         nir_create_passthrough_tcs_impl(options, locations, info->num_outputs,
                                         sctx->patch_vertices);

   return create_shader_state(sctx, tcs);
}

static nir_def *convert_linear_to_srgb(nir_builder *b, nir_def *input)
{
   /* There are small precision differences compared to CB, so the gfx blit will return slightly
    * different results.
    */

   nir_def *comp[4];
   for (unsigned i = 0; i < 3; i++)
      comp[i] = nir_format_linear_to_srgb(b, nir_channel(b, input, i));
   comp[3] = nir_channel(b, input, 3);

   return nir_vec(b, comp, 4);
}

static nir_def *average_samples(nir_builder *b, nir_def **samples, unsigned num_samples)
{
   /* This works like add-reduce by computing the sum of each pair independently, and then
    * computing the sum of each pair of sums, and so on, to get better instruction-level
    * parallelism.
    */
   if (num_samples == 16) {
      for (unsigned i = 0; i < 8; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 8) {
      for (unsigned i = 0; i < 4; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 4) {
      for (unsigned i = 0; i < 2; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 2)
      samples[0] = nir_fadd(b, samples[0], samples[1]);

   return nir_fmul_imm(b, samples[0], 1.0 / num_samples); /* average the sum */
}

static nir_def *image_resolve_msaa(struct si_screen *sscreen, nir_builder *b, nir_variable *img,
                                   unsigned num_samples, nir_def *coord)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *result = NULL;
   nir_variable *var = NULL;

   /* Gfx11 doesn't support samples_identical, so we can't use it. */
   if (sscreen->info.gfx_level < GFX11) {
      /* We need a local variable to get the result out of conditional branches in SSA. */
      var = nir_local_variable_create(b->impl, glsl_vec4_type(), NULL);

      /* If all samples are identical, load only sample 0. */
      nir_push_if(b, nir_image_deref_samples_identical(b, 1, deref_ssa(b, img), coord));
      result = nir_image_deref_load(b, 4, 32, deref_ssa(b, img), coord, zero, zero);
      nir_store_var(b, var, result, 0xf);

      nir_push_else(b, NULL);
   }

   nir_def *sample_index[16];
   for (unsigned i = 0; i < num_samples; i++)
      sample_index[i] = nir_imm_int(b, i);

   /* We need to hide the constant sample indices behind the optimization barrier, otherwise
    * LLVM doesn't put loads into the same clause.
    *
    * TODO: nir_group_loads could do this.
    */
   if (!sscreen->use_aco) {
      for (unsigned i = 0; i < num_samples; i++)
         sample_index[i] = nir_optimization_barrier_vgpr_amd(b, 32, sample_index[i]);
   }

   /* Load all samples. */
   nir_def *samples[16];
   for (unsigned i = 0; i < num_samples; i++) {
      samples[i] = nir_image_deref_load(b, 4, 32, deref_ssa(b, img),
                                        coord, sample_index[i], zero);
   }

   result = average_samples(b, samples, num_samples);

   if (sscreen->info.gfx_level < GFX11) {
      /* Exit the conditional branch and get the result out of the branch. */
      nir_store_var(b, var, result, 0xf);
      nir_pop_if(b, NULL);
      result = nir_load_var(b, var);
   }

   return result;
}

static nir_def *apply_blit_output_modifiers(nir_builder *b, nir_def *color,
                                                const union si_compute_blit_shader_key *options)
{
   if (options->sint_to_uint)
      color = nir_imax(b, color, nir_imm_int(b, 0));

   if (options->uint_to_sint)
      color = nir_umin(b, color, nir_imm_int(b, INT32_MAX));

   if (options->dst_is_srgb)
      color = convert_linear_to_srgb(b, color);

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = options->use_integer_one ? nir_imm_int(b, 1) : nir_imm_float(b, 1);

   /* Set channels not present in src to 0 or 1. This will eliminate code loading and resolving
    * those channels.
    */
   for (unsigned chan = options->last_src_channel + 1; chan <= options->last_dst_channel; chan++)
      color = nir_vector_insert_imm(b, color, chan == 3 ? one : zero, chan);

   /* Discard channels not present in dst. The hardware fills unstored channels with 0. */
   if (options->last_dst_channel < 3)
      color = nir_trim_vector(b, color, options->last_dst_channel + 1);

   /* Convert to FP16 with rtz to match the pixel shader. Not necessary, but it helps verify
    * the behavior of the whole shader by comparing it to the gfx blit.
    */
   if (options->fp16_rtz)
      color = nir_f2f16_rtz(b, color);

   return color;
}

/* The compute blit shader.
 *
 * Differences compared to u_blitter (the gfx blit):
 * - u_blitter doesn't preserve NaNs, but the compute blit does
 * - u_blitter has lower linear->SRGB precision because the CB block doesn't
 *   use FP32, but the compute blit does.
 *
 * Other than that, non-scaled blits are identical to u_blitter.
 *
 * Implementation details:
 * - Out-of-bounds dst coordinates are not clamped at all. The hw drops
 *   out-of-bounds stores for us.
 * - Out-of-bounds src coordinates are clamped by emulating CLAMP_TO_EDGE using
 *   the image_size NIR intrinsic.
 * - X/Y flipping just does this in the shader: -threadIDs - 1
 * - MSAA copies are implemented but disabled because MSAA image stores don't
 *   work.
 */
void *si_create_blit_cs(struct si_context *sctx, const union si_compute_blit_shader_key *options)
{
   const nir_shader_compiler_options *nir_options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, nir_options,
                                                  "blit_non_scaled_cs");
   b.shader->info.num_images = 2;
   if (options->src_is_msaa)
      BITSET_SET(b.shader->info.msaa_images, 0);
   if (options->dst_is_msaa)
      BITSET_SET(b.shader->info.msaa_images, 1);
   /* TODO: 1D blits are 8x slower because the workgroup size is 8x8 */
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 3;

   const struct glsl_type *img_type[2] = {
      glsl_image_type(options->src_is_1d ? GLSL_SAMPLER_DIM_1D :
                      options->src_is_msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,
                      /*is_array*/ true, GLSL_TYPE_FLOAT),
      glsl_image_type(options->dst_is_1d ? GLSL_SAMPLER_DIM_1D :
                      options->dst_is_msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,
                      /*is_array*/ true, GLSL_TYPE_FLOAT),
   };

   nir_variable *img_src = nir_variable_create(b.shader, nir_var_uniform, img_type[0], "img0");
   img_src->data.binding = 0;

   nir_variable *img_dst = nir_variable_create(b.shader, nir_var_uniform, img_type[1], "img1");
   img_dst->data.binding = 1;

   nir_def *zero = nir_imm_int(&b, 0);

   /* Instructions. */
   /* Let's work with 0-based src and dst coordinates (thread IDs) first. */
   nir_def *dst_xyz = nir_pad_vector_imm_int(&b, get_global_ids(&b, options->wg_dim), 0, 3);
   nir_def *src_xyz = dst_xyz;

   /* Flip src coordinates. */
   for (unsigned i = 0; i < 2; i++) {
      if (i ? options->flip_y : options->flip_x) {
         /* x goes from 0 to (dim - 1).
          * The flipped blit should load from -dim to -1.
          * Therefore do: x = -x - 1;
          */
         nir_def *comp = nir_channel(&b, src_xyz, i);
         comp = nir_iadd_imm(&b, nir_ineg(&b, comp), -1);
         src_xyz = nir_vector_insert_imm(&b, src_xyz, comp, i);
      }
   }

   /* Add box.xyz. */
   nir_def *coord_src = NULL, *coord_dst = NULL;
   unpack_2x16_signed(&b, nir_trim_vector(&b, nir_load_user_data_amd(&b), 3),
                      &coord_src, &coord_dst);
   coord_dst = nir_iadd(&b, coord_dst, dst_xyz);
   coord_src = nir_iadd(&b, coord_src, src_xyz);

   /* Clamp to edge for src, only X and Y because Z can't be out of bounds. */
   if (options->xy_clamp_to_edge) {
      unsigned src_clamp_channels = options->src_is_1d ? 0x1 : 0x3;
      nir_def *dim = nir_image_deref_size(&b, 4, 32, deref_ssa(&b, img_src), zero);
      dim = nir_channels(&b, dim, src_clamp_channels);

      nir_def *coord_src_clamped = nir_channels(&b, coord_src, src_clamp_channels);
      coord_src_clamped = nir_imax(&b, coord_src_clamped, nir_imm_int(&b, 0));
      coord_src_clamped = nir_imin(&b, coord_src_clamped, nir_iadd_imm(&b, dim, -1));

      for (unsigned i = 0; i < util_bitcount(src_clamp_channels); i++)
         coord_src = nir_vector_insert_imm(&b, coord_src, nir_channel(&b, coord_src_clamped, i), i);
   }

   /* Swizzle coordinates for 1D_ARRAY. */
   static unsigned swizzle_xz[] = {0, 2, 0, 0};

   if (options->src_is_1d)
      coord_src = nir_swizzle(&b, coord_src, swizzle_xz, 4);
   if (options->dst_is_1d)
      coord_dst = nir_swizzle(&b, coord_dst, swizzle_xz, 4);

   /* Coordinates must have 4 channels in NIR. */
   coord_src = nir_pad_vector(&b, coord_src, 4);
   coord_dst = nir_pad_vector(&b, coord_dst, 4);

   /* TODO: out-of-bounds image stores have no effect, but we could jump over them for better perf */

   /* Execute the image loads and stores. */
   unsigned num_samples = 1 << options->log2_samples;
   nir_def *color;

   if (options->src_is_msaa && !options->dst_is_msaa && !options->sample0_only) {
      /* MSAA resolving (downsampling). */
      assert(num_samples > 1);
      color = image_resolve_msaa(sctx->screen, &b, img_src, num_samples, coord_src);
      color = apply_blit_output_modifiers(&b, color, options);
      nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst, zero, color, zero);

   } else if (options->src_is_msaa && options->dst_is_msaa) {
      /* MSAA copy. */
      nir_def *color[16];
      assert(num_samples > 1);
      /* Group loads together and then stores. */
      for (unsigned i = 0; i < num_samples; i++) {
         color[i] = nir_image_deref_load(&b, 4, 32, deref_ssa(&b, img_src), coord_src,
                                         nir_imm_int(&b, i), zero);
      }
      for (unsigned i = 0; i < num_samples; i++)
         color[i] = apply_blit_output_modifiers(&b, color[i], options);
      for (unsigned i = 0; i < num_samples; i++) {
         nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst,
                               nir_imm_int(&b, i), color[i], zero);
      }
   } else if (!options->src_is_msaa && options->dst_is_msaa) {
      /* MSAA upsampling. */
      assert(num_samples > 1);
      color = nir_image_deref_load(&b, 4, 32, deref_ssa(&b, img_src), coord_src, zero, zero);
      color = apply_blit_output_modifiers(&b, color, options);
      for (unsigned i = 0; i < num_samples; i++) {
         nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst,
                               nir_imm_int(&b, i), color, zero);
      }
   } else {
      /* Non-MSAA copy or read sample 0 only. */
      /* src2 = sample_index (zero), src3 = lod (zero) */
      assert(num_samples == 1);
      color = nir_image_deref_load(&b, 4, 32, deref_ssa(&b, img_src), coord_src, zero, zero);
      color = apply_blit_output_modifiers(&b, color, options);
      nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst, zero, color, zero);
   }

   return create_shader_state(sctx, b.shader);
}

void *si_clear_render_target_shader(struct si_context *sctx, enum pipe_texture_target type)
{
   nir_def *address;
   enum glsl_sampler_dim sampler_type;

   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b =
   nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_render_target");
   b.shader->info.num_ubos = 1;
   b.shader->info.num_images = 1;
   b.shader->num_uniforms = 2;

   switch (type) {
      case PIPE_TEXTURE_1D_ARRAY:
         b.shader->info.workgroup_size[0] = 64;
         b.shader->info.workgroup_size[1] = 1;
         b.shader->info.workgroup_size[2] = 1;
         sampler_type = GLSL_SAMPLER_DIM_1D;
         address = get_global_ids(&b, 2);
         break;
      case PIPE_TEXTURE_2D_ARRAY:
         b.shader->info.workgroup_size[0] = 8;
         b.shader->info.workgroup_size[1] = 8;
         b.shader->info.workgroup_size[2] = 1;
         sampler_type = GLSL_SAMPLER_DIM_2D;
         address = get_global_ids(&b, 3);
         break;
      default:
         unreachable("unsupported texture target type");
   }

   const struct glsl_type *img_type = glsl_image_type(sampler_type, true, GLSL_TYPE_FLOAT);
   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "image");
   output_img->data.image.format = PIPE_FORMAT_R32G32B32A32_FLOAT;

   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *ubo = nir_load_ubo(&b, 4, 32, zero, zero, .range_base = 0, .range = 16);

   /* TODO: No GL CTS tests for 1D arrays, relying on OpenCL CTS for now.
    * As a sanity check, "OpenCL-CTS/test_conformance/images/clFillImage" tests should pass
    */
   if (type == PIPE_TEXTURE_1D_ARRAY) {
      unsigned swizzle[4] = {0, 2, 0, 0};
      ubo = nir_swizzle(&b, ubo, swizzle, 4);
   }

   address = nir_iadd(&b, address, ubo);
   nir_def *coord = nir_pad_vector(&b, address, 4);

   nir_def *data = nir_load_ubo(&b, 4, 32, zero, nir_imm_int(&b, 16), .range_base = 16, .range = 16);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, zero, data, zero,
                         .image_dim = sampler_type, .image_array = true);

   return create_shader_state(sctx, b.shader);
}

void *si_clear_12bytes_buffer_shader(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
   sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b =
   nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_12bytes_buffer");
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 3;

   nir_def *offset = nir_imul_imm(&b, get_global_ids(&b, 1), 12);
   nir_def *value = nir_trim_vector(&b, nir_load_user_data_amd(&b), 3);

   nir_store_ssbo(&b, value, nir_imm_int(&b, 0), offset,
      .access = SI_COMPUTE_DST_CACHE_POLICY != L2_LRU ? ACCESS_NON_TEMPORAL : 0);

   return create_shader_state(sctx, b.shader);
}

void *si_create_ubyte_to_ushort_compute_shader(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   unsigned store_qualifier = ACCESS_COHERENT | ACCESS_RESTRICT;

   /* Don't cache loads, because there is no reuse. */
   unsigned load_qualifier = store_qualifier | ACCESS_NON_TEMPORAL;

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "ubyte_to_ushort");

   unsigned default_wave_size = si_determine_wave_size(sctx->screen, NULL);

   b.shader->info.workgroup_size[0] = default_wave_size;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 2;

   nir_def *load_address = get_global_ids(&b, 1);
   nir_def *store_address = nir_imul_imm(&b, load_address, 2);

   nir_def *ubyte_value = nir_load_ssbo(&b, 1, 8, nir_imm_int(&b, 1),
                                        load_address, .access = load_qualifier);
   nir_store_ssbo(&b, nir_u2uN(&b, ubyte_value, 16), nir_imm_int(&b, 0),
                  store_address, .access = store_qualifier);

   return create_shader_state(sctx, b.shader);
}

/* Create a compute shader implementing clear_buffer or copy_buffer. */
void *si_create_dma_compute_shader(struct si_context *sctx, unsigned num_dwords_per_thread,
                                   bool dst_stream_cache_policy, bool is_copy)
{
   assert(util_is_power_of_two_nonzero(num_dwords_per_thread));

   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   unsigned store_qualifier = ACCESS_COHERENT | ACCESS_RESTRICT;
   if (dst_stream_cache_policy)
      store_qualifier |= ACCESS_NON_TEMPORAL;

   /* Don't cache loads, because there is no reuse. */
   unsigned load_qualifier = store_qualifier | ACCESS_NON_TEMPORAL;

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "create_dma_compute");

   unsigned default_wave_size = si_determine_wave_size(sctx->screen, NULL);

   b.shader->info.workgroup_size[0] = default_wave_size;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.num_ssbos = 1;

   unsigned num_mem_ops = MAX2(1, num_dwords_per_thread / 4);
   unsigned *inst_dwords = alloca(num_mem_ops * sizeof(unsigned));

   for (unsigned i = 0; i < num_mem_ops; i++) {
      if (i * 4 < num_dwords_per_thread)
         inst_dwords[i] = MIN2(4, num_dwords_per_thread - i * 4);
   }

   /* If there are multiple stores,
    * the first store writes into 0 * wavesize + tid,
    * the 2nd store writes into 1 * wavesize + tid,
    * the 3rd store writes into 2 * wavesize + tid, etc.
    */
   nir_def *store_address = get_global_ids(&b, 1);

   /* Convert from a "store size unit" into bytes. */
   store_address = nir_imul_imm(&b, store_address, 4 * inst_dwords[0]);

   nir_def *load_address = store_address, *value, *values[num_mem_ops];
   value = nir_undef(&b, 1, 32);

   if (is_copy) {
      b.shader->info.num_ssbos++;
   } else {
      b.shader->info.cs.user_data_components_amd = inst_dwords[0];
      value = nir_trim_vector(&b, nir_load_user_data_amd(&b), inst_dwords[0]);
   }

   /* Distance between a load and a store for latency hiding. */
   unsigned load_store_distance = is_copy ? 8 : 0;

   for (unsigned i = 0; i < num_mem_ops + load_store_distance; i++) {
      int d = i - load_store_distance;

      if (is_copy && i < num_mem_ops) {
         if (i) {
            load_address = nir_iadd(&b, load_address,
                                    nir_imm_int(&b, 4 * inst_dwords[i] * default_wave_size));
         }
         values[i] = nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 1),load_address,
                                   .access = load_qualifier);
      }

      if (d >= 0) {
         if (d) {
            store_address = nir_iadd(&b, store_address,
                                     nir_imm_int(&b, 4 * inst_dwords[d] * default_wave_size));
         }
         nir_store_ssbo(&b, is_copy ? values[d] : value, nir_imm_int(&b, 0), store_address,
                        .access = store_qualifier);
      }
   }

   return create_shader_state(sctx, b.shader);
}

/* Load samples from the image, and copy them to the same image. This looks like
 * a no-op, but it's not. Loads use FMASK, while stores don't, so samples are
 * reordered to match expanded FMASK.
 *
 * After the shader finishes, FMASK should be cleared to identity.
 */
void *si_create_fmask_expand_cs(struct si_context *sctx, unsigned num_samples, bool is_array)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "create_fmask_expand_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   /* Return an empty compute shader */
   if (num_samples == 0)
      return create_shader_state(sctx, b.shader);

   b.shader->info.num_images = 1;

   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_MS, is_array, GLSL_TYPE_FLOAT);
   nir_variable *img = nir_variable_create(b.shader, nir_var_image, img_type, "image");
   img->data.access = ACCESS_RESTRICT;

   nir_def *z = nir_undef(&b, 1, 32);
   if (is_array) {
      z = nir_channel(&b, nir_load_workgroup_id(&b), 2);
   }

   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *address = get_global_ids(&b, 2);

   nir_def *sample[8], *addresses[8];
   assert(num_samples <= ARRAY_SIZE(sample));

   nir_def *img_def = &nir_build_deref_var(&b, img)->def;

   /* Load samples, resolving FMASK. */
   for (unsigned i = 0; i < num_samples; i++) {
      nir_def *it = nir_imm_int(&b, i);
      sample[i] = nir_vec4(&b, nir_channel(&b, address, 0), nir_channel(&b, address, 1), z, it);
      addresses[i] = nir_image_deref_load(&b, 4, 32, img_def, sample[i], it, zero,
                                          .access = ACCESS_RESTRICT,
                                          .image_dim = GLSL_SAMPLER_DIM_2D,
                                          .image_array = is_array);
   }

   /* Store samples, ignoring FMASK. */
   for (unsigned i = 0; i < num_samples; i++) {
      nir_image_deref_store(&b, img_def, sample[i], nir_imm_int(&b, i), addresses[i], zero,
                            .access = ACCESS_RESTRICT,
                            .image_dim = GLSL_SAMPLER_DIM_2D,
                            .image_array = is_array);
   }

   return create_shader_state(sctx, b.shader);
}

/* This is just a pass-through shader with 1-3 MOV instructions. */
void *si_get_blitter_vs(struct si_context *sctx, enum blitter_attrib_type type, unsigned num_layers)
{
   unsigned vs_blit_property;
   void **vs;

   switch (type) {
   case UTIL_BLITTER_ATTRIB_NONE:
      vs = num_layers > 1 ? &sctx->vs_blit_pos_layered : &sctx->vs_blit_pos;
      vs_blit_property = SI_VS_BLIT_SGPRS_POS;
      break;
   case UTIL_BLITTER_ATTRIB_COLOR:
      vs = num_layers > 1 ? &sctx->vs_blit_color_layered : &sctx->vs_blit_color;
      vs_blit_property = SI_VS_BLIT_SGPRS_POS_COLOR;
      break;
   case UTIL_BLITTER_ATTRIB_TEXCOORD_XY:
   case UTIL_BLITTER_ATTRIB_TEXCOORD_XYZW:
      assert(num_layers == 1);
      vs = &sctx->vs_blit_texcoord;
      vs_blit_property = SI_VS_BLIT_SGPRS_POS_TEXCOORD;
      break;
   default:
      assert(0);
      return NULL;
   }

   if (*vs)
      return *vs;

   /* Add 1 for the attribute ring address. */
   if (sctx->gfx_level >= GFX11 && type != UTIL_BLITTER_ATTRIB_NONE)
      vs_blit_property++;

   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_VERTEX);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_VERTEX, options, "get_blitter_vs");

   /* Tell the shader to load VS inputs from SGPRs: */
   b.shader->info.vs.blit_sgprs_amd = vs_blit_property;
   b.shader->info.vs.window_space_position = true;

   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_copy_var(&b,
                nir_create_variable_with_location(b.shader, nir_var_shader_out,
                                                  VARYING_SLOT_POS, vec4),
                nir_create_variable_with_location(b.shader, nir_var_shader_in,
                                                  VERT_ATTRIB_GENERIC0, vec4));

   if (type != UTIL_BLITTER_ATTRIB_NONE) {
      nir_copy_var(&b,
                   nir_create_variable_with_location(b.shader, nir_var_shader_out,
                                                     VARYING_SLOT_VAR0, vec4),
                   nir_create_variable_with_location(b.shader, nir_var_shader_in,
                                                     VERT_ATTRIB_GENERIC1, vec4));
   }

   if (num_layers > 1) {
      nir_variable *out_layer =
         nir_create_variable_with_location(b.shader, nir_var_shader_out,
                                           VARYING_SLOT_LAYER, glsl_int_type());
      out_layer->data.interpolation = INTERP_MODE_NONE;

      nir_copy_var(&b, out_layer,
                   nir_create_variable_with_location(b.shader, nir_var_system_value,
                                                     SYSTEM_VALUE_INSTANCE_ID, glsl_int_type()));
   }

   *vs = create_shader_state(sctx, b.shader);
   return *vs;
}
