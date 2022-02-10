/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef IRIS_CONTEXT_H
#define IRIS_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/perf/u_trace.h"
#include "util/set.h"
#include "util/slab.h"
#include "util/u_debug.h"
#include "util/u_threaded_context.h"
#include "intel/blorp/blorp.h"
#include "intel/dev/intel_debug.h"
#include "intel/common/intel_l3_config.h"
#include "intel/compiler/brw_compiler.h"
#include "intel/ds/intel_driver_ds.h"
#include "iris_batch.h"
#include "iris_binder.h"
#include "iris_fence.h"
#include "iris_resource.h"
#include "iris_screen.h"

struct iris_bo;
struct iris_context;
struct blorp_batch;
struct blorp_params;

#define IRIS_MAX_TEXTURE_BUFFER_SIZE (1 << 27)
#define IRIS_MAX_TEXTURE_SAMPLERS 32
/* IRIS_MAX_ABOS and IRIS_MAX_SSBOS must be the same. */
#define IRIS_MAX_ABOS 16
#define IRIS_MAX_SSBOS 16
#define IRIS_MAX_VIEWPORTS 16
#define IRIS_MAX_CLIP_PLANES 8
#define IRIS_MAX_GLOBAL_BINDINGS 32

enum iris_param_domain {
   BRW_PARAM_DOMAIN_BUILTIN = 0,
   BRW_PARAM_DOMAIN_IMAGE,
};

enum {
   DRI_CONF_BO_REUSE_DISABLED,
   DRI_CONF_BO_REUSE_ALL
};

#define BRW_PARAM(domain, val)   (BRW_PARAM_DOMAIN_##domain << 24 | (val))
#define BRW_PARAM_DOMAIN(param)  ((uint32_t)(param) >> 24)
#define BRW_PARAM_VALUE(param)   ((uint32_t)(param) & 0x00ffffff)
#define BRW_PARAM_IMAGE(idx, offset) BRW_PARAM(IMAGE, ((idx) << 8) | (offset))
#define BRW_PARAM_IMAGE_IDX(value)   (BRW_PARAM_VALUE(value) >> 8)
#define BRW_PARAM_IMAGE_OFFSET(value)(BRW_PARAM_VALUE(value) & 0xf)

/**
 * Dirty flags.  When state changes, we flag some combination of these
 * to indicate that particular GPU commands need to be re-emitted.
 *
 * Each bit typically corresponds to a single 3DSTATE_* command packet, but
 * in rare cases they map to a group of related packets that need to be
 * emitted together.
 *
 * See iris_upload_render_state().
 */
#define IRIS_DIRTY_COLOR_CALC_STATE               (1ull <<  0)
#define IRIS_DIRTY_POLYGON_STIPPLE                (1ull <<  1)
#define IRIS_DIRTY_SCISSOR_RECT                   (1ull <<  2)
#define IRIS_DIRTY_WM_DEPTH_STENCIL               (1ull <<  3)
#define IRIS_DIRTY_CC_VIEWPORT                    (1ull <<  4)
#define IRIS_DIRTY_SF_CL_VIEWPORT                 (1ull <<  5)
#define IRIS_DIRTY_PS_BLEND                       (1ull <<  6)
#define IRIS_DIRTY_BLEND_STATE                    (1ull <<  7)
#define IRIS_DIRTY_RASTER                         (1ull <<  8)
#define IRIS_DIRTY_CLIP                           (1ull <<  9)
#define IRIS_DIRTY_SBE                            (1ull << 10)
#define IRIS_DIRTY_LINE_STIPPLE                   (1ull << 11)
#define IRIS_DIRTY_VERTEX_ELEMENTS                (1ull << 12)
#define IRIS_DIRTY_MULTISAMPLE                    (1ull << 13)
#define IRIS_DIRTY_VERTEX_BUFFERS                 (1ull << 14)
#define IRIS_DIRTY_SAMPLE_MASK                    (1ull << 15)
#define IRIS_DIRTY_URB                            (1ull << 16)
#define IRIS_DIRTY_DEPTH_BUFFER                   (1ull << 17)
#define IRIS_DIRTY_WM                             (1ull << 18)
#define IRIS_DIRTY_SO_BUFFERS                     (1ull << 19)
#define IRIS_DIRTY_SO_DECL_LIST                   (1ull << 20)
#define IRIS_DIRTY_STREAMOUT                      (1ull << 21)
#define IRIS_DIRTY_VF_SGVS                        (1ull << 22)
#define IRIS_DIRTY_VF                             (1ull << 23)
#define IRIS_DIRTY_VF_TOPOLOGY                    (1ull << 24)
#define IRIS_DIRTY_RENDER_RESOLVES_AND_FLUSHES    (1ull << 25)
#define IRIS_DIRTY_COMPUTE_RESOLVES_AND_FLUSHES   (1ull << 26)
#define IRIS_DIRTY_VF_STATISTICS                  (1ull << 27)
#define IRIS_DIRTY_PMA_FIX                        (1ull << 28)
#define IRIS_DIRTY_DEPTH_BOUNDS                   (1ull << 29)
#define IRIS_DIRTY_RENDER_BUFFER                  (1ull << 30)
#define IRIS_DIRTY_STENCIL_REF                    (1ull << 31)
#define IRIS_DIRTY_VERTEX_BUFFER_FLUSHES          (1ull << 32)
#define IRIS_DIRTY_RENDER_MISC_BUFFER_FLUSHES     (1ull << 33)
#define IRIS_DIRTY_COMPUTE_MISC_BUFFER_FLUSHES    (1ull << 34)
#define IRIS_DIRTY_VFG                            (1ull << 35)

