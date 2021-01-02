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

#pragma once

struct swr_vertex_shader;
struct swr_fragment_shader;
struct swr_geometry_shader;
struct swr_tess_control_shader;
struct swr_tess_evaluation_shader;

struct swr_jit_fs_key;
struct swr_jit_vs_key;
struct swr_jit_gs_key;
struct swr_jit_tcs_key;
struct swr_jit_tes_key;

using PFN_TCS_FUNC = PFN_HS_FUNC;
using PFN_TES_FUNC = PFN_DS_FUNC;

unsigned swr_so_adjust_attrib(unsigned in_attrib,
                              swr_vertex_shader *swr_vs);

PFN_VERTEX_FUNC
swr_compile_vs(struct swr_context *ctx, swr_jit_vs_key &key);

PFN_PIXEL_KERNEL
swr_compile_fs(struct swr_context *ctx, swr_jit_fs_key &key);

PFN_GS_FUNC
swr_compile_gs(struct swr_context *ctx, swr_jit_gs_key &key);

PFN_TCS_FUNC
swr_compile_tcs(struct swr_context *ctx, swr_jit_tcs_key &key);

PFN_TES_FUNC
swr_compile_tes(struct swr_context *ctx, swr_jit_tes_key &key);

void swr_generate_fs_key(struct swr_jit_fs_key &key,
                         struct swr_context *ctx,
                         swr_fragment_shader *swr_fs);

void swr_generate_vs_key(struct swr_jit_vs_key &key,
                         struct swr_context *ctx,
                         swr_vertex_shader *swr_vs);

void swr_generate_fetch_key(struct swr_jit_fetch_key &key,
                            struct swr_vertex_element_state *velems);

void swr_generate_gs_key(struct swr_jit_gs_key &key,
                         struct swr_context *ctx,
                         swr_geometry_shader *swr_gs);

void swr_generate_tcs_key(struct swr_jit_tcs_key &key,
                          struct swr_context *ctx,
                          swr_tess_control_shader *swr_tcs);

void swr_generate_tes_key(struct swr_jit_tes_key &key,
                          struct swr_context *ctx,
                          swr_tess_evaluation_shader *swr_tes);

struct swr_jit_sampler_key {
   unsigned nr_samplers;
   unsigned nr_sampler_views;
   struct swr_sampler_static_state sampler[PIPE_MAX_SHADER_SAMPLER_VIEWS];
};

struct swr_jit_fs_key : swr_jit_sampler_key {
   unsigned nr_cbufs;
   unsigned light_twoside;
   unsigned sprite_coord_enable;
   ubyte vs_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte vs_output_semantic_idx[PIPE_MAX_SHADER_OUTPUTS];
   bool poly_stipple_enable;
};

struct swr_jit_vs_key : swr_jit_sampler_key {
   unsigned clip_plane_mask; // from rasterizer state & vs_info
};

struct swr_jit_fetch_key {
   FETCH_COMPILE_STATE fsState;
};

struct swr_jit_gs_key : swr_jit_sampler_key {
   ubyte vs_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte vs_output_semantic_idx[PIPE_MAX_SHADER_OUTPUTS];
};

// TESS_TODO: revisit this - we probably need to use
// primitive modes, number of vertices emitted, etc.
struct swr_jit_tcs_key : swr_jit_sampler_key {
   ubyte vs_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte vs_output_semantic_idx[PIPE_MAX_SHADER_OUTPUTS];
   unsigned clip_plane_mask; // from rasterizer state & tcs_info
};

// TESS_TODO: revisit this
struct swr_jit_tes_key : swr_jit_sampler_key {
   ubyte prev_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte prev_output_semantic_idx[PIPE_MAX_SHADER_OUTPUTS];
   unsigned clip_plane_mask; // from rasterizer state & tes_info
};

namespace std
{
template <> struct hash<swr_jit_fs_key> {
   std::size_t operator()(const swr_jit_fs_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};

template <> struct hash<swr_jit_vs_key> {
   std::size_t operator()(const swr_jit_vs_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};

template <> struct hash<swr_jit_fetch_key> {
   std::size_t operator()(const swr_jit_fetch_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};

template <> struct hash<swr_jit_gs_key> {
   std::size_t operator()(const swr_jit_gs_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};

template <> struct hash<swr_jit_tcs_key> {
   std::size_t operator()(const swr_jit_tcs_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};

template <> struct hash<swr_jit_tes_key> {
   std::size_t operator()(const swr_jit_tes_key &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};
};

bool operator==(const swr_jit_fs_key &lhs, const swr_jit_fs_key &rhs);
bool operator==(const swr_jit_vs_key &lhs, const swr_jit_vs_key &rhs);
bool operator==(const swr_jit_fetch_key &lhs, const swr_jit_fetch_key &rhs);
bool operator==(const swr_jit_gs_key &lhs, const swr_jit_gs_key &rhs);
bool operator==(const swr_jit_tcs_key &lhs, const swr_jit_tcs_key &rhs);
bool operator==(const swr_jit_tes_key &lhs, const swr_jit_tes_key &rhs);
