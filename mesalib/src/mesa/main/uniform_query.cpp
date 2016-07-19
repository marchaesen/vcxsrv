/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2004-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009-2010  VMware, Inc.  All Rights Reserved.
 * Copyright © 2010, 2011 Intel Corporation
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

#include <stdlib.h>

#include "main/core.h"
#include "main/context.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"
#include "main/uniforms.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/ir_uniform.h"
#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/program.h"
#include "program/hash_table.h"
#include "util/bitscan.h"


extern "C" void GLAPIENTRY
_mesa_GetActiveUniform(GLuint program, GLuint index,
                       GLsizei maxLength, GLsizei *length, GLint *size,
                       GLenum *type, GLcharARB *nameOut)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg;
   struct gl_program_resource *res;

   if (maxLength < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveUniform(maxLength < 0)");
      return;
   }

   shProg = _mesa_lookup_shader_program_err(ctx, program, "glGetActiveUniform");
   if (!shProg)
      return;

   res = _mesa_program_resource_find_index((struct gl_shader_program *) shProg,
                                           GL_UNIFORM, index);

   if (!res) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveUniform(index)");
      return;
   }

   if (nameOut)
      _mesa_get_program_resource_name(shProg, GL_UNIFORM, index, maxLength,
                                      length, nameOut, "glGetActiveUniform");
   if (type)
      _mesa_program_resource_prop((struct gl_shader_program *) shProg,
                                  res, index, GL_TYPE, (GLint*) type,
                                  "glGetActiveUniform");
   if (size)
      _mesa_program_resource_prop((struct gl_shader_program *) shProg,
                                  res, index, GL_ARRAY_SIZE, (GLint*) size,
                                  "glGetActiveUniform");
}

static GLenum
resource_prop_from_uniform_prop(GLenum uni_prop)
{
   switch (uni_prop) {
   case GL_UNIFORM_TYPE:
      return GL_TYPE;
   case GL_UNIFORM_SIZE:
      return GL_ARRAY_SIZE;
   case GL_UNIFORM_NAME_LENGTH:
      return GL_NAME_LENGTH;
   case GL_UNIFORM_BLOCK_INDEX:
      return GL_BLOCK_INDEX;
   case GL_UNIFORM_OFFSET:
      return GL_OFFSET;
   case GL_UNIFORM_ARRAY_STRIDE:
      return GL_ARRAY_STRIDE;
   case GL_UNIFORM_MATRIX_STRIDE:
      return GL_MATRIX_STRIDE;
   case GL_UNIFORM_IS_ROW_MAJOR:
      return GL_IS_ROW_MAJOR;
   case GL_UNIFORM_ATOMIC_COUNTER_BUFFER_INDEX:
      return GL_ATOMIC_COUNTER_BUFFER_INDEX;
   default:
      return 0;
   }
}

extern "C" void GLAPIENTRY
_mesa_GetActiveUniformsiv(GLuint program,
			  GLsizei uniformCount,
			  const GLuint *uniformIndices,
			  GLenum pname,
			  GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader_program *shProg;
   struct gl_program_resource *res;
   GLenum res_prop;

   if (uniformCount < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
		  "glGetActiveUniformsiv(uniformCount < 0)");
      return;
   }

   shProg = _mesa_lookup_shader_program_err(ctx, program, "glGetActiveUniform");
   if (!shProg)
      return;

   res_prop = resource_prop_from_uniform_prop(pname);

   /* We need to first verify that each entry exists as active uniform. If
    * not, generate error and do not cause any other side effects.
    *
    * In the case of and error condition, Page 16 (section 2.3.1 Errors)
    * of the OpenGL 4.5 spec says:
    *
    *     "If the generating command modifies values through a pointer argu-
    *     ment, no change is made to these values."
    */
   for (int i = 0; i < uniformCount; i++) {
      if (!_mesa_program_resource_find_index(shProg, GL_UNIFORM,
                                              uniformIndices[i])) {
         _mesa_error(ctx, GL_INVALID_VALUE, "glGetActiveUniformsiv(index)");
         return;
      }
   }

   for (int i = 0; i < uniformCount; i++) {
      res = _mesa_program_resource_find_index(shProg, GL_UNIFORM,
                                              uniformIndices[i]);
      if (!_mesa_program_resource_prop(shProg, res, uniformIndices[i],
                                       res_prop, &params[i],
                                       "glGetActiveUniformsiv"))
         break;
   }
}

