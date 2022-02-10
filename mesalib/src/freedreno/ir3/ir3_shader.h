/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_SHADER_H_
#define IR3_SHADER_H_

#include <stdio.h>

#include "c11/threads.h"
#include "compiler/nir/nir.h"
#include "compiler/shader_enums.h"
#include "util/bitscan.h"
#include "util/disk_cache.h"

#include "ir3_compiler.h"

struct glsl_type;

/* driver param indices: */
enum ir3_driver_param {
   /* compute shader driver params: */
   IR3_DP_NUM_WORK_GROUPS_X = 0,
   IR3_DP_NUM_WORK_GROUPS_Y = 1,
   IR3_DP_NUM_WORK_GROUPS_Z = 2,
   IR3_DP_WORK_DIM          = 3,
   IR3_DP_BASE_GROUP_X = 4,
   IR3_DP_BASE_GROUP_Y = 5,
   IR3_DP_BASE_GROUP_Z = 6,
   IR3_DP_CS_SUBGROUP_SIZE = 7,
   IR3_DP_LOCAL_GROUP_SIZE_X = 8,
   IR3_DP_LOCAL_GROUP_SIZE_Y = 9,
   IR3_DP_LOCAL_GROUP_SIZE_Z = 10,
   IR3_DP_SUBGROUP_ID_SHIFT = 11,
   /* NOTE: gl_NumWorkGroups should be vec4 aligned because
    * glDispatchComputeIndirect() needs to load these from
    * the info->indirect buffer.  Keep that in mind when/if
    * adding any addition CS driver params.
    */
   IR3_DP_CS_COUNT = 12, /* must be aligned to vec4 */

   /* vertex shader driver params: */
   IR3_DP_DRAWID = 0,
   IR3_DP_VTXID_BASE = 1,
   IR3_DP_INSTID_BASE = 2,
   IR3_DP_VTXCNT_MAX = 3,
   /* user-clip-plane components, up to 8x vec4's: */
   IR3_DP_UCP0_X = 4,
   /* .... */
   IR3_DP_UCP7_W = 35,
   IR3_DP_VS_COUNT = 36, /* must be aligned to vec4 */

   /* fragment shader driver params: */
   IR3_DP_FS_SUBGROUP_SIZE = 0,
};

#define IR3_MAX_SHADER_BUFFERS  32
#define IR3_MAX_SHADER_IMAGES   32
#define IR3_MAX_SO_BUFFERS      4
#define IR3_MAX_SO_STREAMS      4
#define IR3_MAX_SO_OUTPUTS      64
#define IR3_MAX_UBO_PUSH_RANGES 32

/* mirrors SYSTEM_VALUE_BARYCENTRIC_ but starting from 0 */
enum ir3_bary {
   IJ_PERSP_PIXEL,
   IJ_PERSP_SAMPLE,
   IJ_PERSP_CENTROID,
   IJ_PERSP_SIZE,
   IJ_LINEAR_PIXEL,
   IJ_LINEAR_CENTROID,
   IJ_LINEAR_SAMPLE,
   IJ_COUNT,
};

/* Description of what wavesizes are allowed. */
enum ir3_wavesize_option {
   IR3_SINGLE_ONLY,
   IR3_SINGLE_OR_DOUBLE,
   IR3_DOUBLE_ONLY,
};

/**
 * Description of a lowered UBO.
 */
struct ir3_ubo_info {
   uint32_t block;         /* Which constant block */
   uint16_t bindless_base; /* For bindless, which base register is used */
   bool bindless;
};

/**
 * Description of a range of a lowered UBO access.
 *
 * Drivers should not assume that there are not multiple disjoint
 * lowered ranges of a single UBO.
 */
struct ir3_ubo_range {
   struct ir3_ubo_info ubo;
   uint32_t offset;     /* start offset to push in the const register file */
   uint32_t start, end; /* range of block that's actually used */
};

struct ir3_ubo_analysis_state {
   struct ir3_ubo_range range[IR3_MAX_UBO_PUSH_RANGES];
   uint32_t num_enabled;
   uint32_t size;
   uint32_t
      cmdstream_size; /* for per-gen backend to stash required cmdstream size */
};

