/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef AC_SHADER_ABI_H
#define AC_SHADER_ABI_H

#include "ac_shader_args.h"
#include "compiler/shader_enums.h"
#include <llvm-c/Core.h>

#include <assert.h>

#define AC_LLVM_MAX_OUTPUTS (VARYING_SLOT_VAR31 + 1)

#define AC_MAX_INLINE_PUSH_CONSTS 8

enum ac_descriptor_type
{
   AC_DESC_IMAGE,
   AC_DESC_FMASK,
   AC_DESC_SAMPLER,
   AC_DESC_BUFFER,
   AC_DESC_PLANE_0,
   AC_DESC_PLANE_1,
   AC_DESC_PLANE_2,
};

/* Document the shader ABI during compilation. This is what allows radeonsi and
 * radv to share a compiler backend.
 */
struct ac_shader_abi {
   LLVMValueRef outputs[AC_LLVM_MAX_OUTPUTS * 4];

   /* These input registers sometimes need to be fixed up. */
   LLVMValueRef vertex_id;
   LLVMValueRef instance_id;
   LLVMValueRef persp_centroid, linear_centroid;
   LLVMValueRef color0, color1;
   LLVMValueRef user_data;

   /* For VS and PS: pre-loaded shader inputs.
    *
    * Currently only used for NIR shaders; indexed by variables'
    * driver_location.
    */
   LLVMValueRef *inputs;

   /* Varying -> attribute number mapping. Also NIR-only */
   unsigned fs_input_attr_indices[MAX_VARYING];

   void (*emit_outputs)(struct ac_shader_abi *abi, unsigned max_outputs, LLVMValueRef *addrs);

   void (*emit_vertex)(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef *addrs);

   void (*emit_primitive)(struct ac_shader_abi *abi, unsigned stream);

   void (*emit_vertex_with_counter)(struct ac_shader_abi *abi, unsigned stream,
                                    LLVMValueRef vertexidx, LLVMValueRef *addrs);

   LLVMValueRef (*load_inputs)(struct ac_shader_abi *abi,
                               unsigned driver_location, unsigned component,
                               unsigned num_components, unsigned vertex_index,
                               LLVMTypeRef type);

   LLVMValueRef (*load_tess_varyings)(struct ac_shader_abi *abi, LLVMTypeRef type,
                                      LLVMValueRef vertex_index, LLVMValueRef param_index,
                                      unsigned driver_location, unsigned component,
                                      unsigned num_components,
                                      bool load_inputs);

   void (*store_tcs_outputs)(struct ac_shader_abi *abi,
                             LLVMValueRef vertex_index, LLVMValueRef param_index,
                             LLVMValueRef src, unsigned writemask,
                             unsigned component, unsigned location, unsigned driver_location);

   LLVMValueRef (*load_tess_coord)(struct ac_shader_abi *abi);

   LLVMValueRef (*load_patch_vertices_in)(struct ac_shader_abi *abi);

   LLVMValueRef (*load_tess_level)(struct ac_shader_abi *abi, unsigned varying_id,
                                   bool load_default_state);

   LLVMValueRef (*load_ubo)(struct ac_shader_abi *abi,
                            unsigned desc_set, unsigned binding,
                            bool valid_binding, LLVMValueRef index);

   /**
    * Load the descriptor for the given buffer.
    *
    * \param buffer the buffer as presented in NIR: this is the descriptor
    *               in Vulkan, and the buffer index in OpenGL/Gallium
    * \param write whether buffer contents will be written
    */
   LLVMValueRef (*load_ssbo)(struct ac_shader_abi *abi, LLVMValueRef buffer, bool write);

   /**
    * Load a descriptor associated to a sampler.
    *
    * \param descriptor_set the descriptor set index (only for Vulkan)
    * \param base_index the base index of the sampler variable
    * \param constant_index constant part of an array index (or 0, if the
    *                       sampler variable is not an array)
    * \param index non-constant part of an array index (may be NULL)
    * \param desc_type the type of descriptor to load
    * \param image whether the descriptor is loaded for an image operation
    */
   LLVMValueRef (*load_sampler_desc)(struct ac_shader_abi *abi, unsigned descriptor_set,
                                     unsigned base_index, unsigned constant_index,
                                     LLVMValueRef index, enum ac_descriptor_type desc_type,
                                     bool image, bool write, bool bindless);

   /**
    * Load a Vulkan-specific resource.
    *
    * \param index resource index
    * \param desc_set descriptor set
    * \param binding descriptor set binding
    */
   LLVMValueRef (*load_resource)(struct ac_shader_abi *abi, LLVMValueRef index, unsigned desc_set,
                                 unsigned binding);

   LLVMValueRef (*load_sample_position)(struct ac_shader_abi *abi, LLVMValueRef sample_id);

   LLVMValueRef (*load_local_group_size)(struct ac_shader_abi *abi);

   LLVMValueRef (*load_sample_mask_in)(struct ac_shader_abi *abi);

   LLVMValueRef (*load_base_vertex)(struct ac_shader_abi *abi);

   LLVMValueRef (*emit_fbfetch)(struct ac_shader_abi *abi);

   /* Whether to clamp the shadow reference value to [0,1]on GFX8. Radeonsi currently
    * uses it due to promoting D16 to D32, but radv needs it off. */
   bool clamp_shadow_reference;
   bool interp_at_sample_force_center;

   /* Whether bounds checks are required */
   bool robust_buffer_access;

   /* Check for Inf interpolation coeff */
   bool kill_ps_if_inf_interp;

   /* Whether undef values must be converted to zero */
   bool convert_undef_to_zero;

   /* Clamp div by 0 (so it won't produce NaN) */
   bool clamp_div_by_zero;
};

#endif /* AC_SHADER_ABI_H */
