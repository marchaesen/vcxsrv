/*
 * Copyright © 2021 Valve Corporation
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
 *
 */

#include "ac_nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "u_math.h"
#include "u_vector.h"

enum {
   nggc_passflag_used_by_pos = 1,
   nggc_passflag_used_by_other = 2,
   nggc_passflag_used_by_both = nggc_passflag_used_by_pos | nggc_passflag_used_by_other,
};

typedef struct
{
   nir_ssa_def *ssa;
   nir_variable *var;
} saved_uniform;

typedef struct
{
   nir_variable *position_value_var;
   nir_variable *prim_exp_arg_var;
   nir_variable *es_accepted_var;
   nir_variable *gs_accepted_var;
   nir_variable *gs_vtx_indices_vars[3];

   nir_ssa_def *vtx_addr[3];

   struct u_vector saved_uniforms;

   bool passthrough;
   bool export_prim_id;
   bool early_prim_export;
   bool use_edgeflags;
   bool has_prim_query;
   bool streamout_enabled;
   unsigned wave_size;
   unsigned max_num_waves;
   unsigned num_vertices_per_primitives;
   unsigned provoking_vtx_idx;
   unsigned max_es_num_vertices;
   unsigned position_store_base;

   /* LDS params */
   unsigned pervertex_lds_bytes;
   unsigned total_lds_bytes;

   uint64_t inputs_needed_by_pos;
   uint64_t inputs_needed_by_others;
   uint32_t instance_rate_inputs;

   nir_instr *compact_arg_stores[4];
   nir_intrinsic_instr *overwrite_args;

   /* clip distance */
   nir_variable *clip_vertex_var;
   nir_variable *clipdist_neg_mask_var;
   unsigned clipdist_enable_mask;
   unsigned user_clip_plane_enable_mask;
   bool has_clipdist;
} lower_ngg_nogs_state;

typedef struct
{
   /* store output base (driver location) */
   uint8_t base;
   /* output stream index, 2 bit per component */
   uint8_t stream;
   /* Bitmask of components used: 4 bits per slot, 1 bit per component. */
   uint8_t components_mask : 4;
} gs_output_info;

typedef struct
{
   nir_function_impl *impl;
   nir_variable *output_vars[VARYING_SLOT_MAX][4];
   nir_variable *current_clear_primflag_idx_var;
   int const_out_vtxcnt[4];
   int const_out_prmcnt[4];
   unsigned wave_size;
   unsigned max_num_waves;
   unsigned num_vertices_per_primitive;
   unsigned lds_addr_gs_out_vtx;
   unsigned lds_addr_gs_scratch;
   unsigned lds_bytes_per_gs_out_vertex;
   unsigned lds_offs_primflags;
   bool found_out_vtxcnt[4];
   bool output_compile_time_known;
   bool provoking_vertex_last;
   bool can_cull;
   bool streamout_enabled;
   gs_output_info output_info[VARYING_SLOT_MAX];
} lower_ngg_gs_state;

/* LDS layout of Mesh Shader workgroup info. */
enum {
   /* DW0: number of primitives */
   lds_ms_num_prims = 0,
   /* DW1: reserved for future use */
   lds_ms_dw1_reserved = 4,
   /* DW2: workgroup index within the current dispatch */
   lds_ms_wg_index = 8,
   /* DW3: number of API workgroups in flight */
   lds_ms_num_api_waves = 12,
};

/* Potential location for Mesh Shader outputs. */
typedef enum {
   ms_out_mode_lds,
   ms_out_mode_vram,
   ms_out_mode_var,
} ms_out_mode;

typedef struct
{
   uint64_t mask; /* Mask of output locations */
   uint32_t addr; /* Base address */
} ms_out_part;

typedef struct
{
   /* Mesh shader LDS layout. For details, see ms_calculate_output_layout. */
   struct {
      uint32_t workgroup_info_addr;
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
      uint32_t indices_addr;
      uint32_t cull_flags_addr;
      uint32_t total_size;
   } lds;
   /* VRAM "mesh shader scratch ring" layout for outputs that don't fit into the LDS. */
   struct {
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
   } vram;
   /* Outputs without cross-invocation access can be stored in variables. */
   struct {
      ms_out_part vtx_attr;
      ms_out_part prm_attr;
   } var;
} ms_out_mem_layout;

typedef struct
{
   ms_out_mem_layout layout;
   uint64_t per_vertex_outputs;
   uint64_t per_primitive_outputs;
   unsigned vertices_per_prim;

   unsigned wave_size;
   unsigned api_workgroup_size;
   unsigned hw_workgroup_size;

   nir_ssa_def *workgroup_index;
   nir_variable *out_variables[VARYING_SLOT_MAX * 4];
   nir_variable *primitive_count_var;
   nir_variable *vertex_count_var;

   /* True if the lowering needs to insert the layer output. */
   bool insert_layer_output;
   /* True if cull flags are used */
   bool uses_cull_flags;

   struct {
      /* Bitmask of components used: 4 bits per slot, 1 bit per component. */
      uint32_t components_mask;
   } output_info[VARYING_SLOT_MAX];
} lower_ngg_ms_state;

/* Per-vertex LDS layout of culling shaders */
enum {
   /* Position of the ES vertex (at the beginning for alignment reasons) */
   lds_es_pos_x = 0,
   lds_es_pos_y = 4,
   lds_es_pos_z = 8,
   lds_es_pos_w = 12,

   /* 1 when the vertex is accepted, 0 if it should be culled */
   lds_es_vertex_accepted = 16,
   /* ID of the thread which will export the current thread's vertex */
   lds_es_exporter_tid = 17,
   /* bit i is set when the i'th clip distance of a vertex is negative */
   lds_es_clipdist_neg_mask = 18,

   /* Repacked arguments - also listed separately for VS and TES */
   lds_es_arg_0 = 20,

   /* VS arguments which need to be repacked */
   lds_es_vs_vertex_id = 20,
   lds_es_vs_instance_id = 24,

   /* TES arguments which need to be repacked */
   lds_es_tes_u = 20,
   lds_es_tes_v = 24,
   lds_es_tes_rel_patch_id = 28,
   lds_es_tes_patch_id = 32,
};

typedef struct {
   nir_ssa_def *num_repacked_invocations;
   nir_ssa_def *repacked_invocation_index;
} wg_repack_result;

/**
 * Computes a horizontal sum of 8-bit packed values loaded from LDS.
 *
 * Each lane N will sum packed bytes 0 to N-1.
 * We only care about the results from up to wave_id+1 lanes.
 * (Other lanes are not deactivated but their calculation is not used.)
 */
static nir_ssa_def *
summarize_repack(nir_builder *b, nir_ssa_def *packed_counts, unsigned num_lds_dwords)
{
   /* We'll use shift to filter out the bytes not needed by the current lane.
    *
    * Need to shift by: num_lds_dwords * 4 - lane_id (in bytes).
    * However, two shifts are needed because one can't go all the way,
    * so the shift amount is half that (and in bits).
    *
    * When v_dot4_u32_u8 is available, we right-shift a series of 0x01 bytes.
    * This will yield 0x01 at wanted byte positions and 0x00 at unwanted positions,
    * therefore v_dot can get rid of the unneeded values.
    * This sequence is preferable because it better hides the latency of the LDS.
    *
    * If the v_dot instruction can't be used, we left-shift the packed bytes.
    * This will shift out the unneeded bytes and shift in zeroes instead,
    * then we sum them using v_sad_u8.
    */

   nir_ssa_def *lane_id = nir_load_subgroup_invocation(b);
   nir_ssa_def *shift = nir_iadd_imm_nuw(b, nir_imul_imm(b, lane_id, -4u), num_lds_dwords * 16);
   bool use_dot = b->shader->options->has_udot_4x8;

   if (num_lds_dwords == 1) {
      nir_ssa_def *dot_op = !use_dot ? NULL : nir_ushr(b, nir_ushr(b, nir_imm_int(b, 0x01010101), shift), shift);

      /* Broadcast the packed data we read from LDS (to the first 16 lanes, but we only care up to num_waves). */
      nir_ssa_def *packed = nir_lane_permute_16_amd(b, packed_counts, nir_imm_int(b, 0), nir_imm_int(b, 0));

      /* Horizontally add the packed bytes. */
      if (use_dot) {
         return nir_udot_4x8_uadd(b, packed, dot_op, nir_imm_int(b, 0));
      } else {
         nir_ssa_def *sad_op = nir_ishl(b, nir_ishl(b, packed, shift), shift);
         return nir_sad_u8x4(b, sad_op, nir_imm_int(b, 0), nir_imm_int(b, 0));
      }
   } else if (num_lds_dwords == 2) {
      nir_ssa_def *dot_op = !use_dot ? NULL : nir_ushr(b, nir_ushr(b, nir_imm_int64(b, 0x0101010101010101), shift), shift);

      /* Broadcast the packed data we read from LDS (to the first 16 lanes, but we only care up to num_waves). */
      nir_ssa_def *packed_dw0 = nir_lane_permute_16_amd(b, nir_unpack_64_2x32_split_x(b, packed_counts), nir_imm_int(b, 0), nir_imm_int(b, 0));
      nir_ssa_def *packed_dw1 = nir_lane_permute_16_amd(b, nir_unpack_64_2x32_split_y(b, packed_counts), nir_imm_int(b, 0), nir_imm_int(b, 0));

      /* Horizontally add the packed bytes. */
      if (use_dot) {
         nir_ssa_def *sum = nir_udot_4x8_uadd(b, packed_dw0, nir_unpack_64_2x32_split_x(b, dot_op), nir_imm_int(b, 0));
         return nir_udot_4x8_uadd(b, packed_dw1, nir_unpack_64_2x32_split_y(b, dot_op), sum);
      } else {
         nir_ssa_def *sad_op = nir_ishl(b, nir_ishl(b, nir_pack_64_2x32_split(b, packed_dw0, packed_dw1), shift), shift);
         nir_ssa_def *sum = nir_sad_u8x4(b, nir_unpack_64_2x32_split_x(b, sad_op), nir_imm_int(b, 0), nir_imm_int(b, 0));
         return nir_sad_u8x4(b, nir_unpack_64_2x32_split_y(b, sad_op), nir_imm_int(b, 0), sum);
      }
   } else {
      unreachable("Unimplemented NGG wave count");
   }
}

/**
 * Repacks invocations in the current workgroup to eliminate gaps between them.
 *
 * Uses 1 dword of LDS per 4 waves (1 byte of LDS per wave).
 * Assumes that all invocations in the workgroup are active (exec = -1).
 */
static wg_repack_result
repack_invocations_in_workgroup(nir_builder *b, nir_ssa_def *input_bool,
                                unsigned lds_addr_base, unsigned max_num_waves,
                                unsigned wave_size)
{
   /* Input boolean: 1 if the current invocation should survive the repack. */
   assert(input_bool->bit_size == 1);

   /* STEP 1. Count surviving invocations in the current wave.
    *
    * Implemented by a scalar instruction that simply counts the number of bits set in a 32/64-bit mask.
    */

   nir_ssa_def *input_mask = nir_ballot(b, 1, wave_size, input_bool);
   nir_ssa_def *surviving_invocations_in_current_wave = nir_bit_count(b, input_mask);

   /* If we know at compile time that the workgroup has only 1 wave, no further steps are necessary. */
   if (max_num_waves == 1) {
      wg_repack_result r = {
         .num_repacked_invocations = surviving_invocations_in_current_wave,
         .repacked_invocation_index = nir_mbcnt_amd(b, input_mask, nir_imm_int(b, 0)),
      };
      return r;
   }

   /* STEP 2. Waves tell each other their number of surviving invocations.
    *
    * Each wave activates only its first lane (exec = 1), which stores the number of surviving
    * invocations in that wave into the LDS, then reads the numbers from every wave.
    *
    * The workgroup size of NGG shaders is at most 256, which means
    * the maximum number of waves is 4 in Wave64 mode and 8 in Wave32 mode.
    * Each wave writes 1 byte, so it's up to 8 bytes, so at most 2 dwords are necessary.
    */

   const unsigned num_lds_dwords = DIV_ROUND_UP(max_num_waves, 4);
   assert(num_lds_dwords <= 2);

   nir_ssa_def *wave_id = nir_load_subgroup_id(b);
   nir_ssa_def *dont_care = nir_ssa_undef(b, 1, num_lds_dwords * 32);
   nir_if *if_first_lane = nir_push_if(b, nir_elect(b, 1));

   nir_store_shared(b, nir_u2u8(b, surviving_invocations_in_current_wave), wave_id, .base = lds_addr_base);

   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   nir_ssa_def *packed_counts = nir_load_shared(b, 1, num_lds_dwords * 32, nir_imm_int(b, 0), .base = lds_addr_base, .align_mul = 8u);

   nir_pop_if(b, if_first_lane);

   packed_counts = nir_if_phi(b, packed_counts, dont_care);

   /* STEP 3. Compute the repacked invocation index and the total number of surviving invocations.
    *
    * By now, every wave knows the number of surviving invocations in all waves.
    * Each number is 1 byte, and they are packed into up to 2 dwords.
    *
    * Each lane N will sum the number of surviving invocations from waves 0 to N-1.
    * If the workgroup has M waves, then each wave will use only its first M+1 lanes for this.
    * (Other lanes are not deactivated but their calculation is not used.)
    *
    * - We read the sum from the lane whose id is the current wave's id.
    *   Add the masked bitcount to this, and we get the repacked invocation index.
    * - We read the sum from the lane whose id is the number of waves in the workgroup.
    *   This is the total number of surviving invocations in the workgroup.
    */

   nir_ssa_def *num_waves = nir_load_num_subgroups(b);
   nir_ssa_def *sum = summarize_repack(b, packed_counts, num_lds_dwords);

   nir_ssa_def *wg_repacked_index_base = nir_read_invocation(b, sum, wave_id);
   nir_ssa_def *wg_num_repacked_invocations = nir_read_invocation(b, sum, num_waves);
   nir_ssa_def *wg_repacked_index = nir_mbcnt_amd(b, input_mask, wg_repacked_index_base);

   wg_repack_result r = {
      .num_repacked_invocations = wg_num_repacked_invocations,
      .repacked_invocation_index = wg_repacked_index,
   };

   return r;
}

static nir_ssa_def *
pervertex_lds_addr(nir_builder *b, nir_ssa_def *vertex_idx, unsigned per_vtx_bytes)
{
   return nir_imul_imm(b, vertex_idx, per_vtx_bytes);
}

static nir_ssa_def *
emit_pack_ngg_prim_exp_arg(nir_builder *b, unsigned num_vertices_per_primitives,
                           nir_ssa_def *vertex_indices[3], nir_ssa_def *is_null_prim,
                           bool use_edgeflags)
{
   nir_ssa_def *arg = use_edgeflags
                      ? nir_load_initial_edgeflags_amd(b)
                      : nir_imm_int(b, 0);

   for (unsigned i = 0; i < num_vertices_per_primitives; ++i) {
      assert(vertex_indices[i]);
      arg = nir_ior(b, arg, nir_ishl(b, vertex_indices[i], nir_imm_int(b, 10u * i)));
   }

   if (is_null_prim) {
      if (is_null_prim->bit_size == 1)
         is_null_prim = nir_b2i32(b, is_null_prim);
      assert(is_null_prim->bit_size == 32);
      arg = nir_ior(b, arg, nir_ishl(b, is_null_prim, nir_imm_int(b, 31u)));
   }

   return arg;
}

static void
ngg_nogs_init_vertex_indices_vars(nir_builder *b, nir_function_impl *impl, lower_ngg_nogs_state *st)
{
   for (unsigned v = 0; v < st->num_vertices_per_primitives; ++v) {
      st->gs_vtx_indices_vars[v] = nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx_addr");

      nir_ssa_def *vtx = nir_ubfe(b, nir_load_gs_vertex_offset_amd(b, .base = v / 2u),
                         nir_imm_int(b, (v & 1u) * 16u), nir_imm_int(b, 16u));
      nir_store_var(b, st->gs_vtx_indices_vars[v], vtx, 0x1);
   }
}

static nir_ssa_def *
emit_ngg_nogs_prim_exp_arg(nir_builder *b, lower_ngg_nogs_state *st)
{
   if (st->passthrough) {
      assert(!st->export_prim_id || b->shader->info.stage != MESA_SHADER_VERTEX);
      return nir_load_packed_passthrough_primitive_amd(b);
   } else {
      nir_ssa_def *vtx_idx[3] = {0};

      for (unsigned v = 0; v < st->num_vertices_per_primitives; ++v)
         vtx_idx[v] = nir_load_var(b, st->gs_vtx_indices_vars[v]);

      return emit_pack_ngg_prim_exp_arg(b, st->num_vertices_per_primitives, vtx_idx, NULL, st->use_edgeflags);
   }
}

static void
emit_ngg_nogs_prim_export(nir_builder *b, lower_ngg_nogs_state *st, nir_ssa_def *arg)
{
   nir_ssa_def *gs_thread = st->gs_accepted_var
                            ? nir_load_var(b, st->gs_accepted_var)
                            : nir_has_input_primitive_amd(b);

   nir_if *if_gs_thread = nir_push_if(b, gs_thread);
   {
      if (!arg)
         arg = emit_ngg_nogs_prim_exp_arg(b, st);

      if (st->has_prim_query) {
         nir_if *if_shader_query = nir_push_if(b, nir_load_shader_query_enabled_amd(b));
         {
            /* Number of active GS threads. Each has 1 output primitive. */
            nir_ssa_def *num_gs_threads = nir_bit_count(b, nir_ballot(b, 1, st->wave_size, nir_imm_bool(b, true)));
            /* Activate only 1 lane and add the number of primitives to GDS. */
            nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
            {
               /* Use a different GDS offset than NGG GS to ensure that pipeline statistics
                * queries won't return the number of primitives generated by VS/TES.
                */
               nir_gds_atomic_add_amd(b, 32, num_gs_threads, nir_imm_int(b, 4), nir_imm_int(b, 0x100));
            }
            nir_pop_if(b, if_elected);
         }
         nir_pop_if(b, if_shader_query);
      }

      nir_export_primitive_amd(b, arg);
   }
   nir_pop_if(b, if_gs_thread);
}

static void
emit_ngg_nogs_prim_id_store_shared(nir_builder *b, lower_ngg_nogs_state *st)
{
   nir_ssa_def *gs_thread = st->gs_accepted_var ?
      nir_load_var(b, st->gs_accepted_var) : nir_has_input_primitive_amd(b);

   nir_if *if_gs_thread = nir_push_if(b, gs_thread);
   {
      /* Copy Primitive IDs from GS threads to the LDS address
       * corresponding to the ES thread of the provoking vertex.
       * It will be exported as a per-vertex attribute.
       */
      nir_ssa_def *prim_id = nir_load_primitive_id(b);
      nir_ssa_def *provoking_vtx_idx = nir_load_var(b, st->gs_vtx_indices_vars[st->provoking_vtx_idx]);
      nir_ssa_def *addr = pervertex_lds_addr(b, provoking_vtx_idx, st->pervertex_lds_bytes);

      /* primitive id is always at last of a vertex */
      nir_store_shared(b, prim_id, addr, .base = st->pervertex_lds_bytes - 4);
   }
   nir_pop_if(b, if_gs_thread);
}

static void
emit_store_ngg_nogs_es_primitive_id(nir_builder *b, lower_ngg_nogs_state *st)
{
   nir_ssa_def *prim_id = NULL;

   if (b->shader->info.stage == MESA_SHADER_VERTEX) {
      /* LDS address where the primitive ID is stored */
      nir_ssa_def *thread_id_in_threadgroup = nir_load_local_invocation_index(b);
      nir_ssa_def *addr =
         pervertex_lds_addr(b, thread_id_in_threadgroup, st->pervertex_lds_bytes);

      /* Load primitive ID from LDS */
      prim_id = nir_load_shared(b, 1, 32, addr, .base = st->pervertex_lds_bytes - 4);
   } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
      /* Just use tess eval primitive ID, which is the same as the patch ID. */
      prim_id = nir_load_primitive_id(b);
   }

   nir_io_semantics io_sem = {
      .location = VARYING_SLOT_PRIMITIVE_ID,
      .num_slots = 1,
   };

   nir_store_output(b, prim_id, nir_imm_zero(b, 1, 32),
                    .base = io_sem.location,
                    .src_type = nir_type_uint32, .io_semantics = io_sem);
}