/**
 * Describes the layout of shader consts.  This includes:
 *   + User consts + driver lowered UBO ranges
 *   + SSBO sizes
 *   + Image sizes/dimensions
 *   + Driver params (ie. IR3_DP_*)
 *   + TFBO addresses (for generations that do not have hardware streamout)
 *   + Lowered immediates
 *
 * For consts needed to pass internal values to shader which may or may not
 * be required, rather than allocating worst-case const space, we scan the
 * shader and allocate consts as-needed:
 *
 *   + SSBO sizes: only needed if shader has a get_ssbo_size intrinsic
 *     for a given SSBO
 *
 *   + Image dimensions: needed to calculate pixel offset, but only for
 *     images that have a image_store intrinsic
 *
 * Layout of constant registers, each section aligned to vec4.  Note
 * that pointer size (ubo, etc) changes depending on generation.
 *
 *    user consts
 *    UBO addresses
 *    SSBO sizes
 *    image dimensions
 *    if (vertex shader) {
 *        driver params (IR3_DP_VS_COUNT)
 *        if (stream_output.num_outputs > 0)
 *           stream-out addresses
 *    } else if (compute_shader) {
 *        kernel params
 *        driver params (IR3_DP_CS_COUNT)
 *    }
 *    immediates
 *
 * Immediates go last mostly because they are inserted in the CP pass
 * after the nir -> ir3 frontend.
 *
 * Note UBO size in bytes should be aligned to vec4
 */
struct ir3_const_state {
   unsigned num_ubos;
   unsigned num_driver_params; /* scalar */

   /* UBO that should be mapped to the NIR shader's constant_data (or -1). */
   int32_t constant_data_ubo;

   struct {
      /* user const start at zero */
      unsigned ubo;
      unsigned image_dims;
      unsigned kernel_params;
      unsigned driver_param;
      unsigned tfbo;
      unsigned primitive_param;
      unsigned primitive_map;
      unsigned immediate;
   } offsets;

   struct {
      uint32_t mask;  /* bitmask of images that have image_store */
      uint32_t count; /* number of consts allocated */
      /* three const allocated per image which has image_store:
       *  + cpp         (bytes per pixel)
       *  + pitch       (y pitch)
       *  + array_pitch (z pitch)
       */
      uint32_t off[IR3_MAX_SHADER_IMAGES];
   } image_dims;

   unsigned immediates_count;
   unsigned immediates_size;
   uint32_t *immediates;

   /* State of ubo access lowered to push consts: */
   struct ir3_ubo_analysis_state ubo_state;
};

/**
 * A single output for vertex transform feedback.
 */
struct ir3_stream_output {
   unsigned register_index  : 6;  /**< 0 to 63 (OUT index) */
   unsigned start_component : 2;  /** 0 to 3 */
   unsigned num_components  : 3;  /** 1 to 4 */
   unsigned output_buffer   : 3;  /**< 0 to PIPE_MAX_SO_BUFFERS */
   unsigned dst_offset      : 16; /**< offset into the buffer in dwords */
   unsigned stream          : 2;  /**< 0 to 3 */
};

/**
 * Stream output for vertex transform feedback.
 */
struct ir3_stream_output_info {
   unsigned num_outputs;
   /** stride for an entire vertex for each buffer in dwords */
   uint16_t stride[IR3_MAX_SO_BUFFERS];

   /* These correspond to the VPC_SO_STREAM_CNTL fields */
   uint8_t streams_written;
   uint8_t buffer_to_stream[IR3_MAX_SO_BUFFERS];

   /**
    * Array of stream outputs, in the order they are to be written in.
    * Selected components are tightly packed into the output buffer.
    */
   struct ir3_stream_output output[IR3_MAX_SO_OUTPUTS];
};

/**
 * Starting from a4xx, HW supports pre-dispatching texture sampling
 * instructions prior to scheduling a shader stage, when the
 * coordinate maps exactly to an output of the previous stage.
 */

/**
 * There is a limit in the number of pre-dispatches allowed for any
 * given stage.
 */
#define IR3_MAX_SAMPLER_PREFETCH 4

/**
 * This is the output stream value for 'cmd', as used by blob. It may
 * encode the return type (in 3 bits) but it hasn't been verified yet.
 */
#define IR3_SAMPLER_PREFETCH_CMD          0x4
#define IR3_SAMPLER_BINDLESS_PREFETCH_CMD 0x6

/**
 * Stream output for texture sampling pre-dispatches.
 */
struct ir3_sampler_prefetch {
   uint8_t src;
   uint8_t samp_id;
   uint8_t tex_id;
   uint16_t samp_bindless_id;
   uint16_t tex_bindless_id;
   uint8_t dst;
   uint8_t wrmask;
   uint8_t half_precision;
   uint8_t cmd;
};

