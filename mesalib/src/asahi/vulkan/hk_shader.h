/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/compiler/agx_compile.h"
#include "util/macros.h"
#include "agx_linker.h"
#include "agx_nir_lower_vbo.h"
#include "agx_pack.h"
#include "agx_usc.h"
#include "agx_uvs.h"

#include "hk_device.h"
#include "hk_device_memory.h"
#include "hk_private.h"

#include "nir_xfb_info.h"
#include "shader_enums.h"
#include "vk_pipeline_cache.h"

#include "nir.h"

#include "vk_shader.h"

struct hk_physical_device;
struct hk_pipeline_compilation_ctx;
struct vk_descriptor_set_layout;
struct vk_graphics_pipeline_state;
struct vk_pipeline_cache;
struct vk_pipeline_layout;
struct vk_pipeline_robustness_state;
struct vk_shader_module;

/* TODO: Make dynamic */
#define HK_ROOT_UNIFORM       104
#define HK_IMAGE_HEAP_UNIFORM 108

struct hk_shader_info {
   union {
      struct {
         uint32_t attribs_read;
         BITSET_DECLARE(attrib_components_read, AGX_MAX_ATTRIBS * 4);
         uint8_t cull_distance_array_size;
         uint8_t _pad[7];
      } vs;

      struct {
         /* Local workgroup size */
         uint16_t local_size[3];

         uint8_t _pad[26];
      } cs;

      struct {
         struct agx_interp_info interp;
         struct agx_fs_epilog_link_info epilog_key;

         bool reads_sample_mask;
         bool post_depth_coverage;
         bool uses_sample_shading;
         bool early_fragment_tests;
         bool writes_memory;

         uint8_t _pad[7];
      } fs;

      struct {
         uint8_t spacing;
         uint8_t mode;
         enum mesa_prim out_prim;
         bool point_mode;
         bool ccw;
         uint8_t _pad[27];
      } ts;

      struct {
         uint64_t per_vertex_outputs;
         uint32_t output_stride;
         uint8_t output_patch_size;
         uint8_t nr_patch_outputs;
         uint8_t _pad[18];
      } tcs;

      struct {
         unsigned count_words;
         enum mesa_prim out_prim;
         uint8_t _pad[27];
      } gs;

      /* Used to initialize the union for other stages */
      uint8_t _pad[32];
   };

   struct agx_unlinked_uvs_layout uvs;

   /* Transform feedback buffer strides */
   uint8_t xfb_stride[MAX_XFB_BUFFERS];

   gl_shader_stage stage : 8;
   uint8_t clip_distance_array_size;
   uint8_t cull_distance_array_size;
   uint8_t _pad0[1];

   /* XXX: is there a less goofy way to do this? I really don't want dynamic
    * allocation here.
    */
   nir_xfb_info xfb_info;
   nir_xfb_output_info xfb_outputs[64];
};

/*
 * Hash table keys for fast-linked shader variants. These contain the entire
 * prolog/epilog key so we only do 1 hash table lookup instead of 2 in the
 * general case where the linked shader is already ready.
 */
struct hk_fast_link_key_vs {
   struct agx_vs_prolog_key prolog;
};

struct hk_fast_link_key_fs {
   unsigned nr_samples_shaded;
   struct agx_fs_prolog_key prolog;
   struct agx_fs_epilog_key epilog;
};

struct hk_shader {
   struct agx_shader_part b;

   struct hk_shader_info info;
   struct agx_fragment_face_2_packed frag_face;
   struct agx_counts_packed counts;

   const void *code_ptr;
   uint32_t code_size;

   const void *data_ptr;
   uint32_t data_size;

   /* BO for any uploaded shader part */
   struct agx_bo *bo;

   /* Cache of fast linked variants */
   struct {
      simple_mtx_t lock;
      struct hash_table *ht;
   } linked;

   /* If there's only a single possibly linked variant, direct pointer. TODO:
    * Union with the cache to save some space?
    */
   struct hk_linked_shader *only_linked;

