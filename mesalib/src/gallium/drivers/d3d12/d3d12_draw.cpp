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

#include "d3d12_compiler.h"
#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_query.h"
#include "d3d12_resource.h"
#include "d3d12_root_signature.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"

#include "util/u_debug.h"
#include "util/u_draw.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_prim.h"
#include "util/u_prim_restart.h"
#include "util/u_math.h"

extern "C" {
#include "indices/u_primconvert.h"
}

static const D3D12_RECT MAX_SCISSOR = { D3D12_VIEWPORT_BOUNDS_MIN,
                                        D3D12_VIEWPORT_BOUNDS_MIN,
                                        D3D12_VIEWPORT_BOUNDS_MAX,
                                        D3D12_VIEWPORT_BOUNDS_MAX };

static D3D12_GPU_DESCRIPTOR_HANDLE
fill_cbv_descriptors(struct d3d12_context *ctx,
                     struct d3d12_shader *shader,
                     int stage)
{
   struct d3d12_batch *batch = d3d12_current_batch(ctx);
   struct d3d12_descriptor_handle table_start;
   d2d12_descriptor_heap_get_next_handle(batch->view_heap, &table_start);

   for (unsigned i = 0; i < shader->num_cb_bindings; i++) {
      unsigned binding = shader->cb_bindings[i].binding;
      struct pipe_constant_buffer *buffer = &ctx->cbufs[stage][binding];

      D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
      if (buffer && buffer->buffer) {
         struct d3d12_resource *res = d3d12_resource(buffer->buffer);
         d3d12_transition_resource_state(ctx, res, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_BIND_INVALIDATE_NONE);
         cbv_desc.BufferLocation = d3d12_resource_gpu_virtual_address(res) + buffer->buffer_offset;
         cbv_desc.SizeInBytes = MIN2(D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16,
            align(buffer->buffer_size, 256));
         d3d12_batch_reference_resource(batch, res);
      }

      struct d3d12_descriptor_handle handle;
      d3d12_descriptor_heap_alloc_handle(batch->view_heap, &handle);
      d3d12_screen(ctx->base.screen)->dev->CreateConstantBufferView(&cbv_desc, handle.cpu_handle);
   }

   return table_start.gpu_handle;
}

static D3D12_GPU_DESCRIPTOR_HANDLE
fill_srv_descriptors(struct d3d12_context *ctx,
                     struct d3d12_shader *shader,
                     unsigned stage)
{
   struct d3d12_batch *batch = d3d12_current_batch(ctx);
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);
   D3D12_CPU_DESCRIPTOR_HANDLE descs[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct d3d12_descriptor_handle table_start;

   d2d12_descriptor_heap_get_next_handle(batch->view_heap, &table_start);

   for (unsigned i = shader->begin_srv_binding; i < shader->end_srv_binding; i++)
   {
      struct d3d12_sampler_view *view;

      if (i == shader->pstipple_binding) {
         view = (struct d3d12_sampler_view*)ctx->pstipple.sampler_view;
      } else {
         view = (struct d3d12_sampler_view*)ctx->sampler_views[stage][i];
      }

      unsigned desc_idx = i - shader->begin_srv_binding;
      if (view != NULL) {
         descs[desc_idx] = view->handle.cpu_handle;
         d3d12_batch_reference_sampler_view(batch, view);

         D3D12_RESOURCE_STATES state = (stage == PIPE_SHADER_FRAGMENT) ?
                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE :
                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
         if (view->base.texture->target == PIPE_BUFFER) {
            d3d12_transition_resource_state(ctx, d3d12_resource(view->base.texture),
                                            state,
                                            D3D12_BIND_INVALIDATE_NONE);
         } else {
            d3d12_transition_subresources_state(ctx, d3d12_resource(view->base.texture),
                                                view->base.u.tex.first_level, view->mip_levels,
                                                view->base.u.tex.first_layer, view->array_size,
                                                d3d12_get_format_start_plane(view->base.format),
                                                d3d12_get_format_num_planes(view->base.format),
                                                state,
                                                D3D12_BIND_INVALIDATE_NONE);
         }
      } else {
         descs[desc_idx] = screen->null_srvs[shader->srv_bindings[i].dimension].cpu_handle;
      }
   }

   d3d12_descriptor_heap_append_handles(batch->view_heap, descs, shader->end_srv_binding - shader->begin_srv_binding);

   return table_start.gpu_handle;
}

