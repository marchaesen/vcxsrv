/**************************************************************************
 *
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

vfmt->ArrayElement = NAME_AE(ArrayElement);

vfmt->Begin = NAME(Begin);
vfmt->End = NAME(End);
vfmt->PrimitiveRestartNV = NAME(PrimitiveRestartNV);

vfmt->CallList = NAME_CALLLIST(CallList);
vfmt->CallLists = NAME_CALLLIST(CallLists);

vfmt->EvalCoord1f = NAME(EvalCoord1f);
vfmt->EvalCoord1fv = NAME(EvalCoord1fv);
vfmt->EvalCoord2f = NAME(EvalCoord2f);
vfmt->EvalCoord2fv = NAME(EvalCoord2fv);
vfmt->EvalPoint1 = NAME(EvalPoint1);
vfmt->EvalPoint2 = NAME(EvalPoint2);

vfmt->Color3f = NAME(Color3f);
vfmt->Color3fv = NAME(Color3fv);
vfmt->Color4f = NAME(Color4f);
vfmt->Color4fv = NAME(Color4fv);
vfmt->FogCoordfEXT = NAME(FogCoordfEXT);
vfmt->FogCoordfvEXT = NAME(FogCoordfvEXT);
vfmt->MultiTexCoord1fARB = NAME(MultiTexCoord1f);
vfmt->MultiTexCoord1fvARB = NAME(MultiTexCoord1fv);
vfmt->MultiTexCoord2fARB = NAME(MultiTexCoord2f);
vfmt->MultiTexCoord2fvARB = NAME(MultiTexCoord2fv);
vfmt->MultiTexCoord3fARB = NAME(MultiTexCoord3f);
vfmt->MultiTexCoord3fvARB = NAME(MultiTexCoord3fv);
vfmt->MultiTexCoord4fARB = NAME(MultiTexCoord4f);
vfmt->MultiTexCoord4fvARB = NAME(MultiTexCoord4fv);
vfmt->Normal3f = NAME(Normal3f);
vfmt->Normal3fv = NAME(Normal3fv);
vfmt->SecondaryColor3fEXT = NAME(SecondaryColor3fEXT);
vfmt->SecondaryColor3fvEXT = NAME(SecondaryColor3fvEXT);
vfmt->TexCoord1f = NAME(TexCoord1f);
vfmt->TexCoord1fv = NAME(TexCoord1fv);
vfmt->TexCoord2f = NAME(TexCoord2f);
vfmt->TexCoord2fv = NAME(TexCoord2fv);
vfmt->TexCoord3f = NAME(TexCoord3f);
vfmt->TexCoord3fv = NAME(TexCoord3fv);
vfmt->TexCoord4f = NAME(TexCoord4f);
vfmt->TexCoord4fv = NAME(TexCoord4fv);
vfmt->Vertex2f = NAME(Vertex2f);
vfmt->Vertex2fv = NAME(Vertex2fv);
vfmt->Vertex3f = NAME(Vertex3f);
vfmt->Vertex3fv = NAME(Vertex3fv);
vfmt->Vertex4f = NAME(Vertex4f);
vfmt->Vertex4fv = NAME(Vertex4fv);

if (ctx->API == API_OPENGLES2) {
   vfmt->VertexAttrib1fARB = NAME_ES(VertexAttrib1f);
   vfmt->VertexAttrib1fvARB = NAME_ES(VertexAttrib1fv);
   vfmt->VertexAttrib2fARB = NAME_ES(VertexAttrib2f);
   vfmt->VertexAttrib2fvARB = NAME_ES(VertexAttrib2fv);
   vfmt->VertexAttrib3fARB = NAME_ES(VertexAttrib3f);
   vfmt->VertexAttrib3fvARB = NAME_ES(VertexAttrib3fv);
   vfmt->VertexAttrib4fARB = NAME_ES(VertexAttrib4f);
   vfmt->VertexAttrib4fvARB = NAME_ES(VertexAttrib4fv);
} else {
   vfmt->VertexAttrib1fARB = NAME(VertexAttrib1fARB);
   vfmt->VertexAttrib1fvARB = NAME(VertexAttrib1fvARB);
   vfmt->VertexAttrib2fARB = NAME(VertexAttrib2fARB);
   vfmt->VertexAttrib2fvARB = NAME(VertexAttrib2fvARB);
   vfmt->VertexAttrib3fARB = NAME(VertexAttrib3fARB);
   vfmt->VertexAttrib3fvARB = NAME(VertexAttrib3fvARB);
   vfmt->VertexAttrib4fARB = NAME(VertexAttrib4fARB);
   vfmt->VertexAttrib4fvARB = NAME(VertexAttrib4fvARB);
}

/* half float */
vfmt->Vertex2hNV = NAME(Vertex2hNV);
vfmt->Vertex2hvNV = NAME(Vertex2hvNV);
vfmt->Vertex3hNV = NAME(Vertex3hNV);
vfmt->Vertex3hvNV = NAME(Vertex3hvNV);
vfmt->Vertex4hNV = NAME(Vertex4hNV);
vfmt->Vertex4hvNV = NAME(Vertex4hvNV);
vfmt->Normal3hNV = NAME(Normal3hNV);
vfmt->Normal3hvNV = NAME(Normal3hvNV);
vfmt->Color3hNV = NAME(Color3hNV);
vfmt->Color3hvNV = NAME(Color3hvNV);
vfmt->Color4hNV = NAME(Color4hNV);
vfmt->Color4hvNV = NAME(Color4hvNV);
vfmt->TexCoord1hNV = NAME(TexCoord1hNV);
vfmt->TexCoord1hvNV = NAME(TexCoord1hvNV);
vfmt->TexCoord2hNV = NAME(TexCoord2hNV);
vfmt->TexCoord2hvNV = NAME(TexCoord2hvNV);
vfmt->TexCoord3hNV = NAME(TexCoord3hNV);
vfmt->TexCoord3hvNV = NAME(TexCoord3hvNV);
vfmt->TexCoord4hNV = NAME(TexCoord4hNV);
vfmt->TexCoord4hvNV = NAME(TexCoord4hvNV);
vfmt->MultiTexCoord1hNV = NAME(MultiTexCoord1hNV);
vfmt->MultiTexCoord1hvNV = NAME(MultiTexCoord1hvNV);
vfmt->MultiTexCoord2hNV = NAME(MultiTexCoord2hNV);
vfmt->MultiTexCoord2hvNV = NAME(MultiTexCoord2hvNV);
vfmt->MultiTexCoord3hNV = NAME(MultiTexCoord3hNV);
vfmt->MultiTexCoord3hvNV = NAME(MultiTexCoord3hvNV);
vfmt->MultiTexCoord4hNV = NAME(MultiTexCoord4hNV);
vfmt->MultiTexCoord4hvNV = NAME(MultiTexCoord4hvNV);
vfmt->FogCoordhNV = NAME(FogCoordhNV);
vfmt->FogCoordhvNV = NAME(FogCoordhvNV);
vfmt->SecondaryColor3hNV = NAME(SecondaryColor3hNV);
vfmt->SecondaryColor3hvNV = NAME(SecondaryColor3hvNV);