   /* Address to the uploaded preamble section. Preambles are uploaded
    * separately from fast-linked main shaders.
    */
   uint64_t preamble_addr;

   /* Address of the start of the shader data section */
   uint64_t data_addr;
};

enum hk_vs_variant {
   /* Hardware vertex shader, when next stage is fragment */
   HK_VS_VARIANT_HW,

   /* Hardware compute shader, when next is geometry/tessellation */
   HK_VS_VARIANT_SW,

   HK_VS_VARIANTS,
};

enum hk_gs_variant {
   /* Hardware vertex shader used for rasterization */
   HK_GS_VARIANT_RAST,

   /* Main compute shader */
   HK_GS_VARIANT_MAIN,
   HK_GS_VARIANT_MAIN_NO_RAST,

   /* Count compute shader */
   HK_GS_VARIANT_COUNT,
   HK_GS_VARIANT_COUNT_NO_RAST,

   /* Pre-GS compute shader */
   HK_GS_VARIANT_PRE,
   HK_GS_VARIANT_PRE_NO_RAST,

   HK_GS_VARIANTS,
};

/* clang-format off */
static const char *hk_gs_variant_name[] = {
   [HK_GS_VARIANT_RAST] = "Rasterization",
   [HK_GS_VARIANT_MAIN] = "Main",
   [HK_GS_VARIANT_MAIN_NO_RAST] = "Main (rast. discard)",
   [HK_GS_VARIANT_COUNT] = "Count",
   [HK_GS_VARIANT_COUNT_NO_RAST] = "Count (rast. discard)",
   [HK_GS_VARIANT_PRE] = "Pre-GS",
   [HK_GS_VARIANT_PRE_NO_RAST] = "Pre-GS (rast. discard)",
};
/* clang-format on */

static inline unsigned
hk_num_variants(gl_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      return HK_VS_VARIANTS;

   case MESA_SHADER_GEOMETRY:
      return HK_GS_VARIANTS;

   default:
      return 1;
   }
}

/*
 * An hk_api shader maps 1:1 to a VkShader object. An hk_api_shader may contain
 * multiple hardware hk_shader's, built at shader compile time. This complexity
 * is required to efficiently implement the legacy geometry pipeline.
 */
struct hk_api_shader {
   struct vk_shader vk;

   /* Is this an internal passthrough geometry shader? */
   bool is_passthrough;

   struct hk_shader variants[];
};

#define hk_foreach_variant(api_shader, var)                                    \
   for (struct hk_shader *var = api_shader->variants;                          \
        var < api_shader->variants + hk_num_variants(api_shader->vk.stage);    \
        ++var)

static const char *
hk_variant_name(struct hk_api_shader *obj, struct hk_shader *variant)
{
   unsigned i = variant - obj->variants;
   assert(i < hk_num_variants(obj->vk.stage));

   if (hk_num_variants(obj->vk.stage) == 1) {
      return NULL;
   } else if (obj->vk.stage == MESA_SHADER_GEOMETRY) {
      assert(i < ARRAY_SIZE(hk_gs_variant_name));
      return hk_gs_variant_name[i];
   } else {
      assert(i < 2);
      return i == HK_VS_VARIANT_SW ? "Software" : "Hardware";
   }
}

static struct hk_shader *
hk_only_variant(struct hk_api_shader *obj)
{
   if (!obj)
      return NULL;

   assert(hk_num_variants(obj->vk.stage) == 1);
   return &obj->variants[0];
}

static struct hk_shader *
hk_any_variant(struct hk_api_shader *obj)
{
   if (!obj)
      return NULL;

   return &obj->variants[0];
}

static struct hk_shader *
hk_main_gs_variant(struct hk_api_shader *obj, bool rast_disc)
{
   return &obj->variants[HK_GS_VARIANT_MAIN + rast_disc];
}

static struct hk_shader *
hk_count_gs_variant(struct hk_api_shader *obj, bool rast_disc)
{
   return &obj->variants[HK_GS_VARIANT_COUNT + rast_disc];
}

