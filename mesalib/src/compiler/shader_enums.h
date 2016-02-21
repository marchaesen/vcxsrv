/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SHADER_ENUMS_H
#define SHADER_ENUMS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shader stages. Note that these will become 5 with tessellation.
 *
 * The order must match how shaders are ordered in the pipeline.
 * The GLSL linker assumes that if i<j, then the j-th shader is
 * executed later than the i-th shader.
 */
typedef enum
{
   MESA_SHADER_VERTEX = 0,
   MESA_SHADER_TESS_CTRL = 1,
   MESA_SHADER_TESS_EVAL = 2,
   MESA_SHADER_GEOMETRY = 3,
   MESA_SHADER_FRAGMENT = 4,
   MESA_SHADER_COMPUTE = 5,
} gl_shader_stage;

const char *gl_shader_stage_name(gl_shader_stage stage);

/**
 * Translate a gl_shader_stage to a short shader stage name for debug
 * printouts and error messages.
 */
const char *_mesa_shader_stage_to_string(unsigned stage);

/**
 * Translate a gl_shader_stage to a shader stage abbreviation (VS, GS, FS)
 * for debug printouts and error messages.
 */
const char *_mesa_shader_stage_to_abbrev(unsigned stage);

#define MESA_SHADER_STAGES (MESA_SHADER_COMPUTE + 1)


/**
 * Indexes for vertex program attributes.
 * GL_NV_vertex_program aliases generic attributes over the conventional
 * attributes.  In GL_ARB_vertex_program shader the aliasing is optional.
 * In GL_ARB_vertex_shader / OpenGL 2.0 the aliasing is disallowed (the
 * generic attributes are distinct/separate).
 */
typedef enum
{
   VERT_ATTRIB_POS = 0,
   VERT_ATTRIB_WEIGHT = 1,
   VERT_ATTRIB_NORMAL = 2,
   VERT_ATTRIB_COLOR0 = 3,
   VERT_ATTRIB_COLOR1 = 4,
   VERT_ATTRIB_FOG = 5,
   VERT_ATTRIB_COLOR_INDEX = 6,
   VERT_ATTRIB_EDGEFLAG = 7,
   VERT_ATTRIB_TEX0 = 8,
   VERT_ATTRIB_TEX1 = 9,
   VERT_ATTRIB_TEX2 = 10,
   VERT_ATTRIB_TEX3 = 11,
   VERT_ATTRIB_TEX4 = 12,
   VERT_ATTRIB_TEX5 = 13,
   VERT_ATTRIB_TEX6 = 14,
   VERT_ATTRIB_TEX7 = 15,
   VERT_ATTRIB_POINT_SIZE = 16,
   VERT_ATTRIB_GENERIC0 = 17,
   VERT_ATTRIB_GENERIC1 = 18,
   VERT_ATTRIB_GENERIC2 = 19,
   VERT_ATTRIB_GENERIC3 = 20,
   VERT_ATTRIB_GENERIC4 = 21,
   VERT_ATTRIB_GENERIC5 = 22,
   VERT_ATTRIB_GENERIC6 = 23,
   VERT_ATTRIB_GENERIC7 = 24,
   VERT_ATTRIB_GENERIC8 = 25,
   VERT_ATTRIB_GENERIC9 = 26,
   VERT_ATTRIB_GENERIC10 = 27,
   VERT_ATTRIB_GENERIC11 = 28,
   VERT_ATTRIB_GENERIC12 = 29,
   VERT_ATTRIB_GENERIC13 = 30,
   VERT_ATTRIB_GENERIC14 = 31,
   VERT_ATTRIB_GENERIC15 = 32,
   VERT_ATTRIB_MAX = 33
} gl_vert_attrib;

const char *gl_vert_attrib_name(gl_vert_attrib attrib);

/**
 * Symbolic constats to help iterating over
 * specific blocks of vertex attributes.
 *
 * VERT_ATTRIB_FF
 *   includes all fixed function attributes as well as
 *   the aliased GL_NV_vertex_program shader attributes.
 * VERT_ATTRIB_TEX
 *   include the classic texture coordinate attributes.
 *   Is a subset of VERT_ATTRIB_FF.
 * VERT_ATTRIB_GENERIC
 *   include the OpenGL 2.0+ GLSL generic shader attributes.
 *   These alias the generic GL_ARB_vertex_shader attributes.
 */
