/*
 * Copyright © 2022 Collabora, Ltd
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

#ifndef VK_GRAPHICS_STATE_H
#define VK_GRAPHICS_STATE_H

#include "vulkan/vulkan_core.h"

#include "vk_limits.h"

#include "util/bitset.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_command_buffer;
struct vk_device;

/** Enumeration of all Vulkan dynamic graphics states
 *
 * Enumerants are named with both the abreviation of the state group to which
 * the state belongs as well as the name of the state itself.  These are
 * intended to pretty closely match the VkDynamicState enum but may not match
 * perfectly all the time.
 */
enum mesa_vk_dynamic_graphics_state {
   MESA_VK_DYNAMIC_VI,
   MESA_VK_DYNAMIC_VI_BINDING_STRIDES,
   MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY,
   MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE,
   MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS,
   MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT,
   MESA_VK_DYNAMIC_VP_VIEWPORTS,
   MESA_VK_DYNAMIC_VP_SCISSOR_COUNT,
   MESA_VK_DYNAMIC_VP_SCISSORS,
   MESA_VK_DYNAMIC_DR_RECTANGLES,
   MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE,
   MESA_VK_DYNAMIC_RS_CULL_MODE,
   MESA_VK_DYNAMIC_RS_FRONT_FACE,
   MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE,
   MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS,
   MESA_VK_DYNAMIC_RS_LINE_WIDTH,
   MESA_VK_DYNAMIC_RS_LINE_STIPPLE,
   MESA_VK_DYNAMIC_FSR,
   MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS,
   MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE,
   MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP,
   MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS,
   MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE,
   MESA_VK_DYNAMIC_DS_STENCIL_OP,
   MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK,
   MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK,
   MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE,
   MESA_VK_DYNAMIC_CB_LOGIC_OP,
   MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES,
   MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS,

   /* Must be left at the end */
   MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX,
};

/** Populate a bitset with dynamic states
 *
 * This function maps a VkPipelineDynamicStateCreateInfo to a bitset indexed
 * by mesa_vk_dynamic_graphics_state enumerants.
 *
 * @param[out] dynamic  Bitset to populate
 * @param[in]  info     VkPipelineDynamicStateCreateInfo or NULL
 */
void
vk_get_dynamic_graphics_states(BITSET_WORD *dynamic,
                               const VkPipelineDynamicStateCreateInfo *info);

struct vk_vertex_binding_state {
   /** VkVertexInputBindingDescription::stride */
   uint16_t stride;

   /** VkVertexInputBindingDescription::inputRate */
   uint16_t input_rate;

   /** VkVertexInputBindingDivisorDescriptionEXT::divisor or 1 */
   uint32_t divisor;
};

struct vk_vertex_attribute_state {
   /** VkVertexInputAttributeDescription::binding */
   uint32_t binding;

   /** VkVertexInputAttributeDescription::format */
   VkFormat format;

   /** VkVertexInputAttributeDescription::offset */
   uint32_t offset;
};

struct vk_vertex_input_state {
   /** Bitset of which bindings are valid, indexed by binding */
   uint32_t bindings_valid;
   struct vk_vertex_binding_state bindings[MESA_VK_MAX_VERTEX_BINDINGS];

   /** Bitset of which attributes are valid, indexed by location */
   uint32_t attributes_valid;
   struct vk_vertex_attribute_state attributes[MESA_VK_MAX_VERTEX_ATTRIBUTES];
};

struct vk_input_assembly_state {
   /** VkPipelineInputAssemblyStateCreateInfo::topology
     *
     * MESA_VK_DYNAMIC_GRAPHICS_STATE_IA_PRIMITIVE_TOPOLOGY
     */
   uint8_t primitive_topology;

   /** VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable
     *
     * MESA_VK_DYNAMIC_GRAPHICS_STATE_IA_PRIMITIVE_RESTART_ENABLE
     */
   bool primitive_restart_enable;
};

struct vk_tessellation_state {
   /** VkPipelineTessellationStateCreateInfo::patchControlPoints */
   uint8_t patch_control_points;

   /** VkPipelineTessellationDomainOriginStateCreateInfo::domainOrigin */
   uint8_t domain_origin;
};

struct vk_viewport_state {
   /** VkPipelineViewportDepthClipControlCreateInfoEXT::negativeOneToOne */
   bool negative_one_to_one;

