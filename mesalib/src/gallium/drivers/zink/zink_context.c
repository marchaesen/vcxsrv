/*
 * Copyright 2018 Collabora Ltd.
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

#include "zink_context.h"

#include "zink_batch.h"
#include "zink_compiler.h"
#include "zink_fence.h"
#include "zink_format.h"
#include "zink_framebuffer.h"
#include "zink_helpers.h"
#include "zink_program.h"
#include "zink_pipeline.h"
#include "zink_query.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"
#include "zink_surface.h"
#include "zink_inlines.h"

#include "util/u_blitter.h"
#include "util/u_debug.h"
#include "util/format_srgb.h"
#include "util/format/u_format.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_thread.h"
#include "util/u_cpu_detect.h"
#include "util/strndup.h"
#include "nir.h"

#include "util/u_memory.h"
#include "util/u_upload_mgr.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

static void
calc_descriptor_hash_sampler_state(struct zink_sampler_state *sampler_state)
{
   void *hash_data = &sampler_state->sampler;
   size_t data_size = sizeof(VkSampler);
   sampler_state->hash = XXH32(hash_data, data_size, 0);
}

void
debug_describe_zink_buffer_view(char *buf, const struct zink_buffer_view *ptr)
{
   sprintf(buf, "zink_buffer_view");
}

ALWAYS_INLINE static void
check_resource_for_batch_ref(struct zink_context *ctx, struct zink_resource *res)
{
   if (!zink_resource_has_binds(res))
      zink_batch_reference_resource(&ctx->batch, res);
}

static void
zink_context_destroy(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);

   if (util_queue_is_initialized(&screen->flush_queue))
      util_queue_finish(&screen->flush_queue);
   if (ctx->batch.state && !screen->device_lost && VKSCR(QueueWaitIdle)(ctx->batch.state->queue) != VK_SUCCESS)
      debug_printf("vkQueueWaitIdle failed\n");

   util_blitter_destroy(ctx->blitter);
   for (unsigned i = 0; i < ctx->fb_state.nr_cbufs; i++)
      pipe_surface_release(&ctx->base, &ctx->fb_state.cbufs[i]);
   pipe_surface_release(&ctx->base, &ctx->fb_state.zsbuf);

   pipe_resource_reference(&ctx->dummy_vertex_buffer, NULL);
   pipe_resource_reference(&ctx->dummy_xfb_buffer, NULL);

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->dummy_surface); i++)
      pipe_surface_release(&ctx->base, &ctx->dummy_surface[i]);
   zink_buffer_view_reference(screen, &ctx->dummy_bufferview, NULL);

   if (ctx->dd)
      zink_descriptors_deinit_bindless(ctx);

   simple_mtx_destroy(&ctx->batch_mtx);
   if (ctx->batch.state) {
      zink_clear_batch_state(ctx, ctx->batch.state);
      zink_batch_state_destroy(screen, ctx->batch.state);
   }
   struct zink_batch_state *bs = ctx->batch_states;
   while (bs) {
      struct zink_batch_state *bs_next = bs->next;
      zink_clear_batch_state(ctx, bs);
      zink_batch_state_destroy(screen, bs);
      bs = bs_next;
   }
   util_dynarray_foreach(&ctx->free_batch_states, struct zink_batch_state*, bs) {
      zink_clear_batch_state(ctx, *bs);
      zink_batch_state_destroy(screen, *bs);
   }

   for (unsigned i = 0; i < 2; i++) {
      util_idalloc_fini(&ctx->di.bindless[i].tex_slots);
      util_idalloc_fini(&ctx->di.bindless[i].img_slots);
      free(ctx->di.bindless[i].buffer_infos);
      free(ctx->di.bindless[i].img_infos);
      util_dynarray_fini(&ctx->di.bindless[i].updates);
      util_dynarray_fini(&ctx->di.bindless[i].resident);
   }

   if (screen->info.have_KHR_imageless_framebuffer) {
      hash_table_foreach(&ctx->framebuffer_cache, he)
         zink_destroy_framebuffer(screen, he->data);
   } else if (ctx->framebuffer) {
      simple_mtx_lock(&screen->framebuffer_mtx);
      struct hash_entry *entry = _mesa_hash_table_search(&screen->framebuffer_cache, &ctx->framebuffer->state);
      if (zink_framebuffer_reference(screen, &ctx->framebuffer, NULL))
         _mesa_hash_table_remove(&screen->framebuffer_cache, entry);
      simple_mtx_unlock(&screen->framebuffer_mtx);
   }

   hash_table_foreach(ctx->render_pass_cache, he)
      zink_destroy_render_pass(screen, he->data);

   u_upload_destroy(pctx->stream_uploader);
   u_upload_destroy(pctx->const_uploader);
   slab_destroy_child(&ctx->transfer_pool);
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->program_cache); i++)
      _mesa_hash_table_clear(&ctx->program_cache[i], NULL);
   _mesa_hash_table_clear(&ctx->compute_program_cache, NULL);
   _mesa_hash_table_destroy(ctx->render_pass_cache, NULL);
   slab_destroy_child(&ctx->transfer_pool_unsync);

   if (ctx->dd)
      screen->descriptors_deinit(ctx);

   zink_descriptor_layouts_deinit(ctx);

   p_atomic_dec(&screen->base.num_contexts);

   ralloc_free(ctx);
}

static void
check_device_lost(struct zink_context *ctx)
{
   if (!zink_screen(ctx->base.screen)->device_lost || ctx->is_device_lost)
      return;
   debug_printf("ZINK: device lost detected!\n");
   if (ctx->reset.reset)
      ctx->reset.reset(ctx->reset.data, PIPE_GUILTY_CONTEXT_RESET);
   ctx->is_device_lost = true;
}

static enum pipe_reset_status
zink_get_device_reset_status(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);

   enum pipe_reset_status status = PIPE_NO_RESET;

   if (ctx->is_device_lost) {
      // Since we don't know what really happened to the hardware, just
      // assume that we are in the wrong
      status = PIPE_GUILTY_CONTEXT_RESET;

      debug_printf("ZINK: device lost detected!\n");

      if (ctx->reset.reset)
         ctx->reset.reset(ctx->reset.data, status);
   }

   return status;
}

static void
zink_set_device_reset_callback(struct pipe_context *pctx,
                               const struct pipe_device_reset_callback *cb)
{
   struct zink_context *ctx = zink_context(pctx);

   if (cb)
      ctx->reset = *cb;
   else
      memset(&ctx->reset, 0, sizeof(ctx->reset));
}

static void
zink_set_context_param(struct pipe_context *pctx, enum pipe_context_param param,
                       unsigned value)
{
   struct zink_context *ctx = zink_context(pctx);

   switch (param) {
   case PIPE_CONTEXT_PARAM_PIN_THREADS_TO_L3_CACHE:
      util_set_thread_affinity(zink_screen(ctx->base.screen)->flush_queue.threads[0],
                               util_get_cpu_caps()->L3_affinity_mask[value],
                               NULL, util_get_cpu_caps()->num_cpu_mask_bits);
      break;
   default:
      break;
   }
}

static VkSamplerMipmapMode
sampler_mipmap_mode(enum pipe_tex_mipfilter filter)
{
   switch (filter) {
   case PIPE_TEX_MIPFILTER_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
   case PIPE_TEX_MIPFILTER_LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
   case PIPE_TEX_MIPFILTER_NONE:
      unreachable("PIPE_TEX_MIPFILTER_NONE should be dealt with earlier");
   }
   unreachable("unexpected filter");
}

static VkSamplerAddressMode
sampler_address_mode(enum pipe_tex_wrap filter)
{
   switch (filter) {
   case PIPE_TEX_WRAP_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; /* not technically correct, but kinda works */
   default: break;
   }
   unreachable("unexpected wrap");
}

static VkCompareOp
compare_op(enum pipe_compare_func op)
{
   switch (op) {
      case PIPE_FUNC_NEVER: return VK_COMPARE_OP_NEVER;
      case PIPE_FUNC_LESS: return VK_COMPARE_OP_LESS;
      case PIPE_FUNC_EQUAL: return VK_COMPARE_OP_EQUAL;
      case PIPE_FUNC_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
      case PIPE_FUNC_GREATER: return VK_COMPARE_OP_GREATER;
      case PIPE_FUNC_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
      case PIPE_FUNC_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
      case PIPE_FUNC_ALWAYS: return VK_COMPARE_OP_ALWAYS;
   }
   unreachable("unexpected compare");
}

static inline bool
wrap_needs_border_color(unsigned wrap)
{
   return wrap == PIPE_TEX_WRAP_CLAMP || wrap == PIPE_TEX_WRAP_CLAMP_TO_BORDER ||
          wrap == PIPE_TEX_WRAP_MIRROR_CLAMP || wrap == PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
}

static VkBorderColor
get_border_color(const union pipe_color_union *color, bool is_integer, bool need_custom)
{
   if (is_integer) {
      if (color->ui[0] == 0 && color->ui[1] == 0 && color->ui[2] == 0 && color->ui[3] == 0)
         return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
      if (color->ui[0] == 0 && color->ui[1] == 0 && color->ui[2] == 0 && color->ui[3] == 1)
         return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
      if (color->ui[0] == 1 && color->ui[1] == 1 && color->ui[2] == 1 && color->ui[3] == 1)
         return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
      return need_custom ? VK_BORDER_COLOR_INT_CUSTOM_EXT : VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
   }

   if (color->f[0] == 0 && color->f[1] == 0 && color->f[2] == 0 && color->f[3] == 0)
      return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
   if (color->f[0] == 0 && color->f[1] == 0 && color->f[2] == 0 && color->f[3] == 1)
      return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
   if (color->f[0] == 1 && color->f[1] == 1 && color->f[2] == 1 && color->f[3] == 1)
      return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
   return need_custom ? VK_BORDER_COLOR_FLOAT_CUSTOM_EXT : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

static void *
zink_create_sampler_state(struct pipe_context *pctx,
                          const struct pipe_sampler_state *state)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   bool need_custom = false;

   VkSamplerCreateInfo sci = {0};
   VkSamplerCustomBorderColorCreateInfoEXT cbci = {0};
   sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
   sci.unnormalizedCoordinates = !state->normalized_coords;
   sci.magFilter = zink_filter(state->mag_img_filter);
   if (sci.unnormalizedCoordinates)
      sci.minFilter = sci.magFilter;
   else
      sci.minFilter = zink_filter(state->min_img_filter);

   VkSamplerReductionModeCreateInfo rci;
   rci.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO;
   rci.pNext = NULL;
   switch (state->reduction_mode) {
   case PIPE_TEX_REDUCTION_MIN:
      rci.reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN;
      break;
   case PIPE_TEX_REDUCTION_MAX:
      rci.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX;
      break;
   default:
      rci.reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
      break;
   }
   if (state->reduction_mode)
      sci.pNext = &rci;

   if (sci.unnormalizedCoordinates) {
      sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
   } else if (state->min_mip_filter != PIPE_TEX_MIPFILTER_NONE) {
      sci.mipmapMode = sampler_mipmap_mode(state->min_mip_filter);
      sci.minLod = state->min_lod;
      sci.maxLod = state->max_lod;
   } else {
      sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      sci.minLod = 0;
      sci.maxLod = 0.25f;
   }

   if (!sci.unnormalizedCoordinates) {
      sci.addressModeU = sampler_address_mode(state->wrap_s);
      sci.addressModeV = sampler_address_mode(state->wrap_t);
      sci.addressModeW = sampler_address_mode(state->wrap_r);
   } else {
      sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   }
   sci.mipLodBias = state->lod_bias;

   need_custom |= wrap_needs_border_color(state->wrap_s);
   need_custom |= wrap_needs_border_color(state->wrap_t);
   need_custom |= wrap_needs_border_color(state->wrap_r);

   if (state->compare_mode == PIPE_TEX_COMPARE_NONE)
      sci.compareOp = VK_COMPARE_OP_NEVER;
   else {
      sci.compareOp = compare_op(state->compare_func);
      sci.compareEnable = VK_TRUE;
   }

   bool is_integer = state->border_color_is_integer;

   sci.borderColor = get_border_color(&state->border_color, is_integer, need_custom);
   if (sci.borderColor > VK_BORDER_COLOR_INT_OPAQUE_WHITE && need_custom) {
      if (screen->info.have_EXT_custom_border_color &&
          screen->info.border_color_feats.customBorderColorWithoutFormat) {
         cbci.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
         cbci.format = VK_FORMAT_UNDEFINED;
         /* these are identical unions */
         memcpy(&cbci.customBorderColor, &state->border_color, sizeof(union pipe_color_union));
         cbci.pNext = sci.pNext;
         sci.pNext = &cbci;
         UNUSED uint32_t check = p_atomic_inc_return(&screen->cur_custom_border_color_samplers);
         assert(check <= screen->info.border_color_props.maxCustomBorderColorSamplers);
      } else
         sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // TODO with custom shader if we're super interested?
      if (sci.unnormalizedCoordinates)
         sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
   }

   if (state->max_anisotropy > 1) {
      sci.maxAnisotropy = state->max_anisotropy;
      sci.anisotropyEnable = VK_TRUE;
   }

   struct zink_sampler_state *sampler = CALLOC_STRUCT(zink_sampler_state);
   if (!sampler)
      return NULL;

   if (VKSCR(CreateSampler)(screen->dev, &sci, NULL, &sampler->sampler) != VK_SUCCESS) {
      FREE(sampler);
      return NULL;
   }
   util_dynarray_init(&sampler->desc_set_refs.refs, NULL);
   calc_descriptor_hash_sampler_state(sampler);
   sampler->custom_border_color = need_custom;

   return sampler;
}

ALWAYS_INLINE static VkImageLayout
get_layout_for_binding(struct zink_resource *res, enum zink_descriptor_type type, bool is_compute)
{
   if (res->obj->is_buffer)
      return 0;
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return res->image_bind_count[is_compute] ?
             VK_IMAGE_LAYOUT_GENERAL :
             res->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) ?
                //Vulkan-Docs#1490
                //(res->aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL :
                 //res->aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL :
                (res->aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                 res->aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) :
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return VK_IMAGE_LAYOUT_GENERAL;
   default:
      break;
   }
   return 0;
}

ALWAYS_INLINE static struct zink_surface *
get_imageview_for_binding(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type, unsigned idx)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW: {
      struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->sampler_views[stage][idx]);
      return sampler_view->base.texture ? sampler_view->image_view : NULL;
   }
   case ZINK_DESCRIPTOR_TYPE_IMAGE: {
      struct zink_image_view *image_view = &ctx->image_views[stage][idx];
      return image_view->base.resource ? image_view->surface : NULL;
   }
   default:
      break;
   }
   unreachable("ACK");
   return VK_NULL_HANDLE;
}

ALWAYS_INLINE static struct zink_buffer_view *
get_bufferview_for_binding(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type, unsigned idx)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW: {
      struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->sampler_views[stage][idx]);
      return sampler_view->base.texture ? sampler_view->buffer_view : NULL;
   }
   case ZINK_DESCRIPTOR_TYPE_IMAGE: {
      struct zink_image_view *image_view = &ctx->image_views[stage][idx];
      return image_view->base.resource ? image_view->buffer_view : NULL;
   }
   default:
      break;
   }
   unreachable("ACK");
   return VK_NULL_HANDLE;
}

ALWAYS_INLINE static struct zink_resource *
update_descriptor_state_ubo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_null_descriptors = screen->info.rb2_feats.nullDescriptor;
   const enum zink_descriptor_type type = ZINK_DESCRIPTOR_TYPE_UBO;
   ctx->di.descriptor_res[type][shader][slot] = res;
   ctx->di.ubos[shader][slot].offset = ctx->ubos[shader][slot].buffer_offset;
   if (res) {
      ctx->di.ubos[shader][slot].buffer = res->obj->buffer;
      ctx->di.ubos[shader][slot].range = ctx->ubos[shader][slot].buffer_size;
      assert(ctx->di.ubos[shader][slot].range <= screen->info.props.limits.maxUniformBufferRange);
   } else {
      VkBuffer null_buffer = zink_resource(ctx->dummy_vertex_buffer)->obj->buffer;
      ctx->di.ubos[shader][slot].buffer = have_null_descriptors ? VK_NULL_HANDLE : null_buffer;
      ctx->di.ubos[shader][slot].range = VK_WHOLE_SIZE;
   }
   if (!slot) {
      if (res)
         ctx->di.push_valid |= BITFIELD64_BIT(shader);
      else
         ctx->di.push_valid &= ~BITFIELD64_BIT(shader);
   }
   return res;
}

ALWAYS_INLINE static struct zink_resource *
update_descriptor_state_ssbo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_null_descriptors = screen->info.rb2_feats.nullDescriptor;
   const enum zink_descriptor_type type = ZINK_DESCRIPTOR_TYPE_SSBO;
   ctx->di.descriptor_res[type][shader][slot] = res;
   ctx->di.ssbos[shader][slot].offset = ctx->ssbos[shader][slot].buffer_offset;
   if (res) {
      ctx->di.ssbos[shader][slot].buffer = res->obj->buffer;
      ctx->di.ssbos[shader][slot].range = ctx->ssbos[shader][slot].buffer_size;
   } else {
      VkBuffer null_buffer = zink_resource(ctx->dummy_vertex_buffer)->obj->buffer;
      ctx->di.ssbos[shader][slot].buffer = have_null_descriptors ? VK_NULL_HANDLE : null_buffer;
      ctx->di.ssbos[shader][slot].range = VK_WHOLE_SIZE;
   }
   return res;
}

ALWAYS_INLINE static struct zink_resource *
update_descriptor_state_sampler(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_null_descriptors = screen->info.rb2_feats.nullDescriptor;
   const enum zink_descriptor_type type = ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW;
   ctx->di.descriptor_res[type][shader][slot] = res;
   if (res) {
      if (res->obj->is_buffer) {
         struct zink_buffer_view *bv = get_bufferview_for_binding(ctx, shader, type, slot);
         ctx->di.tbos[shader][slot] = bv->buffer_view;
         ctx->di.sampler_surfaces[shader][slot].bufferview = bv;
         ctx->di.sampler_surfaces[shader][slot].is_buffer = true;
      } else {
         struct zink_surface *surface = get_imageview_for_binding(ctx, shader, type, slot);
         ctx->di.textures[shader][slot].imageLayout = get_layout_for_binding(res, type, shader == PIPE_SHADER_COMPUTE);
         ctx->di.textures[shader][slot].imageView = surface->image_view;
         ctx->di.sampler_surfaces[shader][slot].surface = surface;
         ctx->di.sampler_surfaces[shader][slot].is_buffer = false;
      }
   } else {
      if (likely(have_null_descriptors)) {
         ctx->di.textures[shader][slot].imageView = VK_NULL_HANDLE;
         ctx->di.textures[shader][slot].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         ctx->di.tbos[shader][slot] = VK_NULL_HANDLE;
      } else {
         struct zink_surface *null_surface = zink_csurface(ctx->dummy_surface[0]);
         struct zink_buffer_view *null_bufferview = ctx->dummy_bufferview;
         ctx->di.textures[shader][slot].imageView = null_surface->image_view;
         ctx->di.textures[shader][slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         ctx->di.tbos[shader][slot] = null_bufferview->buffer_view;
      }
      memset(&ctx->di.sampler_surfaces[shader][slot], 0, sizeof(ctx->di.sampler_surfaces[shader][slot]));
   }
   return res;
}

ALWAYS_INLINE static struct zink_resource *
update_descriptor_state_image(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_null_descriptors = screen->info.rb2_feats.nullDescriptor;
   const enum zink_descriptor_type type = ZINK_DESCRIPTOR_TYPE_IMAGE;
   ctx->di.descriptor_res[type][shader][slot] = res;
   if (res) {
      if (res->obj->is_buffer) {
         struct zink_buffer_view *bv = get_bufferview_for_binding(ctx, shader, type, slot);
         ctx->di.texel_images[shader][slot] = bv->buffer_view;
         ctx->di.image_surfaces[shader][slot].bufferview = bv;
         ctx->di.image_surfaces[shader][slot].is_buffer = true;
      } else {
         struct zink_surface *surface = get_imageview_for_binding(ctx, shader, type, slot);
         ctx->di.images[shader][slot].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
         ctx->di.images[shader][slot].imageView = surface->image_view;
         ctx->di.image_surfaces[shader][slot].surface = surface;
         ctx->di.image_surfaces[shader][slot].is_buffer = false;
      }
   } else {
      if (likely(have_null_descriptors)) {
         memset(&ctx->di.images[shader][slot], 0, sizeof(ctx->di.images[shader][slot]));
         ctx->di.texel_images[shader][slot] = VK_NULL_HANDLE;
      } else {
         struct zink_surface *null_surface = zink_csurface(ctx->dummy_surface[0]);
         struct zink_buffer_view *null_bufferview = ctx->dummy_bufferview;
         ctx->di.images[shader][slot].imageView = null_surface->image_view;
         ctx->di.images[shader][slot].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
         ctx->di.texel_images[shader][slot] = null_bufferview->buffer_view;
      }
      memset(&ctx->di.image_surfaces[shader][slot], 0, sizeof(ctx->di.image_surfaces[shader][slot]));
   }
   return res;
}

static void
zink_bind_sampler_states(struct pipe_context *pctx,
                         enum pipe_shader_type shader,
                         unsigned start_slot,
                         unsigned num_samplers,
                         void **samplers)
{
   struct zink_context *ctx = zink_context(pctx);
   for (unsigned i = 0; i < num_samplers; ++i) {
      struct zink_sampler_state *state = samplers[i];
      if (ctx->sampler_states[shader][start_slot + i] != state)
         zink_screen(pctx->screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, start_slot, 1);
      ctx->sampler_states[shader][start_slot + i] = state;
      ctx->di.textures[shader][start_slot + i].sampler = state ? state->sampler : VK_NULL_HANDLE;
      if (state)
         zink_batch_usage_set(&state->batch_uses, ctx->batch.state);
   }
   ctx->di.num_samplers[shader] = start_slot + num_samplers;
}

static void
zink_delete_sampler_state(struct pipe_context *pctx,
                          void *sampler_state)
{
   struct zink_sampler_state *sampler = sampler_state;
   struct zink_batch *batch = &zink_context(pctx)->batch;
   zink_descriptor_set_refs_clear(&sampler->desc_set_refs, sampler_state);
   /* may be called if context_create fails */
   if (batch->state)
      util_dynarray_append(&batch->state->zombie_samplers, VkSampler,
                           sampler->sampler);
   if (sampler->custom_border_color)
      p_atomic_dec(&zink_screen(pctx->screen)->cur_custom_border_color_samplers);
   FREE(sampler);
}

static VkImageAspectFlags
sampler_aspect_from_format(enum pipe_format fmt)
{
   if (util_format_is_depth_or_stencil(fmt)) {
      const struct util_format_description *desc = util_format_description(fmt);
      if (util_format_has_depth(desc))
         return VK_IMAGE_ASPECT_DEPTH_BIT;
      assert(util_format_has_stencil(desc));
      return VK_IMAGE_ASPECT_STENCIL_BIT;
   } else
     return VK_IMAGE_ASPECT_COLOR_BIT;
}

static uint32_t
hash_bufferview(void *bvci)
{
   size_t offset = offsetof(VkBufferViewCreateInfo, flags);
   return _mesa_hash_data((char*)bvci + offset, sizeof(VkBufferViewCreateInfo) - offset);
}

static VkBufferViewCreateInfo
create_bvci(struct zink_context *ctx, struct zink_resource *res, enum pipe_format format, uint32_t offset, uint32_t range)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkBufferViewCreateInfo bvci;
   // Zero whole struct (including alignment holes), so hash_bufferview
   // does not access potentially uninitialized data.
   memset(&bvci, 0, sizeof(bvci));
   bvci.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
   bvci.pNext = NULL;
   if (screen->format_props[format].bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)
      bvci.buffer = res->obj->storage_buffer ? res->obj->storage_buffer : res->obj->buffer;
   else
      bvci.buffer = res->obj->buffer;
   bvci.format = zink_get_format(screen, format);
   assert(bvci.format);
   bvci.offset = offset;
   bvci.range = !offset && range == res->base.b.width0 ? VK_WHOLE_SIZE : range;
   uint32_t clamp = util_format_get_blocksize(format) * screen->info.props.limits.maxTexelBufferElements;
   if (bvci.range == VK_WHOLE_SIZE && res->base.b.width0 > clamp)
      bvci.range = clamp;
   bvci.flags = 0;
   return bvci;
}

