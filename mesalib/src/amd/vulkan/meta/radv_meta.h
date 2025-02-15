/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_META_H
#define RADV_META_H

#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_cmd_buffer.h"
#include "radv_device.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_image.h"
#include "radv_image_view.h"
#include "radv_physical_device.h"
#include "radv_pipeline.h"
#include "radv_pipeline_compute.h"
#include "radv_pipeline_graphics.h"
#include "radv_queue.h"
#include "radv_shader.h"
#include "radv_shader_object.h"
#include "radv_sqtt.h"

#include "vk_render_pass.h"
#include "vk_shader_module.h"

#ifdef __cplusplus
extern "C" {
#endif

enum radv_meta_save_flags {
   RADV_META_SAVE_RENDER = (1 << 0),
   RADV_META_SAVE_CONSTANTS = (1 << 1),
   RADV_META_SAVE_DESCRIPTORS = (1 << 2),
   RADV_META_SAVE_GRAPHICS_PIPELINE = (1 << 3),
   RADV_META_SAVE_COMPUTE_PIPELINE = (1 << 4),
   RADV_META_SUSPEND_PREDICATING = (1 << 5),
};

struct radv_meta_saved_state {
   uint32_t flags;

   struct radv_descriptor_set *old_descriptor_set0;
   struct radv_graphics_pipeline *old_graphics_pipeline;
   struct radv_compute_pipeline *old_compute_pipeline;
   struct radv_dynamic_state dynamic;

   struct radv_shader_object *old_shader_objs[MESA_VULKAN_SHADER_STAGES];

   char push_constants[MAX_PUSH_CONSTANTS_SIZE];

   struct radv_rendering_state render;

   unsigned active_emulated_pipeline_queries;
   unsigned active_emulated_prims_gen_queries;
   unsigned active_emulated_prims_xfb_queries;
   unsigned active_occlusion_queries;

   bool predicating;
};

enum radv_blit_ds_layout {
   RADV_BLIT_DS_LAYOUT_TILE_ENABLE,
   RADV_BLIT_DS_LAYOUT_TILE_DISABLE,
   RADV_BLIT_DS_LAYOUT_COUNT,
};

static inline enum radv_blit_ds_layout
radv_meta_blit_ds_to_type(VkImageLayout layout)
{
   return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_BLIT_DS_LAYOUT_TILE_DISABLE : RADV_BLIT_DS_LAYOUT_TILE_ENABLE;
}

static inline VkImageLayout
radv_meta_blit_ds_to_layout(enum radv_blit_ds_layout ds_layout)
{
   return ds_layout == RADV_BLIT_DS_LAYOUT_TILE_ENABLE ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}

enum radv_meta_dst_layout {
   RADV_META_DST_LAYOUT_GENERAL,
   RADV_META_DST_LAYOUT_OPTIMAL,
   RADV_META_DST_LAYOUT_COUNT,
};

static inline enum radv_meta_dst_layout
radv_meta_dst_layout_from_layout(VkImageLayout layout)
{
   return (layout == VK_IMAGE_LAYOUT_GENERAL) ? RADV_META_DST_LAYOUT_GENERAL : RADV_META_DST_LAYOUT_OPTIMAL;
}

static inline VkImageLayout
radv_meta_dst_layout_to_layout(enum radv_meta_dst_layout layout)
{
   return layout == RADV_META_DST_LAYOUT_OPTIMAL ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
}

extern const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS];

