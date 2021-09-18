/*
 * Copyright © 2019 Raspberry Pi
 *
 * Based in part on v3d driver which is:
 *
 * Copyright © 2014-2017 Broadcom
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

#include "v3dv_private.h"
#include "vk_format_info.h"

/* The only version specific structure that we need is
 * TMU_CONFIG_PARAMETER_1. This didn't seem to change significantly from
 * previous V3D versions and we don't expect that to change, so for now let's
 * just hardcode the V3D version here.
 */
#define V3D_VERSION 41
#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"

/* Our Vulkan resource indices represent indices in descriptor maps which
 * include all shader stages, so we need to size the arrays below
 * accordingly. For now we only support a maximum of 3 stages: VS, GS, FS.
 */
#define MAX_STAGES 3

#define MAX_TOTAL_TEXTURE_SAMPLERS (V3D_MAX_TEXTURE_SAMPLERS * MAX_STAGES)
struct texture_bo_list {
   struct v3dv_bo *tex[MAX_TOTAL_TEXTURE_SAMPLERS];
};

/* This tracks state BOs for both textures and samplers, so we
 * multiply by 2.
 */
#define MAX_TOTAL_STATES (2 * V3D_MAX_TEXTURE_SAMPLERS * MAX_STAGES)
struct state_bo_list {
   uint32_t count;
   struct v3dv_bo *states[MAX_TOTAL_STATES];
};

#define MAX_TOTAL_UNIFORM_BUFFERS (1 + MAX_UNIFORM_BUFFERS * MAX_STAGES)
#define MAX_TOTAL_STORAGE_BUFFERS (MAX_STORAGE_BUFFERS * MAX_STAGES)
struct buffer_bo_list {
   struct v3dv_bo *ubo[MAX_TOTAL_UNIFORM_BUFFERS];
   struct v3dv_bo *ssbo[MAX_TOTAL_STORAGE_BUFFERS];
};

static bool
state_bo_in_list(struct state_bo_list *list, struct v3dv_bo *bo)
{
   for (int i = 0; i < list->count; i++) {
      if (list->states[i] == bo)
         return true;
   }
   return false;
}

/*
 * This method checks if the ubo used for push constants is needed to be
 * updated or not.
 *
 * push contants ubo is only used for push constants accessed by a non-const
 * index.
 *
 * FIXME: right now for this cases we are uploading the full
 * push_constants_data. An improvement would be to upload only the data that
 * we need to rely on a UBO.
 */
static void
check_push_constants_ubo(struct v3dv_cmd_buffer *cmd_buffer,
                         struct v3dv_pipeline *pipeline)
{
   if (!(cmd_buffer->state.dirty & V3DV_CMD_DIRTY_PUSH_CONSTANTS) ||
       pipeline->layout->push_constant_size == 0)
      return;

   if (cmd_buffer->push_constants_resource.bo == NULL) {
      cmd_buffer->push_constants_resource.bo =
         v3dv_bo_alloc(cmd_buffer->device, MAX_PUSH_CONSTANTS_SIZE,
                       "push constants", true);

      if (!cmd_buffer->push_constants_resource.bo) {
         fprintf(stderr, "Failed to allocate memory for push constants\n");
         abort();
      }

      bool ok = v3dv_bo_map(cmd_buffer->device,
                            cmd_buffer->push_constants_resource.bo,
                            MAX_PUSH_CONSTANTS_SIZE);
      if (!ok) {
         fprintf(stderr, "failed to map push constants buffer\n");
         abort();
      }
   } else {
      if (cmd_buffer->push_constants_resource.offset + MAX_PUSH_CONSTANTS_SIZE <=
          cmd_buffer->push_constants_resource.bo->size) {
         cmd_buffer->push_constants_resource.offset += MAX_PUSH_CONSTANTS_SIZE;
      } else {
         /* FIXME: we got out of space for push descriptors. Should we create
          * a new bo? This could be easier with a uploader
          */
      }
   }

