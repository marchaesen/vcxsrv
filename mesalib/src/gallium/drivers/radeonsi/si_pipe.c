/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "si_pipe.h"

#include "driver_ddebug/dd_util.h"
#include "gallium/winsys/amdgpu/drm/amdgpu_public.h"
#include "gallium/winsys/radeon/drm/radeon_drm_public.h"
#include "radeon/radeon_uvd.h"
#include "si_compute.h"
#include "si_public.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "ac_shadowed_regs.h"
#include "util/disk_cache.h"
#include "util/u_cpu_detect.h"
#include "util/u_log.h"
#include "util/u_memory.h"
#include "util/u_suballoc.h"
#include "util/u_tests.h"
#include "util/u_upload_mgr.h"
#include "util/xmlconfig.h"
#include "vl/vl_decoder.h"

#include <xf86drm.h>

static struct pipe_context *si_create_context(struct pipe_screen *screen, unsigned flags);

static const struct debug_named_value radeonsi_debug_options[] = {
   /* Shader logging options: */
   {"vs", DBG(VS), "Print vertex shaders"},
   {"ps", DBG(PS), "Print pixel shaders"},
   {"gs", DBG(GS), "Print geometry shaders"},
   {"tcs", DBG(TCS), "Print tessellation control shaders"},
   {"tes", DBG(TES), "Print tessellation evaluation shaders"},
   {"cs", DBG(CS), "Print compute shaders"},
   {"noir", DBG(NO_IR), "Don't print the LLVM IR"},
   {"nonir", DBG(NO_NIR), "Don't print NIR when printing shaders"},
   {"noasm", DBG(NO_ASM), "Don't print disassembled shaders"},
   {"preoptir", DBG(PREOPT_IR), "Print the LLVM IR before initial optimizations"},

   /* Shader compiler options the shader cache should be aware of: */
   {"gisel", DBG(GISEL), "Enable LLVM global instruction selector."},
   {"w32ge", DBG(W32_GE), "Use Wave32 for vertex, tessellation, and geometry shaders."},
   {"w32ps", DBG(W32_PS), "Use Wave32 for pixel shaders."},
   {"w32cs", DBG(W32_CS), "Use Wave32 for computes shaders."},
   {"w64ge", DBG(W64_GE), "Use Wave64 for vertex, tessellation, and geometry shaders."},
   {"w64ps", DBG(W64_PS), "Use Wave64 for pixel shaders."},
   {"w64cs", DBG(W64_CS), "Use Wave64 for computes shaders."},

   /* Shader compiler options (with no effect on the shader cache): */
   {"checkir", DBG(CHECK_IR), "Enable additional sanity checks on shader IR"},
   {"mono", DBG(MONOLITHIC_SHADERS), "Use old-style monolithic shaders compiled on demand"},
   {"nooptvariant", DBG(NO_OPT_VARIANT), "Disable compiling optimized shader variants."},

   /* Information logging options: */
   {"info", DBG(INFO), "Print driver information"},
   {"tex", DBG(TEX), "Print texture info"},
   {"compute", DBG(COMPUTE), "Print compute info"},
   {"vm", DBG(VM), "Print virtual addresses when creating resources"},
   {"cache_stats", DBG(CACHE_STATS), "Print shader cache statistics."},
   {"ib", DBG(IB), "Print command buffers."},

   /* Driver options: */
   {"nowc", DBG(NO_WC), "Disable GTT write combining"},
   {"check_vm", DBG(CHECK_VM), "Check VM faults and dump debug info."},
   {"reserve_vmid", DBG(RESERVE_VMID), "Force VMID reservation per context."},
   {"shadowregs", DBG(SHADOW_REGS), "Enable CP register shadowing."},

   /* 3D engine options: */
   {"nogfx", DBG(NO_GFX), "Disable graphics. Only multimedia compute paths can be used."},
   {"nongg", DBG(NO_NGG), "Disable NGG and use the legacy pipeline."},
   {"nofastlaunch", DBG(NO_FAST_LAUNCH), "Disable NGG GS fast launch."},
   {"nggc", DBG(ALWAYS_NGG_CULLING_ALL), "Always use NGG culling even when it can hurt."},
   {"nggctess", DBG(ALWAYS_NGG_CULLING_TESS), "Always use NGG culling for tessellation."},
   {"nonggc", DBG(NO_NGG_CULLING), "Disable NGG culling."},
   {"switch_on_eop", DBG(SWITCH_ON_EOP), "Program WD/IA to switch on end-of-packet."},
   {"nooutoforder", DBG(NO_OUT_OF_ORDER), "Disable out-of-order rasterization"},
   {"nodpbb", DBG(NO_DPBB), "Disable DPBB."},
   {"dpbb", DBG(DPBB), "Enable DPBB."},
   {"nohyperz", DBG(NO_HYPERZ), "Disable Hyper-Z"},
   {"no2d", DBG(NO_2D_TILING), "Disable 2D tiling"},
   {"notiling", DBG(NO_TILING), "Disable tiling"},
   {"nodisplaytiling", DBG(NO_DISPLAY_TILING), "Disable display tiling"},
   {"nodisplaydcc", DBG(NO_DISPLAY_DCC), "Disable display DCC"},
   {"nodcc", DBG(NO_DCC), "Disable DCC."},
   {"nodccclear", DBG(NO_DCC_CLEAR), "Disable DCC fast clear."},
   {"nodccstore", DBG(NO_DCC_STORE), "Disable DCC stores"},
   {"dccstore", DBG(DCC_STORE), "Enable DCC stores"},
   {"nodccmsaa", DBG(NO_DCC_MSAA), "Disable DCC for MSAA"},
   {"nofmask", DBG(NO_FMASK), "Disable MSAA compression"},

   {"tmz", DBG(TMZ), "Force allocation of scanout/depth/stencil buffer as encrypted"},
   {"sqtt", DBG(SQTT), "Enable SQTT"},

   DEBUG_NAMED_VALUE_END /* must be last */
};

static const struct debug_named_value test_options[] = {
   /* Tests: */
   {"blit", DBG(TEST_BLIT), "Invoke blit tests and exit."},
   {"testvmfaultcp", DBG(TEST_VMFAULT_CP), "Invoke a CP VM fault test and exit."},
   {"testvmfaultshader", DBG(TEST_VMFAULT_SHADER), "Invoke a shader VM fault test and exit."},
   {"testdmaperf", DBG(TEST_DMA_PERF), "Test DMA performance"},
   {"testgds", DBG(TEST_GDS), "Test GDS."},
   {"testgdsmm", DBG(TEST_GDS_MM), "Test GDS memory management."},
   {"testgdsoamm", DBG(TEST_GDS_OA_MM), "Test GDS OA memory management."},

   DEBUG_NAMED_VALUE_END /* must be last */
};

void si_init_compiler(struct si_screen *sscreen, struct ac_llvm_compiler *compiler)
{
   /* Only create the less-optimizing version of the compiler on APUs
    * predating Ryzen (Raven). */
   bool create_low_opt_compiler =
      !sscreen->info.has_dedicated_vram && sscreen->info.chip_class <= GFX8;

   enum ac_target_machine_options tm_options =
      (sscreen->debug_flags & DBG(GISEL) ? AC_TM_ENABLE_GLOBAL_ISEL : 0) |
      (sscreen->debug_flags & DBG(CHECK_IR) ? AC_TM_CHECK_IR : 0) |
      (create_low_opt_compiler ? AC_TM_CREATE_LOW_OPT : 0);

   ac_init_llvm_once();
   ac_init_llvm_compiler(compiler, sscreen->info.family, tm_options);
   compiler->passes = ac_create_llvm_passes(compiler->tm);

   if (compiler->low_opt_tm)
      compiler->low_opt_passes = ac_create_llvm_passes(compiler->low_opt_tm);
}

static void si_destroy_compiler(struct ac_llvm_compiler *compiler)
{
   ac_destroy_llvm_compiler(compiler);
}