enum radv_meta_object_key_type {
   RADV_META_OBJECT_KEY_NOOP = VK_META_OBJECT_KEY_DRIVER_OFFSET,
   RADV_META_OBJECT_KEY_BLIT,
   RADV_META_OBJECT_KEY_BLIT2D,
   RADV_META_OBJECT_KEY_BLIT2D_COLOR,
   RADV_META_OBJECT_KEY_BLIT2D_DEPTH,
   RADV_META_OBJECT_KEY_BLIT2D_STENCIL,
   RADV_META_OBJECT_KEY_FILL_BUFFER,
   RADV_META_OBJECT_KEY_COPY_BUFFER,
   RADV_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER,
   RADV_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE,
   RADV_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE_R32G32B32,
   RADV_META_OBJECT_KEY_COPY_IMAGE,
   RADV_META_OBJECT_KEY_COPY_IMAGE_R32G32B32,
   RADV_META_OBJECT_KEY_COPY_VRS_HTILE,
   RADV_META_OBJECT_KEY_CLEAR_CS,
   RADV_META_OBJECT_KEY_CLEAR_CS_R32G32B32,
   RADV_META_OBJECT_KEY_CLEAR_COLOR,
   RADV_META_OBJECT_KEY_CLEAR_DS,
   RADV_META_OBJECT_KEY_CLEAR_HTILE,
   RADV_META_OBJECT_KEY_CLEAR_DCC_COMP_TO_SINGLE,
   RADV_META_OBJECT_KEY_FAST_CLEAR_ELIMINATE,
   RADV_META_OBJECT_KEY_DCC_DECOMPRESS,
   RADV_META_OBJECT_KEY_DCC_RETILE,
   RADV_META_OBJECT_KEY_HTILE_EXPAND_GFX,
   RADV_META_OBJECT_KEY_HTILE_EXPAND_CS,
   RADV_META_OBJECT_KEY_FMASK_COPY,
   RADV_META_OBJECT_KEY_FMASK_EXPAND,
   RADV_META_OBJECT_KEY_FMASK_DECOMPRESS,
   RADV_META_OBJECT_KEY_RESOLVE_HW,
   RADV_META_OBJECT_KEY_RESOLVE_CS,
   RADV_META_OBJECT_KEY_RESOLVE_COLOR_CS,
   RADV_META_OBJECT_KEY_RESOLVE_DS_CS,
   RADV_META_OBJECT_KEY_RESOLVE_FS,
   RADV_META_OBJECT_KEY_RESOLVE_COLOR_FS,
   RADV_META_OBJECT_KEY_RESOLVE_DS_FS,
   RADV_META_OBJECT_KEY_DGC,
   RADV_META_OBJECT_KEY_QUERY,
   RADV_META_OBJECT_KEY_QUERY_OCCLUSION,
   RADV_META_OBJECT_KEY_QUERY_PIPELINE_STATS,
   RADV_META_OBJECT_KEY_QUERY_TFB,
   RADV_META_OBJECT_KEY_QUERY_TIMESTAMP,
   RADV_META_OBJECT_KEY_QUERY_PRIMS_GEN,
   RADV_META_OBJECT_KEY_QUERY_MESH_PRIMS_GEN,
};

VkResult radv_device_init_meta(struct radv_device *device);
void radv_device_finish_meta(struct radv_device *device);

VkResult radv_device_init_null_accel_struct(struct radv_device *device);
VkResult radv_device_init_accel_struct_build_state(struct radv_device *device);
void radv_device_finish_accel_struct_build_state(struct radv_device *device);

void radv_meta_save(struct radv_meta_saved_state *saved_state, struct radv_cmd_buffer *cmd_buffer, uint32_t flags);

void radv_meta_restore(const struct radv_meta_saved_state *state, struct radv_cmd_buffer *cmd_buffer);

VkImageViewType radv_meta_get_view_type(const struct radv_image *image);

uint32_t radv_meta_get_iview_layer(const struct radv_image *dst_image, const VkImageSubresourceLayers *dst_subresource,
                                   const VkOffset3D *dst_offset);

struct radv_meta_blit2d_surf {
   /** The size of an element in bytes. */
   uint8_t bs;
   VkFormat format;

   struct radv_image *image;
   unsigned level;
   unsigned layer;
   VkImageAspectFlags aspect_mask;
   VkImageLayout current_layout;
   bool disable_compression;
};

struct radv_meta_blit2d_buffer {
   struct radv_buffer *buffer;
   uint32_t offset;
   uint32_t pitch;
   uint8_t bs;
   VkFormat format;
};

struct radv_meta_blit2d_rect {
   uint32_t src_x, src_y;
   uint32_t dst_x, dst_y;
   uint32_t width, height;
};

void radv_meta_begin_blit2d(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_saved_state *save);

