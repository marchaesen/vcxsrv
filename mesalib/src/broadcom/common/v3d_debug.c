/*
 * Copyright 2003 VMware, Inc.
 * Copyright © 2006 Intel Corporation
 * Copyright © 2017 Broadcom
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

/**
 * \file v3d_debug.c
 *
 * Support for the V3D_DEBUG environment variable, along with other
 * miscellaneous debugging code.
 */

#include <stdlib.h>

#include "common/v3d_debug.h"
#include "util/macros.h"
#include "util/debug.h"
#include "c11/threads.h"

uint32_t V3D_DEBUG = 0;

static const struct debug_control debug_control[] = {
        { "cl",          V3D_DEBUG_CL},
        { "clif",        V3D_DEBUG_CLIF},
        { "qpu",         V3D_DEBUG_QPU},
        { "vir",         V3D_DEBUG_VIR},
        { "nir",         V3D_DEBUG_NIR},
        { "tgsi",        V3D_DEBUG_TGSI},
        { "shaderdb",    V3D_DEBUG_SHADERDB},
        { "surface",     V3D_DEBUG_SURFACE},
        { "perf",        V3D_DEBUG_PERF},
        { "norast",      V3D_DEBUG_NORAST},
        { "fs",          V3D_DEBUG_FS},
        { "gs",          V3D_DEBUG_GS},
        { "vs",          V3D_DEBUG_VS},
        { "cs",          V3D_DEBUG_CS},
        { "always_flush", V3D_DEBUG_ALWAYS_FLUSH},
        { "precompile",  V3D_DEBUG_PRECOMPILE},
        { "ra",          V3D_DEBUG_RA},
        { "dump_spirv",  V3D_DEBUG_DUMP_SPIRV},
        { NULL,    0 }
};

uint32_t
v3d_debug_flag_for_shader_stage(gl_shader_stage stage)
{
        uint32_t flags[] = {
                [MESA_SHADER_VERTEX] = V3D_DEBUG_VS,
                [MESA_SHADER_TESS_CTRL] = 0,
                [MESA_SHADER_TESS_EVAL] = 0,
                [MESA_SHADER_GEOMETRY] = V3D_DEBUG_GS,
                [MESA_SHADER_FRAGMENT] = V3D_DEBUG_FS,
                [MESA_SHADER_COMPUTE] = V3D_DEBUG_CS,
        };
        STATIC_ASSERT(MESA_SHADER_STAGES == 6);
        return flags[stage];
}

static void
v3d_process_debug_variable_once(void)
{
        V3D_DEBUG = parse_debug_string(getenv("V3D_DEBUG"), debug_control);

        if (V3D_DEBUG & V3D_DEBUG_SHADERDB)
                V3D_DEBUG |= V3D_DEBUG_NORAST;
}

void
v3d_process_debug_variable(void)
{
        static once_flag v3d_process_debug_variable_flag = ONCE_FLAG_INIT;

        call_once(&v3d_process_debug_variable_flag,
                  v3d_process_debug_variable_once);
}