/* Note that VertexAttrib4fNV is used from dlist.c and api_arrayelt.c so
 * they can have a single entrypoint for updating any of the legacy
 * attribs.
 */
vfmt->VertexAttrib1fNV = NAME(VertexAttrib1fNV);
vfmt->VertexAttrib1fvNV = NAME(VertexAttrib1fvNV);
vfmt->VertexAttrib2fNV = NAME(VertexAttrib2fNV);
vfmt->VertexAttrib2fvNV = NAME(VertexAttrib2fvNV);
vfmt->VertexAttrib3fNV = NAME(VertexAttrib3fNV);
vfmt->VertexAttrib3fvNV = NAME(VertexAttrib3fvNV);
vfmt->VertexAttrib4fNV = NAME(VertexAttrib4fNV);
vfmt->VertexAttrib4fvNV = NAME(VertexAttrib4fvNV);

/* integer-valued */
vfmt->VertexAttribI1i = NAME(VertexAttribI1i);
vfmt->VertexAttribI2i = NAME(VertexAttribI2i);
vfmt->VertexAttribI3i = NAME(VertexAttribI3i);
vfmt->VertexAttribI4i = NAME(VertexAttribI4i);
vfmt->VertexAttribI2iv = NAME(VertexAttribI2iv);
vfmt->VertexAttribI3iv = NAME(VertexAttribI3iv);
vfmt->VertexAttribI4iv = NAME(VertexAttribI4iv);