static struct gl_uniform_storage *
validate_uniform_parameters(struct gl_context *ctx,
			    struct gl_shader_program *shProg,
			    GLint location, GLsizei count,
			    unsigned *array_index,
			    const char *caller)
{
   if (shProg == NULL) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(program not linked)", caller);
      return NULL;
   }

   /* From page 12 (page 26 of the PDF) of the OpenGL 2.1 spec:
    *
    *     "If a negative number is provided where an argument of type sizei or
    *     sizeiptr is specified, the error INVALID_VALUE is generated."
    */
   if (count < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(count < 0)", caller);
      return NULL;
   }

   /* Check that the given location is in bounds of uniform remap table.
    * Unlinked programs will have NumUniformRemapTable == 0, so we can take
    * the shProg->LinkStatus check out of the main path.
    */
   if (unlikely(location >= (GLint) shProg->NumUniformRemapTable)) {
      if (!shProg->LinkStatus)
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(program not linked)",
                     caller);
      else
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(location=%d)",
                     caller, location);

      return NULL;
   }

   if (location == -1) {
      if (!shProg->LinkStatus)
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(program not linked)",
                     caller);

      return NULL;
   }

   /* Page 82 (page 96 of the PDF) of the OpenGL 2.1 spec says:
    *
    *     "If any of the following conditions occur, an INVALID_OPERATION
    *     error is generated by the Uniform* commands, and no uniform values
    *     are changed:
    *
    *     ...
    *
    *         - if no variable with a location of location exists in the
    *           program object currently in use and location is not -1,
    *         - if count is greater than one, and the uniform declared in the
    *           shader is not an array variable,
    */
   if (location < -1 || !shProg->UniformRemapTable[location]) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(location=%d)",
                  caller, location);
      return NULL;
   }

   /* If the driver storage pointer in remap table is -1, we ignore silently.
    *
    * GL_ARB_explicit_uniform_location spec says:
    *     "What happens if Uniform* is called with an explicitly defined
    *     uniform location, but that uniform is deemed inactive by the
    *     linker?
    *
    *     RESOLVED: The call is ignored for inactive uniform variables and
    *     no error is generated."
    *
    */
   if (shProg->UniformRemapTable[location] ==
       INACTIVE_UNIFORM_EXPLICIT_LOCATION)
      return NULL;

   struct gl_uniform_storage *const uni = shProg->UniformRemapTable[location];

   /* Even though no location is assigned to a built-in uniform and this
    * function should already have returned NULL, this test makes it explicit
    * that we are not allowing to update the value of a built-in.
    */
   if (uni->builtin)
      return NULL;

   if (uni->array_elements == 0) {
      if (count > 1) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(count = %u for non-array \"%s\"@%d)",
                     caller, count, uni->name, location);
         return NULL;
      }

      assert((location - uni->remap_location) == 0);
      *array_index = 0;
   } else {
      /* The array index specified by the uniform location is just the uniform
       * location minus the base location of of the uniform.
       */
      *array_index = location - uni->remap_location;

      /* If the uniform is an array, check that array_index is in bounds.
       * array_index is unsigned so no need to check for less than zero.
       */
      if (*array_index >= uni->array_elements) {
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(location=%d)",
                     caller, location);
         return NULL;
      }
   }
   return uni;
}

/**
 * Called via glGetUniform[fiui]v() to get the current value of a uniform.
 */
