/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 7
// llvm redefines DEBUG
#pragma push_macro("DEBUG")
#undef DEBUG
#endif

#include "JitManager.h"
#include "llvm-c/Core.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/IR/LegacyPassManager.h"

#if LLVM_VERSION_MAJOR < 7
#pragma pop_macro("DEBUG")
#endif

#include "state.h"
#include "gen_state_llvm.h"
#include "builder.h"
#include "functionpasses/passes.h"

#include "tgsi/tgsi_strings.h"
#include "util/format/u_format.h"
#include "util/u_prim.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_struct.h"
#include "gallivm/lp_bld_tgsi.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_printf.h"
#include "gallivm/lp_bld_logic.h"

#include "swr_context.h"
#include "gen_surf_state_llvm.h"
#include "gen_swr_context_llvm.h"
#include "swr_resource.h"
#include "swr_state.h"
#include "swr_screen.h"


/////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <inttypes.h>

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "gallivm/lp_bld_type.h"

#if defined(DEBUG) && defined(SWR_VERBOSE_SHADER)
constexpr bool verbose_shader          = true;
constexpr bool verbose_tcs_shader_in   = true;
constexpr bool verbose_tcs_shader_out  = true;
constexpr bool verbose_tcs_shader_loop = true;
constexpr bool verbose_vs_shader       = true;
#else
constexpr bool verbose_shader          = false;
constexpr bool verbose_tcs_shader_in   = false;
constexpr bool verbose_tcs_shader_out  = false;
constexpr bool verbose_tcs_shader_loop = false;
constexpr bool verbose_vs_shader       = false;
#endif

using namespace SwrJit;

static unsigned
locate_linkage(ubyte name, ubyte index, struct tgsi_shader_info *info);

bool operator==(const swr_jit_fs_key &lhs, const swr_jit_fs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_vs_key &lhs, const swr_jit_vs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_fetch_key &lhs, const swr_jit_fetch_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_gs_key &lhs, const swr_jit_gs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_tcs_key &lhs, const swr_jit_tcs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_tes_key &lhs, const swr_jit_tes_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}


static void
swr_generate_sampler_key(const struct lp_tgsi_info &info,
                         struct swr_context *ctx,
                         enum pipe_shader_type shader_type,
                         struct swr_jit_sampler_key &key)
{
   key.nr_samplers = info.base.file_max[TGSI_FILE_SAMPLER] + 1;

   for (unsigned i = 0; i < key.nr_samplers; i++) {
      if (info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
         lp_sampler_static_sampler_state(
            &key.sampler[i].sampler_state,
            ctx->samplers[shader_type][i]);
      }
   }

   /*
    * XXX If TGSI_FILE_SAMPLER_VIEW exists assume all texture opcodes
    * are dx10-style? Can't really have mixed opcodes, at least not
    * if we want to skip the holes here (without rescanning tgsi).
    */
   if (info.base.file_max[TGSI_FILE_SAMPLER_VIEW] != -1) {
      key.nr_sampler_views =
         info.base.file_max[TGSI_FILE_SAMPLER_VIEW] + 1;
      for (unsigned i = 0; i < key.nr_sampler_views; i++) {
         if (info.base.file_mask[TGSI_FILE_SAMPLER_VIEW] & (1u << (i & 31))) {
            const struct pipe_sampler_view *view =
               ctx->sampler_views[shader_type][i];
            lp_sampler_static_texture_state(
               &key.sampler[i].texture_state, view);
            if (view) {
               struct swr_resource *swr_res = swr_resource(view->texture);
               const struct util_format_description *desc =
                  util_format_description(view->format);
               if (swr_res->has_depth && swr_res->has_stencil &&
                   !util_format_has_depth(desc))
                  key.sampler[i].texture_state.format = PIPE_FORMAT_S8_UINT;
            }
         }
      }
   } else {
      key.nr_sampler_views = key.nr_samplers;
      for (unsigned i = 0; i < key.nr_sampler_views; i++) {
         if (info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
            const struct pipe_sampler_view *view =
               ctx->sampler_views[shader_type][i];
            lp_sampler_static_texture_state(
               &key.sampler[i].texture_state, view);
            if (view) {
               struct swr_resource *swr_res = swr_resource(view->texture);
               const struct util_format_description *desc =
                  util_format_description(view->format);
               if (swr_res->has_depth && swr_res->has_stencil &&
                   !util_format_has_depth(desc))
                  key.sampler[i].texture_state.format = PIPE_FORMAT_S8_UINT;
            }
         }
      }
   }
}

void
swr_generate_fs_key(struct swr_jit_fs_key &key,
                    struct swr_context *ctx,
                    swr_fragment_shader *swr_fs)
{
   memset((void*)&key, 0, sizeof(key));

   key.nr_cbufs = ctx->framebuffer.nr_cbufs;
   key.light_twoside = ctx->rasterizer->light_twoside;
   key.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable;

   struct tgsi_shader_info *pPrevShader;
   if (ctx->gs)
      pPrevShader = &ctx->gs->info.base;
   else if (ctx->tes)
      pPrevShader = &ctx->tes->info.base;
   else
      pPrevShader = &ctx->vs->info.base;

   memcpy(&key.vs_output_semantic_name,
          &pPrevShader->output_semantic_name,
          sizeof(key.vs_output_semantic_name));
   memcpy(&key.vs_output_semantic_idx,
          &pPrevShader->output_semantic_index,
          sizeof(key.vs_output_semantic_idx));

   swr_generate_sampler_key(swr_fs->info, ctx, PIPE_SHADER_FRAGMENT, key);

   key.poly_stipple_enable = ctx->rasterizer->poly_stipple_enable &&
      ctx->poly_stipple.prim_is_poly;
}

void
swr_generate_vs_key(struct swr_jit_vs_key &key,
                    struct swr_context *ctx,
                    swr_vertex_shader *swr_vs)
{
   memset((void*)&key, 0, sizeof(key));

   key.clip_plane_mask =
      swr_vs->info.base.clipdist_writemask ?
      swr_vs->info.base.clipdist_writemask & ctx->rasterizer->clip_plane_enable :
      ctx->rasterizer->clip_plane_enable;

   swr_generate_sampler_key(swr_vs->info, ctx, PIPE_SHADER_VERTEX, key);
}

void
swr_generate_fetch_key(struct swr_jit_fetch_key &key,
                       struct swr_vertex_element_state *velems)
{
   memset((void*)&key, 0, sizeof(key));

   key.fsState = velems->fsState;
}

void
swr_generate_gs_key(struct swr_jit_gs_key &key,
                    struct swr_context *ctx,
                    swr_geometry_shader *swr_gs)
{
   memset((void*)&key, 0, sizeof(key));

   struct tgsi_shader_info *pPrevShader = nullptr;

   if (ctx->tes) {
      pPrevShader = &ctx->tes->info.base;
   } else {
      pPrevShader = &ctx->vs->info.base;
   }

   memcpy(&key.vs_output_semantic_name,
          &pPrevShader->output_semantic_name,
          sizeof(key.vs_output_semantic_name));
   memcpy(&key.vs_output_semantic_idx,
          &pPrevShader->output_semantic_index,
          sizeof(key.vs_output_semantic_idx));

   swr_generate_sampler_key(swr_gs->info, ctx, PIPE_SHADER_GEOMETRY, key);
}

void
swr_generate_tcs_key(struct swr_jit_tcs_key &key,
                    struct swr_context *ctx,
                    swr_tess_control_shader *swr_tcs)
{
   memset((void*)&key, 0, sizeof(key));

   struct tgsi_shader_info *pPrevShader = &ctx->vs->info.base;

   memcpy(&key.vs_output_semantic_name,
          &pPrevShader->output_semantic_name,
          sizeof(key.vs_output_semantic_name));
   memcpy(&key.vs_output_semantic_idx,
          &pPrevShader->output_semantic_index,
          sizeof(key.vs_output_semantic_idx));

   key.clip_plane_mask =
      swr_tcs->info.base.clipdist_writemask ?
      swr_tcs->info.base.clipdist_writemask & ctx->rasterizer->clip_plane_enable :
      ctx->rasterizer->clip_plane_enable;

   swr_generate_sampler_key(swr_tcs->info, ctx, PIPE_SHADER_TESS_CTRL, key);
}

void
swr_generate_tes_key(struct swr_jit_tes_key &key,
                    struct swr_context *ctx,
                    swr_tess_evaluation_shader *swr_tes)
{
   memset((void*)&key, 0, sizeof(key));

   struct tgsi_shader_info *pPrevShader = nullptr;

   if (ctx->tcs) {
      pPrevShader = &ctx->tcs->info.base;
   }
   else {
      pPrevShader = &ctx->vs->info.base;
   }

   SWR_ASSERT(pPrevShader != nullptr, "TES: No TCS or VS defined");

   memcpy(&key.prev_output_semantic_name,
         &pPrevShader->output_semantic_name,
         sizeof(key.prev_output_semantic_name));
   memcpy(&key.prev_output_semantic_idx,
         &pPrevShader->output_semantic_index,
         sizeof(key.prev_output_semantic_idx));

   key.clip_plane_mask =
      swr_tes->info.base.clipdist_writemask ?
      swr_tes->info.base.clipdist_writemask & ctx->rasterizer->clip_plane_enable :
      ctx->rasterizer->clip_plane_enable;

   swr_generate_sampler_key(swr_tes->info, ctx, PIPE_SHADER_TESS_EVAL, key);
}

struct BuilderSWR : public Builder {
   BuilderSWR(JitManager *pJitMgr, const char *pName)
      : Builder(pJitMgr)
   {
      pJitMgr->SetupNewModule();
      gallivm = gallivm_create(pName, wrap(&JM()->mContext), NULL);
      pJitMgr->mpCurrentModule = unwrap(gallivm->module);
   }

   ~BuilderSWR() {
      gallivm_free_ir(gallivm);
   }

   void WriteVS(Value *pVal, Value *pVsContext, Value *pVtxOutput,
                unsigned slot, unsigned channel);

   struct gallivm_state *gallivm;
   PFN_VERTEX_FUNC CompileVS(struct swr_context *ctx, swr_jit_vs_key &key);
   PFN_PIXEL_KERNEL CompileFS(struct swr_context *ctx, swr_jit_fs_key &key);
   PFN_GS_FUNC CompileGS(struct swr_context *ctx, swr_jit_gs_key &key);
   PFN_TCS_FUNC CompileTCS(struct swr_context *ctx, swr_jit_tcs_key &key);
   PFN_TES_FUNC CompileTES(struct swr_context *ctx, swr_jit_tes_key &key);

   // GS-specific emit functions
   LLVMValueRef
   swr_gs_llvm_fetch_input(const struct lp_build_gs_iface *gs_iface,
                           struct lp_build_context * bld,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index);
   void
   swr_gs_llvm_emit_vertex(const struct lp_build_gs_iface *gs_base,
                           struct lp_build_context * bld,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec,
                           LLVMValueRef stream_id);

   void
   swr_gs_llvm_end_primitive(const struct lp_build_gs_iface *gs_base,
                             struct lp_build_context * bld,
                             LLVMValueRef total_emitted_vertices_vec_ptr,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec,
                             LLVMValueRef mask_vec);

   void
   swr_gs_llvm_epilogue(const struct lp_build_gs_iface *gs_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec, unsigned stream);

   // TCS-specific emit functions
   void swr_tcs_llvm_emit_prologue(struct lp_build_tgsi_soa_context* bld);
   void swr_tcs_llvm_emit_epilogue(struct lp_build_tgsi_soa_context* bld);

   LLVMValueRef
   swr_tcs_llvm_fetch_input(const struct lp_build_tcs_iface *tcs_iface,
                            struct lp_build_tgsi_context * bld_base,
                            boolean is_vindex_indirect,
                            LLVMValueRef vertex_index,
                            boolean is_aindex_indirect,
                            LLVMValueRef attrib_index,
                            LLVMValueRef swizzle_index);

   LLVMValueRef
   swr_tcs_llvm_fetch_output(const struct lp_build_tcs_iface *tcs_iface,
                             struct lp_build_tgsi_context * bld_base,
                             boolean is_vindex_indirect,
                             LLVMValueRef vertex_index,
                             boolean is_aindex_indirect,
                             LLVMValueRef attrib_index,
                             LLVMValueRef swizzle_index,
                             uint32_t name);

   void
   swr_tcs_llvm_store_output(const struct lp_build_tcs_iface *tcs_iface,
                            struct lp_build_tgsi_context * bld_base,
                            unsigned name,
                            boolean is_vindex_indirect,
                            LLVMValueRef vertex_index,
                            boolean is_aindex_indirect,
                            LLVMValueRef attrib_index,
                            LLVMValueRef swizzle_index,
                            LLVMValueRef value,
                            LLVMValueRef mask_vec);

   // Barrier implementation (available only in TCS)
   void
   swr_tcs_llvm_emit_barrier(const struct lp_build_tcs_iface *tcs_iface,
                             struct lp_build_tgsi_context *bld_base);

   // TES-specific emit functions
   LLVMValueRef
   swr_tes_llvm_fetch_vtx_input(const struct lp_build_tes_iface *tes_iface,
                            struct lp_build_tgsi_context * bld_base,
                            boolean is_vindex_indirect,
                            LLVMValueRef vertex_index,
                            boolean is_aindex_indirect,
                            LLVMValueRef attrib_index,
                            LLVMValueRef swizzle_index);

   LLVMValueRef
   swr_tes_llvm_fetch_patch_input(const struct lp_build_tes_iface *tes_iface,
                            struct lp_build_tgsi_context * bld_base,
                            boolean is_aindex_indirect,
                            LLVMValueRef attrib_index,
                            LLVMValueRef swizzle_index);
};

struct swr_gs_llvm_iface {
   struct lp_build_gs_iface base;
   struct tgsi_shader_info *info;

   BuilderSWR *pBuilder;

   Value *pGsCtx;
   SWR_GS_STATE *pGsState;
   uint32_t num_outputs;
   uint32_t num_verts_per_prim;

   Value *pVtxAttribMap;
};

struct swr_tcs_llvm_iface {
   struct lp_build_tcs_iface base;
   struct tgsi_shader_info *info;

   BuilderSWR *pBuilder;

   Value *pTcsCtx;
   SWR_TS_STATE *pTsState;

   uint32_t output_vertices;