/* unsigned integer-valued */
vfmt->VertexAttribI1ui = NAME(VertexAttribI1ui);
vfmt->VertexAttribI2ui = NAME(VertexAttribI2ui);
vfmt->VertexAttribI3ui = NAME(VertexAttribI3ui);
vfmt->VertexAttribI4ui = NAME(VertexAttribI4ui);
vfmt->VertexAttribI2uiv = NAME(VertexAttribI2uiv);
vfmt->VertexAttribI3uiv = NAME(VertexAttribI3uiv);
vfmt->VertexAttribI4uiv = NAME(VertexAttribI4uiv);

vfmt->Materialfv = NAME(Materialfv);

vfmt->EdgeFlag = NAME(EdgeFlag);
vfmt->Indexf = NAME(Indexf);
vfmt->Indexfv = NAME(Indexfv);

/* ARB_vertex_type_2_10_10_10_rev */
vfmt->VertexP2ui = NAME(VertexP2ui);
vfmt->VertexP2uiv = NAME(VertexP2uiv);
vfmt->VertexP3ui = NAME(VertexP3ui);
vfmt->VertexP3uiv = NAME(VertexP3uiv);
vfmt->VertexP4ui = NAME(VertexP4ui);
vfmt->VertexP4uiv = NAME(VertexP4uiv);

vfmt->TexCoordP1ui = NAME(TexCoordP1ui);
vfmt->TexCoordP1uiv = NAME(TexCoordP1uiv);
vfmt->TexCoordP2ui = NAME(TexCoordP2ui);
vfmt->TexCoordP2uiv = NAME(TexCoordP2uiv);
vfmt->TexCoordP3ui = NAME(TexCoordP3ui);
vfmt->TexCoordP3uiv = NAME(TexCoordP3uiv);
vfmt->TexCoordP4ui = NAME(TexCoordP4ui);
vfmt->TexCoordP4uiv = NAME(TexCoordP4uiv);

vfmt->MultiTexCoordP1ui = NAME(MultiTexCoordP1ui);
vfmt->MultiTexCoordP1uiv = NAME(MultiTexCoordP1uiv);
vfmt->MultiTexCoordP2ui = NAME(MultiTexCoordP2ui);
vfmt->MultiTexCoordP2uiv = NAME(MultiTexCoordP2uiv);
vfmt->MultiTexCoordP3ui = NAME(MultiTexCoordP3ui);
vfmt->MultiTexCoordP3uiv = NAME(MultiTexCoordP3uiv);
vfmt->MultiTexCoordP4ui = NAME(MultiTexCoordP4ui);
vfmt->MultiTexCoordP4uiv = NAME(MultiTexCoordP4uiv);

vfmt->NormalP3ui = NAME(NormalP3ui);
vfmt->NormalP3uiv = NAME(NormalP3uiv);

vfmt->ColorP3ui = NAME(ColorP3ui);
vfmt->ColorP3uiv = NAME(ColorP3uiv);
vfmt->ColorP4ui = NAME(ColorP4ui);
vfmt->ColorP4uiv = NAME(ColorP4uiv);

vfmt->SecondaryColorP3ui = NAME(SecondaryColorP3ui);
vfmt->SecondaryColorP3uiv = NAME(SecondaryColorP3uiv);

