/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * Copyright 2009 VMware, Inc.  All Rights Reserved.
 * Copyright © 2010-2011 Intel Corporation
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

#include "main/glheader.h"
#include "main/context.h"

#include "main/macros.h"
#include "main/samplerobj.h"
#include "main/shaderobj.h"
#include "main/state.h"
#include "main/texenvprogram.h"
#include "main/texobj.h"
#include "main/uniforms.h"
#include "compiler/glsl/ir_builder.h"
#include "compiler/glsl/ir_optimization.h"
#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/glsl_symbol_table.h"
#include "compiler/glsl_types.h"
#include "program/ir_to_mesa.h"
#include "program/program.h"
#include "program/programopt.h"
#include "program/prog_cache.h"
#include "program/prog_instruction.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "program/prog_statevars.h"
#include "util/bitscan.h"

using namespace ir_builder;

/*
 * Note on texture units:
 *
 * The number of texture units supported by fixed-function fragment
 * processing is MAX_TEXTURE_COORD_UNITS, not MAX_TEXTURE_IMAGE_UNITS.
 * That's because there's a one-to-one correspondence between texture
 * coordinates and samplers in fixed-function processing.
 *
 * Since fixed-function vertex processing is limited to MAX_TEXTURE_COORD_UNITS
 * sets of texcoords, so is fixed-function fragment processing.
 *
 * We can safely use ctx->Const.MaxTextureUnits for loop bounds.
 */


static GLboolean
texenv_doing_secondary_color(struct gl_context *ctx)
{
   if (ctx->Light.Enabled &&
       (ctx->Light.Model.ColorControl == GL_SEPARATE_SPECULAR_COLOR))
      return GL_TRUE;

   if (ctx->Fog.ColorSumEnabled)
      return GL_TRUE;

   return GL_FALSE;
}

struct state_key {
   GLuint nr_enabled_units:4;
   GLuint separate_specular:1;
   GLuint fog_mode:2;          /**< FOG_x */
   GLuint inputs_available:12;
   GLuint num_draw_buffers:4;

   /* NOTE: This array of structs must be last! (see "keySize" below) */
   struct {
      GLuint enabled:1;
      GLuint source_index:4;   /**< TEXTURE_x_INDEX */
      GLuint shadow:1;

      /***
       * These are taken from struct gl_tex_env_combine_packed
       * @{
       */
      GLuint ModeRGB:4;
      GLuint ModeA:4;
      GLuint ScaleShiftRGB:2;
      GLuint ScaleShiftA:2;
      GLuint NumArgsRGB:3;
      GLuint NumArgsA:3;
      struct gl_tex_env_argument ArgsRGB[MAX_COMBINER_TERMS];
      struct gl_tex_env_argument ArgsA[MAX_COMBINER_TERMS];
      /** @} */
   } unit[MAX_TEXTURE_COORD_UNITS];
};


/**
 * Do we need to clamp the results of the given texture env/combine mode?
 * If the inputs to the mode are in [0,1] we don't always have to clamp
 * the results.
 */
static GLboolean
need_saturate( GLuint mode )
{
   switch (mode) {
   case TEXENV_MODE_REPLACE:
   case TEXENV_MODE_MODULATE:
   case TEXENV_MODE_INTERPOLATE:
      return GL_FALSE;
   case TEXENV_MODE_ADD:
   case TEXENV_MODE_ADD_SIGNED:
   case TEXENV_MODE_SUBTRACT:
   case TEXENV_MODE_DOT3_RGB:
   case TEXENV_MODE_DOT3_RGB_EXT:
   case TEXENV_MODE_DOT3_RGBA:
   case TEXENV_MODE_DOT3_RGBA_EXT:
   case TEXENV_MODE_MODULATE_ADD_ATI:
   case TEXENV_MODE_MODULATE_SIGNED_ADD_ATI:
   case TEXENV_MODE_MODULATE_SUBTRACT_ATI:
   case TEXENV_MODE_ADD_PRODUCTS_NV:
   case TEXENV_MODE_ADD_PRODUCTS_SIGNED_NV:
      return GL_TRUE;
   default:
      assert(0);
      return GL_FALSE;
   }
}

#define VERT_BIT_TEX_ANY    (0xff << VERT_ATTRIB_TEX0)

/**
 * Identify all possible varying inputs.  The fragment program will
 * never reference non-varying inputs, but will track them via state
 * constants instead.
 *
 * This function figures out all the inputs that the fragment program
 * has access to and filters input bitmask.
 */
static GLbitfield filter_fp_input_mask( GLbitfield fp_inputs,
		    struct gl_context *ctx )
{
   if (ctx->VertexProgram._Overriden) {
      /* Somebody's messing with the vertex program and we don't have
       * a clue what's happening.  Assume that it could be producing
       * all possible outputs.
       */
      return fp_inputs;
   }

   if (ctx->RenderMode == GL_FEEDBACK) {
      /* _NEW_RENDERMODE */
      return fp_inputs & (VARYING_BIT_COL0 | VARYING_BIT_TEX0);
   }

