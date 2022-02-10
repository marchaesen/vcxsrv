/**************************************************************************
 * 
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef TGSI_SCAN_H
#define TGSI_SCAN_H


#include "pipe/p_compiler.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shader summary info
 */
struct tgsi_shader_info
{
   uint num_tokens;

   ubyte num_inputs;
   ubyte num_outputs;
   ubyte input_semantic_name[PIPE_MAX_SHADER_INPUTS]; /**< TGSI_SEMANTIC_x */
   ubyte input_semantic_index[PIPE_MAX_SHADER_INPUTS];
   ubyte input_interpolate[PIPE_MAX_SHADER_INPUTS];
   ubyte input_interpolate_loc[PIPE_MAX_SHADER_INPUTS];
   ubyte input_usage_mask[PIPE_MAX_SHADER_INPUTS];
   ubyte output_semantic_name[PIPE_MAX_SHADER_OUTPUTS]; /**< TGSI_SEMANTIC_x */
   ubyte output_semantic_index[PIPE_MAX_SHADER_OUTPUTS];
   ubyte output_usagemask[PIPE_MAX_SHADER_OUTPUTS];
   ubyte output_streams[PIPE_MAX_SHADER_OUTPUTS];

   ubyte num_system_values;
   ubyte system_value_semantic_name[PIPE_MAX_SHADER_INPUTS];

   ubyte processor;

