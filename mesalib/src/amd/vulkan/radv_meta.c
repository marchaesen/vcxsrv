/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
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

#include "radv_meta.h"

#include "vk_util.h"

#include <fcntl.h>
#include <limits.h>
#ifndef _WIN32
#include <pwd.h>
#endif
#include <sys/stat.h>

void
radv_meta_save(struct radv_meta_saved_state *state, struct radv_cmd_buffer *cmd_buffer,
               uint32_t flags)
{
   VkPipelineBindPoint bind_point = flags & RADV_META_SAVE_GRAPHICS_PIPELINE
                                       ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                       : VK_PIPELINE_BIND_POINT_COMPUTE;
   struct radv_descriptor_state *descriptors_state =
      radv_get_descriptors_state(cmd_buffer, bind_point);

   assert(flags & (RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_COMPUTE_PIPELINE));

   state->flags = flags;

   if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE) {
      assert(!(state->flags & RADV_META_SAVE_COMPUTE_PIPELINE));

      state->old_pipeline = cmd_buffer->state.pipeline;

      /* Save all viewports. */
      state->viewport.count = cmd_buffer->state.dynamic.viewport.count;
      typed_memcpy(state->viewport.viewports, cmd_buffer->state.dynamic.viewport.viewports,
                   MAX_VIEWPORTS);
      typed_memcpy(state->viewport.xform, cmd_buffer->state.dynamic.viewport.xform,
                   MAX_VIEWPORTS);

      /* Save all scissors. */
      state->scissor.count = cmd_buffer->state.dynamic.scissor.count;
      typed_memcpy(state->scissor.scissors, cmd_buffer->state.dynamic.scissor.scissors,
                   MAX_SCISSORS);

      state->cull_mode = cmd_buffer->state.dynamic.cull_mode;
      state->front_face = cmd_buffer->state.dynamic.front_face;

      state->primitive_topology = cmd_buffer->state.dynamic.primitive_topology;

      state->depth_test_enable = cmd_buffer->state.dynamic.depth_test_enable;
      state->depth_write_enable = cmd_buffer->state.dynamic.depth_write_enable;
      state->depth_compare_op = cmd_buffer->state.dynamic.depth_compare_op;
      state->depth_bounds_test_enable = cmd_buffer->state.dynamic.depth_bounds_test_enable;
      state->stencil_test_enable = cmd_buffer->state.dynamic.stencil_test_enable;

      state->stencil_op.front.compare_op = cmd_buffer->state.dynamic.stencil_op.front.compare_op;
      state->stencil_op.front.fail_op = cmd_buffer->state.dynamic.stencil_op.front.fail_op;
      state->stencil_op.front.pass_op = cmd_buffer->state.dynamic.stencil_op.front.pass_op;
      state->stencil_op.front.depth_fail_op =
         cmd_buffer->state.dynamic.stencil_op.front.depth_fail_op;

      state->stencil_op.back.compare_op = cmd_buffer->state.dynamic.stencil_op.back.compare_op;
      state->stencil_op.back.fail_op = cmd_buffer->state.dynamic.stencil_op.back.fail_op;
      state->stencil_op.back.pass_op = cmd_buffer->state.dynamic.stencil_op.back.pass_op;
      state->stencil_op.back.depth_fail_op =
         cmd_buffer->state.dynamic.stencil_op.back.depth_fail_op;

      state->fragment_shading_rate.size = cmd_buffer->state.dynamic.fragment_shading_rate.size;
      state->fragment_shading_rate.combiner_ops[0] =
         cmd_buffer->state.dynamic.fragment_shading_rate.combiner_ops[0];
      state->fragment_shading_rate.combiner_ops[1] =
         cmd_buffer->state.dynamic.fragment_shading_rate.combiner_ops[1];

      state->depth_bias_enable = cmd_buffer->state.dynamic.depth_bias_enable;

      state->primitive_restart_enable = cmd_buffer->state.dynamic.primitive_restart_enable;

      state->rasterizer_discard_enable = cmd_buffer->state.dynamic.rasterizer_discard_enable;

      state->logic_op = cmd_buffer->state.dynamic.logic_op;

      state->color_write_enable = cmd_buffer->state.dynamic.color_write_enable;
   }

   if (state->flags & RADV_META_SAVE_SAMPLE_LOCATIONS) {
      typed_memcpy(&state->sample_location, &cmd_buffer->state.dynamic.sample_location, 1);
   }

   if (state->flags & RADV_META_SAVE_COMPUTE_PIPELINE) {
      assert(!(state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE));

      state->old_pipeline = cmd_buffer->state.compute_pipeline;
   }

   if (state->flags & RADV_META_SAVE_DESCRIPTORS) {
      state->old_descriptor_set0 = descriptors_state->sets[0];
      if (!(descriptors_state->valid & 1) || !state->old_descriptor_set0)
         state->flags &= ~RADV_META_SAVE_DESCRIPTORS;
   }

   if (state->flags & RADV_META_SAVE_CONSTANTS) {
      memcpy(state->push_constants, cmd_buffer->push_constants, MAX_PUSH_CONSTANTS_SIZE);
   }

   if (state->flags & RADV_META_SAVE_PASS) {
      state->pass = cmd_buffer->state.pass;
      state->subpass = cmd_buffer->state.subpass;
      state->framebuffer = cmd_buffer->state.framebuffer;
      state->attachments = cmd_buffer->state.attachments;
      state->render_area = cmd_buffer->state.render_area;
   }
}

