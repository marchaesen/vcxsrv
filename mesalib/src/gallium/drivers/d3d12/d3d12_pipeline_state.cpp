/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_pipeline_state.h"
#include "d3d12_compiler.h"
#include "d3d12_context.h"
#include "d3d12_screen.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_memory.h"
#include "util/u_prim.h"

#include <dxguids/dxguids.h>

struct d3d12_pso_entry {
   struct d3d12_gfx_pipeline_state key;
   ID3D12PipelineState *pso;
};

static const char *
get_semantic_name(int slot, unsigned *index)
{
   *index = 0; /* Default index */

   switch (slot) {

   case VARYING_SLOT_POS:
      return "SV_Position";

    case VARYING_SLOT_FACE:
      return "SV_IsFrontFace";

   case VARYING_SLOT_CLIP_DIST1:
      *index = 1;
      FALLTHROUGH;
   case VARYING_SLOT_CLIP_DIST0:
      return "SV_ClipDistance";

   case VARYING_SLOT_PRIMITIVE_ID:
      return "SV_PrimitiveID";

   default: {
         *index = slot - VARYING_SLOT_POS;
         return "TEXCOORD";
      }
   }
}

static void
fill_so_declaration(const struct pipe_stream_output_info *info,
                    D3D12_SO_DECLARATION_ENTRY *entries, UINT *num_entries,
                    UINT *strides, UINT *num_strides)
{
   int next_offset[MAX_VERTEX_STREAMS] = { 0 };

   *num_entries = 0;

   for (unsigned i = 0; i < info->num_outputs; i++) {
      const struct pipe_stream_output *output = &info->output[i];
      const int buffer = output->output_buffer;
      unsigned index;

      /* Mesa doesn't store entries for gl_SkipComponents in the Outputs[]
       * array.  Instead, it simply increments DstOffset for the following
       * input by the number of components that should be skipped.
       *
       * DirectX12 requires that we create gap entries.
       */
      int skip_components = output->dst_offset - next_offset[buffer];

      if (skip_components > 0) {
         entries[*num_entries].Stream = output->stream;
         entries[*num_entries].SemanticName = NULL;
         entries[*num_entries].ComponentCount = skip_components;
         entries[*num_entries].OutputSlot = buffer;
         (*num_entries)++;
      }

      next_offset[buffer] = output->dst_offset + output->num_components;

      entries[*num_entries].Stream = output->stream;
      entries[*num_entries].SemanticName = get_semantic_name(output->register_index, &index);
      entries[*num_entries].SemanticIndex = index;
      entries[*num_entries].StartComponent = output->start_component;
      entries[*num_entries].ComponentCount = output->num_components;
      entries[*num_entries].OutputSlot = buffer;
      (*num_entries)++;
   }

   for (unsigned i = 0; i < MAX_VERTEX_STREAMS; i++)
      strides[i] = info->stride[i] * 4;
   *num_strides = MAX_VERTEX_STREAMS;
}

