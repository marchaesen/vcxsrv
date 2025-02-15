/**************************************************************************
 * 
 * Copyright 2007-2008 VMware, Inc.
 * All Rights Reserved.
 * Copyright 2009-2010 VMware, Inc.  All rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef TGSI_EXEC_H
#define TGSI_EXEC_H

#include "util/compiler.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"

#if defined __cplusplus
extern "C" {
#endif

#define TGSI_CHAN_X 0
#define TGSI_CHAN_Y 1
#define TGSI_CHAN_Z 2
#define TGSI_CHAN_W 3

#define TGSI_NUM_CHANNELS 4  /* R,G,B,A */
#define TGSI_QUAD_SIZE    4  /* 4 pixel/quad */

#define TGSI_FOR_EACH_CHANNEL( CHAN )\
   for (CHAN = 0; CHAN < TGSI_NUM_CHANNELS; CHAN++)

#define TGSI_IS_DST0_CHANNEL_ENABLED( INST, CHAN )\
   ((INST)->Dst[0].Register.WriteMask & (1 << (CHAN)))

#define TGSI_IF_IS_DST0_CHANNEL_ENABLED( INST, CHAN )\
   if (TGSI_IS_DST0_CHANNEL_ENABLED( INST, CHAN ))

#define TGSI_FOR_EACH_DST0_ENABLED_CHANNEL( INST, CHAN )\
   TGSI_FOR_EACH_CHANNEL( CHAN )\
      TGSI_IF_IS_DST0_CHANNEL_ENABLED( INST, CHAN )

#define TGSI_IS_DST1_CHANNEL_ENABLED( INST, CHAN )\
   ((INST)->Dst[1].Register.WriteMask & (1 << (CHAN)))

#define TGSI_IF_IS_DST1_CHANNEL_ENABLED( INST, CHAN )\
   if (TGSI_IS_DST1_CHANNEL_ENABLED( INST, CHAN ))

#define TGSI_FOR_EACH_DST1_ENABLED_CHANNEL( INST, CHAN )\
   TGSI_FOR_EACH_CHANNEL( CHAN )\
      TGSI_IF_IS_DST1_CHANNEL_ENABLED( INST, CHAN )

/**
  * Registers may be treated as float, signed int or unsigned int.
  */
union tgsi_exec_channel
{
   alignas(16)
   float    f[TGSI_QUAD_SIZE];
   int32_t  i[TGSI_QUAD_SIZE];
   uint32_t u[TGSI_QUAD_SIZE];
};

/**
  * A vector[RGBA] of channels[4 pixels]
  */
struct tgsi_exec_vector
{
   alignas(16) union tgsi_exec_channel xyzw[TGSI_NUM_CHANNELS];
};

/**
 * For fragment programs, information for computing fragment input
 * values from plane equation of the triangle/line.
 */
struct tgsi_interp_coef
{
   float a0[TGSI_NUM_CHANNELS];	/* in an xyzw layout */
   float dadx[TGSI_NUM_CHANNELS];
   float dady[TGSI_NUM_CHANNELS];
};

enum tgsi_sampler_control
{
   TGSI_SAMPLER_LOD_NONE,
   TGSI_SAMPLER_LOD_BIAS,
   TGSI_SAMPLER_LOD_EXPLICIT,
   TGSI_SAMPLER_LOD_ZERO,
   TGSI_SAMPLER_DERIVS_EXPLICIT,
   TGSI_SAMPLER_GATHER,
};

struct tgsi_image_params {
   unsigned unit;
   unsigned tgsi_tex_instr;
   enum pipe_format format;
   unsigned execmask;
};

struct tgsi_image {
   /* image interfaces */
   void (*load)(const struct tgsi_image *image,
                const struct tgsi_image_params *params,
                const int s[TGSI_QUAD_SIZE],
                const int t[TGSI_QUAD_SIZE],
                const int r[TGSI_QUAD_SIZE],
                const int sample[TGSI_QUAD_SIZE],
                float rgba[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE]);

   void (*store)(const struct tgsi_image *image,
                 const struct tgsi_image_params *params,
                 const int s[TGSI_QUAD_SIZE],
                 const int t[TGSI_QUAD_SIZE],
                 const int r[TGSI_QUAD_SIZE],
                 const int sample[TGSI_QUAD_SIZE],
                 float rgba[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE]);