static D3D12_GPU_DESCRIPTOR_HANDLE
fill_sampler_descriptors(struct d3d12_context *ctx,
                         const struct d3d12_shader_selector *shader_sel,
                         unsigned stage)
{
   const struct d3d12_shader *shader = shader_sel->current;
   struct d3d12_batch *batch = d3d12_current_batch(ctx);
   D3D12_CPU_DESCRIPTOR_HANDLE descs[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   struct d3d12_descriptor_handle table_start;

   d2d12_descriptor_heap_get_next_handle(batch->sampler_heap, &table_start);

   for (unsigned i = shader->begin_srv_binding; i < shader->end_srv_binding; i++)
   {
      struct d3d12_sampler_state *sampler;

      if (i == shader->pstipple_binding) {
         sampler = ctx->pstipple.sampler_cso;
      } else {
         sampler = ctx->samplers[stage][i];
      }

      unsigned desc_idx = i - shader->begin_srv_binding;
      if (sampler != NULL) {
         if (sampler->is_shadow_sampler && shader_sel->compare_with_lod_bias_grad)
            descs[desc_idx] = sampler->handle_without_shadow.cpu_handle;
         else
            descs[desc_idx] = sampler->handle.cpu_handle;
      } else
         descs[desc_idx] = ctx->null_sampler.cpu_handle;
   }

   d3d12_descriptor_heap_append_handles(batch->sampler_heap, descs, shader->end_srv_binding - shader->begin_srv_binding);
   return table_start.gpu_handle;
}

static unsigned
fill_state_vars(struct d3d12_context *ctx,
                const struct pipe_draw_info *dinfo,
                const struct pipe_draw_start_count_bias *draw,
                struct d3d12_shader *shader,
                uint32_t *values)
{
   unsigned size = 0;

   for (unsigned j = 0; j < shader->num_state_vars; ++j) {
      uint32_t *ptr = values + size;

      switch (shader->state_vars[j].var) {
      case D3D12_STATE_VAR_Y_FLIP:
         ptr[0] = fui(ctx->flip_y);
         size += 4;
         break;
      case D3D12_STATE_VAR_PT_SPRITE:
         ptr[0] = fui(1.0 / ctx->viewports[0].Width);
         ptr[1] = fui(1.0 / ctx->viewports[0].Height);
         ptr[2] = fui(ctx->gfx_pipeline_state.rast->base.point_size);
         ptr[3] = fui(D3D12_MAX_POINT_SIZE);
         size += 4;
         break;
      case D3D12_STATE_VAR_FIRST_VERTEX:
         ptr[0] = dinfo->index_size ? draw->index_bias : draw->start;
         size += 4;
         break;
      case D3D12_STATE_VAR_DEPTH_TRANSFORM:
         ptr[0] = fui(2.0f * ctx->viewport_states[0].scale[2]);
         ptr[1] = fui(ctx->viewport_states[0].translate[2] - ctx->viewport_states[0].scale[2]);
         size += 4;
         break;
      default:
         unreachable("unknown state variable");
      }
   }

   return size;
}

static bool
check_descriptors_left(struct d3d12_context *ctx)
{
   struct d3d12_batch *batch = d3d12_current_batch(ctx);
   unsigned needed_descs = 0;

   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i) {
      struct d3d12_shader_selector *shader = ctx->gfx_stages[i];

      if (!shader)
         continue;

      needed_descs += shader->current->num_cb_bindings;
      needed_descs += shader->current->end_srv_binding - shader->current->begin_srv_binding;
   }

   if (d3d12_descriptor_heap_get_remaining_handles(batch->view_heap) < needed_descs)
      return false;

   needed_descs = 0;
   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i) {
      struct d3d12_shader_selector *shader = ctx->gfx_stages[i];

      if (!shader)
         continue;

      needed_descs += shader->current->end_srv_binding - shader->current->begin_srv_binding;
   }

   if (d3d12_descriptor_heap_get_remaining_handles(batch->sampler_heap) < needed_descs)
      return false;

   return true;
}

