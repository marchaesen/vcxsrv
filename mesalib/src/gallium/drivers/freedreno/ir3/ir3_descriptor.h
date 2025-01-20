/*
 * Copyright © 2022 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef IR3_DESCRIPTOR_H_
#define IR3_DESCRIPTOR_H_

#include "ir3/ir3_shader.h"

/*
 * When using bindless descriptor sets for image/SSBO (and fb-read) state,
 * since the descriptor sets are large, layout the descriptor set with the
 * first IR3_BINDLESS_SSBO_COUNT slots for SSBOs followed by
 * IR3_BINDLESS_IMAGE_COUNT slots for images.  (For fragment shaders, the
 * last image slot is reserved for fb-read tex descriptor.)
 *
 * Note that these limits are more or less arbitrary.  But the enable_mask
 * in fd_shaderbuf_stateobj / fd_shaderimg_stateobj would need to be more
 * than uint32_t to support more than 32.
 */

#define IR3_BINDLESS_SSBO_OFFSET  0
#define IR3_BINDLESS_SSBO_COUNT   32
#define IR3_BINDLESS_IMAGE_OFFSET IR3_BINDLESS_SSBO_COUNT
#define IR3_BINDLESS_IMAGE_COUNT  32
#define IR3_BINDLESS_DESC_COUNT   (IR3_BINDLESS_IMAGE_OFFSET + IR3_BINDLESS_IMAGE_COUNT)

/**
 * When using bindless descriptor sets for IBO/etc, each shader stage gets
 * it's own descriptor set, avoiding the need to merge image/ssbo state
 * across shader stages.
 */
static inline unsigned
ir3_shader_descriptor_set(enum pipe_shader_type shader)
{
   switch (shader) {
   case PIPE_SHADER_VERTEX: return 0;
   case PIPE_SHADER_TESS_CTRL: return 1;
   case PIPE_SHADER_TESS_EVAL: return 2;
   case PIPE_SHADER_GEOMETRY:  return 3;
   case PIPE_SHADER_FRAGMENT:  return 4;
   case PIPE_SHADER_COMPUTE:   return 0;
   case MESA_SHADER_KERNEL:    return 0;
   default:
      unreachable("bad shader stage");
      return ~0;
   }
}

bool ir3_nir_lower_io_to_bindless(nir_shader *shader);

#endif /* IR3_DESCRIPTOR_H_ */