#define IRIS_ALL_DIRTY_FOR_COMPUTE (IRIS_DIRTY_COMPUTE_RESOLVES_AND_FLUSHES | \
                                    IRIS_DIRTY_COMPUTE_MISC_BUFFER_FLUSHES)

#define IRIS_ALL_DIRTY_FOR_RENDER (~IRIS_ALL_DIRTY_FOR_COMPUTE)

/**
 * Per-stage dirty flags.  When state changes, we flag some combination of
 * these to indicate that particular GPU commands need to be re-emitted.
 * Unlike the IRIS_DIRTY_* flags these are shader stage-specific and can be
 * indexed by shifting the mask by the shader stage index.
 *
 * See iris_upload_render_state().
 */
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_VS        (1ull << 0)
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_TCS       (1ull << 1)
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_TES       (1ull << 2)
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_GS        (1ull << 3)
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_PS        (1ull << 4)
#define IRIS_STAGE_DIRTY_SAMPLER_STATES_CS        (1ull << 5)
#define IRIS_STAGE_DIRTY_UNCOMPILED_VS            (1ull << 6)
#define IRIS_STAGE_DIRTY_UNCOMPILED_TCS           (1ull << 7)
#define IRIS_STAGE_DIRTY_UNCOMPILED_TES           (1ull << 8)
#define IRIS_STAGE_DIRTY_UNCOMPILED_GS            (1ull << 9)
#define IRIS_STAGE_DIRTY_UNCOMPILED_FS            (1ull << 10)
#define IRIS_STAGE_DIRTY_UNCOMPILED_CS            (1ull << 11)
#define IRIS_STAGE_DIRTY_VS                       (1ull << 12)
#define IRIS_STAGE_DIRTY_TCS                      (1ull << 13)
#define IRIS_STAGE_DIRTY_TES                      (1ull << 14)
#define IRIS_STAGE_DIRTY_GS                       (1ull << 15)
#define IRIS_STAGE_DIRTY_FS                       (1ull << 16)
#define IRIS_STAGE_DIRTY_CS                       (1ull << 17)
#define IRIS_SHIFT_FOR_STAGE_DIRTY_CONSTANTS      18
#define IRIS_STAGE_DIRTY_CONSTANTS_VS             (1ull << 18)
#define IRIS_STAGE_DIRTY_CONSTANTS_TCS            (1ull << 19)
#define IRIS_STAGE_DIRTY_CONSTANTS_TES            (1ull << 20)
#define IRIS_STAGE_DIRTY_CONSTANTS_GS             (1ull << 21)
#define IRIS_STAGE_DIRTY_CONSTANTS_FS             (1ull << 22)
#define IRIS_STAGE_DIRTY_CONSTANTS_CS             (1ull << 23)
#define IRIS_SHIFT_FOR_STAGE_DIRTY_BINDINGS       24
#define IRIS_STAGE_DIRTY_BINDINGS_VS              (1ull << 24)
#define IRIS_STAGE_DIRTY_BINDINGS_TCS             (1ull << 25)
#define IRIS_STAGE_DIRTY_BINDINGS_TES             (1ull << 26)
#define IRIS_STAGE_DIRTY_BINDINGS_GS              (1ull << 27)
#define IRIS_STAGE_DIRTY_BINDINGS_FS              (1ull << 28)
#define IRIS_STAGE_DIRTY_BINDINGS_CS              (1ull << 29)

#define IRIS_ALL_STAGE_DIRTY_FOR_COMPUTE (IRIS_STAGE_DIRTY_CS | \
                                          IRIS_STAGE_DIRTY_SAMPLER_STATES_CS | \
                                          IRIS_STAGE_DIRTY_UNCOMPILED_CS |    \
                                          IRIS_STAGE_DIRTY_CONSTANTS_CS |     \
                                          IRIS_STAGE_DIRTY_BINDINGS_CS)

#define IRIS_ALL_STAGE_DIRTY_FOR_RENDER (~IRIS_ALL_STAGE_DIRTY_FOR_COMPUTE)

#define IRIS_ALL_STAGE_DIRTY_BINDINGS_FOR_RENDER (IRIS_STAGE_DIRTY_BINDINGS_VS  | \
                                                  IRIS_STAGE_DIRTY_BINDINGS_TCS | \
                                                  IRIS_STAGE_DIRTY_BINDINGS_TES | \
                                                  IRIS_STAGE_DIRTY_BINDINGS_GS  | \
                                                  IRIS_STAGE_DIRTY_BINDINGS_FS)

#define IRIS_ALL_STAGE_DIRTY_BINDINGS (IRIS_ALL_STAGE_DIRTY_BINDINGS_FOR_RENDER | \
                                       IRIS_STAGE_DIRTY_BINDINGS_CS)

/**
 * Non-orthogonal state (NOS) dependency flags.
 *
 * Shader programs may depend on non-orthogonal state.  These flags are
 * used to indicate that a shader's key depends on the state provided by
 * a certain Gallium CSO.  Changing any CSOs marked as a dependency will
 * cause the driver to re-compute the shader key, possibly triggering a
 * shader recompile.
 */
