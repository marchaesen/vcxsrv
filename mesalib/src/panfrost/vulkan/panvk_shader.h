/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_SHADER_H
#define PANVK_SHADER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "util/pan_ir.h"

#include "pan_desc.h"

#include "panvk_descriptor_set.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"

#include "vk_pipeline_layout.h"

#include "vk_shader.h"

extern const struct vk_device_shader_ops panvk_per_arch(device_shader_ops);

#define MAX_VS_ATTRIBS 16

struct nir_shader;
struct pan_blend_state;
struct panvk_device;

enum panvk_varying_buf_id {
   PANVK_VARY_BUF_GENERAL,
   PANVK_VARY_BUF_POSITION,
   PANVK_VARY_BUF_PSIZ,

   /* Keep last */
   PANVK_VARY_BUF_MAX,
};

struct panvk_graphics_sysvals {
   struct {
      struct {
         float x, y, z;
      } scale, offset;
   } viewport;

   struct {
      float constants[4];
   } blend;

   struct {
      uint32_t first_vertex;
      uint32_t base_vertex;
      uint32_t base_instance;
   } vs;

   struct {
      uint32_t multisampled;
   } fs;

#if PAN_ARCH <= 7
   /* gl_Layer on Bifrost is a bit of hack. We have to issue one draw per
    * layer, and filter primitives at the VS level.
    */
   int32_t layer_id;

   struct {
      uint64_t sets[MAX_SETS];
      uint64_t vs_dyn_ssbos;
      uint64_t fs_dyn_ssbos;
   } desc;
#endif
};

struct panvk_compute_sysvals {
   struct {
      uint32_t x, y, z;
   } base;
   struct {
      uint32_t x, y, z;
   } num_work_groups;
   struct {
      uint32_t x, y, z;
   } local_group_size;

#if PAN_ARCH <= 7
   struct {
      uint64_t sets[MAX_SETS];
      uint64_t dyn_ssbos;
   } desc;
#endif
};

#if PAN_ARCH <= 7
enum panvk_bifrost_desc_table_type {
   PANVK_BIFROST_DESC_TABLE_INVALID = -1,

   /* UBO is encoded on 8 bytes */
   PANVK_BIFROST_DESC_TABLE_UBO = 0,

   /* Images are using a <3DAttributeBuffer,Attribute> pair, each
    * of them being stored in a separate table. */
   PANVK_BIFROST_DESC_TABLE_IMG,

   /* Texture and sampler are encoded on 32 bytes */
   PANVK_BIFROST_DESC_TABLE_TEXTURE,
   PANVK_BIFROST_DESC_TABLE_SAMPLER,

   PANVK_BIFROST_DESC_TABLE_COUNT,
};
#endif

#define COPY_DESC_HANDLE(table, idx)           ((table << 28) | (idx))
#define COPY_DESC_HANDLE_EXTRACT_INDEX(handle) ((handle) & BITFIELD_MASK(28))
#define COPY_DESC_HANDLE_EXTRACT_TABLE(handle) ((handle) >> 28)

struct panvk_shader {
   struct vk_shader vk;
   struct pan_shader_info info;
   struct pan_compute_dim local_size;

   struct {
      uint32_t used_set_mask;

#if PAN_ARCH <= 7
      struct {
         uint32_t map[MAX_DYNAMIC_UNIFORM_BUFFERS];
         uint32_t count;
      } dyn_ubos;
      struct {
         uint32_t map[MAX_DYNAMIC_STORAGE_BUFFERS];
         uint32_t count;
      } dyn_ssbos;
      struct {
         struct panvk_priv_mem map;
         uint32_t count[PANVK_BIFROST_DESC_TABLE_COUNT];
      } others;
#else
      struct {
         uint32_t map[MAX_DYNAMIC_BUFFERS];
         uint32_t count;
      } dyn_bufs;
#endif
   } desc_info;

   const void *bin_ptr;
   uint32_t bin_size;

   struct panvk_priv_mem code_mem;

#if PAN_ARCH <= 7
   struct panvk_priv_mem rsd;
#else
   union {
      struct panvk_priv_mem spd;
      struct {
         struct panvk_priv_mem pos_points;
         struct panvk_priv_mem pos_triangles;
         struct panvk_priv_mem var;
      } spds;
   };
#endif

   const char *nir_str;
   const char *asm_str;
};

struct panvk_shader_link {
   struct {
      struct panvk_priv_mem attribs;
   } vs, fs;
   unsigned buf_strides[PANVK_VARY_BUF_MAX];
};

static inline mali_ptr
panvk_shader_get_dev_addr(const struct panvk_shader *shader)
{
   return shader != NULL ? panvk_priv_mem_dev_addr(shader->code_mem) : 0;
}

VkResult panvk_per_arch(link_shaders)(struct panvk_pool *desc_pool,
                                      const struct panvk_shader *vs,
                                      const struct panvk_shader *fs,
                                      struct panvk_shader_link *link);

static inline void
panvk_shader_link_cleanup(struct panvk_shader_link *link)
{
   panvk_pool_free_mem(&link->vs.attribs);
   panvk_pool_free_mem(&link->fs.attribs);
}

bool panvk_per_arch(nir_lower_descriptors)(
   nir_shader *nir, struct panvk_device *dev,
   const struct vk_pipeline_robustness_state *rs, uint32_t set_layout_count,
   struct vk_descriptor_set_layout *const *set_layouts,
   struct panvk_shader *shader);

/* This a stripped-down version of panvk_shader for internal shaders that
 * are managed by vk_meta (blend and preload shaders). Those don't need the
 * complexity inherent to user provided shaders as they're not exposed. */
struct panvk_internal_shader {
   struct vk_shader vk;
   struct pan_shader_info info;
   struct panvk_priv_mem code_mem;

#if PAN_ARCH <= 7
   struct panvk_priv_mem rsd;
#else
   struct panvk_priv_mem spd;
#endif
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_internal_shader, vk.base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT)

VkResult panvk_per_arch(create_internal_shader)(
   struct panvk_device *dev, nir_shader *nir,
   struct panfrost_compile_inputs *compiler_inputs,
   struct panvk_internal_shader **shader_out);

#endif