#define VERT_ATTRIB_FF(i)           (VERT_ATTRIB_POS + (i))
#define VERT_ATTRIB_FF_MAX          VERT_ATTRIB_GENERIC0

#define VERT_ATTRIB_TEX(i)          (VERT_ATTRIB_TEX0 + (i))
#define VERT_ATTRIB_TEX_MAX         MAX_TEXTURE_COORD_UNITS

#define VERT_ATTRIB_GENERIC(i)      (VERT_ATTRIB_GENERIC0 + (i))
#define VERT_ATTRIB_GENERIC_MAX     MAX_VERTEX_GENERIC_ATTRIBS

/**
 * Bitflags for vertex attributes.
 * These are used in bitfields in many places.
 */
/*@{*/
#define VERT_BIT_POS             BITFIELD64_BIT(VERT_ATTRIB_POS)
#define VERT_BIT_WEIGHT          BITFIELD64_BIT(VERT_ATTRIB_WEIGHT)
#define VERT_BIT_NORMAL          BITFIELD64_BIT(VERT_ATTRIB_NORMAL)
#define VERT_BIT_COLOR0          BITFIELD64_BIT(VERT_ATTRIB_COLOR0)
#define VERT_BIT_COLOR1          BITFIELD64_BIT(VERT_ATTRIB_COLOR1)
#define VERT_BIT_FOG             BITFIELD64_BIT(VERT_ATTRIB_FOG)
#define VERT_BIT_COLOR_INDEX     BITFIELD64_BIT(VERT_ATTRIB_COLOR_INDEX)
#define VERT_BIT_EDGEFLAG        BITFIELD64_BIT(VERT_ATTRIB_EDGEFLAG)
#define VERT_BIT_TEX0            BITFIELD64_BIT(VERT_ATTRIB_TEX0)
#define VERT_BIT_TEX1            BITFIELD64_BIT(VERT_ATTRIB_TEX1)
#define VERT_BIT_TEX2            BITFIELD64_BIT(VERT_ATTRIB_TEX2)
#define VERT_BIT_TEX3            BITFIELD64_BIT(VERT_ATTRIB_TEX3)
#define VERT_BIT_TEX4            BITFIELD64_BIT(VERT_ATTRIB_TEX4)
#define VERT_BIT_TEX5            BITFIELD64_BIT(VERT_ATTRIB_TEX5)
#define VERT_BIT_TEX6            BITFIELD64_BIT(VERT_ATTRIB_TEX6)
#define VERT_BIT_TEX7            BITFIELD64_BIT(VERT_ATTRIB_TEX7)
#define VERT_BIT_POINT_SIZE      BITFIELD64_BIT(VERT_ATTRIB_POINT_SIZE)
#define VERT_BIT_GENERIC0        BITFIELD64_BIT(VERT_ATTRIB_GENERIC0)

#define VERT_BIT(i)              BITFIELD64_BIT(i)
#define VERT_BIT_ALL             BITFIELD64_RANGE(0, VERT_ATTRIB_MAX)

#define VERT_BIT_FF(i)           VERT_BIT(i)
#define VERT_BIT_FF_ALL          BITFIELD64_RANGE(0, VERT_ATTRIB_FF_MAX)
#define VERT_BIT_TEX(i)          VERT_BIT(VERT_ATTRIB_TEX(i))
#define VERT_BIT_TEX_ALL         \
   BITFIELD64_RANGE(VERT_ATTRIB_TEX(0), VERT_ATTRIB_TEX_MAX)

#define VERT_BIT_GENERIC(i)      VERT_BIT(VERT_ATTRIB_GENERIC(i))
#define VERT_BIT_GENERIC_ALL     \
   BITFIELD64_RANGE(VERT_ATTRIB_GENERIC(0), VERT_ATTRIB_GENERIC_MAX)
/*@}*/


/**
 * Indexes for vertex shader outputs, geometry shader inputs/outputs, and
 * fragment shader inputs.
 *
 * Note that some of these values are not available to all pipeline stages.
 *
 * When this enum is updated, the following code must be updated too:
 * - vertResults (in prog_print.c's arb_output_attrib_string())
 * - fragAttribs (in prog_print.c's arb_input_attrib_string())
 * - _mesa_varying_slot_in_fs()
 */