#define MAX_DESCRIPTOR_TABLES (D3D12_GFX_SHADER_STAGES * 3)

static unsigned
update_graphics_root_parameters(struct d3d12_context *ctx,
                                const struct pipe_draw_info *dinfo,
                                const struct pipe_draw_start_count_bias *draw,
                                D3D12_GPU_DESCRIPTOR_HANDLE root_desc_tables[MAX_DESCRIPTOR_TABLES],
                                int root_desc_indices[MAX_DESCRIPTOR_TABLES])
{
   unsigned num_params = 0;
   unsigned num_root_desciptors = 0;

   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i) {
      if (!ctx->gfx_stages[i])
         continue;

      struct d3d12_shader_selector *shader_sel = ctx->gfx_stages[i];
      struct d3d12_shader *shader = shader_sel->current;
      uint64_t dirty = ctx->shader_dirty[i];
      assert(shader);

      if (shader->num_cb_bindings > 0) {
         if (dirty & D3D12_SHADER_DIRTY_CONSTBUF) {
            assert(num_root_desciptors < MAX_DESCRIPTOR_TABLES);
            root_desc_tables[num_root_desciptors] = fill_cbv_descriptors(ctx, shader, i);
            root_desc_indices[num_root_desciptors++] = num_params;
         }
         num_params++;
      }
      if (shader->end_srv_binding > 0) {
         if (dirty & D3D12_SHADER_DIRTY_SAMPLER_VIEWS) {
            assert(num_root_desciptors < MAX_DESCRIPTOR_TABLES);
            root_desc_tables[num_root_desciptors] = fill_srv_descriptors(ctx, shader, i);
            root_desc_indices[num_root_desciptors++] = num_params;
         }
         num_params++;
         if (dirty & D3D12_SHADER_DIRTY_SAMPLERS) {
            assert(num_root_desciptors < MAX_DESCRIPTOR_TABLES);
            root_desc_tables[num_root_desciptors] = fill_sampler_descriptors(ctx, shader_sel, i);
            root_desc_indices[num_root_desciptors++] = num_params;
         }
         num_params++;
      }
      /* TODO Don't always update state vars */
      if (shader->num_state_vars > 0) {
         uint32_t constants[D3D12_MAX_STATE_VARS * 4];
         unsigned size = fill_state_vars(ctx, dinfo, draw, shader, constants);
         ctx->cmdlist->SetGraphicsRoot32BitConstants(num_params, size, constants, 0);
         num_params++;
      }
   }
   return num_root_desciptors;
}

static bool
validate_stream_output_targets(struct d3d12_context *ctx)
{
   unsigned factor = 0;

   if (ctx->gfx_pipeline_state.num_so_targets &&
       ctx->gfx_pipeline_state.stages[PIPE_SHADER_GEOMETRY])
      factor = ctx->gfx_pipeline_state.stages[PIPE_SHADER_GEOMETRY]->key.gs.stream_output_factor;

   if (factor > 1)
      return d3d12_enable_fake_so_buffers(ctx, factor);
   else
      return d3d12_disable_fake_so_buffers(ctx);
}