static void
store_var_components(nir_builder *b, nir_variable *var, nir_ssa_def *value,
                     unsigned component, unsigned writemask)
{
   /* component store */
   if (value->num_components != 4) {
      nir_ssa_def *undef = nir_ssa_undef(b, 1, value->bit_size);

      /* add undef component before and after value to form a vec4 */
      nir_ssa_def *comp[4];
      for (int i = 0; i < 4; i++) {
         comp[i] = (i >= component && i < component + value->num_components) ?
            nir_channel(b, value, i - component) : undef;
      }

      value = nir_vec(b, comp, 4);
      writemask <<= component;
   } else {
      /* if num_component==4, there should be no component offset */
      assert(component == 0);
   }

   nir_store_var(b, var, value, writemask);
}

static void
add_clipdist_bit(nir_builder *b, nir_ssa_def *dist, unsigned index, nir_variable *mask)
{
   nir_ssa_def *is_neg = nir_flt(b, dist, nir_imm_float(b, 0));
   nir_ssa_def *neg_mask = nir_ishl_imm(b, nir_b2i8(b, is_neg), index);
   neg_mask = nir_ior(b, neg_mask, nir_load_var(b, mask));
   nir_store_var(b, mask, neg_mask, 1);
}

static bool
remove_culling_shader_output(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* These are not allowed in VS / TES */
   assert(intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
          intrin->intrinsic != nir_intrinsic_load_per_vertex_input);

   /* We are only interested in output stores now */
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   b->cursor = nir_before_instr(instr);

   /* no indirect output */
   assert(nir_src_is_const(intrin->src[1]) && nir_src_as_uint(intrin->src[1]) == 0);

   unsigned writemask = nir_intrinsic_write_mask(intrin);
   unsigned component = nir_intrinsic_component(intrin);
   nir_ssa_def *store_val = intrin->src[0].ssa;

   /* Position output - store the value to a variable, remove output store */
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   switch (io_sem.location) {
   case VARYING_SLOT_POS:
      store_var_components(b, s->position_value_var, store_val, component, writemask);
      break;
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CLIP_DIST1: {
      unsigned base = io_sem.location == VARYING_SLOT_CLIP_DIST1 ? 4 : 0;
      base += component;

      /* valid clipdist component mask */
      unsigned mask = (s->clipdist_enable_mask >> base) & writemask;
      u_foreach_bit(i, mask) {
         add_clipdist_bit(b, nir_channel(b, store_val, i), base + i,
                          s->clipdist_neg_mask_var);
         s->has_clipdist = true;
      }
      break;
   }
   case VARYING_SLOT_CLIP_VERTEX:
      store_var_components(b, s->clip_vertex_var, store_val, component, writemask);
      break;
   default:
      break;
   }

   /* Remove all output stores */
   nir_instr_remove(instr);
   return true;
}

static void
remove_culling_shader_outputs(nir_shader *culling_shader, lower_ngg_nogs_state *nogs_state)
{
   nir_shader_instructions_pass(culling_shader, remove_culling_shader_output,
                                nir_metadata_block_index | nir_metadata_dominance, nogs_state);

   /* Remove dead code resulting from the deleted outputs. */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, culling_shader, nir_opt_dead_write_vars);
      NIR_PASS(progress, culling_shader, nir_opt_dce);
      NIR_PASS(progress, culling_shader, nir_opt_dead_cf);
   } while (progress);
}

static void
rewrite_uses_to_var(nir_builder *b, nir_ssa_def *old_def, nir_variable *replacement_var, unsigned replacement_var_channel)
{
   if (old_def->parent_instr->type == nir_instr_type_load_const)
      return;

   b->cursor = nir_after_instr(old_def->parent_instr);
   if (b->cursor.instr->type == nir_instr_type_phi)
      b->cursor = nir_after_phis(old_def->parent_instr->block);

   nir_ssa_def *pos_val_rep = nir_load_var(b, replacement_var);
   nir_ssa_def *replacement = nir_channel(b, pos_val_rep, replacement_var_channel);

   if (old_def->num_components > 1) {
      /* old_def uses a swizzled vector component.
       * There is no way to replace the uses of just a single vector component,
       * so instead create a new vector and replace all uses of the old vector.
       */
      nir_ssa_def *old_def_elements[NIR_MAX_VEC_COMPONENTS] = {0};
      for (unsigned j = 0; j < old_def->num_components; ++j)
         old_def_elements[j] = nir_channel(b, old_def, j);
      replacement = nir_vec(b, old_def_elements, old_def->num_components);
   }

   nir_ssa_def_rewrite_uses_after(old_def, replacement, replacement->parent_instr);
}

static bool
remove_extra_pos_output(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* These are not allowed in VS / TES */
   assert(intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
          intrin->intrinsic != nir_intrinsic_load_per_vertex_input);

   /* We are only interested in output stores now */
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   if (io_sem.location != VARYING_SLOT_POS)
      return false;

   b->cursor = nir_before_instr(instr);

   /* In case other outputs use what we calculated for pos,
    * try to avoid calculating it again by rewriting the usages
    * of the store components here.
    */
   nir_ssa_def *store_val = intrin->src[0].ssa;
   unsigned store_pos_component = nir_intrinsic_component(intrin);

   /* save the store base for re-construct store output instruction */
   s->position_store_base = nir_intrinsic_base(intrin);

   nir_instr_remove(instr);

   if (store_val->parent_instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(store_val->parent_instr);
      if (nir_op_is_vec(alu->op)) {
         /* Output store uses a vector, we can easily rewrite uses of each vector element. */

         unsigned num_vec_src = 0;
         if (alu->op == nir_op_mov)
            num_vec_src = 1;
         else if (alu->op == nir_op_vec2)
            num_vec_src = 2;
         else if (alu->op == nir_op_vec3)
            num_vec_src = 3;
         else if (alu->op == nir_op_vec4)
            num_vec_src = 4;
         assert(num_vec_src);

         /* Remember the current components whose uses we wish to replace.
          * This is needed because rewriting one source can affect the others too.
          */
         nir_ssa_def *vec_comps[NIR_MAX_VEC_COMPONENTS] = {0};
         for (unsigned i = 0; i < num_vec_src; i++)
            vec_comps[i] = alu->src[i].src.ssa;

         for (unsigned i = 0; i < num_vec_src; i++)
            rewrite_uses_to_var(b, vec_comps[i], s->position_value_var, store_pos_component + i);
      } else {
         rewrite_uses_to_var(b, store_val, s->position_value_var, store_pos_component);
      }
   } else {
      rewrite_uses_to_var(b, store_val, s->position_value_var, store_pos_component);
   }

   return true;
}

static void
remove_extra_pos_outputs(nir_shader *shader, lower_ngg_nogs_state *nogs_state)
{
   nir_shader_instructions_pass(shader, remove_extra_pos_output,
                                nir_metadata_block_index | nir_metadata_dominance,
                                nogs_state);
}

static bool
remove_compacted_arg(lower_ngg_nogs_state *state, nir_builder *b, unsigned idx)
{
   nir_instr *store_instr = state->compact_arg_stores[idx];
   if (!store_instr)
      return false;

   /* Simply remove the store. */
   nir_instr_remove(store_instr);

   /* Find the intrinsic that overwrites the shader arguments,
    * and change its corresponding source.
    * This will cause NIR's DCE to recognize the load and its phis as dead.
    */
   b->cursor = nir_before_instr(&state->overwrite_args->instr);
   nir_ssa_def *undef_arg = nir_ssa_undef(b, 1, 32);
   nir_ssa_def_rewrite_uses(state->overwrite_args->src[idx].ssa, undef_arg);

   state->compact_arg_stores[idx] = NULL;
   return true;
}

static bool
cleanup_culling_shader_after_dce(nir_shader *shader,
                                 nir_function_impl *function_impl,
                                 lower_ngg_nogs_state *state)
{
   bool uses_vs_vertex_id = false;
   bool uses_vs_instance_id = false;
   bool uses_tes_u = false;
   bool uses_tes_v = false;
   bool uses_tes_rel_patch_id = false;
   bool uses_tes_patch_id = false;

   bool progress = false;
   nir_builder b;
   nir_builder_init(&b, function_impl);

   nir_foreach_block_reverse_safe(block, function_impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_alloc_vertices_and_primitives_amd:
            goto cleanup_culling_shader_after_dce_done;
         case nir_intrinsic_load_vertex_id:
         case nir_intrinsic_load_vertex_id_zero_base:
            uses_vs_vertex_id = true;
            break;
         case nir_intrinsic_load_instance_id:
            uses_vs_instance_id = true;
            break;
         case nir_intrinsic_load_input:
            if (state->instance_rate_inputs &
                (1u << (nir_intrinsic_base(intrin) - VERT_ATTRIB_GENERIC0)))
               uses_vs_instance_id = true;
            else
               uses_vs_vertex_id = true;
            break;
         case nir_intrinsic_load_tess_coord:
            uses_tes_u = uses_tes_v = true;
            break;
         case nir_intrinsic_load_tess_rel_patch_id_amd:
            uses_tes_rel_patch_id = true;
            break;
         case nir_intrinsic_load_primitive_id:
            if (shader->info.stage == MESA_SHADER_TESS_EVAL)
               uses_tes_patch_id = true;
            break;
         default:
            break;
         }
      }
   }

   cleanup_culling_shader_after_dce_done:

   if (shader->info.stage == MESA_SHADER_VERTEX) {
      if (!uses_vs_vertex_id)
         progress |= remove_compacted_arg(state, &b, 0);
      if (!uses_vs_instance_id)
         progress |= remove_compacted_arg(state, &b, 1);
   } else if (shader->info.stage == MESA_SHADER_TESS_EVAL) {
      if (!uses_tes_u)
         progress |= remove_compacted_arg(state, &b, 0);
      if (!uses_tes_v)
         progress |= remove_compacted_arg(state, &b, 1);
      if (!uses_tes_rel_patch_id)
         progress |= remove_compacted_arg(state, &b, 2);
      if (!uses_tes_patch_id)
         progress |= remove_compacted_arg(state, &b, 3);
   }

   return progress;
}

/**
 * Perform vertex compaction after culling.
 *
 * 1. Repack surviving ES invocations (this determines which lane will export which vertex)
 * 2. Surviving ES vertex invocations store their data to LDS
 * 3. Emit GS_ALLOC_REQ
 * 4. Repacked invocations load the vertex data from LDS
 * 5. GS threads update their vertex indices
 */
static void
compact_vertices_after_culling(nir_builder *b,
                               lower_ngg_nogs_state *nogs_state,
                               nir_variable **repacked_arg_vars,
                               nir_variable **gs_vtxaddr_vars,
                               nir_ssa_def *invocation_index,
                               nir_ssa_def *es_vertex_lds_addr,
                               nir_ssa_def *es_exporter_tid,
                               nir_ssa_def *num_live_vertices_in_workgroup,
                               nir_ssa_def *fully_culled,
                               unsigned ngg_scratch_lds_base_addr,
                               unsigned pervertex_lds_bytes,
                               unsigned max_exported_args)
{
   nir_variable *es_accepted_var = nogs_state->es_accepted_var;
   nir_variable *gs_accepted_var = nogs_state->gs_accepted_var;
   nir_variable *position_value_var = nogs_state->position_value_var;
   nir_variable *prim_exp_arg_var = nogs_state->prim_exp_arg_var;

   nir_if *if_es_accepted = nir_push_if(b, nir_load_var(b, es_accepted_var));
   {
      nir_ssa_def *exporter_addr = pervertex_lds_addr(b, es_exporter_tid, pervertex_lds_bytes);

      /* Store the exporter thread's index to the LDS space of the current thread so GS threads can load it */
      nir_store_shared(b, nir_u2u8(b, es_exporter_tid), es_vertex_lds_addr, .base = lds_es_exporter_tid);

      /* Store the current thread's position output to the exporter thread's LDS space */
      nir_ssa_def *pos = nir_load_var(b, position_value_var);
      nir_store_shared(b, pos, exporter_addr, .base = lds_es_pos_x);

      /* Store the current thread's repackable arguments to the exporter thread's LDS space */
      for (unsigned i = 0; i < max_exported_args; ++i) {
         nir_ssa_def *arg_val = nir_load_var(b, repacked_arg_vars[i]);
         nir_intrinsic_instr *store = nir_store_shared(b, arg_val, exporter_addr, .base = lds_es_arg_0 + 4u * i);

         nogs_state->compact_arg_stores[i] = &store->instr;
      }
   }
   nir_pop_if(b, if_es_accepted);

   /* TODO: Consider adding a shortcut exit.
    * Waves that have no vertices and primitives left can s_endpgm right here.
    */

   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   nir_ssa_def *es_survived = nir_ilt(b, invocation_index, num_live_vertices_in_workgroup);
   nir_if *if_packed_es_thread = nir_push_if(b, es_survived);
   {
      /* Read position from the current ES thread's LDS space (written by the exported vertex's ES thread) */
      nir_ssa_def *exported_pos = nir_load_shared(b, 4, 32, es_vertex_lds_addr, .base = lds_es_pos_x);
      nir_store_var(b, position_value_var, exported_pos, 0xfu);

      /* Read the repacked arguments */
      for (unsigned i = 0; i < max_exported_args; ++i) {
         nir_ssa_def *arg_val = nir_load_shared(b, 1, 32, es_vertex_lds_addr, .base = lds_es_arg_0 + 4u * i);
         nir_store_var(b, repacked_arg_vars[i], arg_val, 0x1u);
      }
   }
   nir_push_else(b, if_packed_es_thread);
   {
      nir_store_var(b, position_value_var, nir_ssa_undef(b, 4, 32), 0xfu);
      for (unsigned i = 0; i < max_exported_args; ++i)
         nir_store_var(b, repacked_arg_vars[i], nir_ssa_undef(b, 1, 32), 0x1u);
   }
   nir_pop_if(b, if_packed_es_thread);

   nir_if *if_gs_accepted = nir_push_if(b, nir_load_var(b, gs_accepted_var));
   {
      nir_ssa_def *exporter_vtx_indices[3] = {0};

      /* Load the index of the ES threads that will export the current GS thread's vertices */
      for (unsigned v = 0; v < nogs_state->num_vertices_per_primitives; ++v) {
         nir_ssa_def *vtx_addr = nir_load_var(b, gs_vtxaddr_vars[v]);
         nir_ssa_def *exporter_vtx_idx = nir_load_shared(b, 1, 8, vtx_addr, .base = lds_es_exporter_tid);
         exporter_vtx_indices[v] = nir_u2u32(b, exporter_vtx_idx);
         nir_store_var(b, nogs_state->gs_vtx_indices_vars[v], exporter_vtx_indices[v], 0x1);
      }

      nir_ssa_def *prim_exp_arg =
         emit_pack_ngg_prim_exp_arg(b, nogs_state->num_vertices_per_primitives,
                                    exporter_vtx_indices, NULL, nogs_state->use_edgeflags);
      nir_store_var(b, prim_exp_arg_var, prim_exp_arg, 0x1u);
   }
   nir_pop_if(b, if_gs_accepted);

   nir_store_var(b, es_accepted_var, es_survived, 0x1u);
   nir_store_var(b, gs_accepted_var, nir_bcsel(b, fully_culled, nir_imm_false(b), nir_has_input_primitive_amd(b)), 0x1u);
}

static void
analyze_shader_before_culling_walk(nir_ssa_def *ssa,
                                   uint8_t flag,
                                   lower_ngg_nogs_state *nogs_state)
{
   nir_instr *instr = ssa->parent_instr;
   uint8_t old_pass_flags = instr->pass_flags;
   instr->pass_flags |= flag;

   if (instr->pass_flags == old_pass_flags)
      return; /* Already visited. */

   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      /* VS input loads and SSBO loads are actually VRAM reads on AMD HW. */
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_input: {
         nir_io_semantics in_io_sem = nir_intrinsic_io_semantics(intrin);
         uint64_t in_mask = UINT64_C(1) << (uint64_t) in_io_sem.location;
         if (instr->pass_flags & nggc_passflag_used_by_pos)
            nogs_state->inputs_needed_by_pos |= in_mask;
         else if (instr->pass_flags & nggc_passflag_used_by_other)
            nogs_state->inputs_needed_by_others |= in_mask;
         break;
      }
      default:
         break;
      }

      break;
   }
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      unsigned num_srcs = nir_op_infos[alu->op].num_inputs;

      for (unsigned i = 0; i < num_srcs; ++i) {
         analyze_shader_before_culling_walk(alu->src[i].src.ssa, flag, nogs_state);
      }

      break;
   }
   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_foreach_phi_src_safe(phi_src, phi) {
         analyze_shader_before_culling_walk(phi_src->src.ssa, flag, nogs_state);
      }

      break;
   }
   default:
      break;
   }
}

static void
analyze_shader_before_culling(nir_shader *shader, lower_ngg_nogs_state *nogs_state)
{
   nir_foreach_function(func, shader) {
      nir_foreach_block(block, func->impl) {
         nir_foreach_instr(instr, block) {
            instr->pass_flags = 0;

            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_store_output)
               continue;

            nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
            nir_ssa_def *store_val = intrin->src[0].ssa;
            uint8_t flag = io_sem.location == VARYING_SLOT_POS ? nggc_passflag_used_by_pos : nggc_passflag_used_by_other;
            analyze_shader_before_culling_walk(store_val, flag, nogs_state);
         }
      }
   }
}

/**
 * Save the reusable SSA definitions to variables so that the
 * bottom shader part can reuse them from the top part.
 *
 * 1. We create a new function temporary variable for reusables,
 *    and insert a store+load.
 * 2. The shader is cloned (the top part is created), then the
 *    control flow is reinserted (for the bottom part.)
 * 3. For reusables, we delete the variable stores from the
 *    bottom part. This will make them use the variables from
 *    the top part and DCE the redundant instructions.
 */
static void
save_reusable_variables(nir_builder *b, lower_ngg_nogs_state *nogs_state)
{
   ASSERTED int vec_ok = u_vector_init(&nogs_state->saved_uniforms, 4, sizeof(saved_uniform));
   assert(vec_ok);

   nir_block *block = nir_start_block(b->impl);
   while (block) {
      /* Process the instructions in the current block. */
      nir_foreach_instr_safe(instr, block) {
         /* Find instructions whose SSA definitions are used by both
          * the top and bottom parts of the shader (before and after culling).
          * Only in this case, it makes sense for the bottom part
          * to try to reuse these from the top part.
          */
         if ((instr->pass_flags & nggc_passflag_used_by_both) != nggc_passflag_used_by_both)
            continue;

         /* Determine if we can reuse the current SSA value.
          * When vertex compaction is used, it is possible that the same shader invocation
          * processes a different vertex in the top and bottom part of the shader.
          * Therefore, we only reuse uniform values.
          */
         nir_ssa_def *ssa = NULL;
         switch (instr->type) {
         case nir_instr_type_alu: {
            nir_alu_instr *alu = nir_instr_as_alu(instr);
            if (alu->dest.dest.ssa.divergent)
               continue;
            /* Ignore uniform floats because they regress VGPR usage too much */
            if (nir_op_infos[alu->op].output_type & nir_type_float)
               continue;
            ssa = &alu->dest.dest.ssa;
            break;
         }
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (!nir_intrinsic_can_reorder(intrin) ||
                !nir_intrinsic_infos[intrin->intrinsic].has_dest ||
                intrin->dest.ssa.divergent)
               continue;
            ssa = &intrin->dest.ssa;
            break;
         }
         case nir_instr_type_phi: {
            nir_phi_instr *phi = nir_instr_as_phi(instr);
            if (phi->dest.ssa.divergent)
               continue;
            ssa = &phi->dest.ssa;
            break;
         }
         default:
            continue;
         }

         assert(ssa);

         /* Determine a suitable type for the SSA value. */
         enum glsl_base_type base_type = GLSL_TYPE_UINT;
         switch (ssa->bit_size) {
         case 8: base_type = GLSL_TYPE_UINT8; break;
         case 16: base_type = GLSL_TYPE_UINT16; break;
         case 32: base_type = GLSL_TYPE_UINT; break;
         case 64: base_type = GLSL_TYPE_UINT64; break;
         default: continue;
         }

         const struct glsl_type *t = ssa->num_components == 1
                                     ? glsl_scalar_type(base_type)
                                     : glsl_vector_type(base_type, ssa->num_components);

         saved_uniform *saved = (saved_uniform *) u_vector_add(&nogs_state->saved_uniforms);
         assert(saved);

         /* Create a new NIR variable where we store the reusable value.
          * Then, we reload the variable and replace the uses of the value
          * with the reloaded variable.
          */
         saved->var = nir_local_variable_create(b->impl, t, NULL);
         saved->ssa = ssa;

         b->cursor = instr->type == nir_instr_type_phi
                     ? nir_after_instr_and_phis(instr)
                     : nir_after_instr(instr);
         nir_store_var(b, saved->var, saved->ssa, BITFIELD_MASK(ssa->num_components));
         nir_ssa_def *reloaded = nir_load_var(b, saved->var);
         nir_ssa_def_rewrite_uses_after(ssa, reloaded, reloaded->parent_instr);
      }

      /* Look at the next CF node. */
      nir_cf_node *next_cf_node = nir_cf_node_next(&block->cf_node);
      if (next_cf_node) {
         /* It makes no sense to try to reuse things from within loops. */
         bool next_is_loop = next_cf_node->type == nir_cf_node_loop;

         /* Don't reuse if we're in divergent control flow.
          *
          * Thanks to vertex repacking, the same shader invocation may process a different vertex
          * in the top and bottom part, and it's even possible that this different vertex was initially
          * processed in a different wave. So the two parts may take a different divergent code path.
          * Therefore, these variables in divergent control flow may stay undefined.
          *
          * Note that this problem doesn't exist if vertices are not repacked or if the
          * workgroup only has a single wave.
          */
         bool next_is_divergent_if =
            next_cf_node->type == nir_cf_node_if &&
            nir_cf_node_as_if(next_cf_node)->condition.ssa->divergent;

         if (next_is_loop || next_is_divergent_if) {
            block = nir_cf_node_cf_tree_next(next_cf_node);
            continue;
         }
      }

      /* Go to the next block. */
      block = nir_block_cf_tree_next(block);
   }
}