   LLVMValueRef loop_var;

   Value *pVtxAttribMap;
   Value *pVtxOutputAttribMap;
   Value *pPatchOutputAttribMap;
};

struct swr_tes_llvm_iface {
   struct lp_build_tes_iface base;
   struct tgsi_shader_info *info;

   BuilderSWR *pBuilder;

   Value *pTesCtx;
   SWR_TS_STATE *pTsState;

   uint32_t num_outputs;

   Value *pVtxAttribMap;
   Value *pPatchAttribMap;
};

// trampoline functions so we can use the builder llvm construction methods
static LLVMValueRef
swr_gs_llvm_fetch_input(const struct lp_build_gs_iface *gs_iface,
                           struct lp_build_context * bld,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_iface;

    return iface->pBuilder->swr_gs_llvm_fetch_input(gs_iface, bld,
                                                   is_vindex_indirect,
                                                   vertex_index,
                                                   is_aindex_indirect,
                                                   attrib_index,
                                                   swizzle_index);
}

static void
swr_gs_llvm_emit_vertex(const struct lp_build_gs_iface *gs_base,
                           struct lp_build_context * bld,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec,
                           LLVMValueRef mask_vec,
                           LLVMValueRef stream_id)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_emit_vertex(gs_base, bld,
                                            outputs,
                                            emitted_vertices_vec,
                                            stream_id);
}

static void
swr_gs_llvm_end_primitive(const struct lp_build_gs_iface *gs_base,
                             struct lp_build_context * bld,
                             LLVMValueRef total_emitted_vertices_vec_ptr,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec,
                             LLVMValueRef mask_vec, unsigned stream_id)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_end_primitive(gs_base, bld,
                                              total_emitted_vertices_vec_ptr,
                                              verts_per_prim_vec,
                                              emitted_prims_vec,
                                              mask_vec);
}

static void
swr_gs_llvm_epilogue(const struct lp_build_gs_iface *gs_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec, unsigned stream)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_epilogue(gs_base,
                                         total_emitted_vertices_vec,
                                         emitted_prims_vec, stream);
}

static LLVMValueRef
swr_tcs_llvm_fetch_input(const struct lp_build_tcs_iface *tcs_iface,
                         struct lp_build_context * bld,
                         boolean is_vindex_indirect,
                         LLVMValueRef vertex_index,
                         boolean is_aindex_indirect,
                         LLVMValueRef attrib_index,
                         boolean is_sindex_indirect,
                         LLVMValueRef swizzle_index)
{
    swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;
    struct lp_build_tgsi_context *bld_base = (struct lp_build_tgsi_context*)bld;

    return iface->pBuilder->swr_tcs_llvm_fetch_input(tcs_iface, bld_base,
                                                     is_vindex_indirect,
                                                     vertex_index,
                                                     is_aindex_indirect,
                                                     attrib_index,
                                                     swizzle_index);
}

static LLVMValueRef
swr_tcs_llvm_fetch_output(const struct lp_build_tcs_iface *tcs_iface,
                          struct lp_build_context * bld,
                          boolean is_vindex_indirect,
                          LLVMValueRef vertex_index,
                          boolean is_aindex_indirect,
                          LLVMValueRef attrib_index,
                          boolean is_sindex_indirect,
                          LLVMValueRef swizzle_index,
                          uint32_t name)
{
    swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;
    struct lp_build_tgsi_context *bld_base = (struct lp_build_tgsi_context*)bld;

    return iface->pBuilder->swr_tcs_llvm_fetch_output(tcs_iface, bld_base,
                                                      is_vindex_indirect,
                                                      vertex_index,
                                                      is_aindex_indirect,
                                                      attrib_index,
                                                      swizzle_index,
                                                      name);
}


static void
swr_tcs_llvm_emit_prologue(struct lp_build_context* bld)
{
   lp_build_tgsi_soa_context* bld_base = (lp_build_tgsi_soa_context*)bld;
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)bld_base->tcs_iface;
   iface->pBuilder->swr_tcs_llvm_emit_prologue(bld_base);
}

static void
swr_tcs_llvm_emit_epilogue(struct lp_build_context* bld)
{
   lp_build_tgsi_soa_context* bld_base = (lp_build_tgsi_soa_context*)bld;
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)bld_base->tcs_iface;
   iface->pBuilder->swr_tcs_llvm_emit_epilogue(bld_base);
}

static
void swr_tcs_llvm_store_output(const struct lp_build_tcs_iface *tcs_iface,
                         struct lp_build_context * bld,
                         unsigned name,
                         boolean is_vindex_indirect,
                         LLVMValueRef vertex_index,
                         boolean is_aindex_indirect,
                         LLVMValueRef attrib_index,
                         boolean is_sindex_indirect,
                         LLVMValueRef swizzle_index,
                         LLVMValueRef value,
                         LLVMValueRef mask_vec)
{
    swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;
    struct lp_build_tgsi_context *bld_base = (struct lp_build_tgsi_context*)bld;

    iface->pBuilder->swr_tcs_llvm_store_output(tcs_iface,
                                               bld_base,
                                               name,
                                               is_vindex_indirect,
                                               vertex_index,
                                               is_aindex_indirect,
                                               attrib_index,
                                               swizzle_index,
                                               value,
                                               mask_vec);
}


static
void swr_tcs_llvm_emit_barrier(struct lp_build_context *bld)
{
   lp_build_tgsi_soa_context* bld_base = (lp_build_tgsi_soa_context*)bld;
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)bld_base->tcs_iface;

   iface->pBuilder->swr_tcs_llvm_emit_barrier(bld_base->tcs_iface, &bld_base->bld_base);
}


static LLVMValueRef
swr_tes_llvm_fetch_vtx_input(const struct lp_build_tes_iface *tes_iface,
                             struct lp_build_context * bld,
                             boolean is_vindex_indirect,
                             LLVMValueRef vertex_index,
                             boolean is_aindex_indirect,
                             LLVMValueRef attrib_index,
                             boolean is_sindex_indirect,
                             LLVMValueRef swizzle_index)
{
    swr_tes_llvm_iface *iface = (swr_tes_llvm_iface*)tes_iface;
    struct lp_build_tgsi_context *bld_base = (struct lp_build_tgsi_context*)bld;

    return iface->pBuilder->swr_tes_llvm_fetch_vtx_input(tes_iface, bld_base,
                                                     is_vindex_indirect,
                                                     vertex_index,
                                                     is_aindex_indirect,
                                                     attrib_index,
                                                     swizzle_index);
}

static LLVMValueRef
swr_tes_llvm_fetch_patch_input(const struct lp_build_tes_iface *tes_iface,
                               struct lp_build_context * bld,
                               boolean is_aindex_indirect,
                               LLVMValueRef attrib_index,
                               LLVMValueRef swizzle_index)
{
    swr_tes_llvm_iface *iface = (swr_tes_llvm_iface*)tes_iface;
    struct lp_build_tgsi_context *bld_base = (struct lp_build_tgsi_context*)bld;

    return iface->pBuilder->swr_tes_llvm_fetch_patch_input(tes_iface, bld_base,
                                                     is_aindex_indirect,
                                                     attrib_index,
                                                     swizzle_index);
}

LLVMValueRef
BuilderSWR::swr_gs_llvm_fetch_input(const struct lp_build_gs_iface *gs_iface,
                           struct lp_build_context * bld,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_iface;
    Value *vert_index = unwrap(vertex_index);
    Value *attr_index = unwrap(attrib_index);

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    if (is_vindex_indirect || is_aindex_indirect) {
       int i;
       Value *res = unwrap(bld->zero);
       struct lp_type type = bld->type;

       for (i = 0; i < type.length; i++) {
          Value *vert_chan_index = vert_index;
          Value *attr_chan_index = attr_index;

          if (is_vindex_indirect) {
             vert_chan_index = VEXTRACT(vert_index, C(i));
          }
          if (is_aindex_indirect) {
             attr_chan_index = VEXTRACT(attr_index, C(i));
          }

          Value *attrib =
             LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_chan_index}));

          Value *pVertex = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pVerts});
          Value *pInputVertStride = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_inputVertStride});

          Value *pVector = ADD(MUL(vert_chan_index, pInputVertStride), attrib);
          Value *pInput = LOAD(GEP(pVertex, {pVector, unwrap(swizzle_index)}));

          Value *value = VEXTRACT(pInput, C(i));
          res = VINSERT(res, value, C(i));
       }

       return wrap(res);
    } else {
       Value *attrib = LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_index}));

       Value *pVertex = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pVerts});
       Value *pInputVertStride = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_inputVertStride});

       Value *pVector = ADD(MUL(vert_index, pInputVertStride), attrib);

       Value *pInput = LOAD(GEP(pVertex, {pVector, unwrap(swizzle_index)}));

       return wrap(pInput);
    }
}

// GS output stream layout
#define VERTEX_COUNT_SIZE 32
#define CONTROL_HEADER_SIZE (8*32)

void
BuilderSWR::swr_gs_llvm_emit_vertex(const struct lp_build_gs_iface *gs_base,
                           struct lp_build_context * bld,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec,
                           LLVMValueRef stream_id)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));
    const uint32_t headerSize = VERTEX_COUNT_SIZE + CONTROL_HEADER_SIZE;
    const uint32_t attribSize = 4 * sizeof(float);
    const uint32_t vertSize = attribSize * SWR_VTX_NUM_SLOTS;
    Value *pVertexOffset = MUL(unwrap(emitted_vertices_vec), VIMMED1(vertSize));

    Value *vMask = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_mask});
    Value *vMask1 = TRUNC(vMask, getVectorType(mInt1Ty, mVWidth));

    Value *pStack = STACKSAVE();
    Value *pTmpPtr = ALLOCA(mFP32Ty, C(4)); // used for dummy write for lane masking

    for (uint32_t attrib = 0; attrib < iface->num_outputs; ++attrib) {
       uint32_t attribSlot = attrib;
       uint32_t sgvChannel = 0;
       if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_PSIZE) {
          attribSlot = VERTEX_SGV_SLOT;
          sgvChannel = VERTEX_SGV_POINT_SIZE_COMP;
       } else if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_LAYER) {
          attribSlot = VERTEX_SGV_SLOT;
          sgvChannel = VERTEX_SGV_RTAI_COMP;
       } else if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_VIEWPORT_INDEX) {
          attribSlot = VERTEX_SGV_SLOT;
          sgvChannel = VERTEX_SGV_VAI_COMP;
       } else if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_POSITION) {
          attribSlot = VERTEX_POSITION_SLOT;
       } else {
          attribSlot = VERTEX_ATTRIB_START_SLOT + attrib;
          if (iface->info->writes_position) {
             attribSlot--;
          }
       }

       Value *pOutputOffset = ADD(pVertexOffset, VIMMED1(headerSize + attribSize * attribSlot)); // + sgvChannel ?

       for (uint32_t lane = 0; lane < mVWidth; ++lane) {
          Value *pLaneOffset = VEXTRACT(pOutputOffset, C(lane));
          Value *pStream = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pStreams, lane});
          Value *pStreamOffset = GEP(pStream, pLaneOffset);
          pStreamOffset = BITCAST(pStreamOffset, mFP32PtrTy);

          Value *pLaneMask = VEXTRACT(vMask1, C(lane));
          pStreamOffset = SELECT(pLaneMask, pStreamOffset, pTmpPtr);

          for (uint32_t channel = 0; channel < 4; ++channel) {
             Value *vData;

             if (attribSlot == VERTEX_SGV_SLOT)
                vData = LOAD(unwrap(outputs[attrib][0]));
             else
                vData = LOAD(unwrap(outputs[attrib][channel]));

             if (attribSlot != VERTEX_SGV_SLOT ||
                 sgvChannel == channel) {
                vData = VEXTRACT(vData, C(lane));
                STORE(vData, pStreamOffset);
             }
             pStreamOffset = GEP(pStreamOffset, C(1));
          }
       }
    }

    /* When the output type is not points, the geometry shader may not
     * output data to multiple streams. So early exit here.
     */
    if(iface->pGsState->outputTopology != TOP_POINT_LIST) {
        STACKRESTORE(pStack);
        return;
    }

    // Info about stream id for each vertex
    // is coded in 2 bits (4 vert per byte "box"):
    // ----------------- ----------------- ----
    // |d|d|c|c|b|b|a|a| |h|h|g|g|f|f|e|e| |...
    // ----------------- ----------------- ----

    // Calculate where need to put stream id for current vert
    // in 1 byte "box".
    Value *pShiftControl = MUL(unwrap(emitted_vertices_vec), VIMMED1(2));

    // Calculate in which box put stream id for current vert.
    Value *pOffsetControl = LSHR(unwrap(emitted_vertices_vec), VIMMED1(2));

    // Skip count header
    Value *pStreamIdOffset = ADD(pOffsetControl, VIMMED1(VERTEX_COUNT_SIZE));

    for (uint32_t lane = 0; lane < mVWidth; ++lane) {
       Value *pShift = TRUNC(VEXTRACT(pShiftControl, C(lane)), mInt8Ty);
       Value *pStream = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pStreams, lane});

       Value *pStreamOffset = GEP(pStream, VEXTRACT(pStreamIdOffset, C(lane)));

       // Just make sure that not overflow max - stream id = (0,1,2,3)
       Value *vVal = TRUNC(AND(VEXTRACT(unwrap(stream_id), C(0)), C(0x3)), mInt8Ty);

       // Shift it to correct position in byte "box"
       vVal = SHL(vVal, pShift);

       // Info about other vertices can be already stored
       // so we need to read and add bits from current vert info.
       Value *storedValue = LOAD(pStreamOffset);
       vVal = OR(storedValue, vVal);
       STORE(vVal, pStreamOffset);
    }

    STACKRESTORE(pStack);
}