typedef enum
{
   VARYING_SLOT_POS,
   VARYING_SLOT_COL0, /* COL0 and COL1 must be contiguous */
   VARYING_SLOT_COL1,
   VARYING_SLOT_FOGC,
   VARYING_SLOT_TEX0, /* TEX0-TEX7 must be contiguous */
   VARYING_SLOT_TEX1,
   VARYING_SLOT_TEX2,
   VARYING_SLOT_TEX3,
   VARYING_SLOT_TEX4,
   VARYING_SLOT_TEX5,
   VARYING_SLOT_TEX6,
   VARYING_SLOT_TEX7,
   VARYING_SLOT_PSIZ, /* Does not appear in FS */
   VARYING_SLOT_BFC0, /* Does not appear in FS */
   VARYING_SLOT_BFC1, /* Does not appear in FS */
   VARYING_SLOT_EDGE, /* Does not appear in FS */
   VARYING_SLOT_CLIP_VERTEX, /* Does not appear in FS */
   VARYING_SLOT_CLIP_DIST0,
   VARYING_SLOT_CLIP_DIST1,
   VARYING_SLOT_PRIMITIVE_ID, /* Does not appear in VS */
   VARYING_SLOT_LAYER, /* Appears as VS or GS output */
   VARYING_SLOT_VIEWPORT, /* Appears as VS or GS output */
   VARYING_SLOT_FACE, /* FS only */
   VARYING_SLOT_PNTC, /* FS only */
   VARYING_SLOT_TESS_LEVEL_OUTER, /* Only appears as TCS output. */
   VARYING_SLOT_TESS_LEVEL_INNER, /* Only appears as TCS output. */
   VARYING_SLOT_VAR0, /* First generic varying slot */
   /* the remaining are simply for the benefit of gl_varying_slot_name()
    * and not to be construed as an upper bound:
    */
   VARYING_SLOT_VAR1,
   VARYING_SLOT_VAR2,
   VARYING_SLOT_VAR3,
   VARYING_SLOT_VAR4,
   VARYING_SLOT_VAR5,
   VARYING_SLOT_VAR6,
   VARYING_SLOT_VAR7,
   VARYING_SLOT_VAR8,
   VARYING_SLOT_VAR9,
   VARYING_SLOT_VAR10,
   VARYING_SLOT_VAR11,
   VARYING_SLOT_VAR12,
   VARYING_SLOT_VAR13,
   VARYING_SLOT_VAR14,
   VARYING_SLOT_VAR15,
   VARYING_SLOT_VAR16,
   VARYING_SLOT_VAR17,
   VARYING_SLOT_VAR18,
   VARYING_SLOT_VAR19,
   VARYING_SLOT_VAR20,
   VARYING_SLOT_VAR21,
   VARYING_SLOT_VAR22,
   VARYING_SLOT_VAR23,
   VARYING_SLOT_VAR24,
   VARYING_SLOT_VAR25,
   VARYING_SLOT_VAR26,
   VARYING_SLOT_VAR27,
   VARYING_SLOT_VAR28,
   VARYING_SLOT_VAR29,
   VARYING_SLOT_VAR30,
   VARYING_SLOT_VAR31,
} gl_varying_slot;


#define VARYING_SLOT_MAX	(VARYING_SLOT_VAR0 + MAX_VARYING)
#define VARYING_SLOT_PATCH0	(VARYING_SLOT_MAX)
#define VARYING_SLOT_TESS_MAX	(VARYING_SLOT_PATCH0 + MAX_VARYING)

const char *gl_varying_slot_name(gl_varying_slot slot);

/**
 * Bitflags for varying slots.
 */