/**
 * Reuses suitable variables from the top part of the shader,
 * by deleting their stores from the bottom part.
 */
static void
apply_reusable_variables(nir_builder *b, lower_ngg_nogs_state *nogs_state)
{
   if (!u_vector_length(&nogs_state->saved_uniforms)) {
      u_vector_finish(&nogs_state->saved_uniforms);
      return;
   }

   nir_foreach_block_reverse_safe(block, b->impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         /* When we found any of these intrinsics, it means
          * we reached the top part and we must stop.
          */
         if (intrin->intrinsic == nir_intrinsic_alloc_vertices_and_primitives_amd)
            goto done;

         if (intrin->intrinsic != nir_intrinsic_store_deref)
            continue;
         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         if (deref->deref_type != nir_deref_type_var)
            continue;

         saved_uniform *saved;
         u_vector_foreach(saved, &nogs_state->saved_uniforms) {
            if (saved->var == deref->var) {
               nir_instr_remove(instr);
            }
         }
      }
   }

   done:
   u_vector_finish(&nogs_state->saved_uniforms);
}

static void
cull_primitive_accepted(nir_builder *b, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *)state;

   nir_store_var(b, s->gs_accepted_var, nir_imm_true(b), 0x1u);

   /* Store the accepted state to LDS for ES threads */
   for (unsigned vtx = 0; vtx < s->num_vertices_per_primitives; ++vtx)
      nir_store_shared(b, nir_imm_intN_t(b, 1, 8), s->vtx_addr[vtx], .base = lds_es_vertex_accepted);
}

static void
clipdist_culling_es_part(nir_builder *b, lower_ngg_nogs_state *nogs_state,
                         nir_ssa_def *es_vertex_lds_addr)
{
   /* no gl_ClipDistance used but we have user defined clip plane */
   if (nogs_state->user_clip_plane_enable_mask && !nogs_state->has_clipdist) {
      /* use gl_ClipVertex if defined */
      nir_variable *clip_vertex_var =
         b->shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_CLIP_VERTEX) ?
         nogs_state->clip_vertex_var : nogs_state->position_value_var;
      nir_ssa_def *clip_vertex = nir_load_var(b, clip_vertex_var);

      /* clip against user defined clip planes */
      for (unsigned i = 0; i < 8; i++) {
         if (!(nogs_state->user_clip_plane_enable_mask & BITFIELD_BIT(i)))
            continue;

         nir_ssa_def *plane = nir_load_user_clip_plane(b, .ucp_id = i);
         nir_ssa_def *dist = nir_fdot(b, clip_vertex, plane);
         add_clipdist_bit(b, dist, i, nogs_state->clipdist_neg_mask_var);
      }

      nogs_state->has_clipdist = true;
   }

   /* store clipdist_neg_mask to LDS for culling latter in gs thread */
   if (nogs_state->has_clipdist) {
      nir_ssa_def *mask = nir_load_var(b, nogs_state->clipdist_neg_mask_var);
      nir_store_shared(b, mask, es_vertex_lds_addr, .base = lds_es_clipdist_neg_mask);
   }
}

static void
add_deferred_attribute_culling(nir_builder *b, nir_cf_list *original_extracted_cf, lower_ngg_nogs_state *nogs_state)
{
   bool uses_instance_id = BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   bool uses_tess_primitive_id = BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   unsigned max_exported_args = b->shader->info.stage == MESA_SHADER_VERTEX ? 2 : 4;
   if (b->shader->info.stage == MESA_SHADER_VERTEX && !uses_instance_id)
      max_exported_args--;
   else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL && !uses_tess_primitive_id)
      max_exported_args--;

   unsigned pervertex_lds_bytes = lds_es_arg_0 + max_exported_args * 4u;
   unsigned total_es_lds_bytes = pervertex_lds_bytes * nogs_state->max_es_num_vertices;
   unsigned max_num_waves = nogs_state->max_num_waves;
   unsigned ngg_scratch_lds_base_addr = ALIGN(total_es_lds_bytes, 8u);
   unsigned ngg_scratch_lds_bytes = ALIGN(max_num_waves, 4u);
   nogs_state->total_lds_bytes = MAX2(nogs_state->total_lds_bytes,
                                      ngg_scratch_lds_base_addr + ngg_scratch_lds_bytes);

   nir_function_impl *impl = nir_shader_get_entrypoint(b->shader);

   /* Create some helper variables. */
   nir_variable *position_value_var = nogs_state->position_value_var;
   nir_variable *prim_exp_arg_var = nogs_state->prim_exp_arg_var;
   nir_variable *gs_accepted_var = nogs_state->gs_accepted_var;
   nir_variable *es_accepted_var = nogs_state->es_accepted_var;
   nir_variable *gs_vtxaddr_vars[3] = {
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx0_addr"),
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx1_addr"),
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx2_addr"),
   };
   nir_variable *repacked_arg_vars[4] = {
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_arg_0"),
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_arg_1"),
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_arg_2"),
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_arg_3"),
   };

   if (nogs_state->clipdist_enable_mask || nogs_state->user_clip_plane_enable_mask) {
      nogs_state->clip_vertex_var =
         nir_local_variable_create(impl, glsl_vec4_type(), "clip_vertex");
      nogs_state->clipdist_neg_mask_var =
         nir_local_variable_create(impl, glsl_uint8_t_type(), "clipdist_neg_mask");
   }

   /* Top part of the culling shader (aka. position shader part)
    *
    * We clone the full ES shader and emit it here, but we only really care
    * about its position output, so we delete every other output from this part.
    * The position output is stored into a temporary variable, and reloaded later.
    */

   b->cursor = nir_before_cf_list(&impl->body);

   nir_ssa_def *es_thread = nir_has_input_vertex_amd(b);
   nir_if *if_es_thread = nir_push_if(b, es_thread);
   {
      /* Initialize the position output variable to zeroes, in case not all VS/TES invocations store the output.
       * The spec doesn't require it, but we use (0, 0, 0, 1) because some games rely on that.
       */
      nir_store_var(b, position_value_var, nir_imm_vec4(b, 0.0f, 0.0f, 0.0f, 1.0f), 0xfu);

      /* Now reinsert a clone of the shader code */
      struct hash_table *remap_table = _mesa_pointer_hash_table_create(NULL);
      nir_cf_list_clone_and_reinsert(original_extracted_cf, &if_es_thread->cf_node, b->cursor, remap_table);
      _mesa_hash_table_destroy(remap_table, NULL);
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);

      /* Remember the current thread's shader arguments */
      if (b->shader->info.stage == MESA_SHADER_VERTEX) {
         nir_store_var(b, repacked_arg_vars[0], nir_load_vertex_id_zero_base(b), 0x1u);
         if (uses_instance_id)
            nir_store_var(b, repacked_arg_vars[1], nir_load_instance_id(b), 0x1u);
      } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         nir_ssa_def *tess_coord = nir_load_tess_coord(b);
         nir_store_var(b, repacked_arg_vars[0], nir_channel(b, tess_coord, 0), 0x1u);
         nir_store_var(b, repacked_arg_vars[1], nir_channel(b, tess_coord, 1), 0x1u);
         nir_store_var(b, repacked_arg_vars[2], nir_load_tess_rel_patch_id_amd(b), 0x1u);
         if (uses_tess_primitive_id)
            nir_store_var(b, repacked_arg_vars[3], nir_load_primitive_id(b), 0x1u);
      } else {
         unreachable("Should be VS or TES.");
      }
   }
   nir_pop_if(b, if_es_thread);

   nir_store_var(b, es_accepted_var, es_thread, 0x1u);
   nir_store_var(b, gs_accepted_var, nir_has_input_primitive_amd(b), 0x1u);

   /* Remove all non-position outputs, and put the position output into the variable. */
   nir_metadata_preserve(impl, nir_metadata_none);
   remove_culling_shader_outputs(b->shader, nogs_state);
   b->cursor = nir_after_cf_list(&impl->body);

   /* Run culling algorithms if culling is enabled.
    *
    * NGG culling can be enabled or disabled in runtime.
    * This is determined by a SGPR shader argument which is acccessed
    * by the following NIR intrinsic.
    */

   nir_if *if_cull_en = nir_push_if(b, nir_load_cull_any_enabled_amd(b));
   {
      nir_ssa_def *invocation_index = nir_load_local_invocation_index(b);
      nir_ssa_def *es_vertex_lds_addr = pervertex_lds_addr(b, invocation_index, pervertex_lds_bytes);

      /* ES invocations store their vertex data to LDS for GS threads to read. */
      if_es_thread = nir_push_if(b, nir_has_input_vertex_amd(b));
      {
         /* Store position components that are relevant to culling in LDS */
         nir_ssa_def *pre_cull_pos = nir_load_var(b, position_value_var);
         nir_ssa_def *pre_cull_w = nir_channel(b, pre_cull_pos, 3);
         nir_store_shared(b, pre_cull_w, es_vertex_lds_addr, .base = lds_es_pos_w);
         nir_ssa_def *pre_cull_x_div_w = nir_fdiv(b, nir_channel(b, pre_cull_pos, 0), pre_cull_w);
         nir_ssa_def *pre_cull_y_div_w = nir_fdiv(b, nir_channel(b, pre_cull_pos, 1), pre_cull_w);
         nir_store_shared(b, nir_vec2(b, pre_cull_x_div_w, pre_cull_y_div_w), es_vertex_lds_addr, .base = lds_es_pos_x);

         /* Clear out the ES accepted flag in LDS */
         nir_store_shared(b, nir_imm_zero(b, 1, 8), es_vertex_lds_addr, .align_mul = 4, .base = lds_es_vertex_accepted);

         /* For clipdist culling */
         clipdist_culling_es_part(b, nogs_state, es_vertex_lds_addr);
      }
      nir_pop_if(b, if_es_thread);

      nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                            .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      nir_store_var(b, gs_accepted_var, nir_imm_bool(b, false), 0x1u);
      nir_store_var(b, prim_exp_arg_var, nir_imm_int(b, 1u << 31), 0x1u);

      /* GS invocations load the vertex data and perform the culling. */
      nir_if *if_gs_thread = nir_push_if(b, nir_has_input_primitive_amd(b));
      {
         /* Load vertex indices from input VGPRs */
         nir_ssa_def *vtx_idx[3] = {0};
         for (unsigned vertex = 0; vertex < nogs_state->num_vertices_per_primitives; ++vertex)
            vtx_idx[vertex] = nir_load_var(b, nogs_state->gs_vtx_indices_vars[vertex]);

         nir_ssa_def *pos[3][4] = {0};

         /* Load W positions of vertices first because the culling code will use these first */
         for (unsigned vtx = 0; vtx < nogs_state->num_vertices_per_primitives; ++vtx) {
            nogs_state->vtx_addr[vtx] = pervertex_lds_addr(b, vtx_idx[vtx], pervertex_lds_bytes);
            pos[vtx][3] = nir_load_shared(b, 1, 32, nogs_state->vtx_addr[vtx], .base = lds_es_pos_w);
            nir_store_var(b, gs_vtxaddr_vars[vtx], nogs_state->vtx_addr[vtx], 0x1u);
         }

         /* Load the X/W, Y/W positions of vertices */
         for (unsigned vtx = 0; vtx < nogs_state->num_vertices_per_primitives; ++vtx) {
            nir_ssa_def *xy = nir_load_shared(b, 2, 32, nogs_state->vtx_addr[vtx], .base = lds_es_pos_x);
            pos[vtx][0] = nir_channel(b, xy, 0);
            pos[vtx][1] = nir_channel(b, xy, 1);
         }

         nir_ssa_def *accepted_by_clipdist;
         if (nogs_state->has_clipdist) {
            nir_ssa_def *clipdist_neg_mask = nir_imm_intN_t(b, 0xff, 8);
            for (unsigned vtx = 0; vtx < nogs_state->num_vertices_per_primitives; ++vtx) {
               nir_ssa_def *mask =
                  nir_load_shared(b, 1, 8, nogs_state->vtx_addr[vtx],
                                  .base = lds_es_clipdist_neg_mask);
               clipdist_neg_mask = nir_iand(b, clipdist_neg_mask, mask);
            }
            /* primitive is culled if any plane's clipdist of all vertices are negative */
            accepted_by_clipdist = nir_ieq_imm(b, clipdist_neg_mask, 0);
         } else {
            accepted_by_clipdist = nir_imm_bool(b, true);
         }

         /* See if the current primitive is accepted */
         ac_nir_cull_primitive(b, accepted_by_clipdist, pos,
                               nogs_state->num_vertices_per_primitives,
                               cull_primitive_accepted, nogs_state);
      }
      nir_pop_if(b, if_gs_thread);

      nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                            .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      nir_store_var(b, es_accepted_var, nir_imm_bool(b, false), 0x1u);

      /* ES invocations load their accepted flag from LDS. */
      if_es_thread = nir_push_if(b, nir_has_input_vertex_amd(b));
      {
         nir_ssa_def *accepted = nir_load_shared(b, 1, 8u, es_vertex_lds_addr, .base = lds_es_vertex_accepted, .align_mul = 4u);
         nir_ssa_def *accepted_bool = nir_ine(b, accepted, nir_imm_intN_t(b, 0, 8));
         nir_store_var(b, es_accepted_var, accepted_bool, 0x1u);
      }
      nir_pop_if(b, if_es_thread);

      nir_ssa_def *es_accepted = nir_load_var(b, es_accepted_var);

      /* Repack the vertices that survived the culling. */
      wg_repack_result rep = repack_invocations_in_workgroup(b, es_accepted, ngg_scratch_lds_base_addr,
                                                            nogs_state->max_num_waves, nogs_state->wave_size);
      nir_ssa_def *num_live_vertices_in_workgroup = rep.num_repacked_invocations;
      nir_ssa_def *es_exporter_tid = rep.repacked_invocation_index;

      /* If all vertices are culled, set primitive count to 0 as well. */
      nir_ssa_def *num_exported_prims = nir_load_workgroup_num_input_primitives_amd(b);
      nir_ssa_def *fully_culled = nir_ieq_imm(b, num_live_vertices_in_workgroup, 0u);
      num_exported_prims = nir_bcsel(b, fully_culled, nir_imm_int(b, 0u), num_exported_prims);

      nir_if *if_wave_0 = nir_push_if(b, nir_ieq(b, nir_load_subgroup_id(b), nir_imm_int(b, 0)));
      {
         /* Tell the final vertex and primitive count to the HW. */
         nir_alloc_vertices_and_primitives_amd(b, num_live_vertices_in_workgroup, num_exported_prims);
      }
      nir_pop_if(b, if_wave_0);

      /* Vertex compaction. */
      compact_vertices_after_culling(b, nogs_state,
                                     repacked_arg_vars, gs_vtxaddr_vars,
                                     invocation_index, es_vertex_lds_addr,
                                     es_exporter_tid, num_live_vertices_in_workgroup, fully_culled,
                                     ngg_scratch_lds_base_addr, pervertex_lds_bytes, max_exported_args);
   }
   nir_push_else(b, if_cull_en);
   {
      /* When culling is disabled, we do the same as we would without culling. */
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq(b, nir_load_subgroup_id(b), nir_imm_int(b, 0)));
      {
         nir_ssa_def *vtx_cnt = nir_load_workgroup_num_input_vertices_amd(b);
         nir_ssa_def *prim_cnt = nir_load_workgroup_num_input_primitives_amd(b);
         nir_alloc_vertices_and_primitives_amd(b, vtx_cnt, prim_cnt);
      }
      nir_pop_if(b, if_wave_0);
      nir_store_var(b, prim_exp_arg_var, emit_ngg_nogs_prim_exp_arg(b, nogs_state), 0x1u);
   }
   nir_pop_if(b, if_cull_en);

   /* Update shader arguments.
    *
    * The registers which hold information about the subgroup's
    * vertices and primitives are updated here, so the rest of the shader
    * doesn't need to worry about the culling.
    *
    * These "overwrite" intrinsics must be at top level control flow,
    * otherwise they can mess up the backend (eg. ACO's SSA).
    *
    * TODO:
    * A cleaner solution would be to simply replace all usages of these args
    * with the load of the variables.
    * However, this wouldn't work right now because the backend uses the arguments
    * for purposes not expressed in NIR, eg. VS input loads, etc.
    * This can change if VS input loads and other stuff are lowered to eg. load_buffer_amd.
    */

   if (b->shader->info.stage == MESA_SHADER_VERTEX)
      nogs_state->overwrite_args =
         nir_overwrite_vs_arguments_amd(b,
            nir_load_var(b, repacked_arg_vars[0]), nir_load_var(b, repacked_arg_vars[1]));
   else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL)
      nogs_state->overwrite_args =
         nir_overwrite_tes_arguments_amd(b,
            nir_load_var(b, repacked_arg_vars[0]), nir_load_var(b, repacked_arg_vars[1]),
            nir_load_var(b, repacked_arg_vars[2]), nir_load_var(b, repacked_arg_vars[3]));
   else
      unreachable("Should be VS or TES.");
}

static bool
do_ngg_nogs_store_output_to_lds(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_nogs_state *st = (lower_ngg_nogs_state *)state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned component = nir_intrinsic_component(intrin);
   unsigned write_mask = nir_instr_xfb_write_mask(intrin) >> component;
   if (!write_mask)
      return false;

   b->cursor = nir_before_instr(instr);

   unsigned base_offset = nir_src_as_uint(intrin->src[1]);
   unsigned location = nir_intrinsic_io_semantics(intrin).location + base_offset;
   unsigned packed_location =
      util_bitcount64(b->shader->info.outputs_written & BITFIELD64_MASK(location));
   unsigned offset = packed_location * 16 + component * 4;

   nir_ssa_def *tid = nir_load_local_invocation_index(b);
   nir_ssa_def *addr = pervertex_lds_addr(b, tid, st->pervertex_lds_bytes);

   nir_ssa_def *store_val = intrin->src[0].ssa;
   nir_store_shared(b, store_val, addr, .base = offset, .write_mask = write_mask);

   return true;
}

static void
ngg_nogs_store_all_outputs_to_lds(nir_shader *shader, lower_ngg_nogs_state *st)
{
   nir_shader_instructions_pass(shader, do_ngg_nogs_store_output_to_lds,
                                nir_metadata_block_index | nir_metadata_dominance, st);
}