void
BuilderSWR::swr_gs_llvm_end_primitive(const struct lp_build_gs_iface *gs_base,
                             struct lp_build_context * bld,
                             LLVMValueRef total_emitted_vertices_vec,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec,
                             LLVMValueRef mask_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    /* When the output type is points, the geometry shader may output data
     * to multiple streams, and end_primitive has no effect. Info about
     * stream id for vertices is stored into the same place in memory where
     * end primitive info is stored so early exit in this case.
     */
    if (iface->pGsState->outputTopology == TOP_POINT_LIST) {
        return;
    }

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    Value *vMask = LOAD(iface->pGsCtx, { 0, SWR_GS_CONTEXT_mask });
    Value *vMask1 = TRUNC(vMask, getVectorType(mInt1Ty, 8));

    uint32_t vertsPerPrim = iface->num_verts_per_prim;

    Value *vCount =
       ADD(MUL(unwrap(emitted_prims_vec), VIMMED1(vertsPerPrim)),
           unwrap(verts_per_prim_vec));

    vCount = unwrap(total_emitted_vertices_vec);

    Value *mask = unwrap(mask_vec);
    Value *cmpMask = VMASK(ICMP_NE(unwrap(verts_per_prim_vec), VIMMED1(0)));
    mask = AND(mask, cmpMask);
    vMask1 = TRUNC(mask, getVectorType(mInt1Ty, 8));

    vCount = SUB(vCount, VIMMED1(1));
    Value *vOffset = ADD(UDIV(vCount, VIMMED1(8)), VIMMED1(VERTEX_COUNT_SIZE));
    Value *vValue = SHL(VIMMED1(1), UREM(vCount, VIMMED1(8)));

    vValue = TRUNC(vValue, getVectorType(mInt8Ty, 8));

    Value *pStack = STACKSAVE();
    Value *pTmpPtr = ALLOCA(mInt8Ty, C(4)); // used for dummy read/write for lane masking

    for (uint32_t lane = 0; lane < mVWidth; ++lane) {
       Value *vLaneOffset = VEXTRACT(vOffset, C(lane));
       Value *pStream = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pStreams, lane});
       Value *pStreamOffset = GEP(pStream, vLaneOffset);

       Value *pLaneMask = VEXTRACT(vMask1, C(lane));
       pStreamOffset = SELECT(pLaneMask, pStreamOffset, pTmpPtr);

       Value *vVal = LOAD(pStreamOffset);
       vVal = OR(vVal, VEXTRACT(vValue, C(lane)));
       STORE(vVal, pStreamOffset);
    }

    STACKRESTORE(pStack);
}

void
BuilderSWR::swr_gs_llvm_epilogue(const struct lp_build_gs_iface *gs_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec, unsigned stream)
{
   swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   // Store emit count to each output stream in the first DWORD
   for (uint32_t lane = 0; lane < mVWidth; ++lane)
   {
      Value* pStream = LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pStreams, lane});
      pStream = BITCAST(pStream, mInt32PtrTy);
      Value* pLaneCount = VEXTRACT(unwrap(total_emitted_vertices_vec), C(lane));
      STORE(pLaneCount, pStream);
   }
}

void
BuilderSWR::swr_tcs_llvm_emit_prologue(struct lp_build_tgsi_soa_context* bld)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)bld->tcs_iface;

   Value* loop_var = ALLOCA(mSimdInt32Ty);
   STORE(VBROADCAST(C(0)), loop_var);

   iface->loop_var = wrap(loop_var);

   lp_exec_bgnloop(&bld->exec_mask, true);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));
   bld->system_values.invocation_id = wrap((LOAD(unwrap(iface->loop_var))));

   if (verbose_tcs_shader_loop) {
      lp_build_print_value(gallivm, "Prologue LOOP Iteration BEGIN:", bld->system_values.invocation_id);
   }

}

void
BuilderSWR::swr_tcs_llvm_emit_epilogue(struct lp_build_tgsi_soa_context* bld)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)bld->tcs_iface;

   struct lp_build_context *uint_bld = &bld->bld_base.uint_bld;

   STORE(ADD(LOAD(unwrap(iface->loop_var)), VBROADCAST(C(1))), unwrap(iface->loop_var));
   if (verbose_tcs_shader_loop) {
      lp_build_print_value(gallivm, "Epilogue LOOP: ", wrap(LOAD(unwrap(iface->loop_var))));
   }

   LLVMValueRef tmp = lp_build_cmp(uint_bld, PIPE_FUNC_GEQUAL, wrap(LOAD(unwrap(iface->loop_var))),
                                   wrap(VBROADCAST(C(iface->output_vertices))));
   lp_exec_mask_cond_push(&bld->exec_mask, tmp);
   lp_exec_break(&bld->exec_mask, &bld->bld_base.pc, false);
   lp_exec_mask_cond_pop(&bld->exec_mask);
   lp_exec_endloop(bld->bld_base.base.gallivm, &bld->exec_mask);
}

LLVMValueRef
BuilderSWR::swr_tcs_llvm_fetch_input(const struct lp_build_tcs_iface *tcs_iface,
                                     struct lp_build_tgsi_context * bld_base,
                                     boolean is_vindex_indirect,
                                     LLVMValueRef vertex_index,
                                     boolean is_aindex_indirect,
                                     LLVMValueRef attrib_index,
                                     LLVMValueRef swizzle_index)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;

   Value *vert_index = unwrap(vertex_index);
   Value *attr_index = unwrap(attrib_index);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   if (verbose_tcs_shader_in) {
      lp_build_printf(gallivm, "[TCS IN][VTX] ======================================\n");
      lp_build_print_value(gallivm, "[TCS IN][VTX] vertex_index: ", vertex_index);
      lp_build_print_value(gallivm, "[TCS IN][VTX] attrib_index: ", attrib_index);
      lp_build_printf(gallivm, "[TCS IN][VTX] --------------------------------------\n");
   }

   Value *res = unwrap(bld_base->base.zero);
   if (is_vindex_indirect || is_aindex_indirect) {
      int i;
      struct lp_type type = bld_base->base.type;

      for (i = 0; i < type.length; i++) {
         Value *vert_chan_index = vert_index;
         Value *attr_chan_index = attr_index;

         if (is_vindex_indirect) {
            vert_chan_index = VEXTRACT(vert_index, C(i));
         }
         if (is_aindex_indirect) {
            attr_chan_index = VEXTRACT(attr_index, C(i));
         }

         Value *attrib =
            LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_chan_index}));

         Value *pBase = GEP(iface->pTcsCtx,
                        { C(0), C(SWR_HS_CONTEXT_vert), vert_chan_index,
                        C(simdvertex_attrib), attrib, unwrap(swizzle_index), C(i) });

         Value *val = LOAD(pBase);

         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS IN][VTX] vert_chan_index: ", wrap(vert_chan_index));
            lp_build_print_value(gallivm, "[TCS IN][VTX] attrib_index: ", attrib_index);
            lp_build_print_value(gallivm, "[TCS IN][VTX] attr_chan_index: ", wrap(attr_index));
            lp_build_print_value(gallivm, "[TCS IN][VTX] attrib read from map: ", wrap(attrib));
            lp_build_print_value(gallivm, "[TCS IN][VTX] swizzle_index: ", swizzle_index);
            lp_build_print_value(gallivm, "[TCS IN][VTX] Loaded: ", wrap(val));
         }
         res = VINSERT(res, val, C(i));
      }
   } else {
      Value *attrib = LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_index}));

      Value *pBase = GEP(iface->pTcsCtx,
                        { C(0), C(SWR_HS_CONTEXT_vert), vert_index,
                        C(simdvertex_attrib), attrib, unwrap(swizzle_index) });

      res = LOAD(pBase);

      if (verbose_tcs_shader_in) {
         lp_build_print_value(gallivm, "[TCS IN][VTX] attrib_index: ", attrib_index);
         lp_build_print_value(gallivm, "[TCS IN][VTX] attr_chan_index: ", wrap(attr_index));
         lp_build_print_value(gallivm, "[TCS IN][VTX] attrib read from map: ", wrap(attrib));
         lp_build_print_value(gallivm, "[TCS IN][VTX] swizzle_index: ", swizzle_index);
         lp_build_print_value(gallivm, "[TCS IN][VTX] Loaded: ", wrap(res));
      }
   }
   if (verbose_tcs_shader_in) {
      lp_build_print_value(gallivm, "[TCS IN][VTX] returning: ", wrap(res));
   }
   return wrap(res);
}

LLVMValueRef
BuilderSWR::swr_tcs_llvm_fetch_output(const struct lp_build_tcs_iface *tcs_iface,
                                      struct lp_build_tgsi_context * bld_base,
                                      boolean is_vindex_indirect,
                                      LLVMValueRef vertex_index,
                                      boolean is_aindex_indirect,
                                      LLVMValueRef attrib_index,
                                      LLVMValueRef swizzle_index,
                                      uint32_t name)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;

   Value *vert_index = unwrap(vertex_index);
   Value *attr_index = unwrap(attrib_index);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   if (verbose_tcs_shader_in) {
      lp_build_print_value(gallivm, "[TCS INOUT] Vertex index: ", vertex_index);
      lp_build_print_value(gallivm, "[TCS INOUT] Attrib index: ", wrap(attr_index));
      lp_build_print_value(gallivm, "[TCS INOUT] Swizzle index: ", swizzle_index);
   }

   Value* res = unwrap(bld_base->base.zero);

   for (uint32_t lane = 0; lane < mVWidth; lane++) {
      Value* p1 = LOAD(iface->pTcsCtx, {0, SWR_HS_CONTEXT_pCPout});
      Value* pCpOut = GEP(p1, {lane});

      Value *vert_chan_index = vert_index;
      Value *attr_chan_index = attr_index;

      if (is_vindex_indirect) {
         vert_chan_index = VEXTRACT(vert_index, C(lane));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT] Extracted vertex index: ", wrap(vert_chan_index));
         }
      }

      if (is_aindex_indirect) {
         attr_chan_index = VEXTRACT(attr_index, C(lane));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT] Extracted attrib index: ", wrap(attr_chan_index));
         }
      }

      if (name == TGSI_SEMANTIC_TESSOUTER || name == TGSI_SEMANTIC_TESSINNER) {
         Value* tessFactors = GEP(pCpOut, {(uint32_t)0, ScalarPatch_tessFactors});
         Value* tessFactorArray = nullptr;
         if (name == TGSI_SEMANTIC_TESSOUTER) {
            tessFactorArray = GEP(tessFactors, {(uint32_t)0, SWR_TESSELLATION_FACTORS_OuterTessFactors});
         } else {
            tessFactorArray = GEP(tessFactors, {(uint32_t)0, SWR_TESSELLATION_FACTORS_InnerTessFactors});
         }
         Value* tessFactor = GEP(tessFactorArray, {C(0), unwrap(swizzle_index)});
         res = VINSERT(res, LOAD(tessFactor), C(lane));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT][FACTOR] lane (patch-id): ", wrap(C(lane)));
            lp_build_print_value(gallivm, "[TCS INOUT][FACTOR] loaded value: ", wrap(res));
         }
      } else if (name == TGSI_SEMANTIC_PATCH) {
         Value* attr_index_from_map = LOAD(GEP(iface->pPatchOutputAttribMap, {C(0), attr_chan_index}));
         Value* attr_value = GEP(pCpOut, {C(0), C(ScalarPatch_patchData), C(ScalarCPoint_attrib), attr_index_from_map, unwrap(swizzle_index)});
         res = VINSERT(res, LOAD(attr_value), C(lane));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT][PATCH] attr index loaded from map: ", wrap(attr_index_from_map));
            lp_build_print_value(gallivm, "[TCS INOUT][PATCH] lane (patch-id): ", wrap(C(lane)));
            lp_build_print_value(gallivm, "[TCS INOUT][PATCH] loaded value: ", wrap(res));
         }
      } else {
         // Generic attribute
         Value *attrib =
             LOAD(GEP(iface->pVtxOutputAttribMap, {C(0), attr_chan_index}));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT][VTX] Attrib index from map: ", wrap(attrib));
         }
         Value* attr_chan = GEP(pCpOut, {C(0), C(ScalarPatch_cp), vert_chan_index,
                                    C(ScalarCPoint_attrib), attrib, unwrap(swizzle_index)});

         res = VINSERT(res, LOAD(attr_chan), C(lane));
         if (verbose_tcs_shader_in) {
            lp_build_print_value(gallivm, "[TCS INOUT][VTX] loaded value: ", wrap(res));
         }
      }
   }

   return wrap(res);
}