void radv_meta_blit2d(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src_img,
                      struct radv_meta_blit2d_buffer *src_buf, struct radv_meta_blit2d_surf *dst,
                      struct radv_meta_blit2d_rect *rect);

void radv_meta_end_blit2d(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_saved_state *save);

void radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                               struct radv_meta_blit2d_buffer *dst, struct radv_meta_blit2d_rect *rect);

void radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                                  struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect);
void radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                                 struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect);
void radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *dst,
                              const VkClearColorValue *clear_color);

void radv_expand_depth_stencil(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *subresourceRange,
                               struct radv_sample_locations_state *sample_locs);
void radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                         const VkImageSubresourceRange *subresourceRange);
void radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                         const VkImageSubresourceRange *subresourceRange);
void radv_retile_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image);
void radv_expand_fmask_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                     const VkImageSubresourceRange *subresourceRange);
void radv_copy_vrs_htile(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *vrs_iview, const VkRect2D *rect,
                         struct radv_image *dst_image, uint64_t htile_va, bool read_htile_value);

bool radv_can_use_fmask_copy(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *src_image,
                             const struct radv_image *dst_image, const struct radv_meta_blit2d_rect *rect);
void radv_fmask_copy(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                     struct radv_meta_blit2d_surf *dst);

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                     VkFormat src_format, VkImageLayout src_image_layout, struct radv_image *dst_image,
                                     VkFormat dst_format, VkImageLayout dst_image_layout,
                                     const VkImageResolve2 *region);

void radv_meta_resolve_fragment_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                      VkImageLayout src_image_layout, struct radv_image *dst_image,
                                      VkImageLayout dst_image_layout, const VkImageResolve2 *region);

void radv_decompress_resolve_rendering_src(struct radv_cmd_buffer *cmd_buffer);

void radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                 VkImageLayout src_image_layout, const VkImageResolve2 *region);

uint32_t radv_clear_cmask(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_fmask(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                        const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_htile(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image,
                          const VkImageSubresourceRange *range, uint32_t value, bool is_clear);

void radv_update_buffer_cp(struct radv_cmd_buffer *cmd_buffer, uint64_t va, const void *data, uint64_t size);

void radv_meta_decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, VkImageLayout layout,
                          const VkImageSubresourceLayers *subresource, VkOffset3D offset, VkExtent3D extent);
void radv_meta_decode_astc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, VkImageLayout layout,
                           const VkImageSubresourceLayers *subresource, VkOffset3D offset, VkExtent3D extent);

uint32_t radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image,
                          struct radeon_winsys_bo *bo, uint64_t va, uint64_t size, uint32_t value);

void radv_copy_buffer(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *src_bo,
                      struct radeon_winsys_bo *dst_bo, uint64_t src_offset, uint64_t dst_offset, uint64_t size);

void radv_cmd_buffer_clear_attachment(struct radv_cmd_buffer *cmd_buffer, const VkClearAttachment *attachment);

void radv_cmd_buffer_clear_rendering(struct radv_cmd_buffer *cmd_buffer, const VkRenderingInfo *render_info);

void radv_cmd_buffer_resolve_rendering(struct radv_cmd_buffer *cmd_buffer);

void radv_cmd_buffer_resolve_rendering_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview,
                                          VkImageLayout src_layout, struct radv_image_view *dst_iview,
                                          VkImageLayout dst_layout, const VkImageResolve2 *region);

void radv_depth_stencil_resolve_rendering_cs(struct radv_cmd_buffer *cmd_buffer, VkImageAspectFlags aspects,
                                             VkResolveModeFlagBits resolve_mode);

void radv_cmd_buffer_resolve_rendering_fs(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview,
                                          VkImageLayout src_layout, struct radv_image_view *dst_iview,
                                          VkImageLayout dst_layout);

void radv_depth_stencil_resolve_rendering_fs(struct radv_cmd_buffer *cmd_buffer, VkImageAspectFlags aspects,
                                             VkResolveModeFlagBits resolve_mode);

VkResult radv_meta_get_noop_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_H */