extern "C" void
_mesa_get_uniform(struct gl_context *ctx, GLuint program, GLint location,
		  GLsizei bufSize, enum glsl_base_type returnType,
		  GLvoid *paramsOut)
{
   struct gl_shader_program *shProg =
      _mesa_lookup_shader_program_err(ctx, program, "glGetUniformfv");
   unsigned offset;

   struct gl_uniform_storage *const uni =
      validate_uniform_parameters(ctx, shProg, location, 1,
                                  &offset, "glGetUniform");
   if (uni == NULL) {
      /* For glGetUniform, page 264 (page 278 of the PDF) of the OpenGL 2.1
       * spec says:
       *
       *     "The error INVALID_OPERATION is generated if program has not been
       *     linked successfully, or if location is not a valid location for
       *     program."
       *
       * For glUniform, page 82 (page 96 of the PDF) of the OpenGL 2.1 spec
       * says:
       *
       *     "If the value of location is -1, the Uniform* commands will
       *     silently ignore the data passed in, and the current uniform
       *     values will not be changed."
       *
       * Allowing -1 for the location parameter of glUniform allows
       * applications to avoid error paths in the case that, for example, some
       * uniform variable is removed by the compiler / linker after
       * optimization.  In this case, the new value of the uniform is dropped
       * on the floor.  For the case of glGetUniform, there is nothing
       * sensible to do for a location of -1.
       *
       * If the location was -1, validate_unfirom_parameters will return NULL
       * without raising an error.  Raise the error here.
       */
      if (location == -1) {
         _mesa_error(ctx, GL_INVALID_OPERATION, "glGetUniform(location=%d)",
                     location);
      }

      return;
   }

   {
      unsigned elements = (uni->type->is_sampler())
	 ? 1 : uni->type->components();
      const int dmul = uni->type->is_64bit() ? 2 : 1;
      const int rmul = glsl_base_type_is_64bit(returnType) ? 2 : 1;

      /* Calculate the source base address *BEFORE* modifying elements to
       * account for the size of the user's buffer.
       */
      const union gl_constant_value *const src =
	 &uni->storage[offset * elements * dmul];

      assert(returnType == GLSL_TYPE_FLOAT || returnType == GLSL_TYPE_INT ||
             returnType == GLSL_TYPE_UINT || returnType == GLSL_TYPE_DOUBLE);

      /* doubles have a different size than the other 3 types */
      unsigned bytes = sizeof(src[0]) * elements * rmul;
      if (bufSize < 0 || bytes > (unsigned) bufSize) {
	 _mesa_error( ctx, GL_INVALID_OPERATION,
	             "glGetnUniform*vARB(out of bounds: bufSize is %d,"
	             " but %u bytes are required)", bufSize, bytes );
	 return;
      }

      /* If the return type and the uniform's native type are "compatible,"
       * just memcpy the data.  If the types are not compatible, perform a
       * slower convert-and-copy process.
       */
      if (returnType == uni->type->base_type
	  || ((returnType == GLSL_TYPE_INT
	       || returnType == GLSL_TYPE_UINT)
	      &&
	      (uni->type->base_type == GLSL_TYPE_INT
	       || uni->type->base_type == GLSL_TYPE_UINT
               || uni->type->base_type == GLSL_TYPE_SAMPLER
               || uni->type->base_type == GLSL_TYPE_IMAGE))) {
	 memcpy(paramsOut, src, bytes);
      } else {
	 union gl_constant_value *const dst =
	    (union gl_constant_value *) paramsOut;
	 /* This code could be optimized by putting the loop inside the switch
	  * statements.  However, this is not expected to be
	  * performance-critical code.
	  */
	 for (unsigned i = 0; i < elements; i++) {
	   int sidx = i * dmul;
	   int didx = i * rmul;

	    switch (returnType) {
	    case GLSL_TYPE_FLOAT:
	       switch (uni->type->base_type) {
	       case GLSL_TYPE_UINT:
		  dst[didx].f = (float) src[sidx].u;
		  break;
	       case GLSL_TYPE_INT:
	       case GLSL_TYPE_SAMPLER:
               case GLSL_TYPE_IMAGE:
		  dst[didx].f = (float) src[sidx].i;
		  break;
	       case GLSL_TYPE_BOOL:
		  dst[didx].f = src[sidx].i ? 1.0f : 0.0f;
		  break;
	       case GLSL_TYPE_DOUBLE:
		  dst[didx].f = *(double *)&src[sidx].f;
		  break;
	       default:
		  assert(!"Should not get here.");
		  break;
	       }
	       break;
	    case GLSL_TYPE_DOUBLE:
	       switch (uni->type->base_type) {
	       case GLSL_TYPE_UINT:
		  *(double *)&dst[didx].f = (double) src[sidx].u;
		  break;
	       case GLSL_TYPE_INT:
	       case GLSL_TYPE_SAMPLER:
	       case GLSL_TYPE_IMAGE:
		  *(double *)&dst[didx].f = (double) src[sidx].i;
		  break;
	       case GLSL_TYPE_BOOL:
		  *(double *)&dst[didx].f = src[sidx].i ? 1.0f : 0.0f;
		  break;
	       case GLSL_TYPE_FLOAT:
		  *(double *)&dst[didx].f = (double) src[sidx].f;
		  break;
	       default:
		  assert(!"Should not get here.");
		  break;
	       }
	       break;
	    case GLSL_TYPE_INT:
	    case GLSL_TYPE_UINT:
	       switch (uni->type->base_type) {
	       case GLSL_TYPE_FLOAT:
		  /* While the GL 3.2 core spec doesn't explicitly
		   * state how conversion of float uniforms to integer
		   * values works, in section 6.2 "State Tables" on
		   * page 267 it says:
		   *
		   *     "Unless otherwise specified, when floating
		   *      point state is returned as integer values or
		   *      integer state is returned as floating-point
		   *      values it is converted in the fashion
		   *      described in section 6.1.2"
		   *
		   * That section, on page 248, says:
		   *
		   *     "If GetIntegerv or GetInteger64v are called,
		   *      a floating-point value is rounded to the
		   *      nearest integer..."
		   */
		  dst[didx].i = IROUND(src[sidx].f);
		  break;
	       case GLSL_TYPE_BOOL:
		  dst[didx].i = src[sidx].i ? 1 : 0;
		  break;
	       case GLSL_TYPE_DOUBLE:
		  dst[didx].i = IROUNDD(*(double *)&src[sidx].f);
		  break;
	       default:
		  assert(!"Should not get here.");
		  break;
	       }
	       break;

	    default:
	       assert(!"Should not get here.");
	       break;
	    }
	 }
      }
   }
}