vfmt->VertexAttribP1ui = NAME(VertexAttribP1ui);
vfmt->VertexAttribP1uiv = NAME(VertexAttribP1uiv);
vfmt->VertexAttribP2ui = NAME(VertexAttribP2ui);
vfmt->VertexAttribP2uiv = NAME(VertexAttribP2uiv);
vfmt->VertexAttribP3ui = NAME(VertexAttribP3ui);
vfmt->VertexAttribP3uiv = NAME(VertexAttribP3uiv);
vfmt->VertexAttribP4ui = NAME(VertexAttribP4ui);
vfmt->VertexAttribP4uiv = NAME(VertexAttribP4uiv);

vfmt->VertexAttribL1d = NAME(VertexAttribL1d);
vfmt->VertexAttribL2d = NAME(VertexAttribL2d);
vfmt->VertexAttribL3d = NAME(VertexAttribL3d);
vfmt->VertexAttribL4d = NAME(VertexAttribL4d);

vfmt->VertexAttribL1dv = NAME(VertexAttribL1dv);
vfmt->VertexAttribL2dv = NAME(VertexAttribL2dv);
vfmt->VertexAttribL3dv = NAME(VertexAttribL3dv);
vfmt->VertexAttribL4dv = NAME(VertexAttribL4dv);

vfmt->VertexAttribL1ui64ARB = NAME(VertexAttribL1ui64ARB);
vfmt->VertexAttribL1ui64vARB = NAME(VertexAttribL1ui64vARB);

vfmt->Color4ub = NAME(Color4ub);
vfmt->Materialf = NAME(Materialf);

vfmt->Color3b = NAME(Color3b);
vfmt->Color3d = NAME(Color3d);
vfmt->Color3i = NAME(Color3i);
vfmt->Color3s = NAME(Color3s);
vfmt->Color3ui = NAME(Color3ui);
vfmt->Color3us = NAME(Color3us);
vfmt->Color3ub = NAME(Color3ub);
vfmt->Color4b = NAME(Color4b);
vfmt->Color4d = NAME(Color4d);
vfmt->Color4i = NAME(Color4i);
vfmt->Color4s = NAME(Color4s);
vfmt->Color4ui = NAME(Color4ui);
vfmt->Color4us = NAME(Color4us);
vfmt->Color3bv = NAME(Color3bv);
vfmt->Color3dv = NAME(Color3dv);
vfmt->Color3iv = NAME(Color3iv);
vfmt->Color3sv = NAME(Color3sv);
vfmt->Color3uiv = NAME(Color3uiv);
vfmt->Color3usv = NAME(Color3usv);
vfmt->Color3ubv = NAME(Color3ubv);
vfmt->Color4bv = NAME(Color4bv);
vfmt->Color4dv = NAME(Color4dv);
vfmt->Color4iv = NAME(Color4iv);
vfmt->Color4sv = NAME(Color4sv);
vfmt->Color4uiv = NAME(Color4uiv);
vfmt->Color4usv = NAME(Color4usv);
vfmt->Color4ubv = NAME(Color4ubv);

vfmt->SecondaryColor3b = NAME(SecondaryColor3b);
vfmt->SecondaryColor3d = NAME(SecondaryColor3d);
vfmt->SecondaryColor3i = NAME(SecondaryColor3i);
vfmt->SecondaryColor3s = NAME(SecondaryColor3s);
vfmt->SecondaryColor3ui = NAME(SecondaryColor3ui);
vfmt->SecondaryColor3us = NAME(SecondaryColor3us);
vfmt->SecondaryColor3ub = NAME(SecondaryColor3ub);
vfmt->SecondaryColor3bv = NAME(SecondaryColor3bv);
vfmt->SecondaryColor3dv = NAME(SecondaryColor3dv);
vfmt->SecondaryColor3iv = NAME(SecondaryColor3iv);
vfmt->SecondaryColor3sv = NAME(SecondaryColor3sv);
vfmt->SecondaryColor3uiv = NAME(SecondaryColor3uiv);
vfmt->SecondaryColor3usv = NAME(SecondaryColor3usv);
vfmt->SecondaryColor3ubv = NAME(SecondaryColor3ubv);

