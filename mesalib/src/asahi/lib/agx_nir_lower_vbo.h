/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "util/format/u_formats.h"
#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_MAX_ATTRIBS (16)
#define AGX_MAX_VBUFS   (16)

/* See pipe_vertex_element for justification on the sizes. This structure should
 * be small so it can be embedded into a shader key.
 */
struct agx_attribute {
   /* If instanced, Zero means all get the same value (Vulkan semantics). */
   uint32_t divisor;
   uint32_t stride;
   uint16_t src_offset;

   /* pipe_format, all vertex formats should be <= 255 */
   uint8_t format;

   unsigned buf   : 7;
   bool instanced : 1;
};

bool agx_nir_lower_vbo(nir_shader *shader, struct agx_attribute *attribs);
bool agx_vbo_supports_format(enum pipe_format format);

#ifdef __cplusplus
} /* extern C */
#endif