void
BuilderSWR::swr_tcs_llvm_store_output(const struct lp_build_tcs_iface *tcs_iface,
                                      struct lp_build_tgsi_context *bld_base,
                                      unsigned name,
                                      boolean is_vindex_indirect,
                                      LLVMValueRef vertex_index,
                                      boolean is_aindex_indirect,
                                      LLVMValueRef attrib_index,
                                      LLVMValueRef swizzle_index,
                                      LLVMValueRef value,
                                      LLVMValueRef mask_vec)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;
   struct lp_build_tgsi_soa_context* bld = (struct lp_build_tgsi_soa_context*)bld_base;

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    if (verbose_tcs_shader_out) {
      lp_build_printf(gallivm, "[TCS OUT] =============================================\n");
    }

   if (verbose_tcs_shader_out) {
      lp_build_print_value(gallivm, "[TCS OUT] Store mask: ", bld->exec_mask.exec_mask);
      lp_build_print_value(gallivm, "[TCS OUT] Store value: ", value);
   }

   Value *vert_index = unwrap(vertex_index);
   Value *attr_index = unwrap(attrib_index);

   if (verbose_tcs_shader_out) {
      lp_build_print_value(gallivm, "[TCS OUT] Vertex index: ", vertex_index);
      lp_build_print_value(gallivm, "[TCS OUT] Attrib index: ", wrap(attr_index));
      lp_build_print_value(gallivm, "[TCS OUT] Swizzle index: ", swizzle_index);
   }

   if (is_vindex_indirect) {
      vert_index = VEXTRACT(vert_index, C(0));
      if (verbose_tcs_shader_out) {
         lp_build_print_value(gallivm, "[TCS OUT] Extracted vertex index: ", vertex_index);
      }
   }

   if (is_aindex_indirect) {
      attr_index = VEXTRACT(attr_index, C(0));
      if (verbose_tcs_shader_out) {
         lp_build_print_value(gallivm, "[TCS OUT] Extracted attrib index: ", wrap(attr_index));
      }
   }

   if (verbose_tcs_shader_out) {
      if (bld->exec_mask.has_mask) {
         lp_build_print_value(gallivm, "[TCS OUT] Exec mask: ", bld->exec_mask.exec_mask);
      }
      else {
         lp_build_printf(gallivm, "[TCS OUT] has no mask\n");
      }
   }
   for (uint32_t lane = 0; lane < mVWidth; lane++) {
      Value* p1 = LOAD(iface->pTcsCtx, {0, SWR_HS_CONTEXT_pCPout});
      Value* pCpOut = GEP(p1, {lane});

      if (name == TGSI_SEMANTIC_TESSOUTER || name == TGSI_SEMANTIC_TESSINNER) {
         Value* tessFactors = GEP(pCpOut, {(uint32_t)0, ScalarPatch_tessFactors});
         Value* tessFactorArray = nullptr;
         if (name == TGSI_SEMANTIC_TESSOUTER) {
            tessFactorArray = GEP(tessFactors, {(uint32_t)0, SWR_TESSELLATION_FACTORS_OuterTessFactors});
         } else {
            tessFactorArray = GEP(tessFactors, {(uint32_t)0, SWR_TESSELLATION_FACTORS_InnerTessFactors});
         }
         Value* tessFactor = GEP(tessFactorArray, {C(0), unwrap(swizzle_index)});
         Value* valueToStore = VEXTRACT(unwrap(value), C(lane));
         valueToStore = BITCAST(valueToStore, mFP32Ty);
         if (mask_vec) {
            Value *originalVal = LOAD(tessFactor);
            Value *vMask = TRUNC(VEXTRACT(unwrap(mask_vec), C(lane)), mInt1Ty);
            valueToStore = SELECT(vMask, valueToStore, originalVal);
         }
         STORE(valueToStore, tessFactor);
         if (verbose_tcs_shader_out)
         {
            lp_build_print_value(gallivm, "[TCS OUT][FACTOR] Mask_vec mask: ", mask_vec);
            lp_build_print_value(gallivm, "[TCS OUT][FACTOR] Stored value: ", wrap(valueToStore));
         }
      } else if (name == TGSI_SEMANTIC_PATCH) {
         Value* attrib = LOAD(GEP(iface->pPatchOutputAttribMap, {C(0), attr_index}));
         if (verbose_tcs_shader_out) {
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] vert_index: ", wrap(vert_index));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] attr_index: ", wrap(attr_index));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] vert_index_indirect: ", wrap(C(is_vindex_indirect)));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] attr_index_indirect: ", wrap(C(is_aindex_indirect)));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] attr index loaded from map: ", wrap(attrib));
         }
         Value* attr = GEP(pCpOut, {C(0), C(ScalarPatch_patchData), C(ScalarCPoint_attrib), attrib});
         Value* value_to_store = VEXTRACT(unwrap(value), C(lane));
         if (verbose_tcs_shader_out) {
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] lane (patch-id): ", wrap(C(lane)));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] value to store: ", value);
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] per-patch value to store: ", wrap(value_to_store));
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] chan_index: ", swizzle_index);
         }
         value_to_store = BITCAST(value_to_store, mFP32Ty);
         if (mask_vec) {
            Value *originalVal = LOADV(attr, {C(0), unwrap(swizzle_index)});
            Value *vMask = TRUNC(VEXTRACT(unwrap(mask_vec), C(lane)), mInt1Ty);
            value_to_store = SELECT(vMask, value_to_store, originalVal);
            if (verbose_tcs_shader_out) {
               lp_build_print_value(gallivm, "[TCS OUT][PATCH] store mask: ", mask_vec);
               lp_build_print_value(gallivm, "[TCS OUT][PATCH] loaded original value: ", wrap(originalVal));
               lp_build_print_value(gallivm, "[TCS OUT][PATCH] vMask: ", wrap(vMask));
               lp_build_print_value(gallivm, "[TCS OUT][PATCH] selected value to store: ", wrap(value_to_store));
            }
         }
         STOREV(value_to_store, attr, {C(0), unwrap(swizzle_index)});
         if (verbose_tcs_shader_out) {
            lp_build_print_value(gallivm, "[TCS OUT][PATCH] stored value: ", wrap(value_to_store));
         }
      } else {
         Value* value_to_store = VEXTRACT(unwrap(value), C(lane));
         Value* attrib = LOAD(GEP(iface->pVtxOutputAttribMap, {C(0), attr_index}));

         if (verbose_tcs_shader_out) {
            lp_build_printf(gallivm, "[TCS OUT] Writting attribute\n");
            lp_build_print_value(gallivm, "[TCS OUT][VTX] invocation_id: ", bld->system_values.invocation_id);
            lp_build_print_value(gallivm, "[TCS OUT][VTX] attribIndex: ", wrap(attr_index));
            lp_build_print_value(gallivm, "[TCS OUT][VTX] attrib read from map: ", wrap(attrib));
            lp_build_print_value(gallivm, "[TCS OUT][VTX] chan_index: ", swizzle_index);
            lp_build_print_value(gallivm, "[TCS OUT][VTX] value: ", value);
            lp_build_print_value(gallivm, "[TCS OUT][VTX] value_to_store: ", wrap(value_to_store));
         }

         Value* attr_chan = GEP(pCpOut, {C(0), C(ScalarPatch_cp),
                                    VEXTRACT(unwrap(bld->system_values.invocation_id), C(0)),
                                    C(ScalarCPoint_attrib), attrib, unwrap(swizzle_index)});

         // Mask output values if needed
         value_to_store = BITCAST(value_to_store, mFP32Ty);
         if (mask_vec) {
            Value *originalVal = LOAD(attr_chan);
            Value *vMask = TRUNC(VEXTRACT(unwrap(mask_vec), C(lane)), mInt1Ty);
            value_to_store = SELECT(vMask, value_to_store, originalVal);
         }
         STORE(value_to_store, attr_chan);
         if (verbose_tcs_shader_out) {
            lp_build_print_value(gallivm, "[TCS OUT][VTX] Mask_vec mask: ", mask_vec);
            lp_build_print_value(gallivm, "[TCS OUT][VTX] stored: ", wrap(value_to_store));
         }
      }
   }
}

void
BuilderSWR::swr_tcs_llvm_emit_barrier(const struct lp_build_tcs_iface *tcs_iface,
                                      struct lp_build_tgsi_context *bld_base)
{
   swr_tcs_llvm_iface *iface = (swr_tcs_llvm_iface*)tcs_iface;
   struct lp_build_tgsi_soa_context* bld = (struct lp_build_tgsi_soa_context*)bld_base;

   if (verbose_tcs_shader_loop) {
      lp_build_print_value(gallivm, "Barrier LOOP: Iteration %d END\n", iface->loop_var);
   }

   struct lp_build_context *uint_bld = &bld->bld_base.uint_bld;

   STORE(ADD(LOAD(unwrap(iface->loop_var)), VBROADCAST(C(1))), unwrap(iface->loop_var));

   LLVMValueRef tmp = lp_build_cmp(uint_bld, PIPE_FUNC_GEQUAL, wrap(LOAD(unwrap(iface->loop_var))),
                                   wrap(VBROADCAST(C(iface->output_vertices))));

   lp_exec_mask_cond_push(&bld->exec_mask, tmp);
   lp_exec_break(&bld->exec_mask, &bld->bld_base.pc, false);
   lp_exec_mask_cond_pop(&bld->exec_mask);
   lp_exec_endloop(bld->bld_base.base.gallivm, &bld->exec_mask);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   STORE(VBROADCAST(C(0)), unwrap(iface->loop_var));
   lp_exec_bgnloop(&bld->exec_mask, true);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   bld->system_values.invocation_id = wrap((LOAD(unwrap(iface->loop_var))));

   if (verbose_tcs_shader_loop) {
      lp_build_print_value(gallivm, "Barrier LOOP: Iteration BEGIN: ", iface->loop_var);
      lp_build_print_value(gallivm, "Barrier LOOP: InvocationId: \n", bld->system_values.invocation_id);
   }
}


LLVMValueRef
BuilderSWR::swr_tes_llvm_fetch_patch_input(const struct lp_build_tes_iface *tes_iface,
                                     struct lp_build_tgsi_context * bld_base,
                                     boolean is_aindex_indirect,
                                     LLVMValueRef attrib_index,
                                     LLVMValueRef swizzle_index)
{
    swr_tes_llvm_iface *iface = (swr_tes_llvm_iface*)tes_iface;
    Value *attr_index = unwrap(attrib_index);
    Value *res = unwrap(bld_base->base.zero);

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   if (verbose_shader) {
      lp_build_printf(gallivm, "[TES IN][PATCH] --------------------------------------\n");
   }

    if (is_aindex_indirect) {
       int i;
       struct lp_type type = bld_base->base.type;

       for (i = 0; i < type.length; i++) {
          Value *attr_chan_index = attr_index;

          if (is_aindex_indirect) {
             attr_chan_index = VEXTRACT(attr_index, C(i));
          }

          Value *attrib =
             LOAD(GEP(iface->pPatchAttribMap, {C(0), attr_chan_index}));

          Value *pCpIn = LOAD(iface->pTesCtx, {0, SWR_DS_CONTEXT_pCpIn}, "pCpIn");
          Value *pPatchData = GEP(pCpIn, {(uint32_t)0, ScalarPatch_patchData});
          Value *pAttr = GEP(pPatchData, {(uint32_t)0, ScalarCPoint_attrib});
          Value *Val = LOADV(pAttr, {C(0), attrib, unwrap(swizzle_index)});
          if (verbose_shader) {
            lp_build_print_value(gallivm, "[TES IN][PATCH] attrib_index: ", attrib_index);
            lp_build_print_value(gallivm, "[TES IN][PATCH] attr_chan_index: ", wrap(attr_chan_index));
            lp_build_print_value(gallivm, "[TES IN][PATCH] attrib read from map: ", wrap(attrib));
            lp_build_print_value(gallivm, "[TES IN][PATCH] swizzle_index: ", swizzle_index);
            lp_build_print_value(gallivm, "[TES IN][PATCH] Loaded: ", wrap(Val));
          }
          res = VINSERT(res, Val, C(i));
       }
    } else {
      Value *attrib = LOAD(GEP(iface->pPatchAttribMap, {C(0), attr_index}));

      Value *pCpIn = LOAD(iface->pTesCtx, {(uint32_t)0, SWR_DS_CONTEXT_pCpIn}, "pCpIn");
      Value *pPatchData = GEP(pCpIn, {(uint32_t)0, ScalarPatch_patchData});
      Value *pAttr = GEP(pPatchData, {(uint32_t)0, ScalarCPoint_attrib});
      Value *Val = LOADV(pAttr, {C(0), attrib, unwrap(swizzle_index)});
      if (verbose_shader) {
         lp_build_print_value(gallivm, "[TES IN][PATCH] attrib_index: ", attrib_index);
         lp_build_print_value(gallivm, "[TES IN][PATCH] attr_chan_index: ", wrap(attr_index));
         lp_build_print_value(gallivm, "[TES IN][PATCH] attrib read from map: ", wrap(attrib));
         lp_build_print_value(gallivm, "[TES IN][PATCH] swizzle_index: ", swizzle_index);
         lp_build_print_value(gallivm, "[TES IN][PATCH] Loaded: ", wrap(Val));
      }
      res = VBROADCAST(Val);
    }
    if (verbose_shader) {
       lp_build_print_value(gallivm, "[TES IN][PATCH] returning: ", wrap(res));
    }
    return wrap(res);
}



LLVMValueRef
BuilderSWR::swr_tes_llvm_fetch_vtx_input(const struct lp_build_tes_iface *tes_iface,
                                     struct lp_build_tgsi_context * bld_base,
                                     boolean is_vindex_indirect,
                                     LLVMValueRef vertex_index,
                                     boolean is_aindex_indirect,
                                     LLVMValueRef attrib_index,
                                     LLVMValueRef swizzle_index)
{
    swr_tes_llvm_iface *iface = (swr_tes_llvm_iface*)tes_iface;
    Value *vert_index = unwrap(vertex_index);
    Value *attr_index = unwrap(attrib_index);

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    if (verbose_shader) {
      lp_build_printf(gallivm, "[TES IN][VTX] --------------------------------------\n");
    }

    Value *res = unwrap(bld_base->base.zero);
    if (is_vindex_indirect || is_aindex_indirect) {
       int i;
       struct lp_type type = bld_base->base.type;

       for (i = 0; i < type.length; i++) {
          Value *vert_chan_index = vert_index;
          Value *attr_chan_index = attr_index;

          if (is_vindex_indirect) {
             vert_chan_index = VEXTRACT(vert_index, C(i));
          }
          if (is_aindex_indirect) {
             attr_chan_index = VEXTRACT(attr_index, C(i));
          }

          Value *attrib =
             LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_chan_index}));

          Value *pCpIn = LOAD(iface->pTesCtx, {0, SWR_DS_CONTEXT_pCpIn}, "pCpIn");
          Value *pCp = GEP(pCpIn, {0, ScalarPatch_cp});
          Value *pVertex = GEP(pCp, {(Value*)C(0), vert_chan_index});
          Value *pAttrTab = GEP(pVertex, {uint32_t(0), uint32_t(0)});
          Value *pAttr = GEP(pAttrTab, {(Value*)C(0), attrib});
          Value *Val = LOADV(pAttr, {C(0), unwrap(swizzle_index)});
          if (verbose_shader) {
             lp_build_print_value(gallivm, "[TES IN][VTX] attrib_index: ", attrib_index);
             lp_build_print_value(gallivm, "[TES IN][VTX] attr_chan_index: ", wrap(attr_index));
             lp_build_print_value(gallivm, "[TES IN][VTX] attrib read from map: ", wrap(attrib));
             lp_build_print_value(gallivm, "[TES IN][VTX] swizzle_index: ", swizzle_index);
             lp_build_print_value(gallivm, "[TES IN][VTX] Loaded: ", wrap(Val));
          }
          res = VINSERT(res, Val, C(i));
       }
    } else {
      Value *attrib = LOAD(GEP(iface->pVtxAttribMap, {C(0), attr_index}));

      Value *pCpIn = LOAD(iface->pTesCtx, {0, SWR_DS_CONTEXT_pCpIn}, "pCpIn");
      Value *pCp = GEP(pCpIn, {0, ScalarPatch_cp});
      Value *pVertex = GEP(pCp, {(Value*)C(0), vert_index});
      Value *pAttrTab = GEP(pVertex, {uint32_t(0), uint32_t(0)});
      Value *pAttr = GEP(pAttrTab, {(Value*)C(0), attrib});
      Value *Val = LOADV(pAttr, {C(0), unwrap(swizzle_index)});
      if (verbose_shader) {
         lp_build_print_value(gallivm, "[TES IN][VTX] attrib_index: ", attrib_index);
         lp_build_print_value(gallivm, "[TES IN][VTX] attr_chan_index: ", wrap(attr_index));
         lp_build_print_value(gallivm, "[TES IN][VTX] attrib read from map: ", wrap(attrib));
         lp_build_print_value(gallivm, "[TES IN][VTX] swizzle_index: ", swizzle_index);
         lp_build_print_value(gallivm, "[TES IN][VTX] Loaded: ", wrap(Val));
      }
      res = VBROADCAST(Val);
    }
    if (verbose_shader) {
       lp_build_print_value(gallivm, "[TES IN][VTX] returning: ", wrap(res));
    }
    return wrap(res);
}