void
radv_meta_restore(const struct radv_meta_saved_state *state, struct radv_cmd_buffer *cmd_buffer)
{
   VkPipelineBindPoint bind_point = state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE
                                       ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                       : VK_PIPELINE_BIND_POINT_COMPUTE;

   if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE) {
      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS,
                           radv_pipeline_to_handle(state->old_pipeline));

      cmd_buffer->state.dirty |= RADV_CMD_DIRTY_PIPELINE;

      /* Restore all viewports. */
      cmd_buffer->state.dynamic.viewport.count = state->viewport.count;
      typed_memcpy(cmd_buffer->state.dynamic.viewport.viewports, state->viewport.viewports,
                   MAX_VIEWPORTS);
      typed_memcpy(cmd_buffer->state.dynamic.viewport.xform, state->viewport.xform,
                   MAX_VIEWPORTS);

      /* Restore all scissors. */
      cmd_buffer->state.dynamic.scissor.count = state->scissor.count;
      typed_memcpy(cmd_buffer->state.dynamic.scissor.scissors, state->scissor.scissors,
                   MAX_SCISSORS);

      cmd_buffer->state.dynamic.cull_mode = state->cull_mode;
      cmd_buffer->state.dynamic.front_face = state->front_face;

      cmd_buffer->state.dynamic.primitive_topology = state->primitive_topology;

      cmd_buffer->state.dynamic.depth_test_enable = state->depth_test_enable;
      cmd_buffer->state.dynamic.depth_write_enable = state->depth_write_enable;
      cmd_buffer->state.dynamic.depth_compare_op = state->depth_compare_op;
      cmd_buffer->state.dynamic.depth_bounds_test_enable = state->depth_bounds_test_enable;
      cmd_buffer->state.dynamic.stencil_test_enable = state->stencil_test_enable;

      cmd_buffer->state.dynamic.stencil_op.front.compare_op = state->stencil_op.front.compare_op;
      cmd_buffer->state.dynamic.stencil_op.front.fail_op = state->stencil_op.front.fail_op;
      cmd_buffer->state.dynamic.stencil_op.front.pass_op = state->stencil_op.front.pass_op;
      cmd_buffer->state.dynamic.stencil_op.front.depth_fail_op =
         state->stencil_op.front.depth_fail_op;

      cmd_buffer->state.dynamic.stencil_op.back.compare_op = state->stencil_op.back.compare_op;
      cmd_buffer->state.dynamic.stencil_op.back.fail_op = state->stencil_op.back.fail_op;
      cmd_buffer->state.dynamic.stencil_op.back.pass_op = state->stencil_op.back.pass_op;
      cmd_buffer->state.dynamic.stencil_op.back.depth_fail_op =
         state->stencil_op.back.depth_fail_op;

      cmd_buffer->state.dynamic.fragment_shading_rate.size = state->fragment_shading_rate.size;
      cmd_buffer->state.dynamic.fragment_shading_rate.combiner_ops[0] =
         state->fragment_shading_rate.combiner_ops[0];
      cmd_buffer->state.dynamic.fragment_shading_rate.combiner_ops[1] =
         state->fragment_shading_rate.combiner_ops[1];

      cmd_buffer->state.dynamic.depth_bias_enable = state->depth_bias_enable;

      cmd_buffer->state.dynamic.primitive_restart_enable = state->primitive_restart_enable;

      cmd_buffer->state.dynamic.rasterizer_discard_enable = state->rasterizer_discard_enable;

      cmd_buffer->state.dynamic.logic_op = state->logic_op;

      cmd_buffer->state.dynamic.color_write_enable = state->color_write_enable;

      cmd_buffer->state.dirty |=
         RADV_CMD_DIRTY_DYNAMIC_VIEWPORT | RADV_CMD_DIRTY_DYNAMIC_SCISSOR |
         RADV_CMD_DIRTY_DYNAMIC_CULL_MODE | RADV_CMD_DIRTY_DYNAMIC_FRONT_FACE |
         RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY | RADV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE |
         RADV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE | RADV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP |
         RADV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE |
         RADV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE | RADV_CMD_DIRTY_DYNAMIC_STENCIL_OP |
         RADV_CMD_DIRTY_DYNAMIC_FRAGMENT_SHADING_RATE | RADV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS_ENABLE |
         RADV_CMD_DIRTY_DYNAMIC_PRIMITIVE_RESTART_ENABLE |
         RADV_CMD_DIRTY_DYNAMIC_RASTERIZER_DISCARD_ENABLE | RADV_CMD_DIRTY_DYNAMIC_LOGIC_OP |
         RADV_CMD_DIRTY_DYNAMIC_COLOR_WRITE_ENABLE;
   }

   if (state->flags & RADV_META_SAVE_SAMPLE_LOCATIONS) {
      typed_memcpy(&cmd_buffer->state.dynamic.sample_location.locations,
                   &state->sample_location.locations, 1);

      cmd_buffer->state.dirty |= RADV_CMD_DIRTY_DYNAMIC_SAMPLE_LOCATIONS;
   }

   if (state->flags & RADV_META_SAVE_COMPUTE_PIPELINE) {
      if (state->old_pipeline) {
         radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                              radv_pipeline_to_handle(state->old_pipeline));
      }
   }

   if (state->flags & RADV_META_SAVE_DESCRIPTORS) {
      radv_set_descriptor_set(cmd_buffer, bind_point, state->old_descriptor_set0, 0);
   }

   if (state->flags & RADV_META_SAVE_CONSTANTS) {
      VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT;

      if (state->flags & RADV_META_SAVE_GRAPHICS_PIPELINE)
         stages |= VK_SHADER_STAGE_ALL_GRAPHICS;

      radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), VK_NULL_HANDLE, stages, 0,
                            MAX_PUSH_CONSTANTS_SIZE, state->push_constants);
   }

   if (state->flags & RADV_META_SAVE_PASS) {
      cmd_buffer->state.pass = state->pass;
      cmd_buffer->state.subpass = state->subpass;
      cmd_buffer->state.framebuffer = state->framebuffer;
      cmd_buffer->state.attachments = state->attachments;
      cmd_buffer->state.render_area = state->render_area;
      if (state->subpass)
         cmd_buffer->state.dirty |= RADV_CMD_DIRTY_FRAMEBUFFER;
   }
}