static void
ngg_build_streamout_buffer_info(nir_builder *b,
                                nir_xfb_info *info,
                                unsigned scratch_base,
                                nir_ssa_def *tid_in_tg,
                                nir_ssa_def *gen_prim[4],
                                nir_ssa_def *prim_stride_ret[4],
                                nir_ssa_def *so_buffer_ret[4],
                                nir_ssa_def *buffer_offsets_ret[4],
                                nir_ssa_def *emit_prim_ret[4])
{
   /* For radeonsi which pass this value by arg when VS. Streamout need accurate
    * num-vert-per-prim for writing correct amount of data to buffer.
    */
   nir_ssa_def *num_vert_per_prim = nir_load_num_vertices_per_primitive_amd(b);
   for (unsigned buffer = 0; buffer < 4; buffer++) {
      if (!(info->buffers_written & BITFIELD_BIT(buffer)))
         continue;

      assert(info->buffers[buffer].stride);

      prim_stride_ret[buffer] =
         nir_imul_imm(b, num_vert_per_prim, info->buffers[buffer].stride * 4);
      so_buffer_ret[buffer] = nir_load_streamout_buffer_amd(b, .base = buffer);
   }

   nir_if *if_invocation_0 = nir_push_if(b, nir_ieq_imm(b, tid_in_tg, 0));
   {
      nir_ssa_def *workgroup_buffer_sizes[4];
      for (unsigned buffer = 0; buffer < 4; buffer++) {
         if (info->buffers_written & BITFIELD_BIT(buffer)) {
            nir_ssa_def *buffer_size = nir_channel(b, so_buffer_ret[buffer], 2);
            /* In radeonsi, we may not know if a feedback buffer has been bound when
             * compile time, so have to check buffer size in runtime to disable the
             * GDS update for unbind buffer to prevent the case that previous draw
             * compiled with streamout but does not bind feedback buffer miss update
             * GDS which will affect current draw's streamout.
             */
            nir_ssa_def *buffer_valid = nir_ine_imm(b, buffer_size, 0);
            nir_ssa_def *inc_buffer_size =
               nir_imul(b, gen_prim[info->buffer_to_stream[buffer]], prim_stride_ret[buffer]);
            workgroup_buffer_sizes[buffer] =
               nir_bcsel(b, buffer_valid, inc_buffer_size, nir_imm_int(b, 0));
         } else
            workgroup_buffer_sizes[buffer] = nir_ssa_undef(b, 1, 32);
      }

      nir_ssa_def *ordered_id = nir_load_ordered_id_amd(b);
      /* Get current global offset of buffer and increase by amount of
       * workgroup buffer size. This is an ordered operation sorted by
       * ordered_id; Each buffer info is in a channel of a vec4.
       */
      nir_ssa_def *buffer_offsets =
         nir_ordered_xfb_counter_add_amd(b, ordered_id, nir_vec(b, workgroup_buffer_sizes, 4),
                                         /* mask of buffers to update */
                                         .write_mask = info->buffers_written);

      nir_ssa_def *emit_prim[4];
      memcpy(emit_prim, gen_prim, 4 * sizeof(nir_ssa_def *));

      for (unsigned buffer = 0; buffer < 4; buffer++) {
         if (!(info->buffers_written & BITFIELD_BIT(buffer)))
            continue;

         nir_ssa_def *buffer_size = nir_channel(b, so_buffer_ret[buffer], 2);
         nir_ssa_def *buffer_offset = nir_channel(b, buffer_offsets, buffer);
         nir_ssa_def *remain_size = nir_isub(b, buffer_size, buffer_offset);
         nir_ssa_def *remain_prim = nir_idiv(b, remain_size, prim_stride_ret[buffer]);
         nir_ssa_def *overflow = nir_ilt(b, buffer_size, buffer_offset);

         unsigned stream = info->buffer_to_stream[buffer];
         /* when previous workgroup overflow, we can't emit any primitive */
         emit_prim[stream] = nir_bcsel(
            b, overflow, nir_imm_int(b, 0),
            /* we can emit part primitives, limited by smallest buffer */
            nir_imin(b, emit_prim[stream], remain_prim));

         /* Save to LDS for being accessed by other waves in this workgroup. */
         nir_store_shared(b, buffer_offset, nir_imm_int(b, buffer * 4),
                          .base = scratch_base);
      }

      /* No need to fixup the global buffer offset once we overflowed,
       * because following workgroups overflow for sure.
       */

      /* Save to LDS for being accessed by other waves in this workgroup. */
      for (unsigned stream = 0; stream < 4; stream++) {
         if (!(info->streams_written & BITFIELD_BIT(stream)))
            continue;

         nir_store_shared(b, emit_prim[stream], nir_imm_int(b, stream * 4),
                          .base = scratch_base + 16);
      }
   }
   nir_pop_if(b, if_invocation_0);

   nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                      .memory_scope = NIR_SCOPE_WORKGROUP,
                      .memory_semantics = NIR_MEMORY_ACQ_REL,
                      .memory_modes = nir_var_mem_shared);

   /* Fetch the per-buffer offsets in all waves. */
   for (unsigned buffer = 0; buffer < 4; buffer++) {
      if (!(info->buffers_written & BITFIELD_BIT(buffer)))
         continue;

      buffer_offsets_ret[buffer] =
         nir_load_shared(b, 1, 32, nir_imm_int(b, buffer * 4), .base = scratch_base);
   }

   /* Fetch the per-stream emit prim in all waves. */
   for (unsigned stream = 0; stream < 4; stream++) {
      if (!(info->streams_written & BITFIELD_BIT(stream)))
            continue;

      emit_prim_ret[stream] =
         nir_load_shared(b, 1, 32, nir_imm_int(b, stream * 4), .base = scratch_base + 16);
   }
}

static void
ngg_build_streamout_vertex(nir_builder *b, nir_xfb_info *info,
                           unsigned stream, int *slot_to_register,
                           nir_ssa_def *so_buffer[4], nir_ssa_def *buffer_offsets[4],
                           nir_ssa_def *vtx_buffer_idx, nir_ssa_def *vtx_lds_addr)
{
   nir_ssa_def *vtx_buffer_offsets[4];
   for (unsigned buffer = 0; buffer < 4; buffer++) {
      if (!(info->buffers_written & BITFIELD_BIT(buffer)))
         continue;

      nir_ssa_def *offset = nir_imul_imm(b, vtx_buffer_idx, info->buffers[buffer].stride * 4);
      vtx_buffer_offsets[buffer] = nir_iadd(b, buffer_offsets[buffer], offset);
   }

   for (unsigned i = 0; i < info->output_count; i++) {
      nir_xfb_output_info *out = info->outputs + i;
      if (!out->component_mask || info->buffer_to_stream[out->buffer] != stream)
         continue;

      unsigned base = slot_to_register[out->location];
      unsigned offset = (base * 4 + out->component_offset) * 4;
      unsigned count = util_bitcount(out->component_mask);
      /* component_mask is constructed like this, see nir_gather_xfb_info_from_intrinsics() */
      assert(u_bit_consecutive(out->component_offset, count) == out->component_mask);

      nir_ssa_def *out_data =
         nir_load_shared(b, count, 32, vtx_lds_addr, .base = offset);

      nir_store_buffer_amd(b, out_data, so_buffer[out->buffer],
                           vtx_buffer_offsets[out->buffer],
                           nir_imm_int(b, 0),
                           .base = out->offset,
                           .slc_amd = true);
   }
}

static void
ngg_nogs_build_streamout(nir_builder *b, lower_ngg_nogs_state *s)
{
   int slot_to_register[NUM_TOTAL_VARYING_SLOTS];
   nir_xfb_info *info = nir_gather_xfb_info_from_intrinsics(b->shader, slot_to_register);
   if (unlikely(!info)) {
      s->streamout_enabled = false;
      return;
   }

   unsigned total_es_lds_bytes = s->pervertex_lds_bytes * s->max_es_num_vertices;
   unsigned scratch_base = ALIGN(total_es_lds_bytes, 8u);
   /* 4 dwords for 4 streamout buffer offset, 1 dword for emit prim count */
   unsigned scratch_size = 20;
   s->total_lds_bytes = MAX2(s->total_lds_bytes, scratch_base + scratch_size);

   /* Get global buffer offset where this workgroup will stream out data to. */
   nir_ssa_def *generated_prim = nir_load_workgroup_num_input_primitives_amd(b);
   nir_ssa_def *gen_prim_per_stream[4] = {generated_prim, 0, 0, 0};
   nir_ssa_def *emit_prim_per_stream[4] = {0};
   nir_ssa_def *buffer_offsets[4] = {0};
   nir_ssa_def *so_buffer[4] = {0};
   nir_ssa_def *prim_stride[4] = {0};
   nir_ssa_def *tid_in_tg = nir_load_local_invocation_index(b);
   ngg_build_streamout_buffer_info(b, info, scratch_base, tid_in_tg,
                                   gen_prim_per_stream, prim_stride,
                                   so_buffer, buffer_offsets,
                                   emit_prim_per_stream);

   /* Write out primitive data */
   nir_if *if_emit = nir_push_if(b, nir_ilt(b, tid_in_tg, emit_prim_per_stream[0]));
   {
      unsigned vtx_lds_stride = (b->shader->num_outputs * 4 + 1) * 4;
      nir_ssa_def *num_vert_per_prim = nir_load_num_vertices_per_primitive_amd(b);
      nir_ssa_def *vtx_buffer_idx = nir_imul(b, tid_in_tg, num_vert_per_prim);

      for (unsigned i = 0; i < s->num_vertices_per_primitives; i++) {
         nir_if *if_valid_vertex =
            nir_push_if(b, nir_ilt(b, nir_imm_int(b, i), num_vert_per_prim));
         {
            nir_ssa_def *vtx_lds_idx = nir_load_var(b, s->gs_vtx_indices_vars[i]);
            nir_ssa_def *vtx_lds_addr = pervertex_lds_addr(b, vtx_lds_idx, vtx_lds_stride);
            ngg_build_streamout_vertex(b, info, 0, slot_to_register,
                                       so_buffer, buffer_offsets,
                                       nir_iadd_imm(b, vtx_buffer_idx, i),
                                       vtx_lds_addr);
         }
         nir_pop_if(b, if_valid_vertex);
      }
   }
   nir_pop_if(b, if_emit);

   /* Wait streamout memory ops done before export primitive, otherwise it
    * may not finish when shader ends.
    *
    * If a shader has no param exports, rasterization can start before
    * the shader finishes and thus memory stores might not finish before
    * the pixel shader starts.
    *
    * TODO: we only need this when no param exports.
    *
    * TODO: not sure if we need this barrier when late prim export, as I
    *       can't observe test fail without this barrier.
    */
   nir_memory_barrier_buffer(b);

   free(info);
}

void
ac_nir_lower_ngg_nogs(nir_shader *shader,
                      enum radeon_family family,
                      unsigned max_num_es_vertices,
                      unsigned num_vertices_per_primitives,
                      unsigned max_workgroup_size,
                      unsigned wave_size,
                      bool can_cull,
                      bool early_prim_export,
                      bool passthrough,
                      bool export_prim_id,
                      bool provoking_vtx_last,
                      bool use_edgeflags,
                      bool has_prim_query,
                      bool disable_streamout,
                      uint32_t instance_rate_inputs,
                      uint32_t clipdist_enable_mask,
                      uint32_t user_clip_plane_enable_mask)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);
   assert(max_num_es_vertices && max_workgroup_size && wave_size);
   assert(!(can_cull && passthrough));

   nir_variable *position_value_var = nir_local_variable_create(impl, glsl_vec4_type(), "position_value");
   nir_variable *prim_exp_arg_var = nir_local_variable_create(impl, glsl_uint_type(), "prim_exp_arg");
   nir_variable *es_accepted_var = can_cull ? nir_local_variable_create(impl, glsl_bool_type(), "es_accepted") : NULL;
   nir_variable *gs_accepted_var = can_cull ? nir_local_variable_create(impl, glsl_bool_type(), "gs_accepted") : NULL;

   bool streamout_enabled = shader->xfb_info && !disable_streamout;
   /* streamout need to be done before either prim or vertex export. Because when no
    * param export, rasterization can start right after prim and vertex export,
    * which left streamout buffer writes un-finished.
    */
   if (streamout_enabled)
      early_prim_export = false;

   lower_ngg_nogs_state state = {
      .passthrough = passthrough,
      .export_prim_id = export_prim_id,
      .early_prim_export = early_prim_export,
      .use_edgeflags = use_edgeflags,
      .has_prim_query = has_prim_query,
      .streamout_enabled = streamout_enabled,
      .num_vertices_per_primitives = num_vertices_per_primitives,
      .provoking_vtx_idx = provoking_vtx_last ? (num_vertices_per_primitives - 1) : 0,
      .position_value_var = position_value_var,
      .prim_exp_arg_var = prim_exp_arg_var,
      .es_accepted_var = es_accepted_var,
      .gs_accepted_var = gs_accepted_var,
      .max_num_waves = DIV_ROUND_UP(max_workgroup_size, wave_size),
      .max_es_num_vertices = max_num_es_vertices,
      .wave_size = wave_size,
      .instance_rate_inputs = instance_rate_inputs,
      .clipdist_enable_mask = clipdist_enable_mask,
      .user_clip_plane_enable_mask = user_clip_plane_enable_mask,
   };

   const bool need_prim_id_store_shared =
      export_prim_id && shader->info.stage == MESA_SHADER_VERTEX;

   if (export_prim_id) {
      nir_variable *prim_id_var = nir_variable_create(shader, nir_var_shader_out, glsl_uint_type(), "ngg_prim_id");
      prim_id_var->data.location = VARYING_SLOT_PRIMITIVE_ID;
      prim_id_var->data.driver_location = VARYING_SLOT_PRIMITIVE_ID;
      prim_id_var->data.interpolation = INTERP_MODE_NONE;
      shader->info.outputs_written |= VARYING_BIT_PRIMITIVE_ID;
   }

   nir_builder builder;
   nir_builder *b = &builder; /* This is to avoid the & */
   nir_builder_init(b, impl);

   if (can_cull) {
      /* We need divergence info for culling shaders. */
      nir_divergence_analysis(shader);
      analyze_shader_before_culling(shader, &state);
      save_reusable_variables(b, &state);
   }

   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_cf_list(&impl->body), nir_after_cf_list(&impl->body));
   b->cursor = nir_before_cf_list(&impl->body);

   ngg_nogs_init_vertex_indices_vars(b, impl, &state);

   if (!can_cull) {
      /* Newer chips can use PRIMGEN_PASSTHRU_NO_MSG to skip gs_alloc_req for NGG passthrough. */
      if (!(passthrough && family >= CHIP_NAVI23)) {
         /* Allocate export space on wave 0 - confirm to the HW that we want to use all possible space */
         nir_if *if_wave_0 = nir_push_if(b, nir_ieq(b, nir_load_subgroup_id(b), nir_imm_int(b, 0)));
         {
            nir_ssa_def *vtx_cnt = nir_load_workgroup_num_input_vertices_amd(b);
            nir_ssa_def *prim_cnt = nir_load_workgroup_num_input_primitives_amd(b);
            nir_alloc_vertices_and_primitives_amd(b, vtx_cnt, prim_cnt);
         }
         nir_pop_if(b, if_wave_0);
      }

      /* Take care of early primitive export, otherwise just pack the primitive export argument */
      if (state.early_prim_export)
         emit_ngg_nogs_prim_export(b, &state, NULL);
      else
         nir_store_var(b, prim_exp_arg_var, emit_ngg_nogs_prim_exp_arg(b, &state), 0x1u);
   } else {
      add_deferred_attribute_culling(b, &extracted, &state);
      b->cursor = nir_after_cf_list(&impl->body);

      if (state.early_prim_export)
         emit_ngg_nogs_prim_export(b, &state, nir_load_var(b, state.prim_exp_arg_var));

      /* Wait for culling to finish using LDS. */
      if (need_prim_id_store_shared) {
         nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                               .memory_scope = NIR_SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
      }
   }

   /* determine the LDS vertex stride */
   if (state.streamout_enabled) {
      /* The extra dword is used to avoid LDS bank conflicts and store the primitive id.
       * TODO: only alloc space for outputs that really need streamout.
       */
      state.pervertex_lds_bytes = (shader->num_outputs * 4 + 1) * 4;
   } else if (need_prim_id_store_shared)
      state.pervertex_lds_bytes = 4;

   if (need_prim_id_store_shared) {
      /* We need LDS space when VS needs to export the primitive ID. */
      state.total_lds_bytes = MAX2(state.total_lds_bytes,
                                   state.pervertex_lds_bytes * max_num_es_vertices);

      emit_ngg_nogs_prim_id_store_shared(b, &state);

      /* Wait for GS threads to store primitive ID in LDS. */
      nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP, .memory_scope = NIR_SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   }

   nir_intrinsic_instr *export_vertex_instr;
   nir_ssa_def *es_thread = can_cull ? nir_load_var(b, es_accepted_var) : nir_has_input_vertex_amd(b);

   nir_if *if_es_thread = nir_push_if(b, es_thread);
   {
      /* Run the actual shader */
      nir_cf_reinsert(&extracted, b->cursor);
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);

      if (state.export_prim_id)
         emit_store_ngg_nogs_es_primitive_id(b, &state);

      /* Export all vertex attributes (including the primitive ID) */
      export_vertex_instr = nir_export_vertex_amd(b);
   }
   nir_pop_if(b, if_es_thread);

   if (state.streamout_enabled) {
      /* TODO: support culling after streamout. */
      assert(!can_cull);

      ngg_nogs_build_streamout(b, &state);
   }

   /* streamout may be disabled by ngg_nogs_build_streamout() */
   if (state.streamout_enabled) {
      ngg_nogs_store_all_outputs_to_lds(shader, &state);
      b->cursor = nir_after_cf_list(&impl->body);
   }

   /* Take care of late primitive export */
   if (!state.early_prim_export) {
      emit_ngg_nogs_prim_export(b, &state, nir_load_var(b, prim_exp_arg_var));
   }

   if (can_cull) {
      /* Replace uniforms. */
      apply_reusable_variables(b, &state);

      /* Remove the redundant position output. */
      remove_extra_pos_outputs(shader, &state);

      /* After looking at the performance in apps eg. Doom Eternal, and The Witcher 3,
       * it seems that it's best to put the position export always at the end, and
       * then let ACO schedule it up (slightly) only when early prim export is used.
       */
      b->cursor = nir_before_instr(&export_vertex_instr->instr);

      nir_ssa_def *pos_val = nir_load_var(b, state.position_value_var);
      nir_io_semantics io_sem = { .location = VARYING_SLOT_POS, .num_slots = 1 };
      nir_store_output(b, pos_val, nir_imm_int(b, 0), .base = state.position_store_base,
                       .component = 0, .io_semantics = io_sem);
   }

   nir_metadata_preserve(impl, nir_metadata_none);
   nir_validate_shader(shader, "after emitting NGG VS/TES");

   /* Cleanup */
   nir_opt_dead_write_vars(shader);
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_lower_alu_to_scalar(shader, NULL, NULL);
   nir_lower_phis_to_scalar(shader, true);

   if (can_cull) {
      /* It's beneficial to redo these opts after splitting the shader. */
      nir_opt_sink(shader, nir_move_load_input | nir_move_const_undef | nir_move_copies);
      nir_opt_move(shader, nir_move_load_input | nir_move_copies | nir_move_const_undef);
   }

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, shader, nir_opt_undef);
      NIR_PASS(progress, shader, nir_opt_dce);
      NIR_PASS(progress, shader, nir_opt_dead_cf);

      if (can_cull)
         progress |= cleanup_culling_shader_after_dce(shader, b->impl, &state);
   } while (progress);

   shader->info.shared_size = state.total_lds_bytes;
}

/**
 * Return the address of the LDS storage reserved for the N'th vertex,
 * where N is in emit order, meaning:
 * - during the finale, N is the invocation_index (within the workgroup)
 * - during vertex emit, i.e. while the API GS shader invocation is running,
 *   N = invocation_index * gs_max_out_vertices + emit_idx
 *   where emit_idx is the vertex index in the current API GS invocation.
 *
 * Goals of the LDS memory layout:
 * 1. Eliminate bank conflicts on write for geometry shaders that have all emits
 *    in uniform control flow
 * 2. Eliminate bank conflicts on read for export if, additionally, there is no
 *    culling
 * 3. Agnostic to the number of waves (since we don't know it before compiling)
 * 4. Allow coalescing of LDS instructions (ds_write_b128 etc.)
 * 5. Avoid wasting memory.
 *
 * We use an AoS layout due to point 4 (this also helps point 3). In an AoS
 * layout, elimination of bank conflicts requires that each vertex occupy an
 * odd number of dwords. We use the additional dword to store the output stream
 * index as well as a flag to indicate whether this vertex ends a primitive
 * for rasterization.
 *
 * Swizzling is required to satisfy points 1 and 2 simultaneously.
 *
 * Vertices are stored in export order (gsthread * gs_max_out_vertices + emitidx).
 * Indices are swizzled in groups of 32, which ensures point 1 without
 * disturbing point 2.
 *
 * \return an LDS pointer to type {[N x i32], [4 x i8]}
 */