static void decref_implicit_resource(struct hash_entry *entry)
{
   pipe_resource_reference((struct pipe_resource**)&entry->data, NULL);
}

/*
 * pipe_context
 */
static void si_destroy_context(struct pipe_context *context)
{
   struct si_context *sctx = (struct si_context *)context;
   int i;

   /* Unreference the framebuffer normally to disable related logic
    * properly.
    */
   struct pipe_framebuffer_state fb = {};
   if (context->set_framebuffer_state)
      context->set_framebuffer_state(context, &fb);

   si_release_all_descriptors(sctx);

   if (sctx->chip_class >= GFX10 && sctx->has_graphics)
      gfx10_destroy_query(sctx);

   if (sctx->thread_trace)
      si_destroy_thread_trace(sctx);

   pipe_resource_reference(&sctx->esgs_ring, NULL);
   pipe_resource_reference(&sctx->gsvs_ring, NULL);
   pipe_resource_reference(&sctx->tess_rings, NULL);
   pipe_resource_reference(&sctx->tess_rings_tmz, NULL);
   pipe_resource_reference(&sctx->null_const_buf.buffer, NULL);
   pipe_resource_reference(&sctx->sample_pos_buffer, NULL);
   si_resource_reference(&sctx->border_color_buffer, NULL);
   free(sctx->border_color_table);
   si_resource_reference(&sctx->scratch_buffer, NULL);
   si_resource_reference(&sctx->compute_scratch_buffer, NULL);
   si_resource_reference(&sctx->wait_mem_scratch, NULL);
   si_resource_reference(&sctx->wait_mem_scratch_tmz, NULL);
   si_resource_reference(&sctx->small_prim_cull_info_buf, NULL);

   if (sctx->cs_preamble_state)
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
   if (sctx->cs_preamble_tess_rings)
      si_pm4_free_state(sctx, sctx->cs_preamble_tess_rings, ~0);
   if (sctx->cs_preamble_tess_rings_tmz)
      si_pm4_free_state(sctx, sctx->cs_preamble_tess_rings_tmz, ~0);
   if (sctx->cs_preamble_gs_rings)
      si_pm4_free_state(sctx, sctx->cs_preamble_gs_rings, ~0);
   for (i = 0; i < ARRAY_SIZE(sctx->vgt_shader_config); i++)
      si_pm4_free_state(sctx, sctx->vgt_shader_config[i], SI_STATE_IDX(vgt_shader_config));

   if (sctx->fixed_func_tcs_shader.cso)
      sctx->b.delete_tcs_state(&sctx->b, sctx->fixed_func_tcs_shader.cso);
   if (sctx->custom_dsa_flush)
      sctx->b.delete_depth_stencil_alpha_state(&sctx->b, sctx->custom_dsa_flush);
   if (sctx->custom_blend_resolve)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_resolve);
   if (sctx->custom_blend_fmask_decompress)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_fmask_decompress);
   if (sctx->custom_blend_eliminate_fastclear)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_eliminate_fastclear);
   if (sctx->custom_blend_dcc_decompress)
      sctx->b.delete_blend_state(&sctx->b, sctx->custom_blend_dcc_decompress);
   if (sctx->vs_blit_pos)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_pos);
   if (sctx->vs_blit_pos_layered)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_pos_layered);
   if (sctx->vs_blit_color)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_color);
   if (sctx->vs_blit_color_layered)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_color_layered);
   if (sctx->vs_blit_texcoord)
      sctx->b.delete_vs_state(&sctx->b, sctx->vs_blit_texcoord);
   if (sctx->cs_clear_buffer)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_buffer);
   if (sctx->cs_clear_buffer_rmw)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_buffer_rmw);
   if (sctx->cs_copy_buffer)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_copy_buffer);
   if (sctx->cs_copy_image)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_copy_image);
   if (sctx->cs_copy_image_1d_array)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_copy_image_1d_array);
   if (sctx->cs_clear_render_target)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_render_target);
   if (sctx->cs_clear_render_target_1d_array)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_render_target_1d_array);
   if (sctx->cs_clear_12bytes_buffer)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_12bytes_buffer);
   if (sctx->cs_dcc_decompress)
      sctx->b.delete_compute_state(&sctx->b, sctx->cs_dcc_decompress);
   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_dcc_retile); i++) {
      if (sctx->cs_dcc_retile[i])
         sctx->b.delete_compute_state(&sctx->b, sctx->cs_dcc_retile[i]);
   }
   if (sctx->no_velems_state)
      sctx->b.delete_vertex_elements_state(&sctx->b, sctx->no_velems_state);

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_fmask_expand); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(sctx->cs_fmask_expand[i]); j++) {
         if (sctx->cs_fmask_expand[i][j]) {
            sctx->b.delete_compute_state(&sctx->b, sctx->cs_fmask_expand[i][j]);
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->cs_clear_dcc_msaa); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i]); j++) {
         for (unsigned k = 0; k < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j]); k++) {
            for (unsigned l = 0; l < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j][k]); l++) {
               for (unsigned m = 0; m < ARRAY_SIZE(sctx->cs_clear_dcc_msaa[i][j][k][l]); m++) {
                  if (sctx->cs_clear_dcc_msaa[i][j][k][l][m])
                     sctx->b.delete_compute_state(&sctx->b, sctx->cs_clear_dcc_msaa[i][j][k][l][m]);
               }
            }
         }
      }
   }

   if (sctx->blitter)
      util_blitter_destroy(sctx->blitter);

   if (sctx->query_result_shader)
      sctx->b.delete_compute_state(&sctx->b, sctx->query_result_shader);
   if (sctx->sh_query_result_shader)
      sctx->b.delete_compute_state(&sctx->b, sctx->sh_query_result_shader);

   sctx->ws->cs_destroy(&sctx->gfx_cs);
   if (sctx->ctx)
      sctx->ws->ctx_destroy(sctx->ctx);

   if (sctx->dirty_implicit_resources)
      _mesa_hash_table_destroy(sctx->dirty_implicit_resources,
                               decref_implicit_resource);

   if (sctx->b.stream_uploader)
      u_upload_destroy(sctx->b.stream_uploader);
   if (sctx->b.const_uploader && sctx->b.const_uploader != sctx->b.stream_uploader)
      u_upload_destroy(sctx->b.const_uploader);
   if (sctx->cached_gtt_allocator)
      u_upload_destroy(sctx->cached_gtt_allocator);

   slab_destroy_child(&sctx->pool_transfers);
   slab_destroy_child(&sctx->pool_transfers_unsync);

   u_suballocator_destroy(&sctx->allocator_zeroed_memory);

   sctx->ws->fence_reference(&sctx->last_gfx_fence, NULL);
   si_resource_reference(&sctx->eop_bug_scratch, NULL);
   si_resource_reference(&sctx->eop_bug_scratch_tmz, NULL);
   si_resource_reference(&sctx->shadowed_regs, NULL);
   radeon_bo_reference(sctx->screen->ws, &sctx->gds, NULL);
   radeon_bo_reference(sctx->screen->ws, &sctx->gds_oa, NULL);

   si_destroy_compiler(&sctx->compiler);

   si_saved_cs_reference(&sctx->current_saved_cs, NULL);

   _mesa_hash_table_destroy(sctx->tex_handles, NULL);
   _mesa_hash_table_destroy(sctx->img_handles, NULL);

   util_dynarray_fini(&sctx->resident_tex_handles);
   util_dynarray_fini(&sctx->resident_img_handles);
   util_dynarray_fini(&sctx->resident_tex_needs_color_decompress);
   util_dynarray_fini(&sctx->resident_img_needs_color_decompress);
   util_dynarray_fini(&sctx->resident_tex_needs_depth_decompress);

   if (!(sctx->context_flags & SI_CONTEXT_FLAG_AUX))
      p_atomic_dec(&context->screen->num_contexts);

   FREE(sctx);
}