static void
log_uniform(const void *values, enum glsl_base_type basicType,
	    unsigned rows, unsigned cols, unsigned count,
	    bool transpose,
	    const struct gl_shader_program *shProg,
	    GLint location,
	    const struct gl_uniform_storage *uni)
{

   const union gl_constant_value *v = (const union gl_constant_value *) values;
   const unsigned elems = rows * cols * count;
   const char *const extra = (cols == 1) ? "uniform" : "uniform matrix";

   printf("Mesa: set program %u %s \"%s\" (loc %d, type \"%s\", "
	  "transpose = %s) to: ",
	  shProg->Name, extra, uni->name, location, uni->type->name,
	  transpose ? "true" : "false");
   for (unsigned i = 0; i < elems; i++) {
      if (i != 0 && ((i % rows) == 0))
	 printf(", ");

      switch (basicType) {
      case GLSL_TYPE_UINT:
	 printf("%u ", v[i].u);
	 break;
      case GLSL_TYPE_INT:
	 printf("%d ", v[i].i);
	 break;
      case GLSL_TYPE_FLOAT:
	 printf("%g ", v[i].f);
	 break;
      case GLSL_TYPE_DOUBLE:
         printf("%g ", *(double* )&v[i * 2].f);
         break;
      default:
	 assert(!"Should not get here.");
	 break;
      }
   }
   printf("\n");
   fflush(stdout);
}

#if 0
static void
log_program_parameters(const struct gl_shader_program *shProg)
{
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (shProg->_LinkedShaders[i] == NULL)
	 continue;

      const struct gl_program *const prog = shProg->_LinkedShaders[i]->Program;

      printf("Program %d %s shader parameters:\n",
             shProg->Name, _mesa_shader_stage_to_string(i));
      for (unsigned j = 0; j < prog->Parameters->NumParameters; j++) {
	 printf("%s: %p %f %f %f %f\n",
		prog->Parameters->Parameters[j].Name,
		prog->Parameters->ParameterValues[j],
		prog->Parameters->ParameterValues[j][0].f,
		prog->Parameters->ParameterValues[j][1].f,
		prog->Parameters->ParameterValues[j][2].f,
		prog->Parameters->ParameterValues[j][3].f);
      }
   }
   fflush(stdout);
}
#endif

/**
 * Propagate some values from uniform backing storage to driver storage
 *
 * Values propagated from uniform backing storage to driver storage
 * have all format / type conversions previously requested by the
 * driver applied.  This function is most often called by the
 * implementations of \c glUniform1f, etc. and \c glUniformMatrix2f,
 * etc.
 *
 * \param uni          Uniform whose data is to be propagated to driver storage
 * \param array_index  If \c uni is an array, this is the element of
 *                     the array to be propagated.
 * \param count        Number of array elements to propagate.
 */