static nir_ssa_def *
ngg_gs_out_vertex_addr(nir_builder *b, nir_ssa_def *out_vtx_idx, lower_ngg_gs_state *s)
{
   unsigned write_stride_2exp = ffs(MAX2(b->shader->info.gs.vertices_out, 1)) - 1;

   /* gs_max_out_vertices = 2^(write_stride_2exp) * some odd number */
   if (write_stride_2exp) {
      nir_ssa_def *row = nir_ushr_imm(b, out_vtx_idx, 5);
      nir_ssa_def *swizzle = nir_iand_imm(b, row, (1u << write_stride_2exp) - 1u);
      out_vtx_idx = nir_ixor(b, out_vtx_idx, swizzle);
   }

   nir_ssa_def *out_vtx_offs = nir_imul_imm(b, out_vtx_idx, s->lds_bytes_per_gs_out_vertex);
   return nir_iadd_imm_nuw(b, out_vtx_offs, s->lds_addr_gs_out_vtx);
}

static nir_ssa_def *
ngg_gs_emit_vertex_addr(nir_builder *b, nir_ssa_def *gs_vtx_idx, lower_ngg_gs_state *s)
{
   nir_ssa_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_ssa_def *gs_out_vtx_base = nir_imul_imm(b, tid_in_tg, b->shader->info.gs.vertices_out);
   nir_ssa_def *out_vtx_idx = nir_iadd_nuw(b, gs_out_vtx_base, gs_vtx_idx);

   return ngg_gs_out_vertex_addr(b, out_vtx_idx, s);
}

static void
ngg_gs_clear_primflags(nir_builder *b, nir_ssa_def *num_vertices, unsigned stream, lower_ngg_gs_state *s)
{
   nir_ssa_def *zero_u8 = nir_imm_zero(b, 1, 8);
   nir_store_var(b, s->current_clear_primflag_idx_var, num_vertices, 0x1u);

   nir_loop *loop = nir_push_loop(b);
   {
      nir_ssa_def *current_clear_primflag_idx = nir_load_var(b, s->current_clear_primflag_idx_var);
      nir_if *if_break = nir_push_if(b, nir_uge(b, current_clear_primflag_idx, nir_imm_int(b, b->shader->info.gs.vertices_out)));
      {
         nir_jump(b, nir_jump_break);
      }
      nir_push_else(b, if_break);
      {
         nir_ssa_def *emit_vtx_addr = ngg_gs_emit_vertex_addr(b, current_clear_primflag_idx, s);
         nir_store_shared(b, zero_u8, emit_vtx_addr, .base = s->lds_offs_primflags + stream);
         nir_store_var(b, s->current_clear_primflag_idx_var, nir_iadd_imm_nuw(b, current_clear_primflag_idx, 1), 0x1u);
      }
      nir_pop_if(b, if_break);
   }
   nir_pop_loop(b, loop);
}

static void
ngg_gs_shader_query(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   nir_if *if_shader_query = nir_push_if(b, nir_load_shader_query_enabled_amd(b));
   nir_ssa_def *num_prims_in_wave = NULL;

   /* Calculate the "real" number of emitted primitives from the emitted GS vertices and primitives.
    * GS emits points, line strips or triangle strips.
    * Real primitives are points, lines or triangles.
    */
   if (nir_src_is_const(intrin->src[0]) && nir_src_is_const(intrin->src[1])) {
      unsigned gs_vtx_cnt = nir_src_as_uint(intrin->src[0]);
      unsigned gs_prm_cnt = nir_src_as_uint(intrin->src[1]);
      unsigned total_prm_cnt = gs_vtx_cnt - gs_prm_cnt * (s->num_vertices_per_primitive - 1u);
      nir_ssa_def *num_threads = nir_bit_count(b, nir_ballot(b, 1, s->wave_size, nir_imm_bool(b, true)));
      num_prims_in_wave = nir_imul_imm(b, num_threads, total_prm_cnt);
   } else {
      nir_ssa_def *gs_vtx_cnt = intrin->src[0].ssa;
      nir_ssa_def *prm_cnt = intrin->src[1].ssa;
      if (s->num_vertices_per_primitive > 1)
         prm_cnt = nir_iadd_nuw(b, nir_imul_imm(b, prm_cnt, -1u * (s->num_vertices_per_primitive - 1)), gs_vtx_cnt);
      num_prims_in_wave = nir_reduce(b, prm_cnt, .reduction_op = nir_op_iadd);
   }

   /* Store the query result to GDS using an atomic add. */
   nir_if *if_first_lane = nir_push_if(b, nir_elect(b, 1));
   nir_gds_atomic_add_amd(b, 32, num_prims_in_wave, nir_imm_int(b, 0), nir_imm_int(b, 0x100));
   nir_pop_if(b, if_first_lane);

   nir_pop_if(b, if_shader_query);
}

static bool
lower_ngg_gs_store_output(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   assert(nir_src_is_const(intrin->src[1]));
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned base = nir_intrinsic_base(intrin);
   unsigned writemask = nir_intrinsic_write_mask(intrin);
   unsigned component_offset = nir_intrinsic_component(intrin);
   unsigned base_offset = nir_src_as_uint(intrin->src[1]);
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   unsigned location = io_sem.location + base_offset;
   assert(location < VARYING_SLOT_MAX);

   unsigned base_index = base + base_offset;
   assert(base_index < VARYING_SLOT_MAX);

   nir_ssa_def *store_val = intrin->src[0].ssa;

   /* Small bitsize components consume the same amount of space as 32-bit components,
    * but 64-bit ones consume twice as many. (Vulkan spec 15.1.5)
    *
    * 64-bit IO has been lowered to multi 32-bit IO.
    */
   assert(store_val->bit_size <= 32);

   /* Save output usage info. */
   gs_output_info *info = &s->output_info[location];

   for (unsigned comp = 0; comp < store_val->num_components; ++comp) {
      if (!(writemask & (1 << comp)))
         continue;
      unsigned stream = (io_sem.gs_streams >> (comp * 2)) & 0x3;
      if (!(b->shader->info.gs.active_stream_mask & (1 << stream)))
         continue;

      unsigned component = component_offset + comp;

      /* The same output should always belong to the same base. */
      assert(!info->components_mask || info->base == base_index);
      /* The same output component should always belong to the same stream. */
      assert(!(info->components_mask & (1 << component)) ||
             ((info->stream >> (component * 2)) & 3) == stream);

      info->base = base_index;
      /* Components of the same output slot may belong to different streams. */
      info->stream |= stream << (component * 2);
      info->components_mask |= BITFIELD_BIT(component);

      nir_variable *var = s->output_vars[location][component];
      if (!var) {
         var = nir_local_variable_create(
            s->impl, glsl_uintN_t_type(store_val->bit_size), "output");
         s->output_vars[location][component] = var;
      }
      assert(glsl_base_type_bit_size(glsl_get_base_type(var->type)) == store_val->bit_size);

      nir_store_var(b, var, nir_channel(b, store_val, comp), 0x1u);
   }

   nir_instr_remove(&intrin->instr);
   return true;
}

static unsigned
gs_output_component_mask_with_stream(gs_output_info *info, unsigned stream)
{
   unsigned mask = info->components_mask;
   if (!mask)
      return 0;

   /* clear component when not requested stream */
   for (int i = 0; i < 4; i++) {
      if (((info->stream >> (i * 2)) & 3) != stream)
         mask &= ~(1 << i);
   }

   return mask;
}

static bool
lower_ngg_gs_emit_vertex_with_counter(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   if (!(b->shader->info.gs.active_stream_mask & (1 << stream))) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   nir_ssa_def *gs_emit_vtx_idx = intrin->src[0].ssa;
   nir_ssa_def *current_vtx_per_prim = intrin->src[1].ssa;
   nir_ssa_def *gs_emit_vtx_addr = ngg_gs_emit_vertex_addr(b, gs_emit_vtx_idx, s);

   for (unsigned slot = 0; slot < VARYING_SLOT_MAX; ++slot) {
      unsigned packed_location = util_bitcount64((b->shader->info.outputs_written & BITFIELD64_MASK(slot)));
      gs_output_info *info = &s->output_info[slot];

      unsigned mask = gs_output_component_mask_with_stream(info, stream);
      if (!mask)
         continue;

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_ssa_def *values[4] = {0};
         for (int c = start; c < start + count; ++c) {
            nir_variable *var = s->output_vars[slot][c];
            if (!var) {
               /* no one write to this output before */
               values[c - start] = nir_ssa_undef(b, 1, 32);
               continue;
            }

            /* Load output from variable. */
            nir_ssa_def *val = nir_load_var(b, var);

            /* extend 8/16 bit to 32 bit, 64 bit has been lowered */
            unsigned bit_size = glsl_base_type_bit_size(glsl_get_base_type(var->type));
            values[c - start] = bit_size == 32 ? val : nir_u2u32(b, val);

            /* Clear the variable (it is undefined after emit_vertex) */
            nir_store_var(b, s->output_vars[slot][c], nir_ssa_undef(b, 1, bit_size), 0x1);
         }

         nir_ssa_def *store_val = nir_vec(b, values, (unsigned)count);
         nir_store_shared(b, store_val, gs_emit_vtx_addr,
                          .base = packed_location * 16 + start * 4,
                          .align_mul = 4);
      }
   }

   /* Calculate and store per-vertex primitive flags based on vertex counts:
    * - bit 0: whether this vertex finishes a primitive (a real primitive, not the strip)
    * - bit 1: whether the primitive index is odd (if we are emitting triangle strips, otherwise always 0)
    * - bit 2: whether vertex is live (if culling is enabled: set after culling, otherwise always 1)
    */

   nir_ssa_def *vertex_live_flag = !stream && s->can_cull ?
      nir_ishl_imm(b, nir_b2i32(b, nir_inot(b, nir_load_cull_any_enabled_amd(b))), 2) :
      nir_imm_int(b, 0b100);

   nir_ssa_def *completes_prim = nir_ige(b, current_vtx_per_prim, nir_imm_int(b, s->num_vertices_per_primitive - 1));
   nir_ssa_def *complete_flag = nir_b2i32(b, completes_prim);

   nir_ssa_def *prim_flag = nir_ior(b, vertex_live_flag, complete_flag);
   if (s->num_vertices_per_primitive == 3) {
      nir_ssa_def *odd = nir_iand_imm(b, current_vtx_per_prim, 1);
      prim_flag = nir_iadd_nuw(b, prim_flag, nir_ishl(b, odd, nir_imm_int(b, 1)));
   }

   nir_store_shared(b, nir_u2u8(b, prim_flag), gs_emit_vtx_addr, .base = s->lds_offs_primflags + stream, .align_mul = 4u);
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_end_primitive_with_counter(nir_builder *b, nir_intrinsic_instr *intrin, UNUSED lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   /* These are not needed, we can simply remove them */
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_set_vertex_and_primitive_count(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   if (stream > 0 && !(b->shader->info.gs.active_stream_mask & (1 << stream))) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   s->found_out_vtxcnt[stream] = true;

   /* Clear the primitive flags of non-emitted vertices */
   if (!nir_src_is_const(intrin->src[0]) || nir_src_as_uint(intrin->src[0]) < b->shader->info.gs.vertices_out)
      ngg_gs_clear_primflags(b, intrin->src[0].ssa, stream, s);

   ngg_gs_shader_query(b, intrin, s);
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_gs_state *s = (lower_ngg_gs_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic == nir_intrinsic_store_output)
      return lower_ngg_gs_store_output(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_emit_vertex_with_counter)
      return lower_ngg_gs_emit_vertex_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_end_primitive_with_counter)
      return lower_ngg_gs_end_primitive_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count)
      return lower_ngg_gs_set_vertex_and_primitive_count(b, intrin, s);

   return false;
}

static void
lower_ngg_gs_intrinsics(nir_shader *shader, lower_ngg_gs_state *s)
{
   nir_shader_instructions_pass(shader, lower_ngg_gs_intrinsic, nir_metadata_none, s);
}

static void
ngg_gs_export_primitives(nir_builder *b, nir_ssa_def *max_num_out_prims, nir_ssa_def *tid_in_tg,
                         nir_ssa_def *exporter_tid_in_tg, nir_ssa_def *primflag_0,
                         lower_ngg_gs_state *s)
{
   nir_if *if_prim_export_thread = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_prims));

   /* Only bit 0 matters here - set it to 1 when the primitive should be null */
   nir_ssa_def *is_null_prim = nir_ixor(b, primflag_0, nir_imm_int(b, -1u));

   nir_ssa_def *vtx_indices[3] = {0};
   vtx_indices[s->num_vertices_per_primitive - 1] = exporter_tid_in_tg;
   if (s->num_vertices_per_primitive >= 2)
      vtx_indices[s->num_vertices_per_primitive - 2] = nir_isub(b, exporter_tid_in_tg, nir_imm_int(b, 1));
   if (s->num_vertices_per_primitive == 3)
      vtx_indices[s->num_vertices_per_primitive - 3] = nir_isub(b, exporter_tid_in_tg, nir_imm_int(b, 2));

   if (s->num_vertices_per_primitive == 3) {
      /* API GS outputs triangle strips, but NGG HW understands triangles.
       * We already know the triangles due to how we set the primitive flags, but we need to
       * make sure the vertex order is so that the front/back is correct, and the provoking vertex is kept.
       */

      nir_ssa_def *is_odd = nir_ubfe(b, primflag_0, nir_imm_int(b, 1), nir_imm_int(b, 1));
      if (!s->provoking_vertex_last) {
         vtx_indices[1] = nir_iadd(b, vtx_indices[1], is_odd);
         vtx_indices[2] = nir_isub(b, vtx_indices[2], is_odd);
      } else {
         vtx_indices[0] = nir_iadd(b, vtx_indices[0], is_odd);
         vtx_indices[1] = nir_isub(b, vtx_indices[1], is_odd);
      }
   }

   nir_ssa_def *arg = emit_pack_ngg_prim_exp_arg(b, s->num_vertices_per_primitive, vtx_indices, is_null_prim, false);
   nir_export_primitive_amd(b, arg);
   nir_pop_if(b, if_prim_export_thread);
}

static void
ngg_gs_export_vertices(nir_builder *b, nir_ssa_def *max_num_out_vtx, nir_ssa_def *tid_in_tg,
                       nir_ssa_def *out_vtx_lds_addr, lower_ngg_gs_state *s)
{
   nir_if *if_vtx_export_thread = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_vtx));
   nir_ssa_def *exported_out_vtx_lds_addr = out_vtx_lds_addr;

   if (!s->output_compile_time_known) {
      /* Vertex compaction.
       * The current thread will export a vertex that was live in another invocation.
       * Load the index of the vertex that the current thread will have to export.
       */
      nir_ssa_def *exported_vtx_idx = nir_load_shared(b, 1, 8, out_vtx_lds_addr, .base = s->lds_offs_primflags + 1);
      exported_out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, nir_u2u32(b, exported_vtx_idx), s);
   }

   for (unsigned slot = 0; slot < VARYING_SLOT_MAX; ++slot) {
      if (!(b->shader->info.outputs_written & BITFIELD64_BIT(slot)))
         continue;

      gs_output_info *info = &s->output_info[slot];
      unsigned mask = gs_output_component_mask_with_stream(info, 0);
      if (!mask)
         continue;

      unsigned packed_location = util_bitcount64((b->shader->info.outputs_written & BITFIELD64_MASK(slot)));
      nir_io_semantics io_sem = { .location = slot, .num_slots = 1 };

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_ssa_def *load =
            nir_load_shared(b, count, 32, exported_out_vtx_lds_addr,
                            .base = packed_location * 16 + start * 4,
                            .align_mul = 4);

         for (int i = 0; i < count; i++) {
            nir_variable *var = s->output_vars[slot][start + i];
            assert(var);

            nir_ssa_def *val = nir_channel(b, load, i);

            /* Convert to the expected bit size of the output variable. */
            unsigned bit_size = glsl_base_type_bit_size(glsl_get_base_type(var->type));
            if (bit_size != 32)
               val = nir_u2u(b, val, bit_size);

            nir_store_output(b, val, nir_imm_int(b, 0), .base = info->base,
                             .io_semantics = io_sem, .component = start + i,
                             .write_mask = 1);
         }
      }
   }

   nir_export_vertex_amd(b);
   nir_pop_if(b, if_vtx_export_thread);
}

static void
ngg_gs_setup_vertex_compaction(nir_builder *b, nir_ssa_def *vertex_live, nir_ssa_def *tid_in_tg,
                               nir_ssa_def *exporter_tid_in_tg, lower_ngg_gs_state *s)
{
   assert(vertex_live->bit_size == 1);
   nir_if *if_vertex_live = nir_push_if(b, vertex_live);
   {
      /* Setup the vertex compaction.
       * Save the current thread's id for the thread which will export the current vertex.
       * We reuse stream 1 of the primitive flag of the other thread's vertex for storing this.
       */

      nir_ssa_def *exporter_lds_addr = ngg_gs_out_vertex_addr(b, exporter_tid_in_tg, s);
      nir_ssa_def *tid_in_tg_u8 = nir_u2u8(b, tid_in_tg);
      nir_store_shared(b, tid_in_tg_u8, exporter_lds_addr, .base = s->lds_offs_primflags + 1);
   }
   nir_pop_if(b, if_vertex_live);
}

static nir_ssa_def *
ngg_gs_load_out_vtx_primflag(nir_builder *b, unsigned stream, nir_ssa_def *tid_in_tg,
                             nir_ssa_def *vtx_lds_addr, nir_ssa_def *max_num_out_vtx,
                             lower_ngg_gs_state *s)
{
   nir_ssa_def *zero = nir_imm_int(b, 0);

   nir_if *if_outvtx_thread = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_vtx));
   nir_ssa_def *primflag = nir_load_shared(b, 1, 8, vtx_lds_addr,
                                           .base = s->lds_offs_primflags + stream);
   primflag = nir_u2u32(b, primflag);
   nir_pop_if(b, if_outvtx_thread);

   return nir_if_phi(b, primflag, zero);
}

static void
ngg_gs_out_prim_all_vtxptr(nir_builder *b, nir_ssa_def *last_vtxidx, nir_ssa_def *last_vtxptr,
                           nir_ssa_def *last_vtx_primflag, lower_ngg_gs_state *s,
                           nir_ssa_def *vtxptr[3])
{
   unsigned last_vtx = s->num_vertices_per_primitive - 1;
   vtxptr[last_vtx]= last_vtxptr;

   bool primitive_is_triangle = s->num_vertices_per_primitive == 3;
   nir_ssa_def *is_odd = primitive_is_triangle ?
      nir_ubfe(b, last_vtx_primflag, nir_imm_int(b, 1), nir_imm_int(b, 1)) : NULL;

   for (unsigned i = 0; i < s->num_vertices_per_primitive - 1; i++) {
      nir_ssa_def *vtxidx = nir_iadd_imm(b, last_vtxidx, -(last_vtx - i));

      /* Need to swap vertex 0 and vertex 1 when vertex 2 index is odd to keep
       * CW/CCW order for correct front/back face culling.
       */
      if (primitive_is_triangle)
         vtxidx = i == 0 ? nir_iadd(b, vtxidx, is_odd) : nir_isub(b, vtxidx, is_odd);

      vtxptr[i] = ngg_gs_out_vertex_addr(b, vtxidx, s);
   }
}