static enum pipe_reset_status si_get_reset_status(struct pipe_context *ctx)
{
   struct si_context *sctx = (struct si_context *)ctx;
   if (sctx->context_flags & SI_CONTEXT_FLAG_AUX)
      return PIPE_NO_RESET;

   bool needs_reset;
   enum pipe_reset_status status = sctx->ws->ctx_query_reset_status(sctx->ctx, false, &needs_reset);

   if (status != PIPE_NO_RESET && needs_reset && !(sctx->context_flags & SI_CONTEXT_FLAG_AUX)) {
      /* Call the gallium frontend to set a no-op API dispatch. */
      if (sctx->device_reset_callback.reset) {
         sctx->device_reset_callback.reset(sctx->device_reset_callback.data, status);
      }
   }
   return status;
}

static void si_set_device_reset_callback(struct pipe_context *ctx,
                                         const struct pipe_device_reset_callback *cb)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (cb)
      sctx->device_reset_callback = *cb;
   else
      memset(&sctx->device_reset_callback, 0, sizeof(sctx->device_reset_callback));
}

/* Apitrace profiling:
 *   1) qapitrace : Tools -> Profile: Measure CPU & GPU times
 *   2) In the middle panel, zoom in (mouse wheel) on some bad draw call
 *      and remember its number.
 *   3) In Mesa, enable queries and performance counters around that draw
 *      call and print the results.
 *   4) glretrace --benchmark --markers ..
 */
static void si_emit_string_marker(struct pipe_context *ctx, const char *string, int len)
{
   struct si_context *sctx = (struct si_context *)ctx;

   dd_parse_apitrace_marker(string, len, &sctx->apitrace_call_number);

   if (sctx->thread_trace_enabled)
      si_write_user_event(sctx, &sctx->gfx_cs, UserEventTrigger, string, len);

   if (sctx->log)
      u_log_printf(sctx->log, "\nString marker: %*s\n", len, string);
}

static void si_set_debug_callback(struct pipe_context *ctx, const struct pipe_debug_callback *cb)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_screen *screen = sctx->screen;

   util_queue_finish(&screen->shader_compiler_queue);
   util_queue_finish(&screen->shader_compiler_queue_low_priority);

   if (cb)
      sctx->debug = *cb;
   else
      memset(&sctx->debug, 0, sizeof(sctx->debug));
}

static void si_set_log_context(struct pipe_context *ctx, struct u_log_context *log)
{
   struct si_context *sctx = (struct si_context *)ctx;
   sctx->log = log;

   if (log)
      u_log_add_auto_logger(log, si_auto_log_cs, sctx);
}

static void si_set_context_param(struct pipe_context *ctx, enum pipe_context_param param,
                                 unsigned value)
{
   struct radeon_winsys *ws = ((struct si_context *)ctx)->ws;

   switch (param) {
   case PIPE_CONTEXT_PARAM_PIN_THREADS_TO_L3_CACHE:
      ws->pin_threads_to_L3_cache(ws, value);
      break;
   default:;
   }
}

static void si_set_frontend_noop(struct pipe_context *ctx, bool enable)
{
   struct si_context *sctx = (struct si_context *)ctx;

   ctx->flush(ctx, NULL, PIPE_FLUSH_ASYNC);
   sctx->is_noop = enable;
}