   void (*op)(const struct tgsi_image *image,
              const struct tgsi_image_params *params,
              enum tgsi_opcode opcode,
              const int s[TGSI_QUAD_SIZE],
              const int t[TGSI_QUAD_SIZE],
              const int r[TGSI_QUAD_SIZE],
              const int sample[TGSI_QUAD_SIZE],
              float rgba[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE],
              float rgba2[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE]);

   void (*get_dims)(const struct tgsi_image *image,
                    const struct tgsi_image_params *params,
                    int dims[4]);
};

struct tgsi_buffer_params {
   unsigned unit;
   unsigned execmask;
   unsigned writemask;
};

/* SSBO interfaces */
struct tgsi_buffer {
   void *(*lookup)(const struct tgsi_buffer *buffer,
                   uint32_t unit, uint32_t *size);
};

/**
 * Information for sampling textures, which must be implemented
 * by code outside the TGSI executor.
 */
struct tgsi_sampler
{
   /** Get samples for four fragments in a quad */
   /* this interface contains 5 sets of channels that vary
    * depending on the sampler.
    * s - the first texture coordinate for sampling.
    * t - the second texture coordinate for sampling - unused for 1D,
          layer for 1D arrays.
    * r - the third coordinate for sampling for 3D, cube, cube arrays,
    *     layer for 2D arrays. Compare value for 1D/2D shadows.
    * c0 - Compare value for shadow cube and shadow 2d arrays,
    *      layer for cube arrays.
    * derivs - explicit derivatives.
    * offset - texel offsets
    * lod - lod value, except for shadow cube arrays (compare value there).
    */
   void (*get_samples)(struct tgsi_sampler *sampler,
                       const unsigned sview_index,
                       const unsigned sampler_index,
                       const float s[TGSI_QUAD_SIZE],
                       const float t[TGSI_QUAD_SIZE],
                       const float r[TGSI_QUAD_SIZE],
                       const float c0[TGSI_QUAD_SIZE],
                       const float c1[TGSI_QUAD_SIZE],
                       float derivs[3][2][TGSI_QUAD_SIZE],
                       const int8_t offset[3],
                       enum tgsi_sampler_control control,
                       float rgba[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE]);
   void (*get_dims)(struct tgsi_sampler *sampler,
                    const unsigned sview_index,
                    int level, int dims[4]);
   void (*get_texel)(struct tgsi_sampler *sampler,
                     const unsigned sview_index,
                     const int i[TGSI_QUAD_SIZE],
                     const int j[TGSI_QUAD_SIZE], const int k[TGSI_QUAD_SIZE],
                     const int lod[TGSI_QUAD_SIZE], const int8_t offset[3],
                     float rgba[TGSI_NUM_CHANNELS][TGSI_QUAD_SIZE]);
   void (*query_lod)(const struct tgsi_sampler *tgsi_sampler,
                     const unsigned sview_index,
                     const unsigned sampler_index,
                     const float s[TGSI_QUAD_SIZE],
                     const float t[TGSI_QUAD_SIZE],
                     const float p[TGSI_QUAD_SIZE],
                     const float c0[TGSI_QUAD_SIZE],
                     const enum tgsi_sampler_control control,
                     float mipmap[TGSI_QUAD_SIZE],
                     float lod[TGSI_QUAD_SIZE]);
};

#define TGSI_EXEC_NUM_TEMPS       4096

#define TGSI_EXEC_MAX_NESTING  32
#define TGSI_EXEC_MAX_COND_NESTING  TGSI_EXEC_MAX_NESTING
#define TGSI_EXEC_MAX_LOOP_NESTING  TGSI_EXEC_MAX_NESTING
#define TGSI_EXEC_MAX_SWITCH_NESTING TGSI_EXEC_MAX_NESTING
#define TGSI_EXEC_MAX_CALL_NESTING  TGSI_EXEC_MAX_NESTING

/* The maximum number of input attributes per vertex. For 2D
 * input register files, this is the stride between two 1D
 * arrays.
 */
#define TGSI_EXEC_MAX_INPUT_ATTRIBS 32

/* The maximum number of bytes per constant buffer.
 */
#define TGSI_EXEC_MAX_CONST_BUFFER_SIZE  (4096 * sizeof(float[4]))

/* The maximum number of vertices per primitive */
#define TGSI_MAX_PRIM_VERTICES 6

