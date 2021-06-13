/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#ifndef SWR_STATE_H
#define SWR_STATE_H

#include "pipe/p_defines.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_dump.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_tgsi.h"
#include "util/crc32.h"
#include "api.h"
#include "swr_tex_sample.h"
#include "swr_shader.h"
#include <unordered_map>
#include <memory>

template <typename T>
struct ShaderVariant {
   struct gallivm_state *gallivm;
   T shader;

   ShaderVariant(struct gallivm_state *gs, T code) : gallivm(gs), shader(code) {}
   ~ShaderVariant() { gallivm_destroy(gallivm); }
};

using PFN_TCS_FUNC = PFN_HS_FUNC;
using PFN_TES_FUNC = PFN_DS_FUNC;

typedef ShaderVariant<PFN_VERTEX_FUNC> VariantVS;
typedef ShaderVariant<PFN_PIXEL_KERNEL> VariantFS;
typedef ShaderVariant<PFN_GS_FUNC> VariantGS;
typedef ShaderVariant<PFN_TCS_FUNC> VariantTCS;
typedef ShaderVariant<PFN_TES_FUNC> VariantTES;

/* skeleton */
struct swr_vertex_shader {
   struct pipe_shader_state pipe;
   struct lp_tgsi_info info;
   std::unordered_map<swr_jit_vs_key, std::unique_ptr<VariantVS>> map;
   SWR_STREAMOUT_STATE soState;
   PFN_SO_FUNC soFunc[PIPE_PRIM_MAX] {0};
};

struct swr_fragment_shader {
   struct pipe_shader_state pipe;
   struct lp_tgsi_info info;
   uint32_t constantMask;
   uint32_t flatConstantMask;
   uint32_t pointSpriteMask;
   std::unordered_map<swr_jit_fs_key, std::unique_ptr<VariantFS>> map;
};

struct swr_geometry_shader {
   struct pipe_shader_state pipe;
   struct lp_tgsi_info info;
   SWR_GS_STATE gsState;

   std::unordered_map<swr_jit_gs_key, std::unique_ptr<VariantGS>> map;
};

struct swr_tess_control_shader {
   struct pipe_shader_state pipe;
   struct lp_tgsi_info info;
   uint32_t vertices_per_patch;

   std::unordered_map<swr_jit_tcs_key, std::unique_ptr<VariantTCS>> map;
};

struct swr_tess_evaluation_shader {
   struct pipe_shader_state pipe;
   struct lp_tgsi_info info;
   SWR_TS_STATE ts_state;

   std::unordered_map<swr_jit_tes_key, std::unique_ptr<VariantTES>> map;
};


/* Vertex element state */
struct swr_vertex_element_state {
   FETCH_COMPILE_STATE fsState;
   PFN_FETCH_FUNC fsFunc {NULL};
   uint32_t stream_pitch[PIPE_MAX_ATTRIBS] {0};
   uint32_t min_instance_div[PIPE_MAX_ATTRIBS] {0};
   uint32_t instanced_bufs {0};
   std::unordered_map<swr_jit_fetch_key, PFN_FETCH_FUNC> map;
};

struct swr_blend_state {
   struct pipe_blend_state pipe;
   SWR_BLEND_STATE blendState;
   RENDER_TARGET_BLEND_COMPILE_STATE compileState[PIPE_MAX_COLOR_BUFS];
};

struct swr_poly_stipple {
   struct pipe_poly_stipple pipe;
   bool prim_is_poly;
};

/*
 * Derived SWR API DrawState
 * For convenience of making simple changes without re-deriving state.
 */
struct swr_derived_state {
   SWR_RASTSTATE rastState;
   SWR_VIEWPORT vp[KNOB_NUM_VIEWPORTS_SCISSORS];
   SWR_VIEWPORT_MATRICES vpm;
};

void swr_update_derived(struct pipe_context *,
                        const struct pipe_draw_info * = nullptr,
                        const struct pipe_draw_start_count_bias *draw = nullptr);

/*
 * Conversion functions: Convert mesa state defines to SWR.
 */