   /* _NEW_PROGRAM */
   const GLboolean vertexShader =
         ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX] != NULL;
   const GLboolean vertexProgram = _mesa_arb_vertex_program_enabled(ctx);

   if (!(vertexProgram || vertexShader)) {
      /* Fixed function vertex logic */
      GLbitfield possible_inputs = 0;

      GLbitfield varying_inputs = ctx->VertexProgram._VaryingInputs;
      /* We only update ctx->VertexProgram._VaryingInputs when in VP_MODE_FF _VPMode */
      assert(VP_MODE_FF == ctx->VertexProgram._VPMode);

      /* These get generated in the setup routine regardless of the
       * vertex program:
       */
      /* _NEW_POINT */
      if (ctx->Point.PointSprite) {
         /* All texture varyings are possible to use */
         possible_inputs = VARYING_BITS_TEX_ANY;
      }
      else {
         const GLbitfield possible_tex_inputs =
               ctx->Texture._TexGenEnabled |
               ctx->Texture._TexMatEnabled |
               ((varying_inputs & VERT_BIT_TEX_ANY) >> VERT_ATTRIB_TEX0);

         possible_inputs = (possible_tex_inputs << VARYING_SLOT_TEX0);
      }

      /* First look at what values may be computed by the generated
       * vertex program:
       */
      if (ctx->Light.Enabled) {
         possible_inputs |= VARYING_BIT_COL0;

         if (texenv_doing_secondary_color(ctx))
            possible_inputs |= VARYING_BIT_COL1;
      }

      /* Then look at what might be varying as a result of enabled
       * arrays, etc:
       */
      if (varying_inputs & VERT_BIT_COLOR0)
         possible_inputs |= VARYING_BIT_COL0;
      if (varying_inputs & VERT_BIT_COLOR1)
         possible_inputs |= VARYING_BIT_COL1;

      return fp_inputs & possible_inputs;
   }

   /* calculate from vp->outputs */
   struct gl_program *vprog;

   /* Choose GLSL vertex shader over ARB vertex program.  Need this
    * since vertex shader state validation comes after fragment state
    * validation (see additional comments in state.c).
    */
   if (ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY] != NULL)
      vprog = ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY];
   else if (ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL] != NULL)
      vprog = ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];
   else if (vertexShader)
      vprog = ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX];
   else
      vprog = ctx->VertexProgram.Current;

   GLbitfield possible_inputs = vprog->info.outputs_written;

   /* These get generated in the setup routine regardless of the
    * vertex program:
    */
   /* _NEW_POINT */
   if (ctx->Point.PointSprite) {
      /* All texture varyings are possible to use */
      possible_inputs |= VARYING_BITS_TEX_ANY;
   }

   return fp_inputs & possible_inputs;
}


/**
 * Examine current texture environment state and generate a unique
 * key to identify it.
 */
static GLuint make_state_key( struct gl_context *ctx,  struct state_key *key )
{
   GLbitfield inputs_referenced = VARYING_BIT_COL0;
   GLbitfield mask;
   GLuint keySize;

   memset(key, 0, sizeof(*key));

   /* _NEW_TEXTURE_OBJECT | _NEW_TEXTURE_STATE */
   mask = ctx->Texture._EnabledCoordUnits;
   int i = -1;
   while (mask) {
      i = u_bit_scan(&mask);
      const struct gl_texture_unit *texUnit = &ctx->Texture.Unit[i];
      const struct gl_texture_object *texObj = texUnit->_Current;
      const struct gl_tex_env_combine_packed *comb =
         &ctx->Texture.FixedFuncUnit[i]._CurrentCombinePacked;

      if (!texObj)
         continue;

      key->unit[i].enabled = 1;
      inputs_referenced |= VARYING_BIT_TEX(i);

      key->unit[i].source_index = texObj->TargetIndex;

      const struct gl_sampler_object *samp = _mesa_get_samplerobj(ctx, i);
      if (samp->Attrib.CompareMode == GL_COMPARE_R_TO_TEXTURE) {
         const GLenum format = _mesa_texture_base_format(texObj);
         key->unit[i].shadow = (format == GL_DEPTH_COMPONENT ||
				format == GL_DEPTH_STENCIL_EXT);
      }

      key->unit[i].ModeRGB = comb->ModeRGB;
      key->unit[i].ModeA = comb->ModeA;
      key->unit[i].ScaleShiftRGB = comb->ScaleShiftRGB;
      key->unit[i].ScaleShiftA = comb->ScaleShiftA;
      key->unit[i].NumArgsRGB = comb->NumArgsRGB;
      key->unit[i].NumArgsA = comb->NumArgsA;

      memcpy(key->unit[i].ArgsRGB, comb->ArgsRGB, sizeof comb->ArgsRGB);
      memcpy(key->unit[i].ArgsA, comb->ArgsA, sizeof comb->ArgsA);
   }

   key->nr_enabled_units = i + 1;

   /* _NEW_FOG */
   if (texenv_doing_secondary_color(ctx)) {
      key->separate_specular = 1;
      inputs_referenced |= VARYING_BIT_COL1;
   }

   /* _NEW_FOG */
   key->fog_mode = ctx->Fog._PackedEnabledMode;

   /* _NEW_BUFFERS */
   key->num_draw_buffers = ctx->DrawBuffer->_NumColorDrawBuffers;

   /* _NEW_COLOR */
   if (ctx->Color.AlphaEnabled && key->num_draw_buffers == 0) {
      /* if alpha test is enabled we need to emit at least one color */
      key->num_draw_buffers = 1;
   }

   key->inputs_available = filter_fp_input_mask(inputs_referenced, ctx);

   /* compute size of state key, ignoring unused texture units */
   keySize = sizeof(*key) - sizeof(key->unit)
      + key->nr_enabled_units * sizeof(key->unit[0]);

   return keySize;
}