PFN_GS_FUNC
BuilderSWR::CompileGS(struct swr_context *ctx, swr_jit_gs_key &key)
{
   SWR_GS_STATE *pGS = &ctx->gs->gsState;
   struct tgsi_shader_info *info = &ctx->gs->info.base;

   memset(pGS, 0, sizeof(*pGS));

   pGS->gsEnable = true;

   pGS->numInputAttribs = (VERTEX_ATTRIB_START_SLOT - VERTEX_POSITION_SLOT) + info->num_inputs;
   pGS->outputTopology =
      swr_convert_prim_topology(info->properties[TGSI_PROPERTY_GS_OUTPUT_PRIM], 0);

   /* It's +1 because emit_vertex in swr is always called exactly one time more
    * than max_vertices passed in Geometry Shader. We need to allocate more memory
    * to avoid crash/memory overwritten.
    */
   pGS->maxNumVerts = info->properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES] + 1;
   pGS->instanceCount = info->properties[TGSI_PROPERTY_GS_INVOCATIONS];

   // If point primitive then assume to use multiple streams
   if(pGS->outputTopology == TOP_POINT_LIST) {
      pGS->isSingleStream = false;
   } else {
      pGS->isSingleStream = true;
      pGS->singleStreamID = 0;
   }

   pGS->vertexAttribOffset = VERTEX_POSITION_SLOT;
   pGS->inputVertStride = pGS->numInputAttribs + pGS->vertexAttribOffset;
   pGS->outputVertexSize = SWR_VTX_NUM_SLOTS;
   pGS->controlDataSize = 8; // GS outputs max of 8 32B units
   pGS->controlDataOffset = VERTEX_COUNT_SIZE;
   pGS->outputVertexOffset = pGS->controlDataOffset + CONTROL_HEADER_SIZE;

   pGS->allocationSize =
      VERTEX_COUNT_SIZE + // vertex count
      CONTROL_HEADER_SIZE + // control header
      (SWR_VTX_NUM_SLOTS * 16) * // sizeof vertex
      pGS->maxNumVerts; // num verts

   struct swr_geometry_shader *gs = ctx->gs;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));

   std::vector<Type *> gsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(mInt8Ty, 0),
                              PointerType::get(Gen_SWR_GS_CONTEXT(JM()), 0)};
   FunctionType *vsFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), gsArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(vsFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "GS",
                                     JM()->mpCurrentModule);
#if LLVM_VERSION_MAJOR < 5
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);
#else
   pFunction->addAttributes(AttributeList::FunctionIndex, attrBuilder);
#endif

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pWorkerData = &*argitr++;
   pWorkerData->setName("pWorkerData");
   Value *pGsCtx = &*argitr++;
   pGsCtx->setName("gsCtx");

   Value *consts_ptr =
      GEP(hPrivateData, {C(0), C(swr_draw_context_constantGS)});
   consts_ptr->setName("gs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsGS});
   const_sizes_ptr->setName("num_gs_constants");

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_GEOMETRY);
   assert(sampler != nullptr);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));
   system_values.prim_id = wrap(LOAD(pGsCtx, {0, SWR_GS_CONTEXT_PrimitiveID}));
   system_values.invocation_id = wrap(LOAD(pGsCtx, {0, SWR_GS_CONTEXT_InstanceID}));

   std::vector<Constant*> mapConstants;
   Value *vtxAttribMap = ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   for (unsigned slot = 0; slot < info->num_inputs; slot++) {
      ubyte semantic_name = info->input_semantic_name[slot];
      ubyte semantic_idx = info->input_semantic_index[slot];

      unsigned vs_slot = locate_linkage(semantic_name, semantic_idx, &ctx->vs->info.base);
      assert(vs_slot < PIPE_MAX_SHADER_OUTPUTS);

      vs_slot += VERTEX_ATTRIB_START_SLOT;

      if (ctx->vs->info.base.output_semantic_name[0] == TGSI_SEMANTIC_POSITION)
         vs_slot--;

      if (semantic_name == TGSI_SEMANTIC_POSITION)
         vs_slot = VERTEX_POSITION_SLOT;

      STORE(C(vs_slot), vtxAttribMap, {0, slot});
      mapConstants.push_back(C(vs_slot));
   }

   struct lp_build_mask_context mask;
   Value *mask_val = LOAD(pGsCtx, {0, SWR_GS_CONTEXT_mask}, "gsMask");
   lp_build_mask_begin(&mask, gallivm,
                       lp_type_float_vec(32, 32 * 8), wrap(mask_val));

   // zero out cut buffer so we can load/modify/store bits
   for (uint32_t lane = 0; lane < mVWidth; ++lane)
   {
      Value* pStream = LOAD(pGsCtx, {0, SWR_GS_CONTEXT_pStreams, lane});
#if LLVM_VERSION_MAJOR >= 10
      MEMSET(pStream, C((char)0), VERTEX_COUNT_SIZE + CONTROL_HEADER_SIZE, MaybeAlign(sizeof(float) * KNOB_SIMD_WIDTH));
#else
      MEMSET(pStream, C((char)0), VERTEX_COUNT_SIZE + CONTROL_HEADER_SIZE, sizeof(float) * KNOB_SIMD_WIDTH);
#endif
   }

   struct swr_gs_llvm_iface gs_iface;
   gs_iface.base.fetch_input = ::swr_gs_llvm_fetch_input;
   gs_iface.base.emit_vertex = ::swr_gs_llvm_emit_vertex;
   gs_iface.base.end_primitive = ::swr_gs_llvm_end_primitive;
   gs_iface.base.gs_epilogue = ::swr_gs_llvm_epilogue;
   gs_iface.pBuilder = this;
   gs_iface.pGsCtx = pGsCtx;
   gs_iface.pGsState = pGS;
   gs_iface.num_outputs = gs->info.base.num_outputs;
   gs_iface.num_verts_per_prim =
      u_vertices_per_prim((pipe_prim_type)info->properties[TGSI_PROPERTY_GS_OUTPUT_PRIM]);
   gs_iface.info = info;
   gs_iface.pVtxAttribMap = vtxAttribMap;

   struct lp_build_tgsi_params params;
   memset(&params, 0, sizeof(params));
   params.type = lp_type_float_vec(32, 32 * 8);
   params.mask = & mask;
   params.consts_ptr = wrap(consts_ptr);
   params.const_sizes_ptr = wrap(const_sizes_ptr);
   params.system_values = &system_values;
   params.inputs = inputs;
   params.context_ptr = wrap(hPrivateData);
   params.sampler = sampler;
   params.info = &gs->info.base;
   params.gs_iface = &gs_iface.base;

   lp_build_tgsi_soa(gallivm,
                     gs->pipe.tokens,
                     &params,
                     outputs);

   lp_build_mask_end(&mask);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   RET_VOID();

   gallivm_verify_function(gallivm, wrap(pFunction));
   gallivm_compile_module(gallivm);

   PFN_GS_FUNC pFunc =
      (PFN_GS_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   debug_printf("geom shader  %p\n", pFunc);
   assert(pFunc && "Error: GeomShader = NULL");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}

PFN_TES_FUNC
BuilderSWR::CompileTES(struct swr_context *ctx, swr_jit_tes_key &key)
{
   SWR_TS_STATE *pTS = &ctx->tsState;
   struct tgsi_shader_info *info = &ctx->tes->info.base;

   // tessellation is enabled if TES is present
   // clear tessellation state here then
   memset(pTS, 0, sizeof(*pTS));

   pTS->tsEnable = true;

   unsigned tes_prim_mode = info->properties[TGSI_PROPERTY_TES_PRIM_MODE];
   unsigned tes_spacing = info->properties[TGSI_PROPERTY_TES_SPACING];
   bool tes_vertex_order_cw = info->properties[TGSI_PROPERTY_TES_VERTEX_ORDER_CW];
   bool tes_point_mode = info->properties[TGSI_PROPERTY_TES_POINT_MODE];
   SWR_TS_DOMAIN type = SWR_TS_ISOLINE;
   SWR_TS_PARTITIONING partitioning = SWR_TS_EVEN_FRACTIONAL;
   SWR_TS_OUTPUT_TOPOLOGY topology = SWR_TS_OUTPUT_POINT;
   PRIMITIVE_TOPOLOGY postDSTopology = TOP_POINT_LIST;

   // TESS_TODO: move this to helper functions to improve readability
   switch (tes_prim_mode) {
   case PIPE_PRIM_LINES:
      type = SWR_TS_ISOLINE;
      postDSTopology = TOP_LINE_LIST;
      break;
   case PIPE_PRIM_TRIANGLES:
      type = SWR_TS_TRI;
      postDSTopology = TOP_TRIANGLE_LIST;
      break;
   case PIPE_PRIM_QUADS:
      type = SWR_TS_QUAD;
      // See OpenGL spec - quads are tessellated into triangles
      postDSTopology = TOP_TRIANGLE_LIST;
      break;
   default:
      assert(0);
   }

   switch (tes_spacing) {
   case PIPE_TESS_SPACING_FRACTIONAL_ODD:
      partitioning = SWR_TS_ODD_FRACTIONAL;
      break;
   case PIPE_TESS_SPACING_FRACTIONAL_EVEN:
      partitioning = SWR_TS_EVEN_FRACTIONAL;
      break;
   case PIPE_TESS_SPACING_EQUAL:
      partitioning = SWR_TS_INTEGER;
      break;
   default:
      assert(0);
   }

   if (tes_point_mode) {
      topology = SWR_TS_OUTPUT_POINT;
      postDSTopology = TOP_POINT_LIST;
   }
   else if (tes_prim_mode == PIPE_PRIM_LINES) {
      topology = SWR_TS_OUTPUT_LINE;
   }
   else if (tes_vertex_order_cw) {
      topology = SWR_TS_OUTPUT_TRI_CW;
   }
   else {
      topology = SWR_TS_OUTPUT_TRI_CCW;
   }

   pTS->domain = type;
   pTS->tsOutputTopology = topology;
   pTS->partitioning = partitioning;
   pTS->numDsOutputAttribs = info->num_outputs;
   pTS->postDSTopology = postDSTopology;

   pTS->dsAllocationSize = SWR_VTX_NUM_SLOTS * MAX_NUM_VERTS_PER_PRIM;
   pTS->vertexAttribOffset = VERTEX_ATTRIB_START_SLOT;
   pTS->srcVertexAttribOffset = VERTEX_ATTRIB_START_SLOT;
   pTS->dsOutVtxAttribOffset = VERTEX_ATTRIB_START_SLOT;

   struct swr_tess_evaluation_shader *tes = ctx->tes;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));

   std::vector<Type *> tesArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                               PointerType::get(mInt8Ty, 0),
                               PointerType::get(Gen_SWR_DS_CONTEXT(JM()), 0)};
   FunctionType *tesFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), tesArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(tesFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "TES",
                                     JM()->mpCurrentModule);

#if LLVM_VERSION_MAJOR < 5
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);
#else
   pFunction->addAttributes(AttributeList::FunctionIndex, attrBuilder);