   uint file_mask[TGSI_FILE_COUNT];  /**< bitmask of declared registers */
   uint file_count[TGSI_FILE_COUNT];  /**< number of declared registers */
   int file_max[TGSI_FILE_COUNT];  /**< highest index of declared registers */
   int const_file_max[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned const_buffers_declared; /**< bitmask of declared const buffers */
   unsigned samplers_declared; /**< bitmask of declared samplers */
   ubyte sampler_targets[PIPE_MAX_SHADER_SAMPLER_VIEWS];  /**< TGSI_TEXTURE_x values */
   ubyte sampler_type[PIPE_MAX_SHADER_SAMPLER_VIEWS]; /**< TGSI_RETURN_TYPE_x */
   ubyte num_stream_output_components[4];

   ubyte input_array_first[PIPE_MAX_SHADER_INPUTS];
   ubyte input_array_last[PIPE_MAX_SHADER_INPUTS];
   ubyte output_array_first[PIPE_MAX_SHADER_OUTPUTS];
   ubyte output_array_last[PIPE_MAX_SHADER_OUTPUTS];
   unsigned array_max[TGSI_FILE_COUNT];  /**< highest index array per register file */

   uint immediate_count; /**< number of immediates declared */
   uint num_instructions;
   uint num_memory_instructions; /**< sampler, buffer, and image instructions */

   uint opcode_count[TGSI_OPCODE_LAST];  /**< opcode histogram */

   /**
    * If a tessellation control shader reads outputs, this describes which ones.
    */
   boolean reads_pervertex_outputs;
   boolean reads_perpatch_outputs;
   boolean reads_tessfactor_outputs;

   ubyte colors_read; /**< which color components are read by the FS */
   ubyte colors_written;
   boolean reads_position; /**< does fragment shader read position? */
   boolean reads_z; /**< does fragment shader read depth? */
   boolean reads_samplemask; /**< does fragment shader read sample mask? */
   boolean reads_tess_factors; /**< If TES reads TESSINNER or TESSOUTER */
   boolean writes_z;  /**< does fragment shader write Z value? */
   boolean writes_stencil; /**< does fragment shader write stencil value? */
   boolean writes_samplemask; /**< does fragment shader write sample mask? */
   boolean writes_edgeflag; /**< vertex shader outputs edgeflag */
   boolean uses_kill;  /**< KILL or KILL_IF instruction used? */
   boolean uses_persp_center;
   boolean uses_persp_centroid;
   boolean uses_persp_sample;
   boolean uses_linear_center;
   boolean uses_linear_centroid;
   boolean uses_linear_sample;
   boolean uses_persp_opcode_interp_centroid;
   boolean uses_persp_opcode_interp_offset;
   boolean uses_persp_opcode_interp_sample;
   boolean uses_linear_opcode_interp_centroid;
   boolean uses_linear_opcode_interp_offset;
   boolean uses_linear_opcode_interp_sample;
   boolean uses_instanceid;
   boolean uses_vertexid;
   boolean uses_vertexid_nobase;
   boolean uses_basevertex;
   boolean uses_drawid;
   boolean uses_primid;
   boolean uses_frontface;
   boolean uses_invocationid;
   boolean uses_thread_id[3];
   boolean uses_block_id[3];
   boolean uses_block_size;
   boolean uses_grid_size;
   boolean uses_subgroup_info;
   boolean writes_position;
   boolean writes_psize;
   boolean writes_clipvertex;
   boolean writes_primid;
   boolean writes_viewport_index;
   boolean writes_layer;
   boolean writes_memory; /**< contains stores or atomics to buffers or images */
   boolean uses_doubles; /**< uses any of the double instructions */
   boolean uses_derivatives;
   boolean uses_bindless_samplers;
   boolean uses_bindless_images;
   boolean uses_fbfetch;
   unsigned clipdist_writemask;
   unsigned culldist_writemask;
   unsigned num_written_culldistance;
   unsigned num_written_clipdistance;

   unsigned images_declared; /**< bitmask of declared images */
   unsigned msaa_images_declared; /**< bitmask of declared MSAA images */

   /**
    * Bitmask indicating which declared image is a buffer.
    */
   unsigned images_buffers;
   unsigned images_load; /**< bitmask of images using loads */
   unsigned images_store; /**< bitmask of images using stores */
   unsigned images_atomic; /**< bitmask of images using atomics */
   unsigned shader_buffers_declared; /**< bitmask of declared shader buffers */
   unsigned shader_buffers_load; /**< bitmask of shader buffers using loads */
   unsigned shader_buffers_store; /**< bitmask of shader buffers using stores */
   unsigned shader_buffers_atomic; /**< bitmask of shader buffers using atomics */
   bool uses_bindless_buffer_load;
   bool uses_bindless_buffer_store;
   bool uses_bindless_buffer_atomic;
   bool uses_bindless_image_load;
   bool uses_bindless_image_store;
   bool uses_bindless_image_atomic;

   unsigned hw_atomic_declared; /**< bitmask of declared atomic_counter */
   /**
    * Bitmask indicating which register files are accessed with
    * indirect addressing.  The bits are (1 << TGSI_FILE_x), etc.
    */
   unsigned indirect_files;
   /**
    * Bitmask indicating which register files are read / written with
    * indirect addressing.  The bits are (1 << TGSI_FILE_x).
    */
   unsigned indirect_files_read;
   unsigned indirect_files_written;
   unsigned dim_indirect_files; /**< shader resource indexing */
   unsigned const_buffers_indirect; /**< const buffers using indirect addressing */

   unsigned properties[TGSI_PROPERTY_COUNT]; /* index with TGSI_PROPERTY_ */

   /**
    * Max nesting limit of loops/if's
    */
   unsigned max_depth;
};

struct tgsi_array_info
{
   /** Whether an array with this ID was declared. */
   bool declared;

   /** The OR of all writemasks used to write to this array. */
   ubyte writemask;

   /** The range with which the array was declared. */
   struct tgsi_declaration_range range;
};

struct tgsi_tessctrl_info
{
   /** Whether all codepaths write tess factors in all invocations. */
   bool tessfactors_are_def_in_all_invocs;
};

extern void
tgsi_scan_shader(const struct tgsi_token *tokens,
                 struct tgsi_shader_info *info);

void
tgsi_scan_arrays(const struct tgsi_token *tokens,
                 unsigned file,
                 unsigned max_array_id,
                 struct tgsi_array_info *arrays);

void
tgsi_scan_tess_ctrl(const struct tgsi_token *tokens,
                    const struct tgsi_shader_info *info,
                    struct tgsi_tessctrl_info *out);

static inline bool
tgsi_is_bindless_image_file(unsigned file)
{
   return file != TGSI_FILE_IMAGE &&
          file != TGSI_FILE_MEMORY &&
          file != TGSI_FILE_BUFFER &&
          file != TGSI_FILE_CONSTBUF &&
          file != TGSI_FILE_HW_ATOMIC;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* TGSI_SCAN_H */