/* Configuration key used to identify a shader variant.. different
 * shader variants can be used to implement features not supported
 * in hw (two sided color), binning-pass vertex shader, etc.
 *
 * When adding to this struct, please update ir3_shader_variant()'s debug
 * output.
 */
struct ir3_shader_key {
   union {
      struct {
         /*
          * Combined Vertex/Fragment shader parameters:
          */
         unsigned ucp_enables : 8;

         /* do we need to check {v,f}saturate_{s,t,r}? */
         unsigned has_per_samp : 1;

         /*
          * Fragment shader variant parameters:
          */
         unsigned sample_shading : 1;
         unsigned msaa           : 1;
         /* used when shader needs to handle flat varyings (a4xx)
          * for front/back color inputs to frag shader:
          */
         unsigned rasterflat : 1;

         /* Indicates that this is a tessellation pipeline which requires a
          * whole different kind of vertex shader.  In case of
          * tessellation, this field also tells us which kind of output
          * topology the TES uses, which the TCS needs to know.
          */
#define IR3_TESS_NONE      0
#define IR3_TESS_QUADS     1
#define IR3_TESS_TRIANGLES 2
#define IR3_TESS_ISOLINES  3
         unsigned tessellation : 2;

         unsigned has_gs : 1;

         /* Whether stages after TCS read gl_PrimitiveID, used to determine
          * whether the TCS has to store it in the tess factor BO.
          */
         unsigned tcs_store_primid : 1;

         /* Whether this variant sticks to the "safe" maximum constlen,
          * which guarantees that the combined stages will never go over
          * the limit:
          */
         unsigned safe_constlen : 1;

         /* Whether gl_Layer must be forced to 0 because it isn't written. */
         unsigned layer_zero : 1;

         /* Whether gl_ViewportIndex must be forced to 0 because it isn't
          * written. */
         unsigned view_zero : 1;
      };
      uint32_t global;
   };

   /* bitmask of ms shifts (a3xx) */
   uint32_t vsamples, fsamples;

   /* bitmask of samplers which need astc srgb workaround (a4xx): */
   uint16_t vastc_srgb, fastc_srgb;
};

static inline unsigned
ir3_tess_mode(enum tess_primitive_mode tess_mode)
{
   switch (tess_mode) {
   case TESS_PRIMITIVE_ISOLINES:
      return IR3_TESS_ISOLINES;
   case TESS_PRIMITIVE_TRIANGLES:
      return IR3_TESS_TRIANGLES;
   case TESS_PRIMITIVE_QUADS:
      return IR3_TESS_QUADS;
   default:
      unreachable("bad tessmode");
   }
}

static inline uint32_t
ir3_tess_factor_stride(unsigned patch_type)
{
   /* note: this matches the stride used by ir3's build_tessfactor_base */
   switch (patch_type) {
   case IR3_TESS_ISOLINES:
      return 12;
   case IR3_TESS_TRIANGLES:
      return 20;
   case IR3_TESS_QUADS:
      return 28;
   default:
      unreachable("bad tessmode");
   }
}

static inline bool
ir3_shader_key_equal(const struct ir3_shader_key *a,
                     const struct ir3_shader_key *b)
{
   /* slow-path if we need to check {v,f}saturate_{s,t,r} */
   if (a->has_per_samp || b->has_per_samp)
      return memcmp(a, b, sizeof(struct ir3_shader_key)) == 0;
   return a->global == b->global;
}

/* will the two keys produce different lowering for a fragment shader? */
static inline bool
ir3_shader_key_changes_fs(struct ir3_shader_key *key,
                          struct ir3_shader_key *last_key)
{
   if (last_key->has_per_samp || key->has_per_samp) {
      if ((last_key->fsamples != key->fsamples) ||
          (last_key->fastc_srgb != key->fastc_srgb))
         return true;
   }

   if (last_key->rasterflat != key->rasterflat)
      return true;

   if (last_key->layer_zero != key->layer_zero)
      return true;

   if (last_key->ucp_enables != key->ucp_enables)
      return true;

   if (last_key->safe_constlen != key->safe_constlen)
      return true;

   return false;
}

/* will the two keys produce different lowering for a vertex shader? */
static inline bool
ir3_shader_key_changes_vs(struct ir3_shader_key *key,
                          struct ir3_shader_key *last_key)
{
   if (last_key->has_per_samp || key->has_per_samp) {
      if ((last_key->vsamples != key->vsamples) ||
          (last_key->vastc_srgb != key->vastc_srgb))
         return true;
   }