   memcpy(cmd_buffer->push_constants_resource.bo->map +
          cmd_buffer->push_constants_resource.offset,
          cmd_buffer->push_constants_data,
          MAX_PUSH_CONSTANTS_SIZE);

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_PUSH_CONSTANTS;
}

/** V3D 4.x TMU configuration parameter 0 (texture) */
static void
write_tmu_p0(struct v3dv_cmd_buffer *cmd_buffer,
             struct v3dv_pipeline *pipeline,
             enum broadcom_shader_stage stage,
             struct v3dv_cl_out **uniforms,
             uint32_t data,
             struct texture_bo_list *tex_bos,
             struct state_bo_list *state_bos)
{
   uint32_t texture_idx = v3d_unit_data_get_unit(data);

   struct v3dv_descriptor_state *descriptor_state =
      v3dv_cmd_buffer_get_descriptor_state(cmd_buffer, pipeline);

   /* We need to ensure that the texture bo is added to the job */
   struct v3dv_bo *texture_bo =
      v3dv_descriptor_map_get_texture_bo(descriptor_state,
                                         &pipeline->shared_data->maps[stage]->texture_map,
                                         pipeline->layout, texture_idx);
   assert(texture_bo);
   assert(texture_idx < V3D_MAX_TEXTURE_SAMPLERS);
   tex_bos->tex[texture_idx] = texture_bo;

   struct v3dv_cl_reloc state_reloc =
      v3dv_descriptor_map_get_texture_shader_state(cmd_buffer->device, descriptor_state,
                                                   &pipeline->shared_data->maps[stage]->texture_map,
                                                   pipeline->layout,
                                                   texture_idx);

   cl_aligned_u32(uniforms, state_reloc.bo->offset +
                            state_reloc.offset +
                            v3d_unit_data_get_offset(data));

   /* Texture and Sampler states are typically suballocated, so they are
    * usually the same BO: only flag them once to avoid trying to add them
    * multiple times to the job later.
    */
   if (!state_bo_in_list(state_bos, state_reloc.bo)) {
      assert(state_bos->count < 2 * V3D_MAX_TEXTURE_SAMPLERS);
      state_bos->states[state_bos->count++] = state_reloc.bo;
   }
}

/** V3D 4.x TMU configuration parameter 1 (sampler) */
static void
write_tmu_p1(struct v3dv_cmd_buffer *cmd_buffer,
             struct v3dv_pipeline *pipeline,
             enum broadcom_shader_stage stage,
             struct v3dv_cl_out **uniforms,
             uint32_t data,
             struct state_bo_list *state_bos)
{
   uint32_t sampler_idx = v3d_unit_data_get_unit(data);
   struct v3dv_descriptor_state *descriptor_state =
      v3dv_cmd_buffer_get_descriptor_state(cmd_buffer, pipeline);

   assert(sampler_idx != V3DV_NO_SAMPLER_16BIT_IDX &&
          sampler_idx != V3DV_NO_SAMPLER_32BIT_IDX);

   struct v3dv_cl_reloc sampler_state_reloc =
      v3dv_descriptor_map_get_sampler_state(cmd_buffer->device, descriptor_state,
                                            &pipeline->shared_data->maps[stage]->sampler_map,
                                            pipeline->layout, sampler_idx);

   const struct v3dv_sampler *sampler =
      v3dv_descriptor_map_get_sampler(descriptor_state,
                                      &pipeline->shared_data->maps[stage]->sampler_map,
                                      pipeline->layout, sampler_idx);
   assert(sampler);

   /* Set unnormalized coordinates flag from sampler object */
   uint32_t p1_packed = v3d_unit_data_get_offset(data);
   if (sampler->unnormalized_coordinates) {
      struct V3DX(TMU_CONFIG_PARAMETER_1) p1_unpacked;
      V3DX(TMU_CONFIG_PARAMETER_1_unpack)((uint8_t *)&p1_packed, &p1_unpacked);
      p1_unpacked.unnormalized_coordinates = true;
      V3DX(TMU_CONFIG_PARAMETER_1_pack)(NULL, (uint8_t *)&p1_packed,
                                        &p1_unpacked);
   }