   /** VkPipelineViewportStateCreateInfo::viewportCount */
   uint8_t viewport_count;

   /** VkPipelineViewportStateCreateInfo::scissorCount */
   uint8_t scissor_count;

   /** VkPipelineViewportStateCreateInfo::pViewports */
   VkRect2D scissors[MESA_VK_MAX_SCISSORS];

   /** VkPipelineViewportStateCreateInfo::pScissors */
   VkViewport viewports[MESA_VK_MAX_VIEWPORTS];
};

struct vk_discard_rectangles_state {
   /** VkPipelineDiscardRectangleStateCreateInfoEXT::discardRectangleMode */
   VkDiscardRectangleModeEXT mode;

   /** VkPipelineDiscardRectangleStateCreateInfoEXT::discardRectangleCount */
   uint32_t rectangle_count;

   /** VkPipelineDiscardRectangleStateCreateInfoEXT::pDiscardRectangles */
   VkRect2D rectangles[MESA_VK_MAX_DISCARD_RECTANGLES];
};

struct vk_rasterization_state {
   /** VkPipelineRasterizationStateCreateInfo::rasterizerDiscardEnable
    *
    * This will be false if rasterizer discard is dynamic
    */
   bool rasterizer_discard_enable;

   /** VkPipelineRasterizationStateCreateInfo::depthClampEnable */
   bool depth_clamp_enable;

   /** VkPipelineRasterizationDepthClipStateCreateInfoEXT::depthClipEnable */
   bool depth_clip_enable;

   /** VkPipelineRasterizationStateCreateInfo::polygonMode */
   VkPolygonMode polygon_mode;

   /** VkPipelineRasterizationStateCreateInfo::cullMode */
   VkCullModeFlags cull_mode;

   /** VkPipelineRasterizationStateCreateInfo::frontFace */
   VkFrontFace front_face;

   /** VkPipelineRasterizationConservativeStateCreateInfoEXT::conservativeRasterizationMode */
   VkConservativeRasterizationModeEXT conservative_mode;

   /** VkPipelineRasterizationStateRasterizationOrderAMD::rasterizationOrder */
   VkRasterizationOrderAMD rasterization_order_amd;

   /** VkPipelineRasterizationProvokingVertexStateCreateInfoEXT::provokingVertexMode */
   VkProvokingVertexModeEXT provoking_vertex;

   /** VkPipelineRasterizationStateStreamCreateInfoEXT::rasterizationStream */
   uint32_t rasterization_stream;

   struct {
      /** VkPipelineRasterizationStateCreateInfo::depthBiasEnable */
      bool enable;

      /** VkPipelineRasterizationStateCreateInfo::depthBiasConstantFactor */
      float constant;

      /** VkPipelineRasterizationStateCreateInfo::depthBiasClamp */
      float clamp;

      /** VkPipelineRasterizationStateCreateInfo::depthBiasSlopeFactor */
      float slope;
   } depth_bias;

   struct {
      /** VkPipelineRasterizationStateCreateInfo::lineWidth */
      float width;

      /** VkPipelineRasterizationLineStateCreateInfoEXT::lineRasterizationMode
       *
       * Will be set to VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT if
       * VkPipelineRasterizationLineStateCreateInfoEXT is not provided.
       */
      VkLineRasterizationModeEXT mode;

      struct {
         /** VkPipelineRasterizationLineStateCreateInfoEXT::stippledLineEnable */
         bool enable;

         /** VkPipelineRasterizationLineStateCreateInfoEXT::lineStippleFactor */
         uint32_t factor;

         /** VkPipelineRasterizationLineStateCreateInfoEXT::lineStipplePattern */
         uint16_t pattern;
      } stipple;
   } line;
};

struct vk_fragment_shading_rate_state {
   /** VkPipelineFragmentShadingRateStateCreateInfoKHR::fragmentSize
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_FSR
    */
   VkExtent2D fragment_size;

   /** VkPipelineFragmentShadingRateStateCreateInfoKHR::combinerOps
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_FSR
    */
   VkFragmentShadingRateCombinerOpKHR combiner_ops[2];
};

struct vk_sample_locations_state {
   /** VkSampleLocationsInfoEXT::sampleLocationsPerPixel */
   VkSampleCountFlagBits per_pixel;

   /** VkSampleLocationsInfoEXT::sampleLocationGridSize */
   VkExtent2D grid_size;