/* The maximum number of primitives to be generated */
#define TGSI_MAX_PRIMITIVES 64

/* The maximum total number of vertices */
#define TGSI_MAX_TOTAL_VERTICES (TGSI_MAX_PRIM_VERTICES * TGSI_MAX_PRIMITIVES * PIPE_MAX_ATTRIBS)

#define TGSI_MAX_MISC_INPUTS 8

#define TGSI_MAX_VERTEX_STREAMS 4

/** function call/activation record */
struct tgsi_call_record
{
   unsigned CondStackTop;
   unsigned LoopStackTop;
   unsigned ContStackTop;
   int SwitchStackTop;
   int BreakStackTop;
   unsigned ReturnAddr;
};

/* should match draw_buffer_info */
struct tgsi_exec_consts_info {
   const void *ptr;
   unsigned size;
};

/* Switch-case block state. */
struct tgsi_switch_record {
   unsigned mask;                          /**< execution mask */
   union tgsi_exec_channel selector;   /**< a value case statements are compared to */
   unsigned defaultMask;                   /**< non-execute mask for default case */
};


enum tgsi_break_type {
   TGSI_EXEC_BREAK_INSIDE_LOOP,
   TGSI_EXEC_BREAK_INSIDE_SWITCH
};


#define TGSI_EXEC_MAX_BREAK_STACK (TGSI_EXEC_MAX_LOOP_NESTING + TGSI_EXEC_MAX_SWITCH_NESTING)

typedef float float4[4];

struct tgsi_exec_machine;

typedef void (* apply_sample_offset_func)(
   const struct tgsi_exec_machine *mach,
   unsigned attrib,
   unsigned chan,
   float ofs_x,
   float ofs_y,
   union tgsi_exec_channel *out_chan);

/**
 * Run-time virtual machine state for executing TGSI shader.
 */
struct tgsi_exec_machine
{
   /* Total = program temporaries + internal temporaries
    */
   alignas(16)
   struct tgsi_exec_vector       Temps[TGSI_EXEC_NUM_TEMPS];

   unsigned                       ImmsReserved;
   float4                         *Imms;

   struct tgsi_exec_vector       *Inputs;
   struct tgsi_exec_vector       *Outputs;
   apply_sample_offset_func           *InputSampleOffsetApply;

   /* System values */
   unsigned                      SysSemanticToIndex[TGSI_SEMANTIC_COUNT];
   struct tgsi_exec_vector       SystemValue[TGSI_MAX_MISC_INPUTS];

   struct tgsi_exec_vector       Addrs[3];

   struct tgsi_sampler           *Sampler;

   struct tgsi_image             *Image;
   struct tgsi_buffer            *Buffer;
   unsigned                      ImmLimit;