enum iris_nos_dep {
   IRIS_NOS_FRAMEBUFFER,
   IRIS_NOS_DEPTH_STENCIL_ALPHA,
   IRIS_NOS_RASTERIZER,
   IRIS_NOS_BLEND,
   IRIS_NOS_LAST_VUE_MAP,

   IRIS_NOS_COUNT,
};

/** @{
 *
 * Program cache keys for state based recompiles.
 */

struct iris_base_prog_key {
   unsigned program_string_id;
};

/**
 * Note, we need to take care to have padding explicitly declared
 * for key since we will directly memcmp the whole struct.
 */
struct iris_vue_prog_key {
   struct iris_base_prog_key base;

   unsigned nr_userclip_plane_consts:4;
   unsigned padding:28;
};

struct iris_vs_prog_key {
   struct iris_vue_prog_key vue;
};

struct iris_tcs_prog_key {
   struct iris_vue_prog_key vue;

   enum tess_primitive_mode _tes_primitive_mode;

   uint8_t input_vertices;

   bool quads_workaround;

   /** A bitfield of per-patch outputs written. */
   uint32_t patch_outputs_written;

   /** A bitfield of per-vertex outputs written. */
   uint64_t outputs_written;
};

struct iris_tes_prog_key {
   struct iris_vue_prog_key vue;

   /** A bitfield of per-patch inputs read. */
   uint32_t patch_inputs_read;

   /** A bitfield of per-vertex inputs read. */
   uint64_t inputs_read;
};

struct iris_gs_prog_key {
   struct iris_vue_prog_key vue;
};

struct iris_fs_prog_key {
   struct iris_base_prog_key base;

   unsigned nr_color_regions:5;
   bool flat_shade:1;
   bool alpha_test_replicate_alpha:1;
   bool alpha_to_coverage:1;
   bool clamp_fragment_color:1;
   bool persample_interp:1;
   bool multisample_fbo:1;
   bool force_dual_color_blend:1;
   bool coherent_fb_fetch:1;

   uint8_t color_outputs_valid;
   uint64_t input_slots_valid;
};

struct iris_cs_prog_key {
   struct iris_base_prog_key base;
};

union iris_any_prog_key {
   struct iris_base_prog_key base;
   struct iris_vue_prog_key vue;
   struct iris_vs_prog_key vs;
   struct iris_tcs_prog_key tcs;
   struct iris_tes_prog_key tes;
   struct iris_gs_prog_key gs;
   struct iris_fs_prog_key fs;
   struct iris_cs_prog_key cs;
};

/** @} */

struct iris_depth_stencil_alpha_state;

/**
 * Cache IDs for the in-memory program cache (ice->shaders.cache).
 */
enum iris_program_cache_id {
   IRIS_CACHE_VS  = MESA_SHADER_VERTEX,
   IRIS_CACHE_TCS = MESA_SHADER_TESS_CTRL,
   IRIS_CACHE_TES = MESA_SHADER_TESS_EVAL,
   IRIS_CACHE_GS  = MESA_SHADER_GEOMETRY,
   IRIS_CACHE_FS  = MESA_SHADER_FRAGMENT,
   IRIS_CACHE_CS  = MESA_SHADER_COMPUTE,
   IRIS_CACHE_BLORP,
};

/** @{
 *
 * Defines for PIPE_CONTROL operations, which trigger cache flushes,
 * synchronization, pipelined memory writes, and so on.
 *
 * The bits here are not the actual hardware values.  The actual fields
 * move between various generations, so we just have flags for each
 * potential operation, and use genxml to encode the actual packet.
 */
enum pipe_control_flags
{
   PIPE_CONTROL_FLUSH_LLC                       = (1 << 1),
   PIPE_CONTROL_LRI_POST_SYNC_OP                = (1 << 2),
   PIPE_CONTROL_STORE_DATA_INDEX                = (1 << 3),
   PIPE_CONTROL_CS_STALL                        = (1 << 4),
   PIPE_CONTROL_GLOBAL_SNAPSHOT_COUNT_RESET     = (1 << 5),
   PIPE_CONTROL_SYNC_GFDT                       = (1 << 6),
   PIPE_CONTROL_TLB_INVALIDATE                  = (1 << 7),
   PIPE_CONTROL_MEDIA_STATE_CLEAR               = (1 << 8),
   PIPE_CONTROL_WRITE_IMMEDIATE                 = (1 << 9),
   PIPE_CONTROL_WRITE_DEPTH_COUNT               = (1 << 10),
   PIPE_CONTROL_WRITE_TIMESTAMP                 = (1 << 11),
   PIPE_CONTROL_DEPTH_STALL                     = (1 << 12),
   PIPE_CONTROL_RENDER_TARGET_FLUSH             = (1 << 13),
   PIPE_CONTROL_INSTRUCTION_INVALIDATE          = (1 << 14),
   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE        = (1 << 15),
   PIPE_CONTROL_INDIRECT_STATE_POINTERS_DISABLE = (1 << 16),
   PIPE_CONTROL_NOTIFY_ENABLE                   = (1 << 17),
   PIPE_CONTROL_FLUSH_ENABLE                    = (1 << 18),
   PIPE_CONTROL_DATA_CACHE_FLUSH                = (1 << 19),
   PIPE_CONTROL_VF_CACHE_INVALIDATE             = (1 << 20),
   PIPE_CONTROL_CONST_CACHE_INVALIDATE          = (1 << 21),
   PIPE_CONTROL_STATE_CACHE_INVALIDATE          = (1 << 22),
   PIPE_CONTROL_STALL_AT_SCOREBOARD             = (1 << 23),
   PIPE_CONTROL_DEPTH_CACHE_FLUSH               = (1 << 24),
   PIPE_CONTROL_TILE_CACHE_FLUSH                = (1 << 25),
   PIPE_CONTROL_FLUSH_HDC                       = (1 << 26),
   PIPE_CONTROL_PSS_STALL_SYNC                  = (1 << 27),
};