static struct zink_buffer_view *
get_buffer_view(struct zink_context *ctx, struct zink_resource *res, VkBufferViewCreateInfo *bvci)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_buffer_view *buffer_view = NULL;

   uint32_t hash = hash_bufferview(bvci);
   simple_mtx_lock(&res->bufferview_mtx);
   struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&res->bufferview_cache, hash, bvci);
   if (he) {
      buffer_view = he->data;
      p_atomic_inc(&buffer_view->reference.count);
   } else {
      VkBufferView view;
      if (VKSCR(CreateBufferView)(screen->dev, bvci, NULL, &view) != VK_SUCCESS)
         goto out;
      buffer_view = CALLOC_STRUCT(zink_buffer_view);
      if (!buffer_view) {
         VKSCR(DestroyBufferView)(screen->dev, view, NULL);
         goto out;
      }
      pipe_reference_init(&buffer_view->reference, 1);
      pipe_resource_reference(&buffer_view->pres, &res->base.b);
      util_dynarray_init(&buffer_view->desc_set_refs.refs, NULL);
      buffer_view->bvci = *bvci;
      buffer_view->buffer_view = view;
      buffer_view->hash = hash;
      _mesa_hash_table_insert_pre_hashed(&res->bufferview_cache, hash, &buffer_view->bvci, buffer_view);
   }
out:
   simple_mtx_unlock(&res->bufferview_mtx);
   return buffer_view;
}

enum pipe_swizzle
zink_clamp_void_swizzle(const struct util_format_description *desc, enum pipe_swizzle swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_X:
   case PIPE_SWIZZLE_Y:
   case PIPE_SWIZZLE_Z:
   case PIPE_SWIZZLE_W:
      return desc->channel[swizzle].type == UTIL_FORMAT_TYPE_VOID ? PIPE_SWIZZLE_1 : swizzle;
   default:
      break;
   }
   return swizzle;
}

ALWAYS_INLINE static enum pipe_swizzle
clamp_zs_swizzle(enum pipe_swizzle swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_X:
   case PIPE_SWIZZLE_Y:
   case PIPE_SWIZZLE_Z:
   case PIPE_SWIZZLE_W:
      return PIPE_SWIZZLE_X;
   default:
      break;
   }
   return swizzle;
}

static struct pipe_sampler_view *
zink_create_sampler_view(struct pipe_context *pctx, struct pipe_resource *pres,
                         const struct pipe_sampler_view *state)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   struct zink_context *ctx = zink_context(pctx);
   struct zink_sampler_view *sampler_view = CALLOC_STRUCT_CL(zink_sampler_view);
   bool err;

   sampler_view->base = *state;
   sampler_view->base.texture = NULL;
   pipe_resource_reference(&sampler_view->base.texture, pres);
   sampler_view->base.reference.count = 1;
   sampler_view->base.context = pctx;

   if (state->target != PIPE_BUFFER) {
      VkImageViewCreateInfo ivci;

      struct pipe_surface templ = {0};
      templ.u.tex.level = state->u.tex.first_level;
      templ.format = state->format;
      if (state->target != PIPE_TEXTURE_3D) {
         templ.u.tex.first_layer = state->u.tex.first_layer;
         templ.u.tex.last_layer = state->u.tex.last_layer;
      }

      ivci = create_ivci(screen, res, &templ, state->target);
      ivci.subresourceRange.levelCount = state->u.tex.last_level - state->u.tex.first_level + 1;
      ivci.subresourceRange.aspectMask = sampler_aspect_from_format(state->format);
      /* samplers for stencil aspects of packed formats need to always use stencil swizzle */
      if (ivci.subresourceRange.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
         ivci.components.r = zink_component_mapping(clamp_zs_swizzle(sampler_view->base.swizzle_r));
         ivci.components.g = zink_component_mapping(clamp_zs_swizzle(sampler_view->base.swizzle_g));
         ivci.components.b = zink_component_mapping(clamp_zs_swizzle(sampler_view->base.swizzle_b));
         ivci.components.a = zink_component_mapping(clamp_zs_swizzle(sampler_view->base.swizzle_a));
      } else {
         /* if we have e.g., R8G8B8X8, then we have to ignore alpha since we're just emulating
          * these formats
          */
          if (zink_format_is_voidable_rgba_variant(state->format)) {
             const struct util_format_description *desc = util_format_description(state->format);
             sampler_view->base.swizzle_r = zink_clamp_void_swizzle(desc, sampler_view->base.swizzle_r);
             sampler_view->base.swizzle_g = zink_clamp_void_swizzle(desc, sampler_view->base.swizzle_g);
             sampler_view->base.swizzle_b = zink_clamp_void_swizzle(desc, sampler_view->base.swizzle_b);
             sampler_view->base.swizzle_a = zink_clamp_void_swizzle(desc, sampler_view->base.swizzle_a);
          }
          ivci.components.r = zink_component_mapping(sampler_view->base.swizzle_r);
          ivci.components.g = zink_component_mapping(sampler_view->base.swizzle_g);
          ivci.components.b = zink_component_mapping(sampler_view->base.swizzle_b);
          ivci.components.a = zink_component_mapping(sampler_view->base.swizzle_a);
      }
      assert(ivci.format);

      sampler_view->image_view = (struct zink_surface*)zink_get_surface(ctx, pres, &templ, &ivci);
      err = !sampler_view->image_view;
   } else {
      VkBufferViewCreateInfo bvci = create_bvci(ctx, res, state->format, state->u.buf.offset, state->u.buf.size);
      sampler_view->buffer_view = get_buffer_view(ctx, res, &bvci);
      err = !sampler_view->buffer_view;
   }
   if (err) {
      FREE_CL(sampler_view);
      return NULL;
   }
   return &sampler_view->base;
}

void
zink_destroy_buffer_view(struct zink_screen *screen, struct zink_buffer_view *buffer_view)
{
   struct zink_resource *res = zink_resource(buffer_view->pres);
   simple_mtx_lock(&res->bufferview_mtx);
   if (buffer_view->reference.count) {
      /* got a cache hit during deletion */
      simple_mtx_unlock(&res->bufferview_mtx);
      return;
   }
   struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&res->bufferview_cache, buffer_view->hash, &buffer_view->bvci);
   assert(he);
   _mesa_hash_table_remove(&res->bufferview_cache, he);
   simple_mtx_unlock(&res->bufferview_mtx);
   pipe_resource_reference(&buffer_view->pres, NULL);
   VKSCR(DestroyBufferView)(screen->dev, buffer_view->buffer_view, NULL);
   zink_descriptor_set_refs_clear(&buffer_view->desc_set_refs, buffer_view);
   FREE(buffer_view);
}

static void
zink_sampler_view_destroy(struct pipe_context *pctx,
                          struct pipe_sampler_view *pview)
{
   struct zink_sampler_view *view = zink_sampler_view(pview);
   if (pview->texture->target == PIPE_BUFFER)
      zink_buffer_view_reference(zink_screen(pctx->screen), &view->buffer_view, NULL);
   else {
      zink_surface_reference(zink_screen(pctx->screen), &view->image_view, NULL);
   }
   pipe_resource_reference(&pview->texture, NULL);
   FREE_CL(view);
}

static void
zink_get_sample_position(struct pipe_context *ctx,
                         unsigned sample_count,
                         unsigned sample_index,
                         float *out_value)
{
   /* TODO: handle this I guess */
   assert(zink_screen(ctx->screen)->info.props.limits.standardSampleLocations);
   /* from 26.4. Multisampling */
   switch (sample_count) {
   case 0:
   case 1: {
      float pos[][2] = { {0.5,0.5}, };
      out_value[0] = pos[sample_index][0];
      out_value[1] = pos[sample_index][1];
      break;
   }
   case 2: {
      float pos[][2] = { {0.75,0.75},
                        {0.25,0.25}, };
      out_value[0] = pos[sample_index][0];
      out_value[1] = pos[sample_index][1];
      break;
   }
   case 4: {
      float pos[][2] = { {0.375, 0.125},
                        {0.875, 0.375},
                        {0.125, 0.625},
                        {0.625, 0.875}, };
      out_value[0] = pos[sample_index][0];
      out_value[1] = pos[sample_index][1];
      break;
   }
   case 8: {
      float pos[][2] = { {0.5625, 0.3125},
                        {0.4375, 0.6875},
                        {0.8125, 0.5625},
                        {0.3125, 0.1875},
                        {0.1875, 0.8125},
                        {0.0625, 0.4375},
                        {0.6875, 0.9375},
                        {0.9375, 0.0625}, };
      out_value[0] = pos[sample_index][0];
      out_value[1] = pos[sample_index][1];
      break;
   }
   case 16: {
      float pos[][2] = { {0.5625, 0.5625},
                        {0.4375, 0.3125},
                        {0.3125, 0.625},
                        {0.75, 0.4375},
                        {0.1875, 0.375},
                        {0.625, 0.8125},
                        {0.8125, 0.6875},
                        {0.6875, 0.1875},
                        {0.375, 0.875},
                        {0.5, 0.0625},
                        {0.25, 0.125},
                        {0.125, 0.75},
                        {0.0, 0.5},
                        {0.9375, 0.25},
                        {0.875, 0.9375},
                        {0.0625, 0.0}, };
      out_value[0] = pos[sample_index][0];
      out_value[1] = pos[sample_index][1];
      break;
   }
   default:
      unreachable("unhandled sample count!");
   }
}

static void
zink_set_polygon_stipple(struct pipe_context *pctx,
                         const struct pipe_poly_stipple *ps)
{
}

ALWAYS_INLINE static void
update_res_bind_count(struct zink_context *ctx, struct zink_resource *res, bool is_compute, bool decrement)
{
   if (decrement) {
      assert(res->bind_count[is_compute]);
      if (!--res->bind_count[is_compute])
         _mesa_set_remove_key(ctx->need_barriers[is_compute], res);
      check_resource_for_batch_ref(ctx, res);
   } else
      res->bind_count[is_compute]++;
}

ALWAYS_INLINE static void
update_existing_vbo(struct zink_context *ctx, unsigned slot)
{
   if (!ctx->vertex_buffers[slot].buffer.resource)
      return;
   struct zink_resource *res = zink_resource(ctx->vertex_buffers[slot].buffer.resource);
   res->vbo_bind_mask &= ~BITFIELD_BIT(slot);
   update_res_bind_count(ctx, res, false, true);
}

static void
zink_set_vertex_buffers(struct pipe_context *pctx,
                        unsigned start_slot,
                        unsigned num_buffers,
                        unsigned unbind_num_trailing_slots,
                        bool take_ownership,
                        const struct pipe_vertex_buffer *buffers)
{
   struct zink_context *ctx = zink_context(pctx);
   const bool need_state_change = !zink_screen(pctx->screen)->info.have_EXT_extended_dynamic_state &&
                                  !zink_screen(pctx->screen)->info.have_EXT_vertex_input_dynamic_state;
   uint32_t enabled_buffers = ctx->gfx_pipeline_state.vertex_buffers_enabled_mask;
   enabled_buffers |= u_bit_consecutive(start_slot, num_buffers);
   enabled_buffers &= ~u_bit_consecutive(start_slot + num_buffers, unbind_num_trailing_slots);

   if (buffers) {
      if (need_state_change)
         ctx->vertex_state_changed = true;
      for (unsigned i = 0; i < num_buffers; ++i) {
         const struct pipe_vertex_buffer *vb = buffers + i;
         struct pipe_vertex_buffer *ctx_vb = &ctx->vertex_buffers[start_slot + i];
         update_existing_vbo(ctx, start_slot + i);
         if (!take_ownership)
            pipe_resource_reference(&ctx_vb->buffer.resource, vb->buffer.resource);
         else {
            pipe_resource_reference(&ctx_vb->buffer.resource, NULL);
            ctx_vb->buffer.resource = vb->buffer.resource;
         }
         if (vb->buffer.resource) {
            struct zink_resource *res = zink_resource(vb->buffer.resource);
            res->vbo_bind_mask |= BITFIELD_BIT(start_slot + i);
            update_res_bind_count(ctx, res, false, false);
            ctx_vb->stride = vb->stride;
            ctx_vb->buffer_offset = vb->buffer_offset;
            /* always barrier before possible rebind */
            zink_resource_buffer_barrier(ctx, res, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
         } else {
            enabled_buffers &= ~BITFIELD_BIT(start_slot + i);
         }
      }
   } else {
      if (need_state_change)
         ctx->vertex_state_changed = true;
      for (unsigned i = 0; i < num_buffers; ++i) {
         update_existing_vbo(ctx, start_slot + i);
         pipe_resource_reference(&ctx->vertex_buffers[start_slot + i].buffer.resource, NULL);
      }
   }
   for (unsigned i = 0; i < unbind_num_trailing_slots; i++) {
      update_existing_vbo(ctx, start_slot + i);
      pipe_resource_reference(&ctx->vertex_buffers[start_slot + i].buffer.resource, NULL);
   }
   ctx->gfx_pipeline_state.vertex_buffers_enabled_mask = enabled_buffers;
   ctx->vertex_buffers_dirty = num_buffers > 0;
#ifndef NDEBUG
   u_foreach_bit(b, enabled_buffers)
      assert(ctx->vertex_buffers[b].buffer.resource);
#endif
}

static void
zink_set_viewport_states(struct pipe_context *pctx,
                         unsigned start_slot,
                         unsigned num_viewports,
                         const struct pipe_viewport_state *state)
{
   struct zink_context *ctx = zink_context(pctx);

   for (unsigned i = 0; i < num_viewports; ++i)
      ctx->vp_state.viewport_states[start_slot + i] = state[i];
   ctx->vp_state.num_viewports = start_slot + num_viewports;

   if (!zink_screen(pctx->screen)->info.have_EXT_extended_dynamic_state) {
      if (ctx->gfx_pipeline_state.dyn_state1.num_viewports != ctx->vp_state.num_viewports)
         ctx->gfx_pipeline_state.dirty = true;
      ctx->gfx_pipeline_state.dyn_state1.num_viewports = ctx->vp_state.num_viewports;
   }
   ctx->vp_state_changed = true;
}

static void
zink_set_scissor_states(struct pipe_context *pctx,
                        unsigned start_slot, unsigned num_scissors,
                        const struct pipe_scissor_state *states)
{
   struct zink_context *ctx = zink_context(pctx);

   for (unsigned i = 0; i < num_scissors; i++)
      ctx->vp_state.scissor_states[start_slot + i] = states[i];
   ctx->scissor_changed = true;
}

static void
zink_set_inlinable_constants(struct pipe_context *pctx,
                             enum pipe_shader_type shader,
                             uint num_values, uint32_t *values)
{
   struct zink_context *ctx = (struct zink_context *)pctx;
   const uint32_t bit = BITFIELD_BIT(shader);
   uint32_t *inlinable_uniforms;
   struct zink_shader_key *key = NULL;

   if (shader == PIPE_SHADER_COMPUTE) {
      key = &ctx->compute_pipeline_state.key;
   } else {
      key = &ctx->gfx_pipeline_state.shader_keys.key[shader];
   }
   inlinable_uniforms = key->base.inlined_uniform_values;
   if (!(ctx->inlinable_uniforms_valid_mask & bit) ||
       memcmp(inlinable_uniforms, values, num_values * 4)) {
      memcpy(inlinable_uniforms, values, num_values * 4);
      ctx->dirty_shader_stages |= bit;
      ctx->inlinable_uniforms_valid_mask |= bit;
      key->inline_uniforms = true;
   }
}

ALWAYS_INLINE static void
unbind_ubo(struct zink_context *ctx, struct zink_resource *res, enum pipe_shader_type pstage, unsigned slot)
{
   if (!res)
      return;
   res->ubo_bind_mask[pstage] &= ~BITFIELD_BIT(slot);
   res->ubo_bind_count[pstage == PIPE_SHADER_COMPUTE]--;
   update_res_bind_count(ctx, res, pstage == PIPE_SHADER_COMPUTE, true);
}

static void
invalidate_inlined_uniforms(struct zink_context *ctx, enum pipe_shader_type pstage)
{
   unsigned bit = BITFIELD_BIT(pstage);
   if (!(ctx->inlinable_uniforms_valid_mask & bit))
      return;
   ctx->inlinable_uniforms_valid_mask &= ~bit;
   ctx->dirty_shader_stages |= bit;
   if (pstage == PIPE_SHADER_COMPUTE)
      return;

   struct zink_shader_key *key = &ctx->gfx_pipeline_state.shader_keys.key[pstage];
   key->inline_uniforms = false;
}

static void
zink_set_constant_buffer(struct pipe_context *pctx,
                         enum pipe_shader_type shader, uint index,
                         bool take_ownership,
                         const struct pipe_constant_buffer *cb)
{
   struct zink_context *ctx = zink_context(pctx);
   bool update = false;

   struct zink_resource *res = zink_resource(ctx->ubos[shader][index].buffer);
   if (cb) {
      struct pipe_resource *buffer = cb->buffer;
      unsigned offset = cb->buffer_offset;
      struct zink_screen *screen = zink_screen(pctx->screen);
      if (cb->user_buffer) {
         u_upload_data(ctx->base.const_uploader, 0, cb->buffer_size,
                       screen->info.props.limits.minUniformBufferOffsetAlignment,
                       cb->user_buffer, &offset, &buffer);
      }
      struct zink_resource *new_res = zink_resource(buffer);
      if (new_res) {
         if (new_res != res) {
            unbind_ubo(ctx, res, shader, index);
            new_res->ubo_bind_count[shader == PIPE_SHADER_COMPUTE]++;
            new_res->ubo_bind_mask[shader] |= BITFIELD_BIT(index);
            update_res_bind_count(ctx, new_res, shader == PIPE_SHADER_COMPUTE, false);
         }
         zink_batch_resource_usage_set(&ctx->batch, new_res, false);
         zink_resource_buffer_barrier(ctx, new_res, VK_ACCESS_UNIFORM_READ_BIT,
                                      zink_pipeline_flags_from_pipe_stage(shader));
      }
      update |= ((index || screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY) && ctx->ubos[shader][index].buffer_offset != offset) ||
                !!res != !!buffer || (res && res->obj->buffer != new_res->obj->buffer) ||
                ctx->ubos[shader][index].buffer_size != cb->buffer_size;

      if (take_ownership) {
         pipe_resource_reference(&ctx->ubos[shader][index].buffer, NULL);
         ctx->ubos[shader][index].buffer = buffer;
      } else {
         pipe_resource_reference(&ctx->ubos[shader][index].buffer, buffer);
      }
      ctx->ubos[shader][index].buffer_offset = offset;
      ctx->ubos[shader][index].buffer_size = cb->buffer_size;
      ctx->ubos[shader][index].user_buffer = NULL;

      if (cb->user_buffer)
         pipe_resource_reference(&buffer, NULL);

      if (index + 1 >= ctx->di.num_ubos[shader])
         ctx->di.num_ubos[shader] = index + 1;
      update_descriptor_state_ubo(ctx, shader, index, new_res);
   } else {
      ctx->ubos[shader][index].buffer_offset = 0;
      ctx->ubos[shader][index].buffer_size = 0;
      ctx->ubos[shader][index].user_buffer = NULL;
      if (res) {
         unbind_ubo(ctx, res, shader, index);
         update_descriptor_state_ubo(ctx, shader, index, NULL);
      }
      update = !!ctx->ubos[shader][index].buffer;

      pipe_resource_reference(&ctx->ubos[shader][index].buffer, NULL);
      if (ctx->di.num_ubos[shader] == index + 1)
         ctx->di.num_ubos[shader]--;
   }
   if (index == 0) {
      /* Invalidate current inlinable uniforms. */
      invalidate_inlined_uniforms(ctx, shader);
   }

   if (update)
      zink_screen(pctx->screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_UBO, index, 1);
}

ALWAYS_INLINE static void
unbind_ssbo(struct zink_context *ctx, struct zink_resource *res, enum pipe_shader_type pstage, unsigned slot, bool writable)
{
   if (!res)
      return;
   res->ssbo_bind_mask[pstage] &= ~BITFIELD_BIT(slot);
   update_res_bind_count(ctx, res, pstage == PIPE_SHADER_COMPUTE, true);
   if (writable)
      res->write_bind_count[pstage == PIPE_SHADER_COMPUTE]--;
}

static void
zink_set_shader_buffers(struct pipe_context *pctx,
                        enum pipe_shader_type p_stage,
                        unsigned start_slot, unsigned count,
                        const struct pipe_shader_buffer *buffers,
                        unsigned writable_bitmask)
{
   struct zink_context *ctx = zink_context(pctx);
   bool update = false;
   unsigned max_slot = 0;

   unsigned modified_bits = u_bit_consecutive(start_slot, count);
   unsigned old_writable_mask = ctx->writable_ssbos[p_stage];
   ctx->writable_ssbos[p_stage] &= ~modified_bits;
   ctx->writable_ssbos[p_stage] |= writable_bitmask << start_slot;

   for (unsigned i = 0; i < count; i++) {
      struct pipe_shader_buffer *ssbo = &ctx->ssbos[p_stage][start_slot + i];
      struct zink_resource *res = ssbo->buffer ? zink_resource(ssbo->buffer) : NULL;
      bool was_writable = old_writable_mask & BITFIELD64_BIT(start_slot + i);
      if (buffers && buffers[i].buffer) {
         struct zink_resource *new_res = zink_resource(buffers[i].buffer);
         if (new_res != res) {
            unbind_ssbo(ctx, res, p_stage, i, was_writable);
            new_res->ssbo_bind_mask[p_stage] |= BITFIELD_BIT(i);
            update_res_bind_count(ctx, new_res, p_stage == PIPE_SHADER_COMPUTE, false);
         }
         VkAccessFlags access = VK_ACCESS_SHADER_READ_BIT;
         if (ctx->writable_ssbos[p_stage] & BITFIELD64_BIT(start_slot + i)) {
            new_res->write_bind_count[p_stage == PIPE_SHADER_COMPUTE]++;
            access |= VK_ACCESS_SHADER_WRITE_BIT;
         }
         pipe_resource_reference(&ssbo->buffer, &new_res->base.b);
         zink_batch_resource_usage_set(&ctx->batch, new_res, access & VK_ACCESS_SHADER_WRITE_BIT);
         ssbo->buffer_offset = buffers[i].buffer_offset;
         ssbo->buffer_size = MIN2(buffers[i].buffer_size, new_res->base.b.width0 - ssbo->buffer_offset);
         util_range_add(&new_res->base.b, &new_res->valid_buffer_range, ssbo->buffer_offset,
                        ssbo->buffer_offset + ssbo->buffer_size);
         zink_resource_buffer_barrier(ctx, new_res, access,
                                      zink_pipeline_flags_from_pipe_stage(p_stage));
         update = true;
         max_slot = MAX2(max_slot, start_slot + i);
         update_descriptor_state_ssbo(ctx, p_stage, start_slot + i, new_res);
      } else {
         update = !!res;
         ssbo->buffer_offset = 0;
         ssbo->buffer_size = 0;
         if (res) {
            unbind_ssbo(ctx, res, p_stage, i, was_writable);
            update_descriptor_state_ssbo(ctx, p_stage, start_slot + i, NULL);
         }
         pipe_resource_reference(&ssbo->buffer, NULL);
      }
   }
   if (start_slot + count >= ctx->di.num_ssbos[p_stage])
      ctx->di.num_ssbos[p_stage] = max_slot + 1;
   if (update)
      zink_screen(pctx->screen)->context_invalidate_descriptor_state(ctx, p_stage, ZINK_DESCRIPTOR_TYPE_SSBO, start_slot, count);
}

static void
update_binds_for_samplerviews(struct zink_context *ctx, struct zink_resource *res, bool is_compute)
{
    VkImageLayout layout = get_layout_for_binding(res, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, is_compute);
    if (is_compute) {
       u_foreach_bit(slot, res->sampler_binds[PIPE_SHADER_COMPUTE]) {
          if (ctx->di.textures[PIPE_SHADER_COMPUTE][slot].imageLayout != layout) {
             update_descriptor_state_sampler(ctx, PIPE_SHADER_COMPUTE, slot, res);
             zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, PIPE_SHADER_COMPUTE, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, slot, 1);
          }
       }
    } else {
       for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
          u_foreach_bit(slot, res->sampler_binds[i]) {
             if (ctx->di.textures[i][slot].imageLayout != layout) {
                update_descriptor_state_sampler(ctx, i, slot, res);
                zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, i, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, slot, 1);
             }
          }
       }
    }
}