/*@{*/
#define VARYING_BIT_POS BITFIELD64_BIT(VARYING_SLOT_POS)
#define VARYING_BIT_COL0 BITFIELD64_BIT(VARYING_SLOT_COL0)
#define VARYING_BIT_COL1 BITFIELD64_BIT(VARYING_SLOT_COL1)
#define VARYING_BIT_FOGC BITFIELD64_BIT(VARYING_SLOT_FOGC)
#define VARYING_BIT_TEX0 BITFIELD64_BIT(VARYING_SLOT_TEX0)
#define VARYING_BIT_TEX1 BITFIELD64_BIT(VARYING_SLOT_TEX1)
#define VARYING_BIT_TEX2 BITFIELD64_BIT(VARYING_SLOT_TEX2)
#define VARYING_BIT_TEX3 BITFIELD64_BIT(VARYING_SLOT_TEX3)
#define VARYING_BIT_TEX4 BITFIELD64_BIT(VARYING_SLOT_TEX4)
#define VARYING_BIT_TEX5 BITFIELD64_BIT(VARYING_SLOT_TEX5)
#define VARYING_BIT_TEX6 BITFIELD64_BIT(VARYING_SLOT_TEX6)
#define VARYING_BIT_TEX7 BITFIELD64_BIT(VARYING_SLOT_TEX7)
#define VARYING_BIT_TEX(U) BITFIELD64_BIT(VARYING_SLOT_TEX0 + (U))
#define VARYING_BITS_TEX_ANY BITFIELD64_RANGE(VARYING_SLOT_TEX0, \
                                              MAX_TEXTURE_COORD_UNITS)
#define VARYING_BIT_PSIZ BITFIELD64_BIT(VARYING_SLOT_PSIZ)
#define VARYING_BIT_BFC0 BITFIELD64_BIT(VARYING_SLOT_BFC0)
#define VARYING_BIT_BFC1 BITFIELD64_BIT(VARYING_SLOT_BFC1)
#define VARYING_BIT_EDGE BITFIELD64_BIT(VARYING_SLOT_EDGE)
#define VARYING_BIT_CLIP_VERTEX BITFIELD64_BIT(VARYING_SLOT_CLIP_VERTEX)
#define VARYING_BIT_CLIP_DIST0 BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST0)
#define VARYING_BIT_CLIP_DIST1 BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST1)
#define VARYING_BIT_PRIMITIVE_ID BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_ID)
#define VARYING_BIT_LAYER BITFIELD64_BIT(VARYING_SLOT_LAYER)
#define VARYING_BIT_VIEWPORT BITFIELD64_BIT(VARYING_SLOT_VIEWPORT)
#define VARYING_BIT_FACE BITFIELD64_BIT(VARYING_SLOT_FACE)
#define VARYING_BIT_PNTC BITFIELD64_BIT(VARYING_SLOT_PNTC)
#define VARYING_BIT_TESS_LEVEL_OUTER BITFIELD64_BIT(VARYING_SLOT_TESS_LEVEL_OUTER)
#define VARYING_BIT_TESS_LEVEL_INNER BITFIELD64_BIT(VARYING_SLOT_TESS_LEVEL_INNER)
#define VARYING_BIT_VAR(V) BITFIELD64_BIT(VARYING_SLOT_VAR0 + (V))
/*@}*/

/**
 * Bitflags for system values.
 */
#define SYSTEM_BIT_SAMPLE_ID ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_ID)
#define SYSTEM_BIT_SAMPLE_POS ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_POS)
#define SYSTEM_BIT_SAMPLE_MASK_IN ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_MASK_IN)
#define SYSTEM_BIT_LOCAL_INVOCATION_ID ((uint64_t)1 << SYSTEM_VALUE_LOCAL_INVOCATION_ID)

/**
 * If the gl_register_file is PROGRAM_SYSTEM_VALUE, the register index will be
 * one of these values.  If a NIR variable's mode is nir_var_system_value, it
 * will be one of these values.
 */