vfmt->EdgeFlagv = NAME(EdgeFlagv);

vfmt->Indexd = NAME(Indexd);
vfmt->Indexi = NAME(Indexi);
vfmt->Indexs = NAME(Indexs);
vfmt->Indexub = NAME(Indexub);
vfmt->Indexdv = NAME(Indexdv);
vfmt->Indexiv = NAME(Indexiv);
vfmt->Indexsv = NAME(Indexsv);
vfmt->Indexubv = NAME(Indexubv);
vfmt->Normal3b = NAME(Normal3b);
vfmt->Normal3d = NAME(Normal3d);
vfmt->Normal3i = NAME(Normal3i);
vfmt->Normal3s = NAME(Normal3s);
vfmt->Normal3bv = NAME(Normal3bv);
vfmt->Normal3dv = NAME(Normal3dv);
vfmt->Normal3iv = NAME(Normal3iv);
vfmt->Normal3sv = NAME(Normal3sv);
vfmt->TexCoord1d = NAME(TexCoord1d);
vfmt->TexCoord1i = NAME(TexCoord1i);
vfmt->TexCoord1s = NAME(TexCoord1s);
vfmt->TexCoord2d = NAME(TexCoord2d);
vfmt->TexCoord2s = NAME(TexCoord2s);
vfmt->TexCoord2i = NAME(TexCoord2i);
vfmt->TexCoord3d = NAME(TexCoord3d);
vfmt->TexCoord3i = NAME(TexCoord3i);
vfmt->TexCoord3s = NAME(TexCoord3s);
vfmt->TexCoord4d = NAME(TexCoord4d);
vfmt->TexCoord4i = NAME(TexCoord4i);
vfmt->TexCoord4s = NAME(TexCoord4s);
vfmt->TexCoord1dv = NAME(TexCoord1dv);
vfmt->TexCoord1iv = NAME(TexCoord1iv);
vfmt->TexCoord1sv = NAME(TexCoord1sv);
vfmt->TexCoord2dv = NAME(TexCoord2dv);
vfmt->TexCoord2iv = NAME(TexCoord2iv);
vfmt->TexCoord2sv = NAME(TexCoord2sv);
vfmt->TexCoord3dv = NAME(TexCoord3dv);
vfmt->TexCoord3iv = NAME(TexCoord3iv);
vfmt->TexCoord3sv = NAME(TexCoord3sv);
vfmt->TexCoord4dv = NAME(TexCoord4dv);
vfmt->TexCoord4iv = NAME(TexCoord4iv);
vfmt->TexCoord4sv = NAME(TexCoord4sv);
vfmt->Vertex2d = NAME(Vertex2d);
vfmt->Vertex2i = NAME(Vertex2i);
vfmt->Vertex2s = NAME(Vertex2s);
vfmt->Vertex3d = NAME(Vertex3d);
vfmt->Vertex3i = NAME(Vertex3i);
vfmt->Vertex3s = NAME(Vertex3s);
vfmt->Vertex4d = NAME(Vertex4d);
vfmt->Vertex4i = NAME(Vertex4i);
vfmt->Vertex4s = NAME(Vertex4s);
vfmt->Vertex2dv = NAME(Vertex2dv);
vfmt->Vertex2iv = NAME(Vertex2iv);
vfmt->Vertex2sv = NAME(Vertex2sv);
vfmt->Vertex3dv = NAME(Vertex3dv);
vfmt->Vertex3iv = NAME(Vertex3iv);
vfmt->Vertex3sv = NAME(Vertex3sv);
vfmt->Vertex4dv = NAME(Vertex4dv);
vfmt->Vertex4iv = NAME(Vertex4iv);
vfmt->Vertex4sv = NAME(Vertex4sv);
vfmt->MultiTexCoord1d = NAME(MultiTexCoord1d);
vfmt->MultiTexCoord1dv = NAME(MultiTexCoord1dv);
vfmt->MultiTexCoord1i = NAME(MultiTexCoord1i);
vfmt->MultiTexCoord1iv = NAME(MultiTexCoord1iv);
vfmt->MultiTexCoord1s = NAME(MultiTexCoord1s);
vfmt->MultiTexCoord1sv = NAME(MultiTexCoord1sv);
vfmt->MultiTexCoord2d = NAME(MultiTexCoord2d);
vfmt->MultiTexCoord2dv = NAME(MultiTexCoord2dv);
vfmt->MultiTexCoord2i = NAME(MultiTexCoord2i);
vfmt->MultiTexCoord2iv = NAME(MultiTexCoord2iv);
vfmt->MultiTexCoord2s = NAME(MultiTexCoord2s);
vfmt->MultiTexCoord2sv = NAME(MultiTexCoord2sv);
vfmt->MultiTexCoord3d = NAME(MultiTexCoord3d);
vfmt->MultiTexCoord3dv = NAME(MultiTexCoord3dv);
vfmt->MultiTexCoord3i = NAME(MultiTexCoord3i);
vfmt->MultiTexCoord3iv = NAME(MultiTexCoord3iv);
vfmt->MultiTexCoord3s = NAME(MultiTexCoord3s);
vfmt->MultiTexCoord3sv = NAME(MultiTexCoord3sv);
vfmt->MultiTexCoord4d = NAME(MultiTexCoord4d);
vfmt->MultiTexCoord4dv = NAME(MultiTexCoord4dv);
vfmt->MultiTexCoord4i = NAME(MultiTexCoord4i);
vfmt->MultiTexCoord4iv = NAME(MultiTexCoord4iv);
vfmt->MultiTexCoord4s = NAME(MultiTexCoord4s);
vfmt->MultiTexCoord4sv = NAME(MultiTexCoord4sv);
vfmt->EvalCoord2dv = NAME(EvalCoord2dv);
vfmt->EvalCoord2d = NAME(EvalCoord2d);
vfmt->EvalCoord1dv = NAME(EvalCoord1dv);
vfmt->EvalCoord1d = NAME(EvalCoord1d);
vfmt->Materiali = NAME(Materiali);
vfmt->Materialiv = NAME(Materialiv);
vfmt->FogCoordd = NAME(FogCoordd);
vfmt->FogCoorddv = NAME(FogCoorddv);