static void
flush_pending_clears(struct zink_context *ctx, struct zink_resource *res)
{
   if (res->fb_binds && ctx->clears_enabled)
      zink_fb_clears_apply(ctx, &res->base.b);
}

static inline void
unbind_shader_image_counts(struct zink_context *ctx, struct zink_resource *res, bool is_compute, bool writable)
{
   update_res_bind_count(ctx, res, is_compute, true);
   if (writable)
      res->write_bind_count[is_compute]--;
   res->image_bind_count[is_compute]--;
   /* if this was the last image bind, the sampler bind layouts must be updated */
   if (!res->obj->is_buffer && !res->image_bind_count[is_compute] && res->bind_count[is_compute])
      update_binds_for_samplerviews(ctx, res, is_compute);
}

ALWAYS_INLINE static void
check_for_layout_update(struct zink_context *ctx, struct zink_resource *res, bool is_compute)
{
   VkImageLayout layout = res->bind_count[is_compute] ? zink_descriptor_util_image_layout_eval(res, is_compute) : VK_IMAGE_LAYOUT_UNDEFINED;
   VkImageLayout other_layout = res->bind_count[!is_compute] ? zink_descriptor_util_image_layout_eval(res, !is_compute) : VK_IMAGE_LAYOUT_UNDEFINED;
   if (res->bind_count[is_compute] && layout && res->layout != layout)
      _mesa_set_add(ctx->need_barriers[is_compute], res);
   if (res->bind_count[!is_compute] && other_layout && (layout != other_layout || res->layout != other_layout))
      _mesa_set_add(ctx->need_barriers[!is_compute], res);
}

static void
unbind_shader_image(struct zink_context *ctx, enum pipe_shader_type stage, unsigned slot)
{
   struct zink_image_view *image_view = &ctx->image_views[stage][slot];
   bool is_compute = stage == PIPE_SHADER_COMPUTE;
   if (!image_view->base.resource)
      return;

   struct zink_resource *res = zink_resource(image_view->base.resource);
   unbind_shader_image_counts(ctx, res, is_compute, image_view->base.access & PIPE_IMAGE_ACCESS_WRITE);

   if (image_view->base.resource->target == PIPE_BUFFER) {
      if (zink_batch_usage_exists(image_view->buffer_view->batch_uses))
         zink_batch_reference_bufferview(&ctx->batch, image_view->buffer_view);
      zink_buffer_view_reference(zink_screen(ctx->base.screen), &image_view->buffer_view, NULL);
   } else {
      if (!res->image_bind_count[is_compute])
         check_for_layout_update(ctx, res, is_compute);
      if (zink_batch_usage_exists(image_view->surface->batch_uses))
         zink_batch_reference_surface(&ctx->batch, image_view->surface);
      zink_surface_reference(zink_screen(ctx->base.screen), &image_view->surface, NULL);
   }
   pipe_resource_reference(&image_view->base.resource, NULL);
   image_view->base.resource = NULL;
   image_view->surface = NULL;
}

static struct zink_buffer_view *
create_image_bufferview(struct zink_context *ctx, const struct pipe_image_view *view)
{
   struct zink_resource *res = zink_resource(view->resource);
   VkBufferViewCreateInfo bvci = create_bvci(ctx, res, view->format, view->u.buf.offset, view->u.buf.size);
   struct zink_buffer_view *buffer_view = get_buffer_view(ctx, res, &bvci);
   if (!buffer_view)
      return NULL;
   util_range_add(&res->base.b, &res->valid_buffer_range, view->u.buf.offset,
                  view->u.buf.offset + view->u.buf.size);
   return buffer_view;
}

static void
finalize_image_bind(struct zink_context *ctx, struct zink_resource *res, bool is_compute)
{
   /* if this is the first image bind and there are sampler binds, the image's sampler layout
    * must be updated to GENERAL
    */
   if (res->image_bind_count[is_compute] == 1 &&
       res->bind_count[is_compute] > 1)
      update_binds_for_samplerviews(ctx, res, is_compute);
   check_for_layout_update(ctx, res, is_compute);
}

static struct zink_surface *
create_image_surface(struct zink_context *ctx, const struct pipe_image_view *view, bool is_compute)
{
   struct zink_resource *res = zink_resource(view->resource);
   struct pipe_surface tmpl = {0};
   tmpl.format = view->format;
   tmpl.u.tex.level = view->u.tex.level;
   tmpl.u.tex.first_layer = view->u.tex.first_layer;
   tmpl.u.tex.last_layer = view->u.tex.last_layer;
   struct pipe_surface *psurf = ctx->base.create_surface(&ctx->base, &res->base.b, &tmpl);
   if (!psurf)
      return NULL;
   /* this is actually a zink_ctx_surface, but we just want the inner surface */
   struct zink_surface *surface = zink_csurface(psurf);
   FREE(psurf);
   flush_pending_clears(ctx, res);
   return surface;
}

static void
zink_set_shader_images(struct pipe_context *pctx,
                       enum pipe_shader_type p_stage,
                       unsigned start_slot, unsigned count,
                       unsigned unbind_num_trailing_slots,
                       const struct pipe_image_view *images)
{
   struct zink_context *ctx = zink_context(pctx);
   bool update = false;
   for (unsigned i = 0; i < count; i++) {
      struct zink_image_view *image_view = &ctx->image_views[p_stage][start_slot + i];
      if (images && images[i].resource) {
         struct zink_resource *res = zink_resource(images[i].resource);
         struct zink_resource *old_res = zink_resource(image_view->base.resource);
         if (!zink_resource_object_init_storage(ctx, res)) {
            debug_printf("couldn't create storage image!");
            continue;
         }
         if (res != old_res) {
            if (old_res) {
               unbind_shader_image_counts(ctx, old_res, p_stage == PIPE_SHADER_COMPUTE, image_view->base.access & PIPE_IMAGE_ACCESS_WRITE);
               if (!old_res->obj->is_buffer && !old_res->image_bind_count[p_stage == PIPE_SHADER_COMPUTE])
                  check_for_layout_update(ctx, old_res, p_stage == PIPE_SHADER_COMPUTE);
            }
            update_res_bind_count(ctx, res, p_stage == PIPE_SHADER_COMPUTE, false);
         }
         util_copy_image_view(&image_view->base, images + i);
         VkAccessFlags access = 0;
         if (image_view->base.access & PIPE_IMAGE_ACCESS_WRITE) {
            zink_resource(image_view->base.resource)->write_bind_count[p_stage == PIPE_SHADER_COMPUTE]++;
            access |= VK_ACCESS_SHADER_WRITE_BIT;
         }
         if (image_view->base.access & PIPE_IMAGE_ACCESS_READ) {
            access |= VK_ACCESS_SHADER_READ_BIT;
         }
         res->image_bind_count[p_stage == PIPE_SHADER_COMPUTE]++;
         if (images[i].resource->target == PIPE_BUFFER) {
            image_view->buffer_view = create_image_bufferview(ctx, &images[i]);
            assert(image_view->buffer_view);
            zink_batch_usage_set(&image_view->buffer_view->batch_uses, ctx->batch.state);
            zink_resource_buffer_barrier(ctx, res, access,
                                         zink_pipeline_flags_from_pipe_stage(p_stage));
         } else {
            image_view->surface = create_image_surface(ctx, &images[i], p_stage == PIPE_SHADER_COMPUTE);
            assert(image_view->surface);
            finalize_image_bind(ctx, res, p_stage == PIPE_SHADER_COMPUTE);
            zink_batch_usage_set(&image_view->surface->batch_uses, ctx->batch.state);
         }
         zink_batch_resource_usage_set(&ctx->batch, zink_resource(image_view->base.resource),
                                          zink_resource_access_is_write(access));
         update = true;
         update_descriptor_state_image(ctx, p_stage, start_slot + i, res);
      } else if (image_view->base.resource) {
         update |= !!image_view->base.resource;

         unbind_shader_image(ctx, p_stage, start_slot + i);
         update_descriptor_state_image(ctx, p_stage, start_slot + i, NULL);
      }
   }
   for (unsigned i = 0; i < unbind_num_trailing_slots; i++) {
      update |= !!ctx->image_views[p_stage][start_slot + count + i].base.resource;
      unbind_shader_image(ctx, p_stage, start_slot + count + i);
      update_descriptor_state_image(ctx, p_stage, start_slot + count + i, NULL);
   }
   ctx->di.num_images[p_stage] = start_slot + count;
   if (update)
      zink_screen(pctx->screen)->context_invalidate_descriptor_state(ctx, p_stage, ZINK_DESCRIPTOR_TYPE_IMAGE, start_slot, count);
}

ALWAYS_INLINE static void
check_samplerview_for_batch_ref(struct zink_context *ctx, struct zink_sampler_view *sv)
{
   const struct zink_resource *res = zink_resource(sv->base.texture);
   if ((res->obj->is_buffer && zink_batch_usage_exists(sv->buffer_view->batch_uses)) ||
       (!res->obj->is_buffer && zink_batch_usage_exists(sv->image_view->batch_uses)))
      zink_batch_reference_sampler_view(&ctx->batch, sv);
}

ALWAYS_INLINE static void
unbind_samplerview(struct zink_context *ctx, enum pipe_shader_type stage, unsigned slot)
{
   struct zink_sampler_view *sv = zink_sampler_view(ctx->sampler_views[stage][slot]);
   if (!sv || !sv->base.texture)
      return;
   struct zink_resource *res = zink_resource(sv->base.texture);
   check_samplerview_for_batch_ref(ctx, sv);
   update_res_bind_count(ctx, res, stage == PIPE_SHADER_COMPUTE, true);
   res->sampler_binds[stage] &= ~BITFIELD_BIT(slot);
}

static void
zink_set_sampler_views(struct pipe_context *pctx,
                       enum pipe_shader_type shader_type,
                       unsigned start_slot,
                       unsigned num_views,
                       unsigned unbind_num_trailing_slots,
                       bool take_ownership,
                       struct pipe_sampler_view **views)
{
   struct zink_context *ctx = zink_context(pctx);
   unsigned i;

   bool update = false;
   for (i = 0; i < num_views; ++i) {
      struct pipe_sampler_view *pview = views ? views[i] : NULL;
      struct zink_sampler_view *a = zink_sampler_view(ctx->sampler_views[shader_type][start_slot + i]);
      struct zink_sampler_view *b = zink_sampler_view(pview);
      struct zink_resource *res = b ? zink_resource(b->base.texture) : NULL;
      if (b && b->base.texture) {
         if (!a || zink_resource(a->base.texture) != res) {
            if (a)
               unbind_samplerview(ctx, shader_type, start_slot + i);
            update_res_bind_count(ctx, res, shader_type == PIPE_SHADER_COMPUTE, false);
         } else if (a != b) {
            check_samplerview_for_batch_ref(ctx, a);
         }
         if (res->base.b.target == PIPE_BUFFER) {
            if (b->buffer_view->bvci.buffer != res->obj->buffer) {
               /* if this resource has been rebound while it wasn't set here,
                * its backing resource will have changed and thus we need to update
                * the bufferview
                */
               VkBufferViewCreateInfo bvci = b->buffer_view->bvci;
               bvci.buffer = res->obj->buffer;
               struct zink_buffer_view *buffer_view = get_buffer_view(ctx, res, &bvci);
               assert(buffer_view != b->buffer_view);
               if (zink_batch_usage_exists(b->buffer_view->batch_uses))
                  zink_batch_reference_bufferview(&ctx->batch, b->buffer_view);
               zink_buffer_view_reference(zink_screen(ctx->base.screen), &b->buffer_view, NULL);
               b->buffer_view = buffer_view;
               update = true;
            }
            zink_batch_usage_set(&b->buffer_view->batch_uses, ctx->batch.state);
            zink_resource_buffer_barrier(ctx, res, VK_ACCESS_SHADER_READ_BIT,
                                         zink_pipeline_flags_from_pipe_stage(shader_type));
            if (!a || a->buffer_view->buffer_view != b->buffer_view->buffer_view)
               update = true;
         } else if (!res->obj->is_buffer) {
             if (res->obj != b->image_view->obj) {
                struct pipe_surface *psurf = &b->image_view->base;
                VkImageView iv = b->image_view->image_view;
                zink_rebind_surface(ctx, &psurf);
                b->image_view = zink_surface(psurf);
                update |= iv != b->image_view->image_view;
             } else  if (a != b)
                update = true;
             flush_pending_clears(ctx, res);
             check_for_layout_update(ctx, res, shader_type == PIPE_SHADER_COMPUTE);
             zink_batch_usage_set(&b->image_view->batch_uses, ctx->batch.state);
             if (!a)
                update = true;
         }
         res->sampler_binds[shader_type] |= BITFIELD_BIT(start_slot + i);
         zink_batch_resource_usage_set(&ctx->batch, res, false);
      } else if (a) {
         unbind_samplerview(ctx, shader_type, start_slot + i);
         update = true;
      }
      if (take_ownership) {
         pipe_sampler_view_reference(&ctx->sampler_views[shader_type][start_slot + i], NULL);
         ctx->sampler_views[shader_type][start_slot + i] = pview;
      } else {
         pipe_sampler_view_reference(&ctx->sampler_views[shader_type][start_slot + i], pview);
      }
      update_descriptor_state_sampler(ctx, shader_type, start_slot + i, res);
   }
   for (; i < num_views + unbind_num_trailing_slots; ++i) {
      update |= !!ctx->sampler_views[shader_type][start_slot + i];
      unbind_samplerview(ctx, shader_type, start_slot + i);
      pipe_sampler_view_reference(
         &ctx->sampler_views[shader_type][start_slot + i],
         NULL);
      update_descriptor_state_sampler(ctx, shader_type, start_slot + i, NULL);
   }
   ctx->di.num_sampler_views[shader_type] = start_slot + num_views;
   if (update)
      zink_screen(pctx->screen)->context_invalidate_descriptor_state(ctx, shader_type, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, start_slot, num_views);
}

static uint64_t
zink_create_texture_handle(struct pipe_context *pctx, struct pipe_sampler_view *view, const struct pipe_sampler_state *state)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(view->texture);
   struct zink_sampler_view *sv = zink_sampler_view(view);
   struct zink_bindless_descriptor *bd;
   bd = calloc(1, sizeof(struct zink_bindless_descriptor));
   if (!bd)
      return 0;

   bd->sampler = pctx->create_sampler_state(pctx, state);
   if (!bd->sampler) {
      free(bd);
      return 0;
   }

   bd->ds.is_buffer = res->base.b.target == PIPE_BUFFER;
   if (res->base.b.target == PIPE_BUFFER)
      zink_buffer_view_reference(zink_screen(pctx->screen), &bd->ds.bufferview, sv->buffer_view);
   else
      zink_surface_reference(zink_screen(pctx->screen), &bd->ds.surface, sv->image_view);
   uint64_t handle = util_idalloc_alloc(&ctx->di.bindless[bd->ds.is_buffer].tex_slots);
   if (bd->ds.is_buffer)
      handle += ZINK_MAX_BINDLESS_HANDLES;
   bd->handle = handle;
   _mesa_hash_table_insert(&ctx->di.bindless[bd->ds.is_buffer].tex_handles, (void*)(uintptr_t)handle, bd);
   return handle;
}

static void
zink_delete_texture_handle(struct pipe_context *pctx, uint64_t handle)
{
   struct zink_context *ctx = zink_context(pctx);
   bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
   struct hash_entry *he = _mesa_hash_table_search(&ctx->di.bindless[is_buffer].tex_handles, (void*)(uintptr_t)handle);
   assert(he);
   struct zink_bindless_descriptor *bd = he->data;
   struct zink_descriptor_surface *ds = &bd->ds;
   _mesa_hash_table_remove(&ctx->di.bindless[is_buffer].tex_handles, he);
   uint32_t h = handle;
   util_dynarray_append(&ctx->batch.state->bindless_releases[0], uint32_t, h);

   struct zink_resource *res = zink_descriptor_surface_resource(ds);
   if (ds->is_buffer) {
      if (zink_resource_has_usage(res))
         zink_batch_reference_bufferview(&ctx->batch, ds->bufferview);
      zink_buffer_view_reference(zink_screen(pctx->screen), &ds->bufferview, NULL);
   } else {
      if (zink_resource_has_usage(res))
         zink_batch_reference_surface(&ctx->batch, ds->surface);
      zink_surface_reference(zink_screen(pctx->screen), &ds->surface, NULL);
      pctx->delete_sampler_state(pctx, bd->sampler);
   }
   free(ds);
}