static struct pipe_context *si_create_context(struct pipe_screen *screen, unsigned flags)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   STATIC_ASSERT(DBG_COUNT <= 64);

   /* Don't create a context if it's not compute-only and hw is compute-only. */
   if (!sscreen->info.has_graphics && !(flags & PIPE_CONTEXT_COMPUTE_ONLY))
      return NULL;

   struct si_context *sctx = CALLOC_STRUCT(si_context);
   struct radeon_winsys *ws = sscreen->ws;
   int shader, i;
   bool stop_exec_on_failure = (flags & PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET) != 0;

   if (!sctx)
      return NULL;

   sctx->has_graphics = sscreen->info.chip_class == GFX6 || !(flags & PIPE_CONTEXT_COMPUTE_ONLY);

   if (flags & PIPE_CONTEXT_DEBUG)
      sscreen->record_llvm_ir = true; /* racy but not critical */

   sctx->b.screen = screen; /* this must be set first */
   sctx->b.priv = NULL;
   sctx->b.destroy = si_destroy_context;
   sctx->screen = sscreen; /* Easy accessing of screen/winsys. */
   sctx->is_debug = (flags & PIPE_CONTEXT_DEBUG) != 0;
   sctx->context_flags = flags;

   slab_create_child(&sctx->pool_transfers, &sscreen->pool_transfers);
   slab_create_child(&sctx->pool_transfers_unsync, &sscreen->pool_transfers);

   sctx->ws = sscreen->ws;
   sctx->family = sscreen->info.family;
   sctx->chip_class = sscreen->info.chip_class;

   if (sctx->chip_class == GFX7 || sctx->chip_class == GFX8 || sctx->chip_class == GFX9) {
      sctx->eop_bug_scratch = si_aligned_buffer_create(
         &sscreen->b, SI_RESOURCE_FLAG_DRIVER_INTERNAL,
         PIPE_USAGE_DEFAULT, 16 * sscreen->info.max_render_backends, 256);
      if (sctx->screen->info.has_tmz_support)
         sctx->eop_bug_scratch_tmz = si_aligned_buffer_create(
            &sscreen->b, PIPE_RESOURCE_FLAG_ENCRYPTED | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
            PIPE_USAGE_DEFAULT, 16 * sscreen->info.max_render_backends, 256);
      if (!sctx->eop_bug_scratch)
         goto fail;
   }

   /* Initialize the context handle and the command stream. */
   sctx->ctx = sctx->ws->ctx_create(sctx->ws);
   if (!sctx->ctx)
      goto fail;

   ws->cs_create(&sctx->gfx_cs, sctx->ctx, sctx->has_graphics ? RING_GFX : RING_COMPUTE,
                 (void *)si_flush_gfx_cs, sctx, stop_exec_on_failure);

   /* Initialize private allocators. */
   u_suballocator_init(&sctx->allocator_zeroed_memory, &sctx->b, 128 * 1024, 0,
                       PIPE_USAGE_DEFAULT,
                       SI_RESOURCE_FLAG_CLEAR | SI_RESOURCE_FLAG_32BIT, false);

   sctx->cached_gtt_allocator = u_upload_create(&sctx->b, 16 * 1024, 0, PIPE_USAGE_STAGING, 0);
   if (!sctx->cached_gtt_allocator)
      goto fail;

   /* Initialize public allocators. */
   /* Unify uploaders as follows:
    * - dGPUs with Smart Access Memory: there is only one uploader instance writing to VRAM.
    * - APUs: There is only one uploader instance writing to RAM. VRAM has the same perf on APUs.
    * - Other chips: The const uploader writes to VRAM and the stream uploader writes to RAM.
    */
   bool smart_access_memory = sscreen->info.smart_access_memory;
   bool is_apu = !sscreen->info.has_dedicated_vram;
   sctx->b.stream_uploader =
      u_upload_create(&sctx->b, 1024 * 1024, 0,
                      smart_access_memory && !is_apu ? PIPE_USAGE_DEFAULT : PIPE_USAGE_STREAM,
                      SI_RESOURCE_FLAG_32BIT); /* same flags as const_uploader */
   if (!sctx->b.stream_uploader)
      goto fail;

   if (smart_access_memory || is_apu) {
      sctx->b.const_uploader = sctx->b.stream_uploader;
   } else {
      sctx->b.const_uploader =
         u_upload_create(&sctx->b, 256 * 1024, 0, PIPE_USAGE_DEFAULT,
                         SI_RESOURCE_FLAG_32BIT);
      if (!sctx->b.const_uploader)
         goto fail;
   }

   /* Border colors. */
   if (sscreen->info.has_3d_cube_border_color_mipmap) {
      sctx->border_color_table = malloc(SI_MAX_BORDER_COLORS * sizeof(*sctx->border_color_table));
      if (!sctx->border_color_table)
         goto fail;

      sctx->border_color_buffer = si_resource(pipe_buffer_create(
         screen, 0, PIPE_USAGE_DEFAULT, SI_MAX_BORDER_COLORS * sizeof(*sctx->border_color_table)));
      if (!sctx->border_color_buffer)
         goto fail;

      sctx->border_color_map =
         ws->buffer_map(ws, sctx->border_color_buffer->buf, NULL, PIPE_MAP_WRITE);
      if (!sctx->border_color_map)
         goto fail;
   }

   sctx->ngg = sscreen->use_ngg;
   si_shader_change_notify(sctx);

   /* Initialize context functions used by graphics and compute. */
   if (sctx->chip_class >= GFX10)
      sctx->emit_cache_flush = gfx10_emit_cache_flush;
   else
      sctx->emit_cache_flush = si_emit_cache_flush;

   sctx->b.emit_string_marker = si_emit_string_marker;
   sctx->b.set_debug_callback = si_set_debug_callback;
   sctx->b.set_log_context = si_set_log_context;
   sctx->b.set_context_param = si_set_context_param;
   sctx->b.get_device_reset_status = si_get_reset_status;
   sctx->b.set_device_reset_callback = si_set_device_reset_callback;
   sctx->b.set_frontend_noop = si_set_frontend_noop;

   si_init_all_descriptors(sctx);
   si_init_buffer_functions(sctx);
   si_init_clear_functions(sctx);
   si_init_blit_functions(sctx);
   si_init_compute_functions(sctx);
   si_init_compute_blit_functions(sctx);
   si_init_debug_functions(sctx);
   si_init_fence_functions(sctx);
   si_init_query_functions(sctx);
   si_init_state_compute_functions(sctx);
   si_init_context_texture_functions(sctx);

   /* Initialize graphics-only context functions. */
   if (sctx->has_graphics) {
      if (sctx->chip_class >= GFX10)
         gfx10_init_query(sctx);
      si_init_msaa_functions(sctx);
      si_init_shader_functions(sctx);
      si_init_state_functions(sctx);
      si_init_streamout_functions(sctx);
      si_init_viewport_functions(sctx);
      si_init_spi_map_functions(sctx);

      sctx->blitter = util_blitter_create(&sctx->b);
      if (sctx->blitter == NULL)
         goto fail;
      sctx->blitter->skip_viewport_restore = true;

      /* Some states are expected to be always non-NULL. */
      sctx->noop_blend = util_blitter_get_noop_blend_state(sctx->blitter);
      sctx->queued.named.blend = sctx->noop_blend;

      sctx->noop_dsa = util_blitter_get_noop_dsa_state(sctx->blitter);
      sctx->queued.named.dsa = sctx->noop_dsa;

      sctx->no_velems_state = sctx->b.create_vertex_elements_state(&sctx->b, 0, NULL);
      sctx->vertex_elements = sctx->no_velems_state;

      sctx->discard_rasterizer_state = util_blitter_get_discard_rasterizer_state(sctx->blitter);
      sctx->queued.named.rasterizer = sctx->discard_rasterizer_state;

      switch (sctx->chip_class) {
      case GFX6:
         si_init_draw_functions_GFX6(sctx);
         break;
      case GFX7:
         si_init_draw_functions_GFX7(sctx);
         break;
      case GFX8:
         si_init_draw_functions_GFX8(sctx);
         break;
      case GFX9:
         si_init_draw_functions_GFX9(sctx);
         break;
      case GFX10:
         si_init_draw_functions_GFX10(sctx);
         break;
      case GFX10_3:
         si_init_draw_functions_GFX10_3(sctx);
         break;
      default:
         unreachable("unhandled chip class");
      }
   }

   sctx->sample_mask = 0xffff;

   /* Initialize multimedia functions. */
   if (sscreen->info.has_video_hw.uvd_decode || sscreen->info.has_video_hw.vcn_decode ||
       sscreen->info.has_video_hw.jpeg_decode || sscreen->info.has_video_hw.vce_encode ||
       sscreen->info.has_video_hw.uvd_encode || sscreen->info.has_video_hw.vcn_encode) {
      sctx->b.create_video_codec = si_uvd_create_decoder;
      sctx->b.create_video_buffer = si_video_buffer_create;
      if (screen->resource_create_with_modifiers)
         sctx->b.create_video_buffer_with_modifiers = si_video_buffer_create_with_modifiers;
   } else {
      sctx->b.create_video_codec = vl_create_decoder;
      sctx->b.create_video_buffer = vl_video_buffer_create;
   }

   if (sctx->chip_class >= GFX9) {
      sctx->wait_mem_scratch =
           si_aligned_buffer_create(screen,
                                    SI_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                    PIPE_USAGE_DEFAULT, 8,
                                    sscreen->info.tcc_cache_line_size);
      if (!sctx->wait_mem_scratch)
         goto fail;

      if (sscreen->info.has_tmz_support) {
         sctx->wait_mem_scratch_tmz =
              si_aligned_buffer_create(screen,
                                       SI_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL |
                                       PIPE_RESOURCE_FLAG_ENCRYPTED,
                                       PIPE_USAGE_DEFAULT, 8,
                                       sscreen->info.tcc_cache_line_size);
         if (!sctx->wait_mem_scratch_tmz)
            goto fail;
      }
   }

   /* GFX7 cannot unbind a constant buffer (S_BUFFER_LOAD doesn't skip loads
    * if NUM_RECORDS == 0). We need to use a dummy buffer instead. */
   if (sctx->chip_class == GFX7) {
      sctx->null_const_buf.buffer =
         pipe_aligned_buffer_create(screen,
                                    SI_RESOURCE_FLAG_32BIT | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                    PIPE_USAGE_DEFAULT, 16,
                                    sctx->screen->info.tcc_cache_line_size);
      if (!sctx->null_const_buf.buffer)
         goto fail;
      sctx->null_const_buf.buffer_size = sctx->null_const_buf.buffer->width0;

      unsigned start_shader = sctx->has_graphics ? 0 : PIPE_SHADER_COMPUTE;
      for (shader = start_shader; shader < SI_NUM_SHADERS; shader++) {
         for (i = 0; i < SI_NUM_CONST_BUFFERS; i++) {
            sctx->b.set_constant_buffer(&sctx->b, shader, i, false, &sctx->null_const_buf);
         }
      }

      si_set_internal_const_buffer(sctx, SI_HS_CONST_DEFAULT_TESS_LEVELS, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_VS_CONST_INSTANCE_DIVISORS, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_VS_CONST_CLIP_PLANES, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_PS_CONST_POLY_STIPPLE, &sctx->null_const_buf);
      si_set_internal_const_buffer(sctx, SI_PS_CONST_SAMPLE_POSITIONS, &sctx->null_const_buf);
   }

   uint64_t max_threads_per_block;
   screen->get_compute_param(screen, PIPE_SHADER_IR_NIR, PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK,
                             &max_threads_per_block);

   /* The maximum number of scratch waves. Scratch space isn't divided
    * evenly between CUs. The number is only a function of the number of CUs.
    * We can decrease the constant to decrease the scratch buffer size.
    *
    * sctx->scratch_waves must be >= the maximum possible size of
    * 1 threadgroup, so that the hw doesn't hang from being unable
    * to start any.
    *
    * The recommended value is 4 per CU at most. Higher numbers don't
    * bring much benefit, but they still occupy chip resources (think
    * async compute). I've seen ~2% performance difference between 4 and 32.
    */
   sctx->scratch_waves =
      MAX2(32 * sscreen->info.num_good_compute_units, max_threads_per_block / 64);

   /* Bindless handles. */
   sctx->tex_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   sctx->img_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   util_dynarray_init(&sctx->resident_tex_handles, NULL);
   util_dynarray_init(&sctx->resident_img_handles, NULL);
   util_dynarray_init(&sctx->resident_tex_needs_color_decompress, NULL);
   util_dynarray_init(&sctx->resident_img_needs_color_decompress, NULL);
   util_dynarray_init(&sctx->resident_tex_needs_depth_decompress, NULL);

   sctx->dirty_implicit_resources = _mesa_pointer_hash_table_create(NULL);
   if (!sctx->dirty_implicit_resources)
      goto fail;

   /* The remainder of this function initializes the gfx CS and must be last. */
   assert(sctx->gfx_cs.current.cdw == 0);

   if (sctx->has_graphics) {
      si_init_cp_reg_shadowing(sctx);
   }

   /* Set immutable fields of shader keys. */
   if (sctx->chip_class >= GFX9) {
      /* The LS output / HS input layout can be communicated
       * directly instead of via user SGPRs for merged LS-HS.
       * This also enables jumping over the VS prolog for HS-only waves.
       *
       * When the LS VGPR fix is needed, monolithic shaders can:
       *  - avoid initializing EXEC in both the LS prolog
       *    and the LS main part when !vs_needs_prolog
       *  - remove the fixup for unused input VGPRs
       */
      sctx->shader.tcs.key.opt.prefer_mono = 1;

      /* This enables jumping over the VS prolog for GS-only waves. */
      sctx->shader.gs.key.opt.prefer_mono = 1;
   }

   si_begin_new_gfx_cs(sctx, true);
   assert(sctx->gfx_cs.current.cdw == sctx->initial_gfx_cs_size);

   /* Initialize per-context buffers. */
   if (sctx->wait_mem_scratch)
      si_cp_write_data(sctx, sctx->wait_mem_scratch, 0, 4, V_370_MEM, V_370_ME,
                       &sctx->wait_mem_number);
   if (sctx->wait_mem_scratch_tmz)
      si_cp_write_data(sctx, sctx->wait_mem_scratch_tmz, 0, 4, V_370_MEM, V_370_ME,
                       &sctx->wait_mem_number);

   if (sctx->chip_class == GFX7) {
      /* Clear the NULL constant buffer, because loads should return zeros.
       * Note that this forces CP DMA to be used, because clover deadlocks
       * for some reason when the compute codepath is used.
       */
      uint32_t clear_value = 0;
      si_clear_buffer(sctx, sctx->null_const_buf.buffer, 0, sctx->null_const_buf.buffer->width0,
                      &clear_value, 4, SI_OP_SYNC_AFTER, SI_COHERENCY_SHADER,
                      SI_CP_DMA_CLEAR_METHOD);
   }

   if (!(flags & SI_CONTEXT_FLAG_AUX)) {
      p_atomic_inc(&screen->num_contexts);

      /* Check if the aux_context needs to be recreated */
      struct si_context *saux = (struct si_context *)sscreen->aux_context;

      simple_mtx_lock(&sscreen->aux_context_lock);
      enum pipe_reset_status status = sctx->ws->ctx_query_reset_status(
         saux->ctx, true, NULL);
      if (status != PIPE_NO_RESET) {
         /* We lost the aux_context, create a new one */
         struct u_log_context *aux_log = (saux)->log;
         sscreen->aux_context->set_log_context(sscreen->aux_context, NULL);
         sscreen->aux_context->destroy(sscreen->aux_context);

         sscreen->aux_context = si_create_context(
            &sscreen->b, SI_CONTEXT_FLAG_AUX |
                         (sscreen->options.aux_debug ? PIPE_CONTEXT_DEBUG : 0) |
                         (sscreen->info.has_graphics ? 0 : PIPE_CONTEXT_COMPUTE_ONLY));
         sscreen->aux_context->set_log_context(sscreen->aux_context, aux_log);
      }
      simple_mtx_unlock(&sscreen->aux_context_lock);
   }

   sctx->initial_gfx_cs_size = sctx->gfx_cs.current.cdw;
   return &sctx->b;
