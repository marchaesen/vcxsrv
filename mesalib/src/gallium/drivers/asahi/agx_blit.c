/* 
 * Copyright (C) 2021 Alyssa Rosenzweig
 * Copyright (C) 2020-2021 Collabora, Ltd.
 * Copyright (C) 2014 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "agx_state.h"
#include "compiler/nir/nir_builder.h"
#include "asahi/compiler/agx_compile.h"
#include "gallium/auxiliary/util/u_blitter.h"

static void
agx_build_reload_shader(struct agx_device *dev)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
         &agx_nir_options, "agx_reload");
   b.shader->info.internal = true;

   nir_variable *out = nir_variable_create(b.shader, nir_var_shader_out,
         glsl_vector_type(GLSL_TYPE_FLOAT, 4), "output");
   out->data.location = FRAG_RESULT_DATA0;

   nir_ssa_def *fragcoord = nir_load_frag_coord(&b);
   nir_ssa_def *coord = nir_channels(&b, fragcoord, 0x3);

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 1);
   tex->dest_type = nir_type_float32;
   tex->sampler_dim = GLSL_SAMPLER_DIM_RECT;
   tex->op = nir_texop_tex;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(coord);
   tex->coord_components = 2;
   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
   nir_builder_instr_insert(&b, &tex->instr);
   nir_store_var(&b, out, &tex->dest.ssa, 0xFF);

   unsigned offset = 0;
   unsigned bo_size = 4096;

   struct agx_bo *bo = agx_bo_create(dev, bo_size, AGX_MEMORY_TYPE_SHADER);
   dev->reload.bo = bo;

   for (unsigned i = 0; i < AGX_NUM_FORMATS; ++i) {
      struct util_dynarray binary;
      util_dynarray_init(&binary, NULL);

      nir_shader *s = nir_shader_clone(NULL, b.shader);
      struct agx_shader_info info;

      struct agx_shader_key key = {
         .fs.tib_formats[0] = i
      };

      agx_compile_shader_nir(s, &key, &binary, &info);

      assert(offset + binary.size < bo_size);
      memcpy(((uint8_t *) bo->ptr.cpu) + offset, binary.data, binary.size);

      dev->reload.format[i] = bo->ptr.gpu + offset;
      offset += ALIGN_POT(binary.size, 128);

      util_dynarray_fini(&binary);
   }
}

static void
agx_blitter_save(struct agx_context *ctx, struct blitter_context *blitter,
                 bool render_cond)
{
   util_blitter_save_vertex_buffer_slot(blitter, ctx->vertex_buffers);
   util_blitter_save_vertex_elements(blitter, ctx->attributes);
   util_blitter_save_vertex_shader(blitter, ctx->stage[PIPE_SHADER_VERTEX].shader);
   util_blitter_save_rasterizer(blitter, ctx->rast);
   util_blitter_save_viewport(blitter, &ctx->viewport);
   util_blitter_save_scissor(blitter, &ctx->scissor);
   util_blitter_save_fragment_shader(blitter, ctx->stage[PIPE_SHADER_FRAGMENT].shader);
   util_blitter_save_blend(blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(blitter, &ctx->zs);
   util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
   util_blitter_save_so_targets(blitter, 0, NULL);
   util_blitter_save_sample_mask(blitter, ctx->sample_mask);

   util_blitter_save_framebuffer(blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(blitter,
         ctx->stage[PIPE_SHADER_FRAGMENT].sampler_count,
         (void **)(ctx->stage[PIPE_SHADER_FRAGMENT].samplers));
   util_blitter_save_fragment_sampler_views(blitter,
         ctx->stage[PIPE_SHADER_FRAGMENT].texture_count,
         (struct pipe_sampler_view **)ctx->stage[PIPE_SHADER_FRAGMENT].textures);
   util_blitter_save_fragment_constant_buffer_slot(blitter,
         ctx->stage[PIPE_SHADER_FRAGMENT].cb);

   if (!render_cond) {
      util_blitter_save_render_condition(blitter,
            (struct pipe_query *) ctx->cond_query,
            ctx->cond_cond, ctx->cond_mode);
   }
}

void
agx_blit(struct pipe_context *pipe,
              const struct pipe_blit_info *info)
{
   //if (info->render_condition_enable &&
   //    !agx_render_condition_check(pan_context(pipe)))
   //        return;

   struct agx_context *ctx = agx_context(pipe);

   if (!util_blitter_is_blit_supported(ctx->blitter, info))
      unreachable("Unsupported blit\n");

   agx_blitter_save(ctx, ctx->blitter, info->render_condition_enable);
   util_blitter_blit(ctx->blitter, info);
}

/* We need some fixed shaders for common rendering tasks. When colour buffer
 * reload is not in use, a shader is used to clear a particular colour. At the
 * end of rendering a tile, a shader is used to write it out. These shaders are
 * too trivial to go through the compiler at this stage. */
#define AGX_STOP \
	0x88, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, \
	0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00 \

#define AGX_BLEND \
	0x09, 0x00, 0x00, 0x04, 0xf0, 0xfc, 0x80, 0x03

/* Clears the tilebuffer, where u6-u7 are preloaded with the FP16 clear colour

   0: 7e018c098040         bitop_mov        r0, u6
   6: 7e058e098000         bitop_mov        r1, u7
   c: 09000004f0fc8003     TODO.blend
   */

static uint8_t shader_clear[] = {
   0x7e, 0x01, 0x8c, 0x09, 0x80, 0x40,
   0x7e, 0x05, 0x8e, 0x09, 0x80, 0x00,
   AGX_BLEND,
   AGX_STOP
};

static uint8_t shader_store[] = {
   0x7e, 0x00, 0x04, 0x09, 0x80, 0x00,
   0xb1, 0x80, 0x00, 0x80, 0x00, 0x4a, 0x00, 0x00, 0x0a, 0x00,
   AGX_STOP
};

void
agx_internal_shaders(struct agx_device *dev)
{
   unsigned clear_offset = 0;
   unsigned store_offset = 1024;

   struct agx_bo *bo = agx_bo_create(dev, 4096, AGX_MEMORY_TYPE_SHADER);
   memcpy(((uint8_t *) bo->ptr.cpu) + clear_offset, shader_clear, sizeof(shader_clear));
   memcpy(((uint8_t *) bo->ptr.cpu) + store_offset, shader_store, sizeof(shader_store));

   dev->internal.bo = bo;
   dev->internal.clear = bo->ptr.gpu + clear_offset;
   dev->internal.store = bo->ptr.gpu + store_offset;

   agx_build_reload_shader(dev);
}