   cl_aligned_u32(uniforms, sampler_state_reloc.bo->offset +
                            sampler_state_reloc.offset +
                            p1_packed);

   /* Texture and Sampler states are typically suballocated, so they are
    * usually the same BO: only flag them once to avoid trying to add them
    * multiple times to the job later.
    */
   if (!state_bo_in_list(state_bos, sampler_state_reloc.bo)) {
      assert(state_bos->count < 2 * V3D_MAX_TEXTURE_SAMPLERS);
      state_bos->states[state_bos->count++] = sampler_state_reloc.bo;
   }
}

static void
write_ubo_ssbo_uniforms(struct v3dv_cmd_buffer *cmd_buffer,
                        struct v3dv_pipeline *pipeline,
                        enum broadcom_shader_stage stage,
                        struct v3dv_cl_out **uniforms,
                        enum quniform_contents content,
                        uint32_t data,
                        struct buffer_bo_list *buffer_bos)
{
   struct v3dv_descriptor_state *descriptor_state =
      v3dv_cmd_buffer_get_descriptor_state(cmd_buffer, pipeline);

   struct v3dv_descriptor_map *map =
      content == QUNIFORM_UBO_ADDR || content == QUNIFORM_GET_UBO_SIZE ?
      &pipeline->shared_data->maps[stage]->ubo_map :
      &pipeline->shared_data->maps[stage]->ssbo_map;

   uint32_t offset =
      content == QUNIFORM_UBO_ADDR ?
      v3d_unit_data_get_offset(data) :
      0;

   uint32_t dynamic_offset = 0;

   /* For ubos, index is shifted, as 0 is reserved for push constants.
    */
   if (content == QUNIFORM_UBO_ADDR &&
       v3d_unit_data_get_unit(data) == 0) {
      /* This calls is to ensure that the push_constant_ubo is
       * updated. It already take into account it is should do the
       * update or not
       */
      check_push_constants_ubo(cmd_buffer, pipeline);

      struct v3dv_cl_reloc *resource =
         &cmd_buffer->push_constants_resource;
      assert(resource->bo);

      cl_aligned_u32(uniforms, resource->bo->offset +
                               resource->offset +
                               offset + dynamic_offset);
      buffer_bos->ubo[0] = resource->bo;
   } else {
      uint32_t index =
         content == QUNIFORM_UBO_ADDR ?
         v3d_unit_data_get_unit(data) - 1 :
         data;

      struct v3dv_descriptor *descriptor =
         v3dv_descriptor_map_get_descriptor(descriptor_state, map,
                                            pipeline->layout,
                                            index, &dynamic_offset);
      assert(descriptor);
      assert(descriptor->buffer);
      assert(descriptor->buffer->mem);
      assert(descriptor->buffer->mem->bo);

      if (content == QUNIFORM_GET_SSBO_SIZE ||
          content == QUNIFORM_GET_UBO_SIZE) {
         cl_aligned_u32(uniforms, descriptor->range);
      } else {
         cl_aligned_u32(uniforms, descriptor->buffer->mem->bo->offset +
                                  descriptor->buffer->mem_offset +
                                  descriptor->offset +
                                  offset + dynamic_offset);

         if (content == QUNIFORM_UBO_ADDR) {
            assert(index + 1 < MAX_TOTAL_UNIFORM_BUFFERS);
            buffer_bos->ubo[index + 1] = descriptor->buffer->mem->bo;
         } else {
            assert(index < MAX_TOTAL_STORAGE_BUFFERS);
            buffer_bos->ssbo[index] = descriptor->buffer->mem->bo;
         }
      }
   }
}