   if (last_key->ucp_enables != key->ucp_enables)
      return true;

   if (last_key->safe_constlen != key->safe_constlen)
      return true;

   return false;
}

/**
 * On a4xx+a5xx, Images share state with textures and SSBOs:
 *
 *   + Uses texture (cat5) state/instruction (isam) to read
 *   + Uses SSBO state and instructions (cat6) to write and for atomics
 *
 * Starting with a6xx, Images and SSBOs are basically the same thing,
 * with texture state and isam also used for SSBO reads.
 *
 * On top of that, gallium makes the SSBO (shader_buffers) state semi
 * sparse, with the first half of the state space used for atomic
 * counters lowered to atomic buffers.  We could ignore this, but I
 * don't think we could *really* handle the case of a single shader
 * that used the max # of textures + images + SSBOs.  And once we are
 * offsetting images by num_ssbos (or visa versa) to map them into
 * the same hardware state, the hardware state has become coupled to
 * the shader state, so at this point we might as well just use a
 * mapping table to remap things from image/SSBO idx to hw idx.
 *
 * To make things less (more?) confusing, for the hw "SSBO" state
 * (since it is really both SSBO and Image) I'll use the name "IBO"
 */
struct ir3_ibo_mapping {
#define IBO_INVALID 0xff
   /* Maps logical SSBO state to hw tex state: */
   uint8_t ssbo_to_tex[IR3_MAX_SHADER_BUFFERS];

   /* Maps logical Image state to hw tex state: */
   uint8_t image_to_tex[IR3_MAX_SHADER_IMAGES];

   /* Maps hw state back to logical SSBO or Image state:
    *
    * note IBO_SSBO ORd into values to indicate that the
    * hw slot is used for SSBO state vs Image state.
    */
#define IBO_SSBO 0x80
   uint8_t tex_to_image[32];

   /* including real textures */
   uint8_t num_tex;
   /* the number of real textures, ie. image/ssbo start here */
   uint8_t tex_base;
};

struct ir3_disasm_info {
   bool write_disasm;
   char *nir;
   char *disasm;
};

/* Represents half register in regid */
#define HALF_REG_ID 0x100

/**
 * Shader variant which contains the actual hw shader instructions,
 * and necessary info for shader state setup.
 */
struct ir3_shader_variant {
   struct fd_bo *bo;

   /* variant id (for debug) */
   uint32_t id;

   struct ir3_shader_key key;

   /* vertex shaders can have an extra version for hwbinning pass,
    * which is pointed to by so->binning:
    */
   bool binning_pass;
   //	union {
   struct ir3_shader_variant *binning;
   struct ir3_shader_variant *nonbinning;
   //	};

   struct ir3 *ir; /* freed after assembling machine instructions */

   /* shader variants form a linked list: */
   struct ir3_shader_variant *next;

   /* replicated here to avoid passing extra ptrs everywhere: */
   gl_shader_stage type;
   struct ir3_shader *shader;

   /* variant's copy of nir->constant_data (since we don't track the NIR in
    * the variant, and shader->nir is before the opt pass).  Moves to v->bin
    * after assembly.
    */
   void *constant_data;

   /*
    * Below here is serialized when written to disk cache:
    */

   /* The actual binary shader instructions, size given by info.sizedwords: */
   uint32_t *bin;

   struct ir3_const_state *const_state;

   /*
    * The following macros are used by the shader disk cache save/
    * restore paths to serialize/deserialize the variant.  Any
    * pointers that require special handling in store_variant()
    * and retrieve_variant() should go above here.
    */
#define VARIANT_CACHE_START  offsetof(struct ir3_shader_variant, info)
#define VARIANT_CACHE_PTR(v) (((char *)v) + VARIANT_CACHE_START)
#define VARIANT_CACHE_SIZE                                                     \
   (sizeof(struct ir3_shader_variant) - VARIANT_CACHE_START)

   struct ir3_info info;

   uint32_t constant_data_size;

   /* Levels of nesting of flow control:
    */
   unsigned branchstack;

   unsigned loops;

   /* the instructions length is in units of instruction groups
    * (4 instructions for a3xx, 16 instructions for a4xx.. each
    * instruction is 2 dwords):
    */
   unsigned instrlen;