static void
rebind_bindless_bufferview(struct zink_context *ctx, struct zink_resource *res, struct zink_descriptor_surface *ds)
{
   /* if this resource has been rebound while it wasn't set here,
    * its backing resource will have changed and thus we need to update
    * the bufferview
    */
   VkBufferViewCreateInfo bvci = ds->bufferview->bvci;
   bvci.buffer = res->obj->buffer;
   struct zink_buffer_view *buffer_view = get_buffer_view(ctx, res, &bvci);
   assert(buffer_view != ds->bufferview);
   if (zink_resource_has_usage(res))
      zink_batch_reference_bufferview(&ctx->batch, ds->bufferview);
   zink_buffer_view_reference(zink_screen(ctx->base.screen), &ds->bufferview, NULL);
   ds->bufferview = buffer_view;
}

static void
zero_bindless_descriptor(struct zink_context *ctx, uint32_t handle, bool is_buffer, bool is_image)
{
   if (likely(zink_screen(ctx->base.screen)->info.rb2_feats.nullDescriptor)) {
      if (is_buffer) {
         VkBufferView *bv = &ctx->di.bindless[is_image].buffer_infos[handle];
         *bv = VK_NULL_HANDLE;
      } else {
         VkDescriptorImageInfo *ii = &ctx->di.bindless[is_image].img_infos[handle];
         memset(ii, 0, sizeof(*ii));
      }
   } else {
      if (is_buffer) {
         VkBufferView *bv = &ctx->di.bindless[is_image].buffer_infos[handle];
         struct zink_buffer_view *null_bufferview = ctx->dummy_bufferview;
         *bv = null_bufferview->buffer_view;
      } else {
         struct zink_surface *null_surface = zink_csurface(ctx->dummy_surface[is_image]);
         VkDescriptorImageInfo *ii = &ctx->di.bindless[is_image].img_infos[handle];
         ii->sampler = VK_NULL_HANDLE;
         ii->imageView = null_surface->image_view;
         ii->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      }
   }
}

static void
zink_make_texture_handle_resident(struct pipe_context *pctx, uint64_t handle, bool resident)
{
   struct zink_context *ctx = zink_context(pctx);
   bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
   struct hash_entry *he = _mesa_hash_table_search(&ctx->di.bindless[is_buffer].tex_handles, (void*)(uintptr_t)handle);
   assert(he);
   struct zink_bindless_descriptor *bd = he->data;
   struct zink_descriptor_surface *ds = &bd->ds;
   struct zink_resource *res = zink_descriptor_surface_resource(ds);
   if (is_buffer)
      handle -= ZINK_MAX_BINDLESS_HANDLES;
   if (resident) {
      update_res_bind_count(ctx, res, false, false);
      update_res_bind_count(ctx, res, true, false);
      res->bindless[0]++;
      if (is_buffer) {
         if (ds->bufferview->bvci.buffer != res->obj->buffer)
            rebind_bindless_bufferview(ctx, res, ds);
         VkBufferView *bv = &ctx->di.bindless[0].buffer_infos[handle];
         *bv = ds->bufferview->buffer_view;
         zink_resource_buffer_barrier(ctx, res, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
      } else {
         VkDescriptorImageInfo *ii = &ctx->di.bindless[0].img_infos[handle];
         ii->sampler = bd->sampler->sampler;
         ii->imageView = ds->surface->image_view;
         ii->imageLayout = zink_descriptor_util_image_layout_eval(res, false);
         flush_pending_clears(ctx, res);
         check_for_layout_update(ctx, res, false);
         check_for_layout_update(ctx, res, true);
      }
      zink_batch_resource_usage_set(&ctx->batch, res, false);
      util_dynarray_append(&ctx->di.bindless[0].resident, struct zink_bindless_descriptor *, bd);
      uint32_t h = is_buffer ? handle + ZINK_MAX_BINDLESS_HANDLES : handle;
      util_dynarray_append(&ctx->di.bindless[0].updates, uint32_t, h);
   } else {
      zero_bindless_descriptor(ctx, handle, is_buffer, false);
      util_dynarray_delete_unordered(&ctx->di.bindless[0].resident, struct zink_bindless_descriptor *, bd);
      update_res_bind_count(ctx, res, false, true);
      update_res_bind_count(ctx, res, true, true);
      res->bindless[0]--;
      for (unsigned i = 0; i < 2; i++) {
         if (!res->image_bind_count[i])
            check_for_layout_update(ctx, res, i);
      }
   }
   ctx->di.bindless_dirty[0] = true;
}

static uint64_t
zink_create_image_handle(struct pipe_context *pctx, const struct pipe_image_view *view)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(view->resource);
   struct zink_bindless_descriptor *bd;
   if (!zink_resource_object_init_storage(ctx, res)) {
      debug_printf("couldn't create storage image!");
      return 0;
   }
   bd = malloc(sizeof(struct zink_bindless_descriptor));
   if (!bd)
      return 0;
   bd->sampler = NULL;

   bd->ds.is_buffer = res->base.b.target == PIPE_BUFFER;
   if (res->base.b.target == PIPE_BUFFER)
      bd->ds.bufferview = create_image_bufferview(ctx, view);
   else
      bd->ds.surface = create_image_surface(ctx, view, false);
   uint64_t handle = util_idalloc_alloc(&ctx->di.bindless[bd->ds.is_buffer].img_slots);
   if (bd->ds.is_buffer)
      handle += ZINK_MAX_BINDLESS_HANDLES;
   bd->handle = handle;
   _mesa_hash_table_insert(&ctx->di.bindless[bd->ds.is_buffer].img_handles, (void*)(uintptr_t)handle, bd);
   return handle;
}

static void
zink_delete_image_handle(struct pipe_context *pctx, uint64_t handle)
{
   struct zink_context *ctx = zink_context(pctx);
   bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
   struct hash_entry *he = _mesa_hash_table_search(&ctx->di.bindless[is_buffer].img_handles, (void*)(uintptr_t)handle);
   assert(he);
   struct zink_descriptor_surface *ds = he->data;
   _mesa_hash_table_remove(&ctx->di.bindless[is_buffer].img_handles, he);
   uint32_t h = handle;
   util_dynarray_append(&ctx->batch.state->bindless_releases[1], uint32_t, h);

   struct zink_resource *res = zink_descriptor_surface_resource(ds);
   if (ds->is_buffer) {
      if (zink_resource_has_usage(res))
         zink_batch_reference_bufferview(&ctx->batch, ds->bufferview);
      zink_buffer_view_reference(zink_screen(pctx->screen), &ds->bufferview, NULL);
   } else {
      if (zink_resource_has_usage(res))
         zink_batch_reference_surface(&ctx->batch, ds->surface);
      zink_surface_reference(zink_screen(pctx->screen), &ds->surface, NULL);
   }
   free(ds);
}

static void
zink_make_image_handle_resident(struct pipe_context *pctx, uint64_t handle, unsigned paccess, bool resident)
{
   struct zink_context *ctx = zink_context(pctx);
   bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
   struct hash_entry *he = _mesa_hash_table_search(&ctx->di.bindless[is_buffer].img_handles, (void*)(uintptr_t)handle);
   assert(he);
   struct zink_bindless_descriptor *bd = he->data;
   struct zink_descriptor_surface *ds = &bd->ds;
   bd->access = paccess;
   struct zink_resource *res = zink_descriptor_surface_resource(ds);
   VkAccessFlags access = 0;
   if (paccess & PIPE_IMAGE_ACCESS_WRITE) {
      if (resident) {
         res->write_bind_count[0]++;
         res->write_bind_count[1]++;
      } else {
         res->write_bind_count[0]--;
         res->write_bind_count[1]--;
      }
      access |= VK_ACCESS_SHADER_WRITE_BIT;
   }
   if (paccess & PIPE_IMAGE_ACCESS_READ) {
      access |= VK_ACCESS_SHADER_READ_BIT;
   }
   if (is_buffer)
      handle -= ZINK_MAX_BINDLESS_HANDLES;
   if (resident) {
      update_res_bind_count(ctx, res, false, false);
      update_res_bind_count(ctx, res, true, false);
      res->image_bind_count[0]++;
      res->image_bind_count[1]++;
      res->bindless[1]++;
      if (is_buffer) {
         if (ds->bufferview->bvci.buffer != res->obj->buffer)
            rebind_bindless_bufferview(ctx, res, ds);
         VkBufferView *bv = &ctx->di.bindless[1].buffer_infos[handle];
         *bv = ds->bufferview->buffer_view;
         zink_resource_buffer_barrier(ctx, res, access, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
      } else {
         VkDescriptorImageInfo *ii = &ctx->di.bindless[1].img_infos[handle];
         ii->sampler = VK_NULL_HANDLE;
         ii->imageView = ds->surface->image_view;
         ii->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
         finalize_image_bind(ctx, res, false);
         finalize_image_bind(ctx, res, true);
      }
      zink_batch_resource_usage_set(&ctx->batch, res, zink_resource_access_is_write(access));
      util_dynarray_append(&ctx->di.bindless[1].resident, struct zink_bindless_descriptor *, bd);
      uint32_t h = is_buffer ? handle + ZINK_MAX_BINDLESS_HANDLES : handle;
      util_dynarray_append(&ctx->di.bindless[1].updates, uint32_t, h);
   } else {
      zero_bindless_descriptor(ctx, handle, is_buffer, true);
      util_dynarray_delete_unordered(&ctx->di.bindless[1].resident, struct zink_bindless_descriptor *, bd);
      unbind_shader_image_counts(ctx, res, false, false);
      unbind_shader_image_counts(ctx, res, true, false);
      res->bindless[1]--;
      for (unsigned i = 0; i < 2; i++) {
         if (!res->image_bind_count[i])
            check_for_layout_update(ctx, res, i);
      }
   }
   ctx->di.bindless_dirty[1] = true;
}

static void
zink_set_stencil_ref(struct pipe_context *pctx,
                     const struct pipe_stencil_ref ref)
{
   struct zink_context *ctx = zink_context(pctx);
   ctx->stencil_ref = ref;
   ctx->stencil_ref_changed = true;
}

static void
zink_set_clip_state(struct pipe_context *pctx,
                    const struct pipe_clip_state *pcs)
{
}

static void
zink_set_tess_state(struct pipe_context *pctx,
                    const float default_outer_level[4],
                    const float default_inner_level[2])
{
   struct zink_context *ctx = zink_context(pctx);
   memcpy(&ctx->default_inner_level, default_inner_level, sizeof(ctx->default_inner_level));
   memcpy(&ctx->default_outer_level, default_outer_level, sizeof(ctx->default_outer_level));
}

static void
zink_set_patch_vertices(struct pipe_context *pctx, uint8_t patch_vertices)
{
   struct zink_context *ctx = zink_context(pctx);
   ctx->gfx_pipeline_state.patch_vertices = patch_vertices;
}

void
zink_update_fbfetch(struct zink_context *ctx)
{
   const bool had_fbfetch = ctx->di.fbfetch.imageLayout == VK_IMAGE_LAYOUT_GENERAL;
   if (!ctx->gfx_stages[PIPE_SHADER_FRAGMENT] ||
       !ctx->gfx_stages[PIPE_SHADER_FRAGMENT]->nir->info.fs.uses_fbfetch_output) {
      if (!had_fbfetch)
         return;
      ctx->rp_changed = true;
      zink_batch_no_rp(ctx);
      ctx->di.fbfetch.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ctx->di.fbfetch.imageView = zink_screen(ctx->base.screen)->info.rb2_feats.nullDescriptor ?
                                  VK_NULL_HANDLE :
                                  zink_csurface(ctx->dummy_surface[0])->image_view;
      zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, PIPE_SHADER_FRAGMENT, ZINK_DESCRIPTOR_TYPE_UBO, 0, 1);
      return;
   }

   bool changed = !had_fbfetch;
   if (ctx->fb_state.cbufs[0]) {
      VkImageView fbfetch = zink_csurface(ctx->fb_state.cbufs[0])->image_view;
      changed |= fbfetch != ctx->di.fbfetch.imageView;
      ctx->di.fbfetch.imageView = zink_csurface(ctx->fb_state.cbufs[0])->image_view;
   }
   ctx->di.fbfetch.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
   if (changed) {
      zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, PIPE_SHADER_FRAGMENT, ZINK_DESCRIPTOR_TYPE_UBO, 0, 1);
      ctx->rp_changed = true;
      zink_batch_no_rp(ctx);
   }
}

static size_t
rp_state_size(const struct zink_render_pass_pipeline_state *pstate)
{
   return offsetof(struct zink_render_pass_pipeline_state, attachments) +
                   sizeof(pstate->attachments[0]) * pstate->num_attachments;
}

static uint32_t
hash_rp_state(const void *key)
{
   const struct zink_render_pass_pipeline_state *s = key;
   return _mesa_hash_data(key, rp_state_size(s));
}

static bool
equals_rp_state(const void *a, const void *b)
{
   return !memcmp(a, b, rp_state_size(a));
}

static uint32_t
hash_render_pass_state(const void *key)
{
   struct zink_render_pass_state* s = (struct zink_render_pass_state*)key;
   return _mesa_hash_data(key, offsetof(struct zink_render_pass_state, rts) + sizeof(s->rts[0]) * s->num_rts);
}

static bool
equals_render_pass_state(const void *a, const void *b)
{
   const struct zink_render_pass_state *s_a = a, *s_b = b;
   if (s_a->num_rts != s_b->num_rts)
      return false;
   return memcmp(a, b, offsetof(struct zink_render_pass_state, rts) + sizeof(s_a->rts[0]) * s_a->num_rts) == 0;
}

static struct zink_render_pass *
get_render_pass(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   const struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_render_pass_state state = {0};
   uint32_t clears = 0;
   state.swapchain_init = ctx->new_swapchain;
   state.samples = fb->samples > 0;

   u_foreach_bit(i, ctx->fbfetch_outputs)
      state.rts[i].fbfetch = true;

   for (int i = 0; i < fb->nr_cbufs; i++) {
      struct pipe_surface *surf = fb->cbufs[i];
      if (surf) {
         struct zink_surface *transient = zink_transient_surface(surf);
         state.rts[i].format = zink_get_format(screen, surf->format);
         state.rts[i].samples = MAX3(transient ? transient->base.nr_samples : 0, surf->texture->nr_samples, 1);
         state.rts[i].clear_color = zink_fb_clear_enabled(ctx, i) && !zink_fb_clear_first_needs_explicit(&ctx->fb_clears[i]);
         clears |= !!state.rts[i].clear_color ? PIPE_CLEAR_COLOR0 << i : 0;
         state.rts[i].swapchain = surf->texture->bind & PIPE_BIND_SCANOUT;
         if (transient) {
            state.num_cresolves++;
            state.rts[i].resolve = true;
            if (!state.rts[i].clear_color)
               state.msaa_expand_mask |= BITFIELD_BIT(i);
         }
      } else {
         state.rts[i].format = VK_FORMAT_R8_UINT;
         state.rts[i].samples = fb->samples;
      }
      state.num_rts++;
   }
   state.num_cbufs = fb->nr_cbufs;
   assert(!state.num_cresolves || state.num_cbufs == state.num_cresolves);

   if (fb->zsbuf) {
      struct zink_resource *zsbuf = zink_resource(fb->zsbuf->texture);
      struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
      struct zink_surface *transient = zink_transient_surface(fb->zsbuf);
      state.rts[fb->nr_cbufs].format = zsbuf->format;
      state.rts[fb->nr_cbufs].samples = MAX3(transient ? transient->base.nr_samples : 0, fb->zsbuf->texture->nr_samples, 1);
      if (transient) {
         state.num_zsresolves = 1;
         state.rts[fb->nr_cbufs].resolve = true;
      }
      state.rts[fb->nr_cbufs].clear_color = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                            !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                            (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_DEPTH);
      state.rts[fb->nr_cbufs].clear_stencil = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                              !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                              (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_STENCIL);
      if (state.rts[fb->nr_cbufs].clear_color)
         clears |= PIPE_CLEAR_DEPTH;
      if (state.rts[fb->nr_cbufs].clear_stencil)
         clears |= PIPE_CLEAR_STENCIL;
      const uint64_t outputs_written = ctx->gfx_stages[PIPE_SHADER_FRAGMENT] ?
                                       ctx->gfx_stages[PIPE_SHADER_FRAGMENT]->nir->info.outputs_written : 0;
      bool needs_write = (ctx->dsa_state && ctx->dsa_state->hw_state.depth_write) ||
                                            outputs_written & (BITFIELD64_BIT(FRAG_RESULT_DEPTH) | BITFIELD64_BIT(FRAG_RESULT_STENCIL));
      state.rts[fb->nr_cbufs].needs_write = needs_write || state.num_zsresolves || state.rts[fb->nr_cbufs].clear_color || state.rts[fb->nr_cbufs].clear_stencil;
      state.num_rts++;
   }
   state.have_zsbuf = fb->zsbuf != NULL;
   assert(clears == ctx->rp_clears_enabled);
   state.clears = clears;
   uint32_t hash = hash_render_pass_state(&state);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(ctx->render_pass_cache, hash,
                                                                 &state);
   struct zink_render_pass *rp;
   if (entry) {
      rp = entry->data;
      assert(rp->state.clears == clears);
   } else {
      struct zink_render_pass_pipeline_state pstate;
      pstate.samples = state.samples;
      rp = zink_create_render_pass(screen, &state, &pstate);
      if (!_mesa_hash_table_insert_pre_hashed(ctx->render_pass_cache, hash, &rp->state, rp))
         return NULL;
      bool found = false;
      struct set_entry *entry = _mesa_set_search_or_add(&ctx->render_pass_state_cache, &pstate, &found);
      struct zink_render_pass_pipeline_state *ppstate;
      if (!found) {
         entry->key = ralloc(ctx, struct zink_render_pass_pipeline_state);
         ppstate = (void*)entry->key;
         memcpy(ppstate, &pstate, rp_state_size(&pstate));
         ppstate->id = ctx->render_pass_state_cache.entries;
      }
      ppstate = (void*)entry->key;
      rp->pipeline_state = ppstate->id;
   }
   return rp;
}

static uint32_t
hash_framebuffer_imageless(const void *key)
{
   struct zink_framebuffer_state* s = (struct zink_framebuffer_state*)key;
   return _mesa_hash_data(key, offsetof(struct zink_framebuffer_state, infos) + sizeof(s->infos[0]) * s->num_attachments);
}

static bool
equals_framebuffer_imageless(const void *a, const void *b)
{
   struct zink_framebuffer_state *s = (struct zink_framebuffer_state*)a;
   return memcmp(a, b, offsetof(struct zink_framebuffer_state, infos) + sizeof(s->infos[0]) * s->num_attachments) == 0;
}

static void
setup_framebuffer(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_render_pass *rp = ctx->gfx_pipeline_state.render_pass;

   if (ctx->gfx_pipeline_state.sample_locations_enabled && ctx->sample_locations_changed) {
      unsigned samples = ctx->gfx_pipeline_state.rast_samples + 1;
      unsigned idx = util_logbase2_ceil(MAX2(samples, 1));
      VkExtent2D grid_size = screen->maxSampleLocationGridSize[idx];
 
      for (unsigned pixel = 0; pixel < grid_size.width * grid_size.height; pixel++) {
         for (unsigned sample = 0; sample < samples; sample++) {
            unsigned pixel_x = pixel % grid_size.width;
            unsigned pixel_y = pixel / grid_size.width;
            unsigned wi = pixel * samples + sample;
            unsigned ri = (pixel_y * grid_size.width + pixel_x % grid_size.width);
            ri = ri * samples + sample;
            ctx->vk_sample_locations[wi].x = (ctx->sample_locations[ri] & 0xf) / 16.0f;
            ctx->vk_sample_locations[wi].y = (16 - (ctx->sample_locations[ri] >> 4)) / 16.0f;
         }
      }
   }

   if (rp)
      ctx->rp_changed |= ctx->rp_clears_enabled != rp->state.clears;
   if (ctx->rp_changed)
      rp = get_render_pass(ctx);

   ctx->fb_changed |= rp != ctx->gfx_pipeline_state.render_pass;
   if (rp->pipeline_state != ctx->gfx_pipeline_state.rp_state) {
      ctx->gfx_pipeline_state.rp_state = rp->pipeline_state;
      ctx->gfx_pipeline_state.dirty = true;
   }

   ctx->rp_changed = false;

   if (!ctx->fb_changed)
      return;

   ctx->init_framebuffer(screen, ctx->framebuffer, rp);
   ctx->fb_changed = false;
   ctx->gfx_pipeline_state.render_pass = rp;
}

static VkImageView
prep_fb_attachment(struct zink_context *ctx, struct zink_surface *surf, unsigned i)
{
   if (!surf)
      return zink_csurface(ctx->dummy_surface[util_logbase2_ceil(ctx->fb_state.samples)])->image_view;

   zink_batch_resource_usage_set(&ctx->batch, zink_resource(surf->base.texture), true);
   zink_batch_usage_set(&surf->batch_uses, ctx->batch.state);

   struct zink_resource *res = zink_resource(surf->base.texture);
   VkAccessFlags access;
   VkPipelineStageFlags pipeline;
   VkImageLayout layout = zink_render_pass_attachment_get_barrier_info(ctx->gfx_pipeline_state.render_pass,
                                                                       i, &pipeline, &access);
   zink_resource_image_barrier(ctx, res, layout, access, pipeline);
   return surf->image_view;
}

