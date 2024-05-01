/*
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * Shaders for VMware SVGA winsys.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 * @author Thomas Hellstrom <thellstrom@vmware.com>
 */

#ifndef VMW_SHADER_H_
#define VMW_SHADER_H_

#include "util/compiler.h"
#include "util/u_atomic.h"
#include "util/u_inlines.h"

struct vmw_svga_winsys_shader
{
   int32_t validated;
   struct pipe_reference refcnt;

   struct vmw_winsys_screen *screen;
   struct svga_winsys_buffer *buf;
   uint32_t shid;
};

static inline struct svga_winsys_gb_shader *
svga_winsys_shader(struct vmw_svga_winsys_shader *shader)
{
   assert(!shader || shader->shid != SVGA3D_INVALID_ID);
   return (struct svga_winsys_gb_shader *)shader;
}

static inline struct vmw_svga_winsys_shader *
vmw_svga_winsys_shader(struct svga_winsys_gb_shader *shader)
{
   return (struct vmw_svga_winsys_shader *)shader;
}

void
vmw_svga_winsys_shader_reference(struct vmw_svga_winsys_shader **pdst,
                                  struct vmw_svga_winsys_shader *src);

struct vmw_svga_winsys_shader *
vmw_svga_shader_create(struct svga_winsys_screen *sws,
                       SVGA3dShaderType type,
                       const uint32 *bytecode,
                       uint32 bytecodeLen,
                       const SVGA3dDXShaderSignatureHeader *sgnInfo,
                       uint32 sgnLen);

#endif /* VMW_SHADER_H_ */