   /* the constants length is in units of vec4's, and is the sum of
    * the uniforms and the built-in compiler constants
    */
   unsigned constlen;

   /* The private memory size in bytes */
   unsigned pvtmem_size;
   /* Whether we should use the new per-wave layout rather than per-fiber. */
   bool pvtmem_per_wave;

   /* Size in bytes of required shared memory */
   unsigned shared_size;

   /* About Linkage:
    *   + Let the frag shader determine the position/compmask for the
    *     varyings, since it is the place where we know if the varying
    *     is actually used, and if so, which components are used.  So
    *     what the hw calls "outloc" is taken from the "inloc" of the
    *     frag shader.
    *   + From the vert shader, we only need the output regid
    */

   bool frag_face, color0_mrt;
   uint8_t fragcoord_compmask;

   /* NOTE: for input/outputs, slot is:
    *   gl_vert_attrib  - for VS inputs
    *   gl_varying_slot - for VS output / FS input
    *   gl_frag_result  - for FS output
    */

   /* varyings/outputs: */
   unsigned outputs_count;
   struct {
      uint8_t slot;
      uint8_t regid;
      uint8_t view;
      bool half : 1;
   } outputs[32 + 2]; /* +POSITION +PSIZE */
   bool writes_pos, writes_smask, writes_psize, writes_stencilref;

   /* Size in dwords of all outputs for VS, size of entire patch for HS. */
   uint32_t output_size;

   /* Expected size of incoming output_loc for HS, DS, and GS */
   uint32_t input_size;

   /* Map from location to offset in per-primitive storage. In dwords for
    * HS, where varyings are read in the next stage via ldg with a dword
    * offset, and in bytes for all other stages.
    */
   unsigned output_loc[32 + 4]; /* +POSITION +PSIZE +CLIP_DIST0 +CLIP_DIST1 */

   /* attributes (VS) / varyings (FS):
    * Note that sysval's should come *after* normal inputs.
    */
   unsigned inputs_count;
   struct {
      uint8_t slot;
      uint8_t regid;
      uint8_t compmask;
      /* location of input (ie. offset passed to bary.f, etc).  This
       * matches the SP_VS_VPC_DST_REG.OUTLOCn value (a3xx and a4xx
       * have the OUTLOCn value offset by 8, presumably to account
       * for gl_Position/gl_PointSize)
       */
      uint8_t inloc;
      /* vertex shader specific: */
      bool sysval : 1; /* slot is a gl_system_value */
      /* fragment shader specific: */
      bool bary       : 1; /* fetched varying (vs one loaded into reg) */
      bool rasterflat : 1; /* special handling for emit->rasterflat */
      bool half       : 1;
      bool flat       : 1;
   } inputs[32 + 2]; /* +POSITION +FACE */

   /* sum of input components (scalar).  For frag shaders, it only counts
    * the varying inputs:
    */
   unsigned total_in;

   /* sum of sysval input components (scalar). */
   unsigned sysval_in;

   /* For frag shaders, the total number of inputs (not scalar,
    * ie. SP_VS_PARAM_REG.TOTALVSOUTVAR)
    */
   unsigned varying_in;

   /* Remapping table to map Image and SSBO to hw state: */
   struct ir3_ibo_mapping image_mapping;

   /* number of samplers/textures (which are currently 1:1): */
   int num_samp;

   /* is there an implicit sampler to read framebuffer (FS only).. if
    * so the sampler-idx is 'num_samp - 1' (ie. it is appended after
    * the last "real" texture)
    */
   bool fb_read;

   /* do we have one or more SSBO instructions: */
   bool has_ssbo;

   /* Which bindless resources are used, for filling out sp_xs_config */
   bool bindless_tex;
   bool bindless_samp;
   bool bindless_ibo;
   bool bindless_ubo;

   /* do we need derivatives: */
   bool need_pixlod;

   bool need_fine_derivatives;

   /* do we need VS driver params? */
   bool need_driver_params;

   /* do we have image write, etc (which prevents early-z): */
   bool no_earlyz;

   /* do we have kill, which also prevents early-z, but not necessarily
    * early-lrz (as long as lrz-write is disabled, which must be handled
    * outside of ir3.  Unlike other no_earlyz cases, kill doesn't have
    * side effects that prevent early-lrz discard.
    */
   bool has_kill;

   bool per_samp;

   /* Are we using split or merged register file? */
   bool mergedregs;

   uint8_t clip_mask, cull_mask;