static void
prep_fb_attachments(struct zink_context *ctx, VkImageView *att)
{
   const unsigned cresolve_offset = ctx->fb_state.nr_cbufs + !!ctx->fb_state.zsbuf;
   unsigned num_resolves = 0;
   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      struct zink_surface *surf = zink_csurface(ctx->fb_state.cbufs[i]);
      struct zink_surface *transient = zink_transient_surface(ctx->fb_state.cbufs[i]);
      if (transient) {
         att[i] = prep_fb_attachment(ctx, transient, i);
         att[i + cresolve_offset] = prep_fb_attachment(ctx, surf, i);
         num_resolves++;
      } else {
         att[i] = prep_fb_attachment(ctx, surf, i);
      }
   }
   if (ctx->fb_state.zsbuf) {
      struct zink_surface *surf = zink_csurface(ctx->fb_state.zsbuf);
      struct zink_surface *transient = zink_transient_surface(ctx->fb_state.zsbuf);
      if (transient) {
         att[ctx->fb_state.nr_cbufs] = prep_fb_attachment(ctx, transient, ctx->fb_state.nr_cbufs);
         att[cresolve_offset + num_resolves] = prep_fb_attachment(ctx, surf, ctx->fb_state.nr_cbufs);
      } else {
         att[ctx->fb_state.nr_cbufs] = prep_fb_attachment(ctx, surf, ctx->fb_state.nr_cbufs);
      }
   }
}

static void
update_framebuffer_state(struct zink_context *ctx, int old_w, int old_h)
{
   if (ctx->fb_state.width != old_w || ctx->fb_state.height != old_h)
      ctx->scissor_changed = true;
   /* get_framebuffer adds a ref if the fb is reused or created;
    * always do get_framebuffer first to avoid deleting the same fb
    * we're about to use
    */
   struct zink_framebuffer *fb = ctx->get_framebuffer(ctx);
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (ctx->framebuffer && !screen->info.have_KHR_imageless_framebuffer) {
      simple_mtx_lock(&screen->framebuffer_mtx);
      struct hash_entry *he = _mesa_hash_table_search(&screen->framebuffer_cache, &ctx->framebuffer->state);
      if (ctx->framebuffer && !ctx->framebuffer->state.num_attachments) {
         /* if this has no attachments then its lifetime has ended */
         _mesa_hash_table_remove(&screen->framebuffer_cache, he);
         he = NULL;
         /* ensure an unflushed fb doesn't get destroyed by deferring it */
         util_dynarray_append(&ctx->batch.state->dead_framebuffers, struct zink_framebuffer*, ctx->framebuffer);
         ctx->framebuffer = NULL;
      }
      /* a framebuffer loses 1 ref every time we unset it;
       * we do NOT add refs here, as the ref has already been added in
       * get_framebuffer()
       */
      if (zink_framebuffer_reference(screen, &ctx->framebuffer, NULL) && he)
         _mesa_hash_table_remove(&screen->framebuffer_cache, he);
      simple_mtx_unlock(&screen->framebuffer_mtx);
   }
   ctx->fb_changed |= ctx->framebuffer != fb;
   ctx->framebuffer = fb;
}

static unsigned
begin_render_pass(struct zink_context *ctx)
{
   struct zink_batch *batch = &ctx->batch;
   struct pipe_framebuffer_state *fb_state = &ctx->fb_state;

   VkRenderPassBeginInfo rpbi = {0};
   rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rpbi.renderPass = ctx->gfx_pipeline_state.render_pass->render_pass;
   rpbi.renderArea.offset.x = 0;
   rpbi.renderArea.offset.y = 0;
   rpbi.renderArea.extent.width = fb_state->width;
   rpbi.renderArea.extent.height = fb_state->height;

   VkClearValue clears[PIPE_MAX_COLOR_BUFS + 1] = {0};
   unsigned clear_buffers = 0;
   uint32_t clear_validate = 0;
   for (int i = 0; i < fb_state->nr_cbufs; i++) {
      /* these are no-ops */
      if (!fb_state->cbufs[i] || !zink_fb_clear_enabled(ctx, i))
         continue;
      /* these need actual clear calls inside the rp */
      struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(&ctx->fb_clears[i], 0);
      if (zink_fb_clear_needs_explicit(&ctx->fb_clears[i])) {
         clear_buffers |= (PIPE_CLEAR_COLOR0 << i);
         if (zink_fb_clear_count(&ctx->fb_clears[i]) < 2 ||
             zink_fb_clear_element_needs_explicit(clear))
            continue;
      }
      /* we now know there's one clear that can be done here */
      zink_fb_clear_util_unpack_clear_color(clear, fb_state->cbufs[i]->format, (void*)&clears[i].color);
      rpbi.clearValueCount = i + 1;
      clear_validate |= PIPE_CLEAR_COLOR0 << i;
      assert(ctx->framebuffer->rp->state.clears);
   }
   if (fb_state->zsbuf && zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS)) {
      struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
      struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(fb_clear, 0);
      if (!zink_fb_clear_element_needs_explicit(clear)) {
         clears[fb_state->nr_cbufs].depthStencil.depth = clear->zs.depth;
         clears[fb_state->nr_cbufs].depthStencil.stencil = clear->zs.stencil;
         rpbi.clearValueCount = fb_state->nr_cbufs + 1;
         clear_validate |= clear->zs.bits;
         assert(ctx->framebuffer->rp->state.clears);
      }
      if (zink_fb_clear_needs_explicit(fb_clear)) {
         for (int j = !zink_fb_clear_element_needs_explicit(clear);
              (clear_buffers & PIPE_CLEAR_DEPTHSTENCIL) != PIPE_CLEAR_DEPTHSTENCIL && j < zink_fb_clear_count(fb_clear);
              j++)
            clear_buffers |= zink_fb_clear_element(fb_clear, j)->zs.bits;
      }
   }
   assert(clear_validate == ctx->framebuffer->rp->state.clears);
   rpbi.pClearValues = &clears[0];
   rpbi.framebuffer = ctx->framebuffer->fb;

   assert(ctx->gfx_pipeline_state.render_pass && ctx->framebuffer);

   VkRenderPassAttachmentBeginInfo infos;
   VkImageView att[2 * (PIPE_MAX_COLOR_BUFS + 1)];
   infos.sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
   infos.pNext = NULL;
   infos.attachmentCount = ctx->framebuffer->state.num_attachments;
   infos.pAttachments = att;
   prep_fb_attachments(ctx, att);
   if (zink_screen(ctx->base.screen)->info.have_KHR_imageless_framebuffer) {
#ifndef NDEBUG
      const unsigned cresolve_offset = ctx->fb_state.nr_cbufs + !!ctx->fb_state.zsbuf;
      for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (ctx->fb_state.cbufs[i]) {
            struct zink_surface *surf = zink_csurface(ctx->fb_state.cbufs[i]);
            struct zink_surface *transient = zink_transient_surface(ctx->fb_state.cbufs[i]);
            if (surf->base.format == ctx->fb_state.cbufs[i]->format) {
               if (transient) {
                  assert(zink_resource(transient->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[i].usage);
                  assert(zink_resource(surf->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[cresolve_offset].usage);
               } else {
                  assert(zink_resource(surf->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[i].usage);
               }
            }
         }
      }
      if (ctx->fb_state.zsbuf) {
         struct zink_surface *surf = zink_csurface(ctx->fb_state.zsbuf);
         struct zink_surface *transient = zink_transient_surface(ctx->fb_state.zsbuf);
         if (transient) {
            assert(zink_resource(transient->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[ctx->fb_state.nr_cbufs].usage);
            assert(zink_resource(surf->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[cresolve_offset].usage);
         } else {
            assert(zink_resource(surf->base.texture)->obj->vkusage == ctx->framebuffer->state.infos[ctx->fb_state.nr_cbufs].usage);
         }
      }
#endif
      rpbi.pNext = &infos;
   }

   VKCTX(CmdBeginRenderPass)(batch->state->cmdbuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   batch->in_rp = true;
   ctx->new_swapchain = false;
   return clear_buffers;
}

void
zink_init_vk_sample_locations(struct zink_context *ctx, VkSampleLocationsInfoEXT *loc)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned idx = util_logbase2_ceil(MAX2(ctx->gfx_pipeline_state.rast_samples + 1, 1));
   loc->sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
   loc->pNext = NULL;
   loc->sampleLocationsPerPixel = 1 << idx;
   loc->sampleLocationsCount = ctx->gfx_pipeline_state.rast_samples + 1;
   loc->sampleLocationGridSize = screen->maxSampleLocationGridSize[idx];
   loc->pSampleLocations = ctx->vk_sample_locations;
}

static void
zink_evaluate_depth_buffer(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);

   if (!ctx->fb_state.zsbuf)
      return;

   struct zink_resource *res = zink_resource(ctx->fb_state.zsbuf->texture);
   res->obj->needs_zs_evaluate = true;
   zink_init_vk_sample_locations(ctx, &res->obj->zs_evaluate);
   zink_batch_no_rp(ctx);
}

void
zink_begin_render_pass(struct zink_context *ctx)
{
   setup_framebuffer(ctx);
   /* TODO: need replicate EXT */
   if (ctx->framebuffer->rp->state.msaa_expand_mask) {
      uint32_t rp_state = ctx->gfx_pipeline_state.rp_state;
      struct zink_render_pass *rp = ctx->gfx_pipeline_state.render_pass;

      u_foreach_bit(i, ctx->framebuffer->rp->state.msaa_expand_mask) {
         struct zink_ctx_surface *csurf = (struct zink_ctx_surface*)ctx->fb_state.cbufs[i];
         if (csurf->transient_init)
            continue;
         struct pipe_surface *dst_view = (struct pipe_surface*)csurf->transient;
         assert(dst_view);
         struct pipe_sampler_view src_templ, *src_view;
         struct pipe_resource *src = ctx->fb_state.cbufs[i]->texture;
         struct pipe_box dstbox;

         u_box_3d(0, 0, 0, ctx->fb_state.width, ctx->fb_state.height,
                  1 + dst_view->u.tex.last_layer - dst_view->u.tex.first_layer, &dstbox);

         util_blitter_default_src_texture(ctx->blitter, &src_templ, src, ctx->fb_state.cbufs[i]->u.tex.level);
         src_view = ctx->base.create_sampler_view(&ctx->base, src, &src_templ);

         zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);
         util_blitter_blit_generic(ctx->blitter, dst_view, &dstbox,
                                   src_view, &dstbox, ctx->fb_state.width, ctx->fb_state.height,
                                   PIPE_MASK_RGBAZS, PIPE_TEX_FILTER_NEAREST, NULL,
                                   false, false);

         pipe_sampler_view_reference(&src_view, NULL);
         csurf->transient_init = true;
      }
      ctx->fb_changed = ctx->rp_changed = false;
      ctx->gfx_pipeline_state.rp_state = rp_state;
      ctx->gfx_pipeline_state.render_pass = rp;
   }
   assert(ctx->gfx_pipeline_state.render_pass);
   unsigned clear_buffers = begin_render_pass(ctx);

   if (ctx->render_condition.query)
      zink_start_conditional_render(ctx);
   zink_clear_framebuffer(ctx, clear_buffers);
}

void
zink_end_render_pass(struct zink_context *ctx)
{
   if (ctx->batch.in_rp) {
      if (ctx->render_condition.query)
         zink_stop_conditional_render(ctx);
      VKCTX(CmdEndRenderPass)(ctx->batch.state->cmdbuf);
      for (unsigned i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         struct zink_ctx_surface *csurf = (struct zink_ctx_surface*)ctx->fb_state.cbufs[i];
         if (csurf)
            csurf->transient_init = true;
      }
   }
   ctx->batch.in_rp = false;
}

static void
sync_flush(struct zink_context *ctx, struct zink_batch_state *bs)
{
   if (zink_screen(ctx->base.screen)->threaded)
      util_queue_fence_wait(&bs->flush_completed);
}

static inline VkAccessFlags
get_access_flags_for_binding(struct zink_context *ctx, enum zink_descriptor_type type, enum pipe_shader_type stage, unsigned idx)
{
   VkAccessFlags flags = 0;
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return VK_ACCESS_UNIFORM_READ_BIT;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return VK_ACCESS_SHADER_READ_BIT;
   case ZINK_DESCRIPTOR_TYPE_SSBO: {
      flags = VK_ACCESS_SHADER_READ_BIT;
      if (ctx->writable_ssbos[stage] & (1 << idx))
         flags |= VK_ACCESS_SHADER_WRITE_BIT;
      return flags;
   }
   case ZINK_DESCRIPTOR_TYPE_IMAGE: {
      struct zink_image_view *image_view = &ctx->image_views[stage][idx];
      if (image_view->base.access & PIPE_IMAGE_ACCESS_READ)
         flags |= VK_ACCESS_SHADER_READ_BIT;
      if (image_view->base.access & PIPE_IMAGE_ACCESS_WRITE)
         flags |= VK_ACCESS_SHADER_WRITE_BIT;
      return flags;
   }
   default:
      break;
   }
   unreachable("ACK");
   return 0;
}

static void
update_resource_refs_for_stage(struct zink_context *ctx, enum pipe_shader_type stage)
{
   struct zink_batch *batch = &ctx->batch;
   unsigned max_slot[] = {
      [ZINK_DESCRIPTOR_TYPE_UBO] = ctx->di.num_ubos[stage],
      [ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW] = ctx->di.num_samplers[stage],
      [ZINK_DESCRIPTOR_TYPE_SSBO] = ctx->di.num_ssbos[stage],
      [ZINK_DESCRIPTOR_TYPE_IMAGE] = ctx->di.num_images[stage]
   };
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      for (unsigned j = 0; j < max_slot[i]; j++) {
         if (ctx->di.descriptor_res[i][stage][j]) {
            struct zink_resource *res = ctx->di.descriptor_res[i][stage][j];
            if (!res)
               continue;
            bool is_write = zink_resource_access_is_write(get_access_flags_for_binding(ctx, i, stage, j));
            zink_batch_resource_usage_set(batch, res, is_write);

            struct zink_sampler_view *sv = zink_sampler_view(ctx->sampler_views[stage][j]);
            struct zink_sampler_state *sampler_state = ctx->sampler_states[stage][j];
            struct zink_image_view *iv = &ctx->image_views[stage][j];
            if (sampler_state && i == ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW && j <= ctx->di.num_samplers[stage])
               zink_batch_usage_set(&sampler_state->batch_uses, ctx->batch.state);
            if (sv && i == ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW && j <= ctx->di.num_sampler_views[stage]) {
               if (res->obj->is_buffer)
                  zink_batch_usage_set(&sv->buffer_view->batch_uses, ctx->batch.state);
               else
                  zink_batch_usage_set(&sv->image_view->batch_uses, ctx->batch.state);
               zink_batch_reference_sampler_view(batch, sv);
            } else if (i == ZINK_DESCRIPTOR_TYPE_IMAGE && j <= ctx->di.num_images[stage]) {
               if (res->obj->is_buffer)
                  zink_batch_usage_set(&iv->buffer_view->batch_uses, ctx->batch.state);
               else
                  zink_batch_usage_set(&iv->surface->batch_uses, ctx->batch.state);
               zink_batch_reference_image_view(batch, iv);
            }
         }
      }
   }
}

void
zink_update_descriptor_refs(struct zink_context *ctx, bool compute)
{
   struct zink_batch *batch = &ctx->batch;
   if (compute) {
      update_resource_refs_for_stage(ctx, PIPE_SHADER_COMPUTE);
      if (ctx->curr_compute)
         zink_batch_reference_program(batch, &ctx->curr_compute->base);
   } else {
      for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++)
         update_resource_refs_for_stage(ctx, i);
      unsigned vertex_buffers_enabled_mask = ctx->gfx_pipeline_state.vertex_buffers_enabled_mask;
      unsigned last_vbo = util_last_bit(vertex_buffers_enabled_mask);
      for (unsigned i = 0; i < last_vbo + 1; i++) {
         if (ctx->vertex_buffers[i].buffer.resource)
            zink_batch_resource_usage_set(batch, zink_resource(ctx->vertex_buffers[i].buffer.resource), false);
      }
      if (ctx->curr_program)
         zink_batch_reference_program(batch, &ctx->curr_program->base);
   }
   if (ctx->di.bindless_refs_dirty) {
      ctx->di.bindless_refs_dirty = false;
      for (unsigned i = 0; i < 2; i++) {
         util_dynarray_foreach(&ctx->di.bindless[i].resident, struct zink_bindless_descriptor*, bd) {
            struct zink_resource *res = zink_descriptor_surface_resource(&(*bd)->ds);
            zink_batch_resource_usage_set(&ctx->batch, res, (*bd)->access & PIPE_IMAGE_ACCESS_WRITE);
         }
      }
   }
}

static void
stall(struct zink_context *ctx)
{
   sync_flush(ctx, zink_batch_state(ctx->last_fence));
   zink_vkfence_wait(zink_screen(ctx->base.screen), ctx->last_fence, PIPE_TIMEOUT_INFINITE);
   zink_batch_reset_all(ctx);
}

static void
flush_batch(struct zink_context *ctx, bool sync)
{
   struct zink_batch *batch = &ctx->batch;
   if (ctx->clears_enabled)
      /* start rp to do all the clears */
      zink_begin_render_pass(ctx);
   zink_end_render_pass(ctx);
   zink_end_batch(ctx, batch);
   ctx->deferred_fence = NULL;

   if (sync)
      sync_flush(ctx, ctx->batch.state);

   if (ctx->batch.state->is_device_lost) {
      check_device_lost(ctx);
   } else {
      zink_start_batch(ctx, batch);
      if (zink_screen(ctx->base.screen)->info.have_EXT_transform_feedback && ctx->num_so_targets)
         ctx->dirty_so_targets = true;
      ctx->pipeline_changed[0] = ctx->pipeline_changed[1] = true;
      zink_select_draw_vbo(ctx);
      zink_select_launch_grid(ctx);

      if (ctx->oom_stall)
         stall(ctx);
      ctx->oom_flush = false;
      ctx->oom_stall = false;
      ctx->dd->bindless_bound = false;
      ctx->di.bindless_refs_dirty = true;
   }
}

void
zink_flush_queue(struct zink_context *ctx)
{
   flush_batch(ctx, true);
}

static bool
rebind_fb_surface(struct zink_context *ctx, struct pipe_surface **surf, struct zink_resource *match_res)
{
   if (!*surf)
      return false;
   struct zink_resource *surf_res = zink_resource((*surf)->texture);
   if ((match_res == surf_res) || surf_res->obj != zink_csurface(*surf)->obj)
      return zink_rebind_ctx_surface(ctx, surf);
   return false;
}

static bool
rebind_fb_state(struct zink_context *ctx, struct zink_resource *match_res, bool from_set_fb)
{
   bool rebind = false;
   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      rebind |= rebind_fb_surface(ctx, &ctx->fb_state.cbufs[i], match_res);
      if (from_set_fb && ctx->fb_state.cbufs[i] && ctx->fb_state.cbufs[i]->texture->bind & PIPE_BIND_SCANOUT)
         ctx->new_swapchain = true;
   }
   rebind |= rebind_fb_surface(ctx, &ctx->fb_state.zsbuf, match_res);
   return rebind;
}

static void
unbind_fb_surface(struct zink_context *ctx, struct pipe_surface *surf, bool changed)
{
   if (!surf)
      return;
   struct zink_surface *transient = zink_transient_surface(surf);
   if (changed) {
      zink_fb_clears_apply(ctx, surf->texture);
      if (zink_batch_usage_exists(zink_csurface(surf)->batch_uses)) {
         zink_batch_reference_surface(&ctx->batch, zink_csurface(surf));
         if (transient)
            zink_batch_reference_surface(&ctx->batch, transient);
      }
      ctx->rp_changed = true;
   }
   struct zink_resource *res = zink_resource(surf->texture);
   res->fb_binds--;
   if (!res->fb_binds)
      check_resource_for_batch_ref(ctx, res);
}

static void
zink_set_framebuffer_state(struct pipe_context *pctx,
                           const struct pipe_framebuffer_state *state)
{
   struct zink_context *ctx = zink_context(pctx);
   unsigned samples = state->nr_cbufs || state->zsbuf ? 0 : state->samples;

   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      struct pipe_surface *surf = ctx->fb_state.cbufs[i];
      if (i < state->nr_cbufs)
         ctx->rp_changed |= !!zink_transient_surface(surf) != !!zink_transient_surface(state->cbufs[i]);
      unbind_fb_surface(ctx, surf, i >= state->nr_cbufs || surf != state->cbufs[i]);
   }
   if (ctx->fb_state.zsbuf) {
      struct pipe_surface *surf = ctx->fb_state.zsbuf;
      struct zink_resource *res = zink_resource(surf->texture);
      bool changed = surf != state->zsbuf;
      unbind_fb_surface(ctx, surf, changed);
      if (!changed)
         ctx->rp_changed |= !!zink_transient_surface(surf) != !!zink_transient_surface(state->zsbuf);
      if (changed && unlikely(res->obj->needs_zs_evaluate))
         /* have to flush zs eval while the sample location data still exists,
          * so just throw some random barrier */
         zink_resource_image_barrier(ctx, res, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
   }
   /* renderpass changes if the number or types of attachments change */
   ctx->rp_changed |= ctx->fb_state.nr_cbufs != state->nr_cbufs;
   ctx->rp_changed |= !!ctx->fb_state.zsbuf != !!state->zsbuf;

   unsigned w = ctx->fb_state.width;
   unsigned h = ctx->fb_state.height;

   util_copy_framebuffer_state(&ctx->fb_state, state);
   zink_update_fbfetch(ctx);
   unsigned prev_void_alpha_attachments = ctx->gfx_pipeline_state.void_alpha_attachments;
   ctx->gfx_pipeline_state.void_alpha_attachments = 0;
   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      struct pipe_surface *surf = ctx->fb_state.cbufs[i];
      if (surf) {
         struct zink_surface *transient = zink_transient_surface(surf);
         if (!samples)
            samples = MAX3(transient ? transient->base.nr_samples : 1, surf->texture->nr_samples, 1);
         zink_resource(surf->texture)->fb_binds++;
         ctx->gfx_pipeline_state.void_alpha_attachments |= util_format_has_alpha1(surf->format) ? BITFIELD_BIT(i) : 0;
      }
   }
   if (ctx->gfx_pipeline_state.void_alpha_attachments != prev_void_alpha_attachments)
      ctx->gfx_pipeline_state.dirty = true;
   if (ctx->fb_state.zsbuf) {
      struct pipe_surface *surf = ctx->fb_state.zsbuf;
      struct zink_surface *transient = zink_transient_surface(surf);
      if (!samples)
         samples = MAX3(transient ? transient->base.nr_samples : 1, surf->texture->nr_samples, 1);
      zink_resource(surf->texture)->fb_binds++;
   }
   rebind_fb_state(ctx, NULL, true);
   ctx->fb_state.samples = MAX2(samples, 1);
   update_framebuffer_state(ctx, w, h);

   uint8_t rast_samples = ctx->fb_state.samples - 1;
   if (rast_samples != ctx->gfx_pipeline_state.rast_samples)
      zink_update_fs_key_samples(ctx);
   if (ctx->gfx_pipeline_state.rast_samples != rast_samples) {
      ctx->sample_locations_changed |= ctx->gfx_pipeline_state.sample_locations_enabled;
      ctx->gfx_pipeline_state.dirty = true;
   }
   ctx->gfx_pipeline_state.rast_samples = rast_samples;

   /* need to ensure we start a new rp on next draw */
   zink_batch_no_rp(ctx);
   /* this is an ideal time to oom flush since it won't split a renderpass */
   if (ctx->oom_flush)
      flush_batch(ctx, false);
}