static uint32_t
get_texture_size_from_image_view(struct v3dv_image_view *image_view,
                                 enum quniform_contents contents,
                                 uint32_t data)
{
   switch(contents) {
   case QUNIFORM_IMAGE_WIDTH:
   case QUNIFORM_TEXTURE_WIDTH:
      /* We don't u_minify the values, as we are using the image_view
       * extents
       */
      return image_view->vk.extent.width;
   case QUNIFORM_IMAGE_HEIGHT:
   case QUNIFORM_TEXTURE_HEIGHT:
      return image_view->vk.extent.height;
   case QUNIFORM_IMAGE_DEPTH:
   case QUNIFORM_TEXTURE_DEPTH:
      return image_view->vk.extent.depth;
   case QUNIFORM_IMAGE_ARRAY_SIZE:
   case QUNIFORM_TEXTURE_ARRAY_SIZE:
      if (image_view->vk.view_type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
         return image_view->vk.layer_count;
      } else {
         assert(image_view->vk.layer_count % 6 == 0);
         return image_view->vk.layer_count / 6;
      }
   case QUNIFORM_TEXTURE_LEVELS:
      return image_view->vk.level_count;
   case QUNIFORM_TEXTURE_SAMPLES:
      assert(image_view->vk.image);
      return image_view->vk.image->samples;
   default:
      unreachable("Bad texture size field");
   }
}


static uint32_t
get_texture_size_from_buffer_view(struct v3dv_buffer_view *buffer_view,
                                  enum quniform_contents contents,
                                  uint32_t data)
{
   switch(contents) {
   case QUNIFORM_IMAGE_WIDTH:
   case QUNIFORM_TEXTURE_WIDTH:
      return buffer_view->num_elements;
   /* Only size can be queried for texel buffers  */
   default:
      unreachable("Bad texture size field for texel buffers");
   }
}

static uint32_t
get_texture_size(struct v3dv_cmd_buffer *cmd_buffer,
                 struct v3dv_pipeline *pipeline,
                 enum broadcom_shader_stage stage,
                 enum quniform_contents contents,
                 uint32_t data)
{
   uint32_t texture_idx = data;

   struct v3dv_descriptor_state *descriptor_state =
      v3dv_cmd_buffer_get_descriptor_state(cmd_buffer, pipeline);

   struct v3dv_descriptor *descriptor =
      v3dv_descriptor_map_get_descriptor(descriptor_state,
                                         &pipeline->shared_data->maps[stage]->texture_map,
                                         pipeline->layout,
                                         texture_idx, NULL);

   assert(descriptor);

   switch (descriptor->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return get_texture_size_from_image_view(descriptor->image_view,
                                              contents, data);
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return get_texture_size_from_buffer_view(descriptor->buffer_view,
                                               contents, data);
   default:
      unreachable("Wrong descriptor for getting texture size");
   }
}