extern "C" void
_mesa_propagate_uniforms_to_driver_storage(struct gl_uniform_storage *uni,
					   unsigned array_index,
					   unsigned count)
{
   unsigned i;

   /* vector_elements and matrix_columns can be 0 for samplers.
    */
   const unsigned components = MAX2(1, uni->type->vector_elements);
   const unsigned vectors = MAX2(1, uni->type->matrix_columns);
   const int dmul = uni->type->is_64bit() ? 2 : 1;

   /* Store the data in the driver's requested type in the driver's storage
    * areas.
    */
   unsigned src_vector_byte_stride = components * 4 * dmul;

   for (i = 0; i < uni->num_driver_storage; i++) {
      struct gl_uniform_driver_storage *const store = &uni->driver_storage[i];
      uint8_t *dst = (uint8_t *) store->data;
      const unsigned extra_stride =
	 store->element_stride - (vectors * store->vector_stride);
      const uint8_t *src =
	 (uint8_t *) (&uni->storage[array_index * (dmul * components * vectors)].i);

#if 0
      printf("%s: %p[%d] components=%u vectors=%u count=%u vector_stride=%u "
	     "extra_stride=%u\n",
	     __func__, dst, array_index, components,
	     vectors, count, store->vector_stride, extra_stride);
#endif

      dst += array_index * store->element_stride;

      switch (store->format) {
      case uniform_native: {
	 unsigned j;
	 unsigned v;

	 for (j = 0; j < count; j++) {
	    for (v = 0; v < vectors; v++) {
	       memcpy(dst, src, src_vector_byte_stride);
	       src += src_vector_byte_stride;
	       dst += store->vector_stride;
	    }

	    dst += extra_stride;
	 }
	 break;
      }

      case uniform_int_float: {
	 const int *isrc = (const int *) src;
	 unsigned j;
	 unsigned v;
	 unsigned c;

	 for (j = 0; j < count; j++) {
	    for (v = 0; v < vectors; v++) {
	       for (c = 0; c < components; c++) {
		  ((float *) dst)[c] = (float) *isrc;
		  isrc++;
	       }

	       dst += store->vector_stride;
	    }

	    dst += extra_stride;
	 }
	 break;
      }

      default:
	 assert(!"Should not get here.");
	 break;
      }
   }
}


/**
 * Return printable string for a given GLSL_TYPE_x
 */
static const char *
glsl_type_name(enum glsl_base_type type)
{
   switch (type) {
   case GLSL_TYPE_UINT:
      return "uint";
   case GLSL_TYPE_INT:
      return "int";
   case GLSL_TYPE_FLOAT:
      return "float";
   case GLSL_TYPE_DOUBLE:
      return "double";
   case GLSL_TYPE_BOOL:
      return "bool";
   case GLSL_TYPE_SAMPLER:
      return "sampler";
   case GLSL_TYPE_IMAGE:
      return "image";
   case GLSL_TYPE_ATOMIC_UINT:
      return "atomic_uint";
   case GLSL_TYPE_STRUCT:
      return "struct";
   case GLSL_TYPE_INTERFACE:
      return "interface";
   case GLSL_TYPE_ARRAY:
      return "array";
   case GLSL_TYPE_VOID:
      return "void";
   case GLSL_TYPE_ERROR:
      return "error";
   default:
      return "other";
   }
}


/**
 * Called via glUniform*() functions.
 */