/** State used to build the fragment program:
 */
class texenv_fragment_program : public ir_factory {
public:
   struct gl_shader_program *shader_program;
   struct gl_shader *shader;
   exec_list *top_instructions;
   struct state_key *state;

   ir_variable *src_texture[MAX_TEXTURE_COORD_UNITS];
   /* Reg containing each texture unit's sampled texture color,
    * else undef.
    */

   ir_rvalue *src_previous;	/**< Reg containing color from previous
				 * stage.  May need to be decl'd.
				 */
};

static ir_rvalue *
get_current_attrib(texenv_fragment_program *p, GLuint attrib)
{
   ir_variable *current;
   char name[128];

   snprintf(name, sizeof(name), "gl_CurrentAttribFrag%uMESA", attrib);

   current = p->shader->symbols->get_variable(name);
   assert(current);
   return new(p->mem_ctx) ir_dereference_variable(current);
}

static ir_rvalue *
get_gl_Color(texenv_fragment_program *p)
{
   if (p->state->inputs_available & VARYING_BIT_COL0) {
      ir_variable *var = p->shader->symbols->get_variable("gl_Color");
      assert(var);
      return new(p->mem_ctx) ir_dereference_variable(var);
   } else {
      return get_current_attrib(p, VERT_ATTRIB_COLOR0);
   }
}

static ir_rvalue *
get_source(texenv_fragment_program *p,
	   GLuint src, GLuint unit)
{
   ir_variable *var;
   ir_dereference *deref;

   switch (src) {
   case TEXENV_SRC_TEXTURE:
      return new(p->mem_ctx) ir_dereference_variable(p->src_texture[unit]);

   case TEXENV_SRC_TEXTURE0:
   case TEXENV_SRC_TEXTURE1:
   case TEXENV_SRC_TEXTURE2:
   case TEXENV_SRC_TEXTURE3:
   case TEXENV_SRC_TEXTURE4:
   case TEXENV_SRC_TEXTURE5:
   case TEXENV_SRC_TEXTURE6:
   case TEXENV_SRC_TEXTURE7:
      return new(p->mem_ctx)
	 ir_dereference_variable(p->src_texture[src - TEXENV_SRC_TEXTURE0]);

   case TEXENV_SRC_CONSTANT:
      var = p->shader->symbols->get_variable("gl_TextureEnvColor");
      assert(var);
      deref = new(p->mem_ctx) ir_dereference_variable(var);
      var->data.max_array_access = MAX2(var->data.max_array_access, (int)unit);
      return new(p->mem_ctx) ir_dereference_array(deref,
						  new(p->mem_ctx) ir_constant(unit));

   case TEXENV_SRC_PRIMARY_COLOR:
      var = p->shader->symbols->get_variable("gl_Color");
      assert(var);
      return new(p->mem_ctx) ir_dereference_variable(var);

   case TEXENV_SRC_ZERO:
      return new(p->mem_ctx) ir_constant(0.0f);

   case TEXENV_SRC_ONE:
      return new(p->mem_ctx) ir_constant(1.0f);

   case TEXENV_SRC_PREVIOUS:
      if (!p->src_previous) {
	 return get_gl_Color(p);
      } else {
	 return p->src_previous->clone(p->mem_ctx, NULL);
      }

   default:
      assert(0);
      return NULL;
   }
}

static ir_rvalue *
emit_combine_source(texenv_fragment_program *p,
		    GLuint unit,
		    GLuint source,
		    GLuint operand)
{
   ir_rvalue *src;

   src = get_source(p, source, unit);

   switch (operand) {
   case TEXENV_OPR_ONE_MINUS_COLOR:
      return sub(new(p->mem_ctx) ir_constant(1.0f), src);

   case TEXENV_OPR_ALPHA:
      return src->type->is_scalar() ? src : swizzle_w(src);

   case TEXENV_OPR_ONE_MINUS_ALPHA: {
      ir_rvalue *const scalar = src->type->is_scalar() ? src : swizzle_w(src);

      return sub(new(p->mem_ctx) ir_constant(1.0f), scalar);
   }

   case TEXENV_OPR_COLOR:
      return src;

   default:
      assert(0);
      return src;
   }
}