static nir_ssa_def *
ngg_gs_cull_primitive(nir_builder *b, nir_ssa_def *tid_in_tg, nir_ssa_def *max_vtxcnt,
                      nir_ssa_def *out_vtx_lds_addr, nir_ssa_def *out_vtx_primflag_0,
                      lower_ngg_gs_state *s)
{
   /* we haven't enabled point culling, if enabled this function could be further optimized */
   assert(s->num_vertices_per_primitive > 1);

   /* save the primflag so that we don't need to load it from LDS again */
   nir_variable *primflag_var = nir_local_variable_create(s->impl, glsl_uint_type(), "primflag");
   nir_store_var(b, primflag_var, out_vtx_primflag_0, 1);

   /* last bit of primflag indicate if this is the final vertex of a primitive */
   nir_ssa_def *is_end_prim_vtx = nir_i2b(b, nir_iand_imm(b, out_vtx_primflag_0, 1));
   nir_ssa_def *has_output_vertex = nir_ilt(b, tid_in_tg, max_vtxcnt);
   nir_ssa_def *prim_enable = nir_iand(b, is_end_prim_vtx, has_output_vertex);

   nir_if *if_prim_enable = nir_push_if(b, prim_enable);
   {
      /* Calculate the LDS address of every vertex in the current primitive. */
      nir_ssa_def *vtxptr[3];
      ngg_gs_out_prim_all_vtxptr(b, tid_in_tg, out_vtx_lds_addr, out_vtx_primflag_0, s, vtxptr);

      /* Load the positions from LDS. */
      nir_ssa_def *pos[3][4];
      for (unsigned i = 0; i < s->num_vertices_per_primitive; i++) {
         /* VARYING_SLOT_POS == 0, so base won't count packed location */
         pos[i][3] = nir_load_shared(b, 1, 32, vtxptr[i], .base = 12); /* W */
         nir_ssa_def *xy = nir_load_shared(b, 2, 32, vtxptr[i], .base = 0, .align_mul = 4);
         pos[i][0] = nir_channel(b, xy, 0);
         pos[i][1] = nir_channel(b, xy, 1);

         pos[i][0] = nir_fdiv(b, pos[i][0], pos[i][3]);
         pos[i][1] = nir_fdiv(b, pos[i][1], pos[i][3]);
      }

      /* TODO: support clipdist culling in GS */
      nir_ssa_def *accepted_by_clipdist = nir_imm_bool(b, true);

      nir_ssa_def *accepted = ac_nir_cull_primitive(
         b, accepted_by_clipdist, pos, s->num_vertices_per_primitive, NULL, NULL);

      nir_if *if_rejected = nir_push_if(b, nir_inot(b, accepted));
      {
         /* clear the primflag if rejected */
         nir_store_shared(b, nir_imm_zero(b, 1, 8), out_vtx_lds_addr,
                          .base = s->lds_offs_primflags);

         nir_store_var(b, primflag_var, nir_imm_int(b, 0), 1);
      }
      nir_pop_if(b, if_rejected);
   }
   nir_pop_if(b, if_prim_enable);

   /* Wait for LDS primflag access done. */
   nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                         .memory_scope = NIR_SCOPE_WORKGROUP,
                         .memory_semantics = NIR_MEMORY_ACQ_REL,
                         .memory_modes = nir_var_mem_shared);

   /* only dead vertex need a chance to relive */
   nir_ssa_def *vtx_is_dead = nir_ieq_imm(b, nir_load_var(b, primflag_var), 0);
   nir_ssa_def *vtx_update_primflag = nir_iand(b, vtx_is_dead, has_output_vertex);
   nir_if *if_update_primflag = nir_push_if(b, vtx_update_primflag);
   {
      /* get succeeding vertices' primflag to detect this vertex's liveness */
      for (unsigned i = 1; i < s->num_vertices_per_primitive; i++) {
         nir_ssa_def *vtxidx = nir_iadd_imm(b, tid_in_tg, i);
         nir_ssa_def *not_overflow = nir_ilt(b, vtxidx, max_vtxcnt);
         nir_if *if_not_overflow = nir_push_if(b, not_overflow);
         {
            nir_ssa_def *vtxptr = ngg_gs_out_vertex_addr(b, vtxidx, s);
            nir_ssa_def *vtx_primflag =
               nir_load_shared(b, 1, 8, vtxptr, .base = s->lds_offs_primflags);
            vtx_primflag = nir_u2u32(b, vtx_primflag);

            /* if succeeding vertex is alive end of primitive vertex, need to set current
             * thread vertex's liveness flag (bit 2)
             */
            nir_ssa_def *has_prim = nir_i2b(b, nir_iand_imm(b, vtx_primflag, 1));
            nir_ssa_def *vtx_live_flag =
               nir_bcsel(b, has_prim, nir_imm_int(b, 0b100), nir_imm_int(b, 0));

            /* update this vertex's primflag */
            nir_ssa_def *primflag = nir_load_var(b, primflag_var);
            primflag = nir_ior(b, primflag, vtx_live_flag);
            nir_store_var(b, primflag_var, primflag, 1);
         }
         nir_pop_if(b, if_not_overflow);
      }
   }
   nir_pop_if(b, if_update_primflag);

   return nir_load_var(b, primflag_var);
}

static void
ngg_gs_build_streamout(nir_builder *b, lower_ngg_gs_state *st)
{
   nir_xfb_info *info = nir_gather_xfb_info_from_intrinsics(b->shader, NULL);
   if (unlikely(!info))
      return;

   nir_ssa_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_ssa_def *max_vtxcnt = nir_load_workgroup_num_input_vertices_amd(b);
   nir_ssa_def *out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, tid_in_tg, st);
   nir_ssa_def *prim_live[4] = {0};
   nir_ssa_def *gen_prim[4] = {0};
   nir_ssa_def *export_seq[4] = {0};
   nir_ssa_def *out_vtx_primflag[4] = {0};
   for (unsigned stream = 0; stream < 4; stream++) {
      if (!(info->streams_written & BITFIELD_BIT(stream)))
         continue;

      out_vtx_primflag[stream] =
         ngg_gs_load_out_vtx_primflag(b, stream, tid_in_tg, out_vtx_lds_addr, max_vtxcnt, st);

      /* Check bit 0 of primflag for primitive alive, it's set for every last
       * vertex of a primitive.
       */
      prim_live[stream] = nir_i2b(b, nir_iand_imm(b, out_vtx_primflag[stream], 1));

      unsigned scratch_stride = ALIGN(st->max_num_waves, 4);

      /* We want to export primitives to streamout buffer in sequence,
       * but not all vertices are alive or mark end of a primitive, so
       * there're "holes". We don't need continous invocations to write
       * primitives to streamout buffer like final vertex export, so
       * just repack to get the sequence (export_seq) is enough, no need
       * to do compaction.
       *
       * Use separate scratch space for each stream to avoid barrier.
       * TODO: we may further reduce barriers by writing to all stream
       * LDS at once, then we only need one barrier instead of one each
       * stream..
       */
      wg_repack_result rep =
         repack_invocations_in_workgroup(b, prim_live[stream],
                                         st->lds_addr_gs_scratch + stream * scratch_stride,
                                         st->max_num_waves, st->wave_size);

      /* nir_intrinsic_set_vertex_and_primitive_count can also get primitive count of
       * current wave, but still need LDS to sum all wave's count to get workgroup count.
       * And we need repack to export primitive to streamout buffer anyway, so do here.
       */
      gen_prim[stream] = rep.num_repacked_invocations;
      export_seq[stream] = rep.repacked_invocation_index;
   }

   /* Workgroup barrier: wait for LDS scratch reads finish. */
   nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                      .memory_scope = NIR_SCOPE_WORKGROUP,
                      .memory_semantics = NIR_MEMORY_ACQ_REL,
                      .memory_modes = nir_var_mem_shared);

   /* Get global buffer offset where this workgroup will stream out data to. */
   nir_ssa_def *emit_prim[4] = {0};
   nir_ssa_def *buffer_offsets[4] = {0};
   nir_ssa_def *so_buffer[4] = {0};
   nir_ssa_def *prim_stride[4] = {0};
   ngg_build_streamout_buffer_info(b, info, st->lds_addr_gs_scratch, tid_in_tg, gen_prim,
                                   prim_stride, so_buffer, buffer_offsets, emit_prim);

   /* GS use packed location for vertex LDS storage. */
   int slot_to_register[NUM_TOTAL_VARYING_SLOTS];
   for (int i = 0; i < info->output_count; i++) {
      unsigned location = info->outputs[i].location;
      slot_to_register[location] =
         util_bitcount64(b->shader->info.outputs_written & BITFIELD64_MASK(location));
   }

   for (unsigned stream = 0; stream < 4; stream++) {
      if (!(info->streams_written & BITFIELD_BIT(stream)))
         continue;

      nir_ssa_def *can_emit = nir_ilt(b, export_seq[stream], emit_prim[stream]);
      nir_if *if_emit = nir_push_if(b, nir_iand(b, can_emit, prim_live[stream]));
      {
         /* Get streamout buffer vertex index for the first vertex of this primitive. */
         nir_ssa_def *vtx_buffer_idx =
            nir_imul_imm(b, export_seq[stream], st->num_vertices_per_primitive);

         /* Get all vertices' lds address of this primitive. */
         nir_ssa_def *exported_vtx_lds_addr[3];
         ngg_gs_out_prim_all_vtxptr(b, tid_in_tg, out_vtx_lds_addr,
                                    out_vtx_primflag[stream], st,
                                    exported_vtx_lds_addr);

         /* Write all vertices of this primitive to streamout buffer. */
         for (unsigned i = 0; i < st->num_vertices_per_primitive; i++) {
            ngg_build_streamout_vertex(b, info, stream, slot_to_register,
                                       so_buffer, buffer_offsets,
                                       nir_iadd_imm(b, vtx_buffer_idx, i),
                                       exported_vtx_lds_addr[i]);
         }
      }
      nir_pop_if(b, if_emit);
   }
}

static void
ngg_gs_finale(nir_builder *b, lower_ngg_gs_state *s)
{
   nir_ssa_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_ssa_def *max_vtxcnt = nir_load_workgroup_num_input_vertices_amd(b);
   nir_ssa_def *max_prmcnt = max_vtxcnt; /* They are currently practically the same; both RADV and RadeonSI do this. */
   nir_ssa_def *out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, tid_in_tg, s);

   if (s->output_compile_time_known) {
      /* When the output is compile-time known, the GS writes all possible vertices and primitives it can.
       * The gs_alloc_req needs to happen on one wave only, otherwise the HW hangs.
       */
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq(b, nir_load_subgroup_id(b), nir_imm_zero(b, 1, 32)));
      nir_alloc_vertices_and_primitives_amd(b, max_vtxcnt, max_prmcnt);
      nir_pop_if(b, if_wave_0);
   }

   /* Workgroup barrier already emitted, we can assume all GS output stores are done by now. */

   nir_ssa_def *out_vtx_primflag_0 = ngg_gs_load_out_vtx_primflag(b, 0, tid_in_tg, out_vtx_lds_addr, max_vtxcnt, s);

   if (s->output_compile_time_known) {
      ngg_gs_export_primitives(b, max_vtxcnt, tid_in_tg, tid_in_tg, out_vtx_primflag_0, s);
      ngg_gs_export_vertices(b, max_vtxcnt, tid_in_tg, out_vtx_lds_addr, s);
      return;
   }

   /* cull primitives */
   if (s->can_cull) {
      nir_if *if_cull_en = nir_push_if(b, nir_load_cull_any_enabled_amd(b));

      /* culling code will update the primflag */
      nir_ssa_def *updated_primflag =
         ngg_gs_cull_primitive(b, tid_in_tg, max_vtxcnt, out_vtx_lds_addr,
                               out_vtx_primflag_0, s);

      nir_pop_if(b, if_cull_en);

      out_vtx_primflag_0 = nir_if_phi(b, updated_primflag, out_vtx_primflag_0);
   }

   /* When the output vertex count is not known at compile time:
    * There may be gaps between invocations that have live vertices, but NGG hardware
    * requires that the invocations that export vertices are packed (ie. compact).
    * To ensure this, we need to repack invocations that have a live vertex.
    */
   nir_ssa_def *vertex_live = nir_ine(b, out_vtx_primflag_0, nir_imm_zero(b, 1, out_vtx_primflag_0->bit_size));
   wg_repack_result rep = repack_invocations_in_workgroup(b, vertex_live, s->lds_addr_gs_scratch, s->max_num_waves, s->wave_size);

   nir_ssa_def *workgroup_num_vertices = rep.num_repacked_invocations;
   nir_ssa_def *exporter_tid_in_tg = rep.repacked_invocation_index;

   /* When the workgroup emits 0 total vertices, we also must export 0 primitives (otherwise the HW can hang). */
   nir_ssa_def *any_output = nir_ine(b, workgroup_num_vertices, nir_imm_int(b, 0));
   max_prmcnt = nir_bcsel(b, any_output, max_prmcnt, nir_imm_int(b, 0));

   /* Allocate export space. We currently don't compact primitives, just use the maximum number. */
   nir_if *if_wave_0 = nir_push_if(b, nir_ieq(b, nir_load_subgroup_id(b), nir_imm_zero(b, 1, 32)));
   nir_alloc_vertices_and_primitives_amd(b, workgroup_num_vertices, max_prmcnt);
   nir_pop_if(b, if_wave_0);

   /* Vertex compaction. This makes sure there are no gaps between threads that export vertices. */
   ngg_gs_setup_vertex_compaction(b, vertex_live, tid_in_tg, exporter_tid_in_tg, s);

   /* Workgroup barrier: wait for all LDS stores to finish. */
   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                        .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   ngg_gs_export_primitives(b, max_prmcnt, tid_in_tg, exporter_tid_in_tg, out_vtx_primflag_0, s);
   ngg_gs_export_vertices(b, workgroup_num_vertices, tid_in_tg, out_vtx_lds_addr, s);
}

void
ac_nir_lower_ngg_gs(nir_shader *shader,
                    unsigned wave_size,
                    unsigned max_workgroup_size,
                    unsigned esgs_ring_lds_bytes,
                    unsigned gs_out_vtx_bytes,
                    unsigned gs_total_out_vtx_bytes,
                    bool provoking_vertex_last,
                    bool can_cull,
                    bool disable_streamout)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);

   lower_ngg_gs_state state = {
      .impl = impl,
      .max_num_waves = DIV_ROUND_UP(max_workgroup_size, wave_size),
      .wave_size = wave_size,
      .lds_addr_gs_out_vtx = esgs_ring_lds_bytes,
      .lds_addr_gs_scratch = ALIGN(esgs_ring_lds_bytes + gs_total_out_vtx_bytes, 8u /* for the repacking code */),
      .lds_offs_primflags = gs_out_vtx_bytes,
      .lds_bytes_per_gs_out_vertex = gs_out_vtx_bytes + 4u,
      .provoking_vertex_last = provoking_vertex_last,
      .can_cull = can_cull,
      .streamout_enabled = shader->xfb_info && !disable_streamout,
   };

   unsigned lds_scratch_bytes = ALIGN(state.max_num_waves, 4u);
   /* streamout take 8 dwords for buffer offset and emit vertex per stream */
   if (state.streamout_enabled)
      lds_scratch_bytes = MAX2(lds_scratch_bytes, 32);

   unsigned total_lds_bytes = state.lds_addr_gs_scratch + lds_scratch_bytes;
   shader->info.shared_size = total_lds_bytes;

   if (!can_cull) {
      nir_gs_count_vertices_and_primitives(shader, state.const_out_vtxcnt,
                                           state.const_out_prmcnt, 4u);
      state.output_compile_time_known =
         state.const_out_vtxcnt[0] == shader->info.gs.vertices_out &&
         state.const_out_prmcnt[0] != -1;
   }

   if (!state.output_compile_time_known)
      state.current_clear_primflag_idx_var = nir_local_variable_create(impl, glsl_uint_type(), "current_clear_primflag_idx");

   if (shader->info.gs.output_primitive == SHADER_PRIM_POINTS)
      state.num_vertices_per_primitive = 1;
   else if (shader->info.gs.output_primitive == SHADER_PRIM_LINE_STRIP)
      state.num_vertices_per_primitive = 2;
   else if (shader->info.gs.output_primitive == SHADER_PRIM_TRIANGLE_STRIP)
      state.num_vertices_per_primitive = 3;
   else
      unreachable("Invalid GS output primitive.");

   /* Extract the full control flow. It is going to be wrapped in an if statement. */
   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_cf_list(&impl->body), nir_after_cf_list(&impl->body));

   nir_builder builder;
   nir_builder *b = &builder; /* This is to avoid the & */
   nir_builder_init(b, impl);
   b->cursor = nir_before_cf_list(&impl->body);

   /* Workgroup barrier: wait for ES threads */
   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   /* Wrap the GS control flow. */
   nir_if *if_gs_thread = nir_push_if(b, nir_has_input_primitive_amd(b));

   nir_cf_reinsert(&extracted, b->cursor);
   b->cursor = nir_after_cf_list(&if_gs_thread->then_list);
   nir_pop_if(b, if_gs_thread);

   /* Workgroup barrier: wait for all GS threads to finish */
   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   if (state.streamout_enabled)
      ngg_gs_build_streamout(b, &state);

   /* Lower the GS intrinsics */
   lower_ngg_gs_intrinsics(shader, &state);
   b->cursor = nir_after_cf_list(&impl->body);

   if (!state.found_out_vtxcnt[0]) {
      fprintf(stderr, "Could not find set_vertex_and_primitive_count for stream 0. This would hang your GPU.");
      abort();
   }

   /* Emit the finale sequence */
   ngg_gs_finale(b, &state);
   nir_validate_shader(shader, "after emitting NGG GS");

   /* Cleanup */
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_metadata_preserve(impl, nir_metadata_none);
}

static void
ms_store_prim_indices(nir_builder *b,
                      nir_ssa_def *val,
                      nir_ssa_def *offset_src,
                      lower_ngg_ms_state *s)
{
   assert(val->num_components <= 3);

   if (!offset_src)
      offset_src = nir_imm_int(b, 0);

   nir_store_shared(b, nir_u2u8(b, val), offset_src, .base = s->layout.lds.indices_addr);
}

static nir_ssa_def *
ms_load_prim_indices(nir_builder *b,
                     nir_ssa_def *offset_src,
                     lower_ngg_ms_state *s)
{
   if (!offset_src)
      offset_src = nir_imm_int(b, 0);

   return nir_load_shared(b, 1, 8, offset_src, .base = s->layout.lds.indices_addr);
}