static INLINE SWR_LOGIC_OP
swr_convert_logic_op(const UINT op)
{
   switch (op) {
   case PIPE_LOGICOP_CLEAR:
      return LOGICOP_CLEAR;
   case PIPE_LOGICOP_NOR:
      return LOGICOP_NOR;
   case PIPE_LOGICOP_AND_INVERTED:
      return LOGICOP_AND_INVERTED;
   case PIPE_LOGICOP_COPY_INVERTED:
      return LOGICOP_COPY_INVERTED;
   case PIPE_LOGICOP_AND_REVERSE:
      return LOGICOP_AND_REVERSE;
   case PIPE_LOGICOP_INVERT:
      return LOGICOP_INVERT;
   case PIPE_LOGICOP_XOR:
      return LOGICOP_XOR;
   case PIPE_LOGICOP_NAND:
      return LOGICOP_NAND;
   case PIPE_LOGICOP_AND:
      return LOGICOP_AND;
   case PIPE_LOGICOP_EQUIV:
      return LOGICOP_EQUIV;
   case PIPE_LOGICOP_NOOP:
      return LOGICOP_NOOP;
   case PIPE_LOGICOP_OR_INVERTED:
      return LOGICOP_OR_INVERTED;
   case PIPE_LOGICOP_COPY:
      return LOGICOP_COPY;
   case PIPE_LOGICOP_OR_REVERSE:
      return LOGICOP_OR_REVERSE;
   case PIPE_LOGICOP_OR:
      return LOGICOP_OR;
   case PIPE_LOGICOP_SET:
      return LOGICOP_SET;
   default:
      assert(0 && "Unsupported logic op");
      return LOGICOP_NOOP;
   }
}

static INLINE SWR_STENCILOP
swr_convert_stencil_op(const UINT op)
{
   switch (op) {
   case PIPE_STENCIL_OP_KEEP:
      return STENCILOP_KEEP;
   case PIPE_STENCIL_OP_ZERO:
      return STENCILOP_ZERO;
   case PIPE_STENCIL_OP_REPLACE:
      return STENCILOP_REPLACE;
   case PIPE_STENCIL_OP_INCR:
      return STENCILOP_INCRSAT;
   case PIPE_STENCIL_OP_DECR:
      return STENCILOP_DECRSAT;
   case PIPE_STENCIL_OP_INCR_WRAP:
      return STENCILOP_INCR;
   case PIPE_STENCIL_OP_DECR_WRAP:
      return STENCILOP_DECR;
   case PIPE_STENCIL_OP_INVERT:
      return STENCILOP_INVERT;
   default:
      assert(0 && "Unsupported stencil op");
      return STENCILOP_KEEP;
   }
}

static INLINE SWR_FORMAT
swr_convert_index_type(const UINT index_size)
{
   switch (index_size) {
   case sizeof(unsigned char):
      return R8_UINT;
   case sizeof(unsigned short):
      return R16_UINT;
   case sizeof(unsigned int):
      return R32_UINT;
   default:
      assert(0 && "Unsupported index type");
      return R32_UINT;
   }
}


static INLINE SWR_ZFUNCTION
swr_convert_depth_func(const UINT pipe_func)
{
   switch (pipe_func) {
   case PIPE_FUNC_NEVER:
      return ZFUNC_NEVER;
   case PIPE_FUNC_LESS:
      return ZFUNC_LT;
   case PIPE_FUNC_EQUAL:
      return ZFUNC_EQ;
   case PIPE_FUNC_LEQUAL:
      return ZFUNC_LE;
   case PIPE_FUNC_GREATER:
      return ZFUNC_GT;
   case PIPE_FUNC_NOTEQUAL:
      return ZFUNC_NE;
   case PIPE_FUNC_GEQUAL:
      return ZFUNC_GE;
   case PIPE_FUNC_ALWAYS:
      return ZFUNC_ALWAYS;
   default:
      assert(0 && "Unsupported depth func");
      return ZFUNC_ALWAYS;
   }
}


static INLINE SWR_CULLMODE
swr_convert_cull_mode(const UINT cull_face)
{
   switch (cull_face) {
   case PIPE_FACE_NONE:
      return SWR_CULLMODE_NONE;
   case PIPE_FACE_FRONT:
      return SWR_CULLMODE_FRONT;
   case PIPE_FACE_BACK:
      return SWR_CULLMODE_BACK;
   case PIPE_FACE_FRONT_AND_BACK:
      return SWR_CULLMODE_BOTH;
   default:
      assert(0 && "Invalid cull mode");
      return SWR_CULLMODE_NONE;
   }
}