#define PIPE_CONTROL_CACHE_FLUSH_BITS \
   (PIPE_CONTROL_DEPTH_CACHE_FLUSH |  \
    PIPE_CONTROL_DATA_CACHE_FLUSH |   \
    PIPE_CONTROL_TILE_CACHE_FLUSH |   \
    PIPE_CONTROL_RENDER_TARGET_FLUSH)

#define PIPE_CONTROL_CACHE_INVALIDATE_BITS  \
   (PIPE_CONTROL_STATE_CACHE_INVALIDATE |   \
    PIPE_CONTROL_CONST_CACHE_INVALIDATE |   \
    PIPE_CONTROL_VF_CACHE_INVALIDATE |      \
    PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | \
    PIPE_CONTROL_INSTRUCTION_INVALIDATE)

enum iris_predicate_state {
   /* The first two states are used if we can determine whether to draw
    * without having to look at the values in the query object buffer. This
    * will happen if there is no conditional render in progress, if the query
    * object is already completed or if something else has already added
    * samples to the preliminary result.
    */
   IRIS_PREDICATE_STATE_RENDER,
   IRIS_PREDICATE_STATE_DONT_RENDER,

   /* In this case whether to draw or not depends on the result of an
    * MI_PREDICATE command so the predicate enable bit needs to be checked.
    */
   IRIS_PREDICATE_STATE_USE_BIT,
};

/** @} */

/**
 * An uncompiled, API-facing shader.  This is the Gallium CSO for shaders.
 * It primarily contains the NIR for the shader.
 *
 * Each API-facing shader can be compiled into multiple shader variants,
 * based on non-orthogonal state dependencies, recorded in the shader key.
 *
 * See iris_compiled_shader, which represents a compiled shader variant.
 */
struct iris_uncompiled_shader {
   struct pipe_reference ref;

   /**
    * NIR for the shader.
    *
    * Even for shaders that originate as TGSI, this pointer will be non-NULL.
    */
   struct nir_shader *nir;

   struct pipe_stream_output_info stream_output;

   /* A SHA1 of the serialized NIR for the disk cache. */
   unsigned char nir_sha1[20];

   unsigned program_id;

   /** Bitfield of (1 << IRIS_NOS_*) flags. */
   unsigned nos;

   /** Have any shader variants been compiled yet? */
   bool compiled_once;

   /* Whether shader uses atomic operations. */
   bool uses_atomic_load_store;

   /** Size (in bytes) of the kernel input data */
   unsigned kernel_input_size;

   /** Size (in bytes) of the local (shared) data passed as kernel inputs */
   unsigned kernel_shared_size;

   /** List of iris_compiled_shader variants */
   struct list_head variants;

   /** Lock for the variants list */
   simple_mtx_t lock;

   /** For parallel shader compiles */
   struct util_queue_fence ready;
};

enum iris_surface_group {
   IRIS_SURFACE_GROUP_RENDER_TARGET,
   IRIS_SURFACE_GROUP_RENDER_TARGET_READ,
   IRIS_SURFACE_GROUP_CS_WORK_GROUPS,
   IRIS_SURFACE_GROUP_TEXTURE,
   IRIS_SURFACE_GROUP_IMAGE,
   IRIS_SURFACE_GROUP_UBO,
   IRIS_SURFACE_GROUP_SSBO,

   IRIS_SURFACE_GROUP_COUNT,
};

enum {
   /* Invalid value for a binding table index. */
   IRIS_SURFACE_NOT_USED = 0xa0a0a0a0,
};

struct iris_binding_table {
   uint32_t size_bytes;

   /** Number of surfaces in each group, before compacting. */
   uint32_t sizes[IRIS_SURFACE_GROUP_COUNT];

   /** Initial offset of each group. */
   uint32_t offsets[IRIS_SURFACE_GROUP_COUNT];

   /** Mask of surfaces used in each group. */
   uint64_t used_mask[IRIS_SURFACE_GROUP_COUNT];
};

/**
 * A compiled shader variant, containing a pointer to the GPU assembly,
 * as well as program data and other packets needed by state upload.
 *
 * There can be several iris_compiled_shader variants per API-level shader
 * (iris_uncompiled_shader), due to state-based recompiles (brw_*_prog_key).
 */
struct iris_compiled_shader {
   struct pipe_reference ref;

   /** Link in the iris_uncompiled_shader::variants list */
   struct list_head link;

   /** Key for this variant (but not for BLORP programs) */
   union iris_any_prog_key key;

   /**
    * Is the variant fully compiled and ready?
    *
    * Variants are added to \c iris_uncompiled_shader::variants before
    * compilation actually occurs.  This signals that compilation has
    * completed.
    */
   struct util_queue_fence ready;

   /** Variant is ready, but compilation failed. */
   bool compilation_failed;

   /** Reference to the uploaded assembly. */
   struct iris_state_ref assembly;