static void
zink_set_blend_color(struct pipe_context *pctx,
                     const struct pipe_blend_color *color)
{
   struct zink_context *ctx = zink_context(pctx);
   memcpy(ctx->blend_constants, color->color, sizeof(float) * 4);
}

static void
zink_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
   struct zink_context *ctx = zink_context(pctx);
   ctx->gfx_pipeline_state.sample_mask = sample_mask;
   ctx->gfx_pipeline_state.dirty = true;
}

static void
zink_set_sample_locations(struct pipe_context *pctx, size_t size, const uint8_t *locations)
{
   struct zink_context *ctx = zink_context(pctx);

   ctx->gfx_pipeline_state.sample_locations_enabled = size && locations;
   ctx->sample_locations_changed = ctx->gfx_pipeline_state.sample_locations_enabled;
   if (size > sizeof(ctx->sample_locations))
      size = sizeof(ctx->sample_locations);

   if (locations)
      memcpy(ctx->sample_locations, locations, size);
}

static VkAccessFlags
access_src_flags(VkImageLayout layout)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
      return 0;

   case VK_IMAGE_LAYOUT_GENERAL:
      return VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_ACCESS_SHADER_READ_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_TRANSFER_READ_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_ACCESS_TRANSFER_WRITE_BIT;

   case VK_IMAGE_LAYOUT_PREINITIALIZED:
      return VK_ACCESS_HOST_WRITE_BIT;

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return 0;

   default:
      unreachable("unexpected layout");
   }
}

static VkAccessFlags
access_dst_flags(VkImageLayout layout)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_UNDEFINED:
      return 0;

   case VK_IMAGE_LAYOUT_GENERAL:
      return VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return VK_ACCESS_SHADER_READ_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_TRANSFER_READ_BIT;

   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      return VK_ACCESS_SHADER_READ_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_ACCESS_TRANSFER_WRITE_BIT;

   case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return 0;

   default:
      unreachable("unexpected layout");
   }
}

static VkPipelineStageFlags
pipeline_dst_stage(VkImageLayout layout)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

   case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;

   case VK_IMAGE_LAYOUT_GENERAL:
      return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

   default:
      return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
   }
}

#define ALL_READ_ACCESS_FLAGS \
    (VK_ACCESS_INDIRECT_COMMAND_READ_BIT | \
    VK_ACCESS_INDEX_READ_BIT | \
    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | \
    VK_ACCESS_UNIFORM_READ_BIT | \
    VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | \
    VK_ACCESS_SHADER_READ_BIT | \
    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | \
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | \
    VK_ACCESS_TRANSFER_READ_BIT |\
    VK_ACCESS_HOST_READ_BIT |\
    VK_ACCESS_MEMORY_READ_BIT |\
    VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |\
    VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT |\
    VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT |\
    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |\
    VK_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV |\
    VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT |\
    VK_ACCESS_COMMAND_PREPROCESS_READ_BIT_NV |\
    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV |\
    VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV)


bool
zink_resource_access_is_write(VkAccessFlags flags)
{
   return (flags & ALL_READ_ACCESS_FLAGS) != flags;
}

bool
zink_resource_image_needs_barrier(struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   if (!pipeline)
      pipeline = pipeline_dst_stage(new_layout);
   if (!flags)
      flags = access_dst_flags(new_layout);
   return res->layout != new_layout || (res->obj->access_stage & pipeline) != pipeline ||
          (res->obj->access & flags) != flags ||
          zink_resource_access_is_write(res->obj->access) ||
          zink_resource_access_is_write(flags);
}

bool
zink_resource_image_barrier_init(VkImageMemoryBarrier *imb, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   if (!pipeline)
      pipeline = pipeline_dst_stage(new_layout);
   if (!flags)
      flags = access_dst_flags(new_layout);

   VkImageSubresourceRange isr = {
      res->aspect,
      0, VK_REMAINING_MIP_LEVELS,
      0, VK_REMAINING_ARRAY_LAYERS
   };
   *imb = (VkImageMemoryBarrier){
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      NULL,
      res->obj->access ? res->obj->access : access_src_flags(res->layout),
      flags,
      res->layout,
      new_layout,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      res->obj->image,
      isr
   };
   return res->obj->needs_zs_evaluate || zink_resource_image_needs_barrier(res, new_layout, flags, pipeline);
}

static inline bool
is_shader_pipline_stage(VkPipelineStageFlags pipeline)
{
   return pipeline & (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                      VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                      VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                      VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

static void
resource_check_defer_buffer_barrier(struct zink_context *ctx, struct zink_resource *res, VkPipelineStageFlags pipeline)
{
   assert(res->obj->is_buffer);
   if (res->bind_count[0] - res->so_bind_count > 0) {
      if ((res->obj->is_buffer && res->vbo_bind_mask && !(pipeline & VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)) ||
          ((!res->obj->is_buffer || util_bitcount(res->vbo_bind_mask) != res->bind_count[0]) && !is_shader_pipline_stage(pipeline)))
         /* gfx rebind */
         _mesa_set_add(ctx->need_barriers[0], res);
   }
   if (res->bind_count[1] && !(pipeline & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))
      /* compute rebind */
      _mesa_set_add(ctx->need_barriers[1], res);
}

static inline VkCommandBuffer
get_cmdbuf(struct zink_context *ctx, struct zink_resource *res)
{
   if ((res->obj->access && !res->obj->unordered_barrier) || !ctx->batch.in_rp) {
      zink_batch_no_rp(ctx);
      res->obj->unordered_barrier = false;
      return ctx->batch.state->cmdbuf;
   }
   res->obj->unordered_barrier = true;
   ctx->batch.state->has_barriers = true;
   return ctx->batch.state->barrier_cmdbuf;
}

static void
resource_check_defer_image_barrier(struct zink_context *ctx, struct zink_resource *res, VkImageLayout layout, VkPipelineStageFlags pipeline)
{
   assert(!res->obj->is_buffer);

   bool is_compute = pipeline == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   /* if this is a non-shader barrier and there are binds, always queue a shader barrier */
   bool is_shader = is_shader_pipline_stage(pipeline);
   if ((is_shader || !res->bind_count[is_compute]) &&
       /* if no layout change is needed between gfx and compute, do nothing */
       !res->bind_count[!is_compute] && (!is_compute || !res->fb_binds))
      return;

   if (res->bind_count[!is_compute] && is_shader) {
      /* if the layout is the same between gfx and compute, do nothing */
      if (layout == zink_descriptor_util_image_layout_eval(res, !is_compute))
         return;
   }
   /* queue a layout change if a layout change will be needed */
   if (res->bind_count[!is_compute])
      _mesa_set_add(ctx->need_barriers[!is_compute], res);
   /* also queue a layout change if this is a non-shader layout */
   if (res->bind_count[is_compute] && !is_shader)
      _mesa_set_add(ctx->need_barriers[is_compute], res);
}

void
zink_resource_image_barrier(struct zink_context *ctx, struct zink_resource *res,
                      VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   VkImageMemoryBarrier imb;
   if (!pipeline)
      pipeline = pipeline_dst_stage(new_layout);

   if (!zink_resource_image_barrier_init(&imb, res, new_layout, flags, pipeline))
      return;
   /* only barrier if we're changing layout or doing something besides read -> read */
   VkCommandBuffer cmdbuf = get_cmdbuf(ctx, res);
   assert(new_layout);
   if (!res->obj->access_stage)
      imb.srcAccessMask = 0;
   if (res->obj->needs_zs_evaluate)
      imb.pNext = &res->obj->zs_evaluate;
   res->obj->needs_zs_evaluate = false;
   if (res->dmabuf_acquire) {
      imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
      imb.dstQueueFamilyIndex = zink_screen(ctx->base.screen)->gfx_queue;
      res->dmabuf_acquire = false;
   }
   VKCTX(CmdPipelineBarrier)(
      cmdbuf,
      res->obj->access_stage ? res->obj->access_stage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      pipeline,
      0,
      0, NULL,
      0, NULL,
      1, &imb
   );

   resource_check_defer_image_barrier(ctx, res, new_layout, pipeline);

   if (res->obj->unordered_barrier) {
      res->obj->access |= imb.dstAccessMask;
      res->obj->access_stage |= pipeline;
   } else {
      res->obj->access = imb.dstAccessMask;
      res->obj->access_stage = pipeline;
   }
   res->layout = new_layout;
}


VkPipelineStageFlags
zink_pipeline_flags_from_stage(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   default:
      unreachable("unknown shader stage bit");
   }
}

ALWAYS_INLINE static VkPipelineStageFlags
pipeline_access_stage(VkAccessFlags flags)
{
   if (flags & (VK_ACCESS_UNIFORM_READ_BIT |
                VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_SHADER_WRITE_BIT))
      return VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV |
             VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV |
             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
             VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
             VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
             VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   return VK_PIPELINE_STAGE_TRANSFER_BIT;
}

ALWAYS_INLINE static bool
zink_resource_buffer_needs_barrier(struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   if (!res->obj->access || !res->obj->access_stage)
      return true;
   if (!pipeline)
      pipeline = pipeline_access_stage(flags);
   return zink_resource_access_is_write(res->obj->access) ||
          zink_resource_access_is_write(flags) ||
          ((res->obj->access_stage & pipeline) != pipeline && !(res->obj->access_stage & (pipeline - 1))) ||
          (res->obj->access & flags) != flags;
}

void
zink_resource_buffer_barrier(struct zink_context *ctx, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   VkMemoryBarrier bmb;
   if (!pipeline)
      pipeline = pipeline_access_stage(flags);
   if (!zink_resource_buffer_needs_barrier(res, flags, pipeline))
      return;

   bmb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
   bmb.pNext = NULL;
   bmb.srcAccessMask = res->obj->access;
   bmb.dstAccessMask = flags;
   if (!res->obj->access_stage)
      bmb.srcAccessMask = 0;
   VkCommandBuffer cmdbuf = get_cmdbuf(ctx, res);
   /* only barrier if we're changing layout or doing something besides read -> read */
   VKCTX(CmdPipelineBarrier)(
      cmdbuf,
      res->obj->access_stage ? res->obj->access_stage : pipeline_access_stage(res->obj->access),
      pipeline,
      0,
      1, &bmb,
      0, NULL,
      0, NULL
   );

   resource_check_defer_buffer_barrier(ctx, res, pipeline);

   if (res->obj->unordered_barrier) {
      res->obj->access |= bmb.dstAccessMask;
      res->obj->access_stage |= pipeline;
   } else {
      res->obj->access = bmb.dstAccessMask;
      res->obj->access_stage = pipeline;
   }
}

bool
zink_resource_needs_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   if (res->base.b.target == PIPE_BUFFER)
      return zink_resource_buffer_needs_barrier(res, flags, pipeline);
   return zink_resource_image_needs_barrier(res, layout, flags, pipeline);
}

VkShaderStageFlagBits
zink_shader_stage(enum pipe_shader_type type)
{
   VkShaderStageFlagBits stages[] = {
      [PIPE_SHADER_VERTEX] = VK_SHADER_STAGE_VERTEX_BIT,
      [PIPE_SHADER_FRAGMENT] = VK_SHADER_STAGE_FRAGMENT_BIT,
      [PIPE_SHADER_GEOMETRY] = VK_SHADER_STAGE_GEOMETRY_BIT,
      [PIPE_SHADER_TESS_CTRL] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      [PIPE_SHADER_TESS_EVAL] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      [PIPE_SHADER_COMPUTE] = VK_SHADER_STAGE_COMPUTE_BIT,
   };
   return stages[type];
}

static void
zink_flush(struct pipe_context *pctx,
           struct pipe_fence_handle **pfence,
           unsigned flags)
{
   struct zink_context *ctx = zink_context(pctx);
   bool deferred = flags & PIPE_FLUSH_DEFERRED;
   bool deferred_fence = false;
   struct zink_batch *batch = &ctx->batch;
   struct zink_fence *fence = NULL;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned submit_count = 0;

   /* triggering clears will force has_work */
   if (!deferred && ctx->clears_enabled)
      /* start rp to do all the clears */
      zink_begin_render_pass(ctx);

   if (!batch->has_work) {
       if (pfence) {
          /* reuse last fence */
          fence = ctx->last_fence;
       }
       if (!deferred) {
          struct zink_batch_state *last = zink_batch_state(ctx->last_fence);
          if (last) {
             sync_flush(ctx, last);
             if (last->is_device_lost)
                check_device_lost(ctx);
          }
       }
       tc_driver_internal_flush_notify(ctx->tc);
   } else {
      fence = &batch->state->fence;
      submit_count = batch->state->submit_count;
      if (deferred && !(flags & PIPE_FLUSH_FENCE_FD) && pfence)
         deferred_fence = true;
      else
         flush_batch(ctx, true);
   }

   if (pfence) {
      struct zink_tc_fence *mfence;

      if (flags & TC_FLUSH_ASYNC) {
         mfence = zink_tc_fence(*pfence);
         assert(mfence);
      } else {
         mfence = zink_create_tc_fence();

         screen->base.fence_reference(&screen->base, pfence, NULL);
         *pfence = (struct pipe_fence_handle *)mfence;
      }

      mfence->fence = fence;
      if (fence)
         mfence->submit_count = submit_count;

      if (deferred_fence) {
         assert(fence);
         mfence->deferred_ctx = pctx;
         assert(!ctx->deferred_fence || ctx->deferred_fence == fence);
         ctx->deferred_fence = fence;
      }

      if (!fence || flags & TC_FLUSH_ASYNC) {
         if (!util_queue_fence_is_signalled(&mfence->ready))
            util_queue_fence_signal(&mfence->ready);
      }
   }
   if (fence) {
      if (!(flags & (PIPE_FLUSH_DEFERRED | PIPE_FLUSH_ASYNC)))
         sync_flush(ctx, zink_batch_state(fence));

      if (flags & PIPE_FLUSH_END_OF_FRAME && !(flags & TC_FLUSH_ASYNC) && !deferred) {
         /* if the first frame has not yet occurred, we need an explicit fence here
         * in some cases in order to correctly draw the first frame, though it's
         * unknown at this time why this is the case
         */
         if (!ctx->first_frame_done)
            zink_vkfence_wait(screen, fence, PIPE_TIMEOUT_INFINITE);
         ctx->first_frame_done = true;
      }
   }
}

void
zink_fence_wait(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);

   if (ctx->batch.has_work)
      pctx->flush(pctx, NULL, PIPE_FLUSH_HINT_FINISH);
   if (ctx->last_fence)
      stall(ctx);
}

void
zink_wait_on_batch(struct zink_context *ctx, uint32_t batch_id)
{
   struct zink_batch_state *bs;
   if (!batch_id) {
      /* not submitted yet */
      flush_batch(ctx, true);
      bs = zink_batch_state(ctx->last_fence);
      assert(bs);
      batch_id = bs->fence.batch_id;
   }
   assert(batch_id);
   if (ctx->have_timelines) {
      if (!zink_screen_timeline_wait(zink_screen(ctx->base.screen), batch_id, UINT64_MAX))
         check_device_lost(ctx);
      return;
   }
   simple_mtx_lock(&ctx->batch_mtx);
   struct zink_fence *fence;

   assert(ctx->last_fence);
   if (batch_id == zink_batch_state(ctx->last_fence)->fence.batch_id)
      fence = ctx->last_fence;
   else {
      for (bs = ctx->batch_states; bs; bs = bs->next) {
         if (bs->fence.batch_id < batch_id)
            continue;
         if (!bs->fence.batch_id || bs->fence.batch_id > batch_id)
            break;
      }
      if (!bs || bs->fence.batch_id != batch_id) {
         simple_mtx_unlock(&ctx->batch_mtx);
         /* if we can't find it, it either must have finished already or is on a different context */
         if (!zink_screen_check_last_finished(zink_screen(ctx->base.screen), batch_id)) {
            /* if it hasn't finished, it's on another context, so force a flush so there's something to wait on */
            ctx->batch.has_work = true;
            zink_fence_wait(&ctx->base);
         }
         return;
      }
      fence = &bs->fence;
   }
   simple_mtx_unlock(&ctx->batch_mtx);
   assert(fence);
   sync_flush(ctx, zink_batch_state(fence));
   zink_vkfence_wait(zink_screen(ctx->base.screen), fence, PIPE_TIMEOUT_INFINITE);
}

bool
zink_check_batch_completion(struct zink_context *ctx, uint32_t batch_id, bool have_lock)
{
   assert(ctx->batch.state);
   if (!batch_id)
      /* not submitted yet */
      return false;

   if (zink_screen_check_last_finished(zink_screen(ctx->base.screen), batch_id))
      return true;

   if (ctx->have_timelines) {
      bool success = zink_screen_timeline_wait(zink_screen(ctx->base.screen), batch_id, 0);
      if (!success)
         check_device_lost(ctx);
      return success;
   }
   struct zink_fence *fence;

   if (!have_lock)
      simple_mtx_lock(&ctx->batch_mtx);

   if (ctx->last_fence && batch_id == zink_batch_state(ctx->last_fence)->fence.batch_id)
      fence = ctx->last_fence;
   else {
      struct zink_batch_state *bs;
      for (bs = ctx->batch_states; bs; bs = bs->next) {
         if (bs->fence.batch_id < batch_id)
            continue;
         if (!bs->fence.batch_id || bs->fence.batch_id > batch_id)
            break;
      }
      if (!bs || bs->fence.batch_id != batch_id) {
         if (!have_lock)
            simple_mtx_unlock(&ctx->batch_mtx);
         /* return compare against last_finished, since this has info from all contexts */
         return zink_screen_check_last_finished(zink_screen(ctx->base.screen), batch_id);
      }
      fence = &bs->fence;
   }
   if (!have_lock)
      simple_mtx_unlock(&ctx->batch_mtx);
   assert(fence);
   if (zink_screen(ctx->base.screen)->threaded &&
       !util_queue_fence_is_signalled(&zink_batch_state(fence)->flush_completed))
      return false;
   return zink_vkfence_wait(zink_screen(ctx->base.screen), fence, 0);
}

static void
zink_texture_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!ctx->framebuffer || !ctx->framebuffer->state.num_attachments)
      return;

   zink_batch_no_rp(ctx);
   if (ctx->fb_state.zsbuf) {
      VkMemoryBarrier dmb;
      dmb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      dmb.pNext = NULL;
      dmb.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      dmb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      VKCTX(CmdPipelineBarrier)(
         ctx->batch.state->cmdbuf,
         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         0,
         1, &dmb,
         0, NULL,
         0, NULL
      );
   }
   if (!ctx->fb_state.nr_cbufs)
      return;

   VkMemoryBarrier bmb;
   bmb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
   bmb.pNext = NULL;
   bmb.srcAccessMask = 0;
   bmb.dstAccessMask = 0;
   bmb.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   bmb.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
   VKCTX(CmdPipelineBarrier)(
      ctx->batch.state->cmdbuf,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      1, &bmb,
      0, NULL,
      0, NULL
   );
}

static inline void
mem_barrier(struct zink_context *ctx, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src, VkAccessFlags dst)
{
   struct zink_batch *batch = &ctx->batch;
   VkMemoryBarrier mb;
   mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
   mb.pNext = NULL;
   mb.srcAccessMask = src;
   mb.dstAccessMask = dst;
   zink_end_render_pass(ctx);
   VKCTX(CmdPipelineBarrier)(batch->state->cmdbuf, src_stage, dst_stage, 0, 1, &mb, 0, NULL, 0, NULL);
}