   /** VkSampleLocationsInfoEXT::sampleLocations */
   VkSampleLocationEXT locations[MESA_VK_MAX_SAMPLE_LOCATIONS];
};

struct vk_multisample_state {
   /** VkPipelineMultisampleStateCreateInfo::rasterizationSamples */
   VkSampleCountFlagBits rasterization_samples;

   /** VkPipelineMultisampleStateCreateInfo::sampleShadingEnable */
   bool sample_shading_enable;

   /** VkPipelineMultisampleStateCreateInfo::minSampleShading */
   float min_sample_shading;

   /** VkPipelineMultisampleStateCreateInfo::pSampleMask */
   uint16_t sample_mask;

   /** VkPipelineMultisampleStateCreateInfo::alphaToCoverageEnable */
   bool alpha_to_coverage_enable;

   /** VkPipelineMultisampleStateCreateInfo::alphaToOneEnable */
   bool alpha_to_one_enable;

   /** VkPipelineSampleLocationsStateCreateInfoEXT::sampleLocationsEnable */
   bool sample_locations_enable;

   /** VkPipelineSampleLocationsStateCreateInfoEXT::sampleLocationsInfo
    *
    * May be NULL for dynamic sample locations.
    */
   const struct vk_sample_locations_state *sample_locations;
};

/** Represents the stencil test state for a face */
struct vk_stencil_test_face_state {
   /*
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_STENCIL_OP
    */
   struct {
      /** VkStencilOpState::failOp */
      uint8_t fail;

      /** VkStencilOpState::passOp */
      uint8_t pass;

      /** VkStencilOpState::depthFailOp */
      uint8_t depth_fail;

      /** VkStencilOpState::compareOp */
      uint8_t compare;
   } op;

   /** VkStencilOpState::compareMask
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_STENCIL_COMPARE_MASK
    */
   uint8_t compare_mask;

   /** VkStencilOpState::writeMask
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_STENCIL_WRITE_MASK
    */
   uint8_t write_mask;

   /** VkStencilOpState::reference
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_STENCIL_REFERENCE
    */
   uint8_t reference;
};

struct vk_depth_stencil_state {
   struct {
      /** VkPipelineDepthStencilStateCreateInfo::depthTestEnable
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_DEPTH_TEST_ENABLE
       */
      bool test_enable;

      /** VkPipelineDepthStencilStateCreateInfo::depthWriteEnable
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_DEPTH_WRITE_ENABLE
       */
      bool write_enable;

      /** VkPipelineDepthStencilStateCreateInfo::depthCompareOp
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_DEPTH_COMPARE_OP
       */
      VkCompareOp compare_op;

      struct {
         /** VkPipelineDepthStencilStateCreateInfo::depthBoundsTestEnable
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_DEPTH_BOUNDS_TEST_ENABLE
          */
         bool enable;

         /** VkPipelineDepthStencilStateCreateInfo::min/maxDepthBounds
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_DEPTH_BOUNDS_TEST_BOUNDS
          */
         float min, max;
      } bounds_test;
   } depth;

   struct {
      /** VkPipelineDepthStencilStateCreateInfo::stencilTestEnable
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_DS_STENCIL_TEST_ENABLE
       */
      bool test_enable;

      /** Whether or not stencil is should be written
       *
       * This does not map directly to any particular Vulkan API state and is
       * initialized to true.  If independent stencil disable ever becomes a
       * thing, it will use this state.  vk_optimize_depth_stencil_state() may
       * set this to false if it can prove that the stencil test will never
       * alter the stencil value.
       */
      bool write_enable;

      /** VkPipelineDepthStencilStateCreateInfo::front */
      struct vk_stencil_test_face_state front;

      /** VkPipelineDepthStencilStateCreateInfo::back */
      struct vk_stencil_test_face_state back;
   } stencil;
};

/** Optimize a depth/stencil state
 *
 * The way depth and stencil testing is specified, there are many case where,
 * regardless of depth/stencil writes being enabled, nothing actually gets
 * written due to some other bit of state being set.  In the presence of
 * discards, it's fairly easy to get into cases where early depth/stencil
 * testing is disabled on some hardware, leading to a fairly big performance
 * hit.  This function attempts to optimize the depth stencil state and
 * disable writes and sometimes even testing whenever possible.
 *
 * @param[inout]  ds                   The depth stencil state to optimize
 * @param[in]     ds_aspects           Which image aspects are present in the
 *                                     render pass.
 * @param[in]     consider_write_mask  If true, the write mask will be taken
 *                                     into account when optimizing.  If
 *                                     false, it will be ignored.
 */