vfmt->VertexAttrib1sNV = NAME(VertexAttrib1sNV);
vfmt->VertexAttrib1dNV = NAME(VertexAttrib1dNV);
vfmt->VertexAttrib2sNV = NAME(VertexAttrib2sNV);
vfmt->VertexAttrib2dNV = NAME(VertexAttrib2dNV);
vfmt->VertexAttrib3sNV = NAME(VertexAttrib3sNV);
vfmt->VertexAttrib3dNV = NAME(VertexAttrib3dNV);
vfmt->VertexAttrib4sNV = NAME(VertexAttrib4sNV);
vfmt->VertexAttrib4dNV = NAME(VertexAttrib4dNV);
vfmt->VertexAttrib4ubNV = NAME(VertexAttrib4ubNV);
vfmt->VertexAttrib1svNV = NAME(VertexAttrib1svNV);
vfmt->VertexAttrib1dvNV = NAME(VertexAttrib1dvNV);
vfmt->VertexAttrib2svNV = NAME(VertexAttrib2svNV);
vfmt->VertexAttrib2dvNV = NAME(VertexAttrib2dvNV);
vfmt->VertexAttrib3svNV = NAME(VertexAttrib3svNV);
vfmt->VertexAttrib3dvNV = NAME(VertexAttrib3dvNV);
vfmt->VertexAttrib4svNV = NAME(VertexAttrib4svNV);
vfmt->VertexAttrib4dvNV = NAME(VertexAttrib4dvNV);
vfmt->VertexAttrib4ubvNV = NAME(VertexAttrib4ubvNV);
vfmt->VertexAttribs1svNV = NAME(VertexAttribs1svNV);
vfmt->VertexAttribs1fvNV = NAME(VertexAttribs1fvNV);
vfmt->VertexAttribs1dvNV = NAME(VertexAttribs1dvNV);
vfmt->VertexAttribs2svNV = NAME(VertexAttribs2svNV);
vfmt->VertexAttribs2fvNV = NAME(VertexAttribs2fvNV);
vfmt->VertexAttribs2dvNV = NAME(VertexAttribs2dvNV);
vfmt->VertexAttribs3svNV = NAME(VertexAttribs3svNV);
vfmt->VertexAttribs3fvNV = NAME(VertexAttribs3fvNV);
vfmt->VertexAttribs3dvNV = NAME(VertexAttribs3dvNV);
vfmt->VertexAttribs4svNV = NAME(VertexAttribs4svNV);
vfmt->VertexAttribs4fvNV = NAME(VertexAttribs4fvNV);
vfmt->VertexAttribs4dvNV = NAME(VertexAttribs4dvNV);
vfmt->VertexAttribs4ubvNV = NAME(VertexAttribs4ubvNV);