fail:
   fprintf(stderr, "radeonsi: Failed to create a context.\n");
   si_destroy_context(&sctx->b);
   return NULL;
}

static bool si_is_resource_busy(struct pipe_screen *screen, struct pipe_resource *resource,
                                unsigned usage)
{
   struct radeon_winsys *ws = ((struct si_screen *)screen)->ws;

   return !ws->buffer_wait(ws, si_resource(resource)->buf, 0,
                           /* If mapping for write, we need to wait for all reads and writes.
                            * If mapping for read, we only need to wait for writes.
                            */
                           usage & PIPE_MAP_WRITE ? RADEON_USAGE_READWRITE : RADEON_USAGE_WRITE);
}

static struct pipe_context *si_pipe_create_context(struct pipe_screen *screen, void *priv,
                                                   unsigned flags)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct pipe_context *ctx;

   if (sscreen->debug_flags & DBG(CHECK_VM))
      flags |= PIPE_CONTEXT_DEBUG;

   ctx = si_create_context(screen, flags);

   if (ctx && sscreen->info.chip_class >= GFX9 && sscreen->debug_flags & DBG(SQTT)) {
      if (!si_init_thread_trace((struct si_context *)ctx)) {
         FREE(ctx);
         return NULL;
      }
   }

   if (!(flags & PIPE_CONTEXT_PREFER_THREADED))
      return ctx;

   /* Clover (compute-only) is unsupported. */
   if (flags & PIPE_CONTEXT_COMPUTE_ONLY)
      return ctx;

   /* When shaders are logged to stderr, asynchronous compilation is
    * disabled too. */
   if (sscreen->debug_flags & DBG_ALL_SHADERS)
      return ctx;

   /* Use asynchronous flushes only on amdgpu, since the radeon
    * implementation for fence_server_sync is incomplete. */
   struct pipe_context *tc =
      threaded_context_create(ctx, &sscreen->pool_transfers,
                              si_replace_buffer_storage,
                              sscreen->info.is_amdgpu ? si_create_fence : NULL,
                              si_is_resource_busy,
                              true,
                              &((struct si_context *)ctx)->tc);

   if (tc && tc != ctx)
      threaded_context_init_bytes_mapped_limit((struct threaded_context *)tc, 4);

   return tc;
}

/*
 * pipe_screen
 */
static void si_destroy_screen(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;
   struct si_shader_part *parts[] = {sscreen->vs_prologs, sscreen->tcs_epilogs, sscreen->gs_prologs,
                                     sscreen->ps_prologs, sscreen->ps_epilogs};
   unsigned i;

   if (!sscreen->ws->unref(sscreen->ws))
      return;

   if (sscreen->debug_flags & DBG(CACHE_STATS)) {
      printf("live shader cache:   hits = %u, misses = %u\n", sscreen->live_shader_cache.hits,
             sscreen->live_shader_cache.misses);
      printf("memory shader cache: hits = %u, misses = %u\n", sscreen->num_memory_shader_cache_hits,
             sscreen->num_memory_shader_cache_misses);
      printf("disk shader cache:   hits = %u, misses = %u\n", sscreen->num_disk_shader_cache_hits,
             sscreen->num_disk_shader_cache_misses);
   }

   simple_mtx_destroy(&sscreen->aux_context_lock);

   if (sscreen->aux_context) {
       struct u_log_context *aux_log = ((struct si_context *)sscreen->aux_context)->log;
       if (aux_log) {
          sscreen->aux_context->set_log_context(sscreen->aux_context, NULL);
          u_log_context_destroy(aux_log);
          FREE(aux_log);
       }

       sscreen->aux_context->destroy(sscreen->aux_context);
   }

   util_queue_destroy(&sscreen->shader_compiler_queue);
   util_queue_destroy(&sscreen->shader_compiler_queue_low_priority);

   /* Release the reference on glsl types of the compiler threads. */
   glsl_type_singleton_decref();

   for (i = 0; i < ARRAY_SIZE(sscreen->compiler); i++)
      si_destroy_compiler(&sscreen->compiler[i]);

   for (i = 0; i < ARRAY_SIZE(sscreen->compiler_lowp); i++)
      si_destroy_compiler(&sscreen->compiler_lowp[i]);

   /* Free shader parts. */
   for (i = 0; i < ARRAY_SIZE(parts); i++) {
      while (parts[i]) {
         struct si_shader_part *part = parts[i];

         parts[i] = part->next;
         si_shader_binary_clean(&part->binary);
         FREE(part);
      }
   }
   simple_mtx_destroy(&sscreen->shader_parts_mutex);
   si_destroy_shader_cache(sscreen);

   si_destroy_perfcounters(sscreen);
   si_gpu_load_kill_thread(sscreen);

   simple_mtx_destroy(&sscreen->gpu_load_mutex);

   slab_destroy_parent(&sscreen->pool_transfers);

   disk_cache_destroy(sscreen->disk_shader_cache);
   util_live_shader_cache_deinit(&sscreen->live_shader_cache);
   util_idalloc_mt_fini(&sscreen->buffer_ids);

   sscreen->ws->destroy(sscreen->ws);
   FREE(sscreen);
}

