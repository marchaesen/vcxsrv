/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
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

/**
 * \file prog_statevars.c
 * Program state variable management.
 * \author Brian Paul
 */


#include <stdio.h>
#include "main/glheader.h"
#include "main/context.h"
#include "main/blend.h"

#include "main/macros.h"
#include "main/mtypes.h"
#include "main/fbobject.h"
#include "prog_statevars.h"
#include "prog_parameter.h"
#include "main/samplerobj.h"
#include "main/framebuffer.h"


#define ONE_DIV_SQRT_LN2 (1.201122408786449815)

static ALWAYS_INLINE void
copy_matrix(float *value, const float *m, unsigned firstRow, unsigned lastRow)
{
   unsigned i, row;

   assert(firstRow < 4);
   assert(lastRow < 4);

   for (i = 0, row = firstRow; row <= lastRow; row++) {
      value[i++] = m[row + 0];
      value[i++] = m[row + 4];
      value[i++] = m[row + 8];
      value[i++] = m[row + 12];
   }
}

static ALWAYS_INLINE void
copy_matrix_transposed(float *value, const float *m, unsigned firstRow, unsigned lastRow)
{
   assert(firstRow < 4);
   assert(lastRow < 4);

   memcpy(value, &m[firstRow * 4],
          (lastRow - firstRow + 1) * 4 * sizeof(GLfloat));
}

/**
 * Use the list of tokens in the state[] array to find global GL state
 * and return it in <value>.  Usually, four values are returned in <value>
 * but matrix queries may return as many as 16 values.
 * This function is used for ARB vertex/fragment programs.
 * The program parser will produce the state[] values.
 */