   const void *Consts[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned ConstsSize[PIPE_MAX_CONSTANT_BUFFERS];

   const struct tgsi_token       *Tokens;   /**< Declarations, instructions */
   enum pipe_shader_type         ShaderType; /**< PIPE_SHADER_x */

   /* GEOMETRY processor only. */
   /* Number of vertices emitted per emitted primitive. */
   unsigned                      *Primitives[TGSI_MAX_VERTEX_STREAMS];
   /* Offsets in ->Outputs of the primitives' vertex output data */
   unsigned                      *PrimitiveOffsets[TGSI_MAX_VERTEX_STREAMS];
   unsigned                       NumOutputs;
   unsigned                       MaxOutputVertices;
   /* Offset in ->Outputs for the current vertex to be emitted. */
   unsigned                       OutputVertexOffset;
   /* Number of primitives emitted. */
   unsigned                       OutputPrimCount[TGSI_MAX_VERTEX_STREAMS];

   /* FRAGMENT processor only. */
   const struct tgsi_interp_coef *InterpCoefs;
   struct tgsi_exec_vector       QuadPos;
   float                         Face;    /**< +1 if front facing, -1 if back facing */
   bool                          flatshade_color;

   /* Compute Only */
   void                          *LocalMem;
   unsigned                      LocalMemSize;

   /* See GLSL 4.50 specification for definition of helper invocations */
   unsigned NonHelperMask;  /**< non-helpers */
   /* Conditional execution masks */
   unsigned CondMask;  /**< For IF/ELSE/ENDIF */
   unsigned LoopMask;  /**< For BGNLOOP/ENDLOOP */
   unsigned ContMask;  /**< For loop CONT statements */
   unsigned FuncMask;  /**< For function calls */
   unsigned ExecMask;  /**< = CondMask & LoopMask */
   unsigned KillMask;  /**< Mask of channels killed in the current shader execution */

   /* Current switch-case state. */
   struct tgsi_switch_record Switch;

   /* Current break type. */
   enum tgsi_break_type BreakType;

   /** Condition mask stack (for nested conditionals) */
   unsigned CondStack[TGSI_EXEC_MAX_COND_NESTING];
   int CondStackTop;

   /** Loop mask stack (for nested loops) */
   unsigned LoopStack[TGSI_EXEC_MAX_LOOP_NESTING];
   int LoopStackTop;

   /** Loop label stack */
   unsigned LoopLabelStack[TGSI_EXEC_MAX_LOOP_NESTING];
   int LoopLabelStackTop;

   /** Loop continue mask stack (see comments in tgsi_exec.c) */
   unsigned ContStack[TGSI_EXEC_MAX_LOOP_NESTING];
   int ContStackTop;

   /** Switch case stack */
   struct tgsi_switch_record SwitchStack[TGSI_EXEC_MAX_SWITCH_NESTING];
   int SwitchStackTop;

   enum tgsi_break_type BreakStack[TGSI_EXEC_MAX_BREAK_STACK];
   int BreakStackTop;

   /** Function execution mask stack (for executing subroutine code) */
   unsigned FuncStack[TGSI_EXEC_MAX_CALL_NESTING];
   int FuncStackTop;

   /** Function call stack for saving/restoring the program counter */
   struct tgsi_call_record CallStack[TGSI_EXEC_MAX_CALL_NESTING];
   int CallStackTop;

   struct tgsi_full_instruction *Instructions;
   unsigned NumInstructions;

   struct tgsi_full_declaration *Declarations;
   unsigned NumDeclarations;

   struct tgsi_declaration_sampler_view
      SamplerViews[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   bool UsedGeometryShader;

   int pc;
};

struct tgsi_exec_machine *
tgsi_exec_machine_create(enum pipe_shader_type shader_type);

void
tgsi_exec_machine_destroy(struct tgsi_exec_machine *mach);


void 
tgsi_exec_machine_bind_shader(
   struct tgsi_exec_machine *mach,
   const struct tgsi_token *tokens,
   struct tgsi_sampler *sampler,
   struct tgsi_image *image,
   struct tgsi_buffer *buffer);

uint
tgsi_exec_machine_run(
   struct tgsi_exec_machine *mach, int start_pc );


extern void
tgsi_exec_set_constant_buffers(struct tgsi_exec_machine *mach,
                               unsigned num_bufs,
                               const struct tgsi_exec_consts_info *bufs);

static inline void
tgsi_exec_init_shader_caps(struct pipe_shader_caps *caps)
{
   caps->max_instructions =
   caps->max_alu_instructions =
   caps->max_tex_instructions =
   caps->max_tex_indirections = INT_MAX;
   caps->max_control_flow_depth = TGSI_EXEC_MAX_NESTING;
   caps->max_inputs = TGSI_EXEC_MAX_INPUT_ATTRIBS;
   caps->max_outputs = 32;
   caps->max_const_buffer0_size = TGSI_EXEC_MAX_CONST_BUFFER_SIZE;
   caps->max_const_buffers = PIPE_MAX_CONSTANT_BUFFERS;
   caps->max_temps = TGSI_EXEC_NUM_TEMPS;
   caps->cont_supported = true;
   caps->indirect_temp_addr = true;
   caps->indirect_const_addr = true;
   caps->subroutines = true;
   caps->integers = true;
   caps->max_texture_samplers = PIPE_MAX_SAMPLERS;
   caps->max_sampler_views = PIPE_MAX_SHADER_SAMPLER_VIEWS;
   caps->supported_irs = 1 << PIPE_SHADER_IR_TGSI;
   caps->tgsi_sqrt_supported = true;
   caps->tgsi_any_inout_decl_range = true;
   caps->max_shader_buffers = PIPE_MAX_SHADER_BUFFERS;
   caps->max_shader_images = PIPE_MAX_SHADER_IMAGES;
}

#if defined __cplusplus
} /* extern "C" */
#endif

#endif /* TGSI_EXEC_H */