static struct hk_shader *
hk_pre_gs_variant(struct hk_api_shader *obj, bool rast_disc)
{
   return &obj->variants[HK_GS_VARIANT_PRE + rast_disc];
}

#define HK_MAX_LINKED_USC_SIZE                                                 \
   (AGX_USC_PRESHADER_LENGTH + AGX_USC_FRAGMENT_PROPERTIES_LENGTH +            \
    AGX_USC_REGISTERS_LENGTH + AGX_USC_SHADER_LENGTH + AGX_USC_SHARED_LENGTH + \
    AGX_USC_SAMPLER_LENGTH + (AGX_USC_UNIFORM_LENGTH * 9))

struct hk_linked_shader {
   struct agx_linked_shader b;

   /* Distinct from hk_shader::counts due to addition of cf_binding_count, which
    * is delayed since it depends on cull distance.
    */
   struct agx_fragment_shader_word_0_packed fs_counts;

   /* Baked USC words to bind this linked shader */
   struct {
      uint8_t data[HK_MAX_LINKED_USC_SIZE];
      size_t size;
   } usc;
};

struct hk_linked_shader *hk_fast_link(struct hk_device *dev, bool fragment,
                                      struct hk_shader *main,
                                      struct agx_shader_part *prolog,
                                      struct agx_shader_part *epilog,
                                      unsigned nr_samples_shaded);

extern const struct vk_device_shader_ops hk_device_shader_ops;

uint64_t
hk_physical_device_compiler_flags(const struct hk_physical_device *pdev);

static inline nir_address_format
hk_buffer_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return nir_address_format_64bit_bounded_global;
   default:
      unreachable("Invalid robust buffer access behavior");
   }
}

bool hk_lower_uvs_index(nir_shader *s, unsigned vs_uniform_base);

bool
hk_nir_lower_descriptors(nir_shader *nir,
                         const struct vk_pipeline_robustness_state *rs,
                         uint32_t set_layout_count,
                         struct vk_descriptor_set_layout *const *set_layouts);
void hk_lower_nir(struct hk_device *dev, nir_shader *nir,
                  const struct vk_pipeline_robustness_state *rs,
                  bool is_multiview, uint32_t set_layout_count,
                  struct vk_descriptor_set_layout *const *set_layouts);

VkResult hk_compile_shader(struct hk_device *dev,
                           struct vk_shader_compile_info *info,
                           const struct vk_graphics_pipeline_state *state,
                           const VkAllocationCallbacks *pAllocator,
                           struct hk_api_shader **shader_out);

void hk_preprocess_nir_internal(struct vk_physical_device *vk_pdev,
                                nir_shader *nir);

void hk_api_shader_destroy(struct vk_device *vk_dev,
                           struct vk_shader *vk_shader,
                           const VkAllocationCallbacks *pAllocator);

const nir_shader_compiler_options *
hk_get_nir_options(struct vk_physical_device *vk_pdev, gl_shader_stage stage,
                   UNUSED const struct vk_pipeline_robustness_state *rs);

struct hk_api_shader *hk_meta_shader(struct hk_device *dev,
                                     hk_internal_builder_t builder, void *data,
                                     size_t data_size);

static inline struct hk_shader *
hk_meta_kernel(struct hk_device *dev, hk_internal_builder_t builder, void *data,
               size_t data_size)
{
   return hk_only_variant(hk_meta_shader(dev, builder, data, data_size));
}

struct hk_passthrough_gs_key {
   /* Bit mask of outputs written by the VS/TES, to be passed through */
   uint64_t outputs;

   /* Clip/cull sizes, implies clip/cull written in output */
   uint8_t clip_distance_array_size;
   uint8_t cull_distance_array_size;

   /* Transform feedback buffer strides */
   uint8_t xfb_stride[MAX_XFB_BUFFERS];

   /* Decomposed primitive */
   enum mesa_prim prim;

   /* Transform feedback info. Must add nir_xfb_info_size to get the key size */
   nir_xfb_info xfb_info;
};

void hk_nir_passthrough_gs(struct nir_builder *b, const void *key_);