typedef enum
{
   /**
    * \name Vertex shader system values
    */
   /*@{*/
   /**
    * OpenGL-style vertex ID.
    *
    * Section 2.11.7 (Shader Execution), subsection Shader Inputs, of the
    * OpenGL 3.3 core profile spec says:
    *
    *     "gl_VertexID holds the integer index i implicitly passed by
    *     DrawArrays or one of the other drawing commands defined in section
    *     2.8.3."
    *
    * Section 2.8.3 (Drawing Commands) of the same spec says:
    *
    *     "The commands....are equivalent to the commands with the same base
    *     name (without the BaseVertex suffix), except that the ith element
    *     transferred by the corresponding draw call will be taken from
    *     element indices[i] + basevertex of each enabled array."
    *
    * Additionally, the overview in the GL_ARB_shader_draw_parameters spec
    * says:
    *
    *     "In unextended GL, vertex shaders have inputs named gl_VertexID and
    *     gl_InstanceID, which contain, respectively the index of the vertex
    *     and instance. The value of gl_VertexID is the implicitly passed
    *     index of the vertex being processed, which includes the value of
    *     baseVertex, for those commands that accept it."
    *
    * gl_VertexID gets basevertex added in.  This differs from DirectX where
    * SV_VertexID does \b not get basevertex added in.
    *
    * \note
    * If all system values are available, \c SYSTEM_VALUE_VERTEX_ID will be
    * equal to \c SYSTEM_VALUE_VERTEX_ID_ZERO_BASE plus
    * \c SYSTEM_VALUE_BASE_VERTEX.
    *
    * \sa SYSTEM_VALUE_VERTEX_ID_ZERO_BASE, SYSTEM_VALUE_BASE_VERTEX
    */
   SYSTEM_VALUE_VERTEX_ID,

   /**
    * Instanced ID as supplied to gl_InstanceID
    *
    * Values assigned to gl_InstanceID always begin with zero, regardless of
    * the value of baseinstance.
    *
    * Section 11.1.3.9 (Shader Inputs) of the OpenGL 4.4 core profile spec
    * says:
    *
    *     "gl_InstanceID holds the integer instance number of the current
    *     primitive in an instanced draw call (see section 10.5)."
    *
    * Through a big chain of pseudocode, section 10.5 describes that
    * baseinstance is not counted by gl_InstanceID.  In that section, notice
    *
    *     "If an enabled vertex attribute array is instanced (it has a
    *     non-zero divisor as specified by VertexAttribDivisor), the element
    *     index that is transferred to the GL, for all vertices, is given by
    *
    *         floor(instance/divisor) + baseinstance
    *
    *     If an array corresponding to an attribute required by a vertex
    *     shader is not enabled, then the corresponding element is taken from
    *     the current attribute state (see section 10.2)."
    *
    * Note that baseinstance is \b not included in the value of instance.
    */
   SYSTEM_VALUE_INSTANCE_ID,

   /**
    * DirectX-style vertex ID.
    *
    * Unlike \c SYSTEM_VALUE_VERTEX_ID, this system value does \b not include
    * the value of basevertex.
    *
    * \sa SYSTEM_VALUE_VERTEX_ID, SYSTEM_VALUE_BASE_VERTEX
    */
   SYSTEM_VALUE_VERTEX_ID_ZERO_BASE,

   /**
    * Value of \c basevertex passed to \c glDrawElementsBaseVertex and similar
    * functions.
    *
    * \sa SYSTEM_VALUE_VERTEX_ID, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE
    */
   SYSTEM_VALUE_BASE_VERTEX,

   /**
    * Value of \c baseinstance passed to instanced draw entry points
    *
    * \sa SYSTEM_VALUE_INSTANCE_ID
    */
   SYSTEM_VALUE_BASE_INSTANCE,

   /**
    * From _ARB_shader_draw_parameters:
    *
    *   "Additionally, this extension adds a further built-in variable,
    *    gl_DrawID to the shading language. This variable contains the index
    *    of the draw currently being processed by a Multi* variant of a
    *    drawing command (such as MultiDrawElements or
    *    MultiDrawArraysIndirect)."
    *
    * If GL_ARB_multi_draw_indirect is not supported, this is always 0.
    */
   SYSTEM_VALUE_DRAW_ID,
   /*@}*/

   /**
    * \name Geometry shader system values
    */
   /*@{*/
   SYSTEM_VALUE_INVOCATION_ID,  /**< (Also in Tessellation Control shader) */
   /*@}*/

   /**
    * \name Fragment shader system values
    */
   /*@{*/
   SYSTEM_VALUE_FRAG_COORD,
   SYSTEM_VALUE_FRONT_FACE,
   SYSTEM_VALUE_SAMPLE_ID,
   SYSTEM_VALUE_SAMPLE_POS,
   SYSTEM_VALUE_SAMPLE_MASK_IN,
   SYSTEM_VALUE_HELPER_INVOCATION,
   /*@}*/

   /**
    * \name Tessellation Evaluation shader system values
    */
   /*@{*/
   SYSTEM_VALUE_TESS_COORD,
   SYSTEM_VALUE_VERTICES_IN,    /**< Tessellation vertices in input patch */
   SYSTEM_VALUE_PRIMITIVE_ID,
   SYSTEM_VALUE_TESS_LEVEL_OUTER, /**< TES input */
   SYSTEM_VALUE_TESS_LEVEL_INNER, /**< TES input */
   /*@}*/

   /**
    * \name Compute shader system values
    */
   /*@{*/
   SYSTEM_VALUE_LOCAL_INVOCATION_ID,
   SYSTEM_VALUE_WORK_GROUP_ID,
   SYSTEM_VALUE_NUM_WORK_GROUPS,
   /*@}*/

   /**
    * Driver internal vertex-count, used (for example) for drivers to
    * calculate stride for stream-out outputs.  Not externally visible.
    */
   SYSTEM_VALUE_VERTEX_CNT,

   SYSTEM_VALUE_MAX             /**< Number of values */
} gl_system_value;