static void
fetch_state(struct gl_context *ctx, const gl_state_index16 state[],
            gl_constant_value *val)
{
   GLfloat *value = &val->f;

   switch (state[0]) {
   case STATE_MATERIAL:
      {
         /* state[1] is either 0=front or 1=back side */
         const GLuint face = (GLuint) state[1];
         const struct gl_material *mat = &ctx->Light.Material;
         assert(face == 0 || face == 1);
         /* we rely on tokens numbered so that _BACK_ == _FRONT_+ 1 */
         assert(MAT_ATTRIB_FRONT_AMBIENT + 1 == MAT_ATTRIB_BACK_AMBIENT);
         /* XXX we could get rid of this switch entirely with a little
          * work in arbprogparse.c's parse_state_single_item().
          */
         /* state[2] is the material attribute */
         switch (state[2]) {
         case STATE_AMBIENT:
            COPY_4V(value, mat->Attrib[MAT_ATTRIB_FRONT_AMBIENT + face]);
            return;
         case STATE_DIFFUSE:
            COPY_4V(value, mat->Attrib[MAT_ATTRIB_FRONT_DIFFUSE + face]);
            return;
         case STATE_SPECULAR:
            COPY_4V(value, mat->Attrib[MAT_ATTRIB_FRONT_SPECULAR + face]);
            return;
         case STATE_EMISSION:
            COPY_4V(value, mat->Attrib[MAT_ATTRIB_FRONT_EMISSION + face]);
            return;
         case STATE_SHININESS:
            value[0] = mat->Attrib[MAT_ATTRIB_FRONT_SHININESS + face][0];
            value[1] = 0.0F;
            value[2] = 0.0F;
            value[3] = 1.0F;
            return;
         default:
            unreachable("Invalid material state in fetch_state");
            return;
         }
      }
   case STATE_LIGHT:
      {
         /* state[1] is the light number */
         const GLuint ln = (GLuint) state[1];
         /* state[2] is the light attribute */
         const unsigned index = state[2] - STATE_AMBIENT;
         assert(index < 8);
         if (index != STATE_SPOT_CUTOFF)
            COPY_4V(value, (float*)&ctx->Light.LightSource[ln] + index * 4);
         else
            value[0] = ctx->Light.LightSource[ln].SpotCutoff;
         return;
      }
   case STATE_LIGHT_ATTRIBS:
      /* state[1] is the index of the first value */
      /* state[2] is the number of values */
      assert(state[1] + state[2] <= ARRAY_SIZE(ctx->Light.LightSourceData));
      memcpy(value, &ctx->Light.LightSourceData[state[1]],
             state[2] * sizeof(float));
      return;
   case STATE_LIGHTMODEL_AMBIENT:
      COPY_4V(value, ctx->Light.Model.Ambient);
      return;
   case STATE_LIGHTMODEL_SCENECOLOR:
      if (state[1] == 0) {
         /* front */
         GLint i;
         for (i = 0; i < 3; i++) {
            value[i] = ctx->Light.Model.Ambient[i]
               * ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_AMBIENT][i]
               + ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_EMISSION][i];
         }
	 value[3] = ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_DIFFUSE][3];
      }
      else {
         /* back */
         GLint i;
         for (i = 0; i < 3; i++) {
            value[i] = ctx->Light.Model.Ambient[i]
               * ctx->Light.Material.Attrib[MAT_ATTRIB_BACK_AMBIENT][i]
               + ctx->Light.Material.Attrib[MAT_ATTRIB_BACK_EMISSION][i];
         }
	 value[3] = ctx->Light.Material.Attrib[MAT_ATTRIB_BACK_DIFFUSE][3];
      }
      return;
   case STATE_LIGHTPROD:
      {
         const GLuint ln = (GLuint) state[1];
         const GLuint face = (GLuint) state[2];
         GLint i;
         assert(face == 0 || face == 1);
         switch (state[3]) {
            case STATE_AMBIENT:
               for (i = 0; i < 3; i++) {
                  value[i] = ctx->Light.LightSource[ln].Ambient[i] *
                     ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_AMBIENT+face][i];
               }
               /* [3] = material alpha */
               value[3] = ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_AMBIENT+face][3];
               return;
            case STATE_DIFFUSE:
               for (i = 0; i < 3; i++) {
                  value[i] = ctx->Light.LightSource[ln].Diffuse[i] *
                     ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_DIFFUSE+face][i];
               }
               /* [3] = material alpha */
               value[3] = ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_DIFFUSE+face][3];
               return;
            case STATE_SPECULAR:
               for (i = 0; i < 3; i++) {
                  value[i] = ctx->Light.LightSource[ln].Specular[i] *
                     ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_SPECULAR+face][i];
               }
               /* [3] = material alpha */
               value[3] = ctx->Light.Material.Attrib[MAT_ATTRIB_FRONT_SPECULAR+face][3];
               return;
            default:
               unreachable("Invalid lightprod state in fetch_state");
               return;
         }
      }
   case STATE_TEXGEN:
      {
         /* state[1] is the texture unit */
         const GLuint unit = (GLuint) state[1];
         /* state[2] is the texgen attribute */
         switch (state[2]) {
         case STATE_TEXGEN_EYE_S:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenS.EyePlane);
            return;
         case STATE_TEXGEN_EYE_T:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenT.EyePlane);
            return;
         case STATE_TEXGEN_EYE_R:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenR.EyePlane);
            return;
         case STATE_TEXGEN_EYE_Q:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenQ.EyePlane);
            return;
         case STATE_TEXGEN_OBJECT_S:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenS.ObjectPlane);
            return;
         case STATE_TEXGEN_OBJECT_T:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenT.ObjectPlane);
            return;
         case STATE_TEXGEN_OBJECT_R:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenR.ObjectPlane);
            return;
         case STATE_TEXGEN_OBJECT_Q:
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].GenQ.ObjectPlane);
            return;
         default:
            unreachable("Invalid texgen state in fetch_state");
            return;
         }
      }
   case STATE_TEXENV_COLOR:
      {
         /* state[1] is the texture unit */
         const GLuint unit = (GLuint) state[1];
         if (_mesa_get_clamp_fragment_color(ctx, ctx->DrawBuffer))
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].EnvColor);
         else
            COPY_4V(value, ctx->Texture.FixedFuncUnit[unit].EnvColorUnclamped);
      }
      return;
   case STATE_FOG_COLOR:
      if (_mesa_get_clamp_fragment_color(ctx, ctx->DrawBuffer))
         COPY_4V(value, ctx->Fog.Color);
      else
         COPY_4V(value, ctx->Fog.ColorUnclamped);
      return;
   case STATE_FOG_PARAMS:
      value[0] = ctx->Fog.Density;
      value[1] = ctx->Fog.Start;
      value[2] = ctx->Fog.End;
      value[3] = 1.0f / (ctx->Fog.End - ctx->Fog.Start);
      return;
   case STATE_CLIPPLANE:
      {
         const GLuint plane = (GLuint) state[1];
         COPY_4V(value, ctx->Transform.EyeUserPlane[plane]);
      }
      return;
   case STATE_POINT_SIZE:
      value[0] = ctx->Point.Size;
      value[1] = ctx->Point.MinSize;
      value[2] = ctx->Point.MaxSize;
      value[3] = ctx->Point.Threshold;
      return;
   case STATE_POINT_ATTENUATION:
      value[0] = ctx->Point.Params[0];
      value[1] = ctx->Point.Params[1];
      value[2] = ctx->Point.Params[2];
      value[3] = 1.0F;
      return;
   /* state[0] = modelview, projection, texture, etc. */
   /* state[1] = which texture matrix or program matrix */
   /* state[2] = first row to fetch */
   /* state[3] = last row to fetch */
   case STATE_MODELVIEW_MATRIX: {
      const GLmatrix *matrix = ctx->ModelviewMatrixStack.Top;
      copy_matrix(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_MODELVIEW_MATRIX_INVERSE: {
      const GLmatrix *matrix = ctx->ModelviewMatrixStack.Top;
      copy_matrix(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_MODELVIEW_MATRIX_TRANSPOSE: {
      const GLmatrix *matrix = ctx->ModelviewMatrixStack.Top;
      copy_matrix_transposed(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_MODELVIEW_MATRIX_INVTRANS: {
      const GLmatrix *matrix = ctx->ModelviewMatrixStack.Top;
      copy_matrix_transposed(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_PROJECTION_MATRIX: {
      const GLmatrix *matrix = ctx->ProjectionMatrixStack.Top;
      copy_matrix(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_PROJECTION_MATRIX_INVERSE: {
      const GLmatrix *matrix = ctx->ProjectionMatrixStack.Top;
      copy_matrix(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_PROJECTION_MATRIX_TRANSPOSE: {
      const GLmatrix *matrix = ctx->ProjectionMatrixStack.Top;
      copy_matrix_transposed(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_PROJECTION_MATRIX_INVTRANS: {
      const GLmatrix *matrix = ctx->ProjectionMatrixStack.Top;
      copy_matrix_transposed(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_MVP_MATRIX: {
      const GLmatrix *matrix = &ctx->_ModelProjectMatrix;
      copy_matrix(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_MVP_MATRIX_INVERSE: {
      const GLmatrix *matrix = &ctx->_ModelProjectMatrix;
      copy_matrix(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_MVP_MATRIX_TRANSPOSE: {
      const GLmatrix *matrix = &ctx->_ModelProjectMatrix;
      copy_matrix_transposed(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_MVP_MATRIX_INVTRANS: {
      const GLmatrix *matrix = &ctx->_ModelProjectMatrix;
      copy_matrix_transposed(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_TEXTURE_MATRIX: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->TextureMatrixStack));
      const GLmatrix *matrix = ctx->TextureMatrixStack[index].Top;
      copy_matrix(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_TEXTURE_MATRIX_INVERSE: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->TextureMatrixStack));
      const GLmatrix *matrix = ctx->TextureMatrixStack[index].Top;
      copy_matrix(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_TEXTURE_MATRIX_TRANSPOSE: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->TextureMatrixStack));
      const GLmatrix *matrix = ctx->TextureMatrixStack[index].Top;
      copy_matrix_transposed(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_TEXTURE_MATRIX_INVTRANS: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->TextureMatrixStack));
      const GLmatrix *matrix = ctx->TextureMatrixStack[index].Top;
      copy_matrix_transposed(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_PROGRAM_MATRIX: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->ProgramMatrixStack));
      const GLmatrix *matrix = ctx->ProgramMatrixStack[index].Top;
      copy_matrix(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_PROGRAM_MATRIX_INVERSE: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->ProgramMatrixStack));
      const GLmatrix *matrix = ctx->ProgramMatrixStack[index].Top;
      _math_matrix_analyse((GLmatrix*)matrix); /* Be sure inverse is up to date: */
      copy_matrix(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_PROGRAM_MATRIX_TRANSPOSE: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->ProgramMatrixStack));
      const GLmatrix *matrix = ctx->ProgramMatrixStack[index].Top;
      copy_matrix_transposed(value, matrix->m, state[2], state[3]);
      return;
   }
   case STATE_PROGRAM_MATRIX_INVTRANS: {
      const GLuint index = (GLuint) state[1];
      assert(index < ARRAY_SIZE(ctx->ProgramMatrixStack));
      const GLmatrix *matrix = ctx->ProgramMatrixStack[index].Top;
      _math_matrix_analyse((GLmatrix*)matrix); /* Be sure inverse is up to date: */
      copy_matrix_transposed(value, matrix->inv, state[2], state[3]);
      return;
   }
   case STATE_NUM_SAMPLES:
      val[0].i = MAX2(1, _mesa_geometric_samples(ctx->DrawBuffer));
      return;
   case STATE_DEPTH_RANGE:
      value[0] = ctx->ViewportArray[0].Near;                /* near       */
      value[1] = ctx->ViewportArray[0].Far;                 /* far        */
      value[2] = ctx->ViewportArray[0].Far - ctx->ViewportArray[0].Near; /* far - near */
      value[3] = 1.0;
      return;
   case STATE_FRAGMENT_PROGRAM:
      {
         /* state[1] = {STATE_ENV, STATE_LOCAL} */
         /* state[2] = parameter index          */
         const int idx = (int) state[2];
         switch (state[1]) {
            case STATE_ENV:
               COPY_4V(value, ctx->FragmentProgram.Parameters[idx]);
               return;
            case STATE_LOCAL:
               if (!ctx->FragmentProgram.Current->arb.LocalParams) {
                  ctx->FragmentProgram.Current->arb.LocalParams =
                     rzalloc_array_size(ctx->FragmentProgram.Current,
                                        sizeof(float[4]),
                                        MAX_PROGRAM_LOCAL_PARAMS);
                  if (!ctx->FragmentProgram.Current->arb.LocalParams)
                     return;
               }

               COPY_4V(value,
                       ctx->FragmentProgram.Current->arb.LocalParams[idx]);
               return;
            default:
               unreachable("Bad state switch in fetch_state()");
               return;
         }
      }
      return;

   case STATE_VERTEX_PROGRAM:
      {
         /* state[1] = {STATE_ENV, STATE_LOCAL} */
         /* state[2] = parameter index          */
         const int idx = (int) state[2];
         switch (state[1]) {
            case STATE_ENV:
               COPY_4V(value, ctx->VertexProgram.Parameters[idx]);
               return;
            case STATE_LOCAL:
               if (!ctx->VertexProgram.Current->arb.LocalParams) {
                  ctx->VertexProgram.Current->arb.LocalParams =
                     rzalloc_array_size(ctx->VertexProgram.Current,
                                        sizeof(float[4]),
                                        MAX_PROGRAM_LOCAL_PARAMS);
                  if (!ctx->VertexProgram.Current->arb.LocalParams)
                     return;
               }

               COPY_4V(value,
                       ctx->VertexProgram.Current->arb.LocalParams[idx]);
               return;
            default:
               unreachable("Bad state switch in fetch_state()");
               return;
         }
      }
      return;

   case STATE_NORMAL_SCALE:
      ASSIGN_4V(value, ctx->_ModelViewInvScaleEyespace, 0, 0, 1);
      return;

   case STATE_INTERNAL:
      switch (state[1]) {
      case STATE_CURRENT_ATTRIB:
         {
            const GLuint idx = (GLuint) state[2];
            COPY_4V(value, ctx->Current.Attrib[idx]);
         }
         return;

      case STATE_CURRENT_ATTRIB_MAYBE_VP_CLAMPED:
         {
            const GLuint idx = (GLuint) state[2];
            if(ctx->Light._ClampVertexColor &&
               (idx == VERT_ATTRIB_COLOR0 ||
                idx == VERT_ATTRIB_COLOR1)) {
               value[0] = SATURATE(ctx->Current.Attrib[idx][0]);
               value[1] = SATURATE(ctx->Current.Attrib[idx][1]);
               value[2] = SATURATE(ctx->Current.Attrib[idx][2]);
               value[3] = SATURATE(ctx->Current.Attrib[idx][3]);
            }
            else
               COPY_4V(value, ctx->Current.Attrib[idx]);
         }
         return;

      case STATE_NORMAL_SCALE:
         ASSIGN_4V(value,
                   ctx->_ModelViewInvScale,
                   ctx->_ModelViewInvScale,
                   ctx->_ModelViewInvScale,
                   1);
         return;

      case STATE_FOG_PARAMS_OPTIMIZED: {
         /* for simpler per-vertex/pixel fog calcs. POW (for EXP/EXP2 fog)
          * might be more expensive than EX2 on some hw, plus it needs
          * another constant (e) anyway. Linear fog can now be done with a
          * single MAD.
          * linear: fogcoord * -1/(end-start) + end/(end-start)
          * exp: 2^-(density/ln(2) * fogcoord)
          * exp2: 2^-((density/(sqrt(ln(2))) * fogcoord)^2)
          */
         float val =  (ctx->Fog.End == ctx->Fog.Start)
            ? 1.0f : (GLfloat)(-1.0F / (ctx->Fog.End - ctx->Fog.Start));
         value[0] = val;
         value[1] = ctx->Fog.End * -val;
         value[2] = (GLfloat)(ctx->Fog.Density * M_LOG2E); /* M_LOG2E == 1/ln(2) */
         value[3] = (GLfloat)(ctx->Fog.Density * ONE_DIV_SQRT_LN2);
         return;
      }

      case STATE_POINT_SIZE_CLAMPED:
         {
           /* this includes implementation dependent limits, to avoid
            * another potentially necessary clamp.
            * Note: for sprites, point smooth (point AA) is ignored
            * and we'll clamp to MinPointSizeAA and MaxPointSize, because we
            * expect drivers will want to say their minimum for AA size is 0.0
            * but for non-AA it's 1.0 (because normal points with size below 1.0
            * need to get rounded up to 1.0, hence never disappear). GL does
            * not specify max clamp size for sprites, other than it needs to be
            * at least as large as max AA size, hence use non-AA size there.
            */
            GLfloat minImplSize;
            GLfloat maxImplSize;
            if (ctx->Point.PointSprite) {
               minImplSize = ctx->Const.MinPointSizeAA;
               maxImplSize = ctx->Const.MaxPointSize;
            }
            else if (ctx->Point.SmoothFlag || _mesa_is_multisample_enabled(ctx)) {
               minImplSize = ctx->Const.MinPointSizeAA;
               maxImplSize = ctx->Const.MaxPointSizeAA;
            }
            else {
               minImplSize = ctx->Const.MinPointSize;
               maxImplSize = ctx->Const.MaxPointSize;
            }
            value[0] = ctx->Point.Size;
            value[1] = ctx->Point.MinSize >= minImplSize ? ctx->Point.MinSize : minImplSize;
            value[2] = ctx->Point.MaxSize <= maxImplSize ? ctx->Point.MaxSize : maxImplSize;
            value[3] = ctx->Point.Threshold;
         }
         return;
      case STATE_LIGHT_SPOT_DIR_NORMALIZED:
         {
            /* here, state[2] is the light number */
            /* pre-normalize spot dir */
            const GLuint ln = (GLuint) state[2];
            COPY_3V(value, ctx->Light.Light[ln]._NormSpotDirection);
            value[3] = ctx->Light.LightSource[ln]._CosCutoff;
         }
         return;

      case STATE_LIGHT_POSITION:
         {
            const GLuint ln = (GLuint) state[2];
            COPY_4V(value, ctx->Light.Light[ln]._Position);
         }
         return;

      case STATE_LIGHT_POSITION_NORMALIZED:
         {
            const GLuint ln = (GLuint) state[2];
            float p[4];
            COPY_4V(p, ctx->Light.Light[ln]._Position);
            NORMALIZE_3FV(p);
            COPY_4V(value, p);
         }
         return;

      case STATE_LIGHT_HALF_VECTOR:
         {
            const GLuint ln = (GLuint) state[2];
            GLfloat p[3];
            /* Compute infinite half angle vector:
             *   halfVector = normalize(normalize(lightPos) + (0, 0, 1))
             * light.EyePosition.w should be 0 for infinite lights.
             */
            COPY_3V(p, ctx->Light.Light[ln]._Position);
            NORMALIZE_3FV(p);
            ADD_3V(p, p, ctx->_EyeZDir);
            NORMALIZE_3FV(p);
            COPY_3V(value, p);
            value[3] = 1.0;
         }
         return;

      case STATE_PT_SCALE:
         value[0] = ctx->Pixel.RedScale;
         value[1] = ctx->Pixel.GreenScale;
         value[2] = ctx->Pixel.BlueScale;
         value[3] = ctx->Pixel.AlphaScale;
         return;

      case STATE_PT_BIAS:
         value[0] = ctx->Pixel.RedBias;
         value[1] = ctx->Pixel.GreenBias;
         value[2] = ctx->Pixel.BlueBias;
         value[3] = ctx->Pixel.AlphaBias;
         return;

      case STATE_FB_SIZE:
         value[0] = (GLfloat) (ctx->DrawBuffer->Width - 1);
         value[1] = (GLfloat) (ctx->DrawBuffer->Height - 1);
         value[2] = 0.0F;
         value[3] = 0.0F;
         return;

      case STATE_FB_WPOS_Y_TRANSFORM:
         /* A driver may negate this conditional by using ZW swizzle
          * instead of XY (based on e.g. some other state). */
         if (!ctx->DrawBuffer->FlipY) {
            /* Identity (XY) followed by flipping Y upside down (ZW). */
            value[0] = 1.0F;
            value[1] = 0.0F;
            value[2] = -1.0F;
            value[3] = _mesa_geometric_height(ctx->DrawBuffer);
         } else {
            /* Flipping Y upside down (XY) followed by identity (ZW). */
            value[0] = -1.0F;
            value[1] = _mesa_geometric_height(ctx->DrawBuffer);
            value[2] = 1.0F;
            value[3] = 0.0F;
         }
         return;

      case STATE_TCS_PATCH_VERTICES_IN:
         val[0].i = ctx->TessCtrlProgram.patch_vertices;
         return;

      case STATE_TES_PATCH_VERTICES_IN:
         if (ctx->TessCtrlProgram._Current)
            val[0].i = ctx->TessCtrlProgram._Current->info.tess.tcs_vertices_out;
         else
            val[0].i = ctx->TessCtrlProgram.patch_vertices;
         return;

      case STATE_ADVANCED_BLENDING_MODE:
         val[0].i = _mesa_get_advanced_blend_sh_constant(
                      ctx->Color.BlendEnabled, ctx->Color._AdvancedBlendMode);
         return;

      case STATE_ALPHA_REF:
         value[0] = ctx->Color.AlphaRefUnclamped;
         return;

      case STATE_CLIP_INTERNAL:
         {
            const GLuint plane = (GLuint) state[2];
            COPY_4V(value, ctx->Transform._ClipUserPlane[plane]);
         }
         return;

      /* XXX: make sure new tokens added here are also handled in the
       * _mesa_program_state_flags() switch, below.
       */
      default:
         /* Unknown state indexes are silently ignored here.
          * Drivers may do something special.
          */
         return;
      }
      return;

   case STATE_NOT_STATE_VAR:
      /* Most likely PROGRAM_CONSTANT. This only happens in rare cases, e.g.
       * ARB_vp with ARL, which can't sort parameters by type.
       */
      return;

   default:
      unreachable("Invalid state in _mesa_fetch_state");
      return;
   }
}

unsigned
_mesa_program_state_value_size(const gl_state_index16 state[STATE_LENGTH])
{
   if (state[0] == STATE_LIGHT && state[2] == STATE_SPOT_CUTOFF)
      return 1;

   /* Everything else is packed into vec4s */
   return 4;
}

/**
 * Return a bitmask of the Mesa state flags (_NEW_* values) which would
 * indicate that the given context state may have changed.
 * The bitmask is used during validation to determine if we need to update
 * vertex/fragment program parameters (like "state.material.color") when
 * some GL state has changed.
 */
GLbitfield
_mesa_program_state_flags(const gl_state_index16 state[STATE_LENGTH])
{
   switch (state[0]) {
   case STATE_MATERIAL:
   case STATE_LIGHTPROD:
   case STATE_LIGHTMODEL_SCENECOLOR:
      /* these can be effected by glColor when colormaterial mode is used */
      return _NEW_LIGHT | _NEW_CURRENT_ATTRIB;

   case STATE_LIGHT:
   case STATE_LIGHT_ATTRIBS:
   case STATE_LIGHTMODEL_AMBIENT:
      return _NEW_LIGHT;

   case STATE_TEXGEN:
      return _NEW_TEXTURE_STATE;
   case STATE_TEXENV_COLOR:
      return _NEW_TEXTURE_STATE | _NEW_BUFFERS | _NEW_FRAG_CLAMP;

   case STATE_FOG_COLOR:
      return _NEW_FOG | _NEW_BUFFERS | _NEW_FRAG_CLAMP;
   case STATE_FOG_PARAMS:
      return _NEW_FOG;

   case STATE_CLIPPLANE:
      return _NEW_TRANSFORM;

   case STATE_POINT_SIZE:
   case STATE_POINT_ATTENUATION:
      return _NEW_POINT;

   case STATE_MODELVIEW_MATRIX:
   case STATE_MODELVIEW_MATRIX_INVERSE:
   case STATE_MODELVIEW_MATRIX_TRANSPOSE:
   case STATE_MODELVIEW_MATRIX_INVTRANS:
      return _NEW_MODELVIEW;
   case STATE_PROJECTION_MATRIX:
   case STATE_PROJECTION_MATRIX_INVERSE:
   case STATE_PROJECTION_MATRIX_TRANSPOSE:
   case STATE_PROJECTION_MATRIX_INVTRANS:
      return _NEW_PROJECTION;
   case STATE_MVP_MATRIX:
   case STATE_MVP_MATRIX_INVERSE:
   case STATE_MVP_MATRIX_TRANSPOSE:
   case STATE_MVP_MATRIX_INVTRANS:
      return _NEW_MODELVIEW | _NEW_PROJECTION;
   case STATE_TEXTURE_MATRIX:
   case STATE_TEXTURE_MATRIX_INVERSE:
   case STATE_TEXTURE_MATRIX_TRANSPOSE:
   case STATE_TEXTURE_MATRIX_INVTRANS:
      return _NEW_TEXTURE_MATRIX;
   case STATE_PROGRAM_MATRIX:
   case STATE_PROGRAM_MATRIX_INVERSE:
   case STATE_PROGRAM_MATRIX_TRANSPOSE:
   case STATE_PROGRAM_MATRIX_INVTRANS:
      return _NEW_TRACK_MATRIX;

   case STATE_NUM_SAMPLES:
      return _NEW_BUFFERS;

   case STATE_DEPTH_RANGE:
      return _NEW_VIEWPORT;

   case STATE_FRAGMENT_PROGRAM:
   case STATE_VERTEX_PROGRAM:
      return _NEW_PROGRAM;

   case STATE_NORMAL_SCALE:
      return _NEW_MODELVIEW;

   case STATE_INTERNAL:
      switch (state[1]) {
      case STATE_CURRENT_ATTRIB:
         return _NEW_CURRENT_ATTRIB;
      case STATE_CURRENT_ATTRIB_MAYBE_VP_CLAMPED:
         return _NEW_CURRENT_ATTRIB | _NEW_LIGHT | _NEW_BUFFERS;

      case STATE_NORMAL_SCALE:
         return _NEW_MODELVIEW;

      case STATE_FOG_PARAMS_OPTIMIZED:
	 return _NEW_FOG;
      case STATE_POINT_SIZE_CLAMPED:
         return _NEW_POINT | _NEW_MULTISAMPLE;
      case STATE_LIGHT_SPOT_DIR_NORMALIZED:
      case STATE_LIGHT_POSITION:
      case STATE_LIGHT_POSITION_NORMALIZED:
      case STATE_LIGHT_HALF_VECTOR:
         return _NEW_LIGHT;

      case STATE_PT_SCALE:
      case STATE_PT_BIAS:
         return _NEW_PIXEL;

      case STATE_FB_SIZE:
      case STATE_FB_WPOS_Y_TRANSFORM:
         return _NEW_BUFFERS;

      case STATE_ADVANCED_BLENDING_MODE:
         return _NEW_COLOR;

      case STATE_ALPHA_REF:
         return _NEW_COLOR;

      case STATE_CLIP_INTERNAL:
         return _NEW_TRANSFORM | _NEW_PROJECTION;

      default:
         /* unknown state indexes are silently ignored and
         *  no flag set, since it is handled by the driver.
         */
	 return 0;
      }

   case STATE_NOT_STATE_VAR:
      return 0;

   default:
      _mesa_problem(NULL, "unexpected state[0] in make_state_flags()");
      return 0;
   }
}


static void
append(char *dst, const char *src)
{
   while (*dst)
      dst++;
   while (*src)
     *dst++ = *src++;
   *dst = 0;
}


/**
 * Convert token 'k' to a string, append it onto 'dst' string.
 */
static void
append_token(char *dst, gl_state_index k)
{
   switch (k) {
   case STATE_MATERIAL:
      append(dst, "material.");
      break;
   case STATE_LIGHT:
      append(dst, "light");
      break;
   case STATE_LIGHT_ATTRIBS:
      append(dst, "light.attribs");
      break;
   case STATE_LIGHTMODEL_AMBIENT:
      append(dst, "lightmodel.ambient");
      break;
   case STATE_LIGHTMODEL_SCENECOLOR:
      break;
   case STATE_LIGHTPROD:
      append(dst, "lightprod");
      break;
   case STATE_TEXGEN:
      append(dst, "texgen");
      break;
   case STATE_FOG_COLOR:
      append(dst, "fog.color");
      break;
   case STATE_FOG_PARAMS:
      append(dst, "fog.params");
      break;
   case STATE_CLIPPLANE:
      append(dst, "clip");
      break;
   case STATE_POINT_SIZE:
      append(dst, "point.size");
      break;
   case STATE_POINT_ATTENUATION:
      append(dst, "point.attenuation");
      break;
   case STATE_MODELVIEW_MATRIX:
      append(dst, "matrix.modelview.");
      break;
   case STATE_MODELVIEW_MATRIX_INVERSE:
      append(dst, "matrix.modelview.inverse.");
      break;
   case STATE_MODELVIEW_MATRIX_TRANSPOSE:
      append(dst, "matrix.modelview.transpose.");
      break;
   case STATE_MODELVIEW_MATRIX_INVTRANS:
      append(dst, "matrix.modelview.invtrans.");
      break;
   case STATE_PROJECTION_MATRIX:
      append(dst, "matrix.projection.");
      break;
   case STATE_PROJECTION_MATRIX_INVERSE:
      append(dst, "matrix.projection.inverse.");
      break;
   case STATE_PROJECTION_MATRIX_TRANSPOSE:
      append(dst, "matrix.projection.transpose.");
      break;
   case STATE_PROJECTION_MATRIX_INVTRANS:
      append(dst, "matrix.projection.invtrans.");
      break;
   case STATE_MVP_MATRIX:
      append(dst, "matrix.mvp.");
      break;
   case STATE_MVP_MATRIX_INVERSE:
      append(dst, "matrix.mvp.inverse.");
      break;
   case STATE_MVP_MATRIX_TRANSPOSE:
      append(dst, "matrix.mvp.transpose.");
      break;
   case STATE_MVP_MATRIX_INVTRANS:
      append(dst, "matrix.mvp.invtrans.");
      break;
   case STATE_TEXTURE_MATRIX:
      append(dst, "matrix.texture");
      break;
   case STATE_TEXTURE_MATRIX_INVERSE:
      append(dst, "matrix.texture.inverse");
      break;
   case STATE_TEXTURE_MATRIX_TRANSPOSE:
      append(dst, "matrix.texture.transpose");
      break;
   case STATE_TEXTURE_MATRIX_INVTRANS:
      append(dst, "matrix.texture.invtrans");
      break;
   case STATE_PROGRAM_MATRIX:
      append(dst, "matrix.program");
      break;
   case STATE_PROGRAM_MATRIX_INVERSE:
      append(dst, "matrix.program.inverse");
      break;
   case STATE_PROGRAM_MATRIX_TRANSPOSE:
      append(dst, "matrix.program.transpose");
      break;
   case STATE_PROGRAM_MATRIX_INVTRANS:
      append(dst, "matrix.program.invtrans");
      break;
      break;
   case STATE_AMBIENT:
      append(dst, "ambient");
      break;
   case STATE_DIFFUSE:
      append(dst, "diffuse");
      break;
   case STATE_SPECULAR:
      append(dst, "specular");
      break;
   case STATE_EMISSION:
      append(dst, "emission");
      break;
   case STATE_SHININESS:
      append(dst, "shininess");
      break;
   case STATE_HALF_VECTOR:
      append(dst, "half");
      break;
   case STATE_POSITION:
      append(dst, "position");
      break;
   case STATE_ATTENUATION:
      append(dst, "attenuation");
      break;
   case STATE_SPOT_DIRECTION:
      append(dst, "spot.direction");
      break;
   case STATE_SPOT_CUTOFF:
      append(dst, "spot.cutoff");
      break;
   case STATE_TEXGEN_EYE_S:
      append(dst, "eye.s");
      break;
   case STATE_TEXGEN_EYE_T:
      append(dst, "eye.t");
      break;
   case STATE_TEXGEN_EYE_R:
      append(dst, "eye.r");
      break;
   case STATE_TEXGEN_EYE_Q:
      append(dst, "eye.q");
      break;
   case STATE_TEXGEN_OBJECT_S:
      append(dst, "object.s");
      break;
   case STATE_TEXGEN_OBJECT_T:
      append(dst, "object.t");
      break;
   case STATE_TEXGEN_OBJECT_R:
      append(dst, "object.r");
      break;
   case STATE_TEXGEN_OBJECT_Q:
      append(dst, "object.q");
      break;
   case STATE_TEXENV_COLOR:
      append(dst, "texenv");
      break;
   case STATE_NUM_SAMPLES:
      append(dst, "numsamples");
      break;
   case STATE_DEPTH_RANGE:
      append(dst, "depth.range");
      break;
   case STATE_VERTEX_PROGRAM:
   case STATE_FRAGMENT_PROGRAM:
      break;
   case STATE_ENV:
      append(dst, "env");
      break;
   case STATE_LOCAL:
      append(dst, "local");
      break;
   /* BEGIN internal state vars */
   case STATE_INTERNAL:
      append(dst, "internal.");
      break;
   case STATE_CURRENT_ATTRIB:
      append(dst, "current");
      break;
   case STATE_CURRENT_ATTRIB_MAYBE_VP_CLAMPED:
      append(dst, "currentAttribMaybeVPClamped");
      break;
   case STATE_NORMAL_SCALE:
      append(dst, "normalScale");
      break;
   case STATE_FOG_PARAMS_OPTIMIZED:
      append(dst, "fogParamsOptimized");
      break;
   case STATE_POINT_SIZE_CLAMPED:
      append(dst, "pointSizeClamped");
      break;
   case STATE_LIGHT_SPOT_DIR_NORMALIZED:
      append(dst, "lightSpotDirNormalized");
      break;
   case STATE_LIGHT_POSITION:
      append(dst, "lightPosition");
      break;
   case STATE_LIGHT_POSITION_NORMALIZED:
      append(dst, "light.position.normalized");
      break;
   case STATE_LIGHT_HALF_VECTOR:
      append(dst, "lightHalfVector");
      break;
   case STATE_PT_SCALE:
      append(dst, "PTscale");
      break;
   case STATE_PT_BIAS:
      append(dst, "PTbias");
      break;
   case STATE_FB_SIZE:
      append(dst, "FbSize");
      break;
   case STATE_FB_WPOS_Y_TRANSFORM:
      append(dst, "FbWposYTransform");
      break;
   case STATE_ADVANCED_BLENDING_MODE:
      append(dst, "AdvancedBlendingMode");
      break;
   case STATE_ALPHA_REF:
      append(dst, "alphaRef");
      break;
   case STATE_CLIP_INTERNAL:
      append(dst, "clipInternal");
      break;
   default:
      /* probably STATE_INTERNAL_DRIVER+i (driver private state) */
      append(dst, "driverState");
   }
}

static void
append_face(char *dst, GLint face)
{
   if (face == 0)
      append(dst, "front.");
   else
      append(dst, "back.");
}

static void
append_index(char *dst, GLint index, bool structure)
{
   char s[20];
   sprintf(s, "[%d]%s", index, structure ? "." : "");
   append(dst, s);
}

/**
 * Make a string from the given state vector.
 * For example, return "state.matrix.texture[2].inverse".
 * Use free() to deallocate the string.
 */
char *
_mesa_program_state_string(const gl_state_index16 state[STATE_LENGTH])
{
   char str[1000] = "";
   char tmp[30];

   append(str, "state.");
   append_token(str, state[0]);

   switch (state[0]) {
   case STATE_MATERIAL:
      append_face(str, state[1]);
      append_token(str, state[2]);
      break;
   case STATE_LIGHT:
      append_index(str, state[1], true); /* light number [i]. */
      append_token(str, state[2]); /* coefficients */
      break;
   case STATE_LIGHT_ATTRIBS:
      sprintf(tmp, "[%d..%d]", state[1], state[1] + state[2] - 1);
      append(str, tmp);
      break;
   case STATE_LIGHTMODEL_AMBIENT:
      break;
   case STATE_LIGHTMODEL_SCENECOLOR:
      if (state[1] == 0) {
         append(str, "lightmodel.front.scenecolor");
      }
      else {
         append(str, "lightmodel.back.scenecolor");
      }
      break;
   case STATE_LIGHTPROD:
      append_index(str, state[1], true); /* light number [i]. */
      append_face(str, state[2]);
      append_token(str, state[3]);
      break;
   case STATE_TEXGEN:
      append_index(str, state[1], true); /* tex unit [i] */
      append_token(str, state[2]); /* plane coef */
      break;
   case STATE_TEXENV_COLOR:
      append_index(str, state[1], true); /* tex unit [i] */
      append(str, "color");
      break;
   case STATE_CLIPPLANE:
      append_index(str, state[1], true); /* plane [i] */
      append(str, "plane");
      break;
   case STATE_MODELVIEW_MATRIX:
   case STATE_MODELVIEW_MATRIX_INVERSE:
   case STATE_MODELVIEW_MATRIX_TRANSPOSE:
   case STATE_MODELVIEW_MATRIX_INVTRANS:
   case STATE_PROJECTION_MATRIX:
   case STATE_PROJECTION_MATRIX_INVERSE:
   case STATE_PROJECTION_MATRIX_TRANSPOSE:
   case STATE_PROJECTION_MATRIX_INVTRANS:
   case STATE_MVP_MATRIX:
   case STATE_MVP_MATRIX_INVERSE:
   case STATE_MVP_MATRIX_TRANSPOSE:
   case STATE_MVP_MATRIX_INVTRANS:
   case STATE_TEXTURE_MATRIX:
   case STATE_TEXTURE_MATRIX_INVERSE:
   case STATE_TEXTURE_MATRIX_TRANSPOSE:
   case STATE_TEXTURE_MATRIX_INVTRANS:
   case STATE_PROGRAM_MATRIX:
   case STATE_PROGRAM_MATRIX_INVERSE:
   case STATE_PROGRAM_MATRIX_TRANSPOSE:
   case STATE_PROGRAM_MATRIX_INVTRANS:
      {
         /* state[0] = modelview, projection, texture, etc. */
         /* state[1] = which texture matrix or program matrix */
         /* state[2] = first row to fetch */
         /* state[3] = last row to fetch */
         const gl_state_index mat = state[0];
         const GLuint index = (GLuint) state[1];
         const GLuint firstRow = (GLuint) state[2];
         const GLuint lastRow = (GLuint) state[3];
         if (index ||
             (mat >= STATE_TEXTURE_MATRIX &&
              mat <= STATE_PROGRAM_MATRIX_INVTRANS))
            append_index(str, index, true);
         if (firstRow == lastRow)
            sprintf(tmp, "row[%d]", firstRow);
         else
            sprintf(tmp, "row[%d..%d]", firstRow, lastRow);
         append(str, tmp);
      }
      break;
   case STATE_POINT_SIZE:
      break;
   case STATE_POINT_ATTENUATION:
      break;
   case STATE_FOG_PARAMS:
      break;
   case STATE_FOG_COLOR:
      break;
   case STATE_NUM_SAMPLES:
      break;
   case STATE_DEPTH_RANGE:
      break;
   case STATE_FRAGMENT_PROGRAM:
   case STATE_VERTEX_PROGRAM:
      /* state[1] = {STATE_ENV, STATE_LOCAL} */
      /* state[2] = parameter index          */
      append_token(str, state[1]);
      append_index(str, state[2], false);
      break;
   case STATE_NORMAL_SCALE:
      break;
   case STATE_INTERNAL:
      append_token(str, state[1]);
      if (state[1] == STATE_CURRENT_ATTRIB ||
          state[1] == STATE_CURRENT_ATTRIB_MAYBE_VP_CLAMPED)
         append_index(str, state[2], false);
       break;
   case STATE_NOT_STATE_VAR:
      append(str, "not_state");
      break;
   default:
      _mesa_problem(NULL, "Invalid state in _mesa_program_state_string");
      break;
   }

   return strdup(str);
}


/**
 * Loop over all the parameters in a parameter list.  If the parameter
 * is a GL state reference, look up the current value of that state
 * variable and put it into the parameter's Value[4] array.
 * Other parameter types never change or are explicitly set by the user
 * with glUniform() or glProgramParameter(), etc.
 * This would be called at glBegin time.
 */
void
_mesa_load_state_parameters(struct gl_context *ctx,
                            struct gl_program_parameter_list *paramList)
{
   if (!paramList)
      return;

   int num = paramList->NumParameters;

   for (int i = paramList->FirstStateVarIndex; i < num; i++) {
      unsigned pvo = paramList->Parameters[i].ValueOffset;
      fetch_state(ctx, paramList->Parameters[i].StateIndexes,
                  paramList->ParameterValues + pvo);
   }
}

void
_mesa_upload_state_parameters(struct gl_context *ctx,
                              struct gl_program_parameter_list *paramList,
                              uint32_t *dst)
{
   int num = paramList->NumParameters;

   for (int i = paramList->FirstStateVarIndex; i < num; i++) {
      unsigned pvo = paramList->Parameters[i].ValueOffset;
      fetch_state(ctx, paramList->Parameters[i].StateIndexes,
                  (gl_constant_value*)(dst + pvo));
   }
}

/* Merge consecutive state vars into one for the state vars that allow
 * multiple vec4s.
 *
 * This should be done after shader compilation, so that drivers don't
 * have to deal with multi-slot state parameters in their backends.
 * It's only meant to optimize _mesa_load/upload_state_parameters.
 */
void
_mesa_optimize_state_parameters(struct gl_program_parameter_list *list)
{
   for (int first_param = list->FirstStateVarIndex;
        first_param < (int)list->NumParameters; first_param++) {
      int last_param = first_param;
      int param_diff = 0;

      switch (list->Parameters[first_param].StateIndexes[0]) {
      case STATE_MODELVIEW_MATRIX:
      case STATE_MODELVIEW_MATRIX_INVERSE:
      case STATE_MODELVIEW_MATRIX_TRANSPOSE:
      case STATE_MODELVIEW_MATRIX_INVTRANS:
      case STATE_PROJECTION_MATRIX:
      case STATE_PROJECTION_MATRIX_INVERSE:
      case STATE_PROJECTION_MATRIX_TRANSPOSE:
      case STATE_PROJECTION_MATRIX_INVTRANS:
      case STATE_MVP_MATRIX:
      case STATE_MVP_MATRIX_INVERSE:
      case STATE_MVP_MATRIX_TRANSPOSE:
      case STATE_MVP_MATRIX_INVTRANS:
      case STATE_TEXTURE_MATRIX:
      case STATE_TEXTURE_MATRIX_INVERSE:
      case STATE_TEXTURE_MATRIX_TRANSPOSE:
      case STATE_TEXTURE_MATRIX_INVTRANS:
      case STATE_PROGRAM_MATRIX:
      case STATE_PROGRAM_MATRIX_INVERSE:
      case STATE_PROGRAM_MATRIX_TRANSPOSE:
      case STATE_PROGRAM_MATRIX_INVTRANS:
         /* Skip unaligned state vars. */
         if (list->Parameters[first_param].Size % 4)
            break;

         /* Search for adjacent state vars that refer to adjacent rows. */
         for (int i = first_param + 1; i < (int)list->NumParameters; i++) {
            if (list->Parameters[i].StateIndexes[0] ==
                list->Parameters[i - 1].StateIndexes[0] &&
                list->Parameters[i].StateIndexes[1] ==
                list->Parameters[i - 1].StateIndexes[1] &&
                list->Parameters[i].StateIndexes[2] ==         /* FirstRow */
                list->Parameters[i - 1].StateIndexes[3] + 1 && /* LastRow + 1 */
                list->Parameters[i].Size == 4) {
               last_param = i;
               continue;
            }
            break; /* The adjacent state var is incompatible. */
         }
         if (last_param > first_param) {
            int first_vec = list->Parameters[first_param].StateIndexes[2];
            int last_vec = list->Parameters[last_param].StateIndexes[3];

            assert(first_vec < last_vec);
            assert(last_vec - first_vec == last_param - first_param);

            /* Update LastRow. */
            list->Parameters[first_param].StateIndexes[3] = last_vec;
            list->Parameters[first_param].Size = (last_vec - first_vec + 1) * 4;

            param_diff = last_param - first_param;
         }
         break;

      case STATE_LIGHT:
         /* Skip trimmed state vars. (this shouldn't occur though) */
         if (list->Parameters[first_param].Size !=
             _mesa_program_state_value_size(list->Parameters[first_param].StateIndexes))
            break;

         /* Search for light attributes that are adjacent in memory. */
         for (int i = first_param + 1; i < (int)list->NumParameters; i++) {
            if (list->Parameters[i].StateIndexes[0] == STATE_LIGHT &&
                /* Consecutive attributes of the same light: */
                ((list->Parameters[i].StateIndexes[1] ==
                  list->Parameters[i - 1].StateIndexes[1] &&
                  list->Parameters[i].StateIndexes[2] ==
                  list->Parameters[i - 1].StateIndexes[2] + 1) ||
                 /* Consecutive attributes between 2 lights: */
                 (list->Parameters[i].StateIndexes[1] ==
                  list->Parameters[i - 1].StateIndexes[1] + 1 &&
                  list->Parameters[i].StateIndexes[2] == STATE_AMBIENT &&
                  list->Parameters[i - 1].StateIndexes[2] == STATE_SPOT_CUTOFF))) {
               last_param = i;
               continue;
            }
            break; /* The adjacent state var is incompatible. */
         }
         if (last_param > first_param) {
            /* Convert the state var to STATE_LIGHT_ATTRIBS. */
            list->Parameters[first_param].StateIndexes[0] = STATE_LIGHT_ATTRIBS;
            /* Set the offset in floats. */
            list->Parameters[first_param].StateIndexes[1] =
               list->Parameters[first_param].StateIndexes[1] * /* light index */
               sizeof(struct gl_light_uniforms) / 4 +
               (list->Parameters[first_param].StateIndexes[2] - STATE_AMBIENT) * 4;
            /* Set the size in floats. */
            list->Parameters[first_param].StateIndexes[2] =
            list->Parameters[first_param].Size =
               list->Parameters[last_param].Size +
               list->Parameters[last_param].ValueOffset -
               list->Parameters[first_param].ValueOffset;

            param_diff = last_param - first_param;
         }
         break;
      }

      if (param_diff) {
         /* Update the name. */
         free((void*)list->Parameters[first_param].Name);
         list->Parameters[first_param].Name =
            _mesa_program_state_string(list->Parameters[first_param].StateIndexes);

         /* Free names that we are going to overwrite. */
         for (int i = first_param + 1; i <= last_param; i++)
            free((char*)list->Parameters[i].Name);

         /* Remove the merged state vars. */
         memmove(&list->Parameters[first_param + 1],
                 &list->Parameters[last_param + 1],
                 sizeof(list->Parameters[0]) *
                 (list->NumParameters - last_param - 1));
         list->NumParameters -= param_diff;
      }
   }
}