/**
 * Check if the RGB and Alpha sources and operands match for the given
 * texture unit's combinder state.  When the RGB and A sources and
 * operands match, we can emit fewer instructions.
 */
static GLboolean args_match( const struct state_key *key, GLuint unit )
{
   GLuint i, numArgs = key->unit[unit].NumArgsRGB;

   for (i = 0; i < numArgs; i++) {
      if (key->unit[unit].ArgsA[i].Source != key->unit[unit].ArgsRGB[i].Source)
	 return GL_FALSE;

      switch (key->unit[unit].ArgsA[i].Operand) {
      case TEXENV_OPR_ALPHA:
	 switch (key->unit[unit].ArgsRGB[i].Operand) {
	 case TEXENV_OPR_COLOR:
	 case TEXENV_OPR_ALPHA:
	    break;
	 default:
	    return GL_FALSE;
	 }
	 break;
      case TEXENV_OPR_ONE_MINUS_ALPHA:
	 switch (key->unit[unit].ArgsRGB[i].Operand) {
	 case TEXENV_OPR_ONE_MINUS_COLOR:
	 case TEXENV_OPR_ONE_MINUS_ALPHA:
	    break;
	 default:
	    return GL_FALSE;
	 }
	 break;
      default:
	 return GL_FALSE;	/* impossible */
      }
   }

   return GL_TRUE;
}

static ir_rvalue *
smear(ir_rvalue *val)
{
   if (!val->type->is_scalar())
      return val;

   return swizzle_xxxx(val);
}

static ir_rvalue *
emit_combine(texenv_fragment_program *p,
	     GLuint unit,
	     GLuint nr,
	     GLuint mode,
	     const struct gl_tex_env_argument *opt)
{
   ir_rvalue *src[MAX_COMBINER_TERMS];
   ir_rvalue *tmp0, *tmp1;
   GLuint i;

   assert(nr <= MAX_COMBINER_TERMS);

   for (i = 0; i < nr; i++)
      src[i] = emit_combine_source( p, unit, opt[i].Source, opt[i].Operand );

   switch (mode) {
   case TEXENV_MODE_REPLACE:
      return src[0];

   case TEXENV_MODE_MODULATE:
      return mul(src[0], src[1]);

   case TEXENV_MODE_ADD:
      return add(src[0], src[1]);

   case TEXENV_MODE_ADD_SIGNED:
      return add(add(src[0], src[1]), new(p->mem_ctx) ir_constant(-0.5f));

   case TEXENV_MODE_INTERPOLATE:
      /* Arg0 * (Arg2) + Arg1 * (1-Arg2) */
      tmp0 = mul(src[0], src[2]);
      tmp1 = mul(src[1], sub(new(p->mem_ctx) ir_constant(1.0f),
			     src[2]->clone(p->mem_ctx, NULL)));
      return add(tmp0, tmp1);

   case TEXENV_MODE_SUBTRACT:
      return sub(src[0], src[1]);

   case TEXENV_MODE_DOT3_RGBA:
   case TEXENV_MODE_DOT3_RGBA_EXT:
   case TEXENV_MODE_DOT3_RGB_EXT:
   case TEXENV_MODE_DOT3_RGB: {
      tmp0 = mul(src[0], new(p->mem_ctx) ir_constant(2.0f));
      tmp0 = add(tmp0, new(p->mem_ctx) ir_constant(-1.0f));

      tmp1 = mul(src[1], new(p->mem_ctx) ir_constant(2.0f));
      tmp1 = add(tmp1, new(p->mem_ctx) ir_constant(-1.0f));

      return dot(swizzle_xyz(smear(tmp0)), swizzle_xyz(smear(tmp1)));
   }
   case TEXENV_MODE_MODULATE_ADD_ATI:
      return add(mul(src[0], src[2]), src[1]);

   case TEXENV_MODE_MODULATE_SIGNED_ADD_ATI:
      return add(add(mul(src[0], src[2]), src[1]),
		 new(p->mem_ctx) ir_constant(-0.5f));

   case TEXENV_MODE_MODULATE_SUBTRACT_ATI:
      return sub(mul(src[0], src[2]), src[1]);

   case TEXENV_MODE_ADD_PRODUCTS_NV:
      return add(mul(src[0], src[1]), mul(src[2], src[3]));

   case TEXENV_MODE_ADD_PRODUCTS_SIGNED_NV:
      return add(add(mul(src[0], src[1]), mul(src[2], src[3])),
		 new(p->mem_ctx) ir_constant(-0.5f));
   default:
      assert(0);
      return src[0];
   }
}

/**
 * Generate instructions for one texture unit's env/combiner mode.
 */