static void si_init_gs_info(struct si_screen *sscreen)
{
   sscreen->gs_table_depth = ac_get_gs_table_depth(sscreen->info.chip_class, sscreen->info.family);
}

static void si_test_vmfault(struct si_screen *sscreen, uint64_t test_flags)
{
   struct pipe_context *ctx = sscreen->aux_context;
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_resource *buf = pipe_buffer_create_const0(&sscreen->b, 0, PIPE_USAGE_DEFAULT, 64);

   if (!buf) {
      puts("Buffer allocation failed.");
      exit(1);
   }

   si_resource(buf)->gpu_address = 0; /* cause a VM fault */

   if (test_flags & DBG(TEST_VMFAULT_CP)) {
      si_cp_dma_copy_buffer(sctx, buf, buf, 0, 4, 4, SI_OP_SYNC_BEFORE_AFTER,
                            SI_COHERENCY_NONE, L2_BYPASS);
      ctx->flush(ctx, NULL, 0);
      puts("VM fault test: CP - done.");
   }
   if (test_flags & DBG(TEST_VMFAULT_SHADER)) {
      util_test_constant_buffer(ctx, buf);
      puts("VM fault test: Shader - done.");
   }
   exit(0);
}

static void si_test_gds_memory_management(struct si_context *sctx, unsigned alloc_size,
                                          unsigned alignment, enum radeon_bo_domain domain)
{
   struct radeon_winsys *ws = sctx->ws;
   struct radeon_cmdbuf cs[8];
   struct pb_buffer *gds_bo[ARRAY_SIZE(cs)];

   for (unsigned i = 0; i < ARRAY_SIZE(cs); i++) {
      ws->cs_create(&cs[i], sctx->ctx, RING_COMPUTE, NULL, NULL, false);
      gds_bo[i] = ws->buffer_create(ws, alloc_size, alignment, domain, 0);
      assert(gds_bo[i]);
   }

   for (unsigned iterations = 0; iterations < 20000; iterations++) {
      for (unsigned i = 0; i < ARRAY_SIZE(cs); i++) {
         /* This clears GDS with CP DMA.
          *
          * We don't care if GDS is present. Just add some packet
          * to make the GPU busy for a moment.
          */
         si_cp_dma_clear_buffer(
            sctx, &cs[i], NULL, 0, alloc_size, 0,
            SI_OP_CPDMA_SKIP_CHECK_CS_SPACE, 0,
            0);

         ws->cs_add_buffer(&cs[i], gds_bo[i], RADEON_USAGE_READWRITE, domain, 0);
         ws->cs_flush(&cs[i], PIPE_FLUSH_ASYNC, NULL);
      }
   }
   exit(0);
}

static void si_disk_cache_create(struct si_screen *sscreen)
{
   /* Don't use the cache if shader dumping is enabled. */
   if (sscreen->debug_flags & DBG_ALL_SHADERS)
      return;

   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   char cache_id[20 * 2 + 1];

   _mesa_sha1_init(&ctx);

   if (!disk_cache_get_function_identifier(si_disk_cache_create, &ctx) ||
       !disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
      return;

   _mesa_sha1_final(&ctx, sha1);
   disk_cache_format_hex_id(cache_id, sha1, 20 * 2);

   sscreen->disk_shader_cache = disk_cache_create(sscreen->info.name, cache_id,
                                                  sscreen->info.address32_hi);
}

static void si_set_max_shader_compiler_threads(struct pipe_screen *screen, unsigned max_threads)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   /* This function doesn't allow a greater number of threads than
    * the queue had at its creation. */
   util_queue_adjust_num_threads(&sscreen->shader_compiler_queue, max_threads);
   /* Don't change the number of threads on the low priority queue. */
}

static bool si_is_parallel_shader_compilation_finished(struct pipe_screen *screen, void *shader,
                                                       enum pipe_shader_type shader_type)
{
   struct si_shader_selector *sel = (struct si_shader_selector *)shader;

   return util_queue_fence_is_signalled(&sel->ready);
}