static D3D_PRIMITIVE_TOPOLOGY
topology(enum pipe_prim_type prim_type)
{
   switch (prim_type) {
   case PIPE_PRIM_POINTS:
      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

   case PIPE_PRIM_LINES:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;

   case PIPE_PRIM_LINE_STRIP:
      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;

   case PIPE_PRIM_TRIANGLES:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

   case PIPE_PRIM_TRIANGLE_STRIP:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

   case PIPE_PRIM_LINES_ADJACENCY:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;

   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;

   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;

   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;

/*
   case PIPE_PRIM_PATCHES:
      return D3D_PRIMITIVE_TOPOLOGY_PATCHLIST;
*/

   case PIPE_PRIM_QUADS:
   case PIPE_PRIM_QUAD_STRIP:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; /* HACK: this is just wrong! */

   default:
      debug_printf("pipe_prim_type: %s\n", u_prim_name(prim_type));
      unreachable("unexpected enum pipe_prim_type");
   }
}

static DXGI_FORMAT
ib_format(unsigned index_size)
{
   switch (index_size) {
   case 1: return DXGI_FORMAT_R8_UINT;
   case 2: return DXGI_FORMAT_R16_UINT;
   case 4: return DXGI_FORMAT_R32_UINT;

   default:
      unreachable("unexpected index-buffer size");
   }
}

static void
twoface_emulation(struct d3d12_context *ctx,
                  struct d3d12_rasterizer_state *rast,
                  const struct pipe_draw_info *dinfo,
                  const struct pipe_draw_start_count_bias *draw)
{
   /* draw backfaces */
   ctx->base.bind_rasterizer_state(&ctx->base, rast->twoface_back);
   d3d12_draw_vbo(&ctx->base, dinfo, 0, NULL, draw, 1);

   /* restore real state */
   ctx->base.bind_rasterizer_state(&ctx->base, rast);
}

static void
transition_surface_subresources_state(struct d3d12_context *ctx,
                                      struct pipe_surface *psurf,
                                      struct pipe_resource *pres,
                                      D3D12_RESOURCE_STATES state)
{
   struct d3d12_resource *res = d3d12_resource(pres);
   unsigned start_layer, num_layers;
   if (!d3d12_subresource_id_uses_layer(res->base.target)) {
      start_layer = 0;
      num_layers = 1;
   } else {
      start_layer = psurf->u.tex.first_layer;
      num_layers = psurf->u.tex.last_layer - psurf->u.tex.first_layer + 1;
   }
   d3d12_transition_subresources_state(ctx, res,
                                       psurf->u.tex.level, 1,
                                       start_layer, num_layers,
                                       d3d12_get_format_start_plane(psurf->format),
                                       d3d12_get_format_num_planes(psurf->format),
                                       state,
                                       D3D12_BIND_INVALIDATE_FULL);
}

static bool
prim_supported(enum pipe_prim_type prim_type)
{
   switch (prim_type) {
   case PIPE_PRIM_POINTS:
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_STRIP:
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_LINES_ADJACENCY:
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return true;

   default:
      return false;
   }
}

static inline struct d3d12_shader_selector *
d3d12_last_vertex_stage(struct d3d12_context *ctx)
{
   struct d3d12_shader_selector *sel = ctx->gfx_stages[PIPE_SHADER_GEOMETRY];
   if (!sel || sel->is_gs_variant)
      sel = ctx->gfx_stages[PIPE_SHADER_VERTEX];
   return sel;
}