vfmt->VertexAttrib1s = NAME(VertexAttrib1s);
vfmt->VertexAttrib1d = NAME(VertexAttrib1d);
vfmt->VertexAttrib2s = NAME(VertexAttrib2s);
vfmt->VertexAttrib2d = NAME(VertexAttrib2d);
vfmt->VertexAttrib3s = NAME(VertexAttrib3s);
vfmt->VertexAttrib3d = NAME(VertexAttrib3d);
vfmt->VertexAttrib4s = NAME(VertexAttrib4s);
vfmt->VertexAttrib4d = NAME(VertexAttrib4d);
vfmt->VertexAttrib1sv = NAME(VertexAttrib1sv);
vfmt->VertexAttrib1dv = NAME(VertexAttrib1dv);
vfmt->VertexAttrib2sv = NAME(VertexAttrib2sv);
vfmt->VertexAttrib2dv = NAME(VertexAttrib2dv);
vfmt->VertexAttrib3sv = NAME(VertexAttrib3sv);
vfmt->VertexAttrib3dv = NAME(VertexAttrib3dv);
vfmt->VertexAttrib4sv = NAME(VertexAttrib4sv);
vfmt->VertexAttrib4dv = NAME(VertexAttrib4dv);
vfmt->VertexAttrib4Nub = NAME(VertexAttrib4Nub);
vfmt->VertexAttrib4Nubv = NAME(VertexAttrib4Nubv);
vfmt->VertexAttrib4bv = NAME(VertexAttrib4bv);
vfmt->VertexAttrib4iv = NAME(VertexAttrib4iv);
vfmt->VertexAttrib4ubv = NAME(VertexAttrib4ubv);
vfmt->VertexAttrib4usv = NAME(VertexAttrib4usv);
vfmt->VertexAttrib4uiv = NAME(VertexAttrib4uiv);
vfmt->VertexAttrib4Nbv = NAME(VertexAttrib4Nbv);
vfmt->VertexAttrib4Nsv = NAME(VertexAttrib4Nsv);
vfmt->VertexAttrib4Nusv = NAME(VertexAttrib4Nusv);
vfmt->VertexAttrib4Niv = NAME(VertexAttrib4Niv);
vfmt->VertexAttrib4Nuiv = NAME(VertexAttrib4Nuiv);

vfmt->VertexAttribI1iv = NAME(VertexAttribI1iv);
vfmt->VertexAttribI1uiv = NAME(VertexAttribI1uiv);
vfmt->VertexAttribI4bv = NAME(VertexAttribI4bv);
vfmt->VertexAttribI4sv = NAME(VertexAttribI4sv);
vfmt->VertexAttribI4ubv = NAME(VertexAttribI4ubv);
vfmt->VertexAttribI4usv = NAME(VertexAttribI4usv);