#endif

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pWorkerData = &*argitr++;
   pWorkerData->setName("pWorkerData");
   Value *pTesCtx = &*argitr++;
   pTesCtx->setName("tesCtx");

   Value *consts_ptr =
      GEP(hPrivateData, {C(0), C(swr_draw_context_constantTES)});
   consts_ptr->setName("tes_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsTES});
   const_sizes_ptr->setName("num_tes_constants");

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_TESS_EVAL);
   assert(sampler != nullptr);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));

   // Load and calculate system values
   // Tessellation coordinates (gl_TessCoord)
   Value *vecOffset = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_vectorOffset}, "vecOffset");
   Value *vecStride = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_vectorStride}, "vecStride");
   Value *vecIndex  = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_vectorOffset});

   Value* tess_coord = ALLOCA(ArrayType::get(mSimdFP32Ty, 3));

   Value *tessCoordU = LOADV(LOAD(pTesCtx, {0, SWR_DS_CONTEXT_pDomainU}), {vecIndex}, "tessCoordU");
   STORE(tessCoordU, tess_coord, {0, 0});
   Value *tessCoordV = LOADV(LOAD(pTesCtx, {0, SWR_DS_CONTEXT_pDomainV}), {vecIndex}, "tessCoordV");
   STORE(tessCoordV, tess_coord, {0, 1});
   Value *tessCoordW = FSUB(FSUB(VIMMED1(1.0f), tessCoordU), tessCoordV, "tessCoordW");
   STORE(tessCoordW, tess_coord, {0, 2});
   system_values.tess_coord = wrap(tess_coord);

   // Primitive ID
   system_values.prim_id = wrap(VBROADCAST(LOAD(pTesCtx, {0, SWR_DS_CONTEXT_PrimitiveID}), "PrimitiveID"));

   // Tessellation factors
   Value* pPatch = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_pCpIn});
   Value* pTessFactors = GEP(pPatch, {C(0), C(ScalarPatch_tessFactors)});

   assert(SWR_NUM_OUTER_TESS_FACTORS == 4);
   Value* sys_value_outer_factors = UndefValue::get(getVectorType(mFP32Ty, 4));
   for (unsigned i = 0; i < SWR_NUM_OUTER_TESS_FACTORS; i++) {
      Value* v = LOAD(pTessFactors, {0, SWR_TESSELLATION_FACTORS_OuterTessFactors, i});
      sys_value_outer_factors = VINSERT(sys_value_outer_factors, v, i, "gl_TessLevelOuter");
   }
   system_values.tess_outer = wrap(sys_value_outer_factors);

   assert(SWR_NUM_INNER_TESS_FACTORS == 2);
   Value* sys_value_inner_factors = UndefValue::get(getVectorType(mFP32Ty, 4));
   for (unsigned i = 0; i < SWR_NUM_INNER_TESS_FACTORS; i++) {
      Value* v = LOAD(pTessFactors, {0, SWR_TESSELLATION_FACTORS_InnerTessFactors, i});
      sys_value_inner_factors = VINSERT(sys_value_inner_factors, v, i, "gl_TessLevelInner");
   }
   system_values.tess_inner = wrap(sys_value_inner_factors);

   if (verbose_shader)
   {
      lp_build_print_value(gallivm, "tess_coord = ", system_values.tess_coord);
   }

   struct tgsi_shader_info *pPrevShader = nullptr;

   if (ctx->tcs) {
      pPrevShader = &ctx->tcs->info.base;
   }
   else {
      pPrevShader = &ctx->vs->info.base;
   }

   // Figure out how many per-patch attributes we have
   unsigned perPatchAttrs = 0;
   unsigned genericAttrs = 0;
   unsigned tessLevelAttrs = 0;
   unsigned sgvAttrs = 0;
   for (unsigned slot = 0; slot < pPrevShader->num_outputs; slot++) {
      switch (pPrevShader->output_semantic_name[slot]) {
      case TGSI_SEMANTIC_PATCH:
         perPatchAttrs++;
         break;
      case TGSI_SEMANTIC_GENERIC:
         genericAttrs++;
         break;
      case TGSI_SEMANTIC_TESSINNER:
      case TGSI_SEMANTIC_TESSOUTER:
         tessLevelAttrs++;
         break;
      case TGSI_SEMANTIC_POSITION:
      case TGSI_SEMANTIC_CLIPDIST:
      case TGSI_SEMANTIC_PSIZE:
         sgvAttrs++;
         break;
      default:
         assert(!"Unknown semantic input in TES");
      }
   }

   std::vector<Constant *> mapConstants;
   Value *vtxAttribMap = ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   Value *patchAttribMap = ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   for (unsigned slot = 0; slot < info->num_inputs; slot++) {
      ubyte semantic_name = info->input_semantic_name[slot];
      ubyte semantic_idx = info->input_semantic_index[slot];

      // Where in TCS output is my attribute?
      // TESS_TODO: revisit after implement pass-through TCS
      unsigned tcs_slot = locate_linkage(semantic_name, semantic_idx, pPrevShader);
      assert(tcs_slot < PIPE_MAX_SHADER_OUTPUTS);

      // Skip tessellation levels - these go to the tessellator, not TES
      switch (semantic_name) {
      case TGSI_SEMANTIC_GENERIC:
         tcs_slot = tcs_slot + VERTEX_ATTRIB_START_SLOT - sgvAttrs - tessLevelAttrs;
         break;
      case TGSI_SEMANTIC_PATCH:
         tcs_slot = semantic_idx;
         break;
      case TGSI_SEMANTIC_POSITION:
         tcs_slot = VERTEX_POSITION_SLOT;
         break;
      case TGSI_SEMANTIC_CLIPDIST:
      case TGSI_SEMANTIC_PSIZE:
         break;
      default:
         assert(!"Unexpected semantic found while building TES input map");
      }
      if (semantic_name == TGSI_SEMANTIC_PATCH) {
         STORE(C(tcs_slot), patchAttribMap, {0, slot});
      } else {
         STORE(C(tcs_slot), vtxAttribMap, {0, slot});
      }
      mapConstants.push_back(C(tcs_slot));
   }

   // Build execution mask
   struct lp_build_mask_context mask;
   Value *mask_val = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_mask}, "tesMask");

   if (verbose_shader)
      lp_build_print_value(gallivm, "TES execution mask: ", wrap(mask_val));

   lp_build_mask_begin(&mask, gallivm,
                       lp_type_float_vec(32, 32 * 8), wrap(mask_val));

   struct swr_tes_llvm_iface tes_iface;

   tes_iface.base.fetch_vertex_input = ::swr_tes_llvm_fetch_vtx_input;
   tes_iface.base.fetch_patch_input = ::swr_tes_llvm_fetch_patch_input;

   tes_iface.pBuilder = this;
   tes_iface.pTesCtx = pTesCtx;
   tes_iface.pTsState = pTS;
   tes_iface.num_outputs = tes->info.base.num_outputs;
   tes_iface.info = info;
   tes_iface.pVtxAttribMap = vtxAttribMap;
   tes_iface.pPatchAttribMap = patchAttribMap;

   struct lp_build_tgsi_params params;
   memset(&params, 0, sizeof(params));
   params.type = lp_type_float_vec(32, 32 * 8);
   params.mask = & mask;
   params.consts_ptr = wrap(consts_ptr);
   params.const_sizes_ptr = wrap(const_sizes_ptr);
   params.system_values = &system_values;
   params.inputs = inputs;
   params.context_ptr = wrap(hPrivateData);
   params.sampler = sampler;
   params.info = &tes->info.base;
   params.tes_iface = &tes_iface.base;

   // Build LLVM IR
   lp_build_tgsi_soa(gallivm,
                     tes->pipe.tokens,
                     &params,
                     outputs);

   lp_build_mask_end(&mask);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   // Write output attributes
   Value *dclOut = LOAD(pTesCtx, {0, SWR_DS_CONTEXT_pOutputData}, "dclOut");

   for (uint32_t attrib = 0; attrib < PIPE_MAX_SHADER_OUTPUTS; attrib++) {
      for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
         if (!outputs[attrib][channel])
            continue;

         Value *val = LOAD(unwrap(outputs[attrib][channel]));;
         Value *attribOffset =
            LOAD(pTesCtx, {0, SWR_DS_CONTEXT_outVertexAttribOffset});

         // Assume we write possition
         Value* outputSlot = C(VERTEX_POSITION_SLOT);
         if (tes->info.base.output_semantic_name[attrib] != TGSI_SEMANTIC_POSITION) {
            // No, it's a generic attribute, not a position - let's calculate output slot
            uint32_t outSlot = attrib;
            if (tes->info.base.output_semantic_name[0] == TGSI_SEMANTIC_POSITION) {
               // this shader will write position, so in shader's term
               // output starts at attrib 1, but we will handle that separately,
               // so let's fix the outSlot
               outSlot--;
            }
            outputSlot = ADD(attribOffset, C(outSlot));
         }

         Value *attribVecIndex =
            ADD(MUL(vecStride, MUL(outputSlot, C(4))), vecOffset);

         uint32_t outputComponent = 0;
         uint32_t curComp = outputComponent + channel;
         auto outValIndex = ADD(attribVecIndex, MUL(vecStride, C(curComp)));
         STOREV(val, dclOut, {outValIndex});

         if (verbose_shader) {
             lp_build_printf(gallivm,
                            "TES output [%d][%d]",
                            C(attrib),
                            C(channel));
            lp_build_print_value(gallivm, " = ", wrap(val));
         }
      }
   }

   RET_VOID();

   JM()->DumpToFile(pFunction, "src");
   gallivm_verify_function(gallivm, wrap(pFunction));

   gallivm_compile_module(gallivm);
   JM()->DumpToFile(pFunction, "optimized");

   PFN_TES_FUNC pFunc =
      (PFN_TES_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   debug_printf("tess evaluation shader  %p\n", pFunc);
   assert(pFunc && "Error: TessEvaluationShader = NULL");

   JM()->DumpAsm(pFunction, "asm");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}

PFN_TCS_FUNC
BuilderSWR::CompileTCS(struct swr_context *ctx, swr_jit_tcs_key &key)
{
   SWR_TS_STATE *pTS = &ctx->tsState;
   struct tgsi_shader_info *info = &ctx->tcs->info.base;

   pTS->numHsInputAttribs = info->num_inputs;
   pTS->numHsOutputAttribs = info->num_outputs;

   pTS->hsAllocationSize = sizeof(ScalarPatch);

   pTS->vertexAttribOffset = VERTEX_ATTRIB_START_SLOT;
   pTS->srcVertexAttribOffset = VERTEX_ATTRIB_START_SLOT;

   struct swr_tess_control_shader *tcs = ctx->tcs;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));

   std::vector<Type *> tcsArgs{
      PointerType::get(Gen_swr_draw_context(JM()), 0),
      PointerType::get(mInt8Ty, 0),
      PointerType::get(Gen_SWR_HS_CONTEXT(JM()), 0)};
   FunctionType *tcsFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), tcsArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(tcsFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "TCS",
                                     JM()->mpCurrentModule);

#if LLVM_VERSION_MAJOR < 5
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);
#else
   pFunction->addAttributes(AttributeList::FunctionIndex, attrBuilder);
#endif

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pWorkerData = &*argitr++;
   pWorkerData->setName("pWorkerData");
   Value *pTcsCtx = &*argitr++;
   pTcsCtx->setName("tcsCtx");

   Value *consts_ptr =
      GEP(hPrivateData, {C(0), C(swr_draw_context_constantTCS)});
   consts_ptr->setName("tcs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsTCS});
   const_sizes_ptr->setName("num_tcs_constants");

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_TESS_CTRL);
   assert(sampler != nullptr);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));

   system_values.prim_id =
      wrap(LOAD(pTcsCtx, {0, SWR_HS_CONTEXT_PrimitiveID}));

   system_values.invocation_id = wrap(VBROADCAST(C(0)));
   system_values.vertices_in = wrap(C(tcs->vertices_per_patch));

   if (verbose_shader) {
      lp_build_print_value(gallivm, "TCS::prim_id = ", system_values.prim_id);
      lp_build_print_value(gallivm, "TCS::invocation_id = ", system_values.invocation_id);
      lp_build_print_value(gallivm, "TCS::vertices_in = ", system_values.vertices_in);
   }

   std::vector<Constant *> mapConstants;
   Value *vtxAttribMap =
      ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));

   for (unsigned slot = 0; slot < info->num_inputs; slot++) {
      ubyte semantic_name = info->input_semantic_name[slot];
      ubyte semantic_idx = info->input_semantic_index[slot];

      unsigned vs_slot =
         locate_linkage(semantic_name, semantic_idx, &ctx->vs->info.base);
      assert(vs_slot < PIPE_MAX_SHADER_OUTPUTS);

      vs_slot += VERTEX_ATTRIB_START_SLOT;

      if (ctx->vs->info.base.output_semantic_name[0]
          == TGSI_SEMANTIC_POSITION)
         vs_slot--;

      if (semantic_name == TGSI_SEMANTIC_POSITION)
         vs_slot = VERTEX_POSITION_SLOT;

      STORE(C(vs_slot), vtxAttribMap, {0, slot});
      mapConstants.push_back(C(vs_slot));
   }

   // Prepare map of output attributes. Needed when shader instance wants
   // to read own output or output of other instance, which is allowed in TCS
   Value *vtxOutputAttribMap =
      ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   // Map for per-patch attributes
   Value *patchOutputAttribMap =
      ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   for (unsigned slot = 0; slot < info->num_outputs; slot++) {
      ubyte name = info->output_semantic_name[slot];
      int32_t idx = info->output_semantic_index[slot];
      if (name == TGSI_SEMANTIC_PATCH) {
         STORE(C(idx), patchOutputAttribMap, {0, slot});
      } else {
         int32_t target_slot = slot;
         if (name == TGSI_SEMANTIC_GENERIC) {
            target_slot += VERTEX_ATTRIB_START_SLOT;
         }
         // Now normalize target slot
         for (ubyte as = 0; as < slot; as++) {
            ubyte name = info->output_semantic_name[as];
            switch (name) {
               case TGSI_SEMANTIC_TESSOUTER:
               case TGSI_SEMANTIC_TESSINNER:
               case TGSI_SEMANTIC_PATCH:
               case TGSI_SEMANTIC_POSITION:
                  target_slot--;
            }
         }
         if (name == TGSI_SEMANTIC_POSITION) {
            target_slot = VERTEX_POSITION_SLOT;
         }
         STORE(C(target_slot), vtxOutputAttribMap, {0, slot});
         mapConstants.push_back(C(target_slot));
      }
   }

   struct lp_build_mask_context mask;
   Value *mask_val = LOAD(pTcsCtx, {0, SWR_HS_CONTEXT_mask}, "tcsMask");
   lp_build_mask_begin(
      &mask, gallivm, lp_type_float_vec(32, 32 * 8), wrap(mask_val));

   struct swr_tcs_llvm_iface tcs_iface;

   tcs_iface.base.emit_store_output = ::swr_tcs_llvm_store_output;
   tcs_iface.base.emit_fetch_input = ::swr_tcs_llvm_fetch_input;
   tcs_iface.base.emit_fetch_output = ::swr_tcs_llvm_fetch_output;
   tcs_iface.base.emit_barrier = ::swr_tcs_llvm_emit_barrier;
   tcs_iface.base.emit_prologue = ::swr_tcs_llvm_emit_prologue;
   tcs_iface.base.emit_epilogue = ::swr_tcs_llvm_emit_epilogue;

   tcs_iface.pBuilder = this;
   tcs_iface.pTcsCtx = pTcsCtx;
   tcs_iface.pTsState = pTS;
   tcs_iface.output_vertices = info->properties[TGSI_PROPERTY_TCS_VERTICES_OUT];
   tcs_iface.info = info;
   tcs_iface.pVtxAttribMap = vtxAttribMap;
   tcs_iface.pVtxOutputAttribMap = vtxOutputAttribMap;
   tcs_iface.pPatchOutputAttribMap = patchOutputAttribMap;

   struct lp_build_tgsi_params params;
   memset(&params, 0, sizeof(params));
   params.type = lp_type_float_vec(32, 32 * 8);
   params.mask = &mask;
   params.consts_ptr = wrap(consts_ptr);
   params.const_sizes_ptr = wrap(const_sizes_ptr);
   params.system_values = &system_values;
   params.inputs = inputs;
   params.context_ptr = wrap(hPrivateData);
   params.sampler = sampler;
   params.info = &tcs->info.base;
   params.tcs_iface = &tcs_iface.base;

   lp_build_tgsi_soa(gallivm, tcs->pipe.tokens, &params, outputs);

   lp_build_mask_end(&mask);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));
   RET_VOID();

   JM()->DumpToFile(pFunction, "src");
   gallivm_verify_function(gallivm, wrap(pFunction));
   gallivm_compile_module(gallivm);
   JM()->DumpToFile(pFunction, "optimized");

   PFN_TCS_FUNC pFunc =
      (PFN_TCS_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   debug_printf("tess control shader  %p\n", pFunc);
   assert(pFunc && "Error: TessControlShader = NULL");
   JM()->DumpAsm(pFunction, "asm");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}


