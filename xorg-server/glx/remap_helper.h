/* DO NOT EDIT - This file generated automatically by remap_helper.py (from Mesa) script */

/*
 * Copyright (C) 2009 Chia-I Wu <olv@0xlab.org>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * Chia-I Wu,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "dispatch.h"
#include "remap.h"

/* this is internal to remap.c */
#ifndef need_MESA_remap_table
#error Only remap.c should include this file!
#endif /* need_MESA_remap_table */


static const char _mesa_function_pool[] =
   /* _mesa_function_pool[0]: MapGrid1d (offset 224) */
   "idd\0"
   "glMapGrid1d\0"
   "\0"
   /* _mesa_function_pool[17]: MapGrid1f (offset 225) */
   "iff\0"
   "glMapGrid1f\0"
   "\0"
   /* _mesa_function_pool[34]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "pp\0"
   "glReplacementCodeuiVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[70]: ProgramUniform3i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3i64vARB\0"
   "\0"
   /* _mesa_function_pool[101]: GetProgramResourceLocationIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocationIndex\0"
   "glGetProgramResourceLocationIndexEXT\0"
   "\0"
   /* _mesa_function_pool[177]: TexCoordP1ui (will be remapped) */
   "ii\0"
   "glTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[196]: PolygonStipple (offset 175) */
   "p\0"
   "glPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[216]: ListParameterfSGIX (dynamic) */
   "iif\0"
   "glListParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[242]: MultiTexCoord1dv (offset 377) */
   "ip\0"
   "glMultiTexCoord1dv\0"
   "glMultiTexCoord1dvARB\0"
   "\0"
   /* _mesa_function_pool[287]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[302]: GetTexFilterFuncSGIS (dynamic) */
   "iip\0"
   "glGetTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[330]: AttachShader (will be remapped) */
   "ii\0"
   "glAttachShader\0"
   "\0"
   /* _mesa_function_pool[349]: VertexAttrib3fARB (will be remapped) */
   "ifff\0"
   "glVertexAttrib3f\0"
   "glVertexAttrib3fARB\0"
   "\0"
   /* _mesa_function_pool[392]: Indexubv (offset 316) */
   "p\0"
   "glIndexubv\0"
   "\0"
   /* _mesa_function_pool[406]: GetCompressedTextureImage (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImage\0"
   "\0"
   /* _mesa_function_pool[440]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[466]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[513]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[528]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[567]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[587]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[625]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[664]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[684]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[762]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[801]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[832]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[857]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[890]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[919]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[937]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[970]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[997]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[1019]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[1034]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1055]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1072]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1128]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[1157]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1188]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1237]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1262]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1296]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[1337]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1371]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1435]: DepthRangeIndexedfOES (will be remapped) */
   "iff\0"
   "glDepthRangeIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[1464]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[1498]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[1534]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1579]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1608]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1656]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1681]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1712]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1727]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1758]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1770]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTexture\0"
   "glFramebufferTextureEXT\0"
   "glFramebufferTextureOES\0"
   "\0"
   /* _mesa_function_pool[1845]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[1876]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1903]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[1961]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "glBindFragDataLocationIndexedEXT\0"
   "\0"
   /* _mesa_function_pool[2030]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[2049]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[2077]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[2097]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[2123]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[2158]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "\0"
   /* _mesa_function_pool[2191]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[2240]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[2270]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[2313]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[2330]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[2362]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2402]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2418]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2439]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2460]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2498]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2528]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[2554]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2570]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2597]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2663]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2710]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[2735]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2782]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2803]: Uniform3ui64ARB (will be remapped) */
   "iiii\0"
   "glUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[2827]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2838]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[2882]: ProgramUniform3ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[2914]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[2929]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2940]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[2969]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[2984]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "glGetSamplerParameterIivEXT\0"
   "glGetSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[3070]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[3104]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[3152]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "glDrawElementsInstancedBaseVertexBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[3256]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[3313]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[3369]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[3389]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[3423]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[3455]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[3473]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[3513]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[3545]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[3589]: Uniform1ui64ARB (will be remapped) */
   "ii\0"
   "glUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[3611]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[3637]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[3682]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3693]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3711]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3735]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3752]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3789]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3806]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[3837]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[3865]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[3904]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "glGetnUniformiv\0"
   "glGetnUniformivKHR\0"
   "\0"
   /* _mesa_function_pool[3964]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[3990]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[4086]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[4102]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[4136]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "glBufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[4177]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[4193]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[4216]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[4232]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[4256]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[4289]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[4307]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[4364]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[4396]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[4428]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[4441]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[4466]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[4499]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[4514]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[4536]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[4561]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[4588]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "glBlendFuncSeparateiEXT\0"
   "glBlendFuncSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[4718]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[4750]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[4793]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[4821]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[4843]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[4899]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[4934]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[4959]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[5011]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[5037]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[5052]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[5084]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[5106]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[5130]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[5165]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[5193]: UnmapNamedBuffer (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[5215]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[5242]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[5266]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[5292]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[5317]: StringMarkerGREMEDY (will be remapped) */
   "ip\0"
   "glStringMarkerGREMEDY\0"
   "\0"
   /* _mesa_function_pool[5343]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[5377]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[5396]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[5430]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[5456]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[5491]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[5523]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[5549]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[5597]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[5645]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[5735]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[5795]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[5810]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[5836]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[5863]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[5890]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[5943]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[5989]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[6015]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[6042]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[6067]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[6113]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[6136]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[6163]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[6197]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[6243]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[6269]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[6302]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[6327]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[6385]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[6400]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[6526]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6569]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[6603]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[6626]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[6651]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[6671]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[6695]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[6721]: Uniform2i64vARB (will be remapped) */
   "iip\0"
   "glUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[6744]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[6768]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[6786]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[6827]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[6840]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[6918]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[6953]: BlendBarrier (will be remapped) */
   "\0"
   "glBlendBarrier\0"
   "glBlendBarrierKHR\0"
   "\0"
   /* _mesa_function_pool[6988]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[7012]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[7038]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[7064]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "glGetSamplerParameterIuivEXT\0"
   "glGetSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[7153]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "glDisableiEXT\0"
   "glDisableiOES\0"
   "\0"
   /* _mesa_function_pool[7216]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[7313]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[7335]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "glObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[7372]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[7386]: ProgramUniform1ui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[7416]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "\0"
   /* _mesa_function_pool[7449]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[7467]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[7516]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[7534]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[7589]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[7607]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[7628]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[7654]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[7688]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[7720]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[7749]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[7776]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7822]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[7906]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[7935]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[7961]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[7981]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[8018]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "\0"
   /* _mesa_function_pool[8041]: Uniform1ui64vARB (will be remapped) */
   "iip\0"
   "glUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[8065]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[8122]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[8158]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[8175]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "glGetObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[8219]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[8236]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[8263]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[8298]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8325]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[8370]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[8418]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "glGetDebugMessageLogKHR\0"
   "\0"
   /* _mesa_function_pool[8497]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[8536]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[8560]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[8601]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[8633]: Uniform4ui64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[8658]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[8707]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8736]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8763]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[8808]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[8845]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[8870]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[8901]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[8967]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[8987]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[9072]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "\0"
   /* _mesa_function_pool[9120]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[9165]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[9196]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[9227]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "\0"
   /* _mesa_function_pool[9252]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[9273]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[9287]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[9347]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[9393]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[9459]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[9501]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "\0"
   /* _mesa_function_pool[9547]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[9589]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[9635]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[9677]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[9709]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[9743]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[9808]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[9833]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[9854]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[9909]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[9969]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[10013]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[10046]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[10069]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[10114]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[10160]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[10213]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[10260]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "glGetGraphicsResetStatus\0"
   "glGetGraphicsResetStatusKHR\0"
   "\0"
   /* _mesa_function_pool[10343]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[10369]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[10395]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[10421]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[10438]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[10466]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[10492]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[10526]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[10552]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "glSamplerParameterIivEXT\0"
   "glSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[10629]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[10662]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[10690]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[10723]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[10757]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[10808]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[10821]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[10854]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[10868]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[10903]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[10949]: Uniform2ui64ARB (will be remapped) */
   "iii\0"
   "glUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[10972]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[10998]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[11023]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[11044]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11069]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[11093]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[11117]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[11160]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[11187]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[11235]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[11256]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[11277]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[11290]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[11320]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[11346]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[11362]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[11388]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[11433]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[11482]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[11527]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[11544]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[11556]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[11582]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[11627]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[11649]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[11691]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[11716]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[11731]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[11772]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[11789]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11819]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[11830]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[11848]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[11895]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[11913]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[11938]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[11963]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11989]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[12028]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[12057]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[12102]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[12147]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[12173]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[12191]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[12216]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[12262]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12299]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[12325]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "glDrawElementsInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[12408]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[12455]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[12482]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[12529]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[12579]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[12605]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[12636]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[12657]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[12685]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[12730]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[12757]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[12790]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[12828]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[12845]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[12896]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[12921]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[12952]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[12977]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[13007]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[13023]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[13049]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[13077]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[13123]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[13143]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[13171]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[13189]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[13213]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "glPatchParameteriEXT\0"
   "glPatchParameteriOES\0"
   "\0"
   /* _mesa_function_pool[13277]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13326]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[13356]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[13383]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[13405]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[13458]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[13511]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "\0"
   /* _mesa_function_pool[13537]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[13590]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[13643]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[13688]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[13704]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[13726]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[13812]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[13859]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[13905]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[13935]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[13961]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[14004]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[14031]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[14052]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "\0"
   /* _mesa_function_pool[14086]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[14129]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[14163]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[14191]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[14237]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[14263]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[14308]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[14349]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[14374]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[14410]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[14444]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[14485]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[14530]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[14551]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "glGetObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[14600]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[14648]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[14686]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[14700]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[14726]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[14747]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[14791]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[14835]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "glSamplerParameterIuivEXT\0"
   "glSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[14915]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[14959]: GetInternalformati64v (will be remapped) */
   "iiiip\0"
   "glGetInternalformati64v\0"
   "\0"
   /* _mesa_function_pool[14990]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[15026]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15079]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[15092]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[15130]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[15182]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[15205]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[15231]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[15245]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[15259]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[15283]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[15321]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[15355]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[15379]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[15401]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[15430]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "\0"
   /* _mesa_function_pool[15449]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[15474]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[15500]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[15549]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[15582]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[15614]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[15649]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "glDrawElementsBaseVertexEXT\0"
   "glDrawElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[15737]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[15755]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "glColorMaskiEXT\0"
   "glColorMaskiOES\0"
   "\0"
   /* _mesa_function_pool[15829]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[15865]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[15884]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[15903]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[15922]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[15964]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[15983]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "\0"
   /* _mesa_function_pool[16006]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[16031]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[16076]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[16119]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[16149]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[16182]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16224]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[16259]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[16274]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[16327]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[16341]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16370]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "glGetnUniformuiv\0"
   "glGetnUniformuivKHR\0"
   "\0"
   /* _mesa_function_pool[16433]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[16449]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[16507]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[16528]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[16547]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[16582]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[16607]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[16626]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[16641]: ProgramUniform4i64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[16673]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[16711]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[16737]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[16767]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[16810]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[16855]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[16889]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[16915]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[16941]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[16970]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[17013]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[17048]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[17076]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[17094]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[17117]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[17155]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "glPushDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[17198]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17246]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[17283]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[17296]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[17313]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[17343]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[17366]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[17400]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[17428]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[17455]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[17498]: Uniform3i64vARB (will be remapped) */
   "iip\0"
   "glUniform3i64vARB\0"
   "\0"
   /* _mesa_function_pool[17521]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[17576]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[17594]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[17628]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[17649]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[17687]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[17704]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[17725]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[17774]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[17824]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[17870]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[17889]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[17920]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[17943]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[17970]: Uniform3i64ARB (will be remapped) */
   "iiii\0"
   "glUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[17993]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[18018]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[18036]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[18049]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[18125]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[18159]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[18213]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "glEnableiEXT\0"
   "glEnableiOES\0"
   "\0"
   /* _mesa_function_pool[18272]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[18326]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[18363]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[18381]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[18435]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[18480]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[18497]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[18551]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[18570]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "glDebugMessageInsertKHR\0"
   "\0"
   /* _mesa_function_pool[18647]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[18676]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[18735]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[18756]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[18791]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[18815]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[18827]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[18842]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[18887]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[18916]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[18932]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[18950]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[18966]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[19007]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[19029]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[19047]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[19085]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[19103]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[19121]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[19157]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[19174]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[19201]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[19219]: DispatchComputeGroupSizeARB (will be remapped) */
   "iiiiii\0"
   "glDispatchComputeGroupSizeARB\0"
   "\0"
   /* _mesa_function_pool[19257]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[19275]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[19310]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[19343]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[19370]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[19405]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[19450]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[19500]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[19526]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "glDebugMessageCallbackKHR\0"
   "\0"
   /* _mesa_function_pool[19605]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[19648]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[19703]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "glTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[19771]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[19797]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[19840]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[19883]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[19939]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[19977]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[19996]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[20030]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[20092]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[20113]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[20138]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[20166]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[20193]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[20241]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[20268]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[20290]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[20308]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[20337]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[20372]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "glDrawElementsInstancedBaseVertexEXT\0"
   "glDrawElementsInstancedBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[20488]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[20512]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[20540]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[20568]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[20583]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[20610]: ProgramUniform4i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[20641]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[20670]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[20684]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[20723]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[20775]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[20827]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[20843]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[20901]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[20957]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[20987]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[21038]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[21058]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[21093]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[21119]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[21135]: Uniform3ui64vARB (will be remapped) */
   "iip\0"
   "glUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[21159]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[21182]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[21231]: BufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[21263]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[21291]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[21307]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[21334]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[21361]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[21381]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[21400]: GetnUniformui64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[21428]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[21445]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[21471]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[21499]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[21533]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[21569]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "glPopDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[21606]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[21647]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[21705]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[21732]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "glTexBufferRangeEXT\0"
   "glTexBufferRangeOES\0"
   "\0"
   /* _mesa_function_pool[21796]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[21837]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[21872]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[21899]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[21934]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[21966]: Uniform2ui64vARB (will be remapped) */
   "iip\0"
   "glUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[21990]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[22082]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[22127]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[22158]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[22173]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[22194]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[22218]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[22245]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[22269]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[22288]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "glViewportArrayvOES\0"
   "\0"
   /* _mesa_function_pool[22330]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[22401]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[22454]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[22499]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[22535]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[22559]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[22588]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[22606]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[22618]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "\0"
   /* _mesa_function_pool[22646]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[22663]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[22699]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[22726]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[22737]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[22780]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[22796]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[22832]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[22886]: GetUniformi64vARB (will be remapped) */
   "iip\0"
   "glGetUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[22911]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[22933]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[22975]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[22999]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "iiiii\0"
   "glMultiDrawArraysIndirectCountARB\0"
   "\0"
   /* _mesa_function_pool[23040]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[23102]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[23137]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[23163]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[23183]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[23201]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[23236]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[23281]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[23318]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[23369]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "\0"
   /* _mesa_function_pool[23392]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[23422]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[23470]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "glViewportIndexedfvOES\0"
   "\0"
   /* _mesa_function_pool[23517]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[23539]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[23553]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[23571]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[23590]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[23629]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[23663]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[23685]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[23713]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[23768]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[23803]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[23837]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[23868]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[23899]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[23919]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[23962]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[23995]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[24068]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[24094]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[24139]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[24184]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[24201]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[24245]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[24262]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[24279]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "\0"
   /* _mesa_function_pool[24302]: GetnUniformi64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[24329]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[24365]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[24382]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[24413]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[24440]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[24471]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[24523]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[24542]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[24557]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[24588]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[24610]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[24627]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[24660]: ProgramUniform2i64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[24690]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[24702]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[24723]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[24768]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[24817]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[24842]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[24886]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[24934]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[24973]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[24995]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[25043]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[25091]: ProgramUniform4ui64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[25124]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[25144]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[25173]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[25196]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[25216]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[25237]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[25267]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[25281]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[25296]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "\0"
   /* _mesa_function_pool[25335]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[25364]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[25375]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[25394]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[25444]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[25462]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[25493]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[25529]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[25547]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[25560]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[25579]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[25598]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[25629]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[25647]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[25669]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[25735]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[25760]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[25781]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[25801]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[25821]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[25841]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[25866]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[25892]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[25910]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[25930]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[25947]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[25967]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[26000]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[26043]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[26076]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[26110]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[26140]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[26175]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[26260]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[26288]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[26303]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[26321]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[26352]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[26394]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[26419]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[26474]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[26494]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[26551]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[26583]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[26637]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[26656]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[26671]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[26696]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[26726]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[26741]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[26768]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[26785]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[26812]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[26829]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[26856]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[26903]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[26953]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[27000]: ProgramUniform1i64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[27029]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[27066]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[27106]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[27130]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[27178]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[27228]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[27265]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[27279]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[27306]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[27339]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "glBlendEquationSeparateiEXT\0"
   "glBlendEquationSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[27487]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[27504]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[27549]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[27571]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[27607]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[27689]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[27718]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[27761]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[27776]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[27826]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[27859]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[27884]: Uniform1i64ARB (will be remapped) */
   "ii\0"
   "glUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[27905]: DepthRangeArrayfvOES (will be remapped) */
   "iip\0"
   "glDepthRangeArrayfvOES\0"
   "\0"
   /* _mesa_function_pool[27933]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[27988]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[28016]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[28071]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[28101]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[28118]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[28134]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[28163]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[28177]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[28215]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[28230]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[28265]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[28292]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[28310]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[28340]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[28358]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[28393]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[28419]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[28456]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[28498]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[28524]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[28569]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[28585]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[28641]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[28658]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[28690]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[28707]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[28724]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[28741]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[28771]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[28788]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[28815]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[28832]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[28846]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[28869]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[28897]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[28939]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[28965]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[28997]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[29012]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[29026]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[29059]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[29084]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[29102]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[29119]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[29156]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[29201]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[29258]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[29276]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[29306]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[29321]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[29333]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[29384]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[29406]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[29424]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[29442]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "glBlendFunciEXT\0"
   "glBlendFunciOES\0"
   "\0"
   /* _mesa_function_pool[29530]: ClearNamedFramebufferfi (will be remapped) */
   "iiifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[29563]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[29595]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[29622]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[29656]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "glScissorIndexedOES\0"
   "\0"
   /* _mesa_function_pool[29700]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[29708]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[29742]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[29769]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[29798]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[29849]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[29861]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[29907]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[29929]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[29962]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[30006]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[30035]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[30065]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[30109]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[30126]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "glReadnPixels\0"
   "glReadnPixelsKHR\0"
   "\0"
   /* _mesa_function_pool[30184]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[30218]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[30234]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[30268]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[30284]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[30319]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[30335]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[30351]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[30367]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[30379]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[30415]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[30461]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[30521]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[30539]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[30551]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[30563]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[30584]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[30596]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[30622]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[30678]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[30693]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[30708]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[30722]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[30757]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[30781]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[30796]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[30834]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[30861]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[30906]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[30923]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[30944]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[30961]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[30994]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[31019]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[31066]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[31103]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[31119]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervKHR\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[31171]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[31216]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[31231]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[31247]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[31292]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[31311]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[31331]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[31349]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[31368]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[31390]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[31433]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[31499]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[31534]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[31563]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[31623]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[31641]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[31685]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[31703]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[31721]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[31750]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[31768]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[31792]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[31833]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[31861]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[31879]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[31910]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[31938]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[31972]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[31997]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[32016]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[32032]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[32049]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[32072]: Uniform2i64ARB (will be remapped) */
   "iii\0"
   "glUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[32094]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[32177]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[32208]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[32262]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[32299]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[32329]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[32376]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[32391]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[32413]: ProgramUniform2i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[32444]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[32469]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[32495]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "\0"
   /* _mesa_function_pool[32521]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[32547]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[32573]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[32627]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "glBlendEquationiEXT\0"
   "glBlendEquationiOES\0"
   "\0"
   /* _mesa_function_pool[32734]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[32781]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[32813]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[32831]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[32856]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[32899]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[32944]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[32978]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[33003]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[33037]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[33064]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[33113]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[33162]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[33195]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[33222]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[33252]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[33296]: ProgramUniform4ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[33328]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[33352]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[33396]: ProgramUniform1i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[33427]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[33451]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[33484]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "glTexStorage3DMultisampleOES\0"
   "\0"
   /* _mesa_function_pool[33548]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[33568]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[33594]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[33620]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "glScissorIndexedvOES\0"
   "\0"
   /* _mesa_function_pool[33663]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[33693]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[33725]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[33742]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[33760]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[33803]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[33825]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[33843]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[33866]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[33930]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[33983]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[34009]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[34031]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[34049]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[34072]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[34122]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[34157]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[34172]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[34204]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[34223]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[34243]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[34258]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[34272]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[34288]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[34309]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[34323]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[34354]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "glGetFragDataIndexEXT\0"
   "\0"
   /* _mesa_function_pool[34399]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[34425]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[34474]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[34491]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[34508]: Uniform4ui64vARB (will be remapped) */
   "iip\0"
   "glUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[34532]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[34577]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[34604]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[34626]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[34651]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[34676]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[34719]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[34753]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[34771]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[34813]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[34899]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[34926]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[34952]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[34983]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[35030]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[35048]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[35080]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[35106]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[35141]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[35180]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[35227]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[35266]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[35297]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[35317]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[35342]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[35368]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[35422]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[35440]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[35486]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[35524]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[35551]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[35579]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[35595]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[35611]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[35627]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[35681]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[35712]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[35768]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "glGetTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[35842]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[35858]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[35914]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[36006]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[36078]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[36108]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[36125]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[36163]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[36193]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[36252]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[36289]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[36316]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[36334]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[36371]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[36409]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[36424]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[36456]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "glTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[36521]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[36537]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[36558]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[36592]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[36633]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[36660]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[36687]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[36730]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[36746]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[36767]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36785]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[36819]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[36864]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[36901]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[36930]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[36973]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[36996]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[37042]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[37104]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[37125]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[37215]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[37247]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[37267]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[37287]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[37312]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[37346]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[37392]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "glViewportIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[37440]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[37486]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[37507]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "glDebugMessageControlKHR\0"
   "\0"
   /* _mesa_function_pool[37587]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[37633]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[37673]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[37707]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "iiiiii\0"
   "glMultiDrawElementsIndirectCountARB\0"
   "\0"
   /* _mesa_function_pool[37751]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[37776]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[37789]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[37832]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[37849]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[37876]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[37894]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[37912]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[37964]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[38015]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[38054]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[38070]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[38113]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[38159]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[38188]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[38215]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[38233]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[38270]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[38321]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[38354]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[38373]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[38393]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[38412]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[38447]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[38494]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[38514]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[38541]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[38577]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[38632]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[38647]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[38673]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[38701]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[38732]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "glDrawArraysInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[38810]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[38853]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[38867]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[38909]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[38944]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[38972]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[38986]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[39015]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[39075]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[39141]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[39161]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[39186]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[39219]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[39266]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[39284]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[39317]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[39365]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[39422]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[39441]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[39461]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[39479]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[39505]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[39532]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[39599]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[39653]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "glIsEnablediEXT\0"
   "glIsEnablediOES\0"
   "\0"
   /* _mesa_function_pool[39724]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[39754]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[39770]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[39858]: Uniform1i64vARB (will be remapped) */
   "iip\0"
   "glUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[39881]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[39908]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[39922]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "glTexBufferEXT\0"
   "glTexBufferOES\0"
   "\0"
   /* _mesa_function_pool[39984]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[40001]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[40017]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[40033]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[40049]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "glGetnUniformfv\0"
   "glGetnUniformfvKHR\0"
   "\0"
   /* _mesa_function_pool[40109]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[40125]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[40152]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[40185]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[40206]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[40257]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[40282]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[40338]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[40363]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[40379]: GetUniformui64vARB (will be remapped) */
   "iip\0"
   "glGetUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[40405]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[40424]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[40467]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[40491]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[40506]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[40535]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "\0"
   /* _mesa_function_pool[40558]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[40579]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[40603]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[40651]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[40677]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[40707]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[40726]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[40761]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[40787]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[40811]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[40846]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[40886]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[40952]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[40969]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[40995]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[41040]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[41067]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[41084]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[41129]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[41156]: PrimitiveBoundingBox (will be remapped) */
   "ffffffff\0"
   "glPrimitiveBoundingBox\0"
   "glPrimitiveBoundingBoxARB\0"
   "glPrimitiveBoundingBoxEXT\0"
   "glPrimitiveBoundingBoxOES\0"
   "\0"
   /* _mesa_function_pool[41267]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[41304]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[41331]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[41347]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[41388]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[41435]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[41470]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[41487]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[41536]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[41571]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[41590]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[41640]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[41670]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[41697]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[41723]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[41761]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[41793]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[41812]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[41840]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[41877]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[41922]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[41937]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[41984]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[42002]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "glGetFloati_vOES\0"
   "\0"
   /* _mesa_function_pool[42038]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[42083]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[42103]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[42163]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[42209]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[42237]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[42263]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[42277]: ProgramUniform2ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[42309]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[42329]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[42349]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[42369]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[42397]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[42445]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[42483]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[42501]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[42526]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[42541]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "glCopyImageSubDataEXT\0"
   "glCopyImageSubDataOES\0"
   "\0"
   /* _mesa_function_pool[42621]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[42670]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[42688]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[42706]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[42727]: Uniform4i64vARB (will be remapped) */
   "iip\0"
   "glUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[42750]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[42772]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[42802]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[42814]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[42835]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[42864]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[42922]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[42938]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[42963]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[43021]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[43037]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[43053]: ShaderStorageBlockBinding (will be remapped) */
   "iii\0"
   "glShaderStorageBlockBinding\0"
   "\0"
   /* _mesa_function_pool[43086]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[43102]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[43118]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[43140]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[43170]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[43188]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[43216]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "\0"
   /* _mesa_function_pool[43240]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[43273]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[43301]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[43340]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[43355]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[43390]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[43407]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[43428]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[43473]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[43505]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[43540]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[43586]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[43613]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[43649]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[43679]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[43727]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[43747]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43791]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[43825]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[43882]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[43907]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43971]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[43990]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[44016]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[44030]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[44048]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[44073]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[44149]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[44192]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[44226]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[44240]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[44256]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[44270]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[44315]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[44344]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[44406]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[44422]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[44484]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[44501]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[44538]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[44570]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "glDrawRangeElementsBaseVertexEXT\0"
   "glDrawRangeElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[44675]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[44708]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[44732]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[44752]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[44798]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[44826]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[44865]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[44883]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[44905]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[44920]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[44937]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[44969]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[44999]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[45015]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[45037]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[45070]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[45105]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[45115]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[45161]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[45208]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[45238]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[45263]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[45297]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[45331]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[45351]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[45375]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[45418]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[45495]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "\0"
   /* _mesa_function_pool[45524]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[45544]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[45566]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[45579]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[45594]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[45609]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[45637]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[45673]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[45698]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[45754]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[45785]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[45812]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[45835]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[45855]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[45881]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[45907]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[45946]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[45981]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[46039]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[46055]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[46079]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[46093]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[46117]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[46138]: ProgramUniform2ui64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[46169]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[46184]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "glGetTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[46261]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[46326]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[46369]: Uniform4i64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[46393]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[46431]: ProgramUniform3ui64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[46463]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[46485]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[46536]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[46567]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[46583]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[46612]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[46643]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[46666]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[46685]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[46722]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[46743]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[46770]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[46808]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[46833]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[46868]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[46919]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[46938]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[46973]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[46996]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[47016]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[47071]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[47120]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[47163]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[47206]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[47230]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[47245]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[47283]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[47307]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[47322]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[47340]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[47358]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[47373]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[47397]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[47412]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[47427]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[47475]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[47546]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[47610]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[47643]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[47677]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[47707]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[47816]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[47851]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[47876]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[47902]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[47958]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[48007]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[48026]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[48044]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[48079]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[48098]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[48117]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[48141]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[48157]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[48224]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[48242]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[48293]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[48328]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[48346]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[48369]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[48396]: NamedBufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glNamedBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[48433]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[48470]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[48520]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[48553]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[48598]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[48618]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[48634]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[48650]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[48702]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[48735]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[48756]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[48777]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[48810]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[48834]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[48876]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[48901]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[48921]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[48948]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[48972]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[48985]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[49030]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[49053]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[49073]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[49088]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[49114]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[49129]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[49146]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[49175]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[49207]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[49224]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[49249]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[49264]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[49296]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "glMultiDrawElementsBaseVertexEXT\0"
   "glMultiDrawElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[49400]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[49428]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[49453]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[49517]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[49548]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[49585]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[49606]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[49639]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[49663]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[49690]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[49709]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[49762]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[49800]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[49831]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[49845]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[49866]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "glScissorArrayvOES\0"
   "\0"
   /* _mesa_function_pool[49906]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[49935]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[49952]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[49968]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "\0"
   /* _mesa_function_pool[49998]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[50012]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[50031]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[50050]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[50074]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[50110]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[50140]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[50162]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[50198]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[50227]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[50278]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[50295]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "\0"
   /* _mesa_function_pool[50317]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[50334]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[50357]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[50374]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[50391]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[50442]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[50460]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[50492]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[50523]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[50541]: ProgramUniform3i64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[50572]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[50605]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[50651]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[50697]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[50721]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[50776]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[50839]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[50866]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[50884]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[50911]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[50925]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[50970]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[50991]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[51016]: WindowRectanglesEXT (will be remapped) */
   "iip\0"
   "glWindowRectanglesEXT\0"
   "\0"
   /* _mesa_function_pool[51043]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[51057]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[51071]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[51105]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[51119]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[51146]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[51196]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[51218]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[51237]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[51267]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[51286]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[51314]: ProgramUniform1ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[51346]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[51365]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[51384]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[51402]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[51425]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[51458]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[51478]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[51510]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[51545]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "glObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[51587]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[51600]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[51628]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[51646]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[51677]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "glMinSampleShadingOES\0"
   "\0"
   /* _mesa_function_pool[51743]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[51841]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[51871]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[51926]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[51951]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[51991]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[52016]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 21647, CompressedTexImage1D_remap_index },
   { 18676, CompressedTexImage2D_remap_index },
   { 13726, CompressedTexImage3D_remap_index },
   { 33866, CompressedTexSubImage1D_remap_index },
   { 40886, CompressedTexSubImage2D_remap_index },
   {  7216, CompressedTexSubImage3D_remap_index },
   {  4843, GetCompressedTexImage_remap_index },
   { 20775, LoadTransposeMatrixd_remap_index },
   { 20723, LoadTransposeMatrixf_remap_index },
   { 37912, MultTransposeMatrixd_remap_index },
   { 15130, MultTransposeMatrixf_remap_index },
   { 36592, SampleCoverage_remap_index },
   {  3990, BlendFuncSeparate_remap_index },
   { 24842, FogCoordPointer_remap_index },
   { 45208, FogCoordd_remap_index },
   { 44937, FogCoorddv_remap_index },
   { 36819, MultiDrawArrays_remap_index },
   { 34813, PointParameterf_remap_index },
   {  5645, PointParameterfv_remap_index },
   { 34771, PointParameteri_remap_index },
   {  9969, PointParameteriv_remap_index },
   {  6067, SecondaryColor3b_remap_index },
   { 44752, SecondaryColor3bv_remap_index },
   { 38113, SecondaryColor3d_remap_index },
   { 13859, SecondaryColor3dv_remap_index },
   {  6197, SecondaryColor3i_remap_index },
   { 42163, SecondaryColor3iv_remap_index },
   {  5943, SecondaryColor3s_remap_index },
   { 17824, SecondaryColor3sv_remap_index },
   { 24995, SecondaryColor3ub_remap_index },
   {  8370, SecondaryColor3ubv_remap_index },
   { 25043, SecondaryColor3ui_remap_index },
   { 27130, SecondaryColor3uiv_remap_index },
   { 24886, SecondaryColor3us_remap_index },
   { 11187, SecondaryColor3usv_remap_index },
   { 39365, SecondaryColorPointer_remap_index },
   { 13458, WindowPos2d_remap_index },
   { 19648, WindowPos2dv_remap_index },
   { 13405, WindowPos2f_remap_index },
   { 26419, WindowPos2fv_remap_index },
   { 13537, WindowPos2i_remap_index },
   {  7534, WindowPos2iv_remap_index },
   { 13590, WindowPos2s_remap_index },
   { 51871, WindowPos2sv_remap_index },
   { 18159, WindowPos3d_remap_index },
   { 17521, WindowPos3dv_remap_index },
   { 18272, WindowPos3f_remap_index },
   {  9854, WindowPos3fv_remap_index },
   { 18381, WindowPos3i_remap_index },
   { 50721, WindowPos3iv_remap_index },
   { 18497, WindowPos3s_remap_index },
   { 28016, WindowPos3sv_remap_index },
   {  7416, BeginQuery_remap_index },
   { 50572, BindBuffer_remap_index },
   { 43505, BufferData_remap_index },
   { 11731, BufferSubData_remap_index },
   { 35141, DeleteBuffers_remap_index },
   { 25296, DeleteQueries_remap_index },
   { 22618, EndQuery_remap_index },
   { 47610, GenBuffers_remap_index },
   {  2158, GenQueries_remap_index },
   { 32208, GetBufferParameteriv_remap_index },
   { 47475, GetBufferPointerv_remap_index },
   { 35180, GetBufferSubData_remap_index },
   {  9501, GetQueryObjectiv_remap_index },
   {  9072, GetQueryObjectuiv_remap_index },
   { 14052, GetQueryiv_remap_index },
   { 21263, IsBuffer_remap_index },
   { 32495, IsQuery_remap_index },
   { 14191, MapBuffer_remap_index },
   { 50227, UnmapBuffer_remap_index },
   {   330, AttachShader_remap_index },
   { 41590, BindAttribLocation_remap_index },
   { 47707, BlendEquationSeparate_remap_index },
   { 36371, CompileShader_remap_index },
   { 28340, CreateProgram_remap_index },
   { 35030, CreateShader_remap_index },
   { 23571, DeleteProgram_remap_index },
   { 36316, DeleteShader_remap_index },
   { 49690, DetachShader_remap_index },
   { 39015, DisableVertexAttribArray_remap_index },
   { 26175, DrawBuffers_remap_index },
   { 42963, EnableVertexAttribArray_remap_index },
   { 42397, GetActiveAttrib_remap_index },
   { 48470, GetActiveUniform_remap_index },
   { 20241, GetAttachedShaders_remap_index },
   { 31019, GetAttribLocation_remap_index },
   { 13023, GetProgramInfoLog_remap_index },
   { 25910, GetProgramiv_remap_index },
   {  4536, GetShaderInfoLog_remap_index },
   {  8763, GetShaderSource_remap_index },
   { 19977, GetShaderiv_remap_index },
   {  7467, GetUniformLocation_remap_index },
   { 15283, GetUniformfv_remap_index },
   {  2460, GetUniformiv_remap_index },
   { 39770, GetVertexAttribPointerv_remap_index },
   { 47427, GetVertexAttribdv_remap_index },
   { 40603, GetVertexAttribfv_remap_index },
   { 43679, GetVertexAttribiv_remap_index },
   {  5037, IsProgram_remap_index },
   { 44226, IsShader_remap_index },
   { 33003, LinkProgram_remap_index },
   { 43301, ShaderSource_remap_index },
   { 43273, StencilFuncSeparate_remap_index },
   { 40969, StencilMaskSeparate_remap_index },
   { 42621, StencilOpSeparate_remap_index },
   { 46536, Uniform1f_remap_index },
   {  9709, Uniform1fv_remap_index },
   { 46612, Uniform1i_remap_index },
   { 21499, Uniform1iv_remap_index },
   { 49175, Uniform2f_remap_index },
   { 26076, Uniform2fv_remap_index },
   { 49264, Uniform2i_remap_index },
   { 23803, Uniform2iv_remap_index },
   {   937, Uniform3f_remap_index },
   { 43791, Uniform3fv_remap_index },
   {   857, Uniform3i_remap_index },
   { 45263, Uniform3iv_remap_index },
   {  5396, Uniform4f_remap_index },
   { 10492, Uniform4fv_remap_index },
   {  5343, Uniform4i_remap_index },
   { 52016, Uniform4iv_remap_index },
   { 11848, UniformMatrix2fv_remap_index },
   { 26856, UniformMatrix3fv_remap_index },
   { 12408, UniformMatrix4fv_remap_index },
   { 46770, UseProgram_remap_index },
   { 28456, ValidateProgram_remap_index },
   { 21606, VertexAttrib1d_remap_index },
   { 44149, VertexAttrib1dv_remap_index },
   { 21796, VertexAttrib1s_remap_index },
   { 40424, VertexAttrib1sv_remap_index },
   {  9635, VertexAttrib2d_remap_index },
   { 27718, VertexAttrib2dv_remap_index },
   {  9547, VertexAttrib2s_remap_index },
   { 16970, VertexAttrib2sv_remap_index },
   { 14086, VertexAttrib3d_remap_index },
   { 26000, VertexAttrib3dv_remap_index },
   { 13961, VertexAttrib3s_remap_index },
   { 46326, VertexAttrib3sv_remap_index },
   { 14263, VertexAttrib4Nbv_remap_index },
   { 32899, VertexAttrib4Niv_remap_index },
   { 24139, VertexAttrib4Nsv_remap_index },
   {  1608, VertexAttrib4Nub_remap_index },
   { 38447, VertexAttrib4Nubv_remap_index },
   { 12482, VertexAttrib4Nuiv_remap_index },
   { 41388, VertexAttrib4Nusv_remap_index },
   { 11117, VertexAttrib4bv_remap_index },
   { 33252, VertexAttrib4d_remap_index },
   { 33760, VertexAttrib4dv_remap_index },
   { 45375, VertexAttrib4iv_remap_index },
   { 33352, VertexAttrib4s_remap_index },
   { 22737, VertexAttrib4sv_remap_index },
   { 12102, VertexAttrib4ubv_remap_index },
   { 24094, VertexAttrib4uiv_remap_index },
   {  1534, VertexAttrib4usv_remap_index },
   { 38577, VertexAttribPointer_remap_index },
   { 34577, UniformMatrix2x3fv_remap_index },
   {   970, UniformMatrix2x4fv_remap_index },
   { 12455, UniformMatrix3x2fv_remap_index },
   { 50884, UniformMatrix3x4fv_remap_index },
   { 45785, UniformMatrix4x2fv_remap_index },
   { 14004, UniformMatrix4x3fv_remap_index },
   { 19883, BeginConditionalRender_remap_index },
   { 28585, BeginTransformFeedback_remap_index },
   {  9459, BindBufferBase_remap_index },
   {  9347, BindBufferRange_remap_index },
   { 26583, BindFragDataLocation_remap_index },
   { 27826, ClampColor_remap_index },
   { 20268, ClearBufferfi_remap_index },
   { 20092, ClearBufferfv_remap_index },
   { 24702, ClearBufferiv_remap_index },
   { 45015, ClearBufferuiv_remap_index },
   { 15755, ColorMaski_remap_index },
   {  7153, Disablei_remap_index },
   { 18213, Enablei_remap_index },
   { 27178, EndConditionalRender_remap_index },
   { 23318, EndTransformFeedback_remap_index },
   { 14485, GetBooleani_v_remap_index },
   { 46868, GetFragDataLocation_remap_index },
   { 24723, GetIntegeri_v_remap_index },
   { 33725, GetStringi_remap_index },
   { 35768, GetTexParameterIiv_remap_index },
   { 46184, GetTexParameterIuiv_remap_index },
   { 36006, GetTransformFeedbackVarying_remap_index },
   {  3473, GetUniformuiv_remap_index },
   { 51146, GetVertexAttribIiv_remap_index },
   { 24471, GetVertexAttribIuiv_remap_index },
   { 39653, IsEnabledi_remap_index },
   { 36456, TexParameterIiv_remap_index },
   { 19703, TexParameterIuiv_remap_index },
   { 46261, TransformFeedbackVaryings_remap_index },
   { 40152, Uniform1ui_remap_index },
   { 30379, Uniform1uiv_remap_index },
   { 29622, Uniform2ui_remap_index },
   { 15829, Uniform2uiv_remap_index },
   { 38909, Uniform3ui_remap_index },
   { 22796, Uniform3uiv_remap_index },
   { 14374, Uniform4ui_remap_index },
   { 21533, Uniform4uiv_remap_index },
   { 41877, VertexAttribI1iv_remap_index },
   { 13812, VertexAttribI1uiv_remap_index },
   {  9120, VertexAttribI4bv_remap_index },
   { 28524, VertexAttribI4sv_remap_index },
   { 10213, VertexAttribI4ubv_remap_index },
   {   466, VertexAttribI4usv_remap_index },
   { 47902, VertexAttribIPointer_remap_index },
   { 10160, PrimitiveRestartIndex_remap_index },
   { 39922, TexBuffer_remap_index },
   {  1770, FramebufferTexture_remap_index },
   { 28310, GetBufferParameteri64v_remap_index },
   { 21159, GetInteger64i_v_remap_index },
   { 48242, VertexAttribDivisor_remap_index },
   { 51677, MinSampleShading_remap_index },
   { 24413, MemoryBarrierByRegion_remap_index },
   {  8808, BindProgramARB_remap_index },
   { 36687, DeleteProgramsARB_remap_index },
   { 18326, GenProgramsARB_remap_index },
   { 17013, GetProgramEnvParameterdvARB_remap_index },
   { 35106, GetProgramEnvParameterfvARB_remap_index },
   { 36864, GetProgramLocalParameterdvARB_remap_index },
   { 44501, GetProgramLocalParameterfvARB_remap_index },
   { 26785, GetProgramStringARB_remap_index },
   { 10046, GetProgramivARB_remap_index },
   { 37215, IsProgramARB_remap_index },
   { 20843, ProgramEnvParameter4dARB_remap_index },
   {  3256, ProgramEnvParameter4dvARB_remap_index },
   { 45981, ProgramEnvParameter4fARB_remap_index },
   { 29201, ProgramEnvParameter4fvARB_remap_index },
   { 27228, ProgramLocalParameter4dARB_remap_index },
   {  4899, ProgramLocalParameter4dvARB_remap_index },
   { 36252, ProgramLocalParameter4fARB_remap_index },
   { 23102, ProgramLocalParameter4fvARB_remap_index },
   { 37287, ProgramStringARB_remap_index },
   { 14444, VertexAttrib1fARB_remap_index },
   { 37789, VertexAttrib1fvARB_remap_index },
   { 26352, VertexAttrib2fARB_remap_index },
   { 16076, VertexAttrib2fvARB_remap_index },
   {   349, VertexAttrib3fARB_remap_index },
   { 31390, VertexAttrib3fvARB_remap_index },
   { 30065, VertexAttrib4fARB_remap_index },
   { 17455, VertexAttrib4fvARB_remap_index },
   { 42750, AttachObjectARB_remap_index },
   { 26829, CreateProgramObjectARB_remap_index },
   { 20166, CreateShaderObjectARB_remap_index },
   { 18735, DeleteObjectARB_remap_index },
   { 45544, DetachObjectARB_remap_index },
   { 43140, GetAttachedObjectsARB_remap_index },
   { 29406, GetHandleARB_remap_index },
   { 24588, GetInfoLogARB_remap_index },
   { 25462, GetObjectParameterfvARB_remap_index },
   { 49800, GetObjectParameterivARB_remap_index },
   {  6840, DrawArraysInstancedARB_remap_index },
   {  8987, DrawElementsInstancedARB_remap_index },
   { 16767, BindFramebuffer_remap_index },
   { 10069, BindRenderbuffer_remap_index },
   { 40206, BlitFramebuffer_remap_index },
   {  7822, CheckFramebufferStatus_remap_index },
   { 23995, DeleteFramebuffers_remap_index },
   { 44073, DeleteRenderbuffers_remap_index },
   { 37125, FramebufferRenderbuffer_remap_index },
   { 40282, FramebufferTexture1D_remap_index },
   { 27607, FramebufferTexture2D_remap_index },
   { 32094, FramebufferTexture3D_remap_index },
   { 44344, FramebufferTextureLayer_remap_index },
   { 47546, GenFramebuffers_remap_index },
   { 39532, GenRenderbuffers_remap_index },
   {  9287, GenerateMipmap_remap_index },
   {  6400, GetFramebufferAttachmentParameteriv_remap_index },
   { 51743, GetRenderbufferParameteriv_remap_index },
   {  8065, IsFramebuffer_remap_index },
   { 30461, IsRenderbuffer_remap_index },
   {   684, RenderbufferStorage_remap_index },
   { 18049, RenderbufferStorageMultisample_remap_index },
   {  6327, FlushMappedBufferRange_remap_index },
   { 36930, MapBufferRange_remap_index },
   { 15922, BindVertexArray_remap_index },
   {  1188, DeleteVertexArrays_remap_index },
   { 47120, GenVertexArrays_remap_index },
   { 46393, IsVertexArray_remap_index },
   { 15614, GetActiveUniformBlockName_remap_index },
   { 51478, GetActiveUniformBlockiv_remap_index },
   { 23392, GetActiveUniformName_remap_index },
   { 16941, GetActiveUniformsiv_remap_index },
   { 49663, GetUniformBlockIndex_remap_index },
   { 12579, GetUniformIndices_remap_index },
   { 41304, UniformBlockBinding_remap_index },
   { 50839, CopyBufferSubData_remap_index },
   { 51196, ClientWaitSync_remap_index },
   { 13688, DeleteSync_remap_index },
   { 41331, FenceSync_remap_index },
   { 45524, GetInteger64v_remap_index },
   { 48098, GetSynciv_remap_index },
   { 18815, IsSync_remap_index },
   { 39754, WaitSync_remap_index },
   { 15649, DrawElementsBaseVertex_remap_index },
   { 20372, DrawElementsInstancedBaseVertex_remap_index },
   { 44570, DrawRangeElementsBaseVertex_remap_index },
   { 49296, MultiDrawElementsBaseVertex_remap_index },
   { 28897, ProvokingVertex_remap_index },
   {  6744, GetMultisamplefv_remap_index },
   { 41984, SampleMaski_remap_index },
   {  2330, TexImage2DMultisample_remap_index },
   { 49606, TexImage3DMultisample_remap_index },
   { 27339, BlendEquationSeparateiARB_remap_index },
   { 32627, BlendEquationiARB_remap_index },
   {  4588, BlendFuncSeparateiARB_remap_index },
   { 29442, BlendFunciARB_remap_index },
   {  1961, BindFragDataLocationIndexed_remap_index },
   { 34354, GetFragDataIndex_remap_index },
   {  3455, BindSampler_remap_index },
   { 50970, DeleteSamplers_remap_index },
   { 42670, GenSamplers_remap_index },
   {  2984, GetSamplerParameterIiv_remap_index },
   {  7064, GetSamplerParameterIuiv_remap_index },
   { 27689, GetSamplerParameterfv_remap_index },
   { 29769, GetSamplerParameteriv_remap_index },
   { 31216, IsSampler_remap_index },
   { 10552, SamplerParameterIiv_remap_index },
   { 14835, SamplerParameterIuiv_remap_index },
   { 34626, SamplerParameterf_remap_index },
   { 45881, SamplerParameterfv_remap_index },
   { 50991, SamplerParameteri_remap_index },
   { 33568, SamplerParameteriv_remap_index },
   { 27776, GetQueryObjecti64v_remap_index },
   {  4959, GetQueryObjectui64v_remap_index },
   { 15430, QueryCounter_remap_index },
   { 44999, ColorP3ui_remap_index },
   {  8219, ColorP3uiv_remap_index },
   { 21291, ColorP4ui_remap_index },
   { 30906, ColorP4uiv_remap_index },
   { 16582, MultiTexCoordP1ui_remap_index },
   { 30596, MultiTexCoordP1uiv_remap_index },
   { 32444, MultiTexCoordP2ui_remap_index },
   { 10972, MultiTexCoordP2uiv_remap_index },
   { 30994, MultiTexCoordP3ui_remap_index },
   {   440, MultiTexCoordP3uiv_remap_index },
   { 47851, MultiTexCoordP4ui_remap_index },
   { 40651, MultiTexCoordP4uiv_remap_index },
   { 43390, NormalP3ui_remap_index },
   { 30521, NormalP3uiv_remap_index },
   { 48876, SecondaryColorP3ui_remap_index },
   {  7012, SecondaryColorP3uiv_remap_index },
   {   177, TexCoordP1ui_remap_index },
   {   664, TexCoordP1uiv_remap_index },
   { 31349, TexCoordP2ui_remap_index },
   { 43727, TexCoordP2uiv_remap_index },
   { 17870, TexCoordP3ui_remap_index },
   { 21361, TexCoordP3uiv_remap_index },
   { 40405, TexCoordP4ui_remap_index },
   {  2077, TexCoordP4uiv_remap_index },
   { 17993, VertexAttribP1ui_remap_index },
   {  5011, VertexAttribP1uiv_remap_index },
   { 35317, VertexAttribP2ui_remap_index },
   {  5989, VertexAttribP2uiv_remap_index },
   {  1656, VertexAttribP3ui_remap_index },
   { 33594, VertexAttribP3uiv_remap_index },
   {  5292, VertexAttribP4ui_remap_index },
   { 19500, VertexAttribP4uiv_remap_index },
   { 41470, VertexP2ui_remap_index },
   { 19257, VertexP2uiv_remap_index },
   { 26812, VertexP3ui_remap_index },
   {  7449, VertexP3uiv_remap_index },
   {  3789, VertexP4ui_remap_index },
   { 51628, VertexP4uiv_remap_index },
   {   832, DrawArraysIndirect_remap_index },
   { 27988, DrawElementsIndirect_remap_index },
   {  7961, GetUniformdv_remap_index },
   { 46567, Uniform1d_remap_index },
   { 17076, Uniform1dv_remap_index },
   { 49207, Uniform2d_remap_index },
   { 33742, Uniform2dv_remap_index },
   {   919, Uniform3d_remap_index },
   { 34753, Uniform3dv_remap_index },
   {  5377, Uniform4d_remap_index },
   { 23183, Uniform4dv_remap_index },
   {  4934, UniformMatrix2dv_remap_index },
   { 26741, UniformMatrix2x3dv_remap_index },
   { 19174, UniformMatrix2x4dv_remap_index },
   { 34651, UniformMatrix3dv_remap_index },
   {  5215, UniformMatrix3x2dv_remap_index },
   {  6015, UniformMatrix3x4dv_remap_index },
   { 20113, UniformMatrix4dv_remap_index },
   { 38514, UniformMatrix4x2dv_remap_index },
   { 21872, UniformMatrix4x3dv_remap_index },
   {  6163, GetActiveSubroutineName_remap_index },
   {  6786, GetActiveSubroutineUniformName_remap_index },
   { 19047, GetActiveSubroutineUniformiv_remap_index },
   { 14237, GetProgramStageiv_remap_index },
   { 15474, GetSubroutineIndex_remap_index },
   {  1498, GetSubroutineUniformLocation_remap_index },
   { 51646, GetUniformSubroutineuiv_remap_index },
   {  7720, UniformSubroutinesuiv_remap_index },
   { 17343, PatchParameterfv_remap_index },
   { 13213, PatchParameteri_remap_index },
   { 13049, BindTransformFeedback_remap_index },
   { 12921, DeleteTransformFeedbacks_remap_index },
   { 42209, DrawTransformFeedback_remap_index },
   {  4793, GenTransformFeedbacks_remap_index },
   { 39161, IsTransformFeedback_remap_index },
   { 36289, PauseTransformFeedback_remap_index },
   { 41812, ResumeTransformFeedback_remap_index },
   { 26671, BeginQueryIndexed_remap_index },
   { 48293, DrawTransformFeedbackStream_remap_index },
   { 22911, EndQueryIndexed_remap_index },
   { 25866, GetQueryIndexediv_remap_index },
   { 23629, ClearDepthf_remap_index },
   { 28358, DepthRangef_remap_index },
   { 45037, GetShaderPrecisionFormat_remap_index },
   {  3964, ReleaseShaderCompiler_remap_index },
   { 29907, ShaderBinary_remap_index },
   { 23422, GetProgramBinary_remap_index },
   { 14308, ProgramBinary_remap_index },
   { 14600, ProgramParameteri_remap_index },
   { 13511, GetVertexAttribLdv_remap_index },
   { 50295, VertexAttribL1d_remap_index },
   {  8018, VertexAttribL1dv_remap_index },
   { 40535, VertexAttribL2d_remap_index },
   { 23369, VertexAttribL2dv_remap_index },
   { 43216, VertexAttribL3d_remap_index },
   { 15983, VertexAttribL3dv_remap_index },
   {  9227, VertexAttribL4d_remap_index },
   { 24279, VertexAttribL4dv_remap_index },
   { 49968, VertexAttribLPointer_remap_index },
   { 31768, DepthRangeArrayv_remap_index },
   { 49428, DepthRangeIndexed_remap_index },
   { 39141, GetDoublei_v_remap_index },
   { 42002, GetFloati_v_remap_index },
   { 49866, ScissorArrayv_remap_index },
   { 29656, ScissorIndexed_remap_index },
   { 33620, ScissorIndexedv_remap_index },
   { 22288, ViewportArrayv_remap_index },
   { 37392, ViewportIndexedf_remap_index },
   { 23470, ViewportIndexedfv_remap_index },
   { 10260, GetGraphicsResetStatusARB_remap_index },
   { 35524, GetnColorTableARB_remap_index },
   {  3389, GetnCompressedTexImageARB_remap_index },
   {  1262, GetnConvolutionFilterARB_remap_index },
   {  5863, GetnHistogramARB_remap_index },
   { 22173, GetnMapdvARB_remap_index },
   { 14726, GetnMapfvARB_remap_index },
   { 40558, GetnMapivARB_remap_index },
   { 46093, GetnMinmaxARB_remap_index },
   {  4441, GetnPixelMapfvARB_remap_index },
   {  7038, GetnPixelMapuivARB_remap_index },
   { 13935, GetnPixelMapusvARB_remap_index },
   { 26260, GetnPolygonStippleARB_remap_index },
   { 34122, GetnSeparableFilterARB_remap_index },
   { 12147, GetnTexImageARB_remap_index },
   { 32978, GetnUniformdvARB_remap_index },
   { 40049, GetnUniformfvARB_remap_index },
   {  3904, GetnUniformivARB_remap_index },
   { 16370, GetnUniformuivARB_remap_index },
   { 30126, ReadnPixelsARB_remap_index },
   { 38732, DrawArraysInstancedBaseInstance_remap_index },
   { 12325, DrawElementsInstancedBaseInstance_remap_index },
   {  3152, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 41723, DrawTransformFeedbackInstanced_remap_index },
   { 16031, DrawTransformFeedbackStreamInstanced_remap_index },
   { 49906, GetInternalformativ_remap_index },
   { 23590, GetActiveAtomicCounterBufferiv_remap_index },
   { 51600, BindImageTexture_remap_index },
   { 25375, MemoryBarrier_remap_index },
   { 40185, TexStorage1D_remap_index },
   { 27549, TexStorage2D_remap_index },
   { 32049, TexStorage3D_remap_index },
   {  1579, TextureStorage1DEXT_remap_index },
   { 41640, TextureStorage2DEXT_remap_index },
   { 26321, TextureStorage3DEXT_remap_index },
   { 42501, ClearBufferData_remap_index },
   {  2498, ClearBufferSubData_remap_index },
   { 36973, DispatchCompute_remap_index },
   {  7906, DispatchComputeIndirect_remap_index },
   { 42541, CopyImageSubData_remap_index },
   { 48117, TextureView_remap_index },
   { 25735, BindVertexBuffer_remap_index },
   { 34926, VertexAttribBinding_remap_index },
   { 35551, VertexAttribFormat_remap_index },
   { 38673, VertexAttribIFormat_remap_index },
   { 43188, VertexAttribLFormat_remap_index },
   { 41129, VertexBindingDivisor_remap_index },
   { 38159, FramebufferParameteri_remap_index },
   { 33162, GetFramebufferParameteriv_remap_index },
   { 14959, GetInternalformati64v_remap_index },
   { 44538, MultiDrawArraysIndirect_remap_index },
   { 21837, MultiDrawElementsIndirect_remap_index },
   { 36163, GetProgramInterfaceiv_remap_index },
   {  3806, GetProgramResourceIndex_remap_index },
   {  1464, GetProgramResourceLocation_remap_index },
   {   101, GetProgramResourceLocationIndex_remap_index },
   { 16149, GetProgramResourceName_remap_index },
   { 48777, GetProgramResourceiv_remap_index },
   { 43053, ShaderStorageBlockBinding_remap_index },
   { 21732, TexBufferRange_remap_index },
   { 45297, TexStorage2DMultisample_remap_index },
   { 33484, TexStorage3DMultisample_remap_index },
   {  4136, BufferStorage_remap_index },
   { 45812, ClearTexImage_remap_index },
   { 15582, ClearTexSubImage_remap_index },
   {  5106, BindBuffersBase_remap_index },
   { 17428, BindBuffersRange_remap_index },
   { 12896, BindImageTextures_remap_index },
   {  3369, BindSamplers_remap_index },
   { 48901, BindTextures_remap_index },
   { 29742, BindVertexBuffers_remap_index },
   { 19219, DispatchComputeGroupSizeARB_remap_index },
   { 22999, MultiDrawArraysIndirectCountARB_remap_index },
   { 37707, MultiDrawElementsIndirectCountARB_remap_index },
   { 42688, ClipControl_remap_index },
   { 23517, BindTextureUnit_remap_index },
   { 46685, BlitNamedFramebuffer_remap_index },
   {  7654, CheckNamedFramebufferStatus_remap_index },
   { 26696, ClearNamedBufferData_remap_index },
   { 46938, ClearNamedBufferSubData_remap_index },
   { 29530, ClearNamedFramebufferfi_remap_index },
   { 29563, ClearNamedFramebufferfv_remap_index },
   {  9677, ClearNamedFramebufferiv_remap_index },
   { 43240, ClearNamedFramebufferuiv_remap_index },
   { 45907, CompressedTextureSubImage1D_remap_index },
   { 41347, CompressedTextureSubImage2D_remap_index },
   { 38810, CompressedTextureSubImage3D_remap_index },
   {  3423, CopyNamedBufferSubData_remap_index },
   { 41761, CopyTextureSubImage1D_remap_index },
   { 37312, CopyTextureSubImage2D_remap_index },
   { 48044, CopyTextureSubImage3D_remap_index },
   {  6651, CreateBuffers_remap_index },
   { 32831, CreateFramebuffers_remap_index },
   {  1128, CreateProgramPipelines_remap_index },
   { 36746, CreateQueries_remap_index },
   { 10343, CreateRenderbuffers_remap_index },
   { 43407, CreateSamplers_remap_index },
   { 33803, CreateTextures_remap_index },
   {  1845, CreateTransformFeedbacks_remap_index },
   { 24817, CreateVertexArrays_remap_index },
   {  9196, DisableVertexArrayAttrib_remap_index },
   { 47677, EnableVertexArrayAttrib_remap_index },
   { 16547, FlushMappedNamedBufferRange_remap_index },
   { 36660, GenerateTextureMipmap_remap_index },
   {   406, GetCompressedTextureImage_remap_index },
   {  5456, GetNamedBufferParameteri64v_remap_index },
   { 27306, GetNamedBufferParameteriv_remap_index },
   { 33222, GetNamedBufferPointerv_remap_index },
   { 13326, GetNamedBufferSubData_remap_index },
   { 47071, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 49762, GetNamedFramebufferParameteriv_remap_index },
   { 35227, GetNamedRenderbufferParameteriv_remap_index },
   { 29026, GetQueryBufferObjecti64v_remap_index },
   { 50492, GetQueryBufferObjectiv_remap_index },
   { 10723, GetQueryBufferObjectui64v_remap_index },
   {  3513, GetQueryBufferObjectuiv_remap_index },
   {  3611, GetTextureImage_remap_index },
   { 38412, GetTextureLevelParameterfv_remap_index },
   { 41536, GetTextureLevelParameteriv_remap_index },
   { 16737, GetTextureParameterIiv_remap_index },
   { 25598, GetTextureParameterIuiv_remap_index },
   { 28134, GetTextureParameterfv_remap_index },
   { 31721, GetTextureParameteriv_remap_index },
   { 18125, GetTransformFeedbacki64_v_remap_index },
   {  4396, GetTransformFeedbacki_v_remap_index },
   { 28741, GetTransformFeedbackiv_remap_index },
   {  6569, GetVertexArrayIndexed64iv_remap_index },
   { 36424, GetVertexArrayIndexediv_remap_index },
   { 18791, GetVertexArrayiv_remap_index },
   { 35486, InvalidateNamedFramebufferData_remap_index },
   { 30861, InvalidateNamedFramebufferSubData_remap_index },
   { 11256, MapNamedBuffer_remap_index },
   { 14163, MapNamedBufferRange_remap_index },
   { 50050, NamedBufferData_remap_index },
   { 12730, NamedBufferStorage_remap_index },
   { 21705, NamedBufferSubData_remap_index },
   { 12757, NamedFramebufferDrawBuffer_remap_index },
   { 40811, NamedFramebufferDrawBuffers_remap_index },
   { 29708, NamedFramebufferParameteri_remap_index },
   { 24627, NamedFramebufferReadBuffer_remap_index },
   { 36334, NamedFramebufferRenderbuffer_remap_index },
   {  5491, NamedFramebufferTexture_remap_index },
   { 12790, NamedFramebufferTextureLayer_remap_index },
   { 10013, NamedRenderbufferStorage_remap_index },
   { 31247, NamedRenderbufferStorageMultisample_remap_index },
   { 25216, TextureBuffer_remap_index },
   { 13143, TextureBufferRange_remap_index },
   { 41670, TextureParameterIiv_remap_index },
   { 31910, TextureParameterIuiv_remap_index },
   { 40257, TextureParameterf_remap_index },
   {  2528, TextureParameterfv_remap_index },
   { 40338, TextureParameteri_remap_index },
   { 28393, TextureParameteriv_remap_index },
   { 11938, TextureStorage1D_remap_index },
   { 16711, TextureStorage2D_remap_index },
   { 47245, TextureStorage2DMultisample_remap_index },
   { 21307, TextureStorage3D_remap_index },
   {  3865, TextureStorage3DMultisample_remap_index },
   { 30006, TextureSubImage1D_remap_index },
   { 34952, TextureSubImage2D_remap_index },
   { 38321, TextureSubImage3D_remap_index },
   { 21899, TransformFeedbackBufferBase_remap_index },
   { 17117, TransformFeedbackBufferRange_remap_index },
   {  5193, UnmapNamedBuffer_remap_index },
   { 28658, VertexArrayAttribBinding_remap_index },
   { 14410, VertexArrayAttribFormat_remap_index },
   { 17594, VertexArrayAttribIFormat_remap_index },
   {  3070, VertexArrayAttribLFormat_remap_index },
   { 19310, VertexArrayBindingDivisor_remap_index },
   { 17889, VertexArrayElementBuffer_remap_index },
   { 48702, VertexArrayVertexBuffer_remap_index },
   { 20337, VertexArrayVertexBuffers_remap_index },
   {  2270, GetCompressedTextureSubImage_remap_index },
   {  8263, GetTextureSubImage_remap_index },
   { 21231, BufferPageCommitmentARB_remap_index },
   { 48396, NamedBufferPageCommitmentARB_remap_index },
   { 22886, GetUniformi64vARB_remap_index },
   { 40379, GetUniformui64vARB_remap_index },
   { 24302, GetnUniformi64vARB_remap_index },
   { 21400, GetnUniformui64vARB_remap_index },
   { 27000, ProgramUniform1i64ARB_remap_index },
   { 33396, ProgramUniform1i64vARB_remap_index },
   {  7386, ProgramUniform1ui64ARB_remap_index },
   { 51314, ProgramUniform1ui64vARB_remap_index },
   { 24660, ProgramUniform2i64ARB_remap_index },
   { 32413, ProgramUniform2i64vARB_remap_index },
   { 46138, ProgramUniform2ui64ARB_remap_index },
   { 42277, ProgramUniform2ui64vARB_remap_index },
   { 50541, ProgramUniform3i64ARB_remap_index },
   {    70, ProgramUniform3i64vARB_remap_index },
   { 46431, ProgramUniform3ui64ARB_remap_index },
   {  2882, ProgramUniform3ui64vARB_remap_index },
   { 16641, ProgramUniform4i64ARB_remap_index },
   { 20610, ProgramUniform4i64vARB_remap_index },
   { 25091, ProgramUniform4ui64ARB_remap_index },
   { 33296, ProgramUniform4ui64vARB_remap_index },
   { 27884, Uniform1i64ARB_remap_index },
   { 39858, Uniform1i64vARB_remap_index },
   {  3589, Uniform1ui64ARB_remap_index },
   {  8041, Uniform1ui64vARB_remap_index },
   { 32072, Uniform2i64ARB_remap_index },
   {  6721, Uniform2i64vARB_remap_index },
   { 10949, Uniform2ui64ARB_remap_index },
   { 21966, Uniform2ui64vARB_remap_index },
   { 17970, Uniform3i64ARB_remap_index },
   { 17498, Uniform3i64vARB_remap_index },
   {  2803, Uniform3ui64ARB_remap_index },
   { 21135, Uniform3ui64vARB_remap_index },
   { 46369, Uniform4i64ARB_remap_index },
   { 42727, Uniform4i64vARB_remap_index },
   {  8633, Uniform4ui64ARB_remap_index },
   { 34508, Uniform4ui64vARB_remap_index },
   {  7935, InvalidateBufferData_remap_index },
   { 45754, InvalidateBufferSubData_remap_index },
   { 25335, InvalidateFramebuffer_remap_index },
   { 19121, InvalidateSubFramebuffer_remap_index },
   { 14349, InvalidateTexImage_remap_index },
   { 30184, InvalidateTexSubImage_remap_index },
   { 15182, PolygonOffsetEXT_remap_index },
   { 42706, DrawTexfOES_remap_index },
   { 29424, DrawTexfvOES_remap_index },
   {  1034, DrawTexiOES_remap_index },
   { 35422, DrawTexivOES_remap_index },
   { 14530, DrawTexsOES_remap_index },
   { 25529, DrawTexsvOES_remap_index },
   { 30923, DrawTexxOES_remap_index },
   { 44865, DrawTexxvOES_remap_index },
   { 28788, PointSizePointerOES_remap_index },
   {   997, QueryMatrixxOES_remap_index },
   { 32262, SampleMaskSGIS_remap_index },
   { 38867, SamplePatternSGIS_remap_index },
   { 49224, ColorPointerEXT_remap_index },
   { 32521, EdgeFlagPointerEXT_remap_index },
   { 15259, IndexPointerEXT_remap_index },
   { 15449, NormalPointerEXT_remap_index },
   { 31833, TexCoordPointerEXT_remap_index },
   { 28498, VertexPointerEXT_remap_index },
   { 49146, DiscardFramebufferEXT_remap_index },
   { 12845, ActiveShaderProgram_remap_index },
   { 19450, BindProgramPipeline_remap_index },
   { 32573, CreateShaderProgramv_remap_index },
   {  4307, DeleteProgramPipelines_remap_index },
   { 29798, GenProgramPipelines_remap_index },
   {  9743, GetProgramPipelineInfoLog_remap_index },
   { 35627, GetProgramPipelineiv_remap_index },
   { 29861, IsProgramPipeline_remap_index },
   { 51458, LockArraysEXT_remap_index },
   { 50697, ProgramUniform1d_remap_index },
   { 35080, ProgramUniform1dv_remap_index },
   { 50651, ProgramUniform1f_remap_index },
   { 11433, ProgramUniform1fv_remap_index },
   { 50605, ProgramUniform1i_remap_index },
   { 17725, ProgramUniform1iv_remap_index },
   { 39317, ProgramUniform1ui_remap_index },
   { 50391, ProgramUniform1uiv_remap_index },
   {  2710, ProgramUniform2d_remap_index },
   { 11362, ProgramUniform2dv_remap_index },
   {  2663, ProgramUniform2f_remap_index },
   { 41487, ProgramUniform2fv_remap_index },
   {  2735, ProgramUniform2i_remap_index },
   { 24768, ProgramUniform2iv_remap_index },
   {  8658, ProgramUniform2ui_remap_index },
   { 10757, ProgramUniform2uiv_remap_index },
   {  5523, ProgramUniform3d_remap_index },
   {  5430, ProgramUniform3dv_remap_index },
   {  5549, ProgramUniform3f_remap_index },
   { 34425, ProgramUniform3fv_remap_index },
   {  5597, ProgramUniform3i_remap_index },
   { 15500, ProgramUniform3iv_remap_index },
   { 17774, ProgramUniform3ui_remap_index },
   { 20987, ProgramUniform3uiv_remap_index },
   { 33037, ProgramUniform4d_remap_index },
   { 35342, ProgramUniform4dv_remap_index },
   { 33064, ProgramUniform4f_remap_index },
   { 47958, ProgramUniform4fv_remap_index },
   { 33113, ProgramUniform4i_remap_index },
   {  2191, ProgramUniform4iv_remap_index },
   { 46485, ProgramUniform4ui_remap_index },
   { 37964, ProgramUniform4uiv_remap_index },
   { 15549, ProgramUniformMatrix2dv_remap_index },
   { 23040, ProgramUniformMatrix2fv_remap_index },
   { 18756, ProgramUniformMatrix2x3dv_remap_index },
   { 25669, ProgramUniformMatrix2x3fv_remap_index },
   {  2123, ProgramUniformMatrix2x4dv_remap_index },
   {  9393, ProgramUniformMatrix2x4fv_remap_index },
   { 51425, ProgramUniformMatrix3dv_remap_index },
   { 44422, ProgramUniformMatrix3fv_remap_index },
   { 30722, ProgramUniformMatrix3x2dv_remap_index },
   { 39075, ProgramUniformMatrix3x2fv_remap_index },
   { 26140, ProgramUniformMatrix3x4dv_remap_index },
   { 31433, ProgramUniformMatrix3x4fv_remap_index },
   { 44675, ProgramUniformMatrix4dv_remap_index },
   { 37042, ProgramUniformMatrix4fv_remap_index },
   { 45946, ProgramUniformMatrix4x2dv_remap_index },
   {  2597, ProgramUniformMatrix4x2fv_remap_index },
   { 16224, ProgramUniformMatrix4x3dv_remap_index },
   {  8901, ProgramUniformMatrix4x3fv_remap_index },
   { 45331, UnlockArraysEXT_remap_index },
   { 36996, UseProgramStages_remap_index },
   {  1903, ValidateProgramPipeline_remap_index },
   { 19526, DebugMessageCallback_remap_index },
   { 37507, DebugMessageControl_remap_index },
   { 18570, DebugMessageInsert_remap_index },
   {  8418, GetDebugMessageLog_remap_index },
   {  8175, GetObjectLabel_remap_index },
   { 14551, GetObjectPtrLabel_remap_index },
   {  7335, ObjectLabel_remap_index },
   { 51545, ObjectPtrLabel_remap_index },
   { 21569, PopDebugGroup_remap_index },
   { 17155, PushDebugGroup_remap_index },
   { 10114, SecondaryColor3fEXT_remap_index },
   {  9589, SecondaryColor3fvEXT_remap_index },
   { 34072, MultiDrawElementsEXT_remap_index },
   { 12977, FogCoordfEXT_remap_index },
   { 21934, FogCoordfvEXT_remap_index },
   {  5084, ResizeBuffersMESA_remap_index },
   { 40467, WindowPos4dMESA_remap_index },
   { 32391, WindowPos4dvMESA_remap_index },
   {  5242, WindowPos4fMESA_remap_index },
   { 13704, WindowPos4fvMESA_remap_index },
   { 11069, WindowPos4iMESA_remap_index },
   {  4514, WindowPos4ivMESA_remap_index },
   { 33328, WindowPos4sMESA_remap_index },
   {  7313, WindowPos4svMESA_remap_index },
   { 34172, MultiModeDrawArraysIBM_remap_index },
   { 23768, MultiModeDrawElementsIBM_remap_index },
   { 38986, AreProgramsResidentNV_remap_index },
   { 47373, ExecuteProgramNV_remap_index },
   { 35048, GetProgramParameterdvNV_remap_index },
   { 43473, GetProgramParameterfvNV_remap_index },
   { 23137, GetProgramStringNV_remap_index },
   { 19007, GetProgramivNV_remap_index },
   { 22218, GetTrackMatrixivNV_remap_index },
   { 22699, GetVertexAttribdvNV_remap_index },
   { 20583, GetVertexAttribfvNV_remap_index },
   { 19343, GetVertexAttribivNV_remap_index },
   { 43118, LoadProgramNV_remap_index },
   { 23868, ProgramParameters4dvNV_remap_index },
   { 24557, ProgramParameters4fvNV_remap_index },
   {  7688, RequestResidentProgramsNV_remap_index },
   { 34604, TrackMatrixNV_remap_index },
   { 17094, VertexAttrib1dNV_remap_index },
   { 33427, VertexAttrib1dvNV_remap_index },
   { 33843, VertexAttrib1fNV_remap_index },
   { 49639, VertexAttrib1fvNV_remap_index },
   { 25173, VertexAttrib1sNV_remap_index },
   { 45351, VertexAttrib1svNV_remap_index },
   { 22194, VertexAttrib2dNV_remap_index },
   { 40787, VertexAttrib2dvNV_remap_index },
   { 22975, VertexAttrib2fNV_remap_index },
   { 30757, VertexAttrib2fvNV_remap_index },
   { 15355, VertexAttrib2sNV_remap_index },
   {  6988, VertexAttrib2svNV_remap_index },
   { 43882, VertexAttrib3dNV_remap_index },
   { 46055, VertexAttrib3dvNV_remap_index },
   {  6042, VertexAttrib3fNV_remap_index },
   { 48810, VertexAttrib3fvNV_remap_index },
   {  8845, VertexAttrib3sNV_remap_index },
   { 22245, VertexAttrib3svNV_remap_index },
   { 10395, VertexAttrib4dNV_remap_index },
   {  4232, VertexAttrib4dvNV_remap_index },
   { 10466, VertexAttrib4fNV_remap_index },
   { 48948, VertexAttrib4fvNV_remap_index },
   { 21093, VertexAttrib4sNV_remap_index },
   { 13189, VertexAttrib4svNV_remap_index },
   {  1876, VertexAttrib4ubNV_remap_index },
   { 12952, VertexAttrib4ubvNV_remap_index },
   { 34323, VertexAttribPointerNV_remap_index },
   { 32469, VertexAttribs1dvNV_remap_index },
   { 47876, VertexAttribs1fvNV_remap_index },
   {  7628, VertexAttribs1svNV_remap_index },
   { 40761, VertexAttribs2dvNV_remap_index },
   {  5266, VertexAttribs2fvNV_remap_index },
   { 32547, VertexAttribs2svNV_remap_index },
   {  2097, VertexAttribs3dvNV_remap_index },
   { 42237, VertexAttribs3fvNV_remap_index },
   { 16915, VertexAttribs3svNV_remap_index },
   {  6695, VertexAttribs4dvNV_remap_index },
   { 28939, VertexAttribs4fvNV_remap_index },
   { 12299, VertexAttribs4svNV_remap_index },
   { 38188, VertexAttribs4ubvNV_remap_index },
   { 49517, GetTexBumpParameterfvATI_remap_index },
   { 12605, GetTexBumpParameterivATI_remap_index },
   { 42369, TexBumpParameterfvATI_remap_index },
   { 10438, TexBumpParameterivATI_remap_index },
   { 11290, AlphaFragmentOp1ATI_remap_index },
   {  4256, AlphaFragmentOp2ATI_remap_index },
   { 22663, AlphaFragmentOp3ATI_remap_index },
   { 39505, BeginFragmentShaderATI_remap_index },
   {  4561, BindFragmentShaderATI_remap_index },
   {  8870, ColorFragmentOp1ATI_remap_index },
   { 15321, ColorFragmentOp2ATI_remap_index },
   { 28419, ColorFragmentOp3ATI_remap_index },
   { 20308, DeleteFragmentShaderATI_remap_index },
   { 51991, EndFragmentShaderATI_remap_index },
   { 27279, GenFragmentShadersATI_remap_index },
   { 49030, PassTexCoordATI_remap_index },
   { 42349, SampleMapATI_remap_index },
   { 41435, SetFragmentShaderConstantATI_remap_index },
   { 27905, DepthRangeArrayfvOES_remap_index },
   {  1435, DepthRangeIndexedfOES_remap_index },
   { 10369, ActiveStencilFaceEXT_remap_index },
   { 41840, GetProgramNamedParameterdvNV_remap_index },
   { 27029, GetProgramNamedParameterfvNV_remap_index },
   { 49548, ProgramNamedParameter4dNV_remap_index },
   { 45070, ProgramNamedParameter4dvNV_remap_index },
   { 29119, ProgramNamedParameter4fNV_remap_index },
   { 30284, ProgramNamedParameter4fvNV_remap_index },
   { 28846, PrimitiveRestartNV_remap_index },
   { 29384, GetTexGenxvOES_remap_index },
   { 17576, TexGenxOES_remap_index },
   { 38393, TexGenxvOES_remap_index },
   {  9833, DepthBoundsEXT_remap_index },
   { 45238, BindFramebufferEXT_remap_index },
   { 49088, BindRenderbufferEXT_remap_index },
   {  5317, StringMarkerGREMEDY_remap_index },
   { 36901, BufferParameteriAPPLE_remap_index },
   { 46833, FlushMappedBufferRangeAPPLE_remap_index },
   { 32856, VertexAttribI1iEXT_remap_index },
   { 13643, VertexAttribI1uiEXT_remap_index },
   { 24201, VertexAttribI2iEXT_remap_index },
   { 48553, VertexAttribI2ivEXT_remap_index },
   { 30415, VertexAttribI2uiEXT_remap_index },
   { 41937, VertexAttribI2uivEXT_remap_index },
   { 23236, VertexAttribI3iEXT_remap_index },
   { 50925, VertexAttribI3ivEXT_remap_index },
   { 26953, VertexAttribI3uiEXT_remap_index },
   { 45161, VertexAttribI3uivEXT_remap_index },
   { 45115, VertexAttribI4iEXT_remap_index },
   {  8325, VertexAttribI4ivEXT_remap_index },
   {  3104, VertexAttribI4uiEXT_remap_index },
   { 32734, VertexAttribI4uivEXT_remap_index },
   {  3711, ClearColorIiEXT_remap_index },
   {  1237, ClearColorIuiEXT_remap_index },
   { 28869, BindBufferOffsetEXT_remap_index },
   { 26394, BeginPerfMonitorAMD_remap_index },
   { 38944, DeletePerfMonitorsAMD_remap_index },
   {  6603, EndPerfMonitorAMD_remap_index },
   { 31972, GenPerfMonitorsAMD_remap_index },
   { 15092, GetPerfMonitorCounterDataAMD_remap_index },
   { 41267, GetPerfMonitorCounterInfoAMD_remap_index },
   { 51951, GetPerfMonitorCounterStringAMD_remap_index },
   { 51510, GetPerfMonitorCountersAMD_remap_index },
   { 17246, GetPerfMonitorGroupStringAMD_remap_index },
   { 35266, GetPerfMonitorGroupsAMD_remap_index },
   { 16673, SelectPerfMonitorCountersAMD_remap_index },
   { 17366, GetObjectParameterivAPPLE_remap_index },
   { 51286, ObjectPurgeableAPPLE_remap_index },
   {  2240, ObjectUnpurgeableAPPLE_remap_index },
   { 50140, ActiveProgramEXT_remap_index },
   { 40506, CreateShaderProgramEXT_remap_index },
   { 43990, UseShaderProgramEXT_remap_index },
   { 36125, TextureBarrierNV_remap_index },
   {  2554, VDPAUFiniNV_remap_index },
   {   890, VDPAUGetSurfaceivNV_remap_index },
   { 28292, VDPAUInitNV_remap_index },
   { 25647, VDPAUIsSurfaceNV_remap_index },
   { 25841, VDPAUMapSurfacesNV_remap_index },
   {  3752, VDPAURegisterOutputSurfaceNV_remap_index },
   { 14990, VDPAURegisterVideoSurfaceNV_remap_index },
   { 13356, VDPAUSurfaceAccessNV_remap_index },
   {  5836, VDPAUUnmapSurfacesNV_remap_index },
   { 44969, VDPAUUnregisterSurfaceNV_remap_index },
   { 10998, BeginPerfQueryINTEL_remap_index },
   { 39881, CreatePerfQueryINTEL_remap_index },
   { 19771, DeletePerfQueryINTEL_remap_index },
   { 48346, EndPerfQueryINTEL_remap_index },
   { 50110, GetFirstPerfQueryIdINTEL_remap_index },
   { 36078, GetNextPerfQueryIdINTEL_remap_index },
   { 38015, GetPerfCounterInfoINTEL_remap_index },
   {   801, GetPerfQueryDataINTEL_remap_index },
   { 26551, GetPerfQueryIdByNameINTEL_remap_index },
   { 23962, GetPerfQueryInfoINTEL_remap_index },
   { 45495, PolygonOffsetClampEXT_remap_index },
   { 51016, WindowRectanglesEXT_remap_index },
   { 24440, StencilFuncSeparateATI_remap_index },
   {  6918, ProgramEnvParameters4fvEXT_remap_index },
   {  7981, ProgramLocalParameters4fvEXT_remap_index },
   {  4750, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  4466, EGLImageTargetTexture2DOES_remap_index },
   { 48520, AlphaFuncx_remap_index },
   { 23281, ClearColorx_remap_index },
   { 47643, ClearDepthx_remap_index },
   { 42835, Color4x_remap_index },
   { 28230, DepthRangex_remap_index },
   {  2782, Fogx_remap_index },
   { 17920, Fogxv_remap_index },
   { 10690, Frustumf_remap_index },
   { 10821, Frustumx_remap_index },
   { 23201, LightModelx_remap_index },
   { 38233, LightModelxv_remap_index },
   { 34399, Lightx_remap_index },
   { 49400, Lightxv_remap_index },
   {  4364, LineWidthx_remap_index },
   {  4102, LoadMatrixx_remap_index },
   { 50460, Materialx_remap_index },
   { 30234, Materialxv_remap_index },
   { 51071, MultMatrixx_remap_index },
   { 12216, MultiTexCoord4x_remap_index },
   { 30035, Normal3x_remap_index },
   { 18647, Orthof_remap_index },
   { 18887, Orthox_remap_index },
   { 32781, PointSizex_remap_index },
   {  1296, PolygonOffsetx_remap_index },
   { 44315, Rotatex_remap_index },
   { 23919, SampleCoveragex_remap_index },
   { 15205, Scalex_remap_index },
   { 45609, TexEnvx_remap_index },
   { 51237, TexEnvxv_remap_index },
   {  2362, TexParameterx_remap_index },
   { 37673, Translatex_remap_index },
   { 39284, ClipPlanef_remap_index },
   { 39186, ClipPlanex_remap_index },
   {   762, GetClipPlanef_remap_index },
   {   625, GetClipPlanex_remap_index },
   { 23837, GetFixedv_remap_index },
   {  1337, GetLightxv_remap_index },
   { 27066, GetMaterialxv_remap_index },
   { 25493, GetTexEnvxv_remap_index },
   { 20193, GetTexParameterxv_remap_index },
   { 34676, PointParameterx_remap_index },
   { 44270, PointParameterxv_remap_index },
   { 22933, TexParameterxv_remap_index },
   {  6953, BlendBarrier_remap_index },
   { 41156, PrimitiveBoundingBox_remap_index },
   {    -1, -1 }
};