   /* for astc srgb workaround, the number/base of additional
    * alpha tex states we need, and index of original tex states
    */
   struct {
      unsigned base, count;
      unsigned orig_idx[16];
   } astc_srgb;

   /* texture sampler pre-dispatches */
   uint32_t num_sampler_prefetch;
   struct ir3_sampler_prefetch sampler_prefetch[IR3_MAX_SAMPLER_PREFETCH];

   uint16_t local_size[3];
   bool local_size_variable;

   /* Important for compute shader to determine max reg footprint */
   bool has_barrier;

   struct ir3_disasm_info disasm_info;
};

static inline const char *
ir3_shader_stage(struct ir3_shader_variant *v)
{
   switch (v->type) {
   case MESA_SHADER_VERTEX:
      return v->binning_pass ? "BVERT" : "VERT";
   case MESA_SHADER_TESS_CTRL:
      return "TCS";
   case MESA_SHADER_TESS_EVAL:
      return "TES";
   case MESA_SHADER_GEOMETRY:
      return "GEOM";
   case MESA_SHADER_FRAGMENT:
      return "FRAG";
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return "CL";
   default:
      unreachable("invalid type");
      return NULL;
   }
}

/* Currently we do not do binning for tess.  And for GS there is no
 * cross-stage VS+GS optimization, so the full VS+GS is used in
 * the binning pass.
 */
static inline bool
ir3_has_binning_vs(const struct ir3_shader_key *key)
{
   if (key->tessellation || key->has_gs)
      return false;
   return true;
}

/**
 * Represents a shader at the API level, before state-specific variants are
 * generated.
 */
struct ir3_shader {
   gl_shader_stage type;

   /* shader id (for debug): */
   uint32_t id;
   uint32_t variant_count;

   /* Set by freedreno after shader_state_create, so we can emit debug info
    * when recompiling a shader at draw time.
    */
   bool initial_variants_done;

   struct ir3_compiler *compiler;

   unsigned num_reserved_user_consts;

   /* What API-visible wavesizes are allowed. Even if only double wavesize is
    * allowed, we may still use the smaller wavesize "under the hood" and the
    * application simply sees the upper half as always disabled.
    */
   enum ir3_wavesize_option api_wavesize;

   /* What wavesizes we're allowed to actually use. If the API wavesize is
    * single-only, then this must be single-only too.
    */
   enum ir3_wavesize_option real_wavesize;

   bool nir_finalized;
   struct nir_shader *nir;
   struct ir3_stream_output_info stream_output;

   /* per shader stage specific info: */
   union {
      /* for compute shaders: */
      struct {
         unsigned req_input_mem;    /* in dwords */
         unsigned req_local_mem;
      } cs;
   };

   struct ir3_shader_variant *variants;
   mtx_t variants_lock;

   cache_key cache_key; /* shader disk-cache key */

   /* Bitmask of bits of the shader key used by this shader.  Used to avoid
    * recompiles for GL NOS that doesn't actually apply to the shader.
    */
   struct ir3_shader_key key_mask;
};

/**
 * In order to use the same cmdstream, in particular constlen setup and const
 * emit, for both binning and draw pass (a6xx+), the binning pass re-uses it's
 * corresponding draw pass shaders const_state.
 */
static inline struct ir3_const_state *
ir3_const_state(const struct ir3_shader_variant *v)
{
   if (v->binning_pass)
      return v->nonbinning->const_state;
   return v->const_state;
}

/* Given a variant, calculate the maximum constlen it can have.
 */

static inline unsigned
ir3_max_const(const struct ir3_shader_variant *v)
{
   const struct ir3_compiler *compiler = v->shader->compiler;

   if ((v->shader->type == MESA_SHADER_COMPUTE) ||
       (v->shader->type == MESA_SHADER_KERNEL)) {
      return compiler->max_const_compute;
   } else if (v->key.safe_constlen) {
      return compiler->max_const_safe;
   } else if (v->shader->type == MESA_SHADER_FRAGMENT) {
      return compiler->max_const_frag;
   } else {
      return compiler->max_const_geom;
   }
}

void *ir3_shader_assemble(struct ir3_shader_variant *v);
struct ir3_shader_variant *
ir3_shader_get_variant(struct ir3_shader *shader,
                       const struct ir3_shader_key *key, bool binning_pass,
                       bool keep_ir, bool *created);

struct ir3_shader_options {
   unsigned reserved_user_consts;
   enum ir3_wavesize_option api_wavesize, real_wavesize;
};