static ir_rvalue *
emit_texenv(texenv_fragment_program *p, GLuint unit)
{
   const struct state_key *key = p->state;
   GLboolean rgb_saturate, alpha_saturate;
   GLuint rgb_shift, alpha_shift;

   if (!key->unit[unit].enabled) {
      return get_source(p, TEXENV_SRC_PREVIOUS, 0);
   }

   switch (key->unit[unit].ModeRGB) {
   case TEXENV_MODE_DOT3_RGB_EXT:
      alpha_shift = key->unit[unit].ScaleShiftA;
      rgb_shift = 0;
      break;
   case TEXENV_MODE_DOT3_RGBA_EXT:
      alpha_shift = 0;
      rgb_shift = 0;
      break;
   default:
      rgb_shift = key->unit[unit].ScaleShiftRGB;
      alpha_shift = key->unit[unit].ScaleShiftA;
      break;
   }

   /* If we'll do rgb/alpha shifting don't saturate in emit_combine().
    * We don't want to clamp twice.
    */
   if (rgb_shift)
      rgb_saturate = GL_FALSE;  /* saturate after rgb shift */
   else if (need_saturate(key->unit[unit].ModeRGB))
      rgb_saturate = GL_TRUE;
   else
      rgb_saturate = GL_FALSE;

   if (alpha_shift)
      alpha_saturate = GL_FALSE;  /* saturate after alpha shift */
   else if (need_saturate(key->unit[unit].ModeA))
      alpha_saturate = GL_TRUE;
   else
      alpha_saturate = GL_FALSE;

   ir_variable *temp_var = p->make_temp(glsl_type::vec4_type, "texenv_combine");
   ir_dereference *deref;
   ir_rvalue *val;

   /* Emit the RGB and A combine ops
    */
   if (key->unit[unit].ModeRGB == key->unit[unit].ModeA &&
       args_match(key, unit)) {
      val = emit_combine(p, unit,
			 key->unit[unit].NumArgsRGB,
			 key->unit[unit].ModeRGB,
			 key->unit[unit].ArgsRGB);
      val = smear(val);
      if (rgb_saturate)
	 val = saturate(val);

      p->emit(assign(temp_var, val));
   }
   else if (key->unit[unit].ModeRGB == TEXENV_MODE_DOT3_RGBA_EXT ||
	    key->unit[unit].ModeRGB == TEXENV_MODE_DOT3_RGBA) {
      ir_rvalue *val = emit_combine(p, unit,
				    key->unit[unit].NumArgsRGB,
				    key->unit[unit].ModeRGB,
				    key->unit[unit].ArgsRGB);
      val = smear(val);
      if (rgb_saturate)
	 val = saturate(val);
      p->emit(assign(temp_var, val));
   }
   else {
      /* Need to do something to stop from re-emitting identical
       * argument calculations here:
       */
      val = emit_combine(p, unit,
			 key->unit[unit].NumArgsRGB,
			 key->unit[unit].ModeRGB,
			 key->unit[unit].ArgsRGB);
      val = swizzle_xyz(smear(val));
      if (rgb_saturate)
	 val = saturate(val);
      p->emit(assign(temp_var, val, WRITEMASK_XYZ));

      val = emit_combine(p, unit,
			 key->unit[unit].NumArgsA,
			 key->unit[unit].ModeA,
			 key->unit[unit].ArgsA);
      val = swizzle_w(smear(val));
      if (alpha_saturate)
	 val = saturate(val);
      p->emit(assign(temp_var, val, WRITEMASK_W));
   }

   deref = new(p->mem_ctx) ir_dereference_variable(temp_var);

   /* Deal with the final shift:
    */
   if (alpha_shift || rgb_shift) {
      ir_constant *shift;

      if (rgb_shift == alpha_shift) {
	 shift = new(p->mem_ctx) ir_constant((float)(1 << rgb_shift));
      }
      else {
         ir_constant_data const_data;

         const_data.f[0] = float(1 << rgb_shift);
         const_data.f[1] = float(1 << rgb_shift);
         const_data.f[2] = float(1 << rgb_shift);
         const_data.f[3] = float(1 << alpha_shift);

         shift = new(p->mem_ctx) ir_constant(glsl_type::vec4_type,
                                             &const_data);
      }

      return saturate(mul(deref, shift));
   }
   else
      return deref;
}


/**
 * Generate instruction for getting a texture source term.
 */
