/*
 * Copyright (c) 2012-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0 OR MIT
 */

/*
 * svga3d_limits.h --
 *
 *    SVGA 3d hardware limits
 */





#ifndef _SVGA3D_LIMITS_H_
#define _SVGA3D_LIMITS_H_







#define SVGA3D_NUM_CLIPPLANES                   6
#define SVGA3D_MAX_CONTEXT_IDS                  256
#define SVGA3D_MAX_SURFACE_IDS                  (32 * 1024)


#define SVGA3D_MAX_RENDER_TARGETS               8
#define SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS  (SVGA3D_MAX_RENDER_TARGETS)
#define SVGA3D_MAX_UAVIEWS                      8
#define SVGA3D_DX11_1_MAX_UAVIEWS               64


#define SVGA3D_HB_MAX_SURFACE_SIZE MBYTES_2_BYTES(128)


#define SVGA3D_MAX_SHADERIDS                    5000

#define SVGA3D_MAX_SIMULTANEOUS_SHADERS         20000

#define SVGA3D_NUM_TEXTURE_UNITS                32
#define SVGA3D_NUM_LIGHTS                       8


#define SVGA3D_MAX_VIDEOPROCESSOR_SAMPLERS      32


#define SVGA3D_MAX_SHADER_MEMORY_BYTES (8 * 1024 * 1024)
#define SVGA3D_MAX_SHADER_MEMORY  (SVGA3D_MAX_SHADER_MEMORY_BYTES / \
                                   sizeof(uint32))

#define SVGA3D_MAX_SHADER_THREAD_GROUPS 65535

#define SVGA3D_MAX_CLIP_PLANES    6


#define SVGA3D_MAX_TEXTURE_COORDS 8


#define SVGA3D_MAX_SURFACE_FACES 6


#define SVGA3D_SM4_MAX_SURFACE_ARRAYSIZE 512
#define SVGA3D_SM5_MAX_SURFACE_ARRAYSIZE 2048
#define SVGA3D_MAX_SURFACE_ARRAYSIZE SVGA3D_SM5_MAX_SURFACE_ARRAYSIZE


#define SVGA3D_MAX_VERTEX_ARRAYS   32


#define SVGA3D_MAX_DRAW_PRIMITIVE_RANGES 32


#define SVGA3D_MAX_SAMPLES 8

#endif