const char *gl_system_value_name(gl_system_value sysval);

/**
 * The possible interpolation qualifiers that can be applied to a fragment
 * shader input in GLSL.
 *
 * Note: INTERP_QUALIFIER_NONE must be 0 so that memsetting the
 * gl_fragment_program data structure to 0 causes the default behavior.
 */
enum glsl_interp_qualifier
{
   INTERP_QUALIFIER_NONE = 0,
   INTERP_QUALIFIER_SMOOTH,
   INTERP_QUALIFIER_FLAT,
   INTERP_QUALIFIER_NOPERSPECTIVE,
   INTERP_QUALIFIER_COUNT /**< Number of interpolation qualifiers */
};

const char *glsl_interp_qualifier_name(enum glsl_interp_qualifier qual);

/**
 * Fragment program results
 */
typedef enum
{
   FRAG_RESULT_DEPTH = 0,
   FRAG_RESULT_STENCIL = 1,
   /* If a single color should be written to all render targets, this
    * register is written.  No FRAG_RESULT_DATAn will be written.
    */
   FRAG_RESULT_COLOR = 2,
   FRAG_RESULT_SAMPLE_MASK = 3,

   /* FRAG_RESULT_DATAn are the per-render-target (GLSL gl_FragData[n]
    * or ARB_fragment_program fragment.color[n]) color results.  If
    * any are written, FRAG_RESULT_COLOR will not be written.
    * FRAG_RESULT_DATA1 and up are simply for the benefit of
    * gl_frag_result_name() and not to be construed as an upper bound
    */
   FRAG_RESULT_DATA0 = 4,
   FRAG_RESULT_DATA1,
   FRAG_RESULT_DATA2,
   FRAG_RESULT_DATA3,
   FRAG_RESULT_DATA4,
   FRAG_RESULT_DATA5,
   FRAG_RESULT_DATA6,
   FRAG_RESULT_DATA7,
} gl_frag_result;

const char *gl_frag_result_name(gl_frag_result result);

#define FRAG_RESULT_MAX		(FRAG_RESULT_DATA0 + MAX_DRAW_BUFFERS)

/**
 * \brief Layout qualifiers for gl_FragDepth.
 *
 * Extension AMD_conservative_depth allows gl_FragDepth to be redeclared with
 * a layout qualifier.
 *
 * \see enum ir_depth_layout
 */
enum gl_frag_depth_layout
{
   FRAG_DEPTH_LAYOUT_NONE, /**< No layout is specified. */
   FRAG_DEPTH_LAYOUT_ANY,
   FRAG_DEPTH_LAYOUT_GREATER,
   FRAG_DEPTH_LAYOUT_LESS,
   FRAG_DEPTH_LAYOUT_UNCHANGED
};

/**
 * \brief Buffer access qualifiers
 */
enum gl_buffer_access_qualifier
{
   ACCESS_COHERENT = 1,
   ACCESS_RESTRICT = 2,
   ACCESS_VOLATILE = 4,
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SHADER_ENUMS_H */