void vk_optimize_depth_stencil_state(struct vk_depth_stencil_state *ds,
                                     VkImageAspectFlags ds_aspects,
                                     bool consider_write_mask);

struct vk_color_blend_attachment_state {
   /** VkPipelineColorBlendAttachmentState::blendEnable */
   bool blend_enable;

   /** VkPipelineColorBlendAttachmentState::srcColorBlendFactor */
   uint8_t src_color_blend_factor;

   /** VkPipelineColorBlendAttachmentState::dstColorBlendFactor */
   uint8_t dst_color_blend_factor;

   /** VkPipelineColorBlendAttachmentState::srcAlphaBlendFactor */
   uint8_t src_alpha_blend_factor;

   /** VkPipelineColorBlendAttachmentState::dstAlphaBlendFactor */
   uint8_t dst_alpha_blend_factor;

   /** VkPipelineColorBlendAttachmentState::colorWriteMask */
   uint8_t write_mask;

   /** VkPipelineColorBlendAttachmentState::colorBlendOp */
   VkBlendOp color_blend_op;

   /** VkPipelineColorBlendAttachmentState::alphaBlendOp */
   VkBlendOp alpha_blend_op;
};

struct vk_color_blend_state {
   /** VkPipelineColorBlendStateCreateInfo::logicOpEnable */
   bool logic_op_enable;

   /** VkPipelineColorBlendStateCreateInfo::logicOp */
   uint8_t logic_op;

   /** VkPipelineColorWriteCreateInfoEXT::pColorWriteEnables */
   uint8_t color_write_enables;

   /** VkPipelineColorBlendStateCreateInfo::attachmentCount */
   uint8_t attachment_count;

   /** VkPipelineColorBlendStateCreateInfo::pAttachments */
   struct vk_color_blend_attachment_state attachments[MESA_VK_MAX_COLOR_ATTACHMENTS];

   /** VkPipelineColorBlendStateCreateInfo::blendConstants */
   float blend_constants[4];
};

struct vk_render_pass_state {
   /** Set of image aspects bound as color/depth/stencil attachments
    *
    * Set to VK_IMAGE_ASPECT_METADATA_BIT to indicate that attachment info
    * is invalid.
    */
   VkImageAspectFlags attachment_aspects;

   /** VkGraphicsPipelineCreateInfo::renderPass */
   VkRenderPass render_pass;

   /** VkGraphicsPipelineCreateInfo::subpass */
   uint32_t subpass;

   /** VkPipelineRenderingCreateInfo::viewMask */
   uint32_t view_mask;

   /** VkRenderingSelfDependencyInfoMESA::colorSelfDependencies */
   uint8_t color_self_dependencies;

   /** VkRenderingSelfDependencyInfoMESA::depthSelfDependency */
   bool depth_self_dependency;

   /** VkRenderingSelfDependencyInfoMESA::stencilSelfDependency */
   bool stencil_self_dependency;

   /** VkPipelineRenderingCreateInfo::colorAttachmentCount */
   uint8_t color_attachment_count;

   /** VkPipelineRenderingCreateInfo::pColorAttachmentFormats */
   VkFormat color_attachment_formats[MESA_VK_MAX_COLOR_ATTACHMENTS];

   /** VkPipelineRenderingCreateInfo::depthAttachmentFormat */
   VkFormat depth_attachment_format;

   /** VkPipelineRenderingCreateInfo::stencilAttachmentFormat */
   VkFormat stencil_attachment_format;

   /** VkAttachmentSampleCountInfoAMD::pColorAttachmentSamples */
   uint8_t color_attachment_samples[MESA_VK_MAX_COLOR_ATTACHMENTS];

   /** VkAttachmentSampleCountInfoAMD::depthStencilAttachmentSamples */
   uint8_t depth_stencil_attachment_samples;
};

/** Struct representing all dynamic graphics state
 *
 * Before invoking any core functions, the driver must properly populate
 * initialize this struct:
 *
 *  - Initialize using vk_default_dynamic_graphics_state, if desired
 *  - Set vi to a driver-allocated vk_vertex_input_state struct
 *  - Set ms.sample_locations to a driver-allocated
 *    vk_sample_locations_state struct
 */