static void load_texture( texenv_fragment_program *p, GLuint unit )
{
   ir_dereference *deref;

   if (p->src_texture[unit])
      return;

   const GLuint texTarget = p->state->unit[unit].source_index;
   ir_rvalue *texcoord;

   if (!(p->state->inputs_available & (VARYING_BIT_TEX0 << unit))) {
      texcoord = get_current_attrib(p, VERT_ATTRIB_TEX0 + unit);
   } else {
      ir_variable *tc_array = p->shader->symbols->get_variable("gl_TexCoord");
      assert(tc_array);
      texcoord = new(p->mem_ctx) ir_dereference_variable(tc_array);
      ir_rvalue *index = new(p->mem_ctx) ir_constant(unit);
      texcoord = new(p->mem_ctx) ir_dereference_array(texcoord, index);
      tc_array->data.max_array_access = MAX2(tc_array->data.max_array_access, (int)unit);
   }

   if (!p->state->unit[unit].enabled) {
      p->src_texture[unit] = p->make_temp(glsl_type::vec4_type,
					  "dummy_tex");
      p->emit(p->src_texture[unit]);

      p->emit(assign(p->src_texture[unit], new(p->mem_ctx) ir_constant(0.0f)));
      return ;
   }

   const glsl_type *sampler_type = NULL;
   int coords = 0;

   switch (texTarget) {
   case TEXTURE_1D_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::sampler1DShadow_type;
      else
	 sampler_type = glsl_type::sampler1D_type;
      coords = 1;
      break;
   case TEXTURE_1D_ARRAY_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::sampler1DArrayShadow_type;
      else
	 sampler_type = glsl_type::sampler1DArray_type;
      coords = 2;
      break;
   case TEXTURE_2D_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::sampler2DShadow_type;
      else
	 sampler_type = glsl_type::sampler2D_type;
      coords = 2;
      break;
   case TEXTURE_2D_ARRAY_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::sampler2DArrayShadow_type;
      else
	 sampler_type = glsl_type::sampler2DArray_type;
      coords = 3;
      break;
   case TEXTURE_RECT_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::sampler2DRectShadow_type;
      else
	 sampler_type = glsl_type::sampler2DRect_type;
      coords = 2;
      break;
   case TEXTURE_3D_INDEX:
      assert(!p->state->unit[unit].shadow);
      sampler_type = glsl_type::sampler3D_type;
      coords = 3;
      break;
   case TEXTURE_CUBE_INDEX:
      if (p->state->unit[unit].shadow)
	 sampler_type = glsl_type::samplerCubeShadow_type;
      else
	 sampler_type = glsl_type::samplerCube_type;
      coords = 3;
      break;
   case TEXTURE_EXTERNAL_INDEX:
      assert(!p->state->unit[unit].shadow);
      sampler_type = glsl_type::samplerExternalOES_type;
      coords = 2;
      break;
   }

   p->src_texture[unit] = p->make_temp(glsl_type::vec4_type,
				       "tex");

   ir_texture *tex = new(p->mem_ctx) ir_texture(ir_tex);


   char *sampler_name = ralloc_asprintf(p->mem_ctx, "sampler_%d", unit);
   ir_variable *sampler = new(p->mem_ctx) ir_variable(sampler_type,
						      sampler_name,
						      ir_var_uniform);
   p->top_instructions->push_head(sampler);

   /* Set the texture unit for this sampler in the same way that
    * layout(binding=X) would.
    */
   sampler->data.explicit_binding = true;
   sampler->data.binding = unit;

   deref = new(p->mem_ctx) ir_dereference_variable(sampler);
   tex->set_sampler(deref, glsl_type::vec4_type);

   tex->coordinate = new(p->mem_ctx) ir_swizzle(texcoord, 0, 1, 2, 3, coords);

   if (p->state->unit[unit].shadow) {
      texcoord = texcoord->clone(p->mem_ctx, NULL);
      tex->shadow_comparator = new(p->mem_ctx) ir_swizzle(texcoord,
							  coords, 0, 0, 0,
							  1);
      coords++;
   }

   texcoord = texcoord->clone(p->mem_ctx, NULL);
   tex->projector = swizzle_w(texcoord);

   p->emit(assign(p->src_texture[unit], tex));
}

static void
load_texenv_source(texenv_fragment_program *p,
		   GLuint src, GLuint unit)
{
   switch (src) {
   case TEXENV_SRC_TEXTURE:
      load_texture(p, unit);
      break;

   case TEXENV_SRC_TEXTURE0:
   case TEXENV_SRC_TEXTURE1:
   case TEXENV_SRC_TEXTURE2:
   case TEXENV_SRC_TEXTURE3:
   case TEXENV_SRC_TEXTURE4:
   case TEXENV_SRC_TEXTURE5:
   case TEXENV_SRC_TEXTURE6:
   case TEXENV_SRC_TEXTURE7:
      load_texture(p, src - TEXENV_SRC_TEXTURE0);
      break;

   default:
      /* not a texture src - do nothing */
      break;
   }
}


/**
 * Generate instructions for loading all texture source terms.
 */
static GLboolean
load_texunit_sources( texenv_fragment_program *p, GLuint unit )
{
   const struct state_key *key = p->state;
   GLuint i;

   for (i = 0; i < key->unit[unit].NumArgsRGB; i++) {
      load_texenv_source( p, key->unit[unit].ArgsRGB[i].Source, unit );
   }

   for (i = 0; i < key->unit[unit].NumArgsA; i++) {
      load_texenv_source( p, key->unit[unit].ArgsA[i].Source, unit );
   }

   return GL_TRUE;
}