static bool
depth_bias(struct d3d12_rasterizer_state *state, enum pipe_prim_type reduced_prim)
{
   /* glPolygonOffset is supposed to be only enabled when rendering polygons.
    * In d3d12 case, all polygons (and quads) are lowered to triangles */
   if (reduced_prim != PIPE_PRIM_TRIANGLES)
      return false;

   unsigned fill_mode = state->base.cull_face == PIPE_FACE_FRONT ? state->base.fill_back
                                                                 : state->base.fill_front;

   switch (fill_mode) {
   case PIPE_POLYGON_MODE_FILL:
      return state->base.offset_tri;

   case PIPE_POLYGON_MODE_LINE:
      return state->base.offset_line;

   case PIPE_POLYGON_MODE_POINT:
      return state->base.offset_point;

   default:
      unreachable("unexpected fill mode");
   }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE
topology_type(enum pipe_prim_type reduced_prim)
{
   switch (reduced_prim) {
   case PIPE_PRIM_POINTS:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

   case PIPE_PRIM_LINES:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

   case PIPE_PRIM_TRIANGLES:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

   case PIPE_PRIM_PATCHES:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

   default:
      debug_printf("pipe_prim_type: %s\n", u_prim_name(reduced_prim));
      unreachable("unexpected enum pipe_prim_type");
   }
}

DXGI_FORMAT
d3d12_rtv_format(struct d3d12_context *ctx, unsigned index)
{
   DXGI_FORMAT fmt = ctx->gfx_pipeline_state.rtv_formats[index];

   if (ctx->gfx_pipeline_state.blend->desc.RenderTarget[0].LogicOpEnable &&
       !ctx->gfx_pipeline_state.has_float_rtv) {
      switch (fmt) {
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
         return DXGI_FORMAT_R8G8B8A8_UINT;
      default:
         unreachable("unsupported logic-op format");
      }
   }

   return fmt;
}

static ID3D12PipelineState *
create_gfx_pipeline_state(struct d3d12_context *ctx)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);
   struct d3d12_gfx_pipeline_state *state = &ctx->gfx_pipeline_state;
   enum pipe_prim_type reduced_prim = u_reduced_prim(state->prim_type);
   D3D12_SO_DECLARATION_ENTRY entries[PIPE_MAX_SO_OUTPUTS] = {};
   UINT strides[PIPE_MAX_SO_OUTPUTS] = { 0 };
   UINT num_entries = 0, num_strides = 0;

   D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = { 0 };
   pso_desc.pRootSignature = state->root_signature;

   bool last_vertex_stage_writes_pos = false;

   if (state->stages[PIPE_SHADER_VERTEX]) {
      auto shader = state->stages[PIPE_SHADER_VERTEX];
      pso_desc.VS.BytecodeLength = shader->bytecode_length;
      pso_desc.VS.pShaderBytecode = shader->bytecode;
      last_vertex_stage_writes_pos = (shader->nir->info.outputs_written & VARYING_BIT_POS) != 0;
   }

   if (state->stages[PIPE_SHADER_GEOMETRY]) {
      auto shader = state->stages[PIPE_SHADER_GEOMETRY];
      pso_desc.GS.BytecodeLength = shader->bytecode_length;
      pso_desc.GS.pShaderBytecode = shader->bytecode;
      last_vertex_stage_writes_pos = (shader->nir->info.outputs_written & VARYING_BIT_POS) != 0;
   }

   if (last_vertex_stage_writes_pos && state->stages[PIPE_SHADER_FRAGMENT] &&
       !state->rast->base.rasterizer_discard) {
      auto shader = state->stages[PIPE_SHADER_FRAGMENT];
      pso_desc.PS.BytecodeLength = shader->bytecode_length;
      pso_desc.PS.pShaderBytecode = shader->bytecode;
   }

   if (state->num_so_targets)
      fill_so_declaration(&state->so_info, entries, &num_entries,
                          strides, &num_strides);
   pso_desc.StreamOutput.NumEntries = num_entries;
   pso_desc.StreamOutput.pSODeclaration = entries;
   pso_desc.StreamOutput.RasterizedStream = state->rast->base.rasterizer_discard ? D3D12_SO_NO_RASTERIZED_STREAM : 0;
   pso_desc.StreamOutput.NumStrides = num_strides;
   pso_desc.StreamOutput.pBufferStrides = strides;

   pso_desc.BlendState = state->blend->desc;
   if (state->has_float_rtv)
      pso_desc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;

   pso_desc.DepthStencilState = state->zsa->desc;
   pso_desc.SampleMask = state->sample_mask;
   pso_desc.RasterizerState = state->rast->desc;

   if (reduced_prim != PIPE_PRIM_TRIANGLES)
      pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

   if (depth_bias(state->rast, reduced_prim)) {
      pso_desc.RasterizerState.DepthBias = state->rast->base.offset_units * 2;
      pso_desc.RasterizerState.DepthBiasClamp = state->rast->base.offset_clamp;
      pso_desc.RasterizerState.SlopeScaledDepthBias = state->rast->base.offset_scale;
   }

   pso_desc.InputLayout.pInputElementDescs = state->ves->elements;
   pso_desc.InputLayout.NumElements = state->ves->num_elements;

   pso_desc.IBStripCutValue = state->ib_strip_cut_value;

   pso_desc.PrimitiveTopologyType = topology_type(reduced_prim);

   pso_desc.NumRenderTargets = state->num_cbufs;
   for (unsigned i = 0; i < state->num_cbufs; ++i)
      pso_desc.RTVFormats[i] = d3d12_rtv_format(ctx, i);
   pso_desc.DSVFormat = state->dsv_format;

   pso_desc.SampleDesc.Count = state->samples;
   pso_desc.SampleDesc.Quality = 0;

   pso_desc.NodeMask = 0;

   pso_desc.CachedPSO.pCachedBlob = NULL;
   pso_desc.CachedPSO.CachedBlobSizeInBytes = 0;

   pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

   ID3D12PipelineState *ret;
   if (FAILED(screen->dev->CreateGraphicsPipelineState(&pso_desc,
                                                       IID_PPV_ARGS(&ret)))) {
      debug_printf("D3D12: CreateGraphicsPipelineState failed!\n");
      return NULL;
   }

   return ret;
}