static struct pipe_screen *radeonsi_screen_create_impl(struct radeon_winsys *ws,
                                                       const struct pipe_screen_config *config)
{
   struct si_screen *sscreen = CALLOC_STRUCT(si_screen);
   unsigned hw_threads, num_comp_hi_threads, num_comp_lo_threads;
   uint64_t test_flags;

   if (!sscreen) {
      return NULL;
   }

   {
#define OPT_BOOL(name, dflt, description)                                                          \
   sscreen->options.name = driQueryOptionb(config->options, "radeonsi_" #name);
#include "si_debug_options.h"
   }

   sscreen->ws = ws;
   ws->query_info(ws, &sscreen->info,
                  sscreen->options.enable_sam,
                  sscreen->options.disable_sam);

   if (sscreen->info.chip_class >= GFX9) {
      sscreen->se_tile_repeat = 32 * sscreen->info.max_se;
   } else {
      ac_get_raster_config(&sscreen->info, &sscreen->pa_sc_raster_config,
                           &sscreen->pa_sc_raster_config_1, &sscreen->se_tile_repeat);
   }

   sscreen->debug_flags = debug_get_flags_option("R600_DEBUG", radeonsi_debug_options, 0);
   sscreen->debug_flags |= debug_get_flags_option("AMD_DEBUG", radeonsi_debug_options, 0);
   test_flags = debug_get_flags_option("AMD_TEST", test_options, 0);

   if (sscreen->debug_flags & DBG(NO_GFX))
      sscreen->info.has_graphics = false;

   if ((sscreen->debug_flags & DBG(TMZ)) &&
       !sscreen->info.has_tmz_support) {
      fprintf(stderr, "radeonsi: requesting TMZ features but TMZ is not supported\n");
      FREE(sscreen);
      return NULL;
   }

   util_idalloc_mt_init_tc(&sscreen->buffer_ids);

   /* Set functions first. */
   sscreen->b.context_create = si_pipe_create_context;
   sscreen->b.destroy = si_destroy_screen;
   sscreen->b.set_max_shader_compiler_threads = si_set_max_shader_compiler_threads;
   sscreen->b.is_parallel_shader_compilation_finished = si_is_parallel_shader_compilation_finished;
   sscreen->b.finalize_nir = si_finalize_nir;

   si_init_screen_get_functions(sscreen);
   si_init_screen_buffer_functions(sscreen);
   si_init_screen_fence_functions(sscreen);
   si_init_screen_state_functions(sscreen);
   si_init_screen_texture_functions(sscreen);
   si_init_screen_query_functions(sscreen);
   si_init_screen_live_shader_cache(sscreen);

   /* Set these flags in debug_flags early, so that the shader cache takes
    * them into account.
    *
    * Enable FS_CORRECT_DERIVS_AFTER_KILL by default if LLVM is >= 13. This makes
    * nir_opt_move_discards_to_top more effective.
    */
   if (driQueryOptionb(config->options, "glsl_correct_derivatives_after_discard") ||
       LLVM_VERSION_MAJOR >= 13)
      sscreen->debug_flags |= DBG(FS_CORRECT_DERIVS_AFTER_KILL);

   if (sscreen->debug_flags & DBG(INFO))
      ac_print_gpu_info(&sscreen->info, stdout);

   slab_create_parent(&sscreen->pool_transfers, sizeof(struct si_transfer), 64);

   sscreen->force_aniso = MIN2(16, debug_get_num_option("R600_TEX_ANISO", -1));
   if (sscreen->force_aniso == -1) {
      sscreen->force_aniso = MIN2(16, debug_get_num_option("AMD_TEX_ANISO", -1));
   }

   if (sscreen->force_aniso >= 0) {
      printf("radeonsi: Forcing anisotropy filter to %ix\n",
             /* round down to a power of two */
             1 << util_logbase2(sscreen->force_aniso));
   }

   (void)simple_mtx_init(&sscreen->aux_context_lock, mtx_plain);
   (void)simple_mtx_init(&sscreen->gpu_load_mutex, mtx_plain);

   si_init_gs_info(sscreen);
   if (!si_init_shader_cache(sscreen)) {
      FREE(sscreen);
      return NULL;
   }

   if (sscreen->info.chip_class < GFX10_3)
      sscreen->options.vrs2x2 = false;

   si_disk_cache_create(sscreen);

   /* Determine the number of shader compiler threads. */
   const struct util_cpu_caps_t *caps = util_get_cpu_caps();
   hw_threads = caps->nr_cpus;

   if (hw_threads >= 12) {
      num_comp_hi_threads = hw_threads * 3 / 4;
      num_comp_lo_threads = hw_threads / 3;
   } else if (hw_threads >= 6) {
      num_comp_hi_threads = hw_threads - 2;
      num_comp_lo_threads = hw_threads / 2;
   } else if (hw_threads >= 2) {
      num_comp_hi_threads = hw_threads - 1;
      num_comp_lo_threads = hw_threads / 2;
   } else {
      num_comp_hi_threads = 1;
      num_comp_lo_threads = 1;
   }

   num_comp_hi_threads = MIN2(num_comp_hi_threads, ARRAY_SIZE(sscreen->compiler));
   num_comp_lo_threads = MIN2(num_comp_lo_threads, ARRAY_SIZE(sscreen->compiler_lowp));

   /* Take a reference on the glsl types for the compiler threads. */
   glsl_type_singleton_init_or_ref();

   if (!util_queue_init(
          &sscreen->shader_compiler_queue, "sh", 64, num_comp_hi_threads,
          UTIL_QUEUE_INIT_RESIZE_IF_FULL | UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY, NULL)) {
      si_destroy_shader_cache(sscreen);
      FREE(sscreen);
      glsl_type_singleton_decref();
      return NULL;
   }

   if (!util_queue_init(&sscreen->shader_compiler_queue_low_priority, "shlo", 64,
                        num_comp_lo_threads,
                        UTIL_QUEUE_INIT_RESIZE_IF_FULL | UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY |
                           UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY, NULL)) {
      si_destroy_shader_cache(sscreen);
      FREE(sscreen);
      glsl_type_singleton_decref();
      return NULL;
   }

   if (!debug_get_bool_option("RADEON_DISABLE_PERFCOUNTERS", false))
      si_init_perfcounters(sscreen);

   sscreen->max_memory_usage_kb = sscreen->info.vram_size_kb + sscreen->info.gart_size_kb / 4 * 3;

   /* Determine tessellation ring info. */
   bool double_offchip_buffers = sscreen->info.chip_class >= GFX7 &&
                                 sscreen->info.family != CHIP_CARRIZO &&
                                 sscreen->info.family != CHIP_STONEY;
   /* This must be one less than the maximum number due to a hw limitation.
    * Various hardware bugs need this.
    */
   unsigned max_offchip_buffers_per_se;

   if (sscreen->info.chip_class >= GFX10)
      max_offchip_buffers_per_se = 128;
   /* Only certain chips can use the maximum value. */
   else if (sscreen->info.family == CHIP_VEGA12 || sscreen->info.family == CHIP_VEGA20)
      max_offchip_buffers_per_se = double_offchip_buffers ? 128 : 64;
   else
      max_offchip_buffers_per_se = double_offchip_buffers ? 127 : 63;

   unsigned max_offchip_buffers = max_offchip_buffers_per_se * sscreen->info.max_se;
   unsigned offchip_granularity;

   /* Hawaii has a bug with offchip buffers > 256 that can be worked
    * around by setting 4K granularity.
    */
   if (sscreen->info.family == CHIP_HAWAII) {
      sscreen->tess_offchip_block_dw_size = 4096;
      offchip_granularity = V_03093C_X_4K_DWORDS;
   } else {
      sscreen->tess_offchip_block_dw_size = 8192;
      offchip_granularity = V_03093C_X_8K_DWORDS;
   }

   sscreen->tess_factor_ring_size = 32768 * sscreen->info.max_se;
   sscreen->tess_offchip_ring_size = max_offchip_buffers * sscreen->tess_offchip_block_dw_size * 4;

   if (sscreen->info.chip_class >= GFX10_3) {
      sscreen->vgt_hs_offchip_param =
            S_03093C_OFFCHIP_BUFFERING_GFX103(max_offchip_buffers - 1) |
            S_03093C_OFFCHIP_GRANULARITY_GFX103(offchip_granularity);
   } else if (sscreen->info.chip_class >= GFX7) {
      if (sscreen->info.chip_class >= GFX8)
         --max_offchip_buffers;
      sscreen->vgt_hs_offchip_param = S_03093C_OFFCHIP_BUFFERING_GFX7(max_offchip_buffers) |
                                      S_03093C_OFFCHIP_GRANULARITY_GFX7(offchip_granularity);
   } else {
      assert(offchip_granularity == V_03093C_X_8K_DWORDS);
      sscreen->vgt_hs_offchip_param = S_0089B0_OFFCHIP_BUFFERING(max_offchip_buffers);
   }

   sscreen->has_draw_indirect_multi =
      (sscreen->info.family >= CHIP_POLARIS10) ||
      (sscreen->info.chip_class == GFX8 && sscreen->info.pfp_fw_version >= 121 &&
       sscreen->info.me_fw_version >= 87) ||
      (sscreen->info.chip_class == GFX7 && sscreen->info.pfp_fw_version >= 211 &&
       sscreen->info.me_fw_version >= 173) ||
      (sscreen->info.chip_class == GFX6 && sscreen->info.pfp_fw_version >= 79 &&
       sscreen->info.me_fw_version >= 142);

   sscreen->has_out_of_order_rast =
      sscreen->info.has_out_of_order_rast && !(sscreen->debug_flags & DBG(NO_OUT_OF_ORDER));
   sscreen->assume_no_z_fights = driQueryOptionb(config->options, "radeonsi_assume_no_z_fights") ||
                                 driQueryOptionb(config->options, "allow_draw_out_of_order");
   sscreen->commutative_blend_add =
      driQueryOptionb(config->options, "radeonsi_commutative_blend_add") ||
      driQueryOptionb(config->options, "allow_draw_out_of_order");
   sscreen->allow_draw_out_of_order = driQueryOptionb(config->options, "allow_draw_out_of_order");

   sscreen->use_ngg = !(sscreen->debug_flags & DBG(NO_NGG)) &&
                      sscreen->info.chip_class >= GFX10 &&
                      (sscreen->info.family != CHIP_NAVI14 ||
                       sscreen->info.is_pro_graphics);
   sscreen->use_ngg_culling = sscreen->use_ngg &&
                              sscreen->info.max_render_backends >= 2 &&
                              !((sscreen->debug_flags & DBG(NO_NGG_CULLING)) ||
                                LLVM_VERSION_MAJOR <= 11 /* hangs on 11, see #4874 */);
   sscreen->use_ngg_streamout = false;

   /* Only set this for the cases that are known to work, which are:
    * - GFX9 if bpp >= 4 (in bytes)
    */
   if (sscreen->info.chip_class == GFX9) {
      for (unsigned bpp_log2 = util_logbase2(4); bpp_log2 <= util_logbase2(16); bpp_log2++)
         sscreen->allow_dcc_msaa_clear_to_reg_for_bpp[bpp_log2] = true;
   }

   /* DCC stores have 50% performance of uncompressed stores and sometimes
    * even less than that. It's risky to enable on dGPUs.
    */
   sscreen->always_allow_dcc_stores = !(sscreen->debug_flags & DBG(NO_DCC_STORE)) &&
                                      ((sscreen->info.chip_class >= GFX10_3 &&
                                        !sscreen->info.has_dedicated_vram) ||
                                       sscreen->debug_flags & DBG(DCC_STORE));

   sscreen->dpbb_allowed = !(sscreen->debug_flags & DBG(NO_DPBB)) &&
                           (sscreen->info.chip_class >= GFX10 ||
                            /* Only enable primitive binning on gfx9 APUs by default. */
                            (sscreen->info.chip_class == GFX9 && !sscreen->info.has_dedicated_vram) ||
                            sscreen->debug_flags & DBG(DPBB));

   if (sscreen->dpbb_allowed) {
      if (sscreen->info.has_dedicated_vram) {
         if (sscreen->info.max_render_backends > 4) {
            sscreen->pbb_context_states_per_bin = 1;
            sscreen->pbb_persistent_states_per_bin = 1;
         } else {
            sscreen->pbb_context_states_per_bin = 3;
            sscreen->pbb_persistent_states_per_bin = 8;
         }
      } else {
         /* This is a workaround for:
          *    https://bugs.freedesktop.org/show_bug.cgi?id=110214
          * (an alternative is to insert manual BATCH_BREAK event when
          *  a context_roll is detected). */
         sscreen->pbb_context_states_per_bin = sscreen->info.has_gfx9_scissor_bug ? 1 : 6;
         /* Using 32 here can cause GPU hangs on RAVEN1 */
         sscreen->pbb_persistent_states_per_bin = 16;
      }

      assert(sscreen->pbb_context_states_per_bin >= 1 &&
             sscreen->pbb_context_states_per_bin <= 6);
      assert(sscreen->pbb_persistent_states_per_bin >= 1 &&
             sscreen->pbb_persistent_states_per_bin <= 32);
   }

   (void)simple_mtx_init(&sscreen->shader_parts_mutex, mtx_plain);
   sscreen->use_monolithic_shaders = (sscreen->debug_flags & DBG(MONOLITHIC_SHADERS)) != 0;

   sscreen->barrier_flags.cp_to_L2 = SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE;
   if (sscreen->info.chip_class <= GFX8) {
      sscreen->barrier_flags.cp_to_L2 |= SI_CONTEXT_INV_L2;
      sscreen->barrier_flags.L2_to_cp |= SI_CONTEXT_WB_L2;
   }

   if (debug_get_bool_option("RADEON_DUMP_SHADERS", false))
      sscreen->debug_flags |= DBG_ALL_SHADERS;

   /* Syntax:
    *     EQAA=s,z,c
    * Example:
    *     EQAA=8,4,2

    * That means 8 coverage samples, 4 Z/S samples, and 2 color samples.
    * Constraints:
    *     s >= z >= c (ignoring this only wastes memory)
    *     s = [2..16]
    *     z = [2..8]
    *     c = [2..8]
    *
    * Only MSAA color and depth buffers are overriden.
    */
   if (sscreen->info.has_eqaa_surface_allocator) {
      const char *eqaa = debug_get_option("EQAA", NULL);
      unsigned s, z, f;

      if (eqaa && sscanf(eqaa, "%u,%u,%u", &s, &z, &f) == 3 && s && z && f) {
         sscreen->eqaa_force_coverage_samples = s;
         sscreen->eqaa_force_z_samples = z;
         sscreen->eqaa_force_color_samples = f;
      }
   }

   sscreen->ngg_subgroup_size = 128;
   sscreen->ge_wave_size = 64;
   sscreen->ps_wave_size = 64;
   sscreen->compute_wave_size = 64;

   if (sscreen->info.chip_class >= GFX10) {
      /* Pixel shaders: Wave64 is always fastest.
       * Vertex shaders: Wave64 is probably better, because:
       * - greater chance of L0 cache hits, because more threads are assigned
       *   to the same CU
       * - scalar instructions are only executed once for 64 threads instead of twice
       * - VGPR allocation granularity is half of Wave32, so 1 Wave64 can
       *   sometimes use fewer VGPRs than 2 Wave32
       * - TessMark X64 with NGG culling is faster with Wave64
       */
      if (sscreen->debug_flags & DBG(W32_GE))
         sscreen->ge_wave_size = 32;
      if (sscreen->debug_flags & DBG(W32_PS))
         sscreen->ps_wave_size = 32;
      if (sscreen->debug_flags & DBG(W32_CS))
         sscreen->compute_wave_size = 32;

      if (sscreen->debug_flags & DBG(W64_GE))
         sscreen->ge_wave_size = 64;
      if (sscreen->debug_flags & DBG(W64_PS))
         sscreen->ps_wave_size = 64;
      if (sscreen->debug_flags & DBG(W64_CS))
         sscreen->compute_wave_size = 64;
   }

   /* Create the auxiliary context. This must be done last. */
   sscreen->aux_context = si_create_context(
      &sscreen->b,
      SI_CONTEXT_FLAG_AUX |
      (sscreen->options.aux_debug ? PIPE_CONTEXT_DEBUG : 0) |
      (sscreen->info.has_graphics ? 0 : PIPE_CONTEXT_COMPUTE_ONLY));

   if (sscreen->options.aux_debug) {
      struct u_log_context *log = CALLOC_STRUCT(u_log_context);
      u_log_context_init(log);
      sscreen->aux_context->set_log_context(sscreen->aux_context, log);
   }

   if (test_flags & DBG(TEST_BLIT))
      si_test_blit(sscreen);

   if (test_flags & DBG(TEST_DMA_PERF)) {
      si_test_dma_perf(sscreen);
   }

   if (test_flags & (DBG(TEST_VMFAULT_CP) | DBG(TEST_VMFAULT_SHADER)))
      si_test_vmfault(sscreen, test_flags);

   if (test_flags & DBG(TEST_GDS))
      si_test_gds((struct si_context *)sscreen->aux_context);

   if (test_flags & DBG(TEST_GDS_MM)) {
      si_test_gds_memory_management((struct si_context *)sscreen->aux_context, 32 * 1024, 4,
                                    RADEON_DOMAIN_GDS);
   }
   if (test_flags & DBG(TEST_GDS_OA_MM)) {
      si_test_gds_memory_management((struct si_context *)sscreen->aux_context, 4, 1,
                                    RADEON_DOMAIN_OA);
   }

   ac_print_shadowed_regs(&sscreen->info);

   STATIC_ASSERT(sizeof(union si_vgt_stages_key) == 1);
   return &sscreen->b;
}

struct pipe_screen *radeonsi_screen_create(int fd, const struct pipe_screen_config *config)
{
   drmVersionPtr version = drmGetVersion(fd);
   struct radeon_winsys *rw = NULL;

   driParseConfigFiles(config->options, config->options_info, 0, "radeonsi",
                       NULL, NULL, NULL, 0, NULL, 0);

   switch (version->version_major) {
   case 2:
      rw = radeon_drm_winsys_create(fd, config, radeonsi_screen_create_impl);
      break;
   case 3:
      rw = amdgpu_winsys_create(fd, config, radeonsi_screen_create_impl);
      break;
   }

   drmFreeVersion(version);
   return rw ? rw->screen : NULL;
}