static INLINE SWR_BLEND_OP
swr_convert_blend_func(const UINT blend_func)
{
   switch (blend_func) {
   case PIPE_BLEND_ADD:
      return BLENDOP_ADD;
   case PIPE_BLEND_SUBTRACT:
      return BLENDOP_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return BLENDOP_REVSUBTRACT;
   case PIPE_BLEND_MIN:
      return BLENDOP_MIN;
   case PIPE_BLEND_MAX:
      return BLENDOP_MAX;
   default:
      assert(0 && "Invalid blend func");
      return BLENDOP_ADD;
   }
}

static INLINE SWR_BLEND_FACTOR
swr_convert_blend_factor(const UINT blend_factor)
{
   switch (blend_factor) {
   case PIPE_BLENDFACTOR_ONE:
      return BLENDFACTOR_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:
      return BLENDFACTOR_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return BLENDFACTOR_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA:
      return BLENDFACTOR_DST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR:
      return BLENDFACTOR_DST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return BLENDFACTOR_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_CONST_COLOR:
      return BLENDFACTOR_CONST_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return BLENDFACTOR_CONST_ALPHA;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
      return BLENDFACTOR_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
      return BLENDFACTOR_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_ZERO:
      return BLENDFACTOR_ZERO;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      return BLENDFACTOR_INV_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return BLENDFACTOR_INV_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      return BLENDFACTOR_INV_DST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:
      return BLENDFACTOR_INV_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      return BLENDFACTOR_INV_CONST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return BLENDFACTOR_INV_CONST_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      return BLENDFACTOR_INV_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      return BLENDFACTOR_INV_SRC1_ALPHA;
   default:
      assert(0 && "Invalid blend factor");
      return BLENDFACTOR_ONE;
   }
}

static INLINE enum SWR_SURFACE_TYPE
swr_convert_target_type(const enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_BUFFER:
      return SURFACE_BUFFER;
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return SURFACE_1D;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
      return SURFACE_2D;
   case PIPE_TEXTURE_3D:
      return SURFACE_3D;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return SURFACE_CUBE;
   default:
      assert(0);
      return SURFACE_NULL;
   }
}

/*
 * Convert mesa PIPE_PRIM_X to SWR enum PRIMITIVE_TOPOLOGY
 */
static INLINE enum PRIMITIVE_TOPOLOGY
swr_convert_prim_topology(const unsigned mode, const unsigned tcs_verts)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return TOP_POINT_LIST;
   case PIPE_PRIM_LINES:
      return TOP_LINE_LIST;
   case PIPE_PRIM_LINE_LOOP:
      return TOP_LINE_LOOP;
   case PIPE_PRIM_LINE_STRIP:
      return TOP_LINE_STRIP;
   case PIPE_PRIM_TRIANGLES:
      return TOP_TRIANGLE_LIST;
   case PIPE_PRIM_TRIANGLE_STRIP:
      return TOP_TRIANGLE_STRIP;
   case PIPE_PRIM_TRIANGLE_FAN:
      return TOP_TRIANGLE_FAN;
   case PIPE_PRIM_QUADS:
      return TOP_QUAD_LIST;
   case PIPE_PRIM_QUAD_STRIP:
      return TOP_QUAD_STRIP;
   case PIPE_PRIM_POLYGON:
      return TOP_TRIANGLE_FAN; /* XXX TOP_POLYGON; */
   case PIPE_PRIM_LINES_ADJACENCY:
      return TOP_LINE_LIST_ADJ;
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return TOP_LISTSTRIP_ADJ;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return TOP_TRI_LIST_ADJ;
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return TOP_TRI_STRIP_ADJ;
   case PIPE_PRIM_PATCHES:
      // rasterizer has a separate type for each possible number of patch vertices
      return (PRIMITIVE_TOPOLOGY)((unsigned)TOP_PATCHLIST_BASE + tcs_verts);
   default:
      assert(0 && "Unknown topology");
      return TOP_UNKNOWN;
   }
};

/*
 * convert mesa PIPE_POLYGON_MODE_X to SWR enum SWR_FILLMODE
 */
static INLINE enum SWR_FILLMODE
swr_convert_fill_mode(const unsigned mode)
{
   switch(mode) {
   case PIPE_POLYGON_MODE_FILL:
      return SWR_FILLMODE_SOLID;
   case PIPE_POLYGON_MODE_LINE:
      return SWR_FILLMODE_WIREFRAME;
   case PIPE_POLYGON_MODE_POINT:
      return SWR_FILLMODE_POINT;
   default:
      assert(0 && "Unknown fillmode");
      return SWR_FILLMODE_SOLID; // at least do something sensible
   }
}


#endif