struct vk_dynamic_graphics_state {
   /** Vertex input state
    *
    * Must be provided by the driver if VK_EXT_vertex_input_dynamic_state is
    * supported.
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_VI
    */
   struct vk_vertex_input_state *vi;

   /** Vertex binding strides
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_VI_BINDING_STRIDES
    */
   uint16_t vi_binding_strides[MESA_VK_MAX_VERTEX_BINDINGS];

   struct vk_input_assembly_state ia;

   struct {
      uint32_t patch_control_points;
   } ts;

   /** Viewport state */
   struct {
      /** Viewport count
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_VP_VIEWPORT_COUNT
       */
      uint32_t viewport_count;

      /** Viewports
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_VP_VIEWPORTS
       */
      VkViewport viewports[MESA_VK_MAX_VIEWPORTS];

      /** Scissor count
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_VP_SCISSOR_COUNT
       */
      uint32_t scissor_count;

      /** Scissor rects
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_VP_SCISSORS
       */
      VkRect2D scissors[MESA_VK_MAX_SCISSORS];
   } vp;

   /** Discard rectangles
    *
    * MESA_VK_DYNAMIC_GRAPHICS_STATE_DR_RECTANGLES
    */
   struct {
      uint32_t rectangle_count;
      VkRect2D rectangles[MESA_VK_MAX_DISCARD_RECTANGLES];
   } dr;

   /** Rasterization state */
   struct {
      /** Rasterizer discard
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_RASTERIZER_DISCARD_ENABLE
       */
      bool rasterizer_discard_enable;

      /** Cull mode
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_CULL_MODE
       */
      VkCullModeFlags cull_mode;

      /** Front face
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_FRONT_FACE
       */
      VkFrontFace front_face;

      struct {
         /** Depth bias enable
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_DEPTH_BIAS_ENABLE
          */
         bool enable;

         /** Depth bias constant factor
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_DEPTH_BIAS_FACTORS
          */
         float constant;

         /** Depth bias clamp
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_DEPTH_BIAS_FACTORS
          */
         float clamp;

         /** Depth bias slope
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_DEPTH_BIAS_FACTORS
          */
         float slope;
      } depth_bias;

      struct {
         /** Line width
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_LINE_WIDTH
          */
         float width;

         /** Line stipple
          *
          * MESA_VK_DYNAMIC_GRAPHICS_STATE_RS_LINE_STIPPLE
          */
         struct {
            uint32_t factor;
            uint16_t pattern;
         } stipple;
      } line;
   } rs;

   struct vk_fragment_shading_rate_state fsr;

   /** Multisample state */
   struct {
      /** Sample locations
       *
       * Must be provided by the driver if VK_EXT_sample_locations is
       * supported.
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_MS_SAMPLE_LOCATIONS
       */
      struct vk_sample_locations_state *sample_locations;
   } ms;

   struct vk_depth_stencil_state ds;

   /** Color blend state */
   struct {
      /** Integer color logic op
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_CB_LOGIC_OP,
       */
      VkLogicOp logic_op;

      /** Color write enables
       *
       * Bitmask of color write enables, indexed by color attachment index.
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_CB_COLOR_WRITE_ENABLES,
       */
      uint32_t color_write_enables;

      /** Blend constants
       *
       * MESA_VK_DYNAMIC_GRAPHICS_STATE_CB_BLEND_CONSTANTS,
       */
      float blend_constants[4];
   } cb;

   /** For pipelines, which bits of dynamic state are set */
   BITSET_DECLARE(set, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX);

   /** For command buffers, which bits of dynamic state have changed */
   BITSET_DECLARE(dirty, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX);
};

struct vk_graphics_pipeline_all_state {
   struct vk_vertex_input_state vi;
   struct vk_input_assembly_state ia;
   struct vk_tessellation_state ts;
   struct vk_viewport_state vp;
   struct vk_discard_rectangles_state dr;
   struct vk_rasterization_state rs;
   struct vk_fragment_shading_rate_state fsr;
   struct vk_multisample_state ms;
   struct vk_sample_locations_state ms_sample_locations;
   struct vk_depth_stencil_state ds;
   struct vk_color_blend_state cb;
   struct vk_render_pass_state rp;
};