struct ir3_shader *
ir3_shader_from_nir(struct ir3_compiler *compiler, nir_shader *nir,
                    const struct ir3_shader_options *options,
                    struct ir3_stream_output_info *stream_output);
uint32_t ir3_trim_constlen(struct ir3_shader_variant **variants,
                           const struct ir3_compiler *compiler);
void ir3_shader_destroy(struct ir3_shader *shader);
void ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin, FILE *out);
uint64_t ir3_shader_outputs(const struct ir3_shader *so);

int ir3_glsl_type_size(const struct glsl_type *type, bool bindless);

/*
 * Helper/util:
 */

/* clears shader-key flags which don't apply to the given shader.
 */
static inline void
ir3_key_clear_unused(struct ir3_shader_key *key, struct ir3_shader *shader)
{
   uint32_t *key_bits = (uint32_t *)key;
   uint32_t *key_mask = (uint32_t *)&shader->key_mask;
   STATIC_ASSERT(sizeof(*key) % 4 == 0);
   for (int i = 0; i < sizeof(*key) >> 2; i++)
      key_bits[i] &= key_mask[i];
}

static inline int
ir3_find_output(const struct ir3_shader_variant *so, gl_varying_slot slot)
{
   int j;

   for (j = 0; j < so->outputs_count; j++)
      if (so->outputs[j].slot == slot)
         return j;

   /* it seems optional to have a OUT.BCOLOR[n] for each OUT.COLOR[n]
    * in the vertex shader.. but the fragment shader doesn't know this
    * so  it will always have both IN.COLOR[n] and IN.BCOLOR[n].  So
    * at link time if there is no matching OUT.BCOLOR[n], we must map
    * OUT.COLOR[n] to IN.BCOLOR[n].  And visa versa if there is only
    * a OUT.BCOLOR[n] but no matching OUT.COLOR[n]
    */
   if (slot == VARYING_SLOT_BFC0) {
      slot = VARYING_SLOT_COL0;
   } else if (slot == VARYING_SLOT_BFC1) {
      slot = VARYING_SLOT_COL1;
   } else if (slot == VARYING_SLOT_COL0) {
      slot = VARYING_SLOT_BFC0;
   } else if (slot == VARYING_SLOT_COL1) {
      slot = VARYING_SLOT_BFC1;
   } else {
      return -1;
   }

   for (j = 0; j < so->outputs_count; j++)
      if (so->outputs[j].slot == slot)
         return j;

   debug_assert(0);

   return -1;
}

static inline int
ir3_next_varying(const struct ir3_shader_variant *so, int i)
{
   while (++i < so->inputs_count)
      if (so->inputs[i].compmask && so->inputs[i].bary)
         break;
   return i;
}

struct ir3_shader_linkage {
   /* Maximum location either consumed by the fragment shader or produced by
    * the last geometry stage, i.e. the size required for each vertex in the
    * VPC in DWORD's.
    */
   uint8_t max_loc;

   /* Number of entries in var. */
   uint8_t cnt;

   /* Bitset of locations used, including ones which are only used by the FS.
    */
   uint32_t varmask[4];

   /* Map from VS output to location. */
   struct {
      uint8_t slot;
      uint8_t regid;
      uint8_t compmask;
      uint8_t loc;
   } var[32];

   /* location for fixed-function gl_PrimitiveID passthrough */
   uint8_t primid_loc;

   /* location for fixed-function gl_ViewIndex passthrough */
   uint8_t viewid_loc;

   /* location for combined clip/cull distance arrays */
   uint8_t clip0_loc, clip1_loc;
};

static inline void
ir3_link_add(struct ir3_shader_linkage *l, uint8_t slot, uint8_t regid_,
             uint8_t compmask, uint8_t loc)
{
   for (int j = 0; j < util_last_bit(compmask); j++) {
      uint8_t comploc = loc + j;
      l->varmask[comploc / 32] |= 1 << (comploc % 32);
   }

   l->max_loc = MAX2(l->max_loc, loc + util_last_bit(compmask));

   if (regid_ != regid(63, 0)) {
      int i = l->cnt++;
      debug_assert(i < ARRAY_SIZE(l->var));

      l->var[i].slot = slot;
      l->var[i].regid = regid_;
      l->var[i].compmask = compmask;
      l->var[i].loc = loc;
   }
}