/**
 * Applies the fog calculations.
 *
 * This is basically like the ARB_fragment_prorgam fog options.  Note
 * that ffvertex_prog.c produces fogcoord for us when
 * GL_FOG_COORDINATE_EXT is set to GL_FRAGMENT_DEPTH_EXT.
 */
static ir_rvalue *
emit_fog_instructions(texenv_fragment_program *p,
		      ir_rvalue *fragcolor)
{
   struct state_key *key = p->state;
   ir_rvalue *f, *temp;
   ir_variable *params, *oparams;
   ir_variable *fogcoord;

   /* Temporary storage for the whole fog result.  Fog calculations
    * only affect rgb so we're hanging on to the .a value of fragcolor
    * this way.
    */
   ir_variable *fog_result = p->make_temp(glsl_type::vec4_type, "fog_result");
   p->emit(assign(fog_result, fragcolor));

   fragcolor = swizzle_xyz(fog_result);

   oparams = p->shader->symbols->get_variable("gl_FogParamsOptimizedMESA");
   assert(oparams);
   fogcoord = p->shader->symbols->get_variable("gl_FogFragCoord");
   assert(fogcoord);
   params = p->shader->symbols->get_variable("gl_Fog");
   assert(params);
   f = new(p->mem_ctx) ir_dereference_variable(fogcoord);

   ir_variable *f_var = p->make_temp(glsl_type::float_type, "fog_factor");

   switch (key->fog_mode) {
   case FOG_LINEAR:
      /* f = (end - z) / (end - start)
       *
       * gl_MesaFogParamsOptimized gives us (-1 / (end - start)) and
       * (end / (end - start)) so we can generate a single MAD.
       */
      f = add(mul(f, swizzle_x(oparams)), swizzle_y(oparams));
      break;
   case FOG_EXP:
      /* f = e^(-(density * fogcoord))
       *
       * gl_MesaFogParamsOptimized gives us density/ln(2) so we can
       * use EXP2 which is generally the native instruction without
       * having to do any further math on the fog density uniform.
       */
      f = mul(f, swizzle_z(oparams));
      f = new(p->mem_ctx) ir_expression(ir_unop_neg, f);
      f = new(p->mem_ctx) ir_expression(ir_unop_exp2, f);
      break;
   case FOG_EXP2:
      /* f = e^(-(density * fogcoord)^2)
       *
       * gl_MesaFogParamsOptimized gives us density/sqrt(ln(2)) so we
       * can do this like FOG_EXP but with a squaring after the
       * multiply by density.
       */
      ir_variable *temp_var = p->make_temp(glsl_type::float_type, "fog_temp");
      p->emit(assign(temp_var, mul(f, swizzle_w(oparams))));

      f = mul(temp_var, temp_var);
      f = new(p->mem_ctx) ir_expression(ir_unop_neg, f);
      f = new(p->mem_ctx) ir_expression(ir_unop_exp2, f);
      break;
   }

   p->emit(assign(f_var, saturate(f)));

   f = sub(new(p->mem_ctx) ir_constant(1.0f), f_var);
   temp = new(p->mem_ctx) ir_dereference_variable(params);
   temp = new(p->mem_ctx) ir_dereference_record(temp, "color");
   temp = mul(swizzle_xyz(temp), f);

   p->emit(assign(fog_result, add(temp, mul(fragcolor, f_var)), WRITEMASK_XYZ));

   return new(p->mem_ctx) ir_dereference_variable(fog_result);
}

static void
emit_instructions(texenv_fragment_program *p)
{
   struct state_key *key = p->state;
   GLuint unit;

   if (key->nr_enabled_units) {
      /* First pass - to support texture_env_crossbar, first identify
       * all referenced texture sources and emit texld instructions
       * for each:
       */
      for (unit = 0; unit < key->nr_enabled_units; unit++)
	 if (key->unit[unit].enabled) {
	    load_texunit_sources(p, unit);
	 }

      /* Second pass - emit combine instructions to build final color:
       */
      for (unit = 0; unit < key->nr_enabled_units; unit++) {
	 if (key->unit[unit].enabled) {
	    p->src_previous = emit_texenv(p, unit);
	 }
      }
   }

   ir_rvalue *cf = get_source(p, TEXENV_SRC_PREVIOUS, 0);

   if (key->separate_specular) {
      ir_variable *spec_result = p->make_temp(glsl_type::vec4_type,
					      "specular_add");
      p->emit(assign(spec_result, cf));

      ir_rvalue *secondary;
      if (p->state->inputs_available & VARYING_BIT_COL1) {
	 ir_variable *var =
	    p->shader->symbols->get_variable("gl_SecondaryColor");
	 assert(var);
	 secondary = swizzle_xyz(var);
      } else {
	 secondary = swizzle_xyz(get_current_attrib(p, VERT_ATTRIB_COLOR1));
      }

      p->emit(assign(spec_result, add(swizzle_xyz(spec_result), secondary),
		     WRITEMASK_XYZ));

      cf = new(p->mem_ctx) ir_dereference_variable(spec_result);
   }

   if (key->fog_mode) {
      cf = emit_fog_instructions(p, cf);
   }

   ir_variable *frag_color = p->shader->symbols->get_variable("gl_FragColor");
   assert(frag_color);
   p->emit(assign(frag_color, cf));
}