extern "C" void
_mesa_uniform(struct gl_context *ctx, struct gl_shader_program *shProg,
	      GLint location, GLsizei count,
              const GLvoid *values,
              enum glsl_base_type basicType,
              unsigned src_components)
{
   unsigned offset;
   int size_mul = glsl_base_type_is_64bit(basicType) ? 2 : 1;

   struct gl_uniform_storage *const uni =
      validate_uniform_parameters(ctx, shProg, location, count,
                                  &offset, "glUniform");
   if (uni == NULL)
      return;

   if (uni->type->is_matrix()) {
      /* Can't set matrix uniforms (like mat4) with glUniform */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glUniform%u(uniform \"%s\"@%d is matrix)",
                  src_components, uni->name, location);
      return;
   }

   /* Verify that the types are compatible.
    */
   const unsigned components = uni->type->is_sampler()
      ? 1 : uni->type->vector_elements;

   if (components != src_components) {
      /* glUniformN() must match float/vecN type */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glUniform%u(\"%s\"@%u has %u components, not %u)",
                  src_components, uni->name, location,
                  components, src_components);
      return;
   }

   bool match;
   switch (uni->type->base_type) {
   case GLSL_TYPE_BOOL:
      match = (basicType != GLSL_TYPE_DOUBLE);
      break;
   case GLSL_TYPE_SAMPLER:
      match = (basicType == GLSL_TYPE_INT);
      break;
   case GLSL_TYPE_IMAGE:
      match = (basicType == GLSL_TYPE_INT && _mesa_is_desktop_gl(ctx));
      break;
   default:
      match = (basicType == uni->type->base_type);
      break;
   }

   if (!match) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glUniform%u(\"%s\"@%d is %s, not %s)",
                  src_components, uni->name, location,
                  glsl_type_name(uni->type->base_type),
                  glsl_type_name(basicType));
      return;
   }

   if (unlikely(ctx->_Shader->Flags & GLSL_UNIFORMS)) {
      log_uniform(values, basicType, components, 1, count,
		  false, shProg, location, uni);
   }

   /* Page 100 (page 116 of the PDF) of the OpenGL 3.0 spec says:
    *
    *     "Setting a sampler's value to i selects texture image unit number
    *     i. The values of i range from zero to the implementation- dependent
    *     maximum supported number of texture image units."
    *
    * In addition, table 2.3, "Summary of GL errors," on page 17 (page 33 of
    * the PDF) says:
    *
    *     "Error         Description                    Offending command
    *                                                   ignored?
    *     ...
    *     INVALID_VALUE  Numeric argument out of range  Yes"
    *
    * Based on that, when an invalid sampler is specified, we generate a
    * GL_INVALID_VALUE error and ignore the command.
    */
   if (uni->type->is_sampler()) {
      for (int i = 0; i < count; i++) {
	 const unsigned texUnit = ((unsigned *) values)[i];

         /* check that the sampler (tex unit index) is legal */
         if (texUnit >= ctx->Const.MaxCombinedTextureImageUnits) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glUniform1i(invalid sampler/tex unit index for "
			"uniform %d)",
                        location);
            return;
         }
      }
      /* We need to reset the validate flag on changes to samplers in case
       * two different sampler types are set to the same texture unit.
       */
      ctx->_Shader->Validated = GL_FALSE;
   }

   if (uni->type->is_image()) {
      for (int i = 0; i < count; i++) {
         const int unit = ((GLint *) values)[i];

         /* check that the image unit is legal */
         if (unit < 0 || unit >= (int)ctx->Const.MaxImageUnits) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glUniform1i(invalid image unit index for uniform %d)",
                        location);
            return;
         }
      }
   }

   /* Page 82 (page 96 of the PDF) of the OpenGL 2.1 spec says:
    *
    *     "When loading N elements starting at an arbitrary position k in a
    *     uniform declared as an array, elements k through k + N - 1 in the
    *     array will be replaced with the new values. Values for any array
    *     element that exceeds the highest array element index used, as
    *     reported by GetActiveUniform, will be ignored by the GL."
    *
    * Clamp 'count' to a valid value.  Note that for non-arrays a count > 1
    * will have already generated an error.
    */
   if (uni->array_elements != 0) {
      count = MIN2(count, (int) (uni->array_elements - offset));
   }

   FLUSH_VERTICES(ctx, _NEW_PROGRAM_CONSTANTS);

   /* Store the data in the "actual type" backing storage for the uniform.
    */
   if (!uni->type->is_boolean()) {
      memcpy(&uni->storage[size_mul * components * offset], values,
	     sizeof(uni->storage[0]) * components * count * size_mul);
   } else {
      const union gl_constant_value *src =
	 (const union gl_constant_value *) values;
      union gl_constant_value *dst = &uni->storage[components * offset];
      const unsigned elems = components * count;

      for (unsigned i = 0; i < elems; i++) {
	 if (basicType == GLSL_TYPE_FLOAT) {
            dst[i].i = src[i].f != 0.0f ? ctx->Const.UniformBooleanTrue : 0;
	 } else {
            dst[i].i = src[i].i != 0    ? ctx->Const.UniformBooleanTrue : 0;
	 }
      }
   }

   _mesa_propagate_uniforms_to_driver_storage(uni, offset, count);

   /* If the uniform is a sampler, do the extra magic necessary to propagate
    * the changes through.
    */
   if (uni->type->is_sampler()) {
      bool flushed = false;
      for (int i = 0; i < MESA_SHADER_STAGES; i++) {
	 struct gl_linked_shader *const sh = shProg->_LinkedShaders[i];

	 /* If the shader stage doesn't use the sampler uniform, skip this.
	  */
	 if (sh == NULL || !uni->opaque[i].active)
	    continue;

         for (int j = 0; j < count; j++) {
            sh->SamplerUnits[uni->opaque[i].index + offset + j] =
               ((unsigned *) values)[j];
         }

	 struct gl_program *const prog = sh->Program;

	 assert(sizeof(prog->SamplerUnits) == sizeof(sh->SamplerUnits));

	 /* Determine if any of the samplers used by this shader stage have
	  * been modified.
	  */
	 bool changed = false;
	 GLbitfield mask = sh->active_samplers;
	 while (mask) {
	    const int j = u_bit_scan(&mask);
	    if (prog->SamplerUnits[j] != sh->SamplerUnits[j]) {
	       changed = true;
	       break;
	    }
	 }

	 if (changed) {
	    if (!flushed) {
	       FLUSH_VERTICES(ctx, _NEW_TEXTURE | _NEW_PROGRAM);
	       flushed = true;
	    }

	    _mesa_update_shader_textures_used(shProg, prog);
            if (ctx->Driver.SamplerUniformChange)
	       ctx->Driver.SamplerUniformChange(ctx, prog->Target, prog);
	 }
      }
   }

   /* If the uniform is an image, update the mapping from image
    * uniforms to image units present in the shader data structure.
    */
   if (uni->type->is_image()) {
      for (int i = 0; i < MESA_SHADER_STAGES; i++) {
	 if (uni->opaque[i].active) {
            struct gl_linked_shader *sh = shProg->_LinkedShaders[i];

            for (int j = 0; j < count; j++)
               sh->ImageUnits[uni->opaque[i].index + offset + j] =
                  ((GLint *) values)[j];
         }
      }

      ctx->NewDriverState |= ctx->DriverFlags.NewImageUnits;
   }
}