static void
ms_store_num_prims(nir_builder *b,
                   nir_ssa_def *store_val,
                   lower_ngg_ms_state *s)
{
   nir_ssa_def *addr = nir_imm_int(b, 0);
   nir_store_shared(b, nir_u2u32(b, store_val), addr, .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
}

static nir_ssa_def *
ms_load_num_prims(nir_builder *b,
                  lower_ngg_ms_state *s)
{
   nir_ssa_def *addr = nir_imm_int(b, 0);
   return nir_load_shared(b, 1, 32, addr, .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
}

static void
ms_store_cull_flag(nir_builder *b,
                   nir_ssa_def *val,
                   nir_ssa_def *offset_src,
                   lower_ngg_ms_state *s)
{
   assert(val->num_components == 1);
   assert(val->bit_size == 1);

   if (!offset_src)
      offset_src = nir_imm_int(b, 0);

   nir_store_shared(b, nir_b2i8(b, val), offset_src, .base = s->layout.lds.cull_flags_addr);
}

static nir_ssa_def *
lower_ms_store_output(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      lower_ngg_ms_state *s)
{
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   nir_ssa_def *store_val = intrin->src[0].ssa;

   /* Component makes no sense here. */
   assert(nir_intrinsic_component(intrin) == 0);

   if (io_sem.location == VARYING_SLOT_PRIMITIVE_COUNT) {
      /* Total number of primitives output by the mesh shader workgroup.
       * This can be read and written by any invocation any number of times.
       */

      /* Base, offset and component make no sense here. */
      assert(nir_src_is_const(intrin->src[1]) && nir_src_as_uint(intrin->src[1]) == 0);

      ms_store_num_prims(b, store_val, s);
   } else if (io_sem.location == VARYING_SLOT_PRIMITIVE_INDICES) {
      /* Contrary to the name, these are not primitive indices, but
       * vertex indices for each vertex of the output primitives.
       * The Mesh NV API has these stored in a flat array.
       */

      nir_ssa_def *offset_src = nir_get_io_offset_src(intrin)->ssa;
      ms_store_prim_indices(b, store_val, offset_src, s);
   } else {
      unreachable("Invalid mesh shader output");
   }

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static nir_ssa_def *
lower_ms_load_output(nir_builder *b,
                     nir_intrinsic_instr *intrin,
                     lower_ngg_ms_state *s)
{
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   /* Component makes no sense here. */
   assert(nir_intrinsic_component(intrin) == 0);

   if (io_sem.location == VARYING_SLOT_PRIMITIVE_COUNT) {
      /* Base, offset and component make no sense here. */
      assert(nir_src_is_const(intrin->src[1]) && nir_src_as_uint(intrin->src[1]) == 0);

      return ms_load_num_prims(b, s);
   } else if (io_sem.location == VARYING_SLOT_PRIMITIVE_INDICES) {
      nir_ssa_def *offset_src = nir_get_io_offset_src(intrin)->ssa;
      nir_ssa_def *index = ms_load_prim_indices(b, offset_src, s);
      return nir_u2u(b, index, intrin->dest.ssa.bit_size);
   }

   unreachable("Invalid mesh shader output");
}

static nir_ssa_def *
ms_arrayed_output_base_addr(nir_builder *b,
                            nir_ssa_def *arr_index,
                            unsigned driver_location,
                            unsigned num_arrayed_outputs)
{
   /* Address offset of the array item (vertex or primitive). */
   unsigned arr_index_stride = num_arrayed_outputs * 16u;
   nir_ssa_def *arr_index_off = nir_imul_imm(b, arr_index, arr_index_stride);

   /* IO address offset within the vertex or primitive data. */
   unsigned io_offset = driver_location * 16u;
   nir_ssa_def *io_off = nir_imm_int(b, io_offset);

   return nir_iadd_nuw(b, arr_index_off, io_off);
}

static void
update_ms_output_info_slot(lower_ngg_ms_state *s,
                           unsigned slot, unsigned base_off,
                           uint32_t components_mask)
{
   while (components_mask) {
      s->output_info[slot + base_off].components_mask |= components_mask & 0xF;

      components_mask >>= 4;
      base_off++;
   }
}

static void
update_ms_output_info(nir_intrinsic_instr *intrin,
                      const ms_out_part *out,
                      lower_ngg_ms_state *s)
{
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   nir_src *base_offset_src = nir_get_io_offset_src(intrin);
   uint32_t write_mask = nir_intrinsic_write_mask(intrin);
   unsigned component_offset = nir_intrinsic_component(intrin);

   nir_ssa_def *store_val = intrin->src[0].ssa;
   write_mask = util_widen_mask(write_mask, DIV_ROUND_UP(store_val->bit_size, 32));
   uint32_t components_mask = write_mask << component_offset;

   if (nir_src_is_const(*base_offset_src)) {
      /* Simply mark the components of the current slot as used. */
      unsigned base_off = nir_src_as_uint(*base_offset_src);
      update_ms_output_info_slot(s, io_sem.location, base_off, components_mask);
   } else {
      /* Indirect offset: mark the components of all slots as used. */
      for (unsigned base_off = 0; base_off < io_sem.num_slots; ++base_off)
         update_ms_output_info_slot(s, io_sem.location, base_off, components_mask);
   }
}

static nir_ssa_def *
regroup_store_val(nir_builder *b, nir_ssa_def *store_val)
{
   /* Vulkan spec 15.1.4-15.1.5:
    *
    * The shader interface consists of output slots with 4x 32-bit components.
    * Small bitsize components consume the same space as 32-bit components,
    * but 64-bit ones consume twice as much.
    *
    * The same output slot may consist of components of different bit sizes.
    * Therefore for simplicity we don't store small bitsize components
    * contiguously, but pad them instead. In practice, they are converted to
    * 32-bit and then stored contiguously.
    */

   if (store_val->bit_size < 32) {
      assert(store_val->num_components <= 4);
      nir_ssa_def *comps[4] = {0};
      for (unsigned c = 0; c < store_val->num_components; ++c)
         comps[c] = nir_u2u32(b, nir_channel(b, store_val, c));
      return nir_vec(b, comps, store_val->num_components);
   }

   return store_val;
}

static nir_ssa_def *
regroup_load_val(nir_builder *b, nir_ssa_def *load, unsigned dest_bit_size)
{
   if (dest_bit_size == load->bit_size)
      return load;

   /* Small bitsize components are not stored contiguously, take care of that here. */
   unsigned num_components = load->num_components;
   assert(num_components <= 4);
   nir_ssa_def *components[4] = {0};
   for (unsigned i = 0; i < num_components; ++i)
      components[i] = nir_u2u(b, nir_channel(b, load, i), dest_bit_size);

   return nir_vec(b, components, num_components);
}

static const ms_out_part *
ms_get_out_layout_part(unsigned location,
                       shader_info *info,
                       ms_out_mode *out_mode,
                       lower_ngg_ms_state *s)
{
   uint64_t mask = BITFIELD64_BIT(location);

   if (info->per_primitive_outputs & mask) {
      if (mask & s->layout.lds.prm_attr.mask) {
         *out_mode = ms_out_mode_lds;
         return &s->layout.lds.prm_attr;
      } else if (mask & s->layout.vram.prm_attr.mask) {
         *out_mode = ms_out_mode_vram;
         return &s->layout.vram.prm_attr;
      } else if (mask & s->layout.var.prm_attr.mask) {
         *out_mode = ms_out_mode_var;
         return &s->layout.var.prm_attr;
      }
   } else {
      if (mask & s->layout.lds.vtx_attr.mask) {
         *out_mode = ms_out_mode_lds;
         return &s->layout.lds.vtx_attr;
      } else if (mask & s->layout.vram.vtx_attr.mask) {
         *out_mode = ms_out_mode_vram;
         return &s->layout.vram.vtx_attr;
      } else if (mask & s->layout.var.vtx_attr.mask) {
         *out_mode = ms_out_mode_var;
         return &s->layout.var.vtx_attr;
      }
   }

   unreachable("Couldn't figure out mesh shader output mode.");
}

static void
ms_store_arrayed_output_intrin(nir_builder *b,
                               nir_intrinsic_instr *intrin,
                               lower_ngg_ms_state *s)
{
   unsigned location = nir_intrinsic_io_semantics(intrin).location;

   if (location == VARYING_SLOT_PRIMITIVE_INDICES) {
      /* EXT_mesh_shader primitive indices: array of vectors.
       * They don't count as per-primitive outputs, but the array is indexed
       * by the primitive index, so they are practically per-primitive.
       *
       * The max vertex count is 256, so these indices always fit 8 bits.
       * To reduce LDS use, store these as a flat array of 8-bit values.
       */
      assert(nir_src_is_const(*nir_get_io_offset_src(intrin)));
      assert(nir_src_as_uint(*nir_get_io_offset_src(intrin)) == 0);
      assert(nir_intrinsic_component(intrin) == 0);

      nir_ssa_def *store_val = intrin->src[0].ssa;
      nir_ssa_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
      nir_ssa_def *offset = nir_imul_imm(b, arr_index, s->vertices_per_prim);
      ms_store_prim_indices(b, store_val, offset, s);
      return;
   } else if (location == VARYING_SLOT_CULL_PRIMITIVE) {
      /* EXT_mesh_shader cull primitive: per-primitive bool.
       * To reduce LDS use, store these as an array of 8-bit values.
       */
      assert(nir_src_is_const(*nir_get_io_offset_src(intrin)));
      assert(nir_src_as_uint(*nir_get_io_offset_src(intrin)) == 0);
      assert(nir_intrinsic_component(intrin) == 0);
      assert(nir_intrinsic_write_mask(intrin) == 1);

      nir_ssa_def *store_val = intrin->src[0].ssa;
      nir_ssa_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
      nir_ssa_def *offset = nir_imul_imm(b, arr_index, s->vertices_per_prim);
      ms_store_cull_flag(b, store_val, offset, s);
      return;
   }

   ms_out_mode out_mode;
   const ms_out_part *out = ms_get_out_layout_part(location, &b->shader->info, &out_mode, s);
   update_ms_output_info(intrin, out, s);

   /* We compact the LDS size (we don't reserve LDS space for outputs which can
    * be stored in variables), so we can't rely on the original driver_location.
    * Instead, we compute the first free location based on the output mask.
    */
   unsigned driver_location = util_bitcount64(out->mask & u_bit_consecutive64(0, location));
   unsigned component_offset = nir_intrinsic_component(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   unsigned num_outputs = util_bitcount64(out->mask);
   unsigned const_off = out->addr + component_offset * 4;

   nir_ssa_def *store_val = regroup_store_val(b, intrin->src[0].ssa);
   nir_ssa_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
   nir_ssa_def *base_addr = ms_arrayed_output_base_addr(b, arr_index, driver_location, num_outputs);
   nir_ssa_def *base_offset = nir_get_io_offset_src(intrin)->ssa;
   nir_ssa_def *base_addr_off = nir_imul_imm(b, base_offset, 16u);
   nir_ssa_def *addr = nir_iadd_nuw(b, base_addr, base_addr_off);

   if (out_mode == ms_out_mode_lds) {
      nir_store_shared(b, store_val, addr, .base = const_off,
                     .write_mask = write_mask, .align_mul = 16,
                     .align_offset = const_off % 16);
   } else if (out_mode == ms_out_mode_vram) {
      nir_ssa_def *ring = nir_load_ring_mesh_scratch_amd(b);
      nir_ssa_def *off = nir_load_ring_mesh_scratch_offset_amd(b);
      nir_store_buffer_amd(b, store_val, ring, addr, off,
                           .base = const_off,
                           .write_mask = write_mask,
                           .memory_modes = nir_var_shader_out);
   } else if (out_mode == ms_out_mode_var) {
      if (store_val->bit_size > 32) {
         /* Split 64-bit store values to 32-bit components. */
         store_val = nir_bitcast_vector(b, store_val, 32);
         /* Widen the write mask so it is in 32-bit components. */
         write_mask = util_widen_mask(write_mask, store_val->bit_size / 32);
      }

      u_foreach_bit(comp, write_mask) {
         nir_ssa_def *val = nir_channel(b, store_val, comp);
         unsigned idx = location * 4 + comp + component_offset;
         nir_store_var(b, s->out_variables[idx], val, 0x1);
      }
   } else {
      unreachable("Invalid MS output mode for store");
   }
}

static nir_ssa_def *
ms_load_arrayed_output(nir_builder *b,
                       nir_ssa_def *arr_index,
                       nir_ssa_def *base_offset,
                       unsigned location,
                       unsigned component_offset,
                       unsigned num_components,
                       unsigned load_bit_size,
                       lower_ngg_ms_state *s)
{
   ms_out_mode out_mode;
   const ms_out_part *out = ms_get_out_layout_part(location, &b->shader->info, &out_mode, s);

   unsigned component_addr_off = component_offset * 4;
   unsigned num_outputs = util_bitcount64(out->mask);
   unsigned const_off = out->addr + component_offset * 4;

   /* Use compacted driver location instead of the original. */
   unsigned driver_location = util_bitcount64(out->mask & u_bit_consecutive64(0, location));

   nir_ssa_def *base_addr = ms_arrayed_output_base_addr(b, arr_index, driver_location, num_outputs);
   nir_ssa_def *base_addr_off = nir_imul_imm(b, base_offset, 16);
   nir_ssa_def *addr = nir_iadd_nuw(b, base_addr, base_addr_off);

   if (out_mode == ms_out_mode_lds) {
      return nir_load_shared(b, num_components, load_bit_size, addr, .align_mul = 16,
                             .align_offset = component_addr_off % 16,
                             .base = const_off);
   } else if (out_mode == ms_out_mode_vram) {
      nir_ssa_def *ring = nir_load_ring_mesh_scratch_amd(b);
      nir_ssa_def *off = nir_load_ring_mesh_scratch_offset_amd(b);
      return nir_load_buffer_amd(b, num_components, load_bit_size, ring, addr, off,
                                 .base = const_off,
                                 .memory_modes = nir_var_shader_out);
   } else if (out_mode == ms_out_mode_var) {
      nir_ssa_def *arr[8] = {0};
      unsigned num_32bit_components = num_components * load_bit_size / 32;
      for (unsigned comp = 0; comp < num_32bit_components; ++comp) {
         unsigned idx = location * 4 + comp + component_addr_off;
         arr[comp] = nir_load_var(b, s->out_variables[idx]);
      }
      if (load_bit_size > 32)
         return nir_extract_bits(b, arr, 1, 0, num_components, load_bit_size);
      return nir_vec(b, arr, num_components);
   } else {
      unreachable("Invalid MS output mode for load");
   }
}

static nir_ssa_def *
ms_load_arrayed_output_intrin(nir_builder *b,
                              nir_intrinsic_instr *intrin,
                              lower_ngg_ms_state *s)
{
   nir_ssa_def *arr_index = nir_get_io_arrayed_index_src(intrin)->ssa;
   nir_ssa_def *base_offset = nir_get_io_offset_src(intrin)->ssa;

   unsigned location = nir_intrinsic_io_semantics(intrin).location;
   unsigned component_offset = nir_intrinsic_component(intrin);
   unsigned bit_size = intrin->dest.ssa.bit_size;
   unsigned num_components = intrin->dest.ssa.num_components;
   unsigned load_bit_size = MAX2(bit_size, 32);

   nir_ssa_def *load =
      ms_load_arrayed_output(b, arr_index, base_offset, location, component_offset,
                             num_components, load_bit_size, s);

   return regroup_load_val(b, load, bit_size);
}

static nir_ssa_def *
lower_ms_load_workgroup_index(nir_builder *b,
                              UNUSED nir_intrinsic_instr *intrin,
                              lower_ngg_ms_state *s)
{
   return s->workgroup_index;
}

static nir_ssa_def *
lower_ms_set_vertex_and_primitive_count(nir_builder *b,
                                        nir_intrinsic_instr *intrin,
                                        lower_ngg_ms_state *s)
{
   /* If either the number of vertices or primitives is zero, set both of them to zero. */
   nir_ssa_def *num_vtx = nir_read_first_invocation(b, intrin->src[0].ssa);
   nir_ssa_def *num_prm = nir_read_first_invocation(b, intrin->src[1].ssa);
   nir_ssa_def *zero = nir_imm_int(b, 0);
   nir_ssa_def *is_either_zero = nir_ieq(b, nir_umin(b, num_vtx, num_prm), zero);
   num_vtx = nir_bcsel(b, is_either_zero, zero, num_vtx);
   num_prm = nir_bcsel(b, is_either_zero, zero, num_prm);

   nir_store_var(b, s->vertex_count_var, num_vtx, 0x1);
   nir_store_var(b, s->primitive_count_var, num_prm, 0x1);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static nir_ssa_def *
update_ms_scoped_barrier(nir_builder *b,
                         nir_intrinsic_instr *intrin,
                         lower_ngg_ms_state *s)
{
   /* Output loads and stores are lowered to shared memory access,
    * so we have to update the barriers to also reflect this.
    */
   unsigned mem_modes = nir_intrinsic_memory_modes(intrin);
   if (mem_modes & nir_var_shader_out)
      mem_modes |= nir_var_mem_shared;
   else
      return NULL;

   nir_intrinsic_set_memory_modes(intrin, mem_modes);

   return NIR_LOWER_INSTR_PROGRESS;
}

static nir_ssa_def *
lower_ms_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ngg_ms_state *s = (lower_ngg_ms_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return NULL;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_store_output:
      return lower_ms_store_output(b, intrin, s);
   case nir_intrinsic_load_output:
      return lower_ms_load_output(b, intrin, s);
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_primitive_output:
      ms_store_arrayed_output_intrin(b, intrin, s);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_primitive_output:
      return ms_load_arrayed_output_intrin(b, intrin, s);
   case nir_intrinsic_scoped_barrier:
      return update_ms_scoped_barrier(b, intrin, s);
   case nir_intrinsic_load_workgroup_index:
      return lower_ms_load_workgroup_index(b, intrin, s);
   case nir_intrinsic_set_vertex_and_primitive_count:
      return lower_ms_set_vertex_and_primitive_count(b, intrin, s);
   default:
      unreachable("Not a lowerable mesh shader intrinsic.");
   }
}

static bool
filter_ms_intrinsic(const nir_instr *instr,
                    UNUSED const void *st)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_per_primitive_output ||
          intrin->intrinsic == nir_intrinsic_load_per_primitive_output ||
          intrin->intrinsic == nir_intrinsic_scoped_barrier ||
          intrin->intrinsic == nir_intrinsic_load_workgroup_index ||
          intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count;
}

static void
lower_ms_intrinsics(nir_shader *shader, lower_ngg_ms_state *s)
{
   nir_shader_lower_instructions(shader, filter_ms_intrinsic, lower_ms_intrinsic, s);
}

static void
ms_emit_arrayed_outputs(nir_builder *b,
                        nir_ssa_def *invocation_index,
                        uint64_t mask,
                        lower_ngg_ms_state *s)
{
   nir_ssa_def *zero = nir_imm_int(b, 0);

   u_foreach_bit64(slot, mask) {
      /* Should not occour here, handled separately. */
      assert(slot != VARYING_SLOT_PRIMITIVE_COUNT && slot != VARYING_SLOT_PRIMITIVE_INDICES);

      const nir_io_semantics io_sem = { .location = slot, .num_slots = 1 };
      unsigned component_mask = s->output_info[slot].components_mask;

      while (component_mask) {
         int start_comp = 0, num_components = 1;
         u_bit_scan_consecutive_range(&component_mask, &start_comp, &num_components);

         nir_ssa_def *load =
            ms_load_arrayed_output(b, invocation_index, zero, slot, start_comp,
                                   num_components, 32, s);

         nir_store_output(b, load, nir_imm_int(b, 0), .base = slot, .component = start_comp,
                          .io_semantics = io_sem);
      }
   }
}

static void
emit_ms_prelude(nir_builder *b, lower_ngg_ms_state *s)
{
   b->cursor = nir_before_cf_list(&b->impl->body);

   /* Initialize NIR variables for same-invocation outputs. */
   uint64_t same_invocation_output_mask = s->layout.var.prm_attr.mask | s->layout.var.vtx_attr.mask;

   u_foreach_bit64(slot, same_invocation_output_mask) {
      for (unsigned comp = 0; comp < 4; ++comp) {
         unsigned idx = slot * 4 + comp;
         s->out_variables[idx] = nir_local_variable_create(b->impl, glsl_uint_type(), "ms_var_output");
         nir_store_var(b, s->out_variables[idx], nir_imm_int(b, 0), 0x1);
      }
   }

   bool uses_workgroup_id =
      BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_WORKGROUP_ID) ||
      BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_WORKGROUP_INDEX);

   if (!uses_workgroup_id)
      return;

   /* The HW doesn't support a proper workgroup index for vertex processing stages,
    * so we use the vertex ID which is equivalent to the index of the current workgroup
    * within the current dispatch.
    *
    * Due to the register programming of mesh shaders, this value is only filled for
    * the first invocation of the first wave. To let other waves know, we use LDS.
    */
   nir_ssa_def *workgroup_index = nir_load_vertex_id_zero_base(b);

   if (s->api_workgroup_size <= s->wave_size) {
      /* API workgroup is small, so we don't need to use LDS. */
      s->workgroup_index = nir_read_first_invocation(b, workgroup_index);
      return;
   }

   unsigned workgroup_index_lds_addr = s->layout.lds.workgroup_info_addr + lds_ms_wg_index;

   nir_ssa_def *zero = nir_imm_int(b, 0);
   nir_ssa_def *dont_care = nir_ssa_undef(b, 1, 32);
   nir_ssa_def *loaded_workgroup_index = NULL;

   /* Use elect to make sure only 1 invocation uses LDS. */
   nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
   {
      nir_ssa_def *wave_id = nir_load_subgroup_id(b);
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, wave_id, 0));
      {
         nir_store_shared(b, workgroup_index, zero, .base = workgroup_index_lds_addr);
         nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                               .memory_scope = NIR_SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
      }
      nir_push_else(b, if_wave_0);
      {
         nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                               .memory_scope = NIR_SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
         loaded_workgroup_index = nir_load_shared(b, 1, 32, zero, .base = workgroup_index_lds_addr);
      }
      nir_pop_if(b, if_wave_0);

      workgroup_index = nir_if_phi(b, workgroup_index, loaded_workgroup_index);
   }
   nir_pop_if(b, if_elected);

   workgroup_index = nir_if_phi(b, workgroup_index, dont_care);
   s->workgroup_index = nir_read_first_invocation(b, workgroup_index);
}

static void
set_nv_ms_final_output_counts(nir_builder *b,
                               lower_ngg_ms_state *s,
                               nir_ssa_def **out_num_prm,
                               nir_ssa_def **out_num_vtx)
{
   /* Limitations of the NV extension:
    * - Number of primitives can be written and read by any invocation,
    *   so we have to store/load it to/from LDS to make sure the general case works.
    * - Number of vertices is not actually known, so we just always use the
    *   maximum number here.
    */
   nir_ssa_def *loaded_num_prm;
   nir_ssa_def *dont_care = nir_ssa_undef(b, 1, 32);
   nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
   {
      loaded_num_prm = ms_load_num_prims(b, s);
   }
   nir_pop_if(b, if_elected);
   loaded_num_prm = nir_if_phi(b, loaded_num_prm, dont_care);
   nir_ssa_def *num_prm = nir_read_first_invocation(b, loaded_num_prm);
   nir_ssa_def *num_vtx = nir_imm_int(b, b->shader->info.mesh.max_vertices_out);
   num_prm = nir_umin(b, num_prm, nir_imm_int(b, b->shader->info.mesh.max_primitives_out));

   /* If the shader doesn't actually create any primitives, don't allocate any output. */
   num_vtx = nir_bcsel(b, nir_ieq_imm(b, num_prm, 0), nir_imm_int(b, 0), num_vtx);

   /* Emit GS_ALLOC_REQ on Wave 0 to let the HW know the output size. */
   nir_ssa_def *wave_id = nir_load_subgroup_id(b);
   nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, wave_id, 0));
   {
      nir_alloc_vertices_and_primitives_amd(b, num_vtx, num_prm);
   }
   nir_pop_if(b, if_wave_0);

   *out_num_prm = num_prm;
   *out_num_vtx = num_vtx;
}