   /** Pointer to the assembly in the BO's map. */
   void *map;

   /** The program data (owned by the program cache hash table) */
   struct brw_stage_prog_data *prog_data;

   /** A list of system values to be uploaded as uniforms. */
   enum brw_param_builtin *system_values;
   unsigned num_system_values;

   /** Size (in bytes) of the kernel input data */
   unsigned kernel_input_size;

   /** Number of constbufs expected by the shader. */
   unsigned num_cbufs;

   /**
    * Derived 3DSTATE_STREAMOUT and 3DSTATE_SO_DECL_LIST packets
    * (the VUE-based information for transform feedback outputs).
    */
   uint32_t *streamout;

   struct iris_binding_table bt;

   /**
    * Shader packets and other data derived from prog_data.  These must be
    * completely determined from prog_data.
    */
   uint8_t derived_data[0];
};

/**
 * API context state that is replicated per shader stage.
 */
struct iris_shader_state {
   /** Uniform Buffers */
   struct pipe_shader_buffer constbuf[PIPE_MAX_CONSTANT_BUFFERS];
   struct iris_state_ref constbuf_surf_state[PIPE_MAX_CONSTANT_BUFFERS];

   bool sysvals_need_upload;

   /** Shader Storage Buffers */
   struct pipe_shader_buffer ssbo[PIPE_MAX_SHADER_BUFFERS];
   struct iris_state_ref ssbo_surf_state[PIPE_MAX_SHADER_BUFFERS];

   /** Shader Storage Images (image load store) */
   struct iris_image_view image[PIPE_MAX_SHADER_IMAGES];

   struct iris_state_ref sampler_table;
   struct iris_sampler_state *samplers[IRIS_MAX_TEXTURE_SAMPLERS];
   struct iris_sampler_view *textures[IRIS_MAX_TEXTURE_SAMPLERS];

   /** Bitfield of which constant buffers are bound (non-null). */
   uint32_t bound_cbufs;
   uint32_t dirty_cbufs;

   /** Bitfield of which image views are bound (non-null). */
   uint32_t bound_image_views;

   /** Bitfield of which sampler views are bound (non-null). */
   uint32_t bound_sampler_views;

   /** Bitfield of which shader storage buffers are bound (non-null). */
   uint32_t bound_ssbos;

   /** Bitfield of which shader storage buffers are writable. */
   uint32_t writable_ssbos;
};

/**
 * Gallium CSO for stream output (transform feedback) targets.
 */
struct iris_stream_output_target {
   struct pipe_stream_output_target base;

   /** Storage holding the offset where we're writing in the buffer */
   struct iris_state_ref offset;

   /** Stride (bytes-per-vertex) during this transform feedback operation */
   uint16_t stride;

   /** Does the next 3DSTATE_SO_BUFFER need to zero the offsets? */
   bool zero_offset;
};

/**
 * A pool containing SAMPLER_BORDER_COLOR_STATE entries.
 *
 * See iris_border_color.c for more information.
 */
struct iris_border_color_pool {
   struct iris_bo *bo;
   void *map;
   unsigned insert_point;

   /** Map from border colors to offsets in the buffer. */
   struct hash_table *ht;
};

/**
 * The API context (derived from pipe_context).
 *
 * Most driver state is tracked here.
 */
struct iris_context {
   struct pipe_context ctx;
   struct threaded_context *thrctx;

   /** A debug callback for KHR_debug output. */
   struct pipe_debug_callback dbg;

   /** A device reset status callback for notifying that the GPU is hosed. */
   struct pipe_device_reset_callback reset;

   /** A set of dmabuf resources dirtied beyond their default aux-states. */
   struct set *dirty_dmabufs;

   /** Slab allocator for iris_transfer_map objects. */
   struct slab_child_pool transfer_pool;

   /** Slab allocator for threaded_context's iris_transfer_map objects */
   struct slab_child_pool transfer_pool_unsync;

   struct blorp_context blorp;

   struct iris_batch batches[IRIS_BATCH_COUNT];

   struct u_upload_mgr *query_buffer_uploader;

   struct intel_ds_device ds;

   struct {
      struct {
         /**
          * Either the value of BaseVertex for indexed draw calls or the value
          * of the argument <first> for non-indexed draw calls.
          */
         int firstvertex;
         int baseinstance;
      } params;

      /**
       * Are the above values the ones stored in the draw_params buffer?
       * If so, we can compare them against new values to see if anything
       * changed.  If not, we need to assume they changed.
       */
      bool params_valid;

      /**
       * Resource and offset that stores draw_parameters from the indirect
       * buffer or to the buffer that stures the previous values for non
       * indirect draws.
       */
      struct iris_state_ref draw_params;

      struct {
         /**
          * The value of DrawID. This always comes in from it's own vertex
          * buffer since it's not part of the indirect draw parameters.
          */
         int drawid;

         /**
          * Stores if an indexed or non-indexed draw (~0/0). Useful to
          * calculate BaseVertex as an AND of firstvertex and is_indexed_draw.
          */
         int is_indexed_draw;
      } derived_params;

      /**
       * Resource and offset used for GL_ARB_shader_draw_parameters which
       * contains parameters that are not present in the indirect buffer as
       * drawid and is_indexed_draw. They will go in their own vertex element.
       */
      struct iris_state_ref derived_draw_params;
   } draw;