/**
 * Called by glUniformMatrix*() functions.
 * Note: cols=2, rows=4  ==>  array[2] of vec4
 */
extern "C" void
_mesa_uniform_matrix(struct gl_context *ctx, struct gl_shader_program *shProg,
		     GLuint cols, GLuint rows,
                     GLint location, GLsizei count,
                     GLboolean transpose,
                     const GLvoid *values, enum glsl_base_type basicType)
{
   unsigned offset;
   unsigned vectors;
   unsigned components;
   unsigned elements;
   int size_mul;
   struct gl_uniform_storage *const uni =
      validate_uniform_parameters(ctx, shProg, location, count,
                                  &offset, "glUniformMatrix");
   if (uni == NULL)
      return;

   if (!uni->type->is_matrix()) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
		  "glUniformMatrix(non-matrix uniform)");
      return;
   }

   assert(basicType == GLSL_TYPE_FLOAT || basicType == GLSL_TYPE_DOUBLE);
   size_mul = basicType == GLSL_TYPE_DOUBLE ? 2 : 1;

   assert(!uni->type->is_sampler());
   vectors = uni->type->matrix_columns;
   components = uni->type->vector_elements;

   /* Verify that the types are compatible.  This is greatly simplified for
    * matrices because they can only have a float base type.
    */
   if (vectors != cols || components != rows) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
		  "glUniformMatrix(matrix size mismatch)");
      return;
   }

   /* GL_INVALID_VALUE is generated if `transpose' is not GL_FALSE.
    * http://www.khronos.org/opengles/sdk/docs/man/xhtml/glUniform.xml
    */
   if (transpose) {
      if (ctx->API == API_OPENGLES2 && ctx->Version < 30) {
	 _mesa_error(ctx, GL_INVALID_VALUE,
		     "glUniformMatrix(matrix transpose is not GL_FALSE)");
	 return;
      }
   }

   /* Section 2.11.7 (Uniform Variables) of the OpenGL 4.2 Core Profile spec
    * says:
    *
    *     "If any of the following conditions occur, an INVALID_OPERATION
    *     error is generated by the Uniform* commands, and no uniform values
    *     are changed:
    *
    *     ...
    *
    *     - if the uniform declared in the shader is not of type boolean and
    *       the type indicated in the name of the Uniform* command used does
    *       not match the type of the uniform"
    *
    * There are no Boolean matrix types, so we do not need to allow
    * GLSL_TYPE_BOOL here (as _mesa_uniform does).
    */
   if (uni->type->base_type != basicType) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glUniformMatrix%ux%u(\"%s\"@%d is %s, not %s)",
                  cols, rows, uni->name, location,
                  glsl_type_name(uni->type->base_type),
                  glsl_type_name(basicType));
      return;
   }

   if (unlikely(ctx->_Shader->Flags & GLSL_UNIFORMS)) {
      log_uniform(values, uni->type->base_type, components, vectors, count,
		  bool(transpose), shProg, location, uni);
   }

   /* Page 82 (page 96 of the PDF) of the OpenGL 2.1 spec says:
    *
    *     "When loading N elements starting at an arbitrary position k in a
    *     uniform declared as an array, elements k through k + N - 1 in the
    *     array will be replaced with the new values. Values for any array
    *     element that exceeds the highest array element index used, as
    *     reported by GetActiveUniform, will be ignored by the GL."
    *
    * Clamp 'count' to a valid value.  Note that for non-arrays a count > 1
    * will have already generated an error.
    */
   if (uni->array_elements != 0) {
      count = MIN2(count, (int) (uni->array_elements - offset));
   }

   FLUSH_VERTICES(ctx, _NEW_PROGRAM_CONSTANTS);

   /* Store the data in the "actual type" backing storage for the uniform.
    */
   elements = components * vectors;

   if (!transpose) {
      memcpy(&uni->storage[size_mul * elements * offset], values,
	     sizeof(uni->storage[0]) * elements * count * size_mul);
   } else if (basicType == GLSL_TYPE_FLOAT) {
      /* Copy and transpose the matrix.
       */
      const float *src = (const float *)values;
      float *dst = &uni->storage[elements * offset].f;

      for (int i = 0; i < count; i++) {
	 for (unsigned r = 0; r < rows; r++) {
	    for (unsigned c = 0; c < cols; c++) {
	       dst[(c * components) + r] = src[c + (r * vectors)];
	    }
	 }

	 dst += elements;
	 src += elements;
      }
   } else {
      assert(basicType == GLSL_TYPE_DOUBLE);
      const double *src = (const double *)values;
      double *dst = (double *)&uni->storage[elements * offset].f;

      for (int i = 0; i < count; i++) {
	 for (unsigned r = 0; r < rows; r++) {
	    for (unsigned c = 0; c < cols; c++) {
	       dst[(c * components) + r] = src[c + (r * vectors)];
	    }
	 }

	 dst += elements;
	 src += elements;
      }
   }

   _mesa_propagate_uniforms_to_driver_storage(uni, offset, count);
}


