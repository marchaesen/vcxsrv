/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2004  Brian Paul   All Rights Reserved.
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
 *
 * Authors:
 *    Keith Whitwell <keithw@vmware.com> Gareth Hughes
 */

#include "glheader.h"
#include "api_arrayelt.h"
#include "context.h"

#include "mtypes.h"
#include "vtxfmt.h"
#include "eval.h"
#include "dlist.h"
#include "main/dispatch.h"
#include "vbo/vbo.h"


/**
 * Copy the functions found in the GLvertexformat object into the
 * dispatch table.
 */
static void
install_vtxfmt(struct gl_context *ctx, struct _glapi_table *tab,
               const GLvertexformat *vfmt)
{
   assert(ctx->Version > 0);

   if (ctx->API != API_OPENGL_CORE && ctx->API != API_OPENGLES2) {
      SET_Color4f(tab, vfmt->Color4f);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      _mesa_install_arrayelt_vtxfmt(tab, vfmt);
      SET_Color3f(tab, vfmt->Color3f);
      SET_Color3fv(tab, vfmt->Color3fv);
      SET_Color4fv(tab, vfmt->Color4fv);
      SET_EdgeFlag(tab, vfmt->EdgeFlag);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      _mesa_install_eval_vtxfmt(tab, vfmt);
   }

   if (ctx->API != API_OPENGL_CORE && ctx->API != API_OPENGLES2) {
      SET_Materialfv(tab, vfmt->Materialfv);
      SET_MultiTexCoord4fARB(tab, vfmt->MultiTexCoord4fARB);
      SET_Normal3f(tab, vfmt->Normal3f);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      SET_FogCoordfEXT(tab, vfmt->FogCoordfEXT);
      SET_FogCoordfvEXT(tab, vfmt->FogCoordfvEXT);
      SET_Indexf(tab, vfmt->Indexf);
      SET_Indexfv(tab, vfmt->Indexfv);
      SET_MultiTexCoord1fARB(tab, vfmt->MultiTexCoord1fARB);
      SET_MultiTexCoord1fvARB(tab, vfmt->MultiTexCoord1fvARB);
      SET_MultiTexCoord2fARB(tab, vfmt->MultiTexCoord2fARB);
      SET_MultiTexCoord2fvARB(tab, vfmt->MultiTexCoord2fvARB);
      SET_MultiTexCoord3fARB(tab, vfmt->MultiTexCoord3fARB);
      SET_MultiTexCoord3fvARB(tab, vfmt->MultiTexCoord3fvARB);
      SET_MultiTexCoord4fvARB(tab, vfmt->MultiTexCoord4fvARB);
      SET_Normal3fv(tab, vfmt->Normal3fv);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      SET_SecondaryColor3fEXT(tab, vfmt->SecondaryColor3fEXT);
      SET_SecondaryColor3fvEXT(tab, vfmt->SecondaryColor3fvEXT);
      SET_TexCoord1f(tab, vfmt->TexCoord1f);
      SET_TexCoord1fv(tab, vfmt->TexCoord1fv);
      SET_TexCoord2f(tab, vfmt->TexCoord2f);
      SET_TexCoord2fv(tab, vfmt->TexCoord2fv);
      SET_TexCoord3f(tab, vfmt->TexCoord3f);
      SET_TexCoord3fv(tab, vfmt->TexCoord3fv);
      SET_TexCoord4f(tab, vfmt->TexCoord4f);
      SET_TexCoord4fv(tab, vfmt->TexCoord4fv);
      SET_Vertex2f(tab, vfmt->Vertex2f);
      SET_Vertex2fv(tab, vfmt->Vertex2fv);
      SET_Vertex3f(tab, vfmt->Vertex3f);
      SET_Vertex3fv(tab, vfmt->Vertex3fv);
      SET_Vertex4f(tab, vfmt->Vertex4f);
      SET_Vertex4fv(tab, vfmt->Vertex4fv);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      _mesa_install_dlist_vtxfmt(tab, vfmt);   /* glCallList / glCallLists */

      SET_Begin(tab, vfmt->Begin);
      SET_End(tab, vfmt->End);
      SET_PrimitiveRestartNV(tab, vfmt->PrimitiveRestartNV);
   }

   /* Originally for GL_NV_vertex_program, this is now only used by dlist.c */
   if (ctx->API == API_OPENGL_COMPAT) {
      SET_VertexAttrib1fNV(tab, vfmt->VertexAttrib1fNV);
      SET_VertexAttrib1fvNV(tab, vfmt->VertexAttrib1fvNV);
      SET_VertexAttrib2fNV(tab, vfmt->VertexAttrib2fNV);
      SET_VertexAttrib2fvNV(tab, vfmt->VertexAttrib2fvNV);
      SET_VertexAttrib3fNV(tab, vfmt->VertexAttrib3fNV);
      SET_VertexAttrib3fvNV(tab, vfmt->VertexAttrib3fvNV);
      SET_VertexAttrib4fNV(tab, vfmt->VertexAttrib4fNV);
      SET_VertexAttrib4fvNV(tab, vfmt->VertexAttrib4fvNV);
   }

   if (ctx->API != API_OPENGLES) {
      SET_VertexAttrib1fARB(tab, vfmt->VertexAttrib1fARB);
      SET_VertexAttrib1fvARB(tab, vfmt->VertexAttrib1fvARB);
      SET_VertexAttrib2fARB(tab, vfmt->VertexAttrib2fARB);
      SET_VertexAttrib2fvARB(tab, vfmt->VertexAttrib2fvARB);
      SET_VertexAttrib3fARB(tab, vfmt->VertexAttrib3fARB);
      SET_VertexAttrib3fvARB(tab, vfmt->VertexAttrib3fvARB);
      SET_VertexAttrib4fARB(tab, vfmt->VertexAttrib4fARB);
      SET_VertexAttrib4fvARB(tab, vfmt->VertexAttrib4fvARB);
   }

   /* GL_EXT_gpu_shader4 / OpenGL 3.0 */
   if (_mesa_is_desktop_gl(ctx)) {
      SET_VertexAttribI1iEXT(tab, vfmt->VertexAttribI1i);
      SET_VertexAttribI2iEXT(tab, vfmt->VertexAttribI2i);
      SET_VertexAttribI3iEXT(tab, vfmt->VertexAttribI3i);
      SET_VertexAttribI2ivEXT(tab, vfmt->VertexAttribI2iv);
      SET_VertexAttribI3ivEXT(tab, vfmt->VertexAttribI3iv);

      SET_VertexAttribI1uiEXT(tab, vfmt->VertexAttribI1ui);
      SET_VertexAttribI2uiEXT(tab, vfmt->VertexAttribI2ui);
      SET_VertexAttribI3uiEXT(tab, vfmt->VertexAttribI3ui);
      SET_VertexAttribI2uivEXT(tab, vfmt->VertexAttribI2uiv);
      SET_VertexAttribI3uivEXT(tab, vfmt->VertexAttribI3uiv);
   }

   if (_mesa_is_desktop_gl(ctx) || _mesa_is_gles3(ctx)) {
      SET_VertexAttribI4iEXT(tab, vfmt->VertexAttribI4i);
      SET_VertexAttribI4ivEXT(tab, vfmt->VertexAttribI4iv);
      SET_VertexAttribI4uiEXT(tab, vfmt->VertexAttribI4ui);
      SET_VertexAttribI4uivEXT(tab, vfmt->VertexAttribI4uiv);
   }

   if (ctx->API == API_OPENGL_COMPAT) {
      /* GL_ARB_vertex_type_10_10_10_2_rev / GL 3.3 */
      SET_VertexP2ui(tab, vfmt->VertexP2ui);
      SET_VertexP2uiv(tab, vfmt->VertexP2uiv);
      SET_VertexP3ui(tab, vfmt->VertexP3ui);
      SET_VertexP3uiv(tab, vfmt->VertexP3uiv);
      SET_VertexP4ui(tab, vfmt->VertexP4ui);
      SET_VertexP4uiv(tab, vfmt->VertexP4uiv);

      SET_TexCoordP1ui(tab, vfmt->TexCoordP1ui);
      SET_TexCoordP1uiv(tab, vfmt->TexCoordP1uiv);
      SET_TexCoordP2ui(tab, vfmt->TexCoordP2ui);
      SET_TexCoordP2uiv(tab, vfmt->TexCoordP2uiv);
      SET_TexCoordP3ui(tab, vfmt->TexCoordP3ui);
      SET_TexCoordP3uiv(tab, vfmt->TexCoordP3uiv);
      SET_TexCoordP4ui(tab, vfmt->TexCoordP4ui);
      SET_TexCoordP4uiv(tab, vfmt->TexCoordP4uiv);

      SET_MultiTexCoordP1ui(tab, vfmt->MultiTexCoordP1ui);
      SET_MultiTexCoordP2ui(tab, vfmt->MultiTexCoordP2ui);
      SET_MultiTexCoordP3ui(tab, vfmt->MultiTexCoordP3ui);
      SET_MultiTexCoordP4ui(tab, vfmt->MultiTexCoordP4ui);
      SET_MultiTexCoordP1uiv(tab, vfmt->MultiTexCoordP1uiv);
      SET_MultiTexCoordP2uiv(tab, vfmt->MultiTexCoordP2uiv);
      SET_MultiTexCoordP3uiv(tab, vfmt->MultiTexCoordP3uiv);
      SET_MultiTexCoordP4uiv(tab, vfmt->MultiTexCoordP4uiv);

      SET_NormalP3ui(tab, vfmt->NormalP3ui);
      SET_NormalP3uiv(tab, vfmt->NormalP3uiv);

      SET_ColorP3ui(tab, vfmt->ColorP3ui);
      SET_ColorP4ui(tab, vfmt->ColorP4ui);
      SET_ColorP3uiv(tab, vfmt->ColorP3uiv);
      SET_ColorP4uiv(tab, vfmt->ColorP4uiv);

      SET_SecondaryColorP3ui(tab, vfmt->SecondaryColorP3ui);
      SET_SecondaryColorP3uiv(tab, vfmt->SecondaryColorP3uiv);

      /* GL_NV_half_float */
      SET_Vertex2hNV(tab, vfmt->Vertex2hNV);
      SET_Vertex2hvNV(tab, vfmt->Vertex2hvNV);
      SET_Vertex3hNV(tab, vfmt->Vertex3hNV);
      SET_Vertex3hvNV(tab, vfmt->Vertex3hvNV);
      SET_Vertex4hNV(tab, vfmt->Vertex4hNV);
      SET_Vertex4hvNV(tab, vfmt->Vertex4hvNV);
      SET_Normal3hNV(tab, vfmt->Normal3hNV);
      SET_Normal3hvNV(tab, vfmt->Normal3hvNV);
      SET_Color3hNV(tab, vfmt->Color3hNV);
      SET_Color3hvNV(tab, vfmt->Color4hvNV);
      SET_Color4hNV(tab, vfmt->Color4hNV);
      SET_Color4hvNV(tab, vfmt->Color3hvNV);
      SET_TexCoord1hNV(tab, vfmt->TexCoord1hNV);
      SET_TexCoord1hvNV(tab, vfmt->TexCoord1hvNV);
      SET_TexCoord2hNV(tab, vfmt->TexCoord2hNV);
      SET_TexCoord2hvNV(tab, vfmt->TexCoord2hvNV);
      SET_TexCoord3hNV(tab, vfmt->TexCoord3hNV);
      SET_TexCoord3hvNV(tab, vfmt->TexCoord3hvNV);
      SET_TexCoord4hNV(tab, vfmt->TexCoord4hNV);
      SET_TexCoord4hvNV(tab, vfmt->TexCoord4hvNV);
      SET_MultiTexCoord1hNV(tab, vfmt->MultiTexCoord1hNV);
      SET_MultiTexCoord1hvNV(tab, vfmt->MultiTexCoord1hvNV);
      SET_MultiTexCoord2hNV(tab, vfmt->MultiTexCoord2hNV);
      SET_MultiTexCoord2hvNV(tab, vfmt->MultiTexCoord2hvNV);
      SET_MultiTexCoord3hNV(tab, vfmt->MultiTexCoord3hNV);
      SET_MultiTexCoord3hvNV(tab, vfmt->MultiTexCoord3hvNV);
      SET_MultiTexCoord4hNV(tab, vfmt->MultiTexCoord4hNV);
      SET_MultiTexCoord4hvNV(tab, vfmt->MultiTexCoord4hvNV);
      SET_FogCoordhNV(tab, vfmt->FogCoordhNV);
      SET_FogCoordhvNV(tab, vfmt->FogCoordhvNV);
      SET_SecondaryColor3hNV(tab, vfmt->SecondaryColor3hNV);
      SET_SecondaryColor3hvNV(tab, vfmt->SecondaryColor3hvNV);
   }

   if (_mesa_is_desktop_gl(ctx)) {
      SET_VertexAttribP1ui(tab, vfmt->VertexAttribP1ui);
      SET_VertexAttribP2ui(tab, vfmt->VertexAttribP2ui);
      SET_VertexAttribP3ui(tab, vfmt->VertexAttribP3ui);
      SET_VertexAttribP4ui(tab, vfmt->VertexAttribP4ui);

      SET_VertexAttribP1uiv(tab, vfmt->VertexAttribP1uiv);
      SET_VertexAttribP2uiv(tab, vfmt->VertexAttribP2uiv);
      SET_VertexAttribP3uiv(tab, vfmt->VertexAttribP3uiv);
      SET_VertexAttribP4uiv(tab, vfmt->VertexAttribP4uiv);

      /* GL_ARB_bindless_texture */
      SET_VertexAttribL1ui64ARB(tab, vfmt->VertexAttribL1ui64ARB);
      SET_VertexAttribL1ui64vARB(tab, vfmt->VertexAttribL1ui64vARB);
   }

   if (_mesa_is_desktop_gl(ctx)) {
      /* GL_ARB_vertex_attrib_64bit */
      SET_VertexAttribL1d(tab, vfmt->VertexAttribL1d);
      SET_VertexAttribL2d(tab, vfmt->VertexAttribL2d);
      SET_VertexAttribL3d(tab, vfmt->VertexAttribL3d);
      SET_VertexAttribL4d(tab, vfmt->VertexAttribL4d);

      SET_VertexAttribL1dv(tab, vfmt->VertexAttribL1dv);
      SET_VertexAttribL2dv(tab, vfmt->VertexAttribL2dv);
      SET_VertexAttribL3dv(tab, vfmt->VertexAttribL3dv);
      SET_VertexAttribL4dv(tab, vfmt->VertexAttribL4dv);
   }

   if (ctx->API != API_OPENGL_CORE && ctx->API != API_OPENGLES2) {
      SET_Color4ub(tab, vfmt->Color4ub);
      SET_Materialf(tab, vfmt->Materialf);
   }
   if (ctx->API == API_OPENGL_COMPAT) {
      SET_Color3b(tab, vfmt->Color3b);
      SET_Color3d(tab, vfmt->Color3d);
      SET_Color3i(tab, vfmt->Color3i);
      SET_Color3s(tab, vfmt->Color3s);
      SET_Color3ui(tab, vfmt->Color3ui);
      SET_Color3us(tab, vfmt->Color3us);
      SET_Color3ub(tab, vfmt->Color3ub);
      SET_Color4b(tab, vfmt->Color4b);
      SET_Color4d(tab, vfmt->Color4d);
      SET_Color4i(tab, vfmt->Color4i);
      SET_Color4s(tab, vfmt->Color4s);
      SET_Color4ui(tab, vfmt->Color4ui);
      SET_Color4us(tab, vfmt->Color4us);
      SET_Color3bv(tab, vfmt->Color3bv);
      SET_Color3dv(tab, vfmt->Color3dv);
      SET_Color3iv(tab, vfmt->Color3iv);
      SET_Color3sv(tab, vfmt->Color3sv);
      SET_Color3uiv(tab, vfmt->Color3uiv);
      SET_Color3usv(tab, vfmt->Color3usv);
      SET_Color3ubv(tab, vfmt->Color3ubv);
      SET_Color4bv(tab, vfmt->Color4bv);
      SET_Color4dv(tab, vfmt->Color4dv);
      SET_Color4iv(tab, vfmt->Color4iv);
      SET_Color4sv(tab, vfmt->Color4sv);
      SET_Color4uiv(tab, vfmt->Color4uiv);
      SET_Color4usv(tab, vfmt->Color4usv);
      SET_Color4ubv(tab, vfmt->Color4ubv);

      SET_SecondaryColor3b(tab, vfmt->SecondaryColor3b);
      SET_SecondaryColor3d(tab, vfmt->SecondaryColor3d);
      SET_SecondaryColor3i(tab, vfmt->SecondaryColor3i);
      SET_SecondaryColor3s(tab, vfmt->SecondaryColor3s);
      SET_SecondaryColor3ui(tab, vfmt->SecondaryColor3ui);
      SET_SecondaryColor3us(tab, vfmt->SecondaryColor3us);
      SET_SecondaryColor3ub(tab, vfmt->SecondaryColor3ub);
      SET_SecondaryColor3bv(tab, vfmt->SecondaryColor3bv);
      SET_SecondaryColor3dv(tab, vfmt->SecondaryColor3dv);
      SET_SecondaryColor3iv(tab, vfmt->SecondaryColor3iv);
      SET_SecondaryColor3sv(tab, vfmt->SecondaryColor3sv);
      SET_SecondaryColor3uiv(tab, vfmt->SecondaryColor3uiv);
      SET_SecondaryColor3usv(tab, vfmt->SecondaryColor3usv);
      SET_SecondaryColor3ubv(tab, vfmt->SecondaryColor3ubv);

      SET_EdgeFlagv(tab, vfmt->EdgeFlagv);

      SET_Indexd(tab, vfmt->Indexd);
      SET_Indexi(tab, vfmt->Indexi);
      SET_Indexs(tab, vfmt->Indexs);
      SET_Indexub(tab, vfmt->Indexub);
      SET_Indexdv(tab, vfmt->Indexdv);
      SET_Indexiv(tab, vfmt->Indexiv);
      SET_Indexsv(tab, vfmt->Indexsv);
      SET_Indexubv(tab, vfmt->Indexubv);
      SET_Normal3b(tab, vfmt->Normal3b);
      SET_Normal3d(tab, vfmt->Normal3d);
      SET_Normal3i(tab, vfmt->Normal3i);
      SET_Normal3s(tab, vfmt->Normal3s);
      SET_Normal3bv(tab, vfmt->Normal3bv);
      SET_Normal3dv(tab, vfmt->Normal3dv);
      SET_Normal3iv(tab, vfmt->Normal3iv);
      SET_Normal3sv(tab, vfmt->Normal3sv);
      SET_TexCoord1d(tab, vfmt->TexCoord1d);
      SET_TexCoord1i(tab, vfmt->TexCoord1i);
      SET_TexCoord1s(tab, vfmt->TexCoord1s);
      SET_TexCoord2d(tab, vfmt->TexCoord2d);
      SET_TexCoord2s(tab, vfmt->TexCoord2s);
      SET_TexCoord2i(tab, vfmt->TexCoord2i);
      SET_TexCoord3d(tab, vfmt->TexCoord3d);
      SET_TexCoord3i(tab, vfmt->TexCoord3i);
      SET_TexCoord3s(tab, vfmt->TexCoord3s);
      SET_TexCoord4d(tab, vfmt->TexCoord4d);
      SET_TexCoord4i(tab, vfmt->TexCoord4i);
      SET_TexCoord4s(tab, vfmt->TexCoord4s);
      SET_TexCoord1dv(tab, vfmt->TexCoord1dv);
      SET_TexCoord1iv(tab, vfmt->TexCoord1iv);
      SET_TexCoord1sv(tab, vfmt->TexCoord1sv);
      SET_TexCoord2dv(tab, vfmt->TexCoord2dv);
      SET_TexCoord2iv(tab, vfmt->TexCoord2iv);
      SET_TexCoord2sv(tab, vfmt->TexCoord2sv);
      SET_TexCoord3dv(tab, vfmt->TexCoord3dv);
      SET_TexCoord3iv(tab, vfmt->TexCoord3iv);
      SET_TexCoord3sv(tab, vfmt->TexCoord3sv);
      SET_TexCoord4dv(tab, vfmt->TexCoord4dv);
      SET_TexCoord4iv(tab, vfmt->TexCoord4iv);
      SET_TexCoord4sv(tab, vfmt->TexCoord4sv);
      SET_Vertex2d(tab, vfmt->Vertex2d);
      SET_Vertex2i(tab, vfmt->Vertex2i);
      SET_Vertex2s(tab, vfmt->Vertex2s);
      SET_Vertex3d(tab, vfmt->Vertex3d);
      SET_Vertex3i(tab, vfmt->Vertex3i);
      SET_Vertex3s(tab, vfmt->Vertex3s);
      SET_Vertex4d(tab, vfmt->Vertex4d);
      SET_Vertex4i(tab, vfmt->Vertex4i);
      SET_Vertex4s(tab, vfmt->Vertex4s);
      SET_Vertex2dv(tab, vfmt->Vertex2dv);
      SET_Vertex2iv(tab, vfmt->Vertex2iv);
      SET_Vertex2sv(tab, vfmt->Vertex2sv);
      SET_Vertex3dv(tab, vfmt->Vertex3dv);
      SET_Vertex3iv(tab, vfmt->Vertex3iv);
      SET_Vertex3sv(tab, vfmt->Vertex3sv);
      SET_Vertex4dv(tab, vfmt->Vertex4dv);
      SET_Vertex4iv(tab, vfmt->Vertex4iv);
      SET_Vertex4sv(tab, vfmt->Vertex4sv);
      SET_MultiTexCoord1d(tab, vfmt->MultiTexCoord1d);
      SET_MultiTexCoord1dv(tab, vfmt->MultiTexCoord1dv);
      SET_MultiTexCoord1i(tab, vfmt->MultiTexCoord1i);
      SET_MultiTexCoord1iv(tab, vfmt->MultiTexCoord1iv);
      SET_MultiTexCoord1s(tab, vfmt->MultiTexCoord1s);
      SET_MultiTexCoord1sv(tab, vfmt->MultiTexCoord1sv);
      SET_MultiTexCoord2d(tab, vfmt->MultiTexCoord2d);
      SET_MultiTexCoord2dv(tab, vfmt->MultiTexCoord2dv);
      SET_MultiTexCoord2i(tab, vfmt->MultiTexCoord2i);
      SET_MultiTexCoord2iv(tab, vfmt->MultiTexCoord2iv);
      SET_MultiTexCoord2s(tab, vfmt->MultiTexCoord2s);
      SET_MultiTexCoord2sv(tab, vfmt->MultiTexCoord2sv);
      SET_MultiTexCoord3d(tab, vfmt->MultiTexCoord3d);
      SET_MultiTexCoord3dv(tab, vfmt->MultiTexCoord3dv);
      SET_MultiTexCoord3i(tab, vfmt->MultiTexCoord3i);
      SET_MultiTexCoord3iv(tab, vfmt->MultiTexCoord3iv);
      SET_MultiTexCoord3s(tab, vfmt->MultiTexCoord3s);
      SET_MultiTexCoord3sv(tab, vfmt->MultiTexCoord3sv);
      SET_MultiTexCoord4d(tab, vfmt->MultiTexCoord4d);
      SET_MultiTexCoord4dv(tab, vfmt->MultiTexCoord4dv);
      SET_MultiTexCoord4i(tab, vfmt->MultiTexCoord4i);
      SET_MultiTexCoord4iv(tab, vfmt->MultiTexCoord4iv);
      SET_MultiTexCoord4s(tab, vfmt->MultiTexCoord4s);
      SET_MultiTexCoord4sv(tab, vfmt->MultiTexCoord4sv);
      SET_EvalCoord2dv(tab, vfmt->EvalCoord2dv);
      SET_EvalCoord2d(tab, vfmt->EvalCoord2d);
      SET_EvalCoord1dv(tab, vfmt->EvalCoord1dv);
      SET_EvalCoord1d(tab, vfmt->EvalCoord1d);
      SET_Materiali(tab, vfmt->Materiali);
      SET_Materialiv(tab, vfmt->Materialiv);
      SET_FogCoordd(tab, vfmt->FogCoordd);
      SET_FogCoorddv(tab, vfmt->FogCoorddv);

      SET_VertexAttrib1sNV(tab, vfmt->VertexAttrib1sNV);
      SET_VertexAttrib1dNV(tab, vfmt->VertexAttrib1dNV);
      SET_VertexAttrib2sNV(tab, vfmt->VertexAttrib2sNV);
      SET_VertexAttrib2dNV(tab, vfmt->VertexAttrib2dNV);
      SET_VertexAttrib3sNV(tab, vfmt->VertexAttrib3sNV);
      SET_VertexAttrib3dNV(tab, vfmt->VertexAttrib3dNV);
      SET_VertexAttrib4sNV(tab, vfmt->VertexAttrib4sNV);
      SET_VertexAttrib4dNV(tab, vfmt->VertexAttrib4dNV);
      SET_VertexAttrib4ubNV(tab, vfmt->VertexAttrib4ubNV);
      SET_VertexAttrib1svNV(tab, vfmt->VertexAttrib1svNV);
      SET_VertexAttrib1dvNV(tab, vfmt->VertexAttrib1dvNV);
      SET_VertexAttrib2svNV(tab, vfmt->VertexAttrib2svNV);
      SET_VertexAttrib2dvNV(tab, vfmt->VertexAttrib2dvNV);
      SET_VertexAttrib3svNV(tab, vfmt->VertexAttrib3svNV);
      SET_VertexAttrib3dvNV(tab, vfmt->VertexAttrib3dvNV);
      SET_VertexAttrib4svNV(tab, vfmt->VertexAttrib4svNV);
      SET_VertexAttrib4dvNV(tab, vfmt->VertexAttrib4dvNV);
      SET_VertexAttrib4ubvNV(tab, vfmt->VertexAttrib4ubvNV);
      SET_VertexAttribs1svNV(tab, vfmt->VertexAttribs1svNV);
      SET_VertexAttribs1fvNV(tab, vfmt->VertexAttribs1fvNV);
      SET_VertexAttribs1dvNV(tab, vfmt->VertexAttribs1dvNV);
      SET_VertexAttribs2svNV(tab, vfmt->VertexAttribs2svNV);
      SET_VertexAttribs2fvNV(tab, vfmt->VertexAttribs2fvNV);
      SET_VertexAttribs2dvNV(tab, vfmt->VertexAttribs2dvNV);
      SET_VertexAttribs3svNV(tab, vfmt->VertexAttribs3svNV);
      SET_VertexAttribs3fvNV(tab, vfmt->VertexAttribs3fvNV);
      SET_VertexAttribs3dvNV(tab, vfmt->VertexAttribs3dvNV);
      SET_VertexAttribs4svNV(tab, vfmt->VertexAttribs4svNV);
      SET_VertexAttribs4fvNV(tab, vfmt->VertexAttribs4fvNV);
      SET_VertexAttribs4dvNV(tab, vfmt->VertexAttribs4dvNV);
      SET_VertexAttribs4ubvNV(tab, vfmt->VertexAttribs4ubvNV);
   }

   if (_mesa_is_desktop_gl(ctx)) {
      SET_VertexAttrib1s(tab, vfmt->VertexAttrib1s);
      SET_VertexAttrib1d(tab, vfmt->VertexAttrib1d);
      SET_VertexAttrib2s(tab, vfmt->VertexAttrib2s);
      SET_VertexAttrib2d(tab, vfmt->VertexAttrib2d);
      SET_VertexAttrib3s(tab, vfmt->VertexAttrib3s);
      SET_VertexAttrib3d(tab, vfmt->VertexAttrib3d);
      SET_VertexAttrib4s(tab, vfmt->VertexAttrib4s);
      SET_VertexAttrib4d(tab, vfmt->VertexAttrib4d);
      SET_VertexAttrib1sv(tab, vfmt->VertexAttrib1sv);
      SET_VertexAttrib1dv(tab, vfmt->VertexAttrib1dv);
      SET_VertexAttrib2sv(tab, vfmt->VertexAttrib2sv);
      SET_VertexAttrib2dv(tab, vfmt->VertexAttrib2dv);
      SET_VertexAttrib3sv(tab, vfmt->VertexAttrib3sv);
      SET_VertexAttrib3dv(tab, vfmt->VertexAttrib3dv);
      SET_VertexAttrib4sv(tab, vfmt->VertexAttrib4sv);
      SET_VertexAttrib4dv(tab, vfmt->VertexAttrib4dv);
      SET_VertexAttrib4Nub(tab, vfmt->VertexAttrib4Nub);
      SET_VertexAttrib4Nubv(tab, vfmt->VertexAttrib4Nubv);
      SET_VertexAttrib4bv(tab, vfmt->VertexAttrib4bv);
      SET_VertexAttrib4iv(tab, vfmt->VertexAttrib4iv);
      SET_VertexAttrib4ubv(tab, vfmt->VertexAttrib4ubv);
      SET_VertexAttrib4usv(tab, vfmt->VertexAttrib4usv);
      SET_VertexAttrib4uiv(tab, vfmt->VertexAttrib4uiv);
      SET_VertexAttrib4Nbv(tab, vfmt->VertexAttrib4Nbv);
      SET_VertexAttrib4Nsv(tab, vfmt->VertexAttrib4Nsv);
      SET_VertexAttrib4Nusv(tab, vfmt->VertexAttrib4Nusv);
      SET_VertexAttrib4Niv(tab, vfmt->VertexAttrib4Niv);
      SET_VertexAttrib4Nuiv(tab, vfmt->VertexAttrib4Nuiv);

      /* GL_EXT_gpu_shader4, GL 3.0 */
      SET_VertexAttribI1iv(tab, vfmt->VertexAttribI1iv);
      SET_VertexAttribI1uiv(tab, vfmt->VertexAttribI1uiv);
      SET_VertexAttribI4bv(tab, vfmt->VertexAttribI4bv);
      SET_VertexAttribI4sv(tab, vfmt->VertexAttribI4sv);
      SET_VertexAttribI4ubv(tab, vfmt->VertexAttribI4ubv);
      SET_VertexAttribI4usv(tab, vfmt->VertexAttribI4usv);
   }
}


/**
 * Install per-vertex functions into the API dispatch table for execution.
 */
void
_mesa_install_exec_vtxfmt(struct gl_context *ctx, const GLvertexformat *vfmt)
{
   install_vtxfmt(ctx, ctx->Exec, vfmt);
   if (ctx->BeginEnd)
      install_vtxfmt(ctx, ctx->BeginEnd, vfmt);
}


/**
 * Install per-vertex functions into the API dispatch table for display
 * list compilation.
 */
void
_mesa_install_save_vtxfmt(struct gl_context *ctx, const GLvertexformat *vfmt)
{
   if (_mesa_is_desktop_gl(ctx))
      install_vtxfmt(ctx, ctx->Save, vfmt);
}


/**
 * Install VBO vtxfmt functions.
 *
 * This function depends on ctx->Version.
 */
void
_mesa_initialize_vbo_vtxfmt(struct gl_context *ctx)
{
   _vbo_install_exec_vtxfmt(ctx);
   if (ctx->API == API_OPENGL_COMPAT) {
      _mesa_install_save_vtxfmt(ctx, &ctx->ListState.ListVtxfmt);
   }
}