/**
 * Generate a new fragment program which implements the context's
 * current texture env/combine mode.
 */
static struct gl_shader_program *
create_new_program(struct gl_context *ctx, struct state_key *key)
{
   texenv_fragment_program p;
   unsigned int unit;
   _mesa_glsl_parse_state *state;

   p.mem_ctx = ralloc_context(NULL);
   p.shader = _mesa_new_shader(0, MESA_SHADER_FRAGMENT);
#ifdef DEBUG
   p.shader->SourceChecksum = 0xf18ed; /* fixed */
#endif
   p.shader->ir = new(p.shader) exec_list;
   state = new(p.shader) _mesa_glsl_parse_state(ctx, MESA_SHADER_FRAGMENT,
						p.shader);
   p.shader->symbols = state->symbols;
   p.top_instructions = p.shader->ir;
   p.instructions = p.shader->ir;
   p.state = key;
   p.shader_program = _mesa_new_shader_program(0);

   /* Tell the linker to ignore the fact that we're building a
    * separate shader, in case we're in a GLES2 context that would
    * normally reject that.  The real problem is that we're building a
    * fixed function program in a GLES2 context at all, but that's a
    * big mess to clean up.
    */
   p.shader_program->SeparateShader = GL_TRUE;

   /* The legacy GLSL shadow functions follow the depth texture
    * mode and return vec4. The GLSL 1.30 shadow functions return float and
    * ignore the depth texture mode. That's a shader and state dependency
    * that's difficult to deal with. st/mesa uses a simple but not
    * completely correct solution: if the shader declares GLSL >= 1.30 and
    * the depth texture mode is GL_ALPHA (000X), it sets the XXXX swizzle
    * instead. Thus, the GLSL 1.30 shadow function will get the result in .x
    * and legacy shadow functions will get it in .w as expected.
    * For the fixed-function fragment shader, use 120 to get correct behavior
    * for GL_ALPHA.
    */
   state->language_version = 120;

   state->es_shader = false;
   if (_mesa_is_gles(ctx) && ctx->Extensions.OES_EGL_image_external)
      state->OES_EGL_image_external_enable = true;
   _mesa_glsl_initialize_types(state);
   _mesa_glsl_initialize_variables(p.instructions, state);

   for (unit = 0; unit < ctx->Const.MaxTextureUnits; unit++)
      p.src_texture[unit] = NULL;

   p.src_previous = NULL;

   ir_function *main_f = new(p.mem_ctx) ir_function("main");
   p.emit(main_f);
   state->symbols->add_function(main_f);

   ir_function_signature *main_sig =
      new(p.mem_ctx) ir_function_signature(glsl_type::void_type);
   main_sig->is_defined = true;
   main_f->add_signature(main_sig);

   p.instructions = &main_sig->body;
   if (key->num_draw_buffers)
      emit_instructions(&p);

   validate_ir_tree(p.shader->ir);

   const struct gl_shader_compiler_options *options =
      &ctx->Const.ShaderCompilerOptions[MESA_SHADER_FRAGMENT];

   /* Conservative approach: Don't optimize here, the linker does it too. */
   if (!ctx->Const.GLSLOptimizeConservatively) {
      while (do_common_optimization(p.shader->ir, false, false, options,
                                    ctx->Const.NativeIntegers))
         ;
   }

   reparent_ir(p.shader->ir, p.shader->ir);

   p.shader->CompileStatus = COMPILE_SUCCESS;
   p.shader->Version = state->language_version;
   p.shader_program->Shaders =
      (gl_shader **)malloc(sizeof(*p.shader_program->Shaders));
   p.shader_program->Shaders[0] = p.shader;
   p.shader_program->NumShaders = 1;

   _mesa_glsl_link_shader(ctx, p.shader_program);

   if (!p.shader_program->data->LinkStatus)
      _mesa_problem(ctx, "Failed to link fixed function fragment shader: %s\n",
                    p.shader_program->data->InfoLog);

   ralloc_free(p.mem_ctx);
   return p.shader_program;
}

extern "C" {

/**
 * Return a fragment program which implements the current
 * fixed-function texture, fog and color-sum operations.
 */
struct gl_shader_program *
_mesa_get_fixed_func_fragment_program(struct gl_context *ctx)
{
   struct gl_shader_program *shader_program;
   struct state_key key;
   GLuint keySize;

   keySize = make_state_key(ctx, &key);

   shader_program = (struct gl_shader_program *)
      _mesa_search_program_cache(ctx->FragmentProgram.Cache,
                                 &key, keySize);

   if (!shader_program) {
      shader_program = create_new_program(ctx, &key);

      _mesa_shader_cache_insert(ctx, ctx->FragmentProgram.Cache,
				&key, keySize, shader_program);
   }

   return shader_program;
}

}