   struct {
      struct iris_uncompiled_shader *uncompiled[MESA_SHADER_STAGES];
      struct iris_compiled_shader *prog[MESA_SHADER_STAGES];
      struct iris_compiled_shader *last_vue_shader;
      struct {
         unsigned size[4];
         unsigned entries[4];
         unsigned start[4];
         bool constrained;
      } urb;

      /** Uploader for shader assembly from the driver thread */
      struct u_upload_mgr *uploader_driver;
      /** Uploader for shader assembly from the threaded context */
      struct u_upload_mgr *uploader_unsync;
      struct hash_table *cache;

      /** Is a GS or TES outputting points or lines? */
      bool output_topology_is_points_or_lines;

      /**
       * Scratch buffers for various sizes and stages.
       *
       * Indexed by the "Per-Thread Scratch Space" field's 4-bit encoding,
       * and shader stage.
       */
      struct iris_bo *scratch_bos[1 << 4][MESA_SHADER_STAGES];

      /**
       * Scratch buffer surface states on Gfx12.5+
       */
      struct iris_state_ref scratch_surfs[1 << 4];
   } shaders;

   struct intel_perf_context *perf_ctx;

   /** Frame number for debug prints */
   uint32_t frame;

   struct {
      uint64_t dirty;
      uint64_t stage_dirty;
      uint64_t stage_dirty_for_nos[IRIS_NOS_COUNT];

      unsigned num_viewports;
      unsigned sample_mask;
      struct iris_blend_state *cso_blend;
      struct iris_rasterizer_state *cso_rast;
      struct iris_depth_stencil_alpha_state *cso_zsa;
      struct iris_vertex_element_state *cso_vertex_elements;
      struct pipe_blend_color blend_color;
      struct pipe_poly_stipple poly_stipple;
      struct pipe_viewport_state viewports[IRIS_MAX_VIEWPORTS];
      struct pipe_scissor_state scissors[IRIS_MAX_VIEWPORTS];
      struct pipe_stencil_ref stencil_ref;
      struct pipe_framebuffer_state framebuffer;
      struct pipe_clip_state clip_planes;

      float default_outer_level[4];
      float default_inner_level[2];

      /** Bitfield of which vertex buffers are bound (non-null). */
      uint64_t bound_vertex_buffers;

      uint8_t patch_vertices;
      bool primitive_restart;
      unsigned cut_index;
      enum pipe_prim_type prim_mode:8;
      bool prim_is_points_or_lines;
      uint8_t vertices_per_patch;

      bool window_space_position;

      /** The last compute group size */
      uint32_t last_block[3];

      /** The last compute grid size */
      uint32_t last_grid[3];
      /** Reference to the BO containing the compute grid size */
      struct iris_state_ref grid_size;
      /** Reference to the SURFACE_STATE for the compute grid resource */
      struct iris_state_ref grid_surf_state;

      /**
       * Array of aux usages for drawing, altered to account for any
       * self-dependencies from resources bound for sampling and rendering.
       */
      enum isl_aux_usage draw_aux_usage[BRW_MAX_DRAW_BUFFERS];

      /** Aux usage of the fb's depth buffer (which may or may not exist). */
      enum isl_aux_usage hiz_usage;

      enum intel_urb_deref_block_size urb_deref_block_size;

      /** Are depth writes enabled?  (Depth buffer may or may not exist.) */
      bool depth_writes_enabled;

      /** Are stencil writes enabled?  (Stencil buffer may or may not exist.) */
      bool stencil_writes_enabled;

      /** GenX-specific current state */
      struct iris_genx_state *genx;

      struct iris_shader_state shaders[MESA_SHADER_STAGES];

      /** Do vertex shader uses shader draw parameters ? */
      bool vs_uses_draw_params;
      bool vs_uses_derived_draw_params;
      bool vs_needs_sgvs_element;

      /** Do vertex shader uses edge flag ? */
      bool vs_needs_edge_flag;

      /** Do any samplers need border color?  One bit per shader stage. */
      uint8_t need_border_colors;

      /** Global resource bindings */
      struct pipe_resource *global_bindings[IRIS_MAX_GLOBAL_BINDINGS];

      struct pipe_stream_output_target *so_target[PIPE_MAX_SO_BUFFERS];
      bool streamout_active;

      bool statistics_counters_enabled;

      /** Current conditional rendering mode */
      enum iris_predicate_state predicate;

      /**
       * Query BO with a MI_PREDICATE_RESULT snapshot calculated on the
       * render context that needs to be uploaded to the compute context.
       */
      struct iris_bo *compute_predicate;

      /** Is a PIPE_QUERY_PRIMITIVES_GENERATED query active? */
      bool prims_generated_query_active;

      /** 3DSTATE_STREAMOUT and 3DSTATE_SO_DECL_LIST packets */
      uint32_t *streamout;

      /** The SURFACE_STATE for a 1x1x1 null surface. */
      struct iris_state_ref unbound_tex;

      /** The SURFACE_STATE for a framebuffer-sized null surface. */
      struct iris_state_ref null_fb;

      struct u_upload_mgr *surface_uploader;
      struct u_upload_mgr *bindless_uploader;
      struct u_upload_mgr *dynamic_uploader;

      struct iris_binder binder;

      struct iris_border_color_pool border_color_pool;

      /** The high 16-bits of the last VBO/index buffer addresses */
      uint16_t last_vbo_high_bits[33];
      uint16_t last_index_bo_high_bits;