PFN_GS_FUNC
swr_compile_gs(struct swr_context *ctx, swr_jit_gs_key &key)
{
   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "GS");
   PFN_GS_FUNC func = builder.CompileGS(ctx, key);

   ctx->gs->map.insert(std::make_pair(key, std::unique_ptr<VariantGS>(new VariantGS(builder.gallivm, func))));
   return func;
}

PFN_TCS_FUNC
swr_compile_tcs(struct swr_context *ctx, swr_jit_tcs_key &key)
{
   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "TCS");
   PFN_TCS_FUNC func = builder.CompileTCS(ctx, key);

   ctx->tcs->map.insert(
      std::make_pair(key, std::unique_ptr<VariantTCS>(new VariantTCS(builder.gallivm, func))));

   return func;
}

PFN_TES_FUNC
swr_compile_tes(struct swr_context *ctx, swr_jit_tes_key &key)
{
   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "TES");
   PFN_TES_FUNC func = builder.CompileTES(ctx, key);

   ctx->tes->map.insert(
      std::make_pair(key, std::unique_ptr<VariantTES>(new VariantTES(builder.gallivm, func))));

   return func;
}

void
BuilderSWR::WriteVS(Value *pVal, Value *pVsContext, Value *pVtxOutput, unsigned slot, unsigned channel)
{
#if USE_SIMD16_FRONTEND && !USE_SIMD16_VS
   // interleave the simdvertex components into the dest simd16vertex
   //   slot16offset = slot8offset * 2
   //   comp16offset = comp8offset * 2 + alternateOffset

   Value *offset = LOAD(pVsContext, { 0, SWR_VS_CONTEXT_AlternateOffset });
   Value *pOut = GEP(pVtxOutput, { C(0), C(0), C(slot * 2), offset } );
   STORE(pVal, pOut, {channel * 2});
#else
   Value *pOut = GEP(pVtxOutput, {0, 0, slot});
   STORE(pVal, pOut, {0, channel});
   if (verbose_vs_shader) {
      lp_build_printf(gallivm, "VS: Storing on slot %d, channel %d: ", C(slot), C(channel));
      lp_build_print_value(gallivm, "", wrap(pVal));
   }
#endif
}

PFN_VERTEX_FUNC
BuilderSWR::CompileVS(struct swr_context *ctx, swr_jit_vs_key &key)
{
   struct swr_vertex_shader *swr_vs = ctx->vs;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));

   std::vector<Type *> vsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(mInt8Ty, 0),
                              PointerType::get(Gen_SWR_VS_CONTEXT(JM()), 0)};
   FunctionType *vsFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), vsArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(vsFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "VS",
                                     JM()->mpCurrentModule);
#if LLVM_VERSION_MAJOR < 5
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);
#else
   pFunction->addAttributes(AttributeList::FunctionIndex, attrBuilder);
#endif

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pWorkerData = &*argitr++;
   pWorkerData->setName("pWorkerData");
   Value *pVsCtx = &*argitr++;
   pVsCtx->setName("vsCtx");

   Value *consts_ptr = GEP(hPrivateData, {C(0), C(swr_draw_context_constantVS)});

   consts_ptr->setName("vs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsVS});
   const_sizes_ptr->setName("num_vs_constants");

   Value *vtxInput = LOAD(pVsCtx, {0, SWR_VS_CONTEXT_pVin});
#if USE_SIMD16_VS
   vtxInput = BITCAST(vtxInput, PointerType::get(Gen_simd16vertex(JM()), 0));
#endif

   for (uint32_t attrib = 0; attrib < PIPE_MAX_SHADER_INPUTS; attrib++) {
      const unsigned mask = swr_vs->info.base.input_usage_mask[attrib];
      for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
         if (mask & (1 << channel)) {
            inputs[attrib][channel] =
               wrap(LOAD(vtxInput, {0, 0, attrib, channel}));
         }
      }
   }

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_VERTEX);
   assert(sampler != nullptr);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));
   system_values.instance_id = wrap(LOAD(pVsCtx, {0, SWR_VS_CONTEXT_InstanceID}));

#if USE_SIMD16_VS
   system_values.vertex_id = wrap(LOAD(pVsCtx, {0, SWR_VS_CONTEXT_VertexID16}));
#else
   system_values.vertex_id = wrap(LOAD(pVsCtx, {0, SWR_VS_CONTEXT_VertexID}));
#endif

#if USE_SIMD16_VS
   uint32_t vectorWidth = mVWidth16;
#else
   uint32_t vectorWidth = mVWidth;
#endif

   struct lp_build_tgsi_params params;
   memset(&params, 0, sizeof(params));
   params.type = lp_type_float_vec(32, 32 * vectorWidth);
   params.consts_ptr = wrap(consts_ptr);
   params.const_sizes_ptr = wrap(const_sizes_ptr);
   params.system_values = &system_values;
   params.inputs = inputs;
   params.context_ptr = wrap(hPrivateData);
   params.sampler = sampler;
   params.info = &swr_vs->info.base;

   lp_build_tgsi_soa(gallivm,
                     swr_vs->pipe.tokens,
                     &params,
                     outputs);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   Value *vtxOutput = LOAD(pVsCtx, {0, SWR_VS_CONTEXT_pVout});
#if USE_SIMD16_VS
   vtxOutput = BITCAST(vtxOutput, PointerType::get(Gen_simd16vertex(JM()), 0));
#endif

   for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
      for (uint32_t attrib = 0; attrib < PIPE_MAX_SHADER_OUTPUTS; attrib++) {
         if (!outputs[attrib][channel])
            continue;

         Value *val;
         uint32_t outSlot;

         if (swr_vs->info.base.output_semantic_name[attrib] == TGSI_SEMANTIC_PSIZE) {
            if (channel != VERTEX_SGV_POINT_SIZE_COMP)
               continue;
            val = LOAD(unwrap(outputs[attrib][0]));
            outSlot = VERTEX_SGV_SLOT;
         } else if (swr_vs->info.base.output_semantic_name[attrib] == TGSI_SEMANTIC_POSITION) {
            val = LOAD(unwrap(outputs[attrib][channel]));
            outSlot = VERTEX_POSITION_SLOT;
         } else {
            val = LOAD(unwrap(outputs[attrib][channel]));
            outSlot = VERTEX_ATTRIB_START_SLOT + attrib;
            if (swr_vs->info.base.output_semantic_name[0] == TGSI_SEMANTIC_POSITION)
               outSlot--;
         }

         WriteVS(val, pVsCtx, vtxOutput, outSlot, channel);
      }
   }

   if (ctx->rasterizer->clip_plane_enable ||
       swr_vs->info.base.culldist_writemask) {
      unsigned clip_mask = ctx->rasterizer->clip_plane_enable;

      unsigned cv = 0;
      if (swr_vs->info.base.writes_clipvertex) {
         cv = locate_linkage(TGSI_SEMANTIC_CLIPVERTEX, 0,
                             &swr_vs->info.base);
      } else {
         for (int i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
            if (swr_vs->info.base.output_semantic_name[i] == TGSI_SEMANTIC_POSITION &&
                swr_vs->info.base.output_semantic_index[i] == 0) {
               cv = i;
               break;
            }
         }
      }
      assert(cv < PIPE_MAX_SHADER_OUTPUTS);
      LLVMValueRef cx = LLVMBuildLoad(gallivm->builder, outputs[cv][0], "");
      LLVMValueRef cy = LLVMBuildLoad(gallivm->builder, outputs[cv][1], "");
      LLVMValueRef cz = LLVMBuildLoad(gallivm->builder, outputs[cv][2], "");
      LLVMValueRef cw = LLVMBuildLoad(gallivm->builder, outputs[cv][3], "");

      tgsi_shader_info *pLastFE = &ctx->vs->info.base;

      if (ctx->gs) {
         pLastFE = &ctx->gs->info.base;
      }
      else if (ctx->tes) {
         pLastFE = &ctx->tes->info.base;
      }
      else if (ctx->tcs) {
         pLastFE = &ctx->tcs->info.base;
      }

      for (unsigned val = 0; val < PIPE_MAX_CLIP_PLANES; val++) {
         // clip distance overrides user clip planes
         if ((pLastFE->clipdist_writemask & clip_mask & (1 << val)) ||
             ((pLastFE->culldist_writemask << pLastFE->num_written_clipdistance) & (1 << val))) {
            unsigned cv = locate_linkage(TGSI_SEMANTIC_CLIPDIST, val < 4 ? 0 : 1, pLastFE);
            assert(cv < PIPE_MAX_SHADER_OUTPUTS);
            if (val < 4) {
               LLVMValueRef dist = LLVMBuildLoad(gallivm->builder, outputs[cv][val], "");
               WriteVS(unwrap(dist), pVsCtx, vtxOutput, VERTEX_CLIPCULL_DIST_LO_SLOT, val);
            } else {
               LLVMValueRef dist = LLVMBuildLoad(gallivm->builder, outputs[cv][val - 4], "");
               WriteVS(unwrap(dist), pVsCtx, vtxOutput, VERTEX_CLIPCULL_DIST_HI_SLOT, val - 4);
            }
            continue;
         }

         if (!(clip_mask & (1 << val)))
            continue;

         Value *px = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 0}));
         Value *py = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 1}));
         Value *pz = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 2}));
         Value *pw = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 3}));
#if USE_SIMD16_VS
         Value *bpx = VBROADCAST_16(px);
         Value *bpy = VBROADCAST_16(py);
         Value *bpz = VBROADCAST_16(pz);
         Value *bpw = VBROADCAST_16(pw);
#else
         Value *bpx = VBROADCAST(px);
         Value *bpy = VBROADCAST(py);
         Value *bpz = VBROADCAST(pz);
         Value *bpw = VBROADCAST(pw);
#endif
         Value *dist = FADD(FMUL(unwrap(cx), bpx),
                            FADD(FMUL(unwrap(cy), bpy),
                                 FADD(FMUL(unwrap(cz), bpz),
                                      FMUL(unwrap(cw), bpw))));

         if (val < 4)
            WriteVS(dist, pVsCtx, vtxOutput, VERTEX_CLIPCULL_DIST_LO_SLOT, val);
         else
            WriteVS(dist, pVsCtx, vtxOutput, VERTEX_CLIPCULL_DIST_HI_SLOT, val - 4);
      }
   }

   RET_VOID();

   JM()->DumpToFile(pFunction, "vs_function1");
   gallivm_verify_function(gallivm, wrap(pFunction));
   gallivm_compile_module(gallivm);
   JM()->DumpToFile(pFunction, "vs_function2");

   //   lp_debug_dump_value(func);

   PFN_VERTEX_FUNC pFunc =
      (PFN_VERTEX_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   JM()->DumpAsm(pFunction, "vs_function_asm");
   debug_printf("vert shader  %p\n", pFunc);
   assert(pFunc && "Error: VertShader = NULL");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}

PFN_VERTEX_FUNC
swr_compile_vs(struct swr_context *ctx, swr_jit_vs_key &key)
{
   if (!ctx->vs->pipe.tokens)
      return NULL;

   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "VS");
   PFN_VERTEX_FUNC func = builder.CompileVS(ctx, key);

   ctx->vs->map.insert(std::make_pair(key, std::unique_ptr<VariantVS>(new VariantVS(builder.gallivm, func))));
   return func;
}

unsigned
swr_so_adjust_attrib(unsigned in_attrib,
                     swr_vertex_shader *swr_vs)
{
   ubyte semantic_name;
   unsigned attrib;

   attrib = in_attrib + VERTEX_ATTRIB_START_SLOT;

   if (swr_vs) {
      semantic_name = swr_vs->info.base.output_semantic_name[in_attrib];
      if (semantic_name == TGSI_SEMANTIC_POSITION) {
         attrib = VERTEX_POSITION_SLOT;
      } else if (semantic_name == TGSI_SEMANTIC_PSIZE) {
         attrib = VERTEX_SGV_SLOT;
      } else if (semantic_name == TGSI_SEMANTIC_LAYER) {
         attrib = VERTEX_SGV_SLOT;
      } else {
         if (swr_vs->info.base.writes_position) {
               attrib--;
         }
      }
   }

   return attrib;
}

static unsigned
locate_linkage(ubyte name, ubyte index, struct tgsi_shader_info *info)
{
   for (int i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
      if ((info->output_semantic_name[i] == name)
          && (info->output_semantic_index[i] == index)) {
         return i;
      }
   }

   return 0xFFFFFFFF;
}

PFN_PIXEL_KERNEL
BuilderSWR::CompileFS(struct swr_context *ctx, swr_jit_fs_key &key)
{
   struct swr_fragment_shader *swr_fs = ctx->fs;

   struct tgsi_shader_info *pPrevShader;
   if (ctx->gs)
      pPrevShader = &ctx->gs->info.base;
   else if (ctx->tes)
      pPrevShader = &ctx->tes->info.base;
   else
      pPrevShader = &ctx->vs->info.base;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(inputs, 0, sizeof(inputs));
   memset(outputs, 0, sizeof(outputs));

   struct lp_build_sampler_soa *sampler = NULL;

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));

   std::vector<Type *> fsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(mInt8Ty, 0),
                              PointerType::get(Gen_SWR_PS_CONTEXT(JM()), 0)};
   FunctionType *funcType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), fsArgs, false);

   auto pFunction = Function::Create(funcType,
                                     GlobalValue::ExternalLinkage,
                                     "FS",
                                     JM()->mpCurrentModule);
#if LLVM_VERSION_MAJOR < 5
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);
#else
   pFunction->addAttributes(AttributeList::FunctionIndex, attrBuilder);