static void
set_ms_final_output_counts(nir_builder *b,
                           lower_ngg_ms_state *s,
                           nir_ssa_def **out_num_prm,
                           nir_ssa_def **out_num_vtx)
{
   /* The spec allows the numbers to be divergent, and in that case we need to
    * use the values from the first invocation. Also the HW requires us to set
    * both to 0 if either was 0.
    *
    * These are already done by the lowering.
    */
   nir_ssa_def *num_prm = nir_load_var(b, s->primitive_count_var);
   nir_ssa_def *num_vtx = nir_load_var(b, s->vertex_count_var);

   if (s->hw_workgroup_size <= s->wave_size) {
      /* Single-wave mesh shader workgroup. */
      nir_alloc_vertices_and_primitives_amd(b, num_vtx, num_prm);
      *out_num_prm = num_prm;
      *out_num_vtx = num_vtx;
      return;
   }

   /* Multi-wave mesh shader workgroup:
    * We need to use LDS to distribute the correct values to the other waves.
    *
    * TODO:
    * If we can prove that the values are workgroup-uniform, we can skip this
    * and just use whatever the current wave has. However, NIR divergence analysis
    * currently doesn't support this.
    */

   nir_ssa_def *zero = nir_imm_int(b, 0);

   nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
   {
      nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
      {
         nir_store_shared(b, nir_vec2(b, num_prm, num_vtx), zero,
                          .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
      }
      nir_pop_if(b, if_elected);

      nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                            .memory_scope = NIR_SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_mem_shared);

      nir_alloc_vertices_and_primitives_amd(b, num_vtx, num_prm);
   }
   nir_push_else(b, if_wave_0);
   {
      nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                            .memory_scope = NIR_SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_mem_shared);

      nir_ssa_def *prm_vtx = NULL;
      nir_ssa_def *dont_care_2x32 = nir_ssa_undef(b, 2, 32);
      nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
      {
         prm_vtx = nir_load_shared(b, 2, 32, zero,
                                   .base = s->layout.lds.workgroup_info_addr + lds_ms_num_prims);
      }
      nir_pop_if(b, if_elected);

      prm_vtx = nir_if_phi(b, prm_vtx, dont_care_2x32);
      num_prm = nir_read_first_invocation(b, nir_channel(b, prm_vtx, 0));
      num_vtx = nir_read_first_invocation(b, nir_channel(b, prm_vtx, 1));

      nir_store_var(b, s->primitive_count_var, num_prm, 0x1);
      nir_store_var(b, s->vertex_count_var, num_vtx, 0x1);
   }
   nir_pop_if(b, if_wave_0);

   *out_num_prm = nir_load_var(b, s->primitive_count_var);
   *out_num_vtx = nir_load_var(b, s->vertex_count_var);
}

static void
emit_ms_finale(nir_builder *b, lower_ngg_ms_state *s)
{
   /* We assume there is always a single end block in the shader. */
   nir_block *last_block = nir_impl_last_block(b->impl);
   b->cursor = nir_after_block(last_block);

   nir_scoped_barrier(b, .execution_scope=NIR_SCOPE_WORKGROUP, .memory_scope=NIR_SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_shader_out|nir_var_mem_shared);

   nir_ssa_def *num_prm;
   nir_ssa_def *num_vtx;

   if (b->shader->info.mesh.nv)
      set_nv_ms_final_output_counts(b, s, &num_prm, &num_vtx);
   else
      set_ms_final_output_counts(b, s, &num_prm, &num_vtx);

   nir_ssa_def *invocation_index = nir_load_local_invocation_index(b);

   /* Load vertex/primitive attributes from shared memory and
    * emit store_output intrinsics for them.
    *
    * Contrary to the semantics of the API mesh shader, these are now
    * compliant with NGG HW semantics, meaning that these store the
    * current thread's vertex attributes in a way the HW can export.
    */

   /* Export vertices. */
   nir_ssa_def *has_output_vertex = nir_ilt(b, invocation_index, num_vtx);
   nir_if *if_has_output_vertex = nir_push_if(b, has_output_vertex);
   {
      /* All per-vertex attributes. */
      ms_emit_arrayed_outputs(b, invocation_index, s->per_vertex_outputs, s);
      nir_export_vertex_amd(b);
   }
   nir_pop_if(b, if_has_output_vertex);

   /* Export primitives. */
   nir_ssa_def *has_output_primitive = nir_ilt(b, invocation_index, num_prm);
   nir_if *if_has_output_primitive = nir_push_if(b, has_output_primitive);
   {
      /* Generic per-primitive attributes. */
      ms_emit_arrayed_outputs(b, invocation_index, s->per_primitive_outputs, s);

      /* Insert layer output store if the pipeline uses multiview but the API shader doesn't write it. */
      if (s->insert_layer_output) {
         nir_ssa_def *layer = nir_load_view_index(b);
         const nir_io_semantics io_sem = { .location = VARYING_SLOT_LAYER, .num_slots = 1 };
         nir_store_output(b, layer, nir_imm_int(b, 0), .base = VARYING_SLOT_LAYER, .component = 0, .io_semantics = io_sem);
         b->shader->info.outputs_written |= VARYING_BIT_LAYER;
         b->shader->info.per_primitive_outputs |= VARYING_BIT_LAYER;
      }

      /* Primitive connectivity data: describes which vertices the primitive uses. */
      nir_ssa_def *prim_idx_addr = nir_imul_imm(b, invocation_index, s->vertices_per_prim);
      nir_ssa_def *indices_loaded = nir_load_shared(b, s->vertices_per_prim, 8, prim_idx_addr, .base = s->layout.lds.indices_addr);
      nir_ssa_def *cull_flag = NULL;

      if (s->uses_cull_flags) {
         nir_ssa_def *loaded_cull_flag = nir_load_shared(b, 1, 8, prim_idx_addr, .base = s->layout.lds.cull_flags_addr);
         cull_flag = nir_i2b1(b, nir_u2u32(b, loaded_cull_flag));
      }

      nir_ssa_def *indices[3];
      nir_ssa_def *max_vtx_idx = nir_iadd_imm(b, num_vtx, -1u);

      for (unsigned i = 0; i < s->vertices_per_prim; ++i) {
         indices[i] = nir_u2u32(b, nir_channel(b, indices_loaded, i));
         indices[i] = nir_umin(b, indices[i], max_vtx_idx);
      }

      nir_ssa_def *prim_exp_arg = emit_pack_ngg_prim_exp_arg(b, s->vertices_per_prim, indices, cull_flag, false);
      nir_export_primitive_amd(b, prim_exp_arg);
   }
   nir_pop_if(b, if_has_output_primitive);
}

static void
handle_smaller_ms_api_workgroup(nir_builder *b,
                                lower_ngg_ms_state *s)
{
   if (s->api_workgroup_size >= s->hw_workgroup_size)
      return;

   /* Handle barriers manually when the API workgroup
    * size is less than the HW workgroup size.
    *
    * The problem is that the real workgroup launched on NGG HW
    * will be larger than the size specified by the API, and the
    * extra waves need to keep up with barriers in the API waves.
    *
    * There are 2 different cases:
    * 1. The whole API workgroup fits in a single wave.
    *    We can shrink the barriers to subgroup scope and
    *    don't need to insert any extra ones.
    * 2. The API workgroup occupies multiple waves, but not
    *    all. In this case, we emit code that consumes every
    *    barrier on the extra waves.
    */
   assert(s->hw_workgroup_size % s->wave_size == 0);
   bool scan_barriers = ALIGN(s->api_workgroup_size, s->wave_size) < s->hw_workgroup_size;
   bool can_shrink_barriers = s->api_workgroup_size <= s->wave_size;
   bool need_additional_barriers = scan_barriers && !can_shrink_barriers;

   unsigned api_waves_in_flight_addr = s->layout.lds.workgroup_info_addr + lds_ms_num_api_waves;
   unsigned num_api_waves = DIV_ROUND_UP(s->api_workgroup_size, s->wave_size);

   /* Scan the shader for workgroup barriers. */
   if (scan_barriers) {
      bool has_any_workgroup_barriers = false;

      nir_foreach_block(block, b->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            bool is_workgroup_barrier =
               intrin->intrinsic == nir_intrinsic_scoped_barrier &&
               nir_intrinsic_execution_scope(intrin) == NIR_SCOPE_WORKGROUP;

            if (!is_workgroup_barrier)
               continue;

            if (can_shrink_barriers) {
               /* Every API invocation runs in the first wave.
                * In this case, we can change the barriers to subgroup scope
                * and avoid adding additional barriers.
                */
               nir_intrinsic_set_memory_scope(intrin, NIR_SCOPE_SUBGROUP);
               nir_intrinsic_set_execution_scope(intrin, NIR_SCOPE_SUBGROUP);
            } else {
               has_any_workgroup_barriers = true;
            }
         }
      }

      need_additional_barriers &= has_any_workgroup_barriers;
   }

   /* Extract the full control flow of the shader. */
   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_cf_list(&b->impl->body), nir_after_cf_list(&b->impl->body));
   b->cursor = nir_before_cf_list(&b->impl->body);

   /* Wrap the shader in an if to ensure that only the necessary amount of lanes run it. */
   nir_ssa_def *invocation_index = nir_load_local_invocation_index(b);
   nir_ssa_def *zero = nir_imm_int(b, 0);

   if (need_additional_barriers) {
      /* First invocation stores 0 to number of API waves in flight. */
      nir_if *if_first_in_workgroup = nir_push_if(b, nir_ieq_imm(b, invocation_index, 0));
      {
         nir_store_shared(b, nir_imm_int(b, num_api_waves), zero, .base = api_waves_in_flight_addr);
      }
      nir_pop_if(b, if_first_in_workgroup);

      nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                            .memory_scope = NIR_SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_shader_out | nir_var_mem_shared);
   }

   nir_ssa_def *has_api_ms_invocation = nir_ult(b, invocation_index, nir_imm_int(b, s->api_workgroup_size));
   nir_if *if_has_api_ms_invocation = nir_push_if(b, has_api_ms_invocation);
   {
      nir_cf_reinsert(&extracted, b->cursor);
      b->cursor = nir_after_cf_list(&if_has_api_ms_invocation->then_list);

      if (need_additional_barriers) {
         /* One invocation in each API wave decrements the number of API waves in flight. */
         nir_if *if_elected_again = nir_push_if(b, nir_elect(b, 1));
         {
            nir_shared_atomic_add(b, 32, zero, nir_imm_int(b, -1u), .base = api_waves_in_flight_addr);
         }
         nir_pop_if(b, if_elected_again);

         nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                               .memory_scope = NIR_SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_shader_out | nir_var_mem_shared);
      }
   }
   nir_pop_if(b, if_has_api_ms_invocation);

   if (need_additional_barriers) {
      /* Make sure that waves that don't run any API invocations execute
       * the same amount of barriers as those that do.
       *
       * We do this by executing a barrier until the number of API waves
       * in flight becomes zero.
       */
      nir_ssa_def *has_api_ms_ballot = nir_ballot(b, 1, s->wave_size, has_api_ms_invocation);
      nir_ssa_def *wave_has_no_api_ms = nir_ieq_imm(b, has_api_ms_ballot, 0);
      nir_if *if_wave_has_no_api_ms = nir_push_if(b, wave_has_no_api_ms);
      {
         nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
         {
            nir_loop *loop = nir_push_loop(b);
            {
               nir_scoped_barrier(b, .execution_scope = NIR_SCOPE_WORKGROUP,
                                     .memory_scope = NIR_SCOPE_WORKGROUP,
                                     .memory_semantics = NIR_MEMORY_ACQ_REL,
                                     .memory_modes = nir_var_shader_out | nir_var_mem_shared);

               nir_ssa_def *loaded = nir_load_shared(b, 1, 32, zero, .base = api_waves_in_flight_addr);
               nir_if *if_break = nir_push_if(b, nir_ieq_imm(b, loaded, 0));
               {
                  nir_jump(b, nir_jump_break);
               }
               nir_pop_if(b, if_break);
            }
            nir_pop_loop(b, loop);
         }
         nir_pop_if(b, if_elected);
      }
      nir_pop_if(b, if_wave_has_no_api_ms);
   }
}

static void
ms_move_output(ms_out_part *from, ms_out_part *to)
{
   uint64_t loc = util_logbase2_64(from->mask);
   uint64_t bit = BITFIELD64_BIT(loc);
   from->mask ^= bit;
   to->mask |= bit;
}

static void
ms_calculate_arrayed_output_layout(ms_out_mem_layout *l,
                                   unsigned max_vertices,
                                   unsigned max_primitives)
{
   uint32_t lds_vtx_attr_size = util_bitcount64(l->lds.vtx_attr.mask) * max_vertices * 16;
   uint32_t lds_prm_attr_size = util_bitcount64(l->lds.prm_attr.mask) * max_primitives * 16;
   l->lds.prm_attr.addr = ALIGN(l->lds.vtx_attr.addr + lds_vtx_attr_size, 16);
   l->lds.total_size = l->lds.prm_attr.addr + lds_prm_attr_size;

   uint32_t vram_vtx_attr_size = util_bitcount64(l->vram.vtx_attr.mask) * max_vertices * 16;
   l->vram.prm_attr.addr = ALIGN(l->vram.vtx_attr.addr + vram_vtx_attr_size, 16);
}

static ms_out_mem_layout
ms_calculate_output_layout(unsigned api_shared_size,
                           uint64_t per_vertex_output_mask,
                           uint64_t per_primitive_output_mask,
                           uint64_t cross_invocation_output_access,
                           unsigned max_vertices,
                           unsigned max_primitives,
                           unsigned vertices_per_prim,
                           bool uses_cull)
{
   uint64_t lds_per_vertex_output_mask = per_vertex_output_mask & cross_invocation_output_access;
   uint64_t lds_per_primitive_output_mask = per_primitive_output_mask & cross_invocation_output_access;

   /* Shared memory used by the API shader. */
   ms_out_mem_layout l = { .lds = { .total_size = api_shared_size } };

   /* Outputs without cross-invocation access can be stored in variables. */
   l.var.vtx_attr.mask = per_vertex_output_mask & ~lds_per_vertex_output_mask;
   l.var.prm_attr.mask = per_primitive_output_mask & ~lds_per_primitive_output_mask;

   /* Workgroup information, see ms_workgroup_* for the layout. */
   l.lds.workgroup_info_addr = ALIGN(l.lds.total_size, 16);
   l.lds.total_size = l.lds.workgroup_info_addr + 16;

   /* Per-vertex and per-primitive output attributes.
    * Outputs without cross-invocation access are not included here.
    * First, try to put all outputs into LDS (shared memory).
    * If they don't fit, try to move them to VRAM one by one.
    */
   l.lds.vtx_attr.addr = ALIGN(l.lds.total_size, 16);
   l.lds.vtx_attr.mask = lds_per_vertex_output_mask;
   l.lds.prm_attr.mask = lds_per_primitive_output_mask;
   ms_calculate_arrayed_output_layout(&l, max_vertices, max_primitives);

   /* NGG shaders can only address up to 32K LDS memory.
    * The spec requires us to allow the application to use at least up to 28K
    * shared memory. Additionally, we reserve 2K for driver internal use
    * (eg. primitive indices and such, see below).
    *
    * Move the outputs that do not fit LDS, to VRAM.
    * Start with per-primitive attributes, because those are grouped at the end.
    */
   while (l.lds.total_size >= 30 * 1024) {
      if (l.lds.prm_attr.mask)
         ms_move_output(&l.lds.prm_attr, &l.vram.prm_attr);
      else if (l.lds.vtx_attr.mask)
         ms_move_output(&l.lds.vtx_attr, &l.vram.vtx_attr);
      else
         unreachable("API shader uses too much shared memory.");

      ms_calculate_arrayed_output_layout(&l, max_vertices, max_primitives);
   }

   /* Indices: flat array of 8-bit vertex indices for each primitive. */
   l.lds.indices_addr = ALIGN(l.lds.total_size, 16);
   l.lds.total_size = l.lds.indices_addr + max_primitives * vertices_per_prim;

   if (uses_cull) {
      /* Cull flags: array of 8-bit cull flags for each primitive, 1=cull, 0=keep. */
      l.lds.cull_flags_addr = ALIGN(l.lds.total_size, 16);
      l.lds.total_size = l.lds.cull_flags_addr + max_primitives;
   }

   /* NGG is only allowed to address up to 32K of LDS. */
   assert(l.lds.total_size <= 32 * 1024);
   return l;
}

void
ac_nir_lower_ngg_ms(nir_shader *shader,
                    bool *out_needs_scratch_ring,
                    unsigned wave_size,
                    bool multiview)
{
   unsigned vertices_per_prim =
      num_mesh_vertices_per_primitive(shader->info.mesh.primitive_type);

   uint64_t special_outputs =
      BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_COUNT) | BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_INDICES) |
      BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE);
   uint64_t per_vertex_outputs =
      shader->info.outputs_written & ~shader->info.per_primitive_outputs & ~special_outputs;
   uint64_t per_primitive_outputs =
      shader->info.per_primitive_outputs & shader->info.outputs_written & ~special_outputs;

   /* Whether the shader uses CullPrimitiveEXT */
   bool uses_cull = shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_CULL_PRIMITIVE);
   /* Can't handle indirect register addressing, pretend as if they were cross-invocation. */
   uint64_t cross_invocation_access = shader->info.mesh.ms_cross_invocation_output_access |
                                      shader->info.outputs_accessed_indirectly;

   unsigned max_vertices = shader->info.mesh.max_vertices_out;
   unsigned max_primitives = shader->info.mesh.max_primitives_out;

   ms_out_mem_layout layout =
      ms_calculate_output_layout(shader->info.shared_size, per_vertex_outputs, per_primitive_outputs,
                                 cross_invocation_access, max_vertices, max_primitives, vertices_per_prim, uses_cull);

   shader->info.shared_size = layout.lds.total_size;
   *out_needs_scratch_ring = layout.vram.vtx_attr.mask || layout.vram.prm_attr.mask;

   /* The workgroup size that is specified by the API shader may be different
    * from the size of the workgroup that actually runs on the HW, due to the
    * limitations of NGG: max 0/1 vertex and 0/1 primitive per lane is allowed.
    *
    * Therefore, we must make sure that when the API workgroup size is smaller,
    * we don't run the API shader on more HW invocations than is necessary.
    */
   unsigned api_workgroup_size = shader->info.workgroup_size[0] *
                                 shader->info.workgroup_size[1] *
                                 shader->info.workgroup_size[2];

   unsigned hw_workgroup_size =
      ALIGN(MAX3(api_workgroup_size, max_primitives, max_vertices), wave_size);

   lower_ngg_ms_state state = {
      .layout = layout,
      .wave_size = wave_size,
      .per_vertex_outputs = per_vertex_outputs,
      .per_primitive_outputs = per_primitive_outputs,
      .vertices_per_prim = vertices_per_prim,
      .api_workgroup_size = api_workgroup_size,
      .hw_workgroup_size = hw_workgroup_size,
      .insert_layer_output = multiview && !(shader->info.outputs_written & VARYING_BIT_LAYER),
      .uses_cull_flags = uses_cull,
   };

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);

   state.vertex_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "vertex_count_var");
   state.primitive_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "primitive_count_var");

   nir_builder builder;
   nir_builder *b = &builder; /* This is to avoid the & */
   nir_builder_init(b, impl);
   b->cursor = nir_before_cf_list(&impl->body);

   handle_smaller_ms_api_workgroup(b, &state);
   emit_ms_prelude(b, &state);
   nir_metadata_preserve(impl, nir_metadata_none);

   lower_ms_intrinsics(shader, &state);

   emit_ms_finale(b, &state);
   nir_metadata_preserve(impl, nir_metadata_none);

   /* Cleanup */
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_lower_alu_to_scalar(shader, NULL, NULL);
   nir_lower_phis_to_scalar(shader, true);

   nir_validate_shader(shader, "after emitting NGG MS");
}