void
zink_flush_memory_barrier(struct zink_context *ctx, bool is_compute)
{
   const VkPipelineStageFlags gfx_flags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                          VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                                          VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                                          VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   const VkPipelineStageFlags cs_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   VkPipelineStageFlags src = ctx->batch.last_was_compute ? cs_flags : gfx_flags;
   VkPipelineStageFlags dst = is_compute ? cs_flags : gfx_flags;

   if (ctx->memory_barrier & (PIPE_BARRIER_TEXTURE | PIPE_BARRIER_SHADER_BUFFER | PIPE_BARRIER_IMAGE))
      mem_barrier(ctx, src, dst, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

   if (ctx->memory_barrier & PIPE_BARRIER_CONSTANT_BUFFER)
      mem_barrier(ctx, src, dst,
                  VK_ACCESS_SHADER_WRITE_BIT,
                  VK_ACCESS_UNIFORM_READ_BIT);

   if (!is_compute) {
      if (ctx->memory_barrier & PIPE_BARRIER_INDIRECT_BUFFER)
         mem_barrier(ctx, src, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
      if (ctx->memory_barrier & PIPE_BARRIER_VERTEX_BUFFER)
         mem_barrier(ctx, gfx_flags, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

      if (ctx->memory_barrier & PIPE_BARRIER_INDEX_BUFFER)
         mem_barrier(ctx, gfx_flags, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_INDEX_READ_BIT);
      if (ctx->memory_barrier & PIPE_BARRIER_FRAMEBUFFER)
         zink_texture_barrier(&ctx->base, 0);
      if (ctx->memory_barrier & PIPE_BARRIER_STREAMOUT_BUFFER)
         mem_barrier(ctx, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                            VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
                     VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT);
   }
   ctx->memory_barrier = 0;
}

static void
zink_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct zink_context *ctx = zink_context(pctx);

   flags &= ~PIPE_BARRIER_UPDATE;
   if (!flags)
      return;

   if (flags & PIPE_BARRIER_MAPPED_BUFFER) {
      /* TODO: this should flush all persistent buffers in use as I think */
      flags &= ~PIPE_BARRIER_MAPPED_BUFFER;
   }
   ctx->memory_barrier = flags;
}

static void
zink_flush_resource(struct pipe_context *pctx,
                    struct pipe_resource *pres)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(pres);
   /* TODO: this is not futureproof and should be updated once proper
    * WSI support is added
    */
   if (res->scanout_obj && (pres->bind & (PIPE_BIND_SHARED | PIPE_BIND_SCANOUT)))
      pipe_resource_reference(&ctx->batch.state->flush_res, pres);
}

void
zink_copy_buffer(struct zink_context *ctx, struct zink_resource *dst, struct zink_resource *src,
                 unsigned dst_offset, unsigned src_offset, unsigned size)
{
   VkBufferCopy region;
   region.srcOffset = src_offset;
   region.dstOffset = dst_offset;
   region.size = size;

   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);
   zink_batch_reference_resource_rw(batch, src, false);
   zink_batch_reference_resource_rw(batch, dst, true);
   util_range_add(&dst->base.b, &dst->valid_buffer_range, dst_offset, dst_offset + size);
   zink_resource_buffer_barrier(ctx, src, VK_ACCESS_TRANSFER_READ_BIT, 0);
   zink_resource_buffer_barrier(ctx, dst, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
   VKCTX(CmdCopyBuffer)(batch->state->cmdbuf, src->obj->buffer, dst->obj->buffer, 1, &region);
}

void
zink_copy_image_buffer(struct zink_context *ctx, struct zink_resource *dst, struct zink_resource *src,
                       unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                       unsigned src_level, const struct pipe_box *src_box, enum pipe_map_flags map_flags)
{
   struct zink_resource *img = dst->base.b.target == PIPE_BUFFER ? src : dst;
   struct zink_resource *buf = dst->base.b.target == PIPE_BUFFER ? dst : src;
   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);

   bool buf2img = buf == src;

   if (buf2img) {
      zink_resource_image_barrier(ctx, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0);
      zink_resource_buffer_barrier(ctx, buf, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      zink_resource_image_barrier(ctx, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0);
      zink_resource_buffer_barrier(ctx, buf, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      util_range_add(&dst->base.b, &dst->valid_buffer_range, dstx, dstx + src_box->width);
   }

   VkBufferImageCopy region = {0};
   region.bufferOffset = buf2img ? src_box->x : dstx;
   region.bufferRowLength = 0;
   region.bufferImageHeight = 0;
   region.imageSubresource.mipLevel = buf2img ? dst_level : src_level;
   enum pipe_texture_target img_target = img->base.b.target;
   if (img->need_2D_zs)
      img_target = img_target == PIPE_TEXTURE_1D ? PIPE_TEXTURE_2D : PIPE_TEXTURE_2D_ARRAY;
   switch (img_target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_1D_ARRAY:
      /* these use layer */
      region.imageSubresource.baseArrayLayer = buf2img ? dstz : src_box->z;
      region.imageSubresource.layerCount = src_box->depth;
      region.imageOffset.z = 0;
      region.imageExtent.depth = 1;
      break;
   case PIPE_TEXTURE_3D:
      /* this uses depth */
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageOffset.z = buf2img ? dstz : src_box->z;
      region.imageExtent.depth = src_box->depth;
      break;
   default:
      /* these must only copy one layer */
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageOffset.z = 0;
      region.imageExtent.depth = 1;
   }
   region.imageOffset.x = buf2img ? dstx : src_box->x;
   region.imageOffset.y = buf2img ? dsty : src_box->y;

   region.imageExtent.width = src_box->width;
   region.imageExtent.height = src_box->height;

   zink_batch_reference_resource_rw(batch, img, buf2img);
   zink_batch_reference_resource_rw(batch, buf, !buf2img);

   /* we're using u_transfer_helper_deinterleave, which means we'll be getting PIPE_MAP_* usage
    * to indicate whether to copy either the depth or stencil aspects
    */
   unsigned aspects = 0;
   if (map_flags) {
      assert((map_flags & (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY)) !=
             (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY));
      if (map_flags & PIPE_MAP_DEPTH_ONLY)
         aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
      else if (map_flags & PIPE_MAP_STENCIL_ONLY)
         aspects = VK_IMAGE_ASPECT_STENCIL_BIT;
   }
   if (!aspects)
      aspects = img->aspect;
   while (aspects) {
      int aspect = 1 << u_bit_scan(&aspects);
      region.imageSubresource.aspectMask = aspect;

      /* this may or may not work with multisampled depth/stencil buffers depending on the driver implementation:
       *
       * srcImage must have a sample count equal to VK_SAMPLE_COUNT_1_BIT
       * - vkCmdCopyImageToBuffer spec
       *
       * dstImage must have a sample count equal to VK_SAMPLE_COUNT_1_BIT
       * - vkCmdCopyBufferToImage spec
       */
      if (buf2img)
         VKCTX(CmdCopyBufferToImage)(batch->state->cmdbuf, buf->obj->buffer, img->obj->image, img->layout, 1, &region);
      else
         VKCTX(CmdCopyImageToBuffer)(batch->state->cmdbuf, img->obj->image, img->layout, buf->obj->buffer, 1, &region);
   }
}

static void
zink_resource_copy_region(struct pipe_context *pctx,
                          struct pipe_resource *pdst,
                          unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                          struct pipe_resource *psrc,
                          unsigned src_level, const struct pipe_box *src_box)
{
   struct zink_resource *dst = zink_resource(pdst);
   struct zink_resource *src = zink_resource(psrc);
   struct zink_context *ctx = zink_context(pctx);
   if (dst->base.b.target != PIPE_BUFFER && src->base.b.target != PIPE_BUFFER) {
      VkImageCopy region = {0};
      if (util_format_get_num_planes(src->base.b.format) == 1 &&
          util_format_get_num_planes(dst->base.b.format) == 1) {
      /* If neither the calling command’s srcImage nor the calling command’s dstImage
       * has a multi-planar image format then the aspectMask member of srcSubresource
       * and dstSubresource must match
       *
       * -VkImageCopy spec
       */
         assert(src->aspect == dst->aspect);
      } else
         unreachable("planar formats not yet handled");

      zink_fb_clears_apply_or_discard(ctx, pdst, (struct u_rect){dstx, dstx + src_box->width, dsty, dsty + src_box->height}, false);
      zink_fb_clears_apply_region(ctx, psrc, zink_rect_from_box(src_box));

      region.srcSubresource.aspectMask = src->aspect;
      region.srcSubresource.mipLevel = src_level;
      enum pipe_texture_target src_target = src->base.b.target;
      if (src->need_2D_zs)
         src_target = src_target == PIPE_TEXTURE_1D ? PIPE_TEXTURE_2D : PIPE_TEXTURE_2D_ARRAY;
      switch (src_target) {
      case PIPE_TEXTURE_CUBE:
      case PIPE_TEXTURE_CUBE_ARRAY:
      case PIPE_TEXTURE_2D_ARRAY:
      case PIPE_TEXTURE_1D_ARRAY:
         /* these use layer */
         region.srcSubresource.baseArrayLayer = src_box->z;
         region.srcSubresource.layerCount = src_box->depth;
         region.srcOffset.z = 0;
         region.extent.depth = 1;
         break;
      case PIPE_TEXTURE_3D:
         /* this uses depth */
         region.srcSubresource.baseArrayLayer = 0;
         region.srcSubresource.layerCount = 1;
         region.srcOffset.z = src_box->z;
         region.extent.depth = src_box->depth;
         break;
      default:
         /* these must only copy one layer */
         region.srcSubresource.baseArrayLayer = 0;
         region.srcSubresource.layerCount = 1;
         region.srcOffset.z = 0;
         region.extent.depth = 1;
      }

      region.srcOffset.x = src_box->x;
      region.srcOffset.y = src_box->y;

      region.dstSubresource.aspectMask = dst->aspect;
      region.dstSubresource.mipLevel = dst_level;
      enum pipe_texture_target dst_target = dst->base.b.target;
      if (dst->need_2D_zs)
         dst_target = dst_target == PIPE_TEXTURE_1D ? PIPE_TEXTURE_2D : PIPE_TEXTURE_2D_ARRAY;
      switch (dst_target) {
      case PIPE_TEXTURE_CUBE:
      case PIPE_TEXTURE_CUBE_ARRAY:
      case PIPE_TEXTURE_2D_ARRAY:
      case PIPE_TEXTURE_1D_ARRAY:
         /* these use layer */
         region.dstSubresource.baseArrayLayer = dstz;
         region.dstSubresource.layerCount = src_box->depth;
         region.dstOffset.z = 0;
         break;
      case PIPE_TEXTURE_3D:
         /* this uses depth */
         region.dstSubresource.baseArrayLayer = 0;
         region.dstSubresource.layerCount = 1;
         region.dstOffset.z = dstz;
         break;
      default:
         /* these must only copy one layer */
         region.dstSubresource.baseArrayLayer = 0;
         region.dstSubresource.layerCount = 1;
         region.dstOffset.z = 0;
      }

      region.dstOffset.x = dstx;
      region.dstOffset.y = dsty;
      region.extent.width = src_box->width;
      region.extent.height = src_box->height;

      struct zink_batch *batch = &ctx->batch;
      zink_batch_no_rp(ctx);
      zink_batch_reference_resource_rw(batch, src, false);
      zink_batch_reference_resource_rw(batch, dst, true);

      zink_resource_setup_transfer_layouts(ctx, src, dst);
      VKCTX(CmdCopyImage)(batch->state->cmdbuf, src->obj->image, src->layout,
                     dst->obj->image, dst->layout,
                     1, &region);
   } else if (dst->base.b.target == PIPE_BUFFER &&
              src->base.b.target == PIPE_BUFFER) {
      zink_copy_buffer(ctx, dst, src, dstx, src_box->x, src_box->width);
   } else
      zink_copy_image_buffer(ctx, dst, src, dst_level, dstx, dsty, dstz, src_level, src_box, 0);
}

static struct pipe_stream_output_target *
zink_create_stream_output_target(struct pipe_context *pctx,
                                 struct pipe_resource *pres,
                                 unsigned buffer_offset,
                                 unsigned buffer_size)
{
   struct zink_so_target *t;
   t = CALLOC_STRUCT(zink_so_target);
   if (!t)
      return NULL;

   /* using PIPE_BIND_CUSTOM here lets us create a custom pipe buffer resource,
    * which allows us to differentiate and use VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT
    * as we must for this case
    */
   t->counter_buffer = pipe_buffer_create(pctx->screen, PIPE_BIND_STREAM_OUTPUT | PIPE_BIND_CUSTOM, PIPE_USAGE_DEFAULT, 4);
   if (!t->counter_buffer) {
      FREE(t);
      return NULL;
   }

   t->base.reference.count = 1;
   t->base.context = pctx;
   pipe_resource_reference(&t->base.buffer, pres);
   t->base.buffer_offset = buffer_offset;
   t->base.buffer_size = buffer_size;

   zink_resource(t->base.buffer)->so_valid = true;

   return &t->base;
}

static void
zink_stream_output_target_destroy(struct pipe_context *pctx,
                                  struct pipe_stream_output_target *psot)
{
   struct zink_so_target *t = (struct zink_so_target *)psot;
   pipe_resource_reference(&t->counter_buffer, NULL);
   pipe_resource_reference(&t->base.buffer, NULL);
   FREE(t);
}

static void
zink_set_stream_output_targets(struct pipe_context *pctx,
                               unsigned num_targets,
                               struct pipe_stream_output_target **targets,
                               const unsigned *offsets)
{
   struct zink_context *ctx = zink_context(pctx);

   if (num_targets == 0) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         if (ctx->so_targets[i]) {
            struct zink_resource *so = zink_resource(ctx->so_targets[i]->buffer);
            if (so) {
               so->so_bind_count--;
               update_res_bind_count(ctx, so, false, true);
            }
         }
         pipe_so_target_reference(&ctx->so_targets[i], NULL);
      }
      ctx->num_so_targets = 0;
   } else {
      for (unsigned i = 0; i < num_targets; i++) {
         struct zink_so_target *t = zink_so_target(targets[i]);
         pipe_so_target_reference(&ctx->so_targets[i], targets[i]);
         if (!t)
            continue;
         struct zink_resource *res = zink_resource(t->counter_buffer);
         if (offsets[0] == (unsigned)-1)
            ctx->xfb_barrier |= zink_resource_buffer_needs_barrier(res,
                                                                   VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
                                                                   VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
         else
            ctx->xfb_barrier |= zink_resource_buffer_needs_barrier(res,
                                                                   VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
                                                                   VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT);
         struct zink_resource *so = zink_resource(ctx->so_targets[i]->buffer);
         if (so) {
            so->so_bind_count++;
            update_res_bind_count(ctx, so, false, false);
         }
      }
      for (unsigned i = num_targets; i < ctx->num_so_targets; i++) {
         if (ctx->so_targets[i]) {
            struct zink_resource *so = zink_resource(ctx->so_targets[i]->buffer);
            if (so) {
               so->so_bind_count--;
               update_res_bind_count(ctx, so, false, true);
            }
         }
         pipe_so_target_reference(&ctx->so_targets[i], NULL);
      }
      ctx->num_so_targets = num_targets;

      /* TODO: possibly avoid rebinding on resume if resuming from same buffers? */
      ctx->dirty_so_targets = true;
   }
}

void
zink_rebind_framebuffer(struct zink_context *ctx, struct zink_resource *res)
{
   if (!ctx->framebuffer)
      return;
   bool did_rebind = false;
   if (res->aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
      for (unsigned i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (!ctx->fb_state.cbufs[i] ||
             zink_resource(ctx->fb_state.cbufs[i]->texture) != res)
            continue;
         zink_rebind_ctx_surface(ctx, &ctx->fb_state.cbufs[i]);
         did_rebind = true;
      }
   } else {
      if (ctx->fb_state.zsbuf && zink_resource(ctx->fb_state.zsbuf->texture) != res) {
         zink_rebind_ctx_surface(ctx, &ctx->fb_state.zsbuf);
         did_rebind = true;
      }
   }

   did_rebind |= rebind_fb_state(ctx, res, false);

   if (!did_rebind)
      return;

   zink_batch_no_rp(ctx);
   if (zink_screen(ctx->base.screen)->info.have_KHR_imageless_framebuffer) {
      struct zink_framebuffer *fb = ctx->get_framebuffer(ctx);
      ctx->fb_changed |= ctx->framebuffer != fb;
      ctx->framebuffer = fb;
   }
}

ALWAYS_INLINE static struct zink_resource *
rebind_ubo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot)
{
   struct zink_resource *res = update_descriptor_state_ubo(ctx, shader, slot,
                                                           ctx->di.descriptor_res[ZINK_DESCRIPTOR_TYPE_UBO][shader][slot]);
   zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_UBO, slot, 1);
   return res;
}

ALWAYS_INLINE static struct zink_resource *
rebind_ssbo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot)
{
   const struct pipe_shader_buffer *ssbo = &ctx->ssbos[shader][slot];
   struct zink_resource *res = zink_resource(ssbo->buffer);
   if (!res)
      return NULL;
   util_range_add(&res->base.b, &res->valid_buffer_range, ssbo->buffer_offset,
                  ssbo->buffer_offset + ssbo->buffer_size);
   update_descriptor_state_ssbo(ctx, shader, slot, res);
   zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_SSBO, slot, 1);
   return res;
}

ALWAYS_INLINE static struct zink_resource *
rebind_tbo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot)
{
   struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->sampler_views[shader][slot]);
   if (!sampler_view || sampler_view->base.texture->target != PIPE_BUFFER)
      return NULL;
   struct zink_resource *res = zink_resource(sampler_view->base.texture);
   if (zink_batch_usage_exists(sampler_view->buffer_view->batch_uses))
      zink_batch_reference_bufferview(&ctx->batch, sampler_view->buffer_view);
   VkBufferViewCreateInfo bvci = sampler_view->buffer_view->bvci;
   bvci.buffer = res->obj->buffer;
   zink_buffer_view_reference(zink_screen(ctx->base.screen), &sampler_view->buffer_view, NULL);
   sampler_view->buffer_view = get_buffer_view(ctx, res, &bvci);
   update_descriptor_state_sampler(ctx, shader, slot, res);
   zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, slot, 1);
   return res;
}

ALWAYS_INLINE static struct zink_resource *
rebind_ibo(struct zink_context *ctx, enum pipe_shader_type shader, unsigned slot)
{
   struct zink_image_view *image_view = &ctx->image_views[shader][slot];
   struct zink_resource *res = zink_resource(image_view->base.resource);
   if (!res || res->base.b.target != PIPE_BUFFER)
      return NULL;
   zink_descriptor_set_refs_clear(&image_view->buffer_view->desc_set_refs, image_view->buffer_view);
   if (zink_batch_usage_exists(image_view->buffer_view->batch_uses))
      zink_batch_reference_bufferview(&ctx->batch, image_view->buffer_view);
   VkBufferViewCreateInfo bvci = image_view->buffer_view->bvci;
   bvci.buffer = res->obj->buffer;
   zink_buffer_view_reference(zink_screen(ctx->base.screen), &image_view->buffer_view, NULL);
   if (!zink_resource_object_init_storage(ctx, res)) {
      debug_printf("couldn't create storage image!");
      return NULL;
   }
   image_view->buffer_view = get_buffer_view(ctx, res, &bvci);
   assert(image_view->buffer_view);
   util_range_add(&res->base.b, &res->valid_buffer_range, image_view->base.u.buf.offset,
                  image_view->base.u.buf.offset + image_view->base.u.buf.size);
   update_descriptor_state_image(ctx, shader, slot, res);
   zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, shader, ZINK_DESCRIPTOR_TYPE_IMAGE, slot, 1);
   return res;
}

