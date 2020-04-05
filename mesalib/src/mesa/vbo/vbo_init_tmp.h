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