#endif

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto args = pFunction->arg_begin();
   Value *hPrivateData = &*args++;
   hPrivateData->setName("hPrivateData");
   Value *pWorkerData = &*args++;
   pWorkerData->setName("pWorkerData");
   Value *pPS = &*args++;
   pPS->setName("psCtx");

   Value *consts_ptr = GEP(hPrivateData, {0, swr_draw_context_constantFS});
   consts_ptr->setName("fs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsFS});
   const_sizes_ptr->setName("num_fs_constants");

   // load *pAttribs, *pPerspAttribs
   Value *pRawAttribs = LOAD(pPS, {0, SWR_PS_CONTEXT_pAttribs}, "pRawAttribs");
   Value *pPerspAttribs =
      LOAD(pPS, {0, SWR_PS_CONTEXT_pPerspAttribs}, "pPerspAttribs");

   swr_fs->constantMask = 0;
   swr_fs->flatConstantMask = 0;
   swr_fs->pointSpriteMask = 0;

   for (int attrib = 0; attrib < PIPE_MAX_SHADER_INPUTS; attrib++) {
      const unsigned mask = swr_fs->info.base.input_usage_mask[attrib];
      const unsigned interpMode = swr_fs->info.base.input_interpolate[attrib];
      const unsigned interpLoc = swr_fs->info.base.input_interpolate_loc[attrib];

      if (!mask)
         continue;

      // load i,j
      Value *vi = nullptr, *vj = nullptr;
      switch (interpLoc) {
      case TGSI_INTERPOLATE_LOC_CENTER:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_center}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_center}, "j");
         break;
      case TGSI_INTERPOLATE_LOC_CENTROID:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_centroid}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_centroid}, "j");
         break;
      case TGSI_INTERPOLATE_LOC_SAMPLE:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_sample}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_sample}, "j");
         break;
      }

      // load/compute w
      Value *vw = nullptr, *pAttribs;
      if (interpMode == TGSI_INTERPOLATE_PERSPECTIVE ||
          interpMode == TGSI_INTERPOLATE_COLOR) {
         pAttribs = pPerspAttribs;
         switch (interpLoc) {
         case TGSI_INTERPOLATE_LOC_CENTER:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_center}));
            break;
         case TGSI_INTERPOLATE_LOC_CENTROID:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_centroid}));
            break;
         case TGSI_INTERPOLATE_LOC_SAMPLE:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_sample}));
            break;
         }
      } else {
         pAttribs = pRawAttribs;
         vw = VIMMED1(1.f);
      }

      vw->setName("w");

      ubyte semantic_name = swr_fs->info.base.input_semantic_name[attrib];
      ubyte semantic_idx = swr_fs->info.base.input_semantic_index[attrib];

      if (semantic_name == TGSI_SEMANTIC_FACE) {
         Value *ff =
            UI_TO_FP(LOAD(pPS, {0, SWR_PS_CONTEXT_frontFace}), mFP32Ty);
         ff = FSUB(FMUL(ff, C(2.0f)), C(1.0f));
         ff = VECTOR_SPLAT(JM()->mVWidth, ff, "vFrontFace");

         inputs[attrib][0] = wrap(ff);
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(1.0f));
         continue;
      } else if (semantic_name == TGSI_SEMANTIC_POSITION) { // gl_FragCoord
         if (swr_fs->info.base.properties[TGSI_PROPERTY_FS_COORD_PIXEL_CENTER] ==
             TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER) {
            inputs[attrib][0] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vX, PixelPositions_center}, "vX"));
            inputs[attrib][1] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vY, PixelPositions_center}, "vY"));
         } else {
            inputs[attrib][0] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vX, PixelPositions_UL}, "vX"));
            inputs[attrib][1] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vY, PixelPositions_UL}, "vY"));
         }
         inputs[attrib][2] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vZ}, "vZ"));
         inputs[attrib][3] =
            wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_center}, "vOneOverW"));
         continue;
      } else if (semantic_name == TGSI_SEMANTIC_LAYER) { // gl_Layer
         Value *ff = LOAD(pPS, {0, SWR_PS_CONTEXT_renderTargetArrayIndex});
         ff = VECTOR_SPLAT(JM()->mVWidth, ff, "vRenderTargetArrayIndex");
         inputs[attrib][0] = wrap(ff);
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(0.0f));
         continue;
      } else if (semantic_name == TGSI_SEMANTIC_VIEWPORT_INDEX) { // gl_ViewportIndex
         Value *ff = LOAD(pPS, {0, SWR_PS_CONTEXT_viewportIndex});
         ff = VECTOR_SPLAT(JM()->mVWidth, ff, "vViewportIndex");
         inputs[attrib][0] = wrap(ff);
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(0.0f));
         continue;
      }
      unsigned linkedAttrib =
         locate_linkage(semantic_name, semantic_idx, pPrevShader) - 1;

      uint32_t extraAttribs = 0;
      if (semantic_name == TGSI_SEMANTIC_PRIMID && !ctx->gs) {
         /* non-gs generated primID - need to grab from swizzleMap override */
         linkedAttrib = pPrevShader->num_outputs - 1;
         swr_fs->constantMask |= 1 << linkedAttrib;
         extraAttribs++;
      } else if (semantic_name == TGSI_SEMANTIC_GENERIC &&
          key.sprite_coord_enable & (1 << semantic_idx)) {
         /* we add an extra attrib to the backendState in swr_update_derived. */
         linkedAttrib = pPrevShader->num_outputs + extraAttribs - 1;
         swr_fs->pointSpriteMask |= (1 << linkedAttrib);
         extraAttribs++;
      } else if (linkedAttrib + 1 == 0xFFFFFFFF) {
         inputs[attrib][0] = wrap(VIMMED1(0.0f));
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(1.0f));
         /* If we're reading in color and 2-sided lighting is enabled, we have
          * to keep going.
          */
         if (semantic_name != TGSI_SEMANTIC_COLOR || !key.light_twoside)
            continue;
      } else {
         if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
            swr_fs->constantMask |= 1 << linkedAttrib;
         } else if (interpMode == TGSI_INTERPOLATE_COLOR) {
            swr_fs->flatConstantMask |= 1 << linkedAttrib;
         }
      }

      unsigned bcolorAttrib = 0xFFFFFFFF;
      Value *offset = NULL;
      if (semantic_name == TGSI_SEMANTIC_COLOR && key.light_twoside) {
         bcolorAttrib = locate_linkage(
               TGSI_SEMANTIC_BCOLOR, semantic_idx, pPrevShader);
         /* Neither front nor back colors were available. Nothing to load. */
         if (bcolorAttrib == 0xFFFFFFFF && linkedAttrib == 0xFFFFFFFF)
            continue;
         /* If there is no front color, just always use the back color. */
         if (linkedAttrib + 1 == 0xFFFFFFFF)
            linkedAttrib = bcolorAttrib;

         if (bcolorAttrib != 0xFFFFFFFF) {
            bcolorAttrib -= 1;
            if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
               swr_fs->constantMask |= 1 << bcolorAttrib;
            } else if (interpMode == TGSI_INTERPOLATE_COLOR) {
               swr_fs->flatConstantMask |= 1 << bcolorAttrib;
            }

            unsigned diff = 12 * (bcolorAttrib - linkedAttrib);

            if (diff) {
               Value *back =
                  XOR(C(1), LOAD(pPS, {0, SWR_PS_CONTEXT_frontFace}), "backFace");

               offset = MUL(back, C(diff));
               offset->setName("offset");
            }
         }
      }

      for (int channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
         if (mask & (1 << channel)) {
            Value *indexA = C(linkedAttrib * 12 + channel);
            Value *indexB = C(linkedAttrib * 12 + channel + 4);
            Value *indexC = C(linkedAttrib * 12 + channel + 8);

            if (offset) {
               indexA = ADD(indexA, offset);
               indexB = ADD(indexB, offset);
               indexC = ADD(indexC, offset);
            }

            Value *va = VBROADCAST(LOAD(GEP(pAttribs, indexA)));
            Value *vb = VBROADCAST(LOAD(GEP(pAttribs, indexB)));
            Value *vc = VBROADCAST(LOAD(GEP(pAttribs, indexC)));

            if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
               inputs[attrib][channel] = wrap(va);
            } else {
               Value *vk = FSUB(FSUB(VIMMED1(1.0f), vi), vj);

               vc = FMUL(vk, vc);

               Value *interp = FMUL(va, vi);
               Value *interp1 = FMUL(vb, vj);
               interp = FADD(interp, interp1);
               interp = FADD(interp, vc);
               if (interpMode == TGSI_INTERPOLATE_PERSPECTIVE ||
                   interpMode == TGSI_INTERPOLATE_COLOR)
                  interp = FMUL(interp, vw);
               inputs[attrib][channel] = wrap(interp);
            }
         }
      }
   }

   sampler = swr_sampler_soa_create(key.sampler, PIPE_SHADER_FRAGMENT);
   assert(sampler != nullptr);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));

   struct lp_build_mask_context mask;
   bool uses_mask = false;

   if (swr_fs->info.base.uses_kill ||
       key.poly_stipple_enable) {
      Value *vActiveMask = NULL;
      if (swr_fs->info.base.uses_kill) {
         vActiveMask = LOAD(pPS, {0, SWR_PS_CONTEXT_activeMask}, "activeMask");
      }
      if (key.poly_stipple_enable) {
         // first get fragment xy coords and clip to stipple bounds
         Value *vXf = LOAD(pPS, {0, SWR_PS_CONTEXT_vX, PixelPositions_UL});
         Value *vYf = LOAD(pPS, {0, SWR_PS_CONTEXT_vY, PixelPositions_UL});
         Value *vXu = FP_TO_UI(vXf, mSimdInt32Ty);
         Value *vYu = FP_TO_UI(vYf, mSimdInt32Ty);

         // stipple pattern is 32x32, which means that one line of stipple
         // is stored in one word:
         // vXstipple is bit offset inside 32-bit stipple word
         // vYstipple is word index is stipple array
         Value *vXstipple = AND(vXu, VIMMED1(0x1f)); // & (32-1)
         Value *vYstipple = AND(vYu, VIMMED1(0x1f)); // & (32-1)

         // grab stipple pattern base address
         Value *stipplePtr = GEP(hPrivateData, {0, swr_draw_context_polyStipple, 0});
         stipplePtr = BITCAST(stipplePtr, mInt8PtrTy);

         // peform a gather to grab stipple words for each lane
         Value *vStipple = GATHERDD(VUNDEF_I(), stipplePtr, vYstipple,
                                    VIMMED1(0xffffffff), 4);

         // create a mask with one bit corresponding to the x stipple
         // and AND it with the pattern, to see if we have a bit
         Value *vBitMask = LSHR(VIMMED1(0x80000000), vXstipple);
         Value *vStippleMask = AND(vStipple, vBitMask);
         vStippleMask = ICMP_NE(vStippleMask, VIMMED1(0));
         vStippleMask = VMASK(vStippleMask);

         if (swr_fs->info.base.uses_kill) {
            vActiveMask = AND(vActiveMask, vStippleMask);
         } else {
            vActiveMask = vStippleMask;
         }
      }
      lp_build_mask_begin(
         &mask, gallivm, lp_type_float_vec(32, 32 * 8), wrap(vActiveMask));
      uses_mask = true;
   }

   struct lp_build_tgsi_params params;
   memset(&params, 0, sizeof(params));
   params.type = lp_type_float_vec(32, 32 * 8);
   params.mask = uses_mask ? &mask : NULL;
   params.consts_ptr = wrap(consts_ptr);
   params.const_sizes_ptr = wrap(const_sizes_ptr);
   params.system_values = &system_values;
   params.inputs = inputs;
   params.context_ptr = wrap(hPrivateData);
   params.sampler = sampler;
   params.info = &swr_fs->info.base;

   lp_build_tgsi_soa(gallivm,
                     swr_fs->pipe.tokens,
                     &params,
                     outputs);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   for (uint32_t attrib = 0; attrib < swr_fs->info.base.num_outputs;
        attrib++) {
      switch (swr_fs->info.base.output_semantic_name[attrib]) {
      case TGSI_SEMANTIC_POSITION: {
         // write z
         LLVMValueRef outZ =
            LLVMBuildLoad(gallivm->builder, outputs[attrib][2], "");
         STORE(unwrap(outZ), pPS, {0, SWR_PS_CONTEXT_vZ});
         break;
      }
      case TGSI_SEMANTIC_COLOR: {
         for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
            if (!outputs[attrib][channel])
               continue;

            LLVMValueRef out =
               LLVMBuildLoad(gallivm->builder, outputs[attrib][channel], "");
            if (swr_fs->info.base.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS] &&
                swr_fs->info.base.output_semantic_index[attrib] == 0) {
               for (uint32_t rt = 0; rt < key.nr_cbufs; rt++) {
                  STORE(unwrap(out),
                        pPS,
                        {0, SWR_PS_CONTEXT_shaded, rt, channel});
               }
            } else {
               STORE(unwrap(out),
                     pPS,
                     {0,
                           SWR_PS_CONTEXT_shaded,
                           swr_fs->info.base.output_semantic_index[attrib],
                           channel});
            }
         }
         break;
      }
      default: {
         fprintf(stderr,
                 "unknown output from FS %s[%d]\n",
                 tgsi_semantic_names[swr_fs->info.base
                                        .output_semantic_name[attrib]],
                 swr_fs->info.base.output_semantic_index[attrib]);
         break;
      }
      }
   }

   LLVMValueRef mask_result = 0;
   if (uses_mask) {
      mask_result = lp_build_mask_end(&mask);
   }

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   if (uses_mask) {
      STORE(unwrap(mask_result), pPS, {0, SWR_PS_CONTEXT_activeMask});
   }

   RET_VOID();

   gallivm_verify_function(gallivm, wrap(pFunction));

   gallivm_compile_module(gallivm);

   // after the gallivm passes, we have to lower the core's intrinsics
   llvm::legacy::FunctionPassManager lowerPass(JM()->mpCurrentModule);
   lowerPass.add(createLowerX86Pass(this));
   lowerPass.run(*pFunction);

   PFN_PIXEL_KERNEL kernel =
      (PFN_PIXEL_KERNEL)gallivm_jit_function(gallivm, wrap(pFunction));
   debug_printf("frag shader  %p\n", kernel);
   assert(kernel && "Error: FragShader = NULL");

   JM()->mIsModuleFinalized = true;

   return kernel;
}

PFN_PIXEL_KERNEL
swr_compile_fs(struct swr_context *ctx, swr_jit_fs_key &key)
{
   if (!ctx->fs->pipe.tokens)
      return NULL;

   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "FS");
   PFN_PIXEL_KERNEL func = builder.CompileFS(ctx, key);

   ctx->fs->map.insert(std::make_pair(key, std::unique_ptr<VariantFS>(new VariantFS(builder.gallivm, func))));
   return func;
}