      /**
       * Resources containing streamed state which our render context
       * currently points to.  Used to re-add these to the validation
       * list when we start a new batch and haven't resubmitted commands.
       */
      struct {
         struct pipe_resource *cc_vp;
         struct pipe_resource *sf_cl_vp;
         struct pipe_resource *color_calc;
         struct pipe_resource *scissor;
         struct pipe_resource *blend;
         struct pipe_resource *index_buffer;
         struct pipe_resource *cs_thread_ids;
         struct pipe_resource *cs_desc;
      } last_res;

      /** Records the size of variable-length state for INTEL_DEBUG=bat */
      struct hash_table_u64 *sizes;

      /** Last rendering scale argument provided to genX(emit_hashing_mode). */
      unsigned current_hash_scale;

      /** Resource holding the pixel pipe hashing tables. */
      struct pipe_resource *pixel_hashing_tables;
   } state;
};

#define perf_debug(dbg, ...) do {                      \
   if (INTEL_DEBUG(DEBUG_PERF))                        \
      dbg_printf(__VA_ARGS__);                         \
   if (unlikely(dbg))                                  \
      pipe_debug_message(dbg, PERF_INFO, __VA_ARGS__); \
} while(0)

struct pipe_context *
iris_create_context(struct pipe_screen *screen, void *priv, unsigned flags);
void iris_destroy_context(struct pipe_context *ctx);

void iris_lost_context_state(struct iris_batch *batch);

void iris_mark_dirty_dmabuf(struct iris_context *ice,
                            struct pipe_resource *res);
void iris_flush_dirty_dmabufs(struct iris_context *ice);

void iris_init_blit_functions(struct pipe_context *ctx);
void iris_init_clear_functions(struct pipe_context *ctx);
void iris_init_program_functions(struct pipe_context *ctx);
void iris_init_screen_program_functions(struct pipe_screen *pscreen);
void iris_init_resource_functions(struct pipe_context *ctx);
void iris_init_perfquery_functions(struct pipe_context *ctx);
void iris_update_compiled_shaders(struct iris_context *ice);
void iris_update_compiled_compute_shader(struct iris_context *ice);
void iris_fill_cs_push_const_buffer(struct brw_cs_prog_data *cs_prog_data,
                                    unsigned threads,
                                    uint32_t *dst);


/* iris_blit.c */
void iris_blorp_surf_for_resource(struct isl_device *isl_dev,
                                  struct blorp_surf *surf,
                                  struct pipe_resource *p_res,
                                  enum isl_aux_usage aux_usage,
                                  unsigned level,
                                  bool is_render_target);
void iris_copy_region(struct blorp_context *blorp,
                      struct iris_batch *batch,
                      struct pipe_resource *dst,
                      unsigned dst_level,
                      unsigned dstx, unsigned dsty, unsigned dstz,
                      struct pipe_resource *src,
                      unsigned src_level,
                      const struct pipe_box *src_box);

static inline enum blorp_batch_flags
iris_blorp_flags_for_batch(struct iris_batch *batch)
{
   if (batch->name == IRIS_BATCH_COMPUTE)
      return BLORP_BATCH_USE_COMPUTE;

   if (batch->name == IRIS_BATCH_BLITTER)
      return BLORP_BATCH_USE_BLITTER;

   return 0;
}

/* iris_draw.c */

void iris_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *info,
                   unsigned drawid_offset,
                   const struct pipe_draw_indirect_info *indirect,
                   const struct pipe_draw_start_count_bias *draws,
                   unsigned num_draws);
void iris_launch_grid(struct pipe_context *, const struct pipe_grid_info *);

/* iris_pipe_control.c */

void iris_emit_pipe_control_flush(struct iris_batch *batch,
                                  const char *reason, uint32_t flags);
void iris_emit_pipe_control_write(struct iris_batch *batch,
                                  const char *reason, uint32_t flags,
                                  struct iris_bo *bo, uint32_t offset,
                                  uint64_t imm);
void iris_emit_end_of_pipe_sync(struct iris_batch *batch,
                                const char *reason, uint32_t flags);
void iris_emit_buffer_barrier_for(struct iris_batch *batch,
                                  struct iris_bo *bo,
                                  enum iris_domain access);
void iris_flush_all_caches(struct iris_batch *batch);

#define iris_handle_always_flush_cache(batch) \
   if (unlikely(batch->screen->driconf.always_flush_cache)) \
      iris_flush_all_caches(batch);

void iris_init_flush_functions(struct pipe_context *ctx);

/* iris_border_color.c */

void iris_init_border_color_pool(struct iris_context *ice);
void iris_destroy_border_color_pool(struct iris_context *ice);
void iris_border_color_pool_reserve(struct iris_context *ice, unsigned count);
uint32_t iris_upload_border_color(struct iris_context *ice,
                                  union pipe_color_union *color);

/* iris_program.c */
void iris_upload_ubo_ssbo_surf_state(struct iris_context *ice,
                                     struct pipe_shader_buffer *buf,
                                     struct iris_state_ref *surf_state,
                                     isl_surf_usage_flags_t usage);
const struct shader_info *iris_get_shader_info(const struct iris_context *ice,
                                               gl_shader_stage stage);
struct iris_bo *iris_get_scratch_space(struct iris_context *ice,
                                       unsigned per_thread_scratch,
                                       gl_shader_stage stage);