void
d3d12_draw_vbo(struct pipe_context *pctx,
               const struct pipe_draw_info *dinfo,
               unsigned drawid_offset,
               const struct pipe_draw_indirect_info *indirect,
               const struct pipe_draw_start_count_bias *draws,
               unsigned num_draws)
{
   if (num_draws > 1) {
      util_draw_multi(pctx, dinfo, drawid_offset, indirect, draws, num_draws);
      return;
   }

   if (!indirect && (!draws[0].count || !dinfo->instance_count))
      return;

   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_screen *screen = d3d12_screen(pctx->screen);
   struct d3d12_batch *batch;
   struct pipe_resource *index_buffer = NULL;
   unsigned index_offset = 0;
   enum d3d12_surface_conversion_mode conversion_modes[PIPE_MAX_COLOR_BUFS] = {};

   if (!prim_supported((enum pipe_prim_type)dinfo->mode) ||
       dinfo->index_size == 1 ||
       (dinfo->primitive_restart && dinfo->restart_index != 0xffff &&
        dinfo->restart_index != 0xffffffff)) {

      if (!dinfo->primitive_restart &&
          !u_trim_pipe_prim((enum pipe_prim_type)dinfo->mode, (unsigned *)&draws[0].count))
         return;

      ctx->initial_api_prim = (enum pipe_prim_type)dinfo->mode;
      util_primconvert_save_rasterizer_state(ctx->primconvert, &ctx->gfx_pipeline_state.rast->base);
      util_primconvert_draw_vbo(ctx->primconvert, dinfo, drawid_offset, indirect, draws, num_draws);
      return;
   }

   for (int i = 0; i < ctx->fb.nr_cbufs; ++i) {
      if (ctx->fb.cbufs[i]) {
         struct d3d12_surface *surface = d3d12_surface(ctx->fb.cbufs[i]);
         conversion_modes[i] = d3d12_surface_update_pre_draw(surface, d3d12_rtv_format(ctx, i));
         if (conversion_modes[i] != D3D12_SURFACE_CONVERSION_NONE)
            ctx->cmdlist_dirty |= D3D12_DIRTY_FRAMEBUFFER;
      }
   }

   struct d3d12_rasterizer_state *rast = ctx->gfx_pipeline_state.rast;
   if (rast->twoface_back) {
      enum pipe_prim_type saved_mode = ctx->initial_api_prim;
      twoface_emulation(ctx, rast, dinfo, &draws[0]);
      ctx->initial_api_prim = saved_mode;
   }

   if (ctx->pstipple.enabled)
      ctx->shader_dirty[PIPE_SHADER_FRAGMENT] |= D3D12_SHADER_DIRTY_SAMPLER_VIEWS |
                                                 D3D12_SHADER_DIRTY_SAMPLERS;

   /* this should *really* be fixed at a higher level than here! */
   enum pipe_prim_type reduced_prim = u_reduced_prim((enum pipe_prim_type)dinfo->mode);
   if (reduced_prim == PIPE_PRIM_TRIANGLES &&
       ctx->gfx_pipeline_state.rast->base.cull_face == PIPE_FACE_FRONT_AND_BACK)
      return;

   if (ctx->gfx_pipeline_state.prim_type != dinfo->mode) {
      ctx->gfx_pipeline_state.prim_type = (enum pipe_prim_type)dinfo->mode;
      ctx->state_dirty |= D3D12_DIRTY_PRIM_MODE;
   }

   d3d12_select_shader_variants(ctx, dinfo);
   d3d12_validate_queries(ctx);
   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i) {
      struct d3d12_shader *shader = ctx->gfx_stages[i] ? ctx->gfx_stages[i]->current : NULL;
      if (ctx->gfx_pipeline_state.stages[i] != shader) {
         ctx->gfx_pipeline_state.stages[i] = shader;
         ctx->state_dirty |= D3D12_DIRTY_SHADER;
      }
   }

   /* Reset to an invalid value after it's been used */
   ctx->initial_api_prim = PIPE_PRIM_MAX;

   /* Copy the stream output info from the current vertex/geometry shader */
   if (ctx->state_dirty & D3D12_DIRTY_SHADER) {
      struct d3d12_shader_selector *sel = d3d12_last_vertex_stage(ctx);
      if (sel) {
         ctx->gfx_pipeline_state.so_info = sel->so_info;
      } else {
         memset(&ctx->gfx_pipeline_state.so_info, 0, sizeof(sel->so_info));
      }
   }
   if (!validate_stream_output_targets(ctx)) {
      debug_printf("validate_stream_output_targets() failed\n");
      return;
   }

   D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ib_strip_cut_value =
      D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
   if (dinfo->index_size > 0) {
      assert(dinfo->index_size != 1);

      if (dinfo->has_user_indices) {
         if (!util_upload_index_buffer(pctx, dinfo, &draws[0], &index_buffer,
             &index_offset, 4)) {
            debug_printf("util_upload_index_buffer() failed\n");
            return;
         }
      } else {
         index_buffer = dinfo->index.resource;
      }

      if (dinfo->primitive_restart) {
         assert(dinfo->restart_index == 0xffff ||
                dinfo->restart_index == 0xffffffff);
         ib_strip_cut_value = dinfo->restart_index == 0xffff ?
            D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF :
            D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
      }
   }

   if (ctx->gfx_pipeline_state.ib_strip_cut_value != ib_strip_cut_value) {
      ctx->gfx_pipeline_state.ib_strip_cut_value = ib_strip_cut_value;
      ctx->state_dirty |= D3D12_DIRTY_STRIP_CUT_VALUE;
   }

   if (!ctx->gfx_pipeline_state.root_signature || ctx->state_dirty & D3D12_DIRTY_SHADER) {
      ID3D12RootSignature *root_signature = d3d12_get_root_signature(ctx);
      if (ctx->gfx_pipeline_state.root_signature != root_signature) {
         ctx->gfx_pipeline_state.root_signature = root_signature;
         ctx->state_dirty |= D3D12_DIRTY_ROOT_SIGNATURE;
         for (int i = 0; i < D3D12_GFX_SHADER_STAGES; ++i)
            ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_ALL;
      }
   }

   if (!ctx->current_pso || ctx->state_dirty & D3D12_DIRTY_PSO) {
      ctx->current_pso = d3d12_get_gfx_pipeline_state(ctx);
      assert(ctx->current_pso);
   }

   ctx->cmdlist_dirty |= ctx->state_dirty;

   if (!check_descriptors_left(ctx))
      d3d12_flush_cmdlist(ctx);
   batch = d3d12_current_batch(ctx);

   if (ctx->cmdlist_dirty & D3D12_DIRTY_ROOT_SIGNATURE) {
      d3d12_batch_reference_object(batch, ctx->gfx_pipeline_state.root_signature);
      ctx->cmdlist->SetGraphicsRootSignature(ctx->gfx_pipeline_state.root_signature);
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_PSO) {
      assert(ctx->current_pso);
      d3d12_batch_reference_object(batch, ctx->current_pso);
      ctx->cmdlist->SetPipelineState(ctx->current_pso);
   }

   D3D12_GPU_DESCRIPTOR_HANDLE root_desc_tables[MAX_DESCRIPTOR_TABLES];
   int root_desc_indices[MAX_DESCRIPTOR_TABLES];
   unsigned num_root_desciptors = update_graphics_root_parameters(ctx, dinfo, &draws[0], root_desc_tables, root_desc_indices);

   bool need_zero_one_depth_range = d3d12_need_zero_one_depth_range(ctx);
   if (need_zero_one_depth_range != ctx->need_zero_one_depth_range) {
      ctx->cmdlist_dirty |= D3D12_DIRTY_VIEWPORT;
      ctx->need_zero_one_depth_range = need_zero_one_depth_range;
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_VIEWPORT) {
      if (ctx->need_zero_one_depth_range) {
         D3D12_VIEWPORT viewports[PIPE_MAX_VIEWPORTS];
         for (unsigned i = 0; i < ctx->num_viewports; ++i) {
            viewports[i] = ctx->viewports[i];
            viewports[i].MinDepth = 0.0f;
            viewports[i].MaxDepth = 1.0f;
         }
         ctx->cmdlist->RSSetViewports(ctx->num_viewports, viewports);
      } else
         ctx->cmdlist->RSSetViewports(ctx->num_viewports, ctx->viewports);
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_SCISSOR) {
      if (ctx->gfx_pipeline_state.rast->base.scissor && ctx->num_viewports > 0)
         ctx->cmdlist->RSSetScissorRects(ctx->num_viewports, ctx->scissors);
      else
         ctx->cmdlist->RSSetScissorRects(1, &MAX_SCISSOR);
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_BLEND_COLOR) {
      unsigned blend_factor_flags = ctx->gfx_pipeline_state.blend->blend_factor_flags;
      if (blend_factor_flags & (D3D12_BLEND_FACTOR_COLOR | D3D12_BLEND_FACTOR_ANY)) {
         ctx->cmdlist->OMSetBlendFactor(ctx->blend_factor);
      } else if (blend_factor_flags & D3D12_BLEND_FACTOR_ALPHA) {
         float alpha_const[4] = { ctx->blend_factor[3], ctx->blend_factor[3],
                                 ctx->blend_factor[3], ctx->blend_factor[3] };
         ctx->cmdlist->OMSetBlendFactor(alpha_const);
      }
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_STENCIL_REF)
      ctx->cmdlist->OMSetStencilRef(ctx->stencil_ref.ref_value[0]);

   if (ctx->cmdlist_dirty & D3D12_DIRTY_PRIM_MODE)
      ctx->cmdlist->IASetPrimitiveTopology(topology((enum pipe_prim_type)dinfo->mode));

   for (unsigned i = 0; i < ctx->num_vbs; ++i) {
      if (ctx->vbs[i].buffer.resource) {
         struct d3d12_resource *res = d3d12_resource(ctx->vbs[i].buffer.resource);
         d3d12_transition_resource_state(ctx, res, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_BIND_INVALIDATE_NONE);
         if (ctx->cmdlist_dirty & D3D12_DIRTY_VERTEX_BUFFERS)
            d3d12_batch_reference_resource(batch, res);
      }
   }
   if (ctx->cmdlist_dirty & D3D12_DIRTY_VERTEX_BUFFERS)
      ctx->cmdlist->IASetVertexBuffers(0, ctx->num_vbs, ctx->vbvs);

   if (index_buffer) {
      D3D12_INDEX_BUFFER_VIEW ibv;
      struct d3d12_resource *res = d3d12_resource(index_buffer);
      ibv.BufferLocation = d3d12_resource_gpu_virtual_address(res) + index_offset;
      ibv.SizeInBytes = res->base.width0 - index_offset;
      ibv.Format = ib_format(dinfo->index_size);
      d3d12_transition_resource_state(ctx, res, D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_BIND_INVALIDATE_NONE);
      if (ctx->cmdlist_dirty & D3D12_DIRTY_INDEX_BUFFER ||
          memcmp(&ctx->ibv, &ibv, sizeof(D3D12_INDEX_BUFFER_VIEW)) != 0) {
         ctx->ibv = ibv;
         d3d12_batch_reference_resource(batch, res);
         ctx->cmdlist->IASetIndexBuffer(&ibv);
      }

      if (dinfo->has_user_indices)
         pipe_resource_reference(&index_buffer, NULL);
   }

   if (ctx->cmdlist_dirty & D3D12_DIRTY_FRAMEBUFFER) {
      D3D12_CPU_DESCRIPTOR_HANDLE render_targets[PIPE_MAX_COLOR_BUFS] = {};
      D3D12_CPU_DESCRIPTOR_HANDLE *depth_desc = NULL, tmp_desc;
      for (int i = 0; i < ctx->fb.nr_cbufs; ++i) {
         if (ctx->fb.cbufs[i]) {
            struct d3d12_surface *surface = d3d12_surface(ctx->fb.cbufs[i]);
            render_targets[i] = d3d12_surface_get_handle(surface, conversion_modes[i]);
            d3d12_batch_reference_surface_texture(batch, surface);
         } else
            render_targets[i] = screen->null_rtv.cpu_handle;
      }
      if (ctx->fb.zsbuf) {
         struct d3d12_surface *surface = d3d12_surface(ctx->fb.zsbuf);
         tmp_desc = surface->desc_handle.cpu_handle;
         d3d12_batch_reference_surface_texture(batch, surface);
         depth_desc = &tmp_desc;
      }
      ctx->cmdlist->OMSetRenderTargets(ctx->fb.nr_cbufs, render_targets, FALSE, depth_desc);
   }

   struct pipe_stream_output_target **so_targets = ctx->fake_so_buffer_factor ? ctx->fake_so_targets
                                                                              : ctx->so_targets;
   D3D12_STREAM_OUTPUT_BUFFER_VIEW *so_buffer_views = ctx->fake_so_buffer_factor ? ctx->fake_so_buffer_views
                                                                                 : ctx->so_buffer_views;
   for (unsigned i = 0; i < ctx->gfx_pipeline_state.num_so_targets; ++i) {
      struct d3d12_stream_output_target *target = (struct d3d12_stream_output_target *)so_targets[i];

      if (!target)
         continue;

      struct d3d12_resource *so_buffer = d3d12_resource(target->base.buffer);
      struct d3d12_resource *fill_buffer = d3d12_resource(target->fill_buffer);

      d3d12_resource_make_writeable(pctx, target->base.buffer);

      if (ctx->cmdlist_dirty & D3D12_DIRTY_STREAM_OUTPUT) {
         d3d12_batch_reference_resource(batch, so_buffer);
         d3d12_batch_reference_resource(batch, fill_buffer);
      }

      d3d12_transition_resource_state(ctx, so_buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_BIND_INVALIDATE_NONE);
      d3d12_transition_resource_state(ctx, fill_buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_BIND_INVALIDATE_NONE);
   }
   if (ctx->cmdlist_dirty & D3D12_DIRTY_STREAM_OUTPUT)
      ctx->cmdlist->SOSetTargets(0, 4, so_buffer_views);

   for (int i = 0; i < ctx->fb.nr_cbufs; ++i) {
      struct pipe_surface *psurf = ctx->fb.cbufs[i];
      if (!psurf)
         continue;

      struct pipe_resource *pres = conversion_modes[i] == D3D12_SURFACE_CONVERSION_BGRA_UINT ?
                                      d3d12_surface(psurf)->rgba_texture : psurf->texture;
      transition_surface_subresources_state(ctx, psurf, pres,
         D3D12_RESOURCE_STATE_RENDER_TARGET);
   }
   if (ctx->fb.zsbuf) {
      struct pipe_surface *psurf = ctx->fb.zsbuf;
      transition_surface_subresources_state(ctx, psurf, psurf->texture,
         D3D12_RESOURCE_STATE_DEPTH_WRITE);
   }

   d3d12_apply_resource_states(ctx);

   for (unsigned i = 0; i < num_root_desciptors; ++i)
      ctx->cmdlist->SetGraphicsRootDescriptorTable(root_desc_indices[i], root_desc_tables[i]);

   if (dinfo->index_size > 0)
      ctx->cmdlist->DrawIndexedInstanced(draws[0].count, dinfo->instance_count,
                                         draws[0].start, draws[0].index_bias,
                                         dinfo->start_instance);
   else
      ctx->cmdlist->DrawInstanced(draws[0].count, dinfo->instance_count,
                                  draws[0].start, dinfo->start_instance);

   ctx->state_dirty = 0;

   if (index_buffer)
      ctx->cmdlist_dirty = 0;
   else
      ctx->cmdlist_dirty &= D3D12_DIRTY_INDEX_BUFFER;

   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i)
      ctx->shader_dirty[i] = 0;

   for (int i = 0; i < ctx->fb.nr_cbufs; ++i) {
      if (ctx->fb.cbufs[i]) {
         struct d3d12_surface *surface = d3d12_surface(ctx->fb.cbufs[i]);
         d3d12_surface_update_post_draw(surface, conversion_modes[i]);
      }
   }
}
