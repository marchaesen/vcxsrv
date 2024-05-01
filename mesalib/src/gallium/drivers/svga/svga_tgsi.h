/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_TGSI_H
#define SVGA_TGSI_H

#include "util/compiler.h"
#include "svga3d_reg.h"


#define MAX_VGPU10_ADDR_REGS 4

struct svga_compile_key;
struct svga_context;
struct svga_shader;
struct svga_shader_variant;


/* TGSI doesn't provide use with VS input semantics (they're actually
 * pretty meaningless), so we just generate some plausible ones here.
 * This is called both from within the TGSI translator and when
 * building vdecls to ensure they match up.
 *
 * The real use of this information is matching vertex elements to
 * fragment shader inputs in the case where vertex shader is disabled.
 */
static inline void svga_generate_vdecl_semantics( unsigned idx,
                                                  unsigned *usage,
                                                  unsigned *usage_index )
{
   if (idx == 0) {
      *usage = SVGA3D_DECLUSAGE_POSITION;
      *usage_index = 0;
   }
   else {
      *usage = SVGA3D_DECLUSAGE_TEXCOORD;
      *usage_index = idx - 1;
   }
}



struct svga_shader_variant *
svga_tgsi_vgpu9_translate(struct svga_context *svga,
                          const struct svga_shader *shader,
                          const struct svga_compile_key *key,
                          enum pipe_shader_type unit);

struct svga_shader_variant *
svga_tgsi_vgpu10_translate(struct svga_context *svga,
                           const struct svga_shader *shader,
                           const struct svga_compile_key *key,
                           enum pipe_shader_type unit);

bool svga_shader_verify(const uint32_t *tokens, unsigned nr_tokens);

void
svga_tgsi_scan_shader(struct svga_shader *shader);

struct svga_shader_variant *
svga_tgsi_compile_shader(struct svga_context *svga,
                         struct svga_shader *shader,
                         const struct svga_compile_key *key);
#endif