extern "C" bool
_mesa_sampler_uniforms_are_valid(const struct gl_shader_program *shProg,
				 char *errMsg, size_t errMsgLength)
{
   /* Shader does not have samplers. */
   if (shProg->NumUniformStorage == 0)
      return true;

   if (!shProg->SamplersValidated) {
      _mesa_snprintf(errMsg, errMsgLength,
                     "active samplers with a different type "
                     "refer to the same texture image unit");
      return false;
   }
   return true;
}

extern "C" bool
_mesa_sampler_uniforms_pipeline_are_valid(struct gl_pipeline_object *pipeline)
{
   /* Section 2.11.11 (Shader Execution), subheading "Validation," of the
    * OpenGL 4.1 spec says:
    *
    *     "[INVALID_OPERATION] is generated by any command that transfers
    *     vertices to the GL if:
    *
    *         ...
    *
    *         - Any two active samplers in the current program object are of
    *           different types, but refer to the same texture image unit.
    *
    *         - The number of active samplers in the program exceeds the
    *           maximum number of texture image units allowed."
    */

   GLbitfield mask;
   GLbitfield TexturesUsed[MAX_COMBINED_TEXTURE_IMAGE_UNITS];
   struct gl_linked_shader *shader;
   unsigned active_samplers = 0;
   const struct gl_shader_program **shProg =
      (const struct gl_shader_program **) pipeline->CurrentProgram;


   memset(TexturesUsed, 0, sizeof(TexturesUsed));

   for (unsigned idx = 0; idx < ARRAY_SIZE(pipeline->CurrentProgram); idx++) {
      if (!shProg[idx])
         continue;

      shader = shProg[idx]->_LinkedShaders[idx];
      if (!shader || !shader->Program)
         continue;

      mask = shader->Program->SamplersUsed;
      while (mask) {
         const int s = u_bit_scan(&mask);
         GLuint unit = shader->SamplerUnits[s];
         GLuint tgt = shader->SamplerTargets[s];

         /* FIXME: Samplers are initialized to 0 and Mesa doesn't do a
          * great job of eliminating unused uniforms currently so for now
          * don't throw an error if two sampler types both point to 0.
          */
         if (unit == 0)
            continue;

         if (TexturesUsed[unit] & ~(1 << tgt)) {
            pipeline->InfoLog =
               ralloc_asprintf(pipeline,
                     "Program %d: "
                     "Texture unit %d is accessed with 2 different types",
                     shProg[idx]->Name, unit);
            return false;
         }

         TexturesUsed[unit] |= (1 << tgt);
      }

      active_samplers += shader->num_samplers;
   }

   if (active_samplers > MAX_COMBINED_TEXTURE_IMAGE_UNITS) {
      pipeline->InfoLog =
         ralloc_asprintf(pipeline,
                         "the number of active samplers %d exceed the "
                         "maximum %d",
                         active_samplers, MAX_COMBINED_TEXTURE_IMAGE_UNITS);
      return false;
   }

   return true;
}