struct vk_graphics_pipeline_state {
   /** Bitset of which states are dynamic */
   BITSET_DECLARE(dynamic, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX);

   VkShaderStageFlags shader_stages;

   /** Vertex input state */
   const struct vk_vertex_input_state *vi;

   /** Input assembly state */
   const struct vk_input_assembly_state *ia;

   /** Tessellation state */
   const struct vk_tessellation_state *ts;

   /** Viewport state */
   const struct vk_viewport_state *vp;

   /** Discard Rectangles state */
   const struct vk_discard_rectangles_state *dr;

   /** Rasterization state */
   const struct vk_rasterization_state *rs;

   /** Fragment shading rate state */
   const struct vk_fragment_shading_rate_state *fsr;

   /** Multiesample state */
   const struct vk_multisample_state *ms;

   /** Depth stencil state */
   const struct vk_depth_stencil_state *ds;

   /** Color blend state */
   const struct vk_color_blend_state *cb;

   /** Render pass state */
   const struct vk_render_pass_state *rp;
};

/** Struct for extra information that we need from the subpass.
 *
 * This struct need only be provided if the driver has its own render pass
 * implementation.  If the driver uses the common render pass implementation,
 * we can get this information ourselves.
 */
struct vk_subpass_info {
   /** VkSubpassDescription2::viewMask */
   uint32_t view_mask;

   /**
    * Aspects of all attachments used as color or depth/stencil attachments
    * in the subpass.  Input and resolve attachments should not be considered
    * when computing the attachments aspect mask.  This is used to determine
    * whether or not depth/stencil and color blend state are required for a
    * pipeline.
    */
   VkImageAspectFlags attachment_aspects;
};

/** Populate a vk_graphics_pipeline_state from VkGraphicsPipelineCreateInfo
 *
 * This function crawls the provided VkGraphicsPipelineCreateInfo and uses it
 * to populate the vk_graphics_pipeline_state.  Upon returning from this
 * function, all pointers in `state` will either be `NULL` or point to a valid
 * sub-state structure.  Whenever an extension struct is missing, a reasonable
 * default value is provided whenever possible.  Some states may be left NULL
 * if the state does not exist (such as when rasterizer discard is enabled) or
 * if all of the corresponding states are dynamic.
 *
 * This function assumes that the vk_graphics_pipeline_state is already valid
 * (i.e., all pointers are NULL or point to valid states).  Any states already
 * present are assumed to be identical to how we would populate them from
 * VkGraphicsPipelineCreateInfo.
 *
 * This function can operate in one of two modes with respect to how the
 * memory for states is allocated.  If a `vk_graphics_pipeline_all_state`
 * struct is provided, any newly populated states will point to the relevant
 * field in `all`.  If `all == NULL`, it attempts to dynamically allocate any
 * newly required states using the provided allocator and scope.  The pointer
 * to this new blob of memory is returned via `alloc_ptr_out` and must
 * eventually be freed by the driver.
 *
 * @param[in]  device         The Vulkan device
 * @param[out] state          The graphics pipeline state to populate
 * @param[in]  info           The pCreateInfo from vkCreateGraphicsPipelines
 * @param[in]  sp_info        Subpass info if the driver implements render
 *                            passes itself.  This should be NULL for drivers
 *                            that use the common render pass infrastructure
 *                            built on top of dynamic rendering.
 * @param[in]  all            The vk_graphics_pipeline_all_state to use to
 *                            back any newly needed states.  If NULL, newly
 *                            needed states will be dynamically allocated
 *                            instead.
 * @param[in]  alloc          Allocation callbacks for dynamically allocating
 *                            new state memory.
 * @param[in]  scope          Allocation scope for dynamically allocating new
 *                            state memory.
 * @param[out] alloc_ptr_out  Will be populated with a pointer to any newly
 *                            allocated state.  The driver is responsible for
 *                            freeing this pointer.
 */
VkResult
vk_graphics_pipeline_state_fill(const struct vk_device *device,
                                struct vk_graphics_pipeline_state *state,
                                const VkGraphicsPipelineCreateInfo *info,
                                const struct vk_subpass_info *sp_info,
                                struct vk_graphics_pipeline_all_state *all,
                                const VkAllocationCallbacks *alloc,
                                VkSystemAllocationScope scope,
                                void **alloc_ptr_out);