struct v3dv_cl_reloc
v3dv_write_uniforms_wg_offsets(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_pipeline *pipeline,
                               struct v3dv_shader_variant *variant,
                               uint32_t **wg_count_offsets)
{
   struct v3d_uniform_list *uinfo =
      &variant->prog_data.base->uniforms;
   struct v3dv_dynamic_state *dynamic = &cmd_buffer->state.dynamic;

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);
   assert(job->cmd_buffer == cmd_buffer);

   struct texture_bo_list tex_bos = { 0 };
   struct state_bo_list state_bos = { 0 };
   struct buffer_bo_list buffer_bos = { 0 };

   /* The hardware always pre-fetches the next uniform (also when there
    * aren't any), so we always allocate space for an extra slot. This
    * fixes MMU exceptions reported since Linux kernel 5.4 when the
    * uniforms fill up the tail bytes of a page in the indirect
    * BO. In that scenario, when the hardware pre-fetches after reading
    * the last uniform it will read beyond the end of the page and trigger
    * the MMU exception.
    */
   v3dv_cl_ensure_space(&job->indirect, (uinfo->count + 1) * 4, 4);

   struct v3dv_cl_reloc uniform_stream = v3dv_cl_get_address(&job->indirect);

   struct v3dv_cl_out *uniforms = cl_start(&job->indirect);

   for (int i = 0; i < uinfo->count; i++) {
      uint32_t data = uinfo->data[i];

      switch (uinfo->contents[i]) {
      case QUNIFORM_CONSTANT:
         cl_aligned_u32(&uniforms, data);
         break;

      case QUNIFORM_UNIFORM:
         cl_aligned_u32(&uniforms, cmd_buffer->push_constants_data[data]);
         break;

      case QUNIFORM_VIEWPORT_X_SCALE:
         cl_aligned_f(&uniforms, dynamic->viewport.scale[0][0] * 256.0f);
         break;

      case QUNIFORM_VIEWPORT_Y_SCALE:
         cl_aligned_f(&uniforms, dynamic->viewport.scale[0][1] * 256.0f);
         break;

      case QUNIFORM_VIEWPORT_Z_OFFSET:
         cl_aligned_f(&uniforms, dynamic->viewport.translate[0][2]);
         break;

      case QUNIFORM_VIEWPORT_Z_SCALE:
         cl_aligned_f(&uniforms, dynamic->viewport.scale[0][2]);
         break;

      case QUNIFORM_SSBO_OFFSET:
      case QUNIFORM_UBO_ADDR:
      case QUNIFORM_GET_SSBO_SIZE:
      case QUNIFORM_GET_UBO_SIZE:
         write_ubo_ssbo_uniforms(cmd_buffer, pipeline, variant->stage, &uniforms,
                                 uinfo->contents[i], data, &buffer_bos);

        break;

      case QUNIFORM_IMAGE_TMU_CONFIG_P0:
      case QUNIFORM_TMU_CONFIG_P0:
         write_tmu_p0(cmd_buffer, pipeline, variant->stage,
                      &uniforms, data, &tex_bos, &state_bos);
         break;

      case QUNIFORM_TMU_CONFIG_P1:
         write_tmu_p1(cmd_buffer, pipeline, variant->stage,
                      &uniforms, data, &state_bos);
         break;

      case QUNIFORM_IMAGE_WIDTH:
      case QUNIFORM_IMAGE_HEIGHT:
      case QUNIFORM_IMAGE_DEPTH:
      case QUNIFORM_IMAGE_ARRAY_SIZE:
      case QUNIFORM_TEXTURE_WIDTH:
      case QUNIFORM_TEXTURE_HEIGHT:
      case QUNIFORM_TEXTURE_DEPTH:
      case QUNIFORM_TEXTURE_ARRAY_SIZE:
      case QUNIFORM_TEXTURE_LEVELS:
      case QUNIFORM_TEXTURE_SAMPLES:
         cl_aligned_u32(&uniforms,
                        get_texture_size(cmd_buffer,
                                         pipeline,
                                         variant->stage,
                                         uinfo->contents[i],
                                         data));
         break;

      /* We generate this from geometry shaders to cap the generated gl_Layer
       * to be within the number of layers of the framebuffer so we prevent the
       * binner from trying to access tile state memory out of bounds (for
       * layers that don't exist).
       *
       * Unfortunately, for secondary command buffers we may not know the
       * number of layers in the framebuffer at this stage. Since we are
       * only using this to sanitize the shader and it should not have any
       * impact on correct shaders that emit valid values for gl_Layer,
       * we just work around it by using the largest number of layers we
       * support.
       *
       * FIXME: we could do better than this by recording in the job that
       * the value at this uniform offset is not correct, and patch it when
       * we execute the secondary command buffer into a primary, since we do
       * have the correct number of layers at that point, but again, since this
       * is only for sanityzing the shader and it only affects the specific case
       * of secondary command buffers without framebuffer info available it
       * might not be worth the trouble.
       *
       * With multiview the number of layers is dictated by the view mask
       * and not by the framebuffer layers. We do set the job's frame tiling
       * information correctly from the view mask in that case, however,
       * secondary command buffers may not have valid frame tiling data,
       * so when multiview is enabled, we always set the number of layers
       * from the subpass view mask.
       */
      case QUNIFORM_FB_LAYERS: {
         const struct v3dv_cmd_buffer_state *state = &job->cmd_buffer->state;
         const uint32_t view_mask =
            state->pass->subpasses[state->subpass_idx].view_mask;

         uint32_t num_layers;
         if (view_mask != 0) {
            num_layers = util_last_bit(view_mask);
         } else if (job->frame_tiling.layers != 0) {
            num_layers = job->frame_tiling.layers;
         } else if (cmd_buffer->state.framebuffer) {
            num_layers = cmd_buffer->state.framebuffer->layers;
         } else {
            assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
            num_layers = 2048;
#if DEBUG
            fprintf(stderr, "Skipping gl_LayerID shader sanity check for "
                            "secondary command buffer\n");
#endif
         }
         cl_aligned_u32(&uniforms, num_layers);
         break;
      }

      case QUNIFORM_VIEW_INDEX:
         cl_aligned_u32(&uniforms, job->cmd_buffer->state.view_index);
         break;

      case QUNIFORM_NUM_WORK_GROUPS:
         assert(job->type == V3DV_JOB_TYPE_GPU_CSD);
         assert(job->csd.wg_count[data] > 0);
         if (wg_count_offsets)
            wg_count_offsets[data] = (uint32_t *) uniforms;
         cl_aligned_u32(&uniforms, job->csd.wg_count[data]);
         break;

      case QUNIFORM_WORK_GROUP_BASE:
         assert(job->type == V3DV_JOB_TYPE_GPU_CSD);
         cl_aligned_u32(&uniforms, job->csd.wg_base[data]);
         break;

      case QUNIFORM_SHARED_OFFSET:
         assert(job->type == V3DV_JOB_TYPE_GPU_CSD);
         assert(job->csd.shared_memory);
         cl_aligned_u32(&uniforms, job->csd.shared_memory->offset);
         break;

      case QUNIFORM_SPILL_OFFSET:
         assert(pipeline->spill.bo);
         cl_aligned_u32(&uniforms, pipeline->spill.bo->offset);
         break;

      case QUNIFORM_SPILL_SIZE_PER_THREAD:
         assert(pipeline->spill.size_per_thread > 0);
         cl_aligned_u32(&uniforms, pipeline->spill.size_per_thread);
         break;

      default:
         unreachable("unsupported quniform_contents uniform type\n");
      }
   }

   cl_end(&job->indirect, uniforms);

   for (int i = 0; i < MAX_TOTAL_TEXTURE_SAMPLERS; i++) {
      if (tex_bos.tex[i])
         v3dv_job_add_bo(job, tex_bos.tex[i]);
   }

   for (int i = 0; i < state_bos.count; i++)
      v3dv_job_add_bo(job, state_bos.states[i]);

   for (int i = 0; i < MAX_TOTAL_UNIFORM_BUFFERS; i++) {
      if (buffer_bos.ubo[i])
         v3dv_job_add_bo(job, buffer_bos.ubo[i]);
   }

   for (int i = 0; i < MAX_TOTAL_STORAGE_BUFFERS; i++) {
      if (buffer_bos.ssbo[i])
         v3dv_job_add_bo(job, buffer_bos.ssbo[i]);
   }

   if (job->csd.shared_memory)
      v3dv_job_add_bo(job, job->csd.shared_memory);

   if (pipeline->spill.bo)
      v3dv_job_add_bo(job, pipeline->spill.bo);

   return uniform_stream;
}

struct v3dv_cl_reloc
v3dv_write_uniforms(struct v3dv_cmd_buffer *cmd_buffer,
                    struct v3dv_pipeline *pipeline,
                    struct v3dv_shader_variant *variant)
{
   return v3dv_write_uniforms_wg_offsets(cmd_buffer, pipeline, variant, NULL);
}