static inline void
ir3_link_shaders(struct ir3_shader_linkage *l,
                 const struct ir3_shader_variant *vs,
                 const struct ir3_shader_variant *fs, bool pack_vs_out)
{
   /* On older platforms, varmask isn't programmed at all, and it appears
    * that the hardware generates a mask of used VPC locations using the VS
    * output map, and hangs if a FS bary instruction references a location
    * not in the list. This means that we need to have a dummy entry in the
    * VS out map for things like gl_PointCoord which aren't written by the
    * VS. Furthermore we can't use r63.x, so just pick a random register to
    * use if there is no VS output.
    */
   const unsigned default_regid = pack_vs_out ? regid(63, 0) : regid(0, 0);
   int j = -1, k;

   l->primid_loc = 0xff;
   l->viewid_loc = 0xff;
   l->clip0_loc = 0xff;
   l->clip1_loc = 0xff;

   while (l->cnt < ARRAY_SIZE(l->var)) {
      j = ir3_next_varying(fs, j);

      if (j >= fs->inputs_count)
         break;

      if (fs->inputs[j].inloc >= fs->total_in)
         continue;

      k = ir3_find_output(vs, fs->inputs[j].slot);

      if (k < 0 && fs->inputs[j].slot == VARYING_SLOT_PRIMITIVE_ID) {
         l->primid_loc = fs->inputs[j].inloc;
      }

      if (fs->inputs[j].slot == VARYING_SLOT_VIEW_INDEX) {
         assert(k < 0);
         l->viewid_loc = fs->inputs[j].inloc;
      }

      if (fs->inputs[j].slot == VARYING_SLOT_CLIP_DIST0)
         l->clip0_loc = fs->inputs[j].inloc;

      if (fs->inputs[j].slot == VARYING_SLOT_CLIP_DIST1)
         l->clip1_loc = fs->inputs[j].inloc;

      ir3_link_add(l, fs->inputs[j].slot,
                   k >= 0 ? vs->outputs[k].regid : default_regid,
                   fs->inputs[j].compmask, fs->inputs[j].inloc);
   }
}

static inline uint32_t
ir3_find_output_regid(const struct ir3_shader_variant *so, unsigned slot)
{
   int j;
   for (j = 0; j < so->outputs_count; j++)
      if (so->outputs[j].slot == slot) {
         uint32_t regid = so->outputs[j].regid;
         if (so->outputs[j].half)
            regid |= HALF_REG_ID;
         return regid;
      }
   return regid(63, 0);
}

void ir3_link_stream_out(struct ir3_shader_linkage *l,
                         const struct ir3_shader_variant *v);

#define VARYING_SLOT_GS_HEADER_IR3       (VARYING_SLOT_MAX + 0)
#define VARYING_SLOT_GS_VERTEX_FLAGS_IR3 (VARYING_SLOT_MAX + 1)
#define VARYING_SLOT_TCS_HEADER_IR3      (VARYING_SLOT_MAX + 2)
#define VARYING_SLOT_REL_PATCH_ID_IR3    (VARYING_SLOT_MAX + 3)

static inline uint32_t
ir3_find_sysval_regid(const struct ir3_shader_variant *so, unsigned slot)
{
   int j;
   for (j = 0; j < so->inputs_count; j++)
      if (so->inputs[j].sysval && (so->inputs[j].slot == slot))
         return so->inputs[j].regid;
   return regid(63, 0);
}

/* calculate register footprint in terms of half-regs (ie. one full
 * reg counts as two half-regs).
 */
static inline uint32_t
ir3_shader_halfregs(const struct ir3_shader_variant *v)
{
   return (2 * (v->info.max_reg + 1)) + (v->info.max_half_reg + 1);
}

static inline uint32_t
ir3_shader_nibo(const struct ir3_shader_variant *v)
{
   /* The dummy variant used in binning mode won't have an actual shader. */
   if (!v->shader)
      return 0;

   return v->shader->nir->info.num_ssbos + v->shader->nir->info.num_images;
}

static inline uint32_t
ir3_shader_branchstack_hw(const struct ir3_shader_variant *v)
{
   /* Dummy shader */
   if (!v->shader)
      return 0;

   if (v->shader->compiler->gen < 5)
      return v->branchstack;

   if (v->branchstack > 0) {
      uint32_t branchstack = v->branchstack / 2 + 1;
      return MIN2(branchstack, v->shader->compiler->branchstack_size / 2);
   } else {
      return 0;
   }
}

#endif /* IR3_SHADER_H_ */