/** Merge one vk_graphics_pipeline_state into another
 *
 * Both the destination and source states are assumed to be valid (i.e., all
 * pointers are NULL or point to valid states).  Any states which exist in
 * both are expected to be identical and the state already in dst is used.
 * The only exception here is render pass state which may be only partially
 * defined in which case the fully defined one (if any) is used.
 *
 * @param[out] dst   The destination state.  When the function returns, this
 *                   will be the union of the original dst and src.
 * @param[in]  src   The source state
 */
void
vk_graphics_pipeline_state_merge(struct vk_graphics_pipeline_state *dst,
                                 const struct vk_graphics_pipeline_state *src);

extern const struct vk_dynamic_graphics_state vk_default_dynamic_graphics_state;

/** Initialize a vk_dynamic_graphics_state with defaults
 *
 * @param[out] dyn         Dynamic graphics state to initizlie
 */
void
vk_dynamic_graphics_state_init(struct vk_dynamic_graphics_state *dyn);

/** Clear a vk_dynamic_graphics_state to defaults
 *
 * @param[out] dyn         Dynamic graphics state to initizlie
 */
void
vk_dynamic_graphics_state_clear(struct vk_dynamic_graphics_state *dyn);

/** Initialize a vk_dynamic_graphics_state for a pipeline
 *
 * @param[out] dyn         Dynamic graphics state to initizlie
 * @param[in]  supported   Bitset of all dynamic state supported by the driver.
 * @param[in]  p           The pipeline state from which to initialize the
 *                         dynamic state.
 */
void
vk_dynamic_graphics_state_fill(struct vk_dynamic_graphics_state *dyn,
                               const struct vk_graphics_pipeline_state *p);

/** Mark all states in the given vk_dynamic_graphics_state dirty
 *
 * @param[out] d  Dynamic graphics state struct
 */
static inline void
vk_dynamic_graphics_state_dirty_all(struct vk_dynamic_graphics_state *d)
{
   BITSET_SET_RANGE(d->dirty, 0, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX - 1);
}

/** Mark all states in the given vk_dynamic_graphics_state not dirty
 *
 * @param[out] d  Dynamic graphics state struct
 */
static inline void
vk_dynamic_graphics_state_clear_dirty(struct vk_dynamic_graphics_state *d)
{
   BITSET_ZERO(d->dirty);
}

/** Test if any states in the given vk_dynamic_graphics_state are dirty
 *
 * @param[in]  d  Dynamic graphics state struct to test
 * @returns       true if any state is dirty
 */
static inline bool
vk_dynamic_graphics_state_any_dirty(const struct vk_dynamic_graphics_state *d)
{
   return BITSET_TEST_RANGE(d->dirty,
      0, MESA_VK_DYNAMIC_GRAPHICS_STATE_ENUM_MAX - 1);
}

/** Copies all set state from src to dst
 *
 * Both src and dst are assumed to be properly initialized dynamic state
 * structs.  Anything not set in src, as indicated by src->set, is ignored and
 * those bits of dst are left untouched.
 *
 * @param[out] dst   Copy destination
 * @param[in]  src   Copy source
 */
void
vk_dynamic_graphics_state_copy(struct vk_dynamic_graphics_state *dst,
                               const struct vk_dynamic_graphics_state *src);

/** Set all of the state in src on a command buffer
 *
 * Anything not set, as indicated by src->set, is ignored and those states in
 * the command buffer are left untouched.
 *
 * @param[inout]  cmd   Command buffer to update
 * @param[in]     src   State to set
 */
void
vk_cmd_set_dynamic_graphics_state(struct vk_command_buffer *cmd,
                                  const struct vk_dynamic_graphics_state *src);

/** Set vertex binding strides on a command buffer
 *
 * This is the dynamic state part of vkCmdBindVertexBuffers2().
 *
 * @param[inout]  cmd            Command buffer to update
 * @param[in]     first_binding  First binding to update
 * @param[in]     binding_count  Number of bindings to update
 * @param[in]     strides        binding_count many stride values to set
 */
void
vk_cmd_set_vertex_binding_strides(struct vk_command_buffer *cmd,
                                  uint32_t first_binding,
                                  uint32_t binding_count,
                                  const VkDeviceSize *strides);

#ifdef __cplusplus
}
#endif

#endif  /* VK_GRAPHICS_STATE_H */
