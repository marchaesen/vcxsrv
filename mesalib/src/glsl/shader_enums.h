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
   MESA_SHADER_GEOMETRY = 1,
   MESA_SHADER_FRAGMENT = 2,
   MESA_SHADER_COMPUTE = 3,
} gl_shader_stage;

#define MESA_SHADER_STAGES (MESA_SHADER_COMPUTE + 1)

/**
 * Bitflags for system values.
 */
#define SYSTEM_BIT_SAMPLE_ID ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_ID)
#define SYSTEM_BIT_SAMPLE_POS ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_POS)
#define SYSTEM_BIT_SAMPLE_MASK_IN ((uint64_t)1 << SYSTEM_VALUE_SAMPLE_MASK_IN)
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
   /*@}*/

   /**
    * \name Geometry shader system values
    */
   /*@{*/
   SYSTEM_VALUE_INVOCATION_ID,
   /*@}*/

   /**
    * \name Fragment shader system values
    */
   /*@{*/
   SYSTEM_VALUE_FRONT_FACE,     /**< (not done yet) */
   SYSTEM_VALUE_SAMPLE_ID,
   SYSTEM_VALUE_SAMPLE_POS,
   SYSTEM_VALUE_SAMPLE_MASK_IN,
   /*@}*/

   SYSTEM_VALUE_MAX             /**< Number of values */
} gl_system_value;


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


#endif /* SHADER_ENUMS_H */