VkImageViewType
radv_meta_get_view_type(const struct radv_image *image)
{
   switch (image->type) {
   case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D;
   case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D;
   case VK_IMAGE_TYPE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;
   default:
      unreachable("bad VkImageViewType");
   }
}

/**
 * When creating a destination VkImageView, this function provides the needed
 * VkImageViewCreateInfo::subresourceRange::baseArrayLayer.
 */
uint32_t
radv_meta_get_iview_layer(const struct radv_image *dest_image,
                          const VkImageSubresourceLayers *dest_subresource,
                          const VkOffset3D *dest_offset)
{
   switch (dest_image->type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      return dest_subresource->baseArrayLayer;
   case VK_IMAGE_TYPE_3D:
      /* HACK: Vulkan does not allow attaching a 3D image to a framebuffer,
       * but meta does it anyway. When doing so, we translate the
       * destination's z offset into an array offset.
       */
      return dest_offset->z;
   default:
      assert(!"bad VkImageType");
      return 0;
   }
}

static VKAPI_ATTR void * VKAPI_CALL
meta_alloc(void *_device, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
   struct radv_device *device = _device;
   return device->vk.alloc.pfnAllocation(device->vk.alloc.pUserData, size, alignment,
                                         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static VKAPI_ATTR void * VKAPI_CALL
meta_realloc(void *_device, void *original, size_t size, size_t alignment,
             VkSystemAllocationScope allocationScope)
{
   struct radv_device *device = _device;
   return device->vk.alloc.pfnReallocation(device->vk.alloc.pUserData, original, size, alignment,
                                           VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

static VKAPI_ATTR void VKAPI_CALL
meta_free(void *_device, void *data)
{
   struct radv_device *device = _device;
   device->vk.alloc.pfnFree(device->vk.alloc.pUserData, data);
}

#ifndef _WIN32
static bool
radv_builtin_cache_path(char *path)
{
   char *xdg_cache_home = getenv("XDG_CACHE_HOME");
   const char *suffix = "/radv_builtin_shaders";
   const char *suffix2 = "/.cache/radv_builtin_shaders";
   struct passwd pwd, *result;
   char path2[PATH_MAX + 1]; /* PATH_MAX is not a real max,but suffices here. */
   int ret;

   if (xdg_cache_home) {
      ret = snprintf(path, PATH_MAX + 1, "%s%s%zd", xdg_cache_home, suffix, sizeof(void *) * 8);
      return ret > 0 && ret < PATH_MAX + 1;
   }

   getpwuid_r(getuid(), &pwd, path2, PATH_MAX - strlen(suffix2), &result);
   if (!result)
      return false;

   strcpy(path, pwd.pw_dir);
   strcat(path, "/.cache");
   if (mkdir(path, 0755) && errno != EEXIST)
      return false;

   ret = snprintf(path, PATH_MAX + 1, "%s%s%zd", pwd.pw_dir, suffix2, sizeof(void *) * 8);
   return ret > 0 && ret < PATH_MAX + 1;
}
#endif

static bool
radv_load_meta_pipeline(struct radv_device *device)
{
#ifdef _WIN32
   return false;
#else
   char path[PATH_MAX + 1];
   struct stat st;
   void *data = NULL;
   bool ret = false;

   if (!radv_builtin_cache_path(path))
      return false;

   int fd = open(path, O_RDONLY);
   if (fd < 0)
      return false;
   if (fstat(fd, &st))
      goto fail;
   data = malloc(st.st_size);
   if (!data)
      goto fail;
   if (read(fd, data, st.st_size) == -1)
      goto fail;

   ret = radv_pipeline_cache_load(&device->meta_state.cache, data, st.st_size);
fail:
   free(data);
   close(fd);
   return ret;
#endif
}

static void
radv_store_meta_pipeline(struct radv_device *device)
{
#ifndef _WIN32
   char path[PATH_MAX + 1], path2[PATH_MAX + 7];
   size_t size;
   void *data = NULL;

   if (!device->meta_state.cache.modified)
      return;

   if (radv_GetPipelineCacheData(radv_device_to_handle(device),
                                 radv_pipeline_cache_to_handle(&device->meta_state.cache), &size,
                                 NULL))
      return;

   if (!radv_builtin_cache_path(path))
      return;

   strcpy(path2, path);
   strcat(path2, "XXXXXX");
   int fd = mkstemp(path2); // open(path, O_WRONLY | O_CREAT, 0600);
   if (fd < 0)
      return;
   data = malloc(size);
   if (!data)
      goto fail;

   if (radv_GetPipelineCacheData(radv_device_to_handle(device),
                                 radv_pipeline_cache_to_handle(&device->meta_state.cache), &size,
                                 data))
      goto fail;
   if (write(fd, data, size) == -1)
      goto fail;

   rename(path2, path);
fail:
   free(data);
   close(fd);
   unlink(path2);
#endif
}

VkResult
radv_device_init_meta(struct radv_device *device)
{
   VkResult result;

   memset(&device->meta_state, 0, sizeof(device->meta_state));

   device->meta_state.alloc = (VkAllocationCallbacks){
      .pUserData = device,
      .pfnAllocation = meta_alloc,
      .pfnReallocation = meta_realloc,
      .pfnFree = meta_free,
   };

   device->meta_state.cache.alloc = device->meta_state.alloc;
   radv_pipeline_cache_init(&device->meta_state.cache, device);
   bool loaded_cache = radv_load_meta_pipeline(device);
   bool on_demand = !loaded_cache;

   mtx_init(&device->meta_state.mtx, mtx_plain);

   result = radv_device_init_meta_clear_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_clear;

   result = radv_device_init_meta_resolve_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_resolve;

   result = radv_device_init_meta_blit_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_blit;

   result = radv_device_init_meta_blit2d_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_blit2d;

   result = radv_device_init_meta_bufimage_state(device);
   if (result != VK_SUCCESS)
      goto fail_bufimage;

   result = radv_device_init_meta_depth_decomp_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_depth_decomp;

   result = radv_device_init_meta_buffer_state(device);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   result = radv_device_init_meta_query_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_query;

   result = radv_device_init_meta_fast_clear_flush_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_fast_clear;

   result = radv_device_init_meta_resolve_compute_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_resolve_compute;

   result = radv_device_init_meta_resolve_fragment_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_resolve_fragment;

   result = radv_device_init_meta_fmask_expand_state(device);
   if (result != VK_SUCCESS)
      goto fail_fmask_expand;

   result = radv_device_init_accel_struct_build_state(device);
   if (result != VK_SUCCESS)
      goto fail_accel_struct_build;

   result = radv_device_init_meta_fmask_copy_state(device);
   if (result != VK_SUCCESS)
      goto fail_fmask_copy;

   result = radv_device_init_meta_etc_decode_state(device, on_demand);
   if (result != VK_SUCCESS)
      goto fail_etc_decode;

   return VK_SUCCESS;

fail_etc_decode:
   radv_device_finish_meta_fmask_copy_state(device);
fail_fmask_copy:
   radv_device_finish_accel_struct_build_state(device);
fail_accel_struct_build:
   radv_device_finish_meta_fmask_expand_state(device);
fail_fmask_expand:
   radv_device_finish_meta_resolve_fragment_state(device);
fail_resolve_fragment:
   radv_device_finish_meta_resolve_compute_state(device);
fail_resolve_compute:
   radv_device_finish_meta_fast_clear_flush_state(device);
fail_fast_clear:
   radv_device_finish_meta_query_state(device);
fail_query:
   radv_device_finish_meta_buffer_state(device);
fail_buffer:
   radv_device_finish_meta_depth_decomp_state(device);
fail_depth_decomp:
   radv_device_finish_meta_bufimage_state(device);
fail_bufimage:
   radv_device_finish_meta_blit2d_state(device);
fail_blit2d:
   radv_device_finish_meta_blit_state(device);
fail_blit:
   radv_device_finish_meta_resolve_state(device);
fail_resolve:
   radv_device_finish_meta_clear_state(device);
fail_clear:
   mtx_destroy(&device->meta_state.mtx);
   radv_pipeline_cache_finish(&device->meta_state.cache);
   return result;
}

void
radv_device_finish_meta(struct radv_device *device)
{
   radv_device_finish_meta_etc_decode_state(device);
   radv_device_finish_accel_struct_build_state(device);
   radv_device_finish_meta_clear_state(device);
   radv_device_finish_meta_resolve_state(device);
   radv_device_finish_meta_blit_state(device);
   radv_device_finish_meta_blit2d_state(device);
   radv_device_finish_meta_bufimage_state(device);
   radv_device_finish_meta_depth_decomp_state(device);
   radv_device_finish_meta_query_state(device);
   radv_device_finish_meta_buffer_state(device);
   radv_device_finish_meta_fast_clear_flush_state(device);
   radv_device_finish_meta_resolve_compute_state(device);
   radv_device_finish_meta_resolve_fragment_state(device);
   radv_device_finish_meta_fmask_expand_state(device);
   radv_device_finish_meta_dcc_retile_state(device);
   radv_device_finish_meta_copy_vrs_htile_state(device);
   radv_device_finish_meta_fmask_copy_state(device);

   radv_store_meta_pipeline(device);
   radv_pipeline_cache_finish(&device->meta_state.cache);
   mtx_destroy(&device->meta_state.mtx);
}

nir_builder PRINTFLIKE(2, 3) radv_meta_init_shader(gl_shader_stage stage, const char *name, ...)
{
   nir_builder b = nir_builder_init_simple_shader(stage, NULL, NULL);
   if (name) {
      va_list args;
      va_start(args, name);
      b.shader->info.name = ralloc_vasprintf(b.shader, name, args);
      va_end(args);
   }

   b.shader->info.internal = true;
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   return b;
}

nir_ssa_def *
radv_meta_gen_rect_vertices_comp2(nir_builder *vs_b, nir_ssa_def *comp2)
{

   nir_ssa_def *vertex_id = nir_load_vertex_id_zero_base(vs_b);

   /* vertex 0 - -1.0, -1.0 */
   /* vertex 1 - -1.0, 1.0 */
   /* vertex 2 - 1.0, -1.0 */
   /* so channel 0 is vertex_id != 2 ? -1.0 : 1.0
      channel 1 is vertex id != 1 ? -1.0 : 1.0 */

   nir_ssa_def *c0cmp = nir_ine(vs_b, vertex_id, nir_imm_int(vs_b, 2));
   nir_ssa_def *c1cmp = nir_ine(vs_b, vertex_id, nir_imm_int(vs_b, 1));

   nir_ssa_def *comp[4];
   comp[0] = nir_bcsel(vs_b, c0cmp, nir_imm_float(vs_b, -1.0), nir_imm_float(vs_b, 1.0));

   comp[1] = nir_bcsel(vs_b, c1cmp, nir_imm_float(vs_b, -1.0), nir_imm_float(vs_b, 1.0));
   comp[2] = comp2;
   comp[3] = nir_imm_float(vs_b, 1.0);
   nir_ssa_def *outvec = nir_vec(vs_b, comp, 4);

   return outvec;
}

nir_ssa_def *
radv_meta_gen_rect_vertices(nir_builder *vs_b)
{
   return radv_meta_gen_rect_vertices_comp2(vs_b, nir_imm_float(vs_b, 0.0));
}

/* vertex shader that generates vertices */
nir_shader *
radv_meta_build_nir_vs_generate_vertices(void)
{
   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_variable *v_position;

   nir_builder b = radv_meta_init_shader(MESA_SHADER_VERTEX, "meta_vs_gen_verts");

   nir_ssa_def *outvec = radv_meta_gen_rect_vertices(&b);

   v_position = nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   v_position->data.location = VARYING_SLOT_POS;

   nir_store_var(&b, v_position, outvec, 0xf);

   return b.shader;
}

nir_shader *
radv_meta_build_nir_fs_noop(void)
{
   return radv_meta_init_shader(MESA_SHADER_FRAGMENT, "meta_noop_fs").shader;
}

void
radv_meta_build_resolve_shader_core(nir_builder *b, bool is_integer, int samples,
                                    nir_variable *input_img, nir_variable *color,
                                    nir_ssa_def *img_coord)
{
   /* do a txf_ms on each sample */
   nir_ssa_def *tmp;
   bool inserted_if = false;

   nir_ssa_def *input_img_deref = &nir_build_deref_var(b, input_img)->dest.ssa;

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 3);
   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   tex->op = nir_texop_txf_ms;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(img_coord);
   tex->src[1].src_type = nir_tex_src_ms_index;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));
   tex->src[2].src_type = nir_tex_src_texture_deref;
   tex->src[2].src = nir_src_for_ssa(input_img_deref);
   tex->dest_type = nir_type_float32;
   tex->is_array = false;
   tex->coord_components = 2;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(b, &tex->instr);

   tmp = &tex->dest.ssa;

   if (!is_integer && samples > 1) {
      nir_tex_instr *tex_all_same = nir_tex_instr_create(b->shader, 2);
      tex_all_same->sampler_dim = GLSL_SAMPLER_DIM_MS;
      tex_all_same->op = nir_texop_samples_identical;
      tex_all_same->src[0].src_type = nir_tex_src_coord;
      tex_all_same->src[0].src = nir_src_for_ssa(img_coord);
      tex_all_same->src[1].src_type = nir_tex_src_texture_deref;
      tex_all_same->src[1].src = nir_src_for_ssa(input_img_deref);
      tex_all_same->dest_type = nir_type_bool1;
      tex_all_same->is_array = false;
      tex_all_same->coord_components = 2;

      nir_ssa_dest_init(&tex_all_same->instr, &tex_all_same->dest, 1, 1, "tex");
      nir_builder_instr_insert(b, &tex_all_same->instr);

      nir_ssa_def *all_same = nir_ieq(b, &tex_all_same->dest.ssa, nir_imm_bool(b, false));
      nir_push_if(b, all_same);
      for (int i = 1; i < samples; i++) {
         nir_tex_instr *tex_add = nir_tex_instr_create(b->shader, 3);
         tex_add->sampler_dim = GLSL_SAMPLER_DIM_MS;
         tex_add->op = nir_texop_txf_ms;
         tex_add->src[0].src_type = nir_tex_src_coord;
         tex_add->src[0].src = nir_src_for_ssa(img_coord);
         tex_add->src[1].src_type = nir_tex_src_ms_index;
         tex_add->src[1].src = nir_src_for_ssa(nir_imm_int(b, i));
         tex_add->src[2].src_type = nir_tex_src_texture_deref;
         tex_add->src[2].src = nir_src_for_ssa(input_img_deref);
         tex_add->dest_type = nir_type_float32;
         tex_add->is_array = false;
         tex_add->coord_components = 2;

         nir_ssa_dest_init(&tex_add->instr, &tex_add->dest, 4, 32, "tex");
         nir_builder_instr_insert(b, &tex_add->instr);

         tmp = nir_fadd(b, tmp, &tex_add->dest.ssa);
      }

      tmp = nir_fdiv(b, tmp, nir_imm_float(b, samples));
      nir_store_var(b, color, tmp, 0xf);
      nir_push_else(b, NULL);
      inserted_if = true;
   }
   nir_store_var(b, color, &tex->dest.ssa, 0xf);

   if (inserted_if)
      nir_pop_if(b, NULL);
}

nir_ssa_def *
radv_meta_load_descriptor(nir_builder *b, unsigned desc_set, unsigned binding)
{
   nir_ssa_def *rsrc = nir_vulkan_resource_index(b, 3, 32, nir_imm_int(b, 0), .desc_set = desc_set,
                                                 .binding = binding);
   return nir_channels(b, rsrc, 0x3);
}

nir_ssa_def *
get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_ssa_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_ssa_def *block_ids = nir_channels(b, nir_load_workgroup_id(b, 32), mask);
   nir_ssa_def *block_size = nir_channels(
      b,
      nir_imm_ivec4(b, b->shader->info.workgroup_size[0], b->shader->info.workgroup_size[1],
                    b->shader->info.workgroup_size[2], 0),
      mask);

   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

void
radv_break_on_count(nir_builder *b, nir_variable *var, nir_ssa_def *count)
{
   nir_ssa_def *counter = nir_load_var(b, var);

   nir_push_if(b, nir_uge(b, counter, count));
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, NULL);

   counter = nir_iadd(b, counter, nir_imm_int(b, 1));
   nir_store_var(b, var, counter, 0x1);
}