static uint32_t
hash_gfx_pipeline_state(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct d3d12_gfx_pipeline_state));
}

static bool
equals_gfx_pipeline_state(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct d3d12_gfx_pipeline_state)) == 0;
}

ID3D12PipelineState *
d3d12_get_gfx_pipeline_state(struct d3d12_context *ctx)
{
   uint32_t hash = hash_gfx_pipeline_state(&ctx->gfx_pipeline_state);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(ctx->pso_cache, hash,
                                                                 &ctx->gfx_pipeline_state);
   if (!entry) {
      struct d3d12_pso_entry *data = (struct d3d12_pso_entry *)MALLOC(sizeof(struct d3d12_pso_entry));
      if (!data)
         return NULL;

      data->key = ctx->gfx_pipeline_state;
      data->pso = create_gfx_pipeline_state(ctx);
      if (!data->pso) {
         FREE(data);
         return NULL;
      }

      entry = _mesa_hash_table_insert_pre_hashed(ctx->pso_cache, hash, &data->key, data);
      assert(entry);
   }

   return ((struct d3d12_pso_entry *)(entry->data))->pso;
}

void
d3d12_gfx_pipeline_state_cache_init(struct d3d12_context *ctx)
{
   ctx->pso_cache = _mesa_hash_table_create(NULL, NULL, equals_gfx_pipeline_state);
}

static void
delete_entry(struct hash_entry *entry)
{
   struct d3d12_pso_entry *data = (struct d3d12_pso_entry *)entry->data;
   data->pso->Release();
   FREE(data);
}

static void
remove_entry(struct d3d12_context *ctx, struct hash_entry *entry)
{
   struct d3d12_pso_entry *data = (struct d3d12_pso_entry *)entry->data;

   if (ctx->current_pso == data->pso)
      ctx->current_pso = NULL;
   _mesa_hash_table_remove(ctx->pso_cache, entry);
   delete_entry(entry);
}

void
d3d12_gfx_pipeline_state_cache_destroy(struct d3d12_context *ctx)
{
   _mesa_hash_table_destroy(ctx->pso_cache, delete_entry);
}

void
d3d12_gfx_pipeline_state_cache_invalidate(struct d3d12_context *ctx, const void *state)
{
   hash_table_foreach(ctx->pso_cache, entry) {
      const struct d3d12_gfx_pipeline_state *key = (struct d3d12_gfx_pipeline_state *)entry->key;
      if (key->blend == state || key->zsa == state || key->rast == state)
         remove_entry(ctx, entry);
   }
}

void
d3d12_gfx_pipeline_state_cache_invalidate_shader(struct d3d12_context *ctx,
                                                 enum pipe_shader_type stage,
                                                 struct d3d12_shader_selector *selector)
{
   struct d3d12_shader *shader = selector->first;

   while (shader) {
      hash_table_foreach(ctx->pso_cache, entry) {
         const struct d3d12_gfx_pipeline_state *key = (struct d3d12_gfx_pipeline_state *)entry->key;
         if (key->stages[stage] == shader)
            remove_entry(ctx, entry);
      }
      shader = shader->next_variant;
   }
}