static unsigned
rebind_buffer(struct zink_context *ctx, struct zink_resource *res, uint32_t rebind_mask, const unsigned expected_num_rebinds)
{
   unsigned num_rebinds = 0;
   bool has_write = false;

   if (!zink_resource_has_binds(res))
      return 0;

   assert(!res->bindless[1]); //TODO
   if ((rebind_mask & BITFIELD_BIT(TC_BINDING_STREAMOUT_BUFFER)) || (!rebind_mask && res->so_bind_count && ctx->num_so_targets)) {
      for (unsigned i = 0; i < ctx->num_so_targets; i++) {
         if (ctx->so_targets[i]) {
            struct zink_resource *so = zink_resource(ctx->so_targets[i]->buffer);
            if (so && so == res) {
               ctx->dirty_so_targets = true;
               num_rebinds++;
            }
         }
      }
      rebind_mask &= ~BITFIELD_BIT(TC_BINDING_STREAMOUT_BUFFER);
   }
   if (num_rebinds && expected_num_rebinds >= num_rebinds && !rebind_mask)
      goto end;

   if ((rebind_mask & BITFIELD_BIT(TC_BINDING_VERTEX_BUFFER)) || (!rebind_mask && res->vbo_bind_mask)) {
      u_foreach_bit(slot, res->vbo_bind_mask) {
         if (ctx->vertex_buffers[slot].buffer.resource != &res->base.b) //wrong context
            goto end;
         num_rebinds++;
      }
      rebind_mask &= ~BITFIELD_BIT(TC_BINDING_VERTEX_BUFFER);
      ctx->vertex_buffers_dirty = true;
   }
   if (num_rebinds && expected_num_rebinds >= num_rebinds && !rebind_mask)
      goto end;

   const uint32_t ubo_mask = rebind_mask ?
                             rebind_mask & BITFIELD_RANGE(TC_BINDING_UBO_VS, PIPE_SHADER_TYPES) :
                             ((res->ubo_bind_count[0] ? BITFIELD_RANGE(TC_BINDING_UBO_VS, (PIPE_SHADER_TYPES - 1)) : 0) |
                              (res->ubo_bind_count[1] ? BITFIELD_BIT(TC_BINDING_UBO_CS) : 0));
   u_foreach_bit(shader, ubo_mask >> TC_BINDING_UBO_VS) {
      u_foreach_bit(slot, res->ubo_bind_mask[shader]) {
         if (&res->base.b != ctx->ubos[shader][slot].buffer) //wrong context
            goto end;
         rebind_ubo(ctx, shader, slot);
         num_rebinds++;
      }
   }
   rebind_mask &= ~BITFIELD_RANGE(TC_BINDING_UBO_VS, PIPE_SHADER_TYPES);
   if (num_rebinds && expected_num_rebinds >= num_rebinds && !rebind_mask)
      goto end;

   const unsigned ssbo_mask = rebind_mask ?
                              rebind_mask & BITFIELD_RANGE(TC_BINDING_SSBO_VS, PIPE_SHADER_TYPES) :
                              BITFIELD_RANGE(TC_BINDING_SSBO_VS, PIPE_SHADER_TYPES);
   u_foreach_bit(shader, ssbo_mask >> TC_BINDING_SSBO_VS) {
      u_foreach_bit(slot, res->ssbo_bind_mask[shader]) {
         struct pipe_shader_buffer *ssbo = &ctx->ssbos[shader][slot];
         if (&res->base.b != ssbo->buffer) //wrong context
            goto end;
         rebind_ssbo(ctx, shader, slot);
         has_write |= (ctx->writable_ssbos[shader] & BITFIELD64_BIT(slot)) != 0;
         num_rebinds++;
      }
   }
   rebind_mask &= ~BITFIELD_RANGE(TC_BINDING_SSBO_VS, PIPE_SHADER_TYPES);
   if (num_rebinds && expected_num_rebinds >= num_rebinds && !rebind_mask)
      goto end;
   const unsigned sampler_mask = rebind_mask ?
                                 rebind_mask & BITFIELD_RANGE(TC_BINDING_SAMPLERVIEW_VS, PIPE_SHADER_TYPES) :
                                 BITFIELD_RANGE(TC_BINDING_SAMPLERVIEW_VS, PIPE_SHADER_TYPES);
   u_foreach_bit(shader, sampler_mask >> TC_BINDING_SAMPLERVIEW_VS) {
      u_foreach_bit(slot, res->sampler_binds[shader]) {
         struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->sampler_views[shader][slot]);
         if (&res->base.b != sampler_view->base.texture) //wrong context
            goto end;
         rebind_tbo(ctx, shader, slot);
         num_rebinds++;
      }
   }
   rebind_mask &= ~BITFIELD_RANGE(TC_BINDING_SAMPLERVIEW_VS, PIPE_SHADER_TYPES);
   if (num_rebinds && expected_num_rebinds >= num_rebinds && !rebind_mask)
      goto end;

   const unsigned image_mask = rebind_mask ?
                               rebind_mask & BITFIELD_RANGE(TC_BINDING_IMAGE_VS, PIPE_SHADER_TYPES) :
                               BITFIELD_RANGE(TC_BINDING_IMAGE_VS, PIPE_SHADER_TYPES);
   unsigned num_image_rebinds_remaining = rebind_mask ? expected_num_rebinds - num_rebinds : res->image_bind_count[0] + res->image_bind_count[1];
   u_foreach_bit(shader, image_mask >> TC_BINDING_IMAGE_VS) {
      for (unsigned slot = 0; num_image_rebinds_remaining && slot < ctx->di.num_images[shader]; slot++) {
         struct zink_resource *cres = ctx->di.descriptor_res[ZINK_DESCRIPTOR_TYPE_IMAGE][shader][slot];
         if (res != cres)
            continue;

         rebind_ibo(ctx, shader, slot);
         const struct zink_image_view *image_view = &ctx->image_views[shader][slot];
         has_write |= (image_view->base.access & PIPE_IMAGE_ACCESS_WRITE) != 0;
         num_image_rebinds_remaining--;
         num_rebinds++;
      }
   }
end:
   zink_batch_resource_usage_set(&ctx->batch, res, has_write);
   return num_rebinds;
}

static bool
zink_resource_commit(struct pipe_context *pctx, struct pipe_resource *pres, unsigned level, struct pipe_box *box, bool commit)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(pres);
   struct zink_screen *screen = zink_screen(pctx->screen);

   /* if any current usage exists, flush the queue */
   if (zink_resource_has_unflushed_usage(res))
      zink_flush_queue(ctx);

   bool ret = zink_bo_commit(screen, res, level, box, commit);
   if (!ret)
      check_device_lost(ctx);

   return ret;
}

static void
rebind_image(struct zink_context *ctx, struct zink_resource *res)
{
    zink_rebind_framebuffer(ctx, res);
    if (!zink_resource_has_binds(res))
       return;
    for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++) {
       if (res->sampler_binds[i]) {
          for (unsigned j = 0; j < ctx->di.num_sampler_views[i]; j++) {
             struct zink_sampler_view *sv = zink_sampler_view(ctx->sampler_views[i][j]);
             if (sv && sv->base.texture == &res->base.b) {
                 struct pipe_surface *psurf = &sv->image_view->base;
                 zink_rebind_surface(ctx, &psurf);
                 sv->image_view = zink_surface(psurf);
                 zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, i, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, j, 1);
                 update_descriptor_state_sampler(ctx, i, j, res);
             }
          }
       }
       if (!res->image_bind_count[i == PIPE_SHADER_COMPUTE])
          continue;
       for (unsigned j = 0; j < ctx->di.num_images[i]; j++) {
          if (zink_resource(ctx->image_views[i][j].base.resource) == res) {
             zink_screen(ctx->base.screen)->context_invalidate_descriptor_state(ctx, i, ZINK_DESCRIPTOR_TYPE_IMAGE, j, 1);
             update_descriptor_state_sampler(ctx, i, j, res);
             _mesa_set_add(ctx->need_barriers[i == PIPE_SHADER_COMPUTE], res);
          }
       }
    }
}

bool
zink_resource_rebind(struct zink_context *ctx, struct zink_resource *res)
{
   if (res->base.b.target == PIPE_BUFFER) {
      /* force counter buffer reset */
      res->so_valid = false;
      return rebind_buffer(ctx, res, 0, 0) == res->bind_count[0] + res->bind_count[1];
   }
   rebind_image(ctx, res);
   return false;
}

void
zink_rebind_all_buffers(struct zink_context *ctx)
{
   struct zink_batch *batch = &ctx->batch;
   ctx->vertex_buffers_dirty = ctx->gfx_pipeline_state.vertex_buffers_enabled_mask > 0;
   ctx->dirty_so_targets = ctx->num_so_targets > 0;
   if (ctx->num_so_targets)
      zink_resource_buffer_barrier(ctx, zink_resource(ctx->dummy_xfb_buffer),
                                   VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT);
   for (unsigned shader = PIPE_SHADER_VERTEX; shader < PIPE_SHADER_TYPES; shader++) {
      for (unsigned slot = 0; slot < ctx->di.num_ubos[shader]; slot++) {
         struct zink_resource *res = rebind_ubo(ctx, shader, slot);
         if (res)
            zink_batch_resource_usage_set(batch, res, false);
      }
      for (unsigned slot = 0; slot < ctx->di.num_sampler_views[shader]; slot++) {
         struct zink_resource *res = rebind_tbo(ctx, shader, slot);
         if (res)
            zink_batch_resource_usage_set(batch, res, false);
      }
      for (unsigned slot = 0; slot < ctx->di.num_ssbos[shader]; slot++) {
         struct zink_resource *res = rebind_ssbo(ctx, shader, slot);
         if (res)
            zink_batch_resource_usage_set(batch, res, (ctx->writable_ssbos[shader] & BITFIELD64_BIT(slot)) != 0);
      }
      for (unsigned slot = 0; slot < ctx->di.num_images[shader]; slot++) {
         struct zink_resource *res = rebind_ibo(ctx, shader, slot);
         if (res)
            zink_batch_resource_usage_set(batch, res, (ctx->image_views[shader][slot].base.access & PIPE_IMAGE_ACCESS_WRITE) != 0);
      }
   }
}

static void
zink_context_replace_buffer_storage(struct pipe_context *pctx, struct pipe_resource *dst,
                                    struct pipe_resource *src, unsigned num_rebinds,
                                    uint32_t rebind_mask, uint32_t delete_buffer_id)
{
   struct zink_resource *d = zink_resource(dst);
   struct zink_resource *s = zink_resource(src);
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);

   assert(d->internal_format == s->internal_format);
   assert(d->obj);
   assert(s->obj);
   util_idalloc_mt_free(&screen->buffer_ids, delete_buffer_id);
   zink_descriptor_set_refs_clear(&d->obj->desc_set_refs, d->obj);
   /* add a ref just like check_resource_for_batch_ref() would've */
   if (zink_resource_has_binds(d) && zink_resource_has_usage(d))
      zink_batch_reference_resource(&ctx->batch, d);
   /* don't be too creative */
   zink_resource_object_reference(screen, &d->obj, s->obj);
   /* force counter buffer reset */
   d->so_valid = false;
   if (num_rebinds && rebind_buffer(ctx, d, rebind_mask, num_rebinds) < num_rebinds)
      ctx->buffer_rebind_counter = p_atomic_inc_return(&screen->buffer_rebind_counter);
}

static bool
zink_context_is_resource_busy(struct pipe_screen *pscreen, struct pipe_resource *pres, unsigned usage)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   uint32_t check_usage = 0;
   if (usage & PIPE_MAP_READ)
      check_usage |= ZINK_RESOURCE_ACCESS_WRITE;
   if (usage & PIPE_MAP_WRITE)
      check_usage |= ZINK_RESOURCE_ACCESS_RW;
   return !zink_resource_usage_check_completion(screen, res, check_usage);
}

static void
zink_emit_string_marker(struct pipe_context *pctx,
                        const char *string, int len)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_batch *batch = &zink_context(pctx)->batch;

   /* make sure string is nul-terminated */
   char buf[512], *temp = NULL;
   if (len < ARRAY_SIZE(buf)) {
      memcpy(buf, string, len);
      buf[len] = '\0';
      string = buf;
   } else
      string = temp = strndup(string, len);

   VkDebugUtilsLabelEXT label = {
      VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, NULL,
      string,
      { 0 }
   };
   screen->vk.CmdInsertDebugUtilsLabelEXT(batch->state->cmdbuf, &label);
   free(temp);
}

struct pipe_context *
zink_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_context *ctx = rzalloc(NULL, struct zink_context);
   if (!ctx)
      goto fail;
   ctx->have_timelines = screen->info.have_KHR_timeline_semaphore;

   ctx->pipeline_changed[0] = ctx->pipeline_changed[1] = true;
   ctx->gfx_pipeline_state.dirty = true;
   ctx->compute_pipeline_state.dirty = true;
   ctx->fb_changed = ctx->rp_changed = true;
   ctx->gfx_pipeline_state.gfx_prim_mode = PIPE_PRIM_MAX;

   zink_init_draw_functions(ctx, screen);
   zink_init_grid_functions(ctx);

   ctx->base.screen = pscreen;
   ctx->base.priv = priv;

   if (screen->info.have_KHR_imageless_framebuffer) {
      ctx->get_framebuffer = zink_get_framebuffer_imageless;
      ctx->init_framebuffer = zink_init_framebuffer_imageless;
   } else {
      ctx->get_framebuffer = zink_get_framebuffer;
      ctx->init_framebuffer = zink_init_framebuffer;
   }

   ctx->base.destroy = zink_context_destroy;
   ctx->base.get_device_reset_status = zink_get_device_reset_status;
   ctx->base.set_device_reset_callback = zink_set_device_reset_callback;

   zink_context_state_init(&ctx->base);

   ctx->base.create_sampler_state = zink_create_sampler_state;
   ctx->base.bind_sampler_states = zink_bind_sampler_states;
   ctx->base.delete_sampler_state = zink_delete_sampler_state;

   ctx->base.create_sampler_view = zink_create_sampler_view;
   ctx->base.set_sampler_views = zink_set_sampler_views;
   ctx->base.sampler_view_destroy = zink_sampler_view_destroy;
   ctx->base.get_sample_position = zink_get_sample_position;
   ctx->base.set_sample_locations = zink_set_sample_locations;

   zink_program_init(ctx);

   ctx->base.set_polygon_stipple = zink_set_polygon_stipple;
   ctx->base.set_vertex_buffers = zink_set_vertex_buffers;
   ctx->base.set_viewport_states = zink_set_viewport_states;
   ctx->base.set_scissor_states = zink_set_scissor_states;
   ctx->base.set_inlinable_constants = zink_set_inlinable_constants;
   ctx->base.set_constant_buffer = zink_set_constant_buffer;
   ctx->base.set_shader_buffers = zink_set_shader_buffers;
   ctx->base.set_shader_images = zink_set_shader_images;
   ctx->base.set_framebuffer_state = zink_set_framebuffer_state;
   ctx->base.set_stencil_ref = zink_set_stencil_ref;
   ctx->base.set_clip_state = zink_set_clip_state;
   ctx->base.set_blend_color = zink_set_blend_color;
   ctx->base.set_tess_state = zink_set_tess_state;
   ctx->base.set_patch_vertices = zink_set_patch_vertices;

   ctx->base.set_sample_mask = zink_set_sample_mask;

   ctx->base.clear = zink_clear;
   ctx->base.clear_texture = zink_clear_texture;
   ctx->base.clear_buffer = zink_clear_buffer;
   ctx->base.clear_render_target = zink_clear_render_target;
   ctx->base.clear_depth_stencil = zink_clear_depth_stencil;

   ctx->base.create_fence_fd = zink_create_fence_fd;
   ctx->base.fence_server_sync = zink_fence_server_sync;
   ctx->base.fence_server_signal = zink_fence_server_signal;
   ctx->base.flush = zink_flush;
   ctx->base.memory_barrier = zink_memory_barrier;
   ctx->base.texture_barrier = zink_texture_barrier;
   ctx->base.evaluate_depth_buffer = zink_evaluate_depth_buffer;

   ctx->base.resource_commit = zink_resource_commit;
   ctx->base.resource_copy_region = zink_resource_copy_region;
   ctx->base.blit = zink_blit;
   ctx->base.create_stream_output_target = zink_create_stream_output_target;
   ctx->base.stream_output_target_destroy = zink_stream_output_target_destroy;

   ctx->base.set_stream_output_targets = zink_set_stream_output_targets;
   ctx->base.flush_resource = zink_flush_resource;

   ctx->base.emit_string_marker = zink_emit_string_marker;

   zink_context_surface_init(&ctx->base);
   zink_context_resource_init(&ctx->base);
   zink_context_query_init(&ctx->base);

   _mesa_set_init(&ctx->update_barriers[0][0], ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
   _mesa_set_init(&ctx->update_barriers[1][0], ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
   _mesa_set_init(&ctx->update_barriers[0][1], ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
   _mesa_set_init(&ctx->update_barriers[1][1], ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
   ctx->need_barriers[0] = &ctx->update_barriers[0][0];
   ctx->need_barriers[1] = &ctx->update_barriers[1][0];

   util_dynarray_init(&ctx->free_batch_states, ctx);

   ctx->gfx_pipeline_state.have_EXT_extended_dynamic_state = screen->info.have_EXT_extended_dynamic_state;
   ctx->gfx_pipeline_state.have_EXT_extended_dynamic_state2 = screen->info.have_EXT_extended_dynamic_state2;

   slab_create_child(&ctx->transfer_pool, &screen->transfer_pool);
   slab_create_child(&ctx->transfer_pool_unsync, &screen->transfer_pool);

   ctx->base.stream_uploader = u_upload_create_default(&ctx->base);
   ctx->base.const_uploader = u_upload_create_default(&ctx->base);
   for (int i = 0; i < ARRAY_SIZE(ctx->fb_clears); i++)
      util_dynarray_init(&ctx->fb_clears[i].clears, ctx);

   ctx->blitter = util_blitter_create(&ctx->base);
   if (!ctx->blitter)
      goto fail;

   ctx->gfx_pipeline_state.shader_keys.last_vertex.key.vs_base.last_vertex_stage = true;
   ctx->last_vertex_stage_dirty = true;
   ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_VERTEX].size = sizeof(struct zink_vs_key_base);
   ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_TESS_EVAL].size = sizeof(struct zink_vs_key_base);
   ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_GEOMETRY].size = sizeof(struct zink_vs_key_base);
   ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_FRAGMENT].size = sizeof(struct zink_fs_key);
   _mesa_hash_table_init(&ctx->compute_program_cache, ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
   _mesa_hash_table_init(&ctx->framebuffer_cache, ctx, hash_framebuffer_imageless, equals_framebuffer_imageless);
   _mesa_set_init(&ctx->render_pass_state_cache, ctx, hash_rp_state, equals_rp_state);
   ctx->render_pass_cache = _mesa_hash_table_create(NULL,
                                                    hash_render_pass_state,
                                                    equals_render_pass_state);
   if (!ctx->render_pass_cache)
      goto fail;

   const uint8_t data[] = {0};
   ctx->dummy_vertex_buffer = pipe_buffer_create(&screen->base,
      PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_SHADER_IMAGE, PIPE_USAGE_IMMUTABLE, sizeof(data));
   if (!ctx->dummy_vertex_buffer)
      goto fail;
   ctx->dummy_xfb_buffer = pipe_buffer_create(&screen->base,
      PIPE_BIND_STREAM_OUTPUT, PIPE_USAGE_IMMUTABLE, sizeof(data));
   if (!ctx->dummy_xfb_buffer)
      goto fail;
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->dummy_surface); i++) {
      if (!(screen->info.props.limits.framebufferDepthSampleCounts & BITFIELD_BIT(i)))
         continue;
      ctx->dummy_surface[i] = zink_surface_create_null(ctx, PIPE_TEXTURE_2D, 1024, 1024, BITFIELD_BIT(i));
      if (!ctx->dummy_surface[i])
         goto fail;
   }
   VkBufferViewCreateInfo bvci = create_bvci(ctx, zink_resource(ctx->dummy_vertex_buffer), PIPE_FORMAT_R8_UNORM, 0, sizeof(data));
   ctx->dummy_bufferview = get_buffer_view(ctx, zink_resource(ctx->dummy_vertex_buffer), &bvci);
   if (!ctx->dummy_bufferview)
      goto fail;

   if (!zink_descriptor_layouts_init(ctx))
      goto fail;

   if (!screen->descriptors_init(ctx)) {
      zink_screen_init_descriptor_funcs(screen, true);
      if (!screen->descriptors_init(ctx))
         goto fail;
   }

   ctx->base.create_texture_handle = zink_create_texture_handle;
   ctx->base.delete_texture_handle = zink_delete_texture_handle;
   ctx->base.make_texture_handle_resident = zink_make_texture_handle_resident;
   ctx->base.create_image_handle = zink_create_image_handle;
   ctx->base.delete_image_handle = zink_delete_image_handle;
   ctx->base.make_image_handle_resident = zink_make_image_handle_resident;
   for (unsigned i = 0; i < 2; i++) {
      _mesa_hash_table_init(&ctx->di.bindless[i].img_handles, ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);
      _mesa_hash_table_init(&ctx->di.bindless[i].tex_handles, ctx, _mesa_hash_pointer, _mesa_key_pointer_equal);

      /* allocate 1024 slots and reserve slot 0 */
      util_idalloc_init(&ctx->di.bindless[i].tex_slots, ZINK_MAX_BINDLESS_HANDLES);
      util_idalloc_alloc(&ctx->di.bindless[i].tex_slots);
      util_idalloc_init(&ctx->di.bindless[i].img_slots, ZINK_MAX_BINDLESS_HANDLES);
      util_idalloc_alloc(&ctx->di.bindless[i].img_slots);
      ctx->di.bindless[i].buffer_infos = malloc(sizeof(VkBufferView) * ZINK_MAX_BINDLESS_HANDLES);
      ctx->di.bindless[i].img_infos = malloc(sizeof(VkDescriptorImageInfo) * ZINK_MAX_BINDLESS_HANDLES);
      util_dynarray_init(&ctx->di.bindless[i].updates, NULL);
      util_dynarray_init(&ctx->di.bindless[i].resident, NULL);
   }

   ctx->have_timelines = screen->info.have_KHR_timeline_semaphore;
   simple_mtx_init(&ctx->batch_mtx, mtx_plain);
   zink_start_batch(ctx, &ctx->batch);
   if (!ctx->batch.state)
      goto fail;

   pipe_buffer_write_nooverlap(&ctx->base, ctx->dummy_vertex_buffer, 0, sizeof(data), data);
   pipe_buffer_write_nooverlap(&ctx->base, ctx->dummy_xfb_buffer, 0, sizeof(data), data);

   for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++) {
      /* need to update these based on screen config for null descriptors */
      for (unsigned j = 0; j < 32; j++) {
         update_descriptor_state_ubo(ctx, i, j, NULL);
         update_descriptor_state_sampler(ctx, i, j, NULL);
         update_descriptor_state_ssbo(ctx, i, j, NULL);
         update_descriptor_state_image(ctx, i, j, NULL);
      }
   }
   if (!screen->info.rb2_feats.nullDescriptor)
      ctx->di.fbfetch.imageView = zink_csurface(ctx->dummy_surface[0])->image_view;
   p_atomic_inc(&screen->base.num_contexts);

   zink_select_draw_vbo(ctx);
   zink_select_launch_grid(ctx);

   if (!(flags & PIPE_CONTEXT_PREFER_THREADED) || flags & PIPE_CONTEXT_COMPUTE_ONLY) {
      return &ctx->base;
   }

   struct threaded_context *tc = (struct threaded_context*)threaded_context_create(&ctx->base, &screen->transfer_pool,
                                                     zink_context_replace_buffer_storage,
                                                     &(struct threaded_context_options){
                                                        .create_fence = zink_create_tc_fence_for_tc,
                                                        .is_resource_busy = zink_context_is_resource_busy,
                                                        .driver_calls_flush_notify = true,
                                                        .unsynchronized_get_device_reset_status = true,
                                                     },
                                                     &ctx->tc);

   if (tc && (struct zink_context*)tc != ctx) {
      threaded_context_init_bytes_mapped_limit(tc, 4);
      ctx->base.set_context_param = zink_set_context_param;
   }

   return (struct pipe_context*)tc;

fail:
   if (ctx)
      zink_context_destroy(&ctx->base);
   return NULL;
}