const struct iris_state_ref *iris_get_scratch_surf(struct iris_context *ice,
                                                   unsigned per_thread_scratch);
uint32_t iris_group_index_to_bti(const struct iris_binding_table *bt,
                                 enum iris_surface_group group,
                                 uint32_t index);
uint32_t iris_bti_to_group_index(const struct iris_binding_table *bt,
                                 enum iris_surface_group group,
                                 uint32_t bti);

/* iris_disk_cache.c */

void iris_disk_cache_store(struct disk_cache *cache,
                           const struct iris_uncompiled_shader *ish,
                           const struct iris_compiled_shader *shader,
                           const void *prog_key,
                           uint32_t prog_key_size);
bool
iris_disk_cache_retrieve(struct iris_screen *screen,
                         struct u_upload_mgr *uploader,
                         struct iris_uncompiled_shader *ish,
                         struct iris_compiled_shader *shader,
                         const void *prog_key,
                         uint32_t prog_key_size);

/* iris_program_cache.c */

void iris_init_program_cache(struct iris_context *ice);
void iris_destroy_program_cache(struct iris_context *ice);
struct iris_compiled_shader *iris_find_cached_shader(struct iris_context *ice,
                                                     enum iris_program_cache_id,
                                                     uint32_t key_size,
                                                     const void *key);

struct iris_compiled_shader *iris_create_shader_variant(const struct iris_screen *,
                                                        void *mem_ctx,
                                                        enum iris_program_cache_id cache_id,
                                                        uint32_t key_size,
                                                        const void *key);

void iris_finalize_program(struct iris_compiled_shader *shader,
                           struct brw_stage_prog_data *prog_data,
                           uint32_t *streamout,
                           enum brw_param_builtin *system_values,
                           unsigned num_system_values,
                           unsigned kernel_input_size,
                           unsigned num_cbufs,
                           const struct iris_binding_table *bt);

void iris_upload_shader(struct iris_screen *screen,
                        struct iris_uncompiled_shader *,
                        struct iris_compiled_shader *,
                        struct hash_table *driver_ht,
                        struct u_upload_mgr *uploader,
                        enum iris_program_cache_id,
                        uint32_t key_size,
                        const void *key,
                        const void *assembly);
void iris_delete_shader_variant(struct iris_compiled_shader *shader);

void iris_destroy_shader_state(struct pipe_context *ctx, void *state);

static inline void
iris_uncompiled_shader_reference(struct pipe_context *ctx,
                                 struct iris_uncompiled_shader **dst,
                                 struct iris_uncompiled_shader *src)
{
   if (*dst == src)
      return;

   struct iris_uncompiled_shader *old_dst = *dst;

   if (pipe_reference(old_dst != NULL ? &old_dst->ref : NULL,
                      src != NULL ? &src->ref : NULL)) {
      iris_destroy_shader_state(ctx, *dst);
   }

   *dst = src;
}

static inline void
iris_shader_variant_reference(struct iris_compiled_shader **dst,
                              struct iris_compiled_shader *src)
{
   struct iris_compiled_shader *old_dst = *dst;

   if (pipe_reference(old_dst ? &old_dst->ref: NULL, src ? &src->ref : NULL))
      iris_delete_shader_variant(old_dst);

   *dst = src;
}

bool iris_blorp_lookup_shader(struct blorp_batch *blorp_batch,
                              const void *key,
                              uint32_t key_size,
                              uint32_t *kernel_out,
                              void *prog_data_out);
bool iris_blorp_upload_shader(struct blorp_batch *blorp_batch, uint32_t stage,
                              const void *key, uint32_t key_size,
                              const void *kernel, uint32_t kernel_size,
                              const struct brw_stage_prog_data *prog_data,
                              uint32_t prog_data_size,
                              uint32_t *kernel_out,
                              void *prog_data_out);

/* iris_resolve.c */

void iris_predraw_resolve_inputs(struct iris_context *ice,
                                 struct iris_batch *batch,
                                 bool *draw_aux_buffer_disabled,
                                 gl_shader_stage stage,
                                 bool consider_framebuffer);
void iris_predraw_resolve_framebuffer(struct iris_context *ice,
                                      struct iris_batch *batch,
                                      bool *draw_aux_buffer_disabled);
void iris_predraw_flush_buffers(struct iris_context *ice,
                                struct iris_batch *batch,
                                gl_shader_stage stage);
void iris_postdraw_update_resolve_tracking(struct iris_context *ice,
                                           struct iris_batch *batch);
void iris_cache_flush_for_render(struct iris_batch *batch,
                                 struct iris_bo *bo,
                                 enum isl_aux_usage aux_usage);
int iris_get_driver_query_info(struct pipe_screen *pscreen, unsigned index,
                               struct pipe_driver_query_info *info);
int iris_get_driver_query_group_info(struct pipe_screen *pscreen,
                                     unsigned index,
                                     struct pipe_driver_query_group_info *info);

/* iris_state.c */
void gfx9_toggle_preemption(struct iris_context *ice,
                            struct iris_batch *batch,
                            const struct pipe_draw_info *draw);



#ifdef genX
#  include "iris_genx_protos.h"
#else
#  define genX(x) gfx4_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx5_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx6_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx7_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx75_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx8_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx9_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx11_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx12_##x
#  include "iris_genx_protos.h"
#  undef genX
#  define genX(x) gfx125_##x
#  include "iris_genx_protos.h"
#  undef genX
#endif

#endif
