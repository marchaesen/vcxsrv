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
   /* _mesa_function_pool[664]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[713]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[733]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[811]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[850]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[881]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[906]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[939]: DeleteVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glDeleteVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[969]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[998]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[1016]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[1049]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[1076]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[1098]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[1113]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1134]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1151]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1207]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[1236]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1267]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1316]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1341]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1375]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[1416]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1450]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1514]: DepthRangeIndexedfOES (will be remapped) */
   "iff\0"
   "glDepthRangeIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[1543]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[1577]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[1613]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1658]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1687]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1735]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1760]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1791]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1806]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1837]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1849]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTexture\0"
   "glFramebufferTextureEXT\0"
   "glFramebufferTextureOES\0"
   "\0"
   /* _mesa_function_pool[1924]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[1955]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1982]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[2040]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "glBindFragDataLocationIndexedEXT\0"
   "\0"
   /* _mesa_function_pool[2109]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[2128]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[2156]: DeleteSemaphoresEXT (will be remapped) */
   "ip\0"
   "glDeleteSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[2182]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[2202]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[2228]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[2263]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "glGenQueriesEXT\0"
   "\0"
   /* _mesa_function_pool[2312]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[2361]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[2391]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[2434]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[2451]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[2483]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2523]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2539]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2560]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2581]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2619]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2649]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[2675]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2691]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2718]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2784]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2831]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[2856]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2903]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2924]: Uniform3ui64ARB (will be remapped) */
   "iiii\0"
   "glUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[2948]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2959]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[3003]: ProgramUniform3ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[3035]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[3050]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[3061]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[3090]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[3105]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "glGetSamplerParameterIivEXT\0"
   "glGetSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[3191]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[3225]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[3273]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "glDrawElementsInstancedBaseVertexBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[3377]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[3434]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[3490]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[3510]: ConservativeRasterParameterfNV (will be remapped) */
   "if\0"
   "glConservativeRasterParameterfNV\0"
   "\0"
   /* _mesa_function_pool[3547]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[3581]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[3613]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[3631]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[3671]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[3703]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[3747]: Uniform1ui64ARB (will be remapped) */
   "ii\0"
   "glUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[3769]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[3795]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[3840]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3851]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3869]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3893]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3910]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3947]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3964]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[3995]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[4023]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[4062]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "glGetnUniformiv\0"
   "glGetnUniformivKHR\0"
   "\0"
   /* _mesa_function_pool[4122]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[4148]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[4244]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[4260]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[4294]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "glBufferStorageEXT\0"
   "\0"
   /* _mesa_function_pool[4335]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[4351]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[4374]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[4390]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[4414]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[4447]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[4465]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[4522]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[4554]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[4586]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[4599]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[4624]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[4657]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[4672]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[4694]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[4719]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[4746]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "glBlendFuncSeparateiEXT\0"
   "glBlendFuncSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[4876]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[4908]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[4951]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[4979]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[5001]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[5057]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[5092]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[5117]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[5169]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[5195]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[5210]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[5242]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[5264]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[5288]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[5323]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[5351]: UnmapNamedBuffer (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[5373]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[5400]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[5424]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[5450]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[5475]: StringMarkerGREMEDY (will be remapped) */
   "ip\0"
   "glStringMarkerGREMEDY\0"
   "\0"
   /* _mesa_function_pool[5501]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[5535]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[5554]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[5588]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[5614]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[5649]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[5681]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[5707]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[5755]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[5803]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[5893]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[5953]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[5968]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[5994]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[6021]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[6048]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[6101]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[6147]: TexStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTexStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[6177]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[6203]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[6230]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[6255]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[6301]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[6324]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[6351]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[6385]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[6431]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[6457]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[6490]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[6515]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[6573]: TexStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[6604]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[6619]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[6745]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6788]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[6822]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[6845]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[6870]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[6890]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[6914]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[6940]: Uniform2i64vARB (will be remapped) */
   "iip\0"
   "glUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[6963]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[6987]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[7005]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[7046]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[7059]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[7137]: MakeImageHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeImageHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[7172]: ImportMemoryFdEXT (will be remapped) */
   "iiii\0"
   "glImportMemoryFdEXT\0"
   "\0"
   /* _mesa_function_pool[7198]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[7233]: TexStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTexStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[7262]: BlendBarrier (will be remapped) */
   "\0"
   "glBlendBarrier\0"
   "glBlendBarrierKHR\0"
   "\0"
   /* _mesa_function_pool[7297]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[7321]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[7347]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[7373]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "glGetSamplerParameterIuivEXT\0"
   "glGetSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[7462]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "glDisableiEXT\0"
   "glDisableiOES\0"
   "\0"
   /* _mesa_function_pool[7525]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[7622]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[7644]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "glObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[7681]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[7695]: ProgramUniform1ui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[7725]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "glBeginQueryEXT\0"
   "\0"
   /* _mesa_function_pool[7774]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[7792]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[7841]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[7859]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[7914]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[7932]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[7953]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[7979]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[8013]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[8045]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[8074]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[8101]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8147]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[8231]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[8260]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[8286]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[8306]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[8343]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "\0"
   /* _mesa_function_pool[8366]: Uniform1ui64vARB (will be remapped) */
   "iip\0"
   "glUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[8390]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[8447]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[8483]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[8500]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "glGetObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[8544]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[8561]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[8588]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[8623]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8650]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[8695]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[8719]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[8767]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "glGetDebugMessageLogKHR\0"
   "\0"
   /* _mesa_function_pool[8846]: SubpixelPrecisionBiasNV (will be remapped) */
   "ii\0"
   "glSubpixelPrecisionBiasNV\0"
   "\0"
   /* _mesa_function_pool[8876]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[8900]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[8941]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[8973]: Uniform4ui64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[8998]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[9016]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[9045]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[9072]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[9117]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[9154]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[9179]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[9210]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[9276]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[9296]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[9381]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "glGetQueryObjectuivEXT\0"
   "\0"
   /* _mesa_function_pool[9452]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[9497]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[9528]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[9559]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "\0"
   /* _mesa_function_pool[9584]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[9605]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[9619]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[9679]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[9725]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[9791]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[9833]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "glGetQueryObjectivEXT\0"
   "\0"
   /* _mesa_function_pool[9901]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[9943]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[9989]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[10031]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[10063]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[10097]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[10162]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[10187]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[10208]: BufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[10236]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[10291]: BindVertexArrayAPPLE (dynamic) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[10317]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[10377]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[10421]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[10454]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[10477]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[10522]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[10568]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[10621]: TextureStorageMem3DEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem3DEXT\0"
   "\0"
   /* _mesa_function_pool[10656]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[10703]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "glGetGraphicsResetStatus\0"
   "glGetGraphicsResetStatusKHR\0"
   "\0"
   /* _mesa_function_pool[10786]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[10812]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[10838]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[10864]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[10881]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[10909]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[10935]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[10969]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[10995]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "glSamplerParameterIivEXT\0"
   "glSamplerParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[11072]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[11105]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[11133]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[11166]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[11200]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[11251]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[11264]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[11297]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[11311]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[11346]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[11392]: Uniform2ui64ARB (will be remapped) */
   "iii\0"
   "glUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[11415]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[11441]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[11466]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[11487]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11512]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[11536]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[11560]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[11603]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[11630]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[11678]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[11699]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[11720]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[11733]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[11763]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[11789]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[11805]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[11831]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[11876]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[11925]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[11970]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[11987]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[11999]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[12025]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[12070]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[12092]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[12134]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[12159]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[12174]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[12215]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[12232]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12262]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[12273]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[12291]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[12338]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[12356]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[12381]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[12406]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[12432]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[12471]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[12500]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[12545]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[12590]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[12616]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[12634]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[12659]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[12705]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12742]: UniformHandleui64ARB (will be remapped) */
   "ii\0"
   "glUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[12769]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[12795]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "glDrawElementsInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[12878]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[12925]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[12952]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[12999]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[13049]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[13075]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[13106]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[13127]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[13155]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[13200]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[13227]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[13260]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[13298]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[13315]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[13366]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[13391]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[13422]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[13447]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[13477]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[13493]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[13519]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[13547]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[13593]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[13613]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[13641]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[13659]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[13683]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "glPatchParameteriEXT\0"
   "glPatchParameteriOES\0"
   "\0"
   /* _mesa_function_pool[13747]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13796]: ConservativeRasterParameteriNV (will be remapped) */
   "ii\0"
   "glConservativeRasterParameteriNV\0"
   "\0"
   /* _mesa_function_pool[13833]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[13863]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[13890]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[13912]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[13965]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[14018]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "\0"
   /* _mesa_function_pool[14044]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[14097]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[14150]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[14195]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[14211]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[14233]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[14319]: GenSemaphoresEXT (will be remapped) */
   "ip\0"
   "glGenSemaphoresEXT\0"
   "\0"
   /* _mesa_function_pool[14342]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[14389]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[14435]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[14465]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[14491]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[14534]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[14561]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[14582]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "glGetQueryivEXT\0"
   "\0"
   /* _mesa_function_pool[14632]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[14675]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[14709]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[14737]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[14783]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[14809]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[14854]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[14895]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[14920]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[14956]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[14990]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[15031]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[15076]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[15097]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "glGetObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[15146]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[15194]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[15232]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[15246]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[15272]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[15293]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[15337]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[15381]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "glSamplerParameterIuivEXT\0"
   "glSamplerParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[15461]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[15505]: GetInternalformati64v (will be remapped) */
   "iiiip\0"
   "glGetInternalformati64v\0"
   "\0"
   /* _mesa_function_pool[15536]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[15572]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15625]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[15638]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[15676]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[15728]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[15751]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[15777]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[15791]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[15805]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[15829]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[15867]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[15901]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[15925]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[15947]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[15976]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "glQueryCounterEXT\0"
   "\0"
   /* _mesa_function_pool[16013]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[16038]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[16064]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[16113]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[16146]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[16178]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[16213]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "glDrawElementsBaseVertexEXT\0"
   "glDrawElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[16301]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[16319]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "glColorMaskiEXT\0"
   "glColorMaskiOES\0"
   "\0"
   /* _mesa_function_pool[16393]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[16429]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[16448]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[16467]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[16486]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[16528]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[16547]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "\0"
   /* _mesa_function_pool[16570]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[16595]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[16640]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[16683]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[16713]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[16746]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16788]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[16823]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[16838]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[16891]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[16905]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16934]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "glGetnUniformuiv\0"
   "glGetnUniformuivKHR\0"
   "\0"
   /* _mesa_function_pool[16997]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[17013]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[17071]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[17092]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[17111]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[17146]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[17171]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[17190]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[17205]: ProgramUniform4i64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[17237]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[17275]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[17301]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[17331]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[17374]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[17419]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[17453]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[17479]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[17505]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[17534]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[17577]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[17612]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[17640]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[17658]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[17694]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[17732]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "glPushDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[17775]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17823]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[17860]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[17873]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[17890]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[17920]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[17943]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[17977]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[18005]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[18032]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[18075]: Uniform3i64vARB (will be remapped) */
   "iip\0"
   "glUniform3i64vARB\0"
   "\0"
   /* _mesa_function_pool[18098]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[18153]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[18171]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[18205]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[18226]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[18264]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[18281]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[18302]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[18351]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[18401]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[18447]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[18466]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[18497]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[18520]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[18547]: Uniform3i64ARB (will be remapped) */
   "iiii\0"
   "glUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[18570]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[18595]: GetImageHandleARB (will be remapped) */
   "iiiii\0"
   "glGetImageHandleARB\0"
   "\0"
   /* _mesa_function_pool[18622]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[18640]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[18653]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[18729]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[18763]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[18817]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "glEnableiEXT\0"
   "glEnableiOES\0"
   "\0"
   /* _mesa_function_pool[18876]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[18930]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[18967]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[18985]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[19039]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[19084]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[19101]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[19155]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[19174]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "glDebugMessageInsertKHR\0"
   "\0"
   /* _mesa_function_pool[19251]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[19280]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[19339]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[19360]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[19395]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[19419]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[19431]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[19446]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[19491]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[19520]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[19536]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[19554]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[19570]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[19611]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[19633]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[19651]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[19689]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[19707]: MakeImageHandleResidentARB (will be remapped) */
   "ii\0"
   "glMakeImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[19740]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[19776]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[19793]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[19820]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[19838]: DispatchComputeGroupSizeARB (will be remapped) */
   "iiiiii\0"
   "glDispatchComputeGroupSizeARB\0"
   "\0"
   /* _mesa_function_pool[19876]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[19894]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[19929]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[19962]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[19989]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[20024]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[20069]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[20119]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[20145]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "glDebugMessageCallbackKHR\0"
   "\0"
   /* _mesa_function_pool[20224]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[20267]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[20322]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "glTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[20390]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[20416]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[20459]: GenVertexArraysAPPLE (dynamic) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[20486]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[20529]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[20585]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[20623]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[20642]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[20676]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[20738]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[20759]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[20784]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[20812]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[20839]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[20887]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[20914]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[20936]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[20954]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[20983]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[21018]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "glDrawElementsInstancedBaseVertexEXT\0"
   "glDrawElementsInstancedBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[21134]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[21158]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[21186]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[21214]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[21229]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[21256]: ProgramUniform4i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[21287]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[21316]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[21330]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[21369]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[21421]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[21473]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[21489]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[21547]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[21603]: IsSemaphoreEXT (will be remapped) */
   "i\0"
   "glIsSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[21623]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[21653]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[21704]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[21724]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[21759]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[21785]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[21801]: Uniform3ui64vARB (will be remapped) */
   "iip\0"
   "glUniform3ui64vARB\0"
   "\0"
   /* _mesa_function_pool[21825]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[21848]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[21897]: BufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[21929]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[21957]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[21973]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[22000]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[22027]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[22047]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[22066]: GetnUniformui64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[22094]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[22111]: TextureStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTextureStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[22157]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[22183]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[22211]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[22245]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[22281]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "glPopDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[22318]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[22359]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[22417]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[22444]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "glTexBufferRangeEXT\0"
   "glTexBufferRangeOES\0"
   "\0"
   /* _mesa_function_pool[22508]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[22549]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[22584]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[22611]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[22646]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[22678]: Uniform2ui64vARB (will be remapped) */
   "iip\0"
   "glUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[22702]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[22794]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[22839]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[22870]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[22885]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[22906]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[22930]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[22957]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[22981]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[23000]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "glViewportArrayvOES\0"
   "\0"
   /* _mesa_function_pool[23042]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[23113]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[23166]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[23211]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[23247]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[23271]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[23300]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[23318]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[23330]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "glEndQueryEXT\0"
   "\0"
   /* _mesa_function_pool[23372]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[23389]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[23425]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[23452]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[23463]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[23506]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[23522]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[23558]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[23612]: GetUniformi64vARB (will be remapped) */
   "iip\0"
   "glGetUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[23637]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[23659]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[23701]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[23738]: MultiDrawArraysIndirectCountARB (will be remapped) */
   "iiiii\0"
   "glMultiDrawArraysIndirectCountARB\0"
   "glMultiDrawArraysIndirectCount\0"
   "\0"
   /* _mesa_function_pool[23810]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[23872]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[23907]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[23933]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[23953]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[23971]: GetUnsignedBytevEXT (will be remapped) */
   "ip\0"
   "glGetUnsignedBytevEXT\0"
   "\0"
   /* _mesa_function_pool[23997]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[24032]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[24077]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[24114]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[24165]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "\0"
   /* _mesa_function_pool[24188]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[24218]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[24266]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "glViewportIndexedfvOES\0"
   "\0"
   /* _mesa_function_pool[24313]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[24335]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[24349]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[24367]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[24386]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[24425]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[24459]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[24481]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[24509]: GetTextureHandleARB (will be remapped) */
   "i\0"
   "glGetTextureHandleARB\0"
   "\0"
   /* _mesa_function_pool[24534]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[24589]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[24624]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[24658]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[24689]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[24720]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[24740]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[24783]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[24816]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[24889]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[24915]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[24960]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[25005]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[25022]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[25066]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[25083]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[25100]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "\0"
   /* _mesa_function_pool[25123]: GetnUniformi64vARB (will be remapped) */
   "iiip\0"
   "glGetnUniformi64vARB\0"
   "\0"
   /* _mesa_function_pool[25150]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[25186]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[25203]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[25234]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[25261]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[25292]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[25344]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[25363]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[25378]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[25409]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[25431]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[25448]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[25481]: ProgramUniformHandleui64ARB (will be remapped) */
   "iii\0"
   "glProgramUniformHandleui64ARB\0"
   "\0"
   /* _mesa_function_pool[25516]: ProgramUniform2i64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[25546]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[25558]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[25579]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[25624]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[25673]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[25698]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[25742]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[25790]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[25829]: TextureStorageMem1DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorageMem1DEXT\0"
   "\0"
   /* _mesa_function_pool[25862]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[25884]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[25932]: NamedBufferStorageMemEXT (will be remapped) */
   "iiii\0"
   "glNamedBufferStorageMemEXT\0"
   "\0"
   /* _mesa_function_pool[25965]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[26013]: ProgramUniform4ui64ARB (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui64ARB\0"
   "\0"
   /* _mesa_function_pool[26046]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26066]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[26095]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[26118]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[26138]: SignalSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glSignalSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[26167]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[26188]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[26218]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[26232]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[26247]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "glDeleteQueriesEXT\0"
   "\0"
   /* _mesa_function_pool[26305]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[26334]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[26345]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[26364]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[26414]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[26432]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[26463]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[26499]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[26517]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[26530]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[26549]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[26568]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[26599]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[26617]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[26639]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26705]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[26730]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[26751]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[26771]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[26791]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[26811]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[26836]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[26862]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[26880]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[26900]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[26917]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[26937]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[26970]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[27013]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[27046]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[27080]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[27110]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[27145]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[27230]: VertexAttribL1ui64ARB (will be remapped) */
   "ii\0"
   "glVertexAttribL1ui64ARB\0"
   "\0"
   /* _mesa_function_pool[27258]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[27286]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[27301]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[27319]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[27350]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[27392]: SpecializeShaderARB (will be remapped) */
   "ipipp\0"
   "glSpecializeShaderARB\0"
   "glSpecializeShader\0"
   "\0"
   /* _mesa_function_pool[27440]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[27465]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[27520]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[27540]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[27597]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[27629]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[27683]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[27702]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[27717]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[27742]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[27772]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[27787]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[27814]: UniformHandleui64vARB (will be remapped) */
   "iip\0"
   "glUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[27843]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[27860]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[27887]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[27904]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[27931]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[27978]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[28028]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[28075]: ProgramUniform1i64ARB (will be remapped) */
   "iii\0"
   "glProgramUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[28104]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[28141]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[28181]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[28205]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[28253]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[28303]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[28340]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[28354]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[28381]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[28414]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "glBlendEquationSeparateiEXT\0"
   "glBlendEquationSeparateiOES\0"
   "\0"
   /* _mesa_function_pool[28562]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[28579]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[28624]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[28646]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[28682]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[28764]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[28793]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[28836]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[28851]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[28901]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[28934]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[28959]: Uniform1i64ARB (will be remapped) */
   "ii\0"
   "glUniform1i64ARB\0"
   "\0"
   /* _mesa_function_pool[28980]: DepthRangeArrayfvOES (will be remapped) */
   "iip\0"
   "glDepthRangeArrayfvOES\0"
   "\0"
   /* _mesa_function_pool[29008]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[29063]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[29091]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[29146]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[29176]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[29193]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[29209]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[29238]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[29252]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[29290]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[29305]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[29340]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[29367]: ProgramUniformHandleui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniformHandleui64vARB\0"
   "\0"
   /* _mesa_function_pool[29404]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[29422]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[29452]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[29470]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[29505]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[29531]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[29568]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[29610]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[29636]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[29681]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[29697]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[29753]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[29770]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[29802]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[29819]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[29836]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[29853]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[29883]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[29900]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[29927]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[29944]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[29958]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[29981]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[30009]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[30051]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[30077]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[30109]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[30124]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[30138]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[30171]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[30196]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[30214]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[30231]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[30268]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[30313]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[30370]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[30388]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[30418]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[30433]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[30445]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[30496]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[30518]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[30536]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[30554]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "glBlendFunciEXT\0"
   "glBlendFunciOES\0"
   "\0"
   /* _mesa_function_pool[30642]: ClearNamedFramebufferfi (will be remapped) */
   "iiifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[30675]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[30707]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[30734]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[30768]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "glScissorIndexedOES\0"
   "\0"
   /* _mesa_function_pool[30812]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[30820]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[30854]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[30881]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[30910]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[30961]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[30973]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[31019]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[31041]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[31074]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[31118]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[31147]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[31177]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[31221]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[31238]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "glReadnPixels\0"
   "glReadnPixelsKHR\0"
   "\0"
   /* _mesa_function_pool[31296]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[31330]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[31346]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[31380]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[31396]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[31431]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[31447]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[31463]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[31479]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[31491]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[31527]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[31573]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[31633]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[31651]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[31663]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[31675]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[31696]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[31708]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[31734]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[31790]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[31805]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[31820]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[31834]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[31869]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[31893]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[31908]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[31946]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[31973]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[32018]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[32035]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[32056]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[32073]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[32106]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[32131]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[32178]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[32215]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[32231]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervKHR\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[32283]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[32328]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[32343]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[32359]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[32404]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[32423]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[32443]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[32461]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[32480]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[32502]: WaitSemaphoreEXT (will be remapped) */
   "iipipp\0"
   "glWaitSemaphoreEXT\0"
   "\0"
   /* _mesa_function_pool[32529]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[32572]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[32638]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[32673]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[32702]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[32762]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[32780]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[32824]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[32842]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[32860]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[32889]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[32907]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[32931]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[32972]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[33000]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[33018]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[33049]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[33077]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[33111]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[33136]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[33155]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[33171]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[33188]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[33211]: Uniform2i64ARB (will be remapped) */
   "iii\0"
   "glUniform2i64ARB\0"
   "\0"
   /* _mesa_function_pool[33233]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[33316]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[33347]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[33401]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[33425]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[33455]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[33502]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[33517]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[33539]: ProgramUniform2i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2i64vARB\0"
   "\0"
   /* _mesa_function_pool[33570]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[33595]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[33621]: ImportSemaphoreFdEXT (will be remapped) */
   "iii\0"
   "glImportSemaphoreFdEXT\0"
   "\0"
   /* _mesa_function_pool[33649]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "glIsQueryEXT\0"
   "\0"
   /* _mesa_function_pool[33688]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[33714]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[33740]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[33794]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "glBlendEquationiEXT\0"
   "glBlendEquationiOES\0"
   "\0"
   /* _mesa_function_pool[33901]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[33948]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[33980]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[33998]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[34044]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[34087]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[34132]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[34166]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[34191]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[34225]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[34252]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[34301]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[34350]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[34383]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[34410]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[34440]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[34484]: ProgramUniform4ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[34516]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[34540]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[34584]: ProgramUniform1i64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[34615]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[34639]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[34672]: GetSemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glGetSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[34709]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "glTexStorage3DMultisampleOES\0"
   "\0"
   /* _mesa_function_pool[34773]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[34793]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[34819]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[34845]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "glScissorIndexedvOES\0"
   "\0"
   /* _mesa_function_pool[34888]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[34918]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[34950]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[34967]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[34985]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[35028]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[35050]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[35068]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[35091]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[35155]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[35208]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[35234]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[35256]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[35274]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[35297]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[35347]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[35382]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[35397]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[35429]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[35448]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[35468]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[35483]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[35497]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[35513]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[35534]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[35548]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[35579]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "glGetFragDataIndexEXT\0"
   "\0"
   /* _mesa_function_pool[35624]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[35650]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[35699]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[35716]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[35733]: Uniform4ui64vARB (will be remapped) */
   "iip\0"
   "glUniform4ui64vARB\0"
   "\0"
   /* _mesa_function_pool[35757]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[35802]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[35829]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[35851]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[35876]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[35901]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[35944]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[35978]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[35996]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[36038]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[36124]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[36151]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[36177]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[36208]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[36255]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[36273]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[36305]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[36331]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[36366]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[36405]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[36452]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[36491]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[36522]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[36542]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[36567]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[36593]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[36647]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[36665]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[36711]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[36749]: SemaphoreParameterui64vEXT (will be remapped) */
   "iip\0"
   "glSemaphoreParameterui64vEXT\0"
   "\0"
   /* _mesa_function_pool[36783]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[36810]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[36838]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[36854]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[36870]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[36886]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[36940]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[36971]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[37027]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "glGetTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[37101]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[37117]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[37173]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[37265]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[37337]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[37367]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[37384]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[37422]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[37452]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[37511]: VertexAttribL1ui64vARB (will be remapped) */
   "ip\0"
   "glVertexAttribL1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[37540]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[37577]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[37604]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[37622]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[37659]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[37697]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[37712]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[37744]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "glTexParameterIivOES\0"
   "\0"
   /* _mesa_function_pool[37809]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[37825]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[37846]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[37880]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[37921]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[37948]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[37975]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[38018]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[38034]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[38055]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[38073]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[38107]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[38152]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[38189]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[38218]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[38261]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[38284]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[38330]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[38392]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[38413]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[38503]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[38535]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[38555]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[38575]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[38600]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[38634]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[38680]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "glViewportIndexedfOES\0"
   "\0"
   /* _mesa_function_pool[38728]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[38774]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[38795]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "glDebugMessageControlKHR\0"
   "\0"
   /* _mesa_function_pool[38875]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[38921]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[38961]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[38995]: MultiDrawElementsIndirectCountARB (will be remapped) */
   "iiiiii\0"
   "glMultiDrawElementsIndirectCountARB\0"
   "glMultiDrawElementsIndirectCount\0"
   "\0"
   /* _mesa_function_pool[39072]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[39097]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[39110]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[39153]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[39170]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39197]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[39215]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[39233]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[39285]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[39336]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[39375]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[39391]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[39434]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[39480]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[39509]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[39536]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[39554]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[39591]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[39642]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[39675]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[39694]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[39714]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[39733]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[39768]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[39815]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39835]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[39862]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[39917]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[39932]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[39958]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[39986]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[40017]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "glDrawArraysInstancedBaseInstanceEXT\0"
   "\0"
   /* _mesa_function_pool[40095]: TextureStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTextureStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[40142]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[40185]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[40199]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[40241]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[40276]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[40304]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[40318]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[40347]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[40407]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[40473]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[40493]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[40518]: GetMemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glGetMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[40555]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[40588]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40635]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[40653]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[40686]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[40734]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[40791]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[40810]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[40830]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[40848]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[40874]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[40901]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[40968]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[41022]: TextureStorageMem2DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorageMem2DEXT\0"
   "\0"
   /* _mesa_function_pool[41056]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "glIsEnablediEXT\0"
   "glIsEnablediOES\0"
   "\0"
   /* _mesa_function_pool[41127]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[41157]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[41173]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[41261]: Uniform1i64vARB (will be remapped) */
   "iip\0"
   "glUniform1i64vARB\0"
   "\0"
   /* _mesa_function_pool[41284]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[41311]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[41325]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "glTexBufferEXT\0"
   "glTexBufferOES\0"
   "\0"
   /* _mesa_function_pool[41387]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[41404]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[41420]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[41436]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[41452]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "glGetnUniformfv\0"
   "glGetnUniformfvKHR\0"
   "\0"
   /* _mesa_function_pool[41512]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[41528]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[41555]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[41588]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[41609]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[41660]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[41685]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[41741]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[41766]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[41782]: GetUniformui64vARB (will be remapped) */
   "iip\0"
   "glGetUniformui64vARB\0"
   "\0"
   /* _mesa_function_pool[41808]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[41827]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[41870]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[41894]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[41909]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[41938]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "\0"
   /* _mesa_function_pool[41961]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[41982]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42006]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[42054]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[42080]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[42110]: IsMemoryObjectEXT (will be remapped) */
   "i\0"
   "glIsMemoryObjectEXT\0"
   "\0"
   /* _mesa_function_pool[42133]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[42152]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[42187]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[42213]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[42237]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[42272]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[42312]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[42378]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[42395]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[42421]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[42466]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[42493]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[42510]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[42555]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[42582]: PrimitiveBoundingBox (will be remapped) */
   "ffffffff\0"
   "glPrimitiveBoundingBox\0"
   "glPrimitiveBoundingBoxARB\0"
   "glPrimitiveBoundingBoxEXT\0"
   "glPrimitiveBoundingBoxOES\0"
   "\0"
   /* _mesa_function_pool[42693]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[42730]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[42757]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[42773]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[42814]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[42861]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[42896]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[42913]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[42962]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[42997]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[43016]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[43066]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[43096]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[43123]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[43149]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[43187]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[43219]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[43238]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[43266]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[43303]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[43348]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[43363]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[43410]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[43428]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "glGetFloati_vOES\0"
   "\0"
   /* _mesa_function_pool[43464]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[43509]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[43529]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[43589]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[43614]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[43642]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[43668]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[43682]: ProgramUniform2ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform2ui64vARB\0"
   "\0"
   /* _mesa_function_pool[43714]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[43734]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[43754]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[43774]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[43802]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[43850]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[43888]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[43906]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[43931]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[43946]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "glCopyImageSubDataEXT\0"
   "glCopyImageSubDataOES\0"
   "\0"
   /* _mesa_function_pool[44026]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[44075]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[44093]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[44111]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[44132]: Uniform4i64vARB (will be remapped) */
   "iip\0"
   "glUniform4i64vARB\0"
   "\0"
   /* _mesa_function_pool[44155]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[44177]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[44207]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[44219]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[44240]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[44269]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[44327]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[44343]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[44368]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[44426]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[44442]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[44458]: ShaderStorageBlockBinding (will be remapped) */
   "iii\0"
   "glShaderStorageBlockBinding\0"
   "\0"
   /* _mesa_function_pool[44491]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[44507]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[44523]: MemoryObjectParameterivEXT (will be remapped) */
   "iip\0"
   "glMemoryObjectParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[44557]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[44579]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[44609]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[44627]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[44655]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "\0"
   /* _mesa_function_pool[44679]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[44712]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[44740]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[44779]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[44794]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[44829]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[44846]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[44867]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[44912]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[44944]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[44979]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[45025]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[45052]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[45088]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[45118]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[45166]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[45186]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[45230]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[45264]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[45321]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[45346]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[45410]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[45429]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[45455]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[45469]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[45487]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[45512]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[45588]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[45631]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[45665]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[45679]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[45695]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[45709]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[45754]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[45783]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[45845]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[45861]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[45923]: DeleteMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glDeleteMemoryObjectsEXT\0"
   "\0"
   /* _mesa_function_pool[45952]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[45969]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[46006]: MakeTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[46040]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[46072]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "glDrawRangeElementsBaseVertexEXT\0"
   "glDrawRangeElementsBaseVertexOES\0"
   "\0"
   /* _mesa_function_pool[46177]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[46210]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[46234]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[46254]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[46300]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[46328]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46367]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[46385]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[46407]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[46422]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[46439]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[46471]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[46501]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[46517]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[46539]: GetUnsignedBytei_vEXT (will be remapped) */
   "iip\0"
   "glGetUnsignedBytei_vEXT\0"
   "\0"
   /* _mesa_function_pool[46568]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[46601]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[46636]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[46646]: MakeTextureHandleNonResidentARB (will be remapped) */
   "i\0"
   "glMakeTextureHandleNonResidentARB\0"
   "\0"
   /* _mesa_function_pool[46683]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[46729]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[46776]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[46806]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[46831]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[46865]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[46899]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[46919]: GetVertexAttribLui64vARB (will be remapped) */
   "iip\0"
   "glGetVertexAttribLui64vARB\0"
   "\0"
   /* _mesa_function_pool[46951]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[46994]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[47071]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "glPolygonOffsetClamp\0"
   "\0"
   /* _mesa_function_pool[47121]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[47141]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[47163]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[47176]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[47191]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[47206]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[47234]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[47270]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[47295]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[47351]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[47382]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[47409]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[47432]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[47452]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[47478]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[47504]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[47543]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[47578]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[47636]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[47652]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[47676]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[47690]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[47714]: IsImageHandleResidentARB (will be remapped) */
   "i\0"
   "glIsImageHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[47744]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[47765]: ProgramUniform2ui64ARB (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui64ARB\0"
   "\0"
   /* _mesa_function_pool[47796]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[47811]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "glGetTexParameterIuivOES\0"
   "\0"
   /* _mesa_function_pool[47888]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[47953]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[47996]: Uniform4i64ARB (will be remapped) */
   "iiiii\0"
   "glUniform4i64ARB\0"
   "\0"
   /* _mesa_function_pool[48020]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[48058]: ProgramUniform3ui64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui64ARB\0"
   "\0"
   /* _mesa_function_pool[48090]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[48112]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[48163]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[48194]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[48210]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[48239]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[48270]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[48293]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[48312]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[48349]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[48370]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[48397]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[48435]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[48460]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[48495]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[48546]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[48565]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[48600]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[48623]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[48643]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[48698]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[48747]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[48790]: TexStorageMem2DMultisampleEXT (will be remapped) */
   "iiiiiiii\0"
   "glTexStorageMem2DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[48832]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[48875]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[48899]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[48914]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[48952]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[48976]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[48991]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[49009]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[49027]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[49042]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[49066]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[49081]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[49096]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[49144]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[49215]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[49279]: IsTextureHandleResidentARB (will be remapped) */
   "i\0"
   "glIsTextureHandleResidentARB\0"
   "\0"
   /* _mesa_function_pool[49311]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[49344]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[49378]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[49408]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[49517]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[49552]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[49577]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[49603]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[49659]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[49708]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[49727]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[49745]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[49780]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[49799]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[49818]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[49842]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[49858]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[49925]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[49943]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[49994]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[50029]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[50047]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[50070]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[50097]: NamedBufferPageCommitmentARB (will be remapped) */
   "iiii\0"
   "glNamedBufferPageCommitmentARB\0"
   "\0"
   /* _mesa_function_pool[50134]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[50171]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[50221]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[50254]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[50299]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[50319]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[50335]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[50351]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[50403]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[50442]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[50475]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[50496]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[50517]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[50550]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[50574]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[50616]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[50641]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[50661]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[50688]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[50712]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[50725]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[50770]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[50793]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[50813]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[50828]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[50854]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[50869]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[50886]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[50915]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[50947]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[50964]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[50989]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[51004]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[51036]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "glMultiDrawElementsBaseVertexEXT\0"
   "\0"
   /* _mesa_function_pool[51107]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[51135]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[51160]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[51224]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[51255]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[51292]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[51313]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[51346]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[51370]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[51397]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[51416]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[51469]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[51507]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[51538]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[51552]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[51573]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "glScissorArrayvOES\0"
   "\0"
   /* _mesa_function_pool[51613]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[51642]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[51659]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[51675]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "\0"
   /* _mesa_function_pool[51705]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[51719]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[51738]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[51757]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[51781]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[51817]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[51847]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[51869]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[51905]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[51934]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[51985]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[52002]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "\0"
   /* _mesa_function_pool[52024]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[52041]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[52064]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[52081]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[52098]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[52149]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[52167]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[52199]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[52230]: GetTextureSamplerHandleARB (will be remapped) */
   "ii\0"
   "glGetTextureSamplerHandleARB\0"
   "\0"
   /* _mesa_function_pool[52263]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[52281]: ProgramUniform3i64ARB (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i64ARB\0"
   "\0"
   /* _mesa_function_pool[52312]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[52345]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[52391]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[52437]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[52461]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[52516]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[52579]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[52606]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[52624]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[52651]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[52665]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[52710]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[52731]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[52756]: WindowRectanglesEXT (will be remapped) */
   "iip\0"
   "glWindowRectanglesEXT\0"
   "\0"
   /* _mesa_function_pool[52783]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[52797]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[52811]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[52845]: TexStorageMem3DMultisampleEXT (will be remapped) */
   "iiiiiiiii\0"
   "glTexStorageMem3DMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[52888]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[52902]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[52929]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[52979]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[53001]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[53020]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[53050]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[53069]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[53097]: ProgramUniform1ui64vARB (will be remapped) */
   "iiip\0"
   "glProgramUniform1ui64vARB\0"
   "\0"
   /* _mesa_function_pool[53129]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[53148]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[53167]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[53185]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[53208]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[53241]: FramebufferFetchBarrierEXT (will be remapped) */
   "\0"
   "glFramebufferFetchBarrierEXT\0"
   "\0"
   /* _mesa_function_pool[53272]: IsVertexArrayAPPLE (dynamic) */
   "i\0"
   "glIsVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[53296]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[53316]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[53348]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[53383]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "glObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[53425]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[53438]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[53466]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[53484]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[53515]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "glMinSampleShadingOES\0"
   "\0"
   /* _mesa_function_pool[53581]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[53679]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[53709]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[53732]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[53787]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[53812]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[53852]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[53877]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   /* _mesa_function_pool[53911]: CreateMemoryObjectsEXT (will be remapped) */
   "ip\0"
   "glCreateMemoryObjectsEXT\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 22359, CompressedTexImage1D_remap_index },
   { 19280, CompressedTexImage2D_remap_index },
   { 14233, CompressedTexImage3D_remap_index },
   { 35091, CompressedTexSubImage1D_remap_index },
   { 42312, CompressedTexSubImage2D_remap_index },
   {  7525, CompressedTexSubImage3D_remap_index },
   {  5001, GetCompressedTexImage_remap_index },
   { 21421, LoadTransposeMatrixd_remap_index },
   { 21369, LoadTransposeMatrixf_remap_index },
   { 39233, MultTransposeMatrixd_remap_index },
   { 15676, MultTransposeMatrixf_remap_index },
   { 37880, SampleCoverage_remap_index },
   {  4148, BlendFuncSeparate_remap_index },
   { 25698, FogCoordPointer_remap_index },
   { 46776, FogCoordd_remap_index },
   { 46439, FogCoorddv_remap_index },
   { 38107, MultiDrawArrays_remap_index },
   { 36038, PointParameterf_remap_index },
   {  5803, PointParameterfv_remap_index },
   { 35996, PointParameteri_remap_index },
   { 10377, PointParameteriv_remap_index },
   {  6255, SecondaryColor3b_remap_index },
   { 46254, SecondaryColor3bv_remap_index },
   { 39434, SecondaryColor3d_remap_index },
   { 14389, SecondaryColor3dv_remap_index },
   {  6385, SecondaryColor3i_remap_index },
   { 33998, SecondaryColor3iv_remap_index },
   {  6101, SecondaryColor3s_remap_index },
   { 18401, SecondaryColor3sv_remap_index },
   { 25884, SecondaryColor3ub_remap_index },
   {  8719, SecondaryColor3ubv_remap_index },
   { 25965, SecondaryColor3ui_remap_index },
   { 28205, SecondaryColor3uiv_remap_index },
   { 25742, SecondaryColor3us_remap_index },
   { 11630, SecondaryColor3usv_remap_index },
   { 40734, SecondaryColorPointer_remap_index },
   { 13965, WindowPos2d_remap_index },
   { 20267, WindowPos2dv_remap_index },
   { 13912, WindowPos2f_remap_index },
   { 27465, WindowPos2fv_remap_index },
   { 14044, WindowPos2i_remap_index },
   {  7859, WindowPos2iv_remap_index },
   { 14097, WindowPos2s_remap_index },
   { 53732, WindowPos2sv_remap_index },
   { 18763, WindowPos3d_remap_index },
   { 18098, WindowPos3dv_remap_index },
   { 18876, WindowPos3f_remap_index },
   { 10236, WindowPos3fv_remap_index },
   { 18985, WindowPos3i_remap_index },
   { 52461, WindowPos3iv_remap_index },
   { 19101, WindowPos3s_remap_index },
   { 29091, WindowPos3sv_remap_index },
   {  7725, BeginQuery_remap_index },
   { 52312, BindBuffer_remap_index },
   { 44944, BufferData_remap_index },
   { 12174, BufferSubData_remap_index },
   { 36366, DeleteBuffers_remap_index },
   { 26247, DeleteQueries_remap_index },
   { 23330, EndQuery_remap_index },
   { 49311, GenBuffers_remap_index },
   {  2263, GenQueries_remap_index },
   { 33347, GetBufferParameteriv_remap_index },
   { 49144, GetBufferPointerv_remap_index },
   { 36405, GetBufferSubData_remap_index },
   {  9833, GetQueryObjectiv_remap_index },
   {  9381, GetQueryObjectuiv_remap_index },
   { 14582, GetQueryiv_remap_index },
   { 21929, IsBuffer_remap_index },
   { 33649, IsQuery_remap_index },
   { 14737, MapBuffer_remap_index },
   { 51934, UnmapBuffer_remap_index },
   {   330, AttachShader_remap_index },
   { 43016, BindAttribLocation_remap_index },
   { 49408, BlendEquationSeparate_remap_index },
   { 37659, CompileShader_remap_index },
   { 29452, CreateProgram_remap_index },
   { 36255, CreateShader_remap_index },
   { 24367, DeleteProgram_remap_index },
   { 37604, DeleteShader_remap_index },
   { 51397, DetachShader_remap_index },
   { 40347, DisableVertexAttribArray_remap_index },
   { 27145, DrawBuffers_remap_index },
   { 44368, EnableVertexAttribArray_remap_index },
   { 43802, GetActiveAttrib_remap_index },
   { 50171, GetActiveUniform_remap_index },
   { 20887, GetAttachedShaders_remap_index },
   { 32131, GetAttribLocation_remap_index },
   { 13493, GetProgramInfoLog_remap_index },
   { 26880, GetProgramiv_remap_index },
   {  4694, GetShaderInfoLog_remap_index },
   {  9072, GetShaderSource_remap_index },
   { 20623, GetShaderiv_remap_index },
   {  7792, GetUniformLocation_remap_index },
   { 15829, GetUniformfv_remap_index },
   {  2581, GetUniformiv_remap_index },
   { 41173, GetVertexAttribPointerv_remap_index },
   { 49096, GetVertexAttribdv_remap_index },
   { 42006, GetVertexAttribfv_remap_index },
   { 45118, GetVertexAttribiv_remap_index },
   {  5195, IsProgram_remap_index },
   { 45665, IsShader_remap_index },
   { 34191, LinkProgram_remap_index },
   { 44740, ShaderSource_remap_index },
   { 44712, StencilFuncSeparate_remap_index },
   { 42395, StencilMaskSeparate_remap_index },
   { 44026, StencilOpSeparate_remap_index },
   { 48163, Uniform1f_remap_index },
   { 10063, Uniform1fv_remap_index },
   { 48239, Uniform1i_remap_index },
   { 22211, Uniform1iv_remap_index },
   { 50915, Uniform2f_remap_index },
   { 27046, Uniform2fv_remap_index },
   { 51004, Uniform2i_remap_index },
   { 24624, Uniform2iv_remap_index },
   {  1016, Uniform3f_remap_index },
   { 45230, Uniform3fv_remap_index },
   {   906, Uniform3i_remap_index },
   { 46831, Uniform3iv_remap_index },
   {  5554, Uniform4f_remap_index },
   { 10935, Uniform4fv_remap_index },
   {  5501, Uniform4i_remap_index },
   { 53877, Uniform4iv_remap_index },
   { 12291, UniformMatrix2fv_remap_index },
   { 27931, UniformMatrix3fv_remap_index },
   { 12878, UniformMatrix4fv_remap_index },
   { 48397, UseProgram_remap_index },
   { 29568, ValidateProgram_remap_index },
   { 22318, VertexAttrib1d_remap_index },
   { 45588, VertexAttrib1dv_remap_index },
   { 22508, VertexAttrib1s_remap_index },
   { 41827, VertexAttrib1sv_remap_index },
   {  9989, VertexAttrib2d_remap_index },
   { 28793, VertexAttrib2dv_remap_index },
   {  9901, VertexAttrib2s_remap_index },
   { 17534, VertexAttrib2sv_remap_index },
   { 14632, VertexAttrib3d_remap_index },
   { 26970, VertexAttrib3dv_remap_index },
   { 14491, VertexAttrib3s_remap_index },
   { 47953, VertexAttrib3sv_remap_index },
   { 14809, VertexAttrib4Nbv_remap_index },
   { 34087, VertexAttrib4Niv_remap_index },
   { 24960, VertexAttrib4Nsv_remap_index },
   {  1687, VertexAttrib4Nub_remap_index },
   { 39768, VertexAttrib4Nubv_remap_index },
   { 12952, VertexAttrib4Nuiv_remap_index },
   { 42814, VertexAttrib4Nusv_remap_index },
   { 11560, VertexAttrib4bv_remap_index },
   { 34440, VertexAttrib4d_remap_index },
   { 34985, VertexAttrib4dv_remap_index },
   { 46951, VertexAttrib4iv_remap_index },
   { 34540, VertexAttrib4s_remap_index },
   { 23463, VertexAttrib4sv_remap_index },
   { 12545, VertexAttrib4ubv_remap_index },
   { 24915, VertexAttrib4uiv_remap_index },
   {  1613, VertexAttrib4usv_remap_index },
   { 39862, VertexAttribPointer_remap_index },
   { 35802, UniformMatrix2x3fv_remap_index },
   {  1049, UniformMatrix2x4fv_remap_index },
   { 12925, UniformMatrix3x2fv_remap_index },
   { 52624, UniformMatrix3x4fv_remap_index },
   { 47382, UniformMatrix4x2fv_remap_index },
   { 14534, UniformMatrix4x3fv_remap_index },
   { 20529, BeginConditionalRender_remap_index },
   { 29697, BeginTransformFeedback_remap_index },
   {  9791, BindBufferBase_remap_index },
   {  9679, BindBufferRange_remap_index },
   { 27629, BindFragDataLocation_remap_index },
   { 28901, ClampColor_remap_index },
   { 20914, ClearBufferfi_remap_index },
   { 20738, ClearBufferfv_remap_index },
   { 25558, ClearBufferiv_remap_index },
   { 46517, ClearBufferuiv_remap_index },
   { 16319, ColorMaski_remap_index },
   {  7462, Disablei_remap_index },
   { 18817, Enablei_remap_index },
   { 28253, EndConditionalRender_remap_index },
   { 24114, EndTransformFeedback_remap_index },
   { 15031, GetBooleani_v_remap_index },
   { 48495, GetFragDataLocation_remap_index },
   { 25579, GetIntegeri_v_remap_index },
   { 34950, GetStringi_remap_index },
   { 37027, GetTexParameterIiv_remap_index },
   { 47811, GetTexParameterIuiv_remap_index },
   { 37265, GetTransformFeedbackVarying_remap_index },
   {  3631, GetUniformuiv_remap_index },
   { 52929, GetVertexAttribIiv_remap_index },
   { 25292, GetVertexAttribIuiv_remap_index },
   { 41056, IsEnabledi_remap_index },
   { 37744, TexParameterIiv_remap_index },
   { 20322, TexParameterIuiv_remap_index },
   { 47888, TransformFeedbackVaryings_remap_index },
   { 41555, Uniform1ui_remap_index },
   { 31491, Uniform1uiv_remap_index },
   { 30734, Uniform2ui_remap_index },
   { 16393, Uniform2uiv_remap_index },
   { 40241, Uniform3ui_remap_index },
   { 23522, Uniform3uiv_remap_index },
   { 14920, Uniform4ui_remap_index },
   { 22245, Uniform4uiv_remap_index },
   { 43303, VertexAttribI1iv_remap_index },
   { 14342, VertexAttribI1uiv_remap_index },
   {  9452, VertexAttribI4bv_remap_index },
   { 29636, VertexAttribI4sv_remap_index },
   { 10656, VertexAttribI4ubv_remap_index },
   {   466, VertexAttribI4usv_remap_index },
   { 49603, VertexAttribIPointer_remap_index },
   { 10568, PrimitiveRestartIndex_remap_index },
   { 41325, TexBuffer_remap_index },
   {  1849, FramebufferTexture_remap_index },
   { 29422, GetBufferParameteri64v_remap_index },
   { 21825, GetInteger64i_v_remap_index },
   { 49943, VertexAttribDivisor_remap_index },
   { 53515, MinSampleShading_remap_index },
   { 25234, MemoryBarrierByRegion_remap_index },
   {  9117, BindProgramARB_remap_index },
   { 37975, DeleteProgramsARB_remap_index },
   { 18930, GenProgramsARB_remap_index },
   { 17577, GetProgramEnvParameterdvARB_remap_index },
   { 36331, GetProgramEnvParameterfvARB_remap_index },
   { 38152, GetProgramLocalParameterdvARB_remap_index },
   { 45969, GetProgramLocalParameterfvARB_remap_index },
   { 27860, GetProgramStringARB_remap_index },
   { 10454, GetProgramivARB_remap_index },
   { 38503, IsProgramARB_remap_index },
   { 21489, ProgramEnvParameter4dARB_remap_index },
   {  3377, ProgramEnvParameter4dvARB_remap_index },
   { 47578, ProgramEnvParameter4fARB_remap_index },
   { 30313, ProgramEnvParameter4fvARB_remap_index },
   { 28303, ProgramLocalParameter4dARB_remap_index },
   {  5057, ProgramLocalParameter4dvARB_remap_index },
   { 37540, ProgramLocalParameter4fARB_remap_index },
   { 23872, ProgramLocalParameter4fvARB_remap_index },
   { 38575, ProgramStringARB_remap_index },
   { 14990, VertexAttrib1fARB_remap_index },
   { 39110, VertexAttrib1fvARB_remap_index },
   { 27350, VertexAttrib2fARB_remap_index },
   { 16640, VertexAttrib2fvARB_remap_index },
   {   349, VertexAttrib3fARB_remap_index },
   { 32529, VertexAttrib3fvARB_remap_index },
   { 31177, VertexAttrib4fARB_remap_index },
   { 18032, VertexAttrib4fvARB_remap_index },
   { 44155, AttachObjectARB_remap_index },
   { 27904, CreateProgramObjectARB_remap_index },
   { 20812, CreateShaderObjectARB_remap_index },
   { 19339, DeleteObjectARB_remap_index },
   { 47141, DetachObjectARB_remap_index },
   { 44579, GetAttachedObjectsARB_remap_index },
   { 30518, GetHandleARB_remap_index },
   { 25409, GetInfoLogARB_remap_index },
   { 26432, GetObjectParameterfvARB_remap_index },
   { 51507, GetObjectParameterivARB_remap_index },
   {  7059, DrawArraysInstancedARB_remap_index },
   {  9296, DrawElementsInstancedARB_remap_index },
   { 17331, BindFramebuffer_remap_index },
   { 10477, BindRenderbuffer_remap_index },
   { 41609, BlitFramebuffer_remap_index },
   {  8147, CheckFramebufferStatus_remap_index },
   { 24816, DeleteFramebuffers_remap_index },
   { 45512, DeleteRenderbuffers_remap_index },
   { 38413, FramebufferRenderbuffer_remap_index },
   { 41685, FramebufferTexture1D_remap_index },
   { 28682, FramebufferTexture2D_remap_index },
   { 33233, FramebufferTexture3D_remap_index },
   { 45783, FramebufferTextureLayer_remap_index },
   { 49215, GenFramebuffers_remap_index },
   { 40901, GenRenderbuffers_remap_index },
   {  9619, GenerateMipmap_remap_index },
   {  6619, GetFramebufferAttachmentParameteriv_remap_index },
   { 53581, GetRenderbufferParameteriv_remap_index },
   {  8390, IsFramebuffer_remap_index },
   { 31573, IsRenderbuffer_remap_index },
   {   733, RenderbufferStorage_remap_index },
   { 18653, RenderbufferStorageMultisample_remap_index },
   {  6515, FlushMappedBufferRange_remap_index },
   { 38218, MapBufferRange_remap_index },
   { 16486, BindVertexArray_remap_index },
   {  1267, DeleteVertexArrays_remap_index },
   { 48747, GenVertexArrays_remap_index },
   { 48020, IsVertexArray_remap_index },
   { 16178, GetActiveUniformBlockName_remap_index },
   { 53316, GetActiveUniformBlockiv_remap_index },
   { 24188, GetActiveUniformName_remap_index },
   { 17505, GetActiveUniformsiv_remap_index },
   { 51370, GetUniformBlockIndex_remap_index },
   { 13049, GetUniformIndices_remap_index },
   { 42730, UniformBlockBinding_remap_index },
   { 52579, CopyBufferSubData_remap_index },
   { 52979, ClientWaitSync_remap_index },
   { 14195, DeleteSync_remap_index },
   { 42757, FenceSync_remap_index },
   { 47121, GetInteger64v_remap_index },
   { 49799, GetSynciv_remap_index },
   { 19419, IsSync_remap_index },
   { 41157, WaitSync_remap_index },
   { 16213, DrawElementsBaseVertex_remap_index },
   { 21018, DrawElementsInstancedBaseVertex_remap_index },
   { 46072, DrawRangeElementsBaseVertex_remap_index },
   { 51036, MultiDrawElementsBaseVertex_remap_index },
   { 30009, ProvokingVertex_remap_index },
   {  6963, GetMultisamplefv_remap_index },
   { 43410, SampleMaski_remap_index },
   {  2451, TexImage2DMultisample_remap_index },
   { 51313, TexImage3DMultisample_remap_index },
   { 28414, BlendEquationSeparateiARB_remap_index },
   { 33794, BlendEquationiARB_remap_index },
   {  4746, BlendFuncSeparateiARB_remap_index },
   { 30554, BlendFunciARB_remap_index },
   {  2040, BindFragDataLocationIndexed_remap_index },
   { 35579, GetFragDataIndex_remap_index },
   {  3613, BindSampler_remap_index },
   { 52710, DeleteSamplers_remap_index },
   { 44075, GenSamplers_remap_index },
   {  3105, GetSamplerParameterIiv_remap_index },
   {  7373, GetSamplerParameterIuiv_remap_index },
   { 28764, GetSamplerParameterfv_remap_index },
   { 30881, GetSamplerParameteriv_remap_index },
   { 32328, IsSampler_remap_index },
   { 10995, SamplerParameterIiv_remap_index },
   { 15381, SamplerParameterIuiv_remap_index },
   { 35851, SamplerParameterf_remap_index },
   { 47478, SamplerParameterfv_remap_index },
   { 52731, SamplerParameteri_remap_index },
   { 34793, SamplerParameteriv_remap_index },
   { 28851, GetQueryObjecti64v_remap_index },
   {  5117, GetQueryObjectui64v_remap_index },
   { 15976, QueryCounter_remap_index },
   { 46501, ColorP3ui_remap_index },
   {  8544, ColorP3uiv_remap_index },
   { 21957, ColorP4ui_remap_index },
   { 32018, ColorP4uiv_remap_index },
   { 17146, MultiTexCoordP1ui_remap_index },
   { 31708, MultiTexCoordP1uiv_remap_index },
   { 33570, MultiTexCoordP2ui_remap_index },
   { 11415, MultiTexCoordP2uiv_remap_index },
   { 32106, MultiTexCoordP3ui_remap_index },
   {   440, MultiTexCoordP3uiv_remap_index },
   { 49552, MultiTexCoordP4ui_remap_index },
   { 42054, MultiTexCoordP4uiv_remap_index },
   { 44829, NormalP3ui_remap_index },
   { 31633, NormalP3uiv_remap_index },
   { 50616, SecondaryColorP3ui_remap_index },
   {  7321, SecondaryColorP3uiv_remap_index },
   {   177, TexCoordP1ui_remap_index },
   {   713, TexCoordP1uiv_remap_index },
   { 32461, TexCoordP2ui_remap_index },
   { 45166, TexCoordP2uiv_remap_index },
   { 18447, TexCoordP3ui_remap_index },
   { 22027, TexCoordP3uiv_remap_index },
   { 41808, TexCoordP4ui_remap_index },
   {  2182, TexCoordP4uiv_remap_index },
   { 18570, VertexAttribP1ui_remap_index },
   {  5169, VertexAttribP1uiv_remap_index },
   { 36542, VertexAttribP2ui_remap_index },
   {  6177, VertexAttribP2uiv_remap_index },
   {  1735, VertexAttribP3ui_remap_index },
   { 34819, VertexAttribP3uiv_remap_index },
   {  5450, VertexAttribP4ui_remap_index },
   { 20119, VertexAttribP4uiv_remap_index },
   { 42896, VertexP2ui_remap_index },
   { 19876, VertexP2uiv_remap_index },
   { 27887, VertexP3ui_remap_index },
   {  7774, VertexP3uiv_remap_index },
   {  3947, VertexP4ui_remap_index },
   { 53466, VertexP4uiv_remap_index },
   {   881, DrawArraysIndirect_remap_index },
   { 29063, DrawElementsIndirect_remap_index },
   {  8286, GetUniformdv_remap_index },
   { 48194, Uniform1d_remap_index },
   { 17640, Uniform1dv_remap_index },
   { 50947, Uniform2d_remap_index },
   { 34967, Uniform2dv_remap_index },
   {   998, Uniform3d_remap_index },
   { 35978, Uniform3dv_remap_index },
   {  5535, Uniform4d_remap_index },
   { 23953, Uniform4dv_remap_index },
   {  5092, UniformMatrix2dv_remap_index },
   { 27787, UniformMatrix2x3dv_remap_index },
   { 19793, UniformMatrix2x4dv_remap_index },
   { 35876, UniformMatrix3dv_remap_index },
   {  5373, UniformMatrix3x2dv_remap_index },
   {  6203, UniformMatrix3x4dv_remap_index },
   { 20759, UniformMatrix4dv_remap_index },
   { 39835, UniformMatrix4x2dv_remap_index },
   { 22584, UniformMatrix4x3dv_remap_index },
   {  6351, GetActiveSubroutineName_remap_index },
   {  7005, GetActiveSubroutineUniformName_remap_index },
   { 19651, GetActiveSubroutineUniformiv_remap_index },
   { 14783, GetProgramStageiv_remap_index },
   { 16038, GetSubroutineIndex_remap_index },
   {  1577, GetSubroutineUniformLocation_remap_index },
   { 53484, GetUniformSubroutineuiv_remap_index },
   {  8045, UniformSubroutinesuiv_remap_index },
   { 17920, PatchParameterfv_remap_index },
   { 13683, PatchParameteri_remap_index },
   { 13519, BindTransformFeedback_remap_index },
   { 13391, DeleteTransformFeedbacks_remap_index },
   { 43614, DrawTransformFeedback_remap_index },
   {  4951, GenTransformFeedbacks_remap_index },
   { 40493, IsTransformFeedback_remap_index },
   { 37577, PauseTransformFeedback_remap_index },
   { 43238, ResumeTransformFeedback_remap_index },
   { 27717, BeginQueryIndexed_remap_index },
   { 49994, DrawTransformFeedbackStream_remap_index },
   { 23637, EndQueryIndexed_remap_index },
   { 26836, GetQueryIndexediv_remap_index },
   { 24425, ClearDepthf_remap_index },
   { 29470, DepthRangef_remap_index },
   { 46568, GetShaderPrecisionFormat_remap_index },
   {  4122, ReleaseShaderCompiler_remap_index },
   { 31019, ShaderBinary_remap_index },
   { 24218, GetProgramBinary_remap_index },
   { 14854, ProgramBinary_remap_index },
   { 15146, ProgramParameteri_remap_index },
   { 14018, GetVertexAttribLdv_remap_index },
   { 52002, VertexAttribL1d_remap_index },
   {  8343, VertexAttribL1dv_remap_index },
   { 41938, VertexAttribL2d_remap_index },
   { 24165, VertexAttribL2dv_remap_index },
   { 44655, VertexAttribL3d_remap_index },
   { 16547, VertexAttribL3dv_remap_index },
   {  9559, VertexAttribL4d_remap_index },
   { 25100, VertexAttribL4dv_remap_index },
   { 51675, VertexAttribLPointer_remap_index },
   { 32907, DepthRangeArrayv_remap_index },
   { 51135, DepthRangeIndexed_remap_index },
   { 40473, GetDoublei_v_remap_index },
   { 43428, GetFloati_v_remap_index },
   { 51573, ScissorArrayv_remap_index },
   { 30768, ScissorIndexed_remap_index },
   { 34845, ScissorIndexedv_remap_index },
   { 23000, ViewportArrayv_remap_index },
   { 38680, ViewportIndexedf_remap_index },
   { 24266, ViewportIndexedfv_remap_index },
   { 10703, GetGraphicsResetStatusARB_remap_index },
   { 36783, GetnColorTableARB_remap_index },
   {  3547, GetnCompressedTexImageARB_remap_index },
   {  1341, GetnConvolutionFilterARB_remap_index },
   {  6021, GetnHistogramARB_remap_index },
   { 22885, GetnMapdvARB_remap_index },
   { 15272, GetnMapfvARB_remap_index },
   { 41961, GetnMapivARB_remap_index },
   { 47690, GetnMinmaxARB_remap_index },
   {  4599, GetnPixelMapfvARB_remap_index },
   {  7347, GetnPixelMapuivARB_remap_index },
   { 14465, GetnPixelMapusvARB_remap_index },
   { 27258, GetnPolygonStippleARB_remap_index },
   { 35347, GetnSeparableFilterARB_remap_index },
   { 12590, GetnTexImageARB_remap_index },
   { 34166, GetnUniformdvARB_remap_index },
   { 41452, GetnUniformfvARB_remap_index },
   {  4062, GetnUniformivARB_remap_index },
   { 16934, GetnUniformuivARB_remap_index },
   { 31238, ReadnPixelsARB_remap_index },
   { 40017, DrawArraysInstancedBaseInstance_remap_index },
   { 12795, DrawElementsInstancedBaseInstance_remap_index },
   {  3273, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 43149, DrawTransformFeedbackInstanced_remap_index },
   { 16595, DrawTransformFeedbackStreamInstanced_remap_index },
   { 51613, GetInternalformativ_remap_index },
   { 24386, GetActiveAtomicCounterBufferiv_remap_index },
   { 53438, BindImageTexture_remap_index },
   { 26345, MemoryBarrier_remap_index },
   { 41588, TexStorage1D_remap_index },
   { 28624, TexStorage2D_remap_index },
   { 33188, TexStorage3D_remap_index },
   {  1658, TextureStorage1DEXT_remap_index },
   { 43066, TextureStorage2DEXT_remap_index },
   { 27319, TextureStorage3DEXT_remap_index },
   { 43906, ClearBufferData_remap_index },
   {  2619, ClearBufferSubData_remap_index },
   { 38261, DispatchCompute_remap_index },
   {  8231, DispatchComputeIndirect_remap_index },
   { 43946, CopyImageSubData_remap_index },
   { 49818, TextureView_remap_index },
   { 26705, BindVertexBuffer_remap_index },
   { 36151, VertexAttribBinding_remap_index },
   { 36810, VertexAttribFormat_remap_index },
   { 39958, VertexAttribIFormat_remap_index },
   { 44627, VertexAttribLFormat_remap_index },
   { 42555, VertexBindingDivisor_remap_index },
   { 39480, FramebufferParameteri_remap_index },
   { 34350, GetFramebufferParameteriv_remap_index },
   { 15505, GetInternalformati64v_remap_index },
   { 46040, MultiDrawArraysIndirect_remap_index },
   { 22549, MultiDrawElementsIndirect_remap_index },
   { 37422, GetProgramInterfaceiv_remap_index },
   {  3964, GetProgramResourceIndex_remap_index },
   {  1543, GetProgramResourceLocation_remap_index },
   {   101, GetProgramResourceLocationIndex_remap_index },
   { 16713, GetProgramResourceName_remap_index },
   { 50517, GetProgramResourceiv_remap_index },
   { 44458, ShaderStorageBlockBinding_remap_index },
   { 22444, TexBufferRange_remap_index },
   { 46865, TexStorage2DMultisample_remap_index },
   { 34709, TexStorage3DMultisample_remap_index },
   {  4294, BufferStorage_remap_index },
   { 47409, ClearTexImage_remap_index },
   { 16146, ClearTexSubImage_remap_index },
   {  5264, BindBuffersBase_remap_index },
   { 18005, BindBuffersRange_remap_index },
   { 13366, BindImageTextures_remap_index },
   {  3490, BindSamplers_remap_index },
   { 50641, BindTextures_remap_index },
   { 30854, BindVertexBuffers_remap_index },
   { 18595, GetImageHandleARB_remap_index },
   { 24509, GetTextureHandleARB_remap_index },
   { 52230, GetTextureSamplerHandleARB_remap_index },
   { 46919, GetVertexAttribLui64vARB_remap_index },
   { 47714, IsImageHandleResidentARB_remap_index },
   { 49279, IsTextureHandleResidentARB_remap_index },
   {  7137, MakeImageHandleNonResidentARB_remap_index },
   { 19707, MakeImageHandleResidentARB_remap_index },
   { 46646, MakeTextureHandleNonResidentARB_remap_index },
   { 46006, MakeTextureHandleResidentARB_remap_index },
   { 25481, ProgramUniformHandleui64ARB_remap_index },
   { 29367, ProgramUniformHandleui64vARB_remap_index },
   { 12742, UniformHandleui64ARB_remap_index },
   { 27814, UniformHandleui64vARB_remap_index },
   { 27230, VertexAttribL1ui64ARB_remap_index },
   { 37511, VertexAttribL1ui64vARB_remap_index },
   { 19838, DispatchComputeGroupSizeARB_remap_index },
   { 23738, MultiDrawArraysIndirectCountARB_remap_index },
   { 38995, MultiDrawElementsIndirectCountARB_remap_index },
   { 44093, ClipControl_remap_index },
   { 24313, BindTextureUnit_remap_index },
   { 48312, BlitNamedFramebuffer_remap_index },
   {  7979, CheckNamedFramebufferStatus_remap_index },
   { 27742, ClearNamedBufferData_remap_index },
   { 48565, ClearNamedBufferSubData_remap_index },
   { 30642, ClearNamedFramebufferfi_remap_index },
   { 30675, ClearNamedFramebufferfv_remap_index },
   { 10031, ClearNamedFramebufferiv_remap_index },
   { 44679, ClearNamedFramebufferuiv_remap_index },
   { 47504, CompressedTextureSubImage1D_remap_index },
   { 42773, CompressedTextureSubImage2D_remap_index },
   { 40142, CompressedTextureSubImage3D_remap_index },
   {  3581, CopyNamedBufferSubData_remap_index },
   { 43187, CopyTextureSubImage1D_remap_index },
   { 38600, CopyTextureSubImage2D_remap_index },
   { 49745, CopyTextureSubImage3D_remap_index },
   {  6870, CreateBuffers_remap_index },
   { 43589, CreateFramebuffers_remap_index },
   {  1207, CreateProgramPipelines_remap_index },
   { 38034, CreateQueries_remap_index },
   { 10786, CreateRenderbuffers_remap_index },
   { 44846, CreateSamplers_remap_index },
   { 35028, CreateTextures_remap_index },
   {  1924, CreateTransformFeedbacks_remap_index },
   { 25673, CreateVertexArrays_remap_index },
   {  9528, DisableVertexArrayAttrib_remap_index },
   { 49378, EnableVertexArrayAttrib_remap_index },
   { 17111, FlushMappedNamedBufferRange_remap_index },
   { 37948, GenerateTextureMipmap_remap_index },
   {   406, GetCompressedTextureImage_remap_index },
   {  5614, GetNamedBufferParameteri64v_remap_index },
   { 28381, GetNamedBufferParameteriv_remap_index },
   { 34410, GetNamedBufferPointerv_remap_index },
   { 13833, GetNamedBufferSubData_remap_index },
   { 48698, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 51469, GetNamedFramebufferParameteriv_remap_index },
   { 36452, GetNamedRenderbufferParameteriv_remap_index },
   { 30138, GetQueryBufferObjecti64v_remap_index },
   { 52199, GetQueryBufferObjectiv_remap_index },
   { 11166, GetQueryBufferObjectui64v_remap_index },
   {  3671, GetQueryBufferObjectuiv_remap_index },
   {  3769, GetTextureImage_remap_index },
   { 39733, GetTextureLevelParameterfv_remap_index },
   { 42962, GetTextureLevelParameteriv_remap_index },
   { 17301, GetTextureParameterIiv_remap_index },
   { 26568, GetTextureParameterIuiv_remap_index },
   { 29209, GetTextureParameterfv_remap_index },
   { 32860, GetTextureParameteriv_remap_index },
   { 18729, GetTransformFeedbacki64_v_remap_index },
   {  4554, GetTransformFeedbacki_v_remap_index },
   { 29853, GetTransformFeedbackiv_remap_index },
   {  6788, GetVertexArrayIndexed64iv_remap_index },
   { 37712, GetVertexArrayIndexediv_remap_index },
   { 19395, GetVertexArrayiv_remap_index },
   { 36711, InvalidateNamedFramebufferData_remap_index },
   { 31973, InvalidateNamedFramebufferSubData_remap_index },
   { 11699, MapNamedBuffer_remap_index },
   { 14709, MapNamedBufferRange_remap_index },
   { 51757, NamedBufferData_remap_index },
   { 13200, NamedBufferStorage_remap_index },
   { 22417, NamedBufferSubData_remap_index },
   { 13227, NamedFramebufferDrawBuffer_remap_index },
   { 42237, NamedFramebufferDrawBuffers_remap_index },
   { 30820, NamedFramebufferParameteri_remap_index },
   { 25448, NamedFramebufferReadBuffer_remap_index },
   { 37622, NamedFramebufferRenderbuffer_remap_index },
   {  5649, NamedFramebufferTexture_remap_index },
   { 13260, NamedFramebufferTextureLayer_remap_index },
   { 10421, NamedRenderbufferStorage_remap_index },
   { 32359, NamedRenderbufferStorageMultisample_remap_index },
   { 26167, TextureBuffer_remap_index },
   { 13613, TextureBufferRange_remap_index },
   { 43096, TextureParameterIiv_remap_index },
   { 33049, TextureParameterIuiv_remap_index },
   { 41660, TextureParameterf_remap_index },
   {  2649, TextureParameterfv_remap_index },
   { 41741, TextureParameteri_remap_index },
   { 29505, TextureParameteriv_remap_index },
   { 12381, TextureStorage1D_remap_index },
   { 17275, TextureStorage2D_remap_index },
   { 48914, TextureStorage2DMultisample_remap_index },
   { 21973, TextureStorage3D_remap_index },
   {  4023, TextureStorage3DMultisample_remap_index },
   { 31118, TextureSubImage1D_remap_index },
   { 36177, TextureSubImage2D_remap_index },
   { 39642, TextureSubImage3D_remap_index },
   { 22611, TransformFeedbackBufferBase_remap_index },
   { 17694, TransformFeedbackBufferRange_remap_index },
   {  5351, UnmapNamedBuffer_remap_index },
   { 29770, VertexArrayAttribBinding_remap_index },
   { 14956, VertexArrayAttribFormat_remap_index },
   { 18171, VertexArrayAttribIFormat_remap_index },
   {  3191, VertexArrayAttribLFormat_remap_index },
   { 19929, VertexArrayBindingDivisor_remap_index },
   { 18466, VertexArrayElementBuffer_remap_index },
   { 50442, VertexArrayVertexBuffer_remap_index },
   { 20983, VertexArrayVertexBuffers_remap_index },
   {  2391, GetCompressedTextureSubImage_remap_index },
   {  8588, GetTextureSubImage_remap_index },
   { 21897, BufferPageCommitmentARB_remap_index },
   { 50097, NamedBufferPageCommitmentARB_remap_index },
   { 23612, GetUniformi64vARB_remap_index },
   { 41782, GetUniformui64vARB_remap_index },
   { 25123, GetnUniformi64vARB_remap_index },
   { 22066, GetnUniformui64vARB_remap_index },
   { 28075, ProgramUniform1i64ARB_remap_index },
   { 34584, ProgramUniform1i64vARB_remap_index },
   {  7695, ProgramUniform1ui64ARB_remap_index },
   { 53097, ProgramUniform1ui64vARB_remap_index },
   { 25516, ProgramUniform2i64ARB_remap_index },
   { 33539, ProgramUniform2i64vARB_remap_index },
   { 47765, ProgramUniform2ui64ARB_remap_index },
   { 43682, ProgramUniform2ui64vARB_remap_index },
   { 52281, ProgramUniform3i64ARB_remap_index },
   {    70, ProgramUniform3i64vARB_remap_index },
   { 48058, ProgramUniform3ui64ARB_remap_index },
   {  3003, ProgramUniform3ui64vARB_remap_index },
   { 17205, ProgramUniform4i64ARB_remap_index },
   { 21256, ProgramUniform4i64vARB_remap_index },
   { 26013, ProgramUniform4ui64ARB_remap_index },
   { 34484, ProgramUniform4ui64vARB_remap_index },
   { 28959, Uniform1i64ARB_remap_index },
   { 41261, Uniform1i64vARB_remap_index },
   {  3747, Uniform1ui64ARB_remap_index },
   {  8366, Uniform1ui64vARB_remap_index },
   { 33211, Uniform2i64ARB_remap_index },
   {  6940, Uniform2i64vARB_remap_index },
   { 11392, Uniform2ui64ARB_remap_index },
   { 22678, Uniform2ui64vARB_remap_index },
   { 18547, Uniform3i64ARB_remap_index },
   { 18075, Uniform3i64vARB_remap_index },
   {  2924, Uniform3ui64ARB_remap_index },
   { 21801, Uniform3ui64vARB_remap_index },
   { 47996, Uniform4i64ARB_remap_index },
   { 44132, Uniform4i64vARB_remap_index },
   {  8973, Uniform4ui64ARB_remap_index },
   { 35733, Uniform4ui64vARB_remap_index },
   { 27392, SpecializeShaderARB_remap_index },
   {  8260, InvalidateBufferData_remap_index },
   { 47351, InvalidateBufferSubData_remap_index },
   { 26305, InvalidateFramebuffer_remap_index },
   { 19740, InvalidateSubFramebuffer_remap_index },
   { 14895, InvalidateTexImage_remap_index },
   { 31296, InvalidateTexSubImage_remap_index },
   { 15728, PolygonOffsetEXT_remap_index },
   { 44111, DrawTexfOES_remap_index },
   { 30536, DrawTexfvOES_remap_index },
   {  1113, DrawTexiOES_remap_index },
   { 36647, DrawTexivOES_remap_index },
   { 15076, DrawTexsOES_remap_index },
   { 26499, DrawTexsvOES_remap_index },
   { 32035, DrawTexxOES_remap_index },
   { 46367, DrawTexxvOES_remap_index },
   { 29900, PointSizePointerOES_remap_index },
   {  1076, QueryMatrixxOES_remap_index },
   { 23701, SampleMaskSGIS_remap_index },
   { 40199, SamplePatternSGIS_remap_index },
   { 50964, ColorPointerEXT_remap_index },
   { 33688, EdgeFlagPointerEXT_remap_index },
   { 15805, IndexPointerEXT_remap_index },
   { 16013, NormalPointerEXT_remap_index },
   { 32972, TexCoordPointerEXT_remap_index },
   { 29610, VertexPointerEXT_remap_index },
   { 50886, DiscardFramebufferEXT_remap_index },
   { 13315, ActiveShaderProgram_remap_index },
   { 20069, BindProgramPipeline_remap_index },
   { 33740, CreateShaderProgramv_remap_index },
   {  4465, DeleteProgramPipelines_remap_index },
   { 30910, GenProgramPipelines_remap_index },
   { 10097, GetProgramPipelineInfoLog_remap_index },
   { 36886, GetProgramPipelineiv_remap_index },
   { 30973, IsProgramPipeline_remap_index },
   { 53296, LockArraysEXT_remap_index },
   { 52437, ProgramUniform1d_remap_index },
   { 36305, ProgramUniform1dv_remap_index },
   { 52391, ProgramUniform1f_remap_index },
   { 11876, ProgramUniform1fv_remap_index },
   { 52345, ProgramUniform1i_remap_index },
   { 18302, ProgramUniform1iv_remap_index },
   { 40686, ProgramUniform1ui_remap_index },
   { 52098, ProgramUniform1uiv_remap_index },
   {  2831, ProgramUniform2d_remap_index },
   { 11805, ProgramUniform2dv_remap_index },
   {  2784, ProgramUniform2f_remap_index },
   { 42913, ProgramUniform2fv_remap_index },
   {  2856, ProgramUniform2i_remap_index },
   { 25624, ProgramUniform2iv_remap_index },
   {   664, ProgramUniform2ui_remap_index },
   { 11200, ProgramUniform2uiv_remap_index },
   {  5681, ProgramUniform3d_remap_index },
   {  5588, ProgramUniform3dv_remap_index },
   {  5707, ProgramUniform3f_remap_index },
   { 35650, ProgramUniform3fv_remap_index },
   {  5755, ProgramUniform3i_remap_index },
   { 16064, ProgramUniform3iv_remap_index },
   { 18351, ProgramUniform3ui_remap_index },
   { 21653, ProgramUniform3uiv_remap_index },
   { 34225, ProgramUniform4d_remap_index },
   { 36567, ProgramUniform4dv_remap_index },
   { 34252, ProgramUniform4f_remap_index },
   { 49659, ProgramUniform4fv_remap_index },
   { 34301, ProgramUniform4i_remap_index },
   {  2312, ProgramUniform4iv_remap_index },
   { 48112, ProgramUniform4ui_remap_index },
   { 39285, ProgramUniform4uiv_remap_index },
   { 16113, ProgramUniformMatrix2dv_remap_index },
   { 23810, ProgramUniformMatrix2fv_remap_index },
   { 19360, ProgramUniformMatrix2x3dv_remap_index },
   { 26639, ProgramUniformMatrix2x3fv_remap_index },
   {  2228, ProgramUniformMatrix2x4dv_remap_index },
   {  9725, ProgramUniformMatrix2x4fv_remap_index },
   { 53208, ProgramUniformMatrix3dv_remap_index },
   { 45861, ProgramUniformMatrix3fv_remap_index },
   { 31834, ProgramUniformMatrix3x2dv_remap_index },
   { 40407, ProgramUniformMatrix3x2fv_remap_index },
   { 27110, ProgramUniformMatrix3x4dv_remap_index },
   { 32572, ProgramUniformMatrix3x4fv_remap_index },
   { 46177, ProgramUniformMatrix4dv_remap_index },
   { 38330, ProgramUniformMatrix4fv_remap_index },
   { 47543, ProgramUniformMatrix4x2dv_remap_index },
   {  2718, ProgramUniformMatrix4x2fv_remap_index },
   { 16788, ProgramUniformMatrix4x3dv_remap_index },
   {  9210, ProgramUniformMatrix4x3fv_remap_index },
   { 46899, UnlockArraysEXT_remap_index },
   { 38284, UseProgramStages_remap_index },
   {  1982, ValidateProgramPipeline_remap_index },
   { 20145, DebugMessageCallback_remap_index },
   { 38795, DebugMessageControl_remap_index },
   { 19174, DebugMessageInsert_remap_index },
   {  8767, GetDebugMessageLog_remap_index },
   {  8500, GetObjectLabel_remap_index },
   { 15097, GetObjectPtrLabel_remap_index },
   {  7644, ObjectLabel_remap_index },
   { 53383, ObjectPtrLabel_remap_index },
   { 22281, PopDebugGroup_remap_index },
   { 17732, PushDebugGroup_remap_index },
   { 10522, SecondaryColor3fEXT_remap_index },
   {  9943, SecondaryColor3fvEXT_remap_index },
   { 35297, MultiDrawElementsEXT_remap_index },
   { 13447, FogCoordfEXT_remap_index },
   { 22646, FogCoordfvEXT_remap_index },
   {  5242, ResizeBuffersMESA_remap_index },
   { 41870, WindowPos4dMESA_remap_index },
   { 33517, WindowPos4dvMESA_remap_index },
   {  5400, WindowPos4fMESA_remap_index },
   { 14211, WindowPos4fvMESA_remap_index },
   { 11512, WindowPos4iMESA_remap_index },
   {  4672, WindowPos4ivMESA_remap_index },
   { 34516, WindowPos4sMESA_remap_index },
   {  7622, WindowPos4svMESA_remap_index },
   { 35397, MultiModeDrawArraysIBM_remap_index },
   { 24589, MultiModeDrawElementsIBM_remap_index },
   { 40318, AreProgramsResidentNV_remap_index },
   { 49042, ExecuteProgramNV_remap_index },
   { 36273, GetProgramParameterdvNV_remap_index },
   { 44912, GetProgramParameterfvNV_remap_index },
   { 23907, GetProgramStringNV_remap_index },
   { 19611, GetProgramivNV_remap_index },
   { 22930, GetTrackMatrixivNV_remap_index },
   { 23425, GetVertexAttribdvNV_remap_index },
   { 21229, GetVertexAttribfvNV_remap_index },
   { 19962, GetVertexAttribivNV_remap_index },
   { 44557, LoadProgramNV_remap_index },
   { 24689, ProgramParameters4dvNV_remap_index },
   { 25378, ProgramParameters4fvNV_remap_index },
   {  8013, RequestResidentProgramsNV_remap_index },
   { 35829, TrackMatrixNV_remap_index },
   { 53709, VertexAttrib1dNV_remap_index },
   { 34615, VertexAttrib1dvNV_remap_index },
   { 35068, VertexAttrib1fNV_remap_index },
   { 51346, VertexAttrib1fvNV_remap_index },
   { 26095, VertexAttrib1sNV_remap_index },
   {  8695, VertexAttrib1svNV_remap_index },
   { 22906, VertexAttrib2dNV_remap_index },
   { 42213, VertexAttrib2dvNV_remap_index },
   { 33401, VertexAttrib2fNV_remap_index },
   { 31869, VertexAttrib2fvNV_remap_index },
   { 15901, VertexAttrib2sNV_remap_index },
   {  7297, VertexAttrib2svNV_remap_index },
   { 45321, VertexAttrib3dNV_remap_index },
   { 47652, VertexAttrib3dvNV_remap_index },
   {  6230, VertexAttrib3fNV_remap_index },
   { 50550, VertexAttrib3fvNV_remap_index },
   {  9154, VertexAttrib3sNV_remap_index },
   { 22957, VertexAttrib3svNV_remap_index },
   { 10838, VertexAttrib4dNV_remap_index },
   {  4390, VertexAttrib4dvNV_remap_index },
   { 10909, VertexAttrib4fNV_remap_index },
   { 50688, VertexAttrib4fvNV_remap_index },
   { 21759, VertexAttrib4sNV_remap_index },
   { 13659, VertexAttrib4svNV_remap_index },
   {  1955, VertexAttrib4ubNV_remap_index },
   { 13422, VertexAttrib4ubvNV_remap_index },
   { 35548, VertexAttribPointerNV_remap_index },
   { 33595, VertexAttribs1dvNV_remap_index },
   { 49577, VertexAttribs1fvNV_remap_index },
   {  7953, VertexAttribs1svNV_remap_index },
   { 42187, VertexAttribs2dvNV_remap_index },
   {  5424, VertexAttribs2fvNV_remap_index },
   { 33714, VertexAttribs2svNV_remap_index },
   {  2202, VertexAttribs3dvNV_remap_index },
   { 43642, VertexAttribs3fvNV_remap_index },
   { 17479, VertexAttribs3svNV_remap_index },
   {  6914, VertexAttribs4dvNV_remap_index },
   { 30051, VertexAttribs4fvNV_remap_index },
   { 12769, VertexAttribs4svNV_remap_index },
   { 39509, VertexAttribs4ubvNV_remap_index },
   { 51224, GetTexBumpParameterfvATI_remap_index },
   { 13075, GetTexBumpParameterivATI_remap_index },
   { 43774, TexBumpParameterfvATI_remap_index },
   { 10881, TexBumpParameterivATI_remap_index },
   { 11733, AlphaFragmentOp1ATI_remap_index },
   {  4414, AlphaFragmentOp2ATI_remap_index },
   { 23389, AlphaFragmentOp3ATI_remap_index },
   { 40874, BeginFragmentShaderATI_remap_index },
   {  4719, BindFragmentShaderATI_remap_index },
   {  9179, ColorFragmentOp1ATI_remap_index },
   { 15867, ColorFragmentOp2ATI_remap_index },
   { 29531, ColorFragmentOp3ATI_remap_index },
   { 20954, DeleteFragmentShaderATI_remap_index },
   { 53852, EndFragmentShaderATI_remap_index },
   { 28354, GenFragmentShadersATI_remap_index },
   { 50770, PassTexCoordATI_remap_index },
   { 43754, SampleMapATI_remap_index },
   { 42861, SetFragmentShaderConstantATI_remap_index },
   { 28980, DepthRangeArrayfvOES_remap_index },
   {  1514, DepthRangeIndexedfOES_remap_index },
   { 10812, ActiveStencilFaceEXT_remap_index },
   { 43266, GetProgramNamedParameterdvNV_remap_index },
   { 28104, GetProgramNamedParameterfvNV_remap_index },
   { 51255, ProgramNamedParameter4dNV_remap_index },
   { 46601, ProgramNamedParameter4dvNV_remap_index },
   { 30231, ProgramNamedParameter4fNV_remap_index },
   { 31396, ProgramNamedParameter4fvNV_remap_index },
   { 29958, PrimitiveRestartNV_remap_index },
   { 30496, GetTexGenxvOES_remap_index },
   { 18153, TexGenxOES_remap_index },
   { 39714, TexGenxvOES_remap_index },
   { 10187, DepthBoundsEXT_remap_index },
   { 46806, BindFramebufferEXT_remap_index },
   { 50828, BindRenderbufferEXT_remap_index },
   {  5475, StringMarkerGREMEDY_remap_index },
   { 38189, BufferParameteriAPPLE_remap_index },
   { 48460, FlushMappedBufferRangeAPPLE_remap_index },
   { 34044, VertexAttribI1iEXT_remap_index },
   { 14150, VertexAttribI1uiEXT_remap_index },
   { 25022, VertexAttribI2iEXT_remap_index },
   { 50254, VertexAttribI2ivEXT_remap_index },
   { 31527, VertexAttribI2uiEXT_remap_index },
   { 43363, VertexAttribI2uivEXT_remap_index },
   { 24032, VertexAttribI3iEXT_remap_index },
   { 52665, VertexAttribI3ivEXT_remap_index },
   { 28028, VertexAttribI3uiEXT_remap_index },
   { 46729, VertexAttribI3uivEXT_remap_index },
   { 46683, VertexAttribI4iEXT_remap_index },
   {  8650, VertexAttribI4ivEXT_remap_index },
   {  3225, VertexAttribI4uiEXT_remap_index },
   { 33901, VertexAttribI4uivEXT_remap_index },
   {  3869, ClearColorIiEXT_remap_index },
   {  1316, ClearColorIuiEXT_remap_index },
   { 29981, BindBufferOffsetEXT_remap_index },
   { 27440, BeginPerfMonitorAMD_remap_index },
   { 40276, DeletePerfMonitorsAMD_remap_index },
   {  6822, EndPerfMonitorAMD_remap_index },
   { 33111, GenPerfMonitorsAMD_remap_index },
   { 15638, GetPerfMonitorCounterDataAMD_remap_index },
   { 42693, GetPerfMonitorCounterInfoAMD_remap_index },
   { 53812, GetPerfMonitorCounterStringAMD_remap_index },
   { 53348, GetPerfMonitorCountersAMD_remap_index },
   { 17823, GetPerfMonitorGroupStringAMD_remap_index },
   { 36491, GetPerfMonitorGroupsAMD_remap_index },
   { 17237, SelectPerfMonitorCountersAMD_remap_index },
   { 17943, GetObjectParameterivAPPLE_remap_index },
   { 53069, ObjectPurgeableAPPLE_remap_index },
   {  2361, ObjectUnpurgeableAPPLE_remap_index },
   { 51847, ActiveProgramEXT_remap_index },
   { 41909, CreateShaderProgramEXT_remap_index },
   { 45429, UseShaderProgramEXT_remap_index },
   { 37384, TextureBarrierNV_remap_index },
   {  2675, VDPAUFiniNV_remap_index },
   {   969, VDPAUGetSurfaceivNV_remap_index },
   { 29404, VDPAUInitNV_remap_index },
   { 26617, VDPAUIsSurfaceNV_remap_index },
   { 26811, VDPAUMapSurfacesNV_remap_index },
   {  3910, VDPAURegisterOutputSurfaceNV_remap_index },
   { 15536, VDPAURegisterVideoSurfaceNV_remap_index },
   { 13863, VDPAUSurfaceAccessNV_remap_index },
   {  5994, VDPAUUnmapSurfacesNV_remap_index },
   { 46471, VDPAUUnregisterSurfaceNV_remap_index },
   { 11441, BeginPerfQueryINTEL_remap_index },
   { 41284, CreatePerfQueryINTEL_remap_index },
   { 20390, DeletePerfQueryINTEL_remap_index },
   { 50047, EndPerfQueryINTEL_remap_index },
   { 51817, GetFirstPerfQueryIdINTEL_remap_index },
   { 37337, GetNextPerfQueryIdINTEL_remap_index },
   { 39336, GetPerfCounterInfoINTEL_remap_index },
   {   850, GetPerfQueryDataINTEL_remap_index },
   { 27597, GetPerfQueryIdByNameINTEL_remap_index },
   { 24783, GetPerfQueryInfoINTEL_remap_index },
   { 47071, PolygonOffsetClampEXT_remap_index },
   {  8846, SubpixelPrecisionBiasNV_remap_index },
   {  3510, ConservativeRasterParameterfNV_remap_index },
   { 13796, ConservativeRasterParameteriNV_remap_index },
   { 52756, WindowRectanglesEXT_remap_index },
   { 10208, BufferStorageMemEXT_remap_index },
   { 53911, CreateMemoryObjectsEXT_remap_index },
   { 45923, DeleteMemoryObjectsEXT_remap_index },
   {  2156, DeleteSemaphoresEXT_remap_index },
   { 14319, GenSemaphoresEXT_remap_index },
   { 40518, GetMemoryObjectParameterivEXT_remap_index },
   { 34672, GetSemaphoreParameterui64vEXT_remap_index },
   { 46539, GetUnsignedBytei_vEXT_remap_index },
   { 23971, GetUnsignedBytevEXT_remap_index },
   { 42110, IsMemoryObjectEXT_remap_index },
   { 21603, IsSemaphoreEXT_remap_index },
   { 44523, MemoryObjectParameterivEXT_remap_index },
   { 25932, NamedBufferStorageMemEXT_remap_index },
   { 36749, SemaphoreParameterui64vEXT_remap_index },
   { 26138, SignalSemaphoreEXT_remap_index },
   {  7233, TexStorageMem1DEXT_remap_index },
   {  6147, TexStorageMem2DEXT_remap_index },
   { 48790, TexStorageMem2DMultisampleEXT_remap_index },
   {  6573, TexStorageMem3DEXT_remap_index },
   { 52845, TexStorageMem3DMultisampleEXT_remap_index },
   { 25829, TextureStorageMem1DEXT_remap_index },
   { 41022, TextureStorageMem2DEXT_remap_index },
   { 22111, TextureStorageMem2DMultisampleEXT_remap_index },
   { 10621, TextureStorageMem3DEXT_remap_index },
   { 40095, TextureStorageMem3DMultisampleEXT_remap_index },
   { 32502, WaitSemaphoreEXT_remap_index },
   {  7172, ImportMemoryFdEXT_remap_index },
   { 33621, ImportSemaphoreFdEXT_remap_index },
   { 53241, FramebufferFetchBarrierEXT_remap_index },
   { 25261, StencilFuncSeparateATI_remap_index },
   {  7198, ProgramEnvParameters4fvEXT_remap_index },
   {  8306, ProgramLocalParameters4fvEXT_remap_index },
   {  4908, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  4624, EGLImageTargetTexture2DOES_remap_index },
   { 50221, AlphaFuncx_remap_index },
   { 24077, ClearColorx_remap_index },
   { 49344, ClearDepthx_remap_index },
   { 44240, Color4x_remap_index },
   { 29305, DepthRangex_remap_index },
   {  2903, Fogx_remap_index },
   { 18497, Fogxv_remap_index },
   { 11133, Frustumf_remap_index },
   { 11264, Frustumx_remap_index },
   { 23997, LightModelx_remap_index },
   { 39554, LightModelxv_remap_index },
   { 35624, Lightx_remap_index },
   { 51107, Lightxv_remap_index },
   {  4522, LineWidthx_remap_index },
   {  4260, LoadMatrixx_remap_index },
   { 52167, Materialx_remap_index },
   { 31346, Materialxv_remap_index },
   { 52811, MultMatrixx_remap_index },
   { 12659, MultiTexCoord4x_remap_index },
   { 31147, Normal3x_remap_index },
   { 19251, Orthof_remap_index },
   { 19491, Orthox_remap_index },
   { 33948, PointSizex_remap_index },
   {  1375, PolygonOffsetx_remap_index },
   { 45754, Rotatex_remap_index },
   { 24740, SampleCoveragex_remap_index },
   { 15751, Scalex_remap_index },
   { 47206, TexEnvx_remap_index },
   { 53020, TexEnvxv_remap_index },
   {  2483, TexParameterx_remap_index },
   { 38961, Translatex_remap_index },
   { 40653, ClipPlanef_remap_index },
   { 40555, ClipPlanex_remap_index },
   {   811, GetClipPlanef_remap_index },
   {   625, GetClipPlanex_remap_index },
   { 24658, GetFixedv_remap_index },
   {  1416, GetLightxv_remap_index },
   { 28141, GetMaterialxv_remap_index },
   { 26463, GetTexEnvxv_remap_index },
   { 20839, GetTexParameterxv_remap_index },
   { 35901, PointParameterx_remap_index },
   { 45709, PointParameterxv_remap_index },
   { 23659, TexParameterxv_remap_index },
   {  7262, BlendBarrier_remap_index },
   { 42582, PrimitiveBoundingBox_remap_index },
   {    -1, -1 }
};

