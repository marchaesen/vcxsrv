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

enum agx_robustness_level {
   /* No robustness */
   AGX_ROBUSTNESS_DISABLED,

   /* Invalid load/store must not fault, but undefined value/effect */
   AGX_ROBUSTNESS_GLES,

   /* Invalid load/store access something from the array (or 0) */
   AGX_ROBUSTNESS_GL,

   /* Invalid loads return 0 and invalid stores are dropped */
   AGX_ROBUSTNESS_D3D,
};

struct agx_robustness {
   enum agx_robustness_level level;

   /* Whether hardware "soft fault" is enabled. */
   bool soft_fault;
};

bool agx_nir_lower_vbo(nir_shader *shader, struct agx_attribute *attribs,
                       struct agx_robustness rs);

bool agx_vbo_supports_format(enum pipe_format format);

#ifdef __cplusplus
} /* extern C */
#endif
