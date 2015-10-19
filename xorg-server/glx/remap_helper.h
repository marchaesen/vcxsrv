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
   /* _mesa_function_pool[70]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[111]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTextureARB\0"
   "glFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[162]: TexCoordP1ui (will be remapped) */
   "ii\0"
   "glTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[181]: PolygonStipple (offset 175) */
   "p\0"
   "glPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[201]: ListParameterfSGIX (dynamic) */
   "iif\0"
   "glListParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[227]: MultiTexCoord1dv (offset 377) */
   "ip\0"
   "glMultiTexCoord1dv\0"
   "glMultiTexCoord1dvARB\0"
   "\0"
   /* _mesa_function_pool[272]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[287]: GetTexFilterFuncSGIS (dynamic) */
   "iip\0"
   "glGetTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[315]: AttachShader (will be remapped) */
   "ii\0"
   "glAttachShader\0"
   "\0"
   /* _mesa_function_pool[334]: VertexAttrib3fARB (will be remapped) */
   "ifff\0"
   "glVertexAttrib3f\0"
   "glVertexAttrib3fARB\0"
   "\0"
   /* _mesa_function_pool[377]: Indexubv (offset 316) */
   "p\0"
   "glIndexubv\0"
   "\0"
   /* _mesa_function_pool[391]: GetCompressedTextureImage (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImage\0"
   "\0"
   /* _mesa_function_pool[425]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[451]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[498]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[513]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[552]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[572]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[610]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[649]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[669]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[747]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[786]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[817]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[842]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[875]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[904]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[922]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[955]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[982]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[1004]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[1019]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1040]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1057]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1113]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[1142]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1173]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysAPPLE\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1248]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1273]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1307]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1341]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1405]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[1439]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[1475]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1520]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1549]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1597]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1622]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1653]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1668]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1699]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1711]: GetProgramResourceLocationIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocationIndex\0"
   "\0"
   /* _mesa_function_pool[1750]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[1781]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1808]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[1866]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "\0"
   /* _mesa_function_pool[1902]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[1921]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[1949]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[1969]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[1995]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[2030]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "\0"
   /* _mesa_function_pool[2063]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[2112]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[2142]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[2185]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[2202]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[2234]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2274]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2290]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2311]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2332]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2370]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2400]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[2426]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2442]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2469]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2535]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2582]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[2607]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2654]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2675]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2686]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[2730]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[2745]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2756]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[2785]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[2800]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[2830]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[2864]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[2912]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "\0"
   /* _mesa_function_pool[2967]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[3024]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[3080]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[3100]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[3134]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[3166]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[3184]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[3224]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[3256]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[3300]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[3326]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[3371]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3382]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3400]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3424]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3441]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3478]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3495]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[3526]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[3554]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[3593]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "\0"
   /* _mesa_function_pool[3618]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[3644]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[3740]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[3756]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[3790]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "\0"
   /* _mesa_function_pool[3812]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[3828]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[3851]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[3867]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[3891]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[3924]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[3942]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[3999]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[4031]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[4063]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[4076]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[4101]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[4134]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[4149]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[4171]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[4196]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[4223]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "\0"
   /* _mesa_function_pool[4305]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[4337]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[4380]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[4408]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[4430]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[4486]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[4521]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[4546]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[4598]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[4624]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[4639]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4671]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[4693]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[4717]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[4752]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4780]: UnmapNamedBuffer (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[4802]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[4829]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[4853]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[4913]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[4938]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[4972]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[4991]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[5025]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[5051]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[5086]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[5118]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[5144]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[5192]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[5240]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[5330]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[5390]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[5405]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[5431]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[5458]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[5485]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[5538]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[5584]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[5610]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[5637]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[5662]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[5708]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[5731]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[5758]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[5792]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[5838]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5864]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[5897]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5922]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[5980]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[5995]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[6121]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6164]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[6198]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[6221]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[6246]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[6266]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[6290]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[6314]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[6332]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[6373]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[6386]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[6464]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[6499]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[6523]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[6549]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[6575]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[6606]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "\0"
   /* _mesa_function_pool[6641]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[6738]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[6760]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "glObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[6797]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[6811]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "\0"
   /* _mesa_function_pool[6844]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[6862]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[6911]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[6929]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[6984]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[7002]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[7051]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[7077]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[7111]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[7143]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[7172]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[7199]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7245]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[7329]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[7358]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[7384]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[7404]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[7441]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "\0"
   /* _mesa_function_pool[7464]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[7521]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[7557]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[7574]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "glGetObjectLabelKHR\0"
   "\0"
   /* _mesa_function_pool[7618]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[7635]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[7662]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[7697]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7724]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[7769]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[7817]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "glGetDebugMessageLogKHR\0"
   "\0"
   /* _mesa_function_pool[7896]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[7935]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[7959]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[8000]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[8032]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[8081]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8110]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8137]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[8182]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[8219]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[8244]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[8275]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[8341]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[8361]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[8446]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "\0"
   /* _mesa_function_pool[8494]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[8539]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[8570]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[8601]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "\0"
   /* _mesa_function_pool[8626]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[8647]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[8661]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[8687]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[8733]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[8799]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[8841]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "\0"
   /* _mesa_function_pool[8887]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[8929]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[8975]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[9017]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[9049]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[9083]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[9148]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[9173]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[9194]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[9249]: BindVertexArrayAPPLE (will be remapped) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[9275]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[9335]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[9379]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[9412]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[9435]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[9480]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[9526]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[9579]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[9626]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "\0"
   /* _mesa_function_pool[9656]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[9682]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[9708]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[9734]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[9751]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[9779]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[9805]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[9839]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[9865]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[9892]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[9925]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[9953]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[9986]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[10020]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[10071]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[10084]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[10117]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[10131]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[10166]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[10212]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[10238]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[10263]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[10284]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[10309]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[10333]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[10357]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[10400]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[10427]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[10475]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[10496]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[10517]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[10530]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[10560]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[10586]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[10602]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[10628]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[10673]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[10722]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[10767]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[10784]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[10796]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[10822]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[10867]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[10889]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[10931]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[10956]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[10971]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[11012]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[11048]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11078]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[11089]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[11107]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[11154]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[11172]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[11197]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[11222]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11248]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[11287]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[11316]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[11361]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[11406]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[11432]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[11450]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[11475]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[11521]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11558]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[11584]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[11628]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[11675]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[11702]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[11749]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[11799]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[11825]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[11856]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[11877]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[11905]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[11950]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[11977]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[12010]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[12048]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[12065]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[12116]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[12141]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[12172]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[12197]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[12227]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[12243]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[12269]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[12297]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[12343]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[12363]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[12391]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[12409]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[12433]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "\0"
   /* _mesa_function_pool[12455]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12504]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[12534]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[12561]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[12583]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[12636]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[12689]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "\0"
   /* _mesa_function_pool[12715]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[12768]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[12821]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[12866]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[12882]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[12904]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[12990]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[13037]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[13083]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[13113]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[13139]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[13182]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[13209]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[13230]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "\0"
   /* _mesa_function_pool[13264]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[13307]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[13341]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[13369]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[13415]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[13441]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[13486]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[13527]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[13552]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[13588]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[13629]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[13674]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[13695]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "glGetObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[13744]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteriARB\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[13815]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[13853]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[13867]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[13893]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[13914]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[13958]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[14002]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[14030]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[14074]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[14110]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[14163]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[14176]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[14214]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[14266]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[14289]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[14315]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[14329]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[14343]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[14367]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[14405]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[14439]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[14463]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[14485]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[14514]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "\0"
   /* _mesa_function_pool[14533]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[14558]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[14584]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[14633]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[14666]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[14698]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[14733]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[14765]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[14783]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "\0"
   /* _mesa_function_pool[14825]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[14861]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[14880]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[14899]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[14918]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[14960]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[14979]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "\0"
   /* _mesa_function_pool[15002]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[15027]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[15072]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[15115]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[15145]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[15178]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15220]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[15255]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[15270]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[15323]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[15337]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15366]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "\0"
   /* _mesa_function_pool[15392]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[15408]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[15466]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[15487]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[15506]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[15541]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[15566]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[15585]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[15600]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[15638]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[15664]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[15694]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[15737]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[15782]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[15816]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[15842]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[15868]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[15897]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[15940]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[15975]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[16003]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[16021]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[16057]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[16095]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "glPushDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[16138]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16186]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[16223]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[16236]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[16253]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[16283]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[16306]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[16340]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[16368]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[16395]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[16438]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[16493]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[16511]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[16545]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[16566]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[16604]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[16621]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[16642]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[16691]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[16741]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[16787]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[16806]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[16837]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[16860]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[16887]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[16912]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[16930]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[16943]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[17019]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[17053]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[17107]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "\0"
   /* _mesa_function_pool[17140]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[17194]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[17231]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[17249]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[17303]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[17348]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[17365]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[17419]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[17438]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "glDebugMessageInsertKHR\0"
   "\0"
   /* _mesa_function_pool[17515]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[17544]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[17603]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[17624]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[17659]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[17683]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[17695]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[17710]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[17755]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[17784]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[17800]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[17818]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[17834]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17875]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[17897]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[17915]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[17953]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[17971]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[17989]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[18025]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[18042]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[18069]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[18087]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[18105]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[18140]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[18173]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[18200]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[18235]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[18280]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[18330]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[18356]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "glDebugMessageCallbackKHR\0"
   "\0"
   /* _mesa_function_pool[18435]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[18478]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[18533]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[18579]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[18605]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[18648]: GenVertexArraysAPPLE (will be remapped) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[18675]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[18718]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[18774]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[18812]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[18831]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[18865]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[18927]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[18948]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[18973]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[19001]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[19028]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[19076]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[19103]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[19125]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[19143]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[19172]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[19207]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "\0"
   /* _mesa_function_pool[19249]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[19273]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[19301]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[19329]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[19344]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[19371]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[19400]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[19414]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[19453]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[19505]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[19557]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[19573]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[19631]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[19687]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[19717]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[19768]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[19788]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[19823]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[19849]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[19865]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[19888]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[19937]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[19965]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[19981]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[20008]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[20035]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[20055]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[20089]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[20106]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[20132]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[20160]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[20194]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[20230]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "glPopDebugGroupKHR\0"
   "\0"
   /* _mesa_function_pool[20267]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[20308]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[20366]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[20393]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "\0"
   /* _mesa_function_pool[20417]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[20458]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[20493]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[20520]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[20555]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[20587]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[20612]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[20704]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[20749]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[20780]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[20795]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[20814]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[20838]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[20865]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[20889]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[20908]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "\0"
   /* _mesa_function_pool[20930]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[21001]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[21054]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[21099]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[21135]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[21159]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[21188]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[21206]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[21218]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "\0"
   /* _mesa_function_pool[21246]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[21263]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[21280]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[21307]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[21318]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[21361]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[21377]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[21413]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[21467]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[21489]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[21531]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[21568]: FramebufferTextureFaceARB (dynamic) */
   "iiiii\0"
   "glFramebufferTextureFaceARB\0"
   "\0"
   /* _mesa_function_pool[21603]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[21665]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[21700]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[21726]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[21746]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[21764]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[21799]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[21844]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[21881]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[21932]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "\0"
   /* _mesa_function_pool[21955]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[21973]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[22021]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "\0"
   /* _mesa_function_pool[22045]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[22067]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[22081]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[22099]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[22118]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[22157]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[22191]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[22213]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[22241]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[22296]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[22331]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[22365]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[22396]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[22427]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[22447]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[22490]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[22523]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[22596]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[22622]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[22667]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[22712]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[22729]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[22773]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[22790]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[22807]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "\0"
   /* _mesa_function_pool[22830]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[22866]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[22883]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[22914]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[22941]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[22972]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[23024]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[23043]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[23058]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[23089]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[23111]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[23128]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[23161]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[23173]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[23194]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[23239]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[23288]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[23313]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[23357]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[23405]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[23444]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[23466]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[23514]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[23544]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[23592]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[23639]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[23659]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[23688]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[23711]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[23731]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[23752]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[23782]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[23796]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[23811]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "\0"
   /* _mesa_function_pool[23850]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[23879]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[23890]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[23909]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[23959]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[23977]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[24008]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[24044]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[24062]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[24075]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[24094]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[24113]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[24144]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[24162]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[24184]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[24250]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[24275]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[24296]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[24316]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[24336]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[24356]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[24381]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[24407]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[24425]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[24445]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[24462]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[24482]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[24515]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[24558]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[24591]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[24625]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[24655]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[24690]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[24775]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[24803]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[24818]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[24836]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[24867]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[24909]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[24964]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[24984]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[25041]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[25073]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[25127]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[25146]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[25161]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[25186]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[25216]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[25231]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[25258]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[25275]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[25302]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[25319]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[25346]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[25393]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[25443]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[25490]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[25511]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[25548]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[25588]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[25612]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[25660]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[25710]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[25747]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[25761]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[25788]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[25821]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "\0"
   /* _mesa_function_pool[25913]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[25930]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[25975]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[25997]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[26033]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[26115]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[26136]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[26165]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[26208]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[26223]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[26273]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[26306]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[26331]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[26386]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[26414]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[26469]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[26499]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[26516]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[26532]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[26561]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[26575]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[26613]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[26628]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[26663]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[26690]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[26708]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[26738]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[26756]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[26791]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[26817]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[26854]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[26896]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[26922]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[26967]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[26983]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[27039]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[27056]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[27088]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[27105]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[27122]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[27139]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[27169]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[27186]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[27213]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[27230]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[27244]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[27267]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[27295]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[27337]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[27363]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[27395]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[27410]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[27424]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[27457]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[27482]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[27500]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[27517]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[27554]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[27599]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[27656]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[27674]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[27704]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[27719]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[27731]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[27782]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[27804]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[27830]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[27848]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "\0"
   /* _mesa_function_pool[27904]: ClearNamedFramebufferfi (will be remapped) */
   "iifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[27936]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[27968]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[27995]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[28029]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "\0"
   /* _mesa_function_pool[28053]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[28061]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[28095]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[28122]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[28151]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[28202]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[28214]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[28260]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[28282]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[28315]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[28359]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[28388]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[28418]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[28462]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[28479]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "\0"
   /* _mesa_function_pool[28506]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[28540]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[28556]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[28590]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[28606]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[28641]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[28657]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[28673]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[28689]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[28701]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[28737]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[28783]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[28843]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[28861]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[28873]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[28885]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[28906]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[28918]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[28944]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[29000]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[29015]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[29030]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[29044]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[29079]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[29103]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[29118]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[29156]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[29183]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[29228]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[29245]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[29266]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[29283]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[29316]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[29341]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[29388]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[29425]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[29441]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[29486]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[29501]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[29517]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[29562]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[29581]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[29601]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[29619]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[29638]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[29660]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[29703]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[29769]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[29804]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[29833]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[29893]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[29911]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[29955]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[29973]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[29991]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[30020]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[30038]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[30062]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[30103]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[30131]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[30149]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[30180]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[30208]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[30242]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[30267]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[30286]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[30302]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[30319]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[30342]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[30425]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[30456]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[30510]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[30534]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[30564]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[30611]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[30626]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[30648]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[30673]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[30699]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "\0"
   /* _mesa_function_pool[30725]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[30751]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[30777]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[30831]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "\0"
   /* _mesa_function_pool[30898]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[30945]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[30977]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[30995]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[31020]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[31063]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[31108]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[31142]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[31167]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[31201]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[31228]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[31277]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[31326]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[31359]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[31386]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[31416]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[31460]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[31484]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[31528]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[31552]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[31585]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "glTexStorage3DMultisampleOES\0"
   "\0"
   /* _mesa_function_pool[31649]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[31669]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[31695]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[31721]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "\0"
   /* _mesa_function_pool[31743]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[31773]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[31805]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[31822]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[31840]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[31883]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[31905]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[31923]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[31946]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[32010]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[32063]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[32089]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[32111]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[32129]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[32152]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[32202]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[32237]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[32252]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[32284]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[32303]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[32323]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[32338]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[32352]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[32368]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[32389]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[32403]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[32434]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "\0"
   /* _mesa_function_pool[32457]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[32483]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[32532]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[32549]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[32566]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[32611]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[32638]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[32660]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[32685]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[32710]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[32753]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[32787]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[32805]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[32847]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[32933]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[32960]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[32986]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[33017]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[33064]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[33082]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[33114]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[33140]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[33175]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[33214]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[33261]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[33300]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[33331]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[33351]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[33376]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[33402]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[33456]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[33474]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[33520]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[33558]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[33585]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[33613]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[33629]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[33645]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[33661]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[33715]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[33746]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[33802]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[33852]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[33868]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[33924]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[34016]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[34088]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[34118]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[34135]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[34173]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[34203]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[34262]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[34299]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[34326]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[34344]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[34381]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[34419]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[34434]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[34466]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[34510]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[34526]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[34547]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[34581]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[34622]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[34649]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[34676]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[34719]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[34735]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[34756]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[34774]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[34808]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[34853]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[34890]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[34919]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[34962]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[34985]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[35031]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[35093]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[35114]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[35204]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[35236]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[35256]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[35276]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[35301]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35335]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[35381]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "\0"
   /* _mesa_function_pool[35407]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[35453]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[35474]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "glDebugMessageControlKHR\0"
   "\0"
   /* _mesa_function_pool[35554]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[35600]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[35640]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[35674]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[35699]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[35712]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[35755]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[35772]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[35799]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[35817]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[35835]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[35887]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[35938]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[35977]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[35993]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[36036]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[36082]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[36111]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[36138]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[36156]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[36193]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[36244]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[36277]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[36296]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[36316]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[36335]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[36370]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[36417]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36437]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[36464]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[36519]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[36534]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36560]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[36588]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[36619]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[36660]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[36703]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[36717]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[36759]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[36794]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[36822]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[36836]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[36865]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[36925]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[36991]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[37011]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[37036]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[37069]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[37116]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[37134]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[37167]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[37215]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37272]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[37291]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[37311]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[37329]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[37355]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[37382]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[37449]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[37503]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "\0"
   /* _mesa_function_pool[37542]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[37572]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[37588]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[37676]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[37703]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[37717]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "\0"
   /* _mesa_function_pool[37749]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[37766]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[37782]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[37798]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[37814]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[37839]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[37855]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[37882]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[37915]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[37936]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[37987]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[38012]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[38068]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[38093]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[38109]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[38128]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[38171]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[38195]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[38210]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[38239]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "\0"
   /* _mesa_function_pool[38262]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[38283]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[38307]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[38355]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[38381]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[38411]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[38430]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[38465]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[38482]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[38506]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[38541]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[38581]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[38647]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[38664]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[38690]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[38735]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[38762]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[38779]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[38824]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[38851]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[38888]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[38915]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[38931]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[38972]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[39019]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[39054]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[39071]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[39120]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[39155]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[39174]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[39224]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[39254]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[39281]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[39307]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[39345]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[39377]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[39396]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[39424]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[39461]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[39506]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[39521]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[39568]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[39586]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "\0"
   /* _mesa_function_pool[39605]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[39650]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[39670]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[39730]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[39776]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[39804]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[39830]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[39844]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[39864]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[39884]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[39904]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[39932]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[39980]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40018]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[40036]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[40061]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[40076]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "\0"
   /* _mesa_function_pool[40112]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[40161]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[40179]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[40197]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[40218]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[40240]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[40270]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[40282]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[40303]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[40332]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[40390]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[40406]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[40431]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[40489]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[40505]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[40521]: ShaderStorageBlockBinding (will be remapped) */
   "iii\0"
   "glShaderStorageBlockBinding\0"
   "\0"
   /* _mesa_function_pool[40554]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[40570]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[40586]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[40608]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[40638]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[40656]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[40684]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "\0"
   /* _mesa_function_pool[40708]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[40741]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[40769]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[40808]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[40823]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[40858]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[40875]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[40896]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[40941]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[40973]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[41008]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[41054]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[41081]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[41117]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[41147]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[41195]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[41215]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41259]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[41293]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[41350]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[41375]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41439]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[41458]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[41484]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[41498]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[41516]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[41541]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[41617]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[41660]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[41694]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[41708]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[41724]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[41738]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[41783]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[41812]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerARB\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[41903]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[41919]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[41981]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[41998]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[42035]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[42067]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[42106]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[42139]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[42163]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[42183]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[42229]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[42257]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42296]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[42314]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[42336]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[42351]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[42368]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[42400]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[42430]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[42446]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[42468]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[42501]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[42536]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[42546]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[42592]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[42622]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[42647]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[42681]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[42715]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[42735]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[42759]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[42802]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[42879]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "\0"
   /* _mesa_function_pool[42908]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[42928]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[42950]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[42963]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[42978]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[42993]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[43021]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[43057]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[43082]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[43138]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[43169]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[43196]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[43219]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[43239]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[43265]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[43291]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[43330]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[43365]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[43423]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[43439]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[43463]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[43477]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[43501]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[43522]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[43557]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[43572]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[43624]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[43689]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[43732]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayAPPLE\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[43791]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[43813]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[43864]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[43895]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[43911]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[43940]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[43971]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[43994]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[44013]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[44050]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[44071]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[44098]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[44136]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[44161]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[44196]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[44247]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[44266]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[44301]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[44324]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[44344]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[44399]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[44442]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44485]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[44509]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[44524]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[44562]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[44586]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[44601]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[44619]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[44637]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[44652]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[44676]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[44691]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[44706]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[44754]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[44825]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[44889]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[44922]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[44956]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[44986]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[45095]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[45130]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[45155]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[45181]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[45237]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[45286]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[45305]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[45323]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[45358]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[45377]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[45396]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[45420]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[45436]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[45503]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[45521]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[45572]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[45607]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[45625]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[45648]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[45675]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[45712]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[45762]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[45795]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[45840]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[45860]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[45876]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[45892]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[45944]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[45977]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[45998]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[46019]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[46052]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[46076]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46118]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[46143]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[46163]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[46190]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[46214]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[46227]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[46272]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[46295]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[46315]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[46330]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[46356]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[46371]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[46388]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[46417]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[46449]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[46466]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[46491]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[46506]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[46538]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[46576]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[46604]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[46629]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[46693]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[46724]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[46761]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[46782]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[46815]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[46839]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[46866]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[46885]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[46938]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[46976]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[47007]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[47021]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[47042]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "\0"
   /* _mesa_function_pool[47063]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[47092]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[47109]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[47125]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "\0"
   /* _mesa_function_pool[47155]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[47169]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[47188]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[47207]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[47231]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[47267]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[47297]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[47319]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[47355]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[47384]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[47435]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[47452]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "\0"
   /* _mesa_function_pool[47474]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[47491]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[47514]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[47531]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[47557]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[47608]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[47626]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[47658]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[47689]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[47707]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[47740]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[47786]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[47832]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[47856]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[47911]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[47974]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[48001]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[48019]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[48046]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[48060]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[48105]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[48126]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[48151]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[48165]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[48179]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[48213]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[48227]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[48254]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[48304]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[48326]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[48345]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[48375]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[48394]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[48422]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[48441]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[48460]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[48478]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[48501]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[48534]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[48554]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[48586]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[48621]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "glObjectPtrLabelKHR\0"
   "\0"
   /* _mesa_function_pool[48663]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[48676]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[48704]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[48722]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[48753]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "\0"
   /* _mesa_function_pool[48797]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[48895]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[48925]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[48948]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[49003]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[49028]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[49068]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[49093]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 20308, CompressedTexImage1D_remap_index },
   { 17544, CompressedTexImage2D_remap_index },
   { 12904, CompressedTexImage3D_remap_index },
   { 31946, CompressedTexSubImage1D_remap_index },
   { 38581, CompressedTexSubImage2D_remap_index },
   {  6641, CompressedTexSubImage3D_remap_index },
   {  4430, GetCompressedTexImage_remap_index },
   { 19505, LoadTransposeMatrixd_remap_index },
   { 19453, LoadTransposeMatrixf_remap_index },
   { 35835, MultTransposeMatrixd_remap_index },
   { 14214, MultTransposeMatrixf_remap_index },
   { 34581, SampleCoverage_remap_index },
   {  3644, BlendFuncSeparate_remap_index },
   { 23313, FogCoordPointer_remap_index },
   { 42592, FogCoordd_remap_index },
   { 42368, FogCoorddv_remap_index },
   { 34808, MultiDrawArrays_remap_index },
   { 32847, PointParameterf_remap_index },
   {  5240, PointParameterfv_remap_index },
   { 32805, PointParameteri_remap_index },
   {  9335, PointParameteriv_remap_index },
   {  5662, SecondaryColor3b_remap_index },
   { 42183, SecondaryColor3bv_remap_index },
   { 36036, SecondaryColor3d_remap_index },
   { 13037, SecondaryColor3dv_remap_index },
   {  5792, SecondaryColor3i_remap_index },
   { 39730, SecondaryColor3iv_remap_index },
   {  5538, SecondaryColor3s_remap_index },
   { 16741, SecondaryColor3sv_remap_index },
   { 23466, SecondaryColor3ub_remap_index },
   {  7769, SecondaryColor3ubv_remap_index },
   { 23544, SecondaryColor3ui_remap_index },
   { 25612, SecondaryColor3uiv_remap_index },
   { 23357, SecondaryColor3us_remap_index },
   { 10427, SecondaryColor3usv_remap_index },
   { 37215, SecondaryColorPointer_remap_index },
   { 12636, WindowPos2d_remap_index },
   { 18478, WindowPos2dv_remap_index },
   { 12583, WindowPos2f_remap_index },
   { 24909, WindowPos2fv_remap_index },
   { 12715, WindowPos2i_remap_index },
   {  6929, WindowPos2iv_remap_index },
   { 12768, WindowPos2s_remap_index },
   { 48948, WindowPos2sv_remap_index },
   { 17053, WindowPos3d_remap_index },
   { 16438, WindowPos3dv_remap_index },
   { 17140, WindowPos3f_remap_index },
   {  9194, WindowPos3fv_remap_index },
   { 17249, WindowPos3i_remap_index },
   { 47856, WindowPos3iv_remap_index },
   { 17365, WindowPos3s_remap_index },
   { 26414, WindowPos3sv_remap_index },
   {  6811, BeginQuery_remap_index },
   { 47707, BindBuffer_remap_index },
   { 40973, BufferData_remap_index },
   { 10971, BufferSubData_remap_index },
   { 33175, DeleteBuffers_remap_index },
   { 23811, DeleteQueries_remap_index },
   { 21218, EndQuery_remap_index },
   { 44889, GenBuffers_remap_index },
   {  2030, GenQueries_remap_index },
   { 30456, GetBufferParameteriv_remap_index },
   { 44754, GetBufferPointerv_remap_index },
   { 33214, GetBufferSubData_remap_index },
   {  8841, GetQueryObjectiv_remap_index },
   {  8446, GetQueryObjectuiv_remap_index },
   { 13230, GetQueryiv_remap_index },
   { 19937, IsBuffer_remap_index },
   { 30699, IsQuery_remap_index },
   { 13369, MapBuffer_remap_index },
   { 47384, UnmapBuffer_remap_index },
   {   315, AttachShader_remap_index },
   { 39174, BindAttribLocation_remap_index },
   { 44986, BlendEquationSeparate_remap_index },
   { 34381, CompileShader_remap_index },
   { 26738, CreateProgram_remap_index },
   { 33064, CreateShader_remap_index },
   { 22099, DeleteProgram_remap_index },
   { 34326, DeleteShader_remap_index },
   { 46866, DetachShader_remap_index },
   { 36865, DisableVertexAttribArray_remap_index },
   { 24690, DrawBuffers_remap_index },
   { 40431, EnableVertexAttribArray_remap_index },
   { 39932, GetActiveAttrib_remap_index },
   { 45712, GetActiveUniform_remap_index },
   { 19076, GetAttachedShaders_remap_index },
   { 29341, GetAttribLocation_remap_index },
   { 12243, GetProgramInfoLog_remap_index },
   { 24425, GetProgramiv_remap_index },
   {  4171, GetShaderInfoLog_remap_index },
   {  8137, GetShaderSource_remap_index },
   { 18812, GetShaderiv_remap_index },
   {  6862, GetUniformLocation_remap_index },
   { 14367, GetUniformfv_remap_index },
   {  2332, GetUniformiv_remap_index },
   { 37588, GetVertexAttribPointerv_remap_index },
   { 44706, GetVertexAttribdv_remap_index },
   { 38307, GetVertexAttribfv_remap_index },
   { 41147, GetVertexAttribiv_remap_index },
   {  4624, IsProgram_remap_index },
   { 41694, IsShader_remap_index },
   { 31167, LinkProgram_remap_index },
   { 40769, ShaderSource_remap_index },
   { 40741, StencilFuncSeparate_remap_index },
   { 38664, StencilMaskSeparate_remap_index },
   { 40112, StencilOpSeparate_remap_index },
   { 43864, Uniform1f_remap_index },
   {  9049, Uniform1fv_remap_index },
   { 43940, Uniform1i_remap_index },
   { 20160, Uniform1iv_remap_index },
   { 46417, Uniform2f_remap_index },
   { 24591, Uniform2fv_remap_index },
   { 46506, Uniform2i_remap_index },
   { 22331, Uniform2iv_remap_index },
   {   922, Uniform3f_remap_index },
   { 41259, Uniform3fv_remap_index },
   {   842, Uniform3i_remap_index },
   { 42647, Uniform3iv_remap_index },
   {  4991, Uniform4f_remap_index },
   {  9805, Uniform4fv_remap_index },
   {  4938, Uniform4i_remap_index },
   { 49093, Uniform4iv_remap_index },
   { 11107, UniformMatrix2fv_remap_index },
   { 25346, UniformMatrix3fv_remap_index },
   { 11628, UniformMatrix4fv_remap_index },
   { 44098, UseProgram_remap_index },
   { 26854, ValidateProgram_remap_index },
   { 20267, VertexAttrib1d_remap_index },
   { 41617, VertexAttrib1dv_remap_index },
   { 20417, VertexAttrib1s_remap_index },
   { 38128, VertexAttrib1sv_remap_index },
   {  8975, VertexAttrib2d_remap_index },
   { 26165, VertexAttrib2dv_remap_index },
   {  8887, VertexAttrib2s_remap_index },
   { 15897, VertexAttrib2sv_remap_index },
   { 13264, VertexAttrib3d_remap_index },
   { 24515, VertexAttrib3dv_remap_index },
   { 13139, VertexAttrib3s_remap_index },
   { 43689, VertexAttrib3sv_remap_index },
   { 13441, VertexAttrib4Nbv_remap_index },
   { 31063, VertexAttrib4Niv_remap_index },
   { 22667, VertexAttrib4Nsv_remap_index },
   {  1549, VertexAttrib4Nub_remap_index },
   { 36370, VertexAttrib4Nubv_remap_index },
   { 11702, VertexAttrib4Nuiv_remap_index },
   { 38972, VertexAttrib4Nusv_remap_index },
   { 10357, VertexAttrib4bv_remap_index },
   { 31416, VertexAttrib4d_remap_index },
   { 31840, VertexAttrib4dv_remap_index },
   { 42759, VertexAttrib4iv_remap_index },
   { 31484, VertexAttrib4s_remap_index },
   { 21318, VertexAttrib4sv_remap_index },
   { 11361, VertexAttrib4ubv_remap_index },
   { 22622, VertexAttrib4uiv_remap_index },
   {  1475, VertexAttrib4usv_remap_index },
   { 36464, VertexAttribPointer_remap_index },
   { 32611, UniformMatrix2x3fv_remap_index },
   {   955, UniformMatrix2x4fv_remap_index },
   { 11675, UniformMatrix3x2fv_remap_index },
   { 48019, UniformMatrix3x4fv_remap_index },
   { 43169, UniformMatrix4x2fv_remap_index },
   { 13182, UniformMatrix4x3fv_remap_index },
   { 18718, BeginConditionalRender_remap_index },
   { 26983, BeginTransformFeedback_remap_index },
   {  8799, BindBufferBase_remap_index },
   {  8687, BindBufferRange_remap_index },
   { 25073, BindFragDataLocation_remap_index },
   { 26273, ClampColor_remap_index },
   { 19103, ClearBufferfi_remap_index },
   { 18927, ClearBufferfv_remap_index },
   { 23173, ClearBufferiv_remap_index },
   { 42446, ClearBufferuiv_remap_index },
   { 14783, ColorMaski_remap_index },
   {  6606, Disablei_remap_index },
   { 17107, Enablei_remap_index },
   { 25660, EndConditionalRender_remap_index },
   { 21881, EndTransformFeedback_remap_index },
   { 13629, GetBooleani_v_remap_index },
   { 44196, GetFragDataLocation_remap_index },
   { 23194, GetIntegeri_v_remap_index },
   { 31805, GetStringi_remap_index },
   { 33802, GetTexParameterIiv_remap_index },
   { 43572, GetTexParameterIuiv_remap_index },
   { 34016, GetTransformFeedbackVarying_remap_index },
   {  3184, GetUniformuiv_remap_index },
   { 48254, GetVertexAttribIiv_remap_index },
   { 22972, GetVertexAttribIuiv_remap_index },
   { 37503, IsEnabledi_remap_index },
   { 34466, TexParameterIiv_remap_index },
   { 18533, TexParameterIuiv_remap_index },
   { 43624, TransformFeedbackVaryings_remap_index },
   { 37882, Uniform1ui_remap_index },
   { 28701, Uniform1uiv_remap_index },
   { 27995, Uniform2ui_remap_index },
   { 14825, Uniform2uiv_remap_index },
   { 36759, Uniform3ui_remap_index },
   { 21377, Uniform3uiv_remap_index },
   { 13552, Uniform4ui_remap_index },
   { 20194, Uniform4uiv_remap_index },
   { 39461, VertexAttribI1iv_remap_index },
   { 12990, VertexAttribI1uiv_remap_index },
   {  8494, VertexAttribI4bv_remap_index },
   { 26922, VertexAttribI4sv_remap_index },
   {  9579, VertexAttribI4ubv_remap_index },
   {   451, VertexAttribI4usv_remap_index },
   { 45181, VertexAttribIPointer_remap_index },
   {  9526, PrimitiveRestartIndex_remap_index },
   { 37717, TexBuffer_remap_index },
   {   111, FramebufferTexture_remap_index },
   { 26708, GetBufferParameteri64v_remap_index },
   { 19865, GetInteger64i_v_remap_index },
   { 45521, VertexAttribDivisor_remap_index },
   { 48753, MinSampleShading_remap_index },
   { 22914, MemoryBarrierByRegion_remap_index },
   {  8182, BindProgramARB_remap_index },
   { 34676, DeleteProgramsARB_remap_index },
   { 17194, GenProgramsARB_remap_index },
   { 15940, GetProgramEnvParameterdvARB_remap_index },
   { 33140, GetProgramEnvParameterfvARB_remap_index },
   { 34853, GetProgramLocalParameterdvARB_remap_index },
   { 41998, GetProgramLocalParameterfvARB_remap_index },
   { 25275, GetProgramStringARB_remap_index },
   {  9412, GetProgramivARB_remap_index },
   { 35204, IsProgramARB_remap_index },
   { 19573, ProgramEnvParameter4dARB_remap_index },
   {  2967, ProgramEnvParameter4dvARB_remap_index },
   { 43365, ProgramEnvParameter4fARB_remap_index },
   { 27599, ProgramEnvParameter4fvARB_remap_index },
   { 25710, ProgramLocalParameter4dARB_remap_index },
   {  4486, ProgramLocalParameter4dvARB_remap_index },
   { 34262, ProgramLocalParameter4fARB_remap_index },
   { 21665, ProgramLocalParameter4fvARB_remap_index },
   { 35276, ProgramStringARB_remap_index },
   { 13588, VertexAttrib1fARB_remap_index },
   { 35712, VertexAttrib1fvARB_remap_index },
   { 24867, VertexAttrib2fARB_remap_index },
   { 15072, VertexAttrib2fvARB_remap_index },
   {   334, VertexAttrib3fARB_remap_index },
   { 29660, VertexAttrib3fvARB_remap_index },
   { 28418, VertexAttrib4fARB_remap_index },
   { 16395, VertexAttrib4fvARB_remap_index },
   { 40218, AttachObjectARB_remap_index },
   { 25319, CreateProgramObjectARB_remap_index },
   { 19001, CreateShaderObjectARB_remap_index },
   { 17603, DeleteObjectARB_remap_index },
   { 42928, DetachObjectARB_remap_index },
   { 40608, GetAttachedObjectsARB_remap_index },
   { 21955, GetHandleARB_remap_index },
   { 23089, GetInfoLogARB_remap_index },
   { 23977, GetObjectParameterfvARB_remap_index },
   { 46976, GetObjectParameterivARB_remap_index },
   {  6386, DrawArraysInstancedARB_remap_index },
   {  8361, DrawElementsInstancedARB_remap_index },
   { 15694, BindFramebuffer_remap_index },
   {  9435, BindRenderbuffer_remap_index },
   { 37936, BlitFramebuffer_remap_index },
   {  7245, CheckFramebufferStatus_remap_index },
   { 22523, DeleteFramebuffers_remap_index },
   { 41541, DeleteRenderbuffers_remap_index },
   { 35114, FramebufferRenderbuffer_remap_index },
   { 38012, FramebufferTexture1D_remap_index },
   { 26033, FramebufferTexture2D_remap_index },
   { 30342, FramebufferTexture3D_remap_index },
   { 41812, FramebufferTextureLayer_remap_index },
   { 44825, GenFramebuffers_remap_index },
   { 37382, GenRenderbuffers_remap_index },
   {  4853, GenerateMipmap_remap_index },
   {  5995, GetFramebufferAttachmentParameteriv_remap_index },
   { 48797, GetRenderbufferParameteriv_remap_index },
   {  7464, IsFramebuffer_remap_index },
   { 28783, IsRenderbuffer_remap_index },
   {   669, RenderbufferStorage_remap_index },
   { 16943, RenderbufferStorageMultisample_remap_index },
   {  5922, FlushMappedBufferRange_remap_index },
   { 34919, MapBufferRange_remap_index },
   { 14918, BindVertexArray_remap_index },
   {  1173, DeleteVertexArrays_remap_index },
   { 44399, GenVertexArrays_remap_index },
   { 43732, IsVertexArray_remap_index },
   { 14698, GetActiveUniformBlockName_remap_index },
   { 48554, GetActiveUniformBlockiv_remap_index },
   { 23514, GetActiveUniformName_remap_index },
   { 15868, GetActiveUniformsiv_remap_index },
   { 46839, GetUniformBlockIndex_remap_index },
   { 11799, GetUniformIndices_remap_index },
   { 38888, UniformBlockBinding_remap_index },
   { 47974, CopyBufferSubData_remap_index },
   { 48304, ClientWaitSync_remap_index },
   { 12866, DeleteSync_remap_index },
   { 38915, FenceSync_remap_index },
   { 42908, GetInteger64v_remap_index },
   { 45377, GetSynciv_remap_index },
   { 17683, IsSync_remap_index },
   { 37572, WaitSync_remap_index },
   { 14733, DrawElementsBaseVertex_remap_index },
   { 19207, DrawElementsInstancedBaseVertex_remap_index },
   { 42067, DrawRangeElementsBaseVertex_remap_index },
   { 46538, MultiDrawElementsBaseVertex_remap_index },
   { 27295, ProvokingVertex_remap_index },
   {  6290, GetMultisamplefv_remap_index },
   { 39568, SampleMaski_remap_index },
   {  2202, TexImage2DMultisample_remap_index },
   { 46782, TexImage3DMultisample_remap_index },
   { 25821, BlendEquationSeparateiARB_remap_index },
   { 30831, BlendEquationiARB_remap_index },
   {  4223, BlendFuncSeparateiARB_remap_index },
   { 27848, BlendFunciARB_remap_index },
   {  1866, BindFragDataLocationIndexed_remap_index },
   { 32434, GetFragDataIndex_remap_index },
   {  3166, BindSampler_remap_index },
   { 48105, DeleteSamplers_remap_index },
   { 40161, GenSamplers_remap_index },
   {  2800, GetSamplerParameterIiv_remap_index },
   {  6575, GetSamplerParameterIuiv_remap_index },
   { 26136, GetSamplerParameterfv_remap_index },
   { 28122, GetSamplerParameteriv_remap_index },
   { 29486, IsSampler_remap_index },
   {  9865, SamplerParameterIiv_remap_index },
   { 14002, SamplerParameterIuiv_remap_index },
   { 32660, SamplerParameterf_remap_index },
   { 43265, SamplerParameterfv_remap_index },
   { 48126, SamplerParameteri_remap_index },
   { 31669, SamplerParameteriv_remap_index },
   { 26223, GetQueryObjecti64v_remap_index },
   {  4546, GetQueryObjectui64v_remap_index },
   { 14514, QueryCounter_remap_index },
   { 42430, ColorP3ui_remap_index },
   {  7618, ColorP3uiv_remap_index },
   { 19965, ColorP4ui_remap_index },
   { 29228, ColorP4uiv_remap_index },
   { 15541, MultiTexCoordP1ui_remap_index },
   { 28918, MultiTexCoordP1uiv_remap_index },
   { 30648, MultiTexCoordP2ui_remap_index },
   { 10212, MultiTexCoordP2uiv_remap_index },
   { 29316, MultiTexCoordP3ui_remap_index },
   {   425, MultiTexCoordP3uiv_remap_index },
   { 45130, MultiTexCoordP4ui_remap_index },
   { 38355, MultiTexCoordP4uiv_remap_index },
   { 40858, NormalP3ui_remap_index },
   { 28843, NormalP3uiv_remap_index },
   { 46118, SecondaryColorP3ui_remap_index },
   {  6523, SecondaryColorP3uiv_remap_index },
   {   162, TexCoordP1ui_remap_index },
   {   649, TexCoordP1uiv_remap_index },
   { 29619, TexCoordP2ui_remap_index },
   { 41195, TexCoordP2uiv_remap_index },
   { 16787, TexCoordP3ui_remap_index },
   { 20035, TexCoordP3uiv_remap_index },
   { 38109, TexCoordP4ui_remap_index },
   {  1949, TexCoordP4uiv_remap_index },
   { 16887, VertexAttribP1ui_remap_index },
   {  4598, VertexAttribP1uiv_remap_index },
   { 33351, VertexAttribP2ui_remap_index },
   {  5584, VertexAttribP2uiv_remap_index },
   {  1597, VertexAttribP3ui_remap_index },
   { 31695, VertexAttribP3uiv_remap_index },
   {  4913, VertexAttribP4ui_remap_index },
   { 18330, VertexAttribP4uiv_remap_index },
   { 39054, VertexP2ui_remap_index },
   { 18087, VertexP2uiv_remap_index },
   { 25302, VertexP3ui_remap_index },
   {  6844, VertexP3uiv_remap_index },
   {  3478, VertexP4ui_remap_index },
   { 48704, VertexP4uiv_remap_index },
   {   817, DrawArraysIndirect_remap_index },
   { 26386, DrawElementsIndirect_remap_index },
   {  7384, GetUniformdv_remap_index },
   { 43895, Uniform1d_remap_index },
   { 16003, Uniform1dv_remap_index },
   { 46449, Uniform2d_remap_index },
   { 31822, Uniform2dv_remap_index },
   {   904, Uniform3d_remap_index },
   { 32787, Uniform3dv_remap_index },
   {  4972, Uniform4d_remap_index },
   { 21746, Uniform4dv_remap_index },
   {  4521, UniformMatrix2dv_remap_index },
   { 25231, UniformMatrix2x3dv_remap_index },
   { 18042, UniformMatrix2x4dv_remap_index },
   { 32685, UniformMatrix3dv_remap_index },
   {  4802, UniformMatrix3x2dv_remap_index },
   {  5610, UniformMatrix3x4dv_remap_index },
   { 18948, UniformMatrix4dv_remap_index },
   { 36437, UniformMatrix4x2dv_remap_index },
   { 20493, UniformMatrix4x3dv_remap_index },
   {  5758, GetActiveSubroutineName_remap_index },
   {  6332, GetActiveSubroutineUniformName_remap_index },
   { 17915, GetActiveSubroutineUniformiv_remap_index },
   { 13415, GetProgramStageiv_remap_index },
   { 14558, GetSubroutineIndex_remap_index },
   {  1439, GetSubroutineUniformLocation_remap_index },
   { 48722, GetUniformSubroutineuiv_remap_index },
   {  7143, UniformSubroutinesuiv_remap_index },
   { 16283, PatchParameterfv_remap_index },
   { 12433, PatchParameteri_remap_index },
   { 12269, BindTransformFeedback_remap_index },
   { 12141, DeleteTransformFeedbacks_remap_index },
   { 39776, DrawTransformFeedback_remap_index },
   {  4380, GenTransformFeedbacks_remap_index },
   { 37011, IsTransformFeedback_remap_index },
   { 34299, PauseTransformFeedback_remap_index },
   { 39396, ResumeTransformFeedback_remap_index },
   { 25161, BeginQueryIndexed_remap_index },
   { 45572, DrawTransformFeedbackStream_remap_index },
   { 21467, EndQueryIndexed_remap_index },
   { 24381, GetQueryIndexediv_remap_index },
   { 22157, ClearDepthf_remap_index },
   { 26756, DepthRangef_remap_index },
   { 42468, GetShaderPrecisionFormat_remap_index },
   {  3618, ReleaseShaderCompiler_remap_index },
   { 28260, ShaderBinary_remap_index },
   { 21973, GetProgramBinary_remap_index },
   { 13486, ProgramBinary_remap_index },
   { 13744, ProgramParameteri_remap_index },
   { 12689, GetVertexAttribLdv_remap_index },
   { 47452, VertexAttribL1d_remap_index },
   {  7441, VertexAttribL1dv_remap_index },
   { 38239, VertexAttribL2d_remap_index },
   { 21932, VertexAttribL2dv_remap_index },
   { 40684, VertexAttribL3d_remap_index },
   { 14979, VertexAttribL3dv_remap_index },
   {  8601, VertexAttribL4d_remap_index },
   { 22807, VertexAttribL4dv_remap_index },
   { 47125, VertexAttribLPointer_remap_index },
   { 30038, DepthRangeArrayv_remap_index },
   { 46604, DepthRangeIndexed_remap_index },
   { 36991, GetDoublei_v_remap_index },
   { 39586, GetFloati_v_remap_index },
   { 47042, ScissorArrayv_remap_index },
   { 28029, ScissorIndexed_remap_index },
   { 31721, ScissorIndexedv_remap_index },
   { 20908, ViewportArrayv_remap_index },
   { 35381, ViewportIndexedf_remap_index },
   { 22021, ViewportIndexedfv_remap_index },
   {  9626, GetGraphicsResetStatusARB_remap_index },
   { 33558, GetnColorTableARB_remap_index },
   {  3100, GetnCompressedTexImageARB_remap_index },
   {  1273, GetnConvolutionFilterARB_remap_index },
   {  5458, GetnHistogramARB_remap_index },
   { 26115, GetnMapdvARB_remap_index },
   { 13893, GetnMapfvARB_remap_index },
   { 38262, GetnMapivARB_remap_index },
   { 43477, GetnMinmaxARB_remap_index },
   {  4076, GetnPixelMapfvARB_remap_index },
   {  6549, GetnPixelMapuivARB_remap_index },
   { 13113, GetnPixelMapusvARB_remap_index },
   { 24775, GetnPolygonStippleARB_remap_index },
   { 32202, GetnSeparableFilterARB_remap_index },
   { 11406, GetnTexImageARB_remap_index },
   { 31142, GetnUniformdvARB_remap_index },
   { 37814, GetnUniformfvARB_remap_index },
   {  3593, GetnUniformivARB_remap_index },
   { 15366, GetnUniformuivARB_remap_index },
   { 28479, ReadnPixelsARB_remap_index },
   { 36619, DrawArraysInstancedBaseInstance_remap_index },
   { 11584, DrawElementsInstancedBaseInstance_remap_index },
   {  2912, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 39307, DrawTransformFeedbackInstanced_remap_index },
   { 15027, DrawTransformFeedbackStreamInstanced_remap_index },
   { 47063, GetInternalformativ_remap_index },
   { 22118, GetActiveAtomicCounterBufferiv_remap_index },
   { 48676, BindImageTexture_remap_index },
   { 23890, MemoryBarrier_remap_index },
   { 37915, TexStorage1D_remap_index },
   { 25975, TexStorage2D_remap_index },
   { 30319, TexStorage3D_remap_index },
   {  1520, TextureStorage1DEXT_remap_index },
   { 39224, TextureStorage2DEXT_remap_index },
   { 24836, TextureStorage3DEXT_remap_index },
   { 40036, ClearBufferData_remap_index },
   {  2370, ClearBufferSubData_remap_index },
   { 34962, DispatchCompute_remap_index },
   {  7329, DispatchComputeIndirect_remap_index },
   { 40076, CopyImageSubData_remap_index },
   { 45396, TextureView_remap_index },
   { 24250, BindVertexBuffer_remap_index },
   { 32960, VertexAttribBinding_remap_index },
   { 33585, VertexAttribFormat_remap_index },
   { 36560, VertexAttribIFormat_remap_index },
   { 40656, VertexAttribLFormat_remap_index },
   { 38824, VertexBindingDivisor_remap_index },
   { 36082, FramebufferParameteri_remap_index },
   { 31326, GetFramebufferParameteriv_remap_index },
   { 42035, MultiDrawArraysIndirect_remap_index },
   { 20458, MultiDrawElementsIndirect_remap_index },
   { 34173, GetProgramInterfaceiv_remap_index },
   {  3495, GetProgramResourceIndex_remap_index },
   {  1405, GetProgramResourceLocation_remap_index },
   {  1711, GetProgramResourceLocationIndex_remap_index },
   { 15145, GetProgramResourceName_remap_index },
   { 46019, GetProgramResourceiv_remap_index },
   { 40521, ShaderStorageBlockBinding_remap_index },
   { 20393, TexBufferRange_remap_index },
   { 42681, TexStorage2DMultisample_remap_index },
   { 31585, TexStorage3DMultisample_remap_index },
   {  3790, BufferStorage_remap_index },
   { 43196, ClearTexImage_remap_index },
   { 14666, ClearTexSubImage_remap_index },
   {  4693, BindBuffersBase_remap_index },
   { 16368, BindBuffersRange_remap_index },
   { 12116, BindImageTextures_remap_index },
   {  3080, BindSamplers_remap_index },
   { 46143, BindTextures_remap_index },
   { 28095, BindVertexBuffers_remap_index },
   { 40179, ClipControl_remap_index },
   { 22045, BindTextureUnit_remap_index },
   { 44013, BlitNamedFramebuffer_remap_index },
   {  7077, CheckNamedFramebufferStatus_remap_index },
   { 25186, ClearNamedBufferData_remap_index },
   { 44266, ClearNamedBufferSubData_remap_index },
   { 27904, ClearNamedFramebufferfi_remap_index },
   { 27936, ClearNamedFramebufferfv_remap_index },
   {  9017, ClearNamedFramebufferiv_remap_index },
   { 40708, ClearNamedFramebufferuiv_remap_index },
   { 43291, CompressedTextureSubImage1D_remap_index },
   { 38931, CompressedTextureSubImage2D_remap_index },
   { 36660, CompressedTextureSubImage3D_remap_index },
   {  3134, CopyNamedBufferSubData_remap_index },
   { 39345, CopyTextureSubImage1D_remap_index },
   { 35301, CopyTextureSubImage2D_remap_index },
   { 45323, CopyTextureSubImage3D_remap_index },
   {  6246, CreateBuffers_remap_index },
   { 30995, CreateFramebuffers_remap_index },
   {  1113, CreateProgramPipelines_remap_index },
   { 34735, CreateQueries_remap_index },
   {  9656, CreateRenderbuffers_remap_index },
   { 40875, CreateSamplers_remap_index },
   { 31883, CreateTextures_remap_index },
   {  1750, CreateTransformFeedbacks_remap_index },
   { 23288, CreateVertexArrays_remap_index },
   {  8570, DisableVertexArrayAttrib_remap_index },
   { 44956, EnableVertexArrayAttrib_remap_index },
   { 15506, FlushMappedNamedBufferRange_remap_index },
   { 34649, GenerateTextureMipmap_remap_index },
   {   391, GetCompressedTextureImage_remap_index },
   {  5051, GetNamedBufferParameteri64v_remap_index },
   { 25788, GetNamedBufferParameteriv_remap_index },
   { 31386, GetNamedBufferPointerv_remap_index },
   { 12504, GetNamedBufferSubData_remap_index },
   {  7002, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 46938, GetNamedFramebufferParameteriv_remap_index },
   { 33261, GetNamedRenderbufferParameteriv_remap_index },
   { 27424, GetQueryBufferObjecti64v_remap_index },
   { 47658, GetQueryBufferObjectiv_remap_index },
   {  9986, GetQueryBufferObjectui64v_remap_index },
   {  3224, GetQueryBufferObjectuiv_remap_index },
   {  3300, GetTextureImage_remap_index },
   { 36335, GetTextureLevelParameterfv_remap_index },
   { 39120, GetTextureLevelParameteriv_remap_index },
   { 15664, GetTextureParameterIiv_remap_index },
   { 24113, GetTextureParameterIuiv_remap_index },
   { 26532, GetTextureParameterfv_remap_index },
   { 29991, GetTextureParameteriv_remap_index },
   { 17019, GetTransformFeedbacki64_v_remap_index },
   {  4031, GetTransformFeedbacki_v_remap_index },
   { 27139, GetTransformFeedbackiv_remap_index },
   {  6164, GetVertexArrayIndexed64iv_remap_index },
   { 34434, GetVertexArrayIndexediv_remap_index },
   { 17659, GetVertexArrayiv_remap_index },
   { 33520, InvalidateNamedFramebufferData_remap_index },
   { 29183, InvalidateNamedFramebufferSubData_remap_index },
   { 10496, MapNamedBuffer_remap_index },
   { 13341, MapNamedBufferRange_remap_index },
   { 47207, NamedBufferData_remap_index },
   { 11950, NamedBufferStorage_remap_index },
   { 20366, NamedBufferSubData_remap_index },
   { 11977, NamedFramebufferDrawBuffer_remap_index },
   { 38506, NamedFramebufferDrawBuffers_remap_index },
   { 28061, NamedFramebufferParameteri_remap_index },
   { 23128, NamedFramebufferReadBuffer_remap_index },
   { 34344, NamedFramebufferRenderbuffer_remap_index },
   {  5086, NamedFramebufferTexture_remap_index },
   { 12010, NamedFramebufferTextureLayer_remap_index },
   {  9379, NamedRenderbufferStorage_remap_index },
   { 29517, NamedRenderbufferStorageMultisample_remap_index },
   { 23731, TextureBuffer_remap_index },
   { 12363, TextureBufferRange_remap_index },
   { 39254, TextureParameterIiv_remap_index },
   { 30180, TextureParameterIuiv_remap_index },
   { 37987, TextureParameterf_remap_index },
   {  2400, TextureParameterfv_remap_index },
   { 38068, TextureParameteri_remap_index },
   { 26791, TextureParameteriv_remap_index },
   { 11197, TextureStorage1D_remap_index },
   { 15638, TextureStorage2D_remap_index },
   { 44524, TextureStorage2DMultisample_remap_index },
   { 19981, TextureStorage3D_remap_index },
   {  3554, TextureStorage3DMultisample_remap_index },
   { 28359, TextureSubImage1D_remap_index },
   { 32986, TextureSubImage2D_remap_index },
   { 36244, TextureSubImage3D_remap_index },
   { 20520, TransformFeedbackBufferBase_remap_index },
   { 16057, TransformFeedbackBufferRange_remap_index },
   {  4780, UnmapNamedBuffer_remap_index },
   { 27056, VertexArrayAttribBinding_remap_index },
   { 20055, VertexArrayAttribFormat_remap_index },
   { 16511, VertexArrayAttribIFormat_remap_index },
   {  2830, VertexArrayAttribLFormat_remap_index },
   { 18140, VertexArrayBindingDivisor_remap_index },
   { 16806, VertexArrayElementBuffer_remap_index },
   { 45944, VertexArrayVertexBuffer_remap_index },
   { 19172, VertexArrayVertexBuffers_remap_index },
   {  2142, GetCompressedTextureSubImage_remap_index },
   {  7662, GetTextureSubImage_remap_index },
   {  7358, InvalidateBufferData_remap_index },
   { 43138, InvalidateBufferSubData_remap_index },
   { 23850, InvalidateFramebuffer_remap_index },
   { 17989, InvalidateSubFramebuffer_remap_index },
   { 13527, InvalidateTexImage_remap_index },
   { 28506, InvalidateTexSubImage_remap_index },
   { 14266, PolygonOffsetEXT_remap_index },
   { 40197, DrawTexfOES_remap_index },
   { 27830, DrawTexfvOES_remap_index },
   {  1019, DrawTexiOES_remap_index },
   { 33456, DrawTexivOES_remap_index },
   { 13674, DrawTexsOES_remap_index },
   { 24044, DrawTexsvOES_remap_index },
   { 29245, DrawTexxOES_remap_index },
   { 42296, DrawTexxvOES_remap_index },
   { 27186, PointSizePointerOES_remap_index },
   {   982, QueryMatrixxOES_remap_index },
   { 21531, SampleMaskSGIS_remap_index },
   { 36717, SamplePatternSGIS_remap_index },
   { 46466, ColorPointerEXT_remap_index },
   { 30725, EdgeFlagPointerEXT_remap_index },
   { 14343, IndexPointerEXT_remap_index },
   { 14533, NormalPointerEXT_remap_index },
   { 30103, TexCoordPointerEXT_remap_index },
   { 26896, VertexPointerEXT_remap_index },
   { 46388, DiscardFramebufferEXT_remap_index },
   { 12065, ActiveShaderProgram_remap_index },
   { 18280, BindProgramPipeline_remap_index },
   { 30777, CreateShaderProgramv_remap_index },
   {  3942, DeleteProgramPipelines_remap_index },
   { 28151, GenProgramPipelines_remap_index },
   {  9083, GetProgramPipelineInfoLog_remap_index },
   { 33661, GetProgramPipelineiv_remap_index },
   { 28214, IsProgramPipeline_remap_index },
   { 48534, LockArraysEXT_remap_index },
   { 47832, ProgramUniform1d_remap_index },
   { 33114, ProgramUniform1dv_remap_index },
   { 47786, ProgramUniform1f_remap_index },
   { 10673, ProgramUniform1fv_remap_index },
   { 47740, ProgramUniform1i_remap_index },
   { 16642, ProgramUniform1iv_remap_index },
   { 37167, ProgramUniform1ui_remap_index },
   { 47557, ProgramUniform1uiv_remap_index },
   {  2582, ProgramUniform2d_remap_index },
   { 10602, ProgramUniform2dv_remap_index },
   {  2535, ProgramUniform2f_remap_index },
   { 39071, ProgramUniform2fv_remap_index },
   {  2607, ProgramUniform2i_remap_index },
   { 23239, ProgramUniform2iv_remap_index },
   {  8032, ProgramUniform2ui_remap_index },
   { 10020, ProgramUniform2uiv_remap_index },
   {  5118, ProgramUniform3d_remap_index },
   {  5025, ProgramUniform3dv_remap_index },
   {  5144, ProgramUniform3f_remap_index },
   { 32483, ProgramUniform3fv_remap_index },
   {  5192, ProgramUniform3i_remap_index },
   { 14584, ProgramUniform3iv_remap_index },
   { 16691, ProgramUniform3ui_remap_index },
   { 19717, ProgramUniform3uiv_remap_index },
   { 31201, ProgramUniform4d_remap_index },
   { 33376, ProgramUniform4dv_remap_index },
   { 31228, ProgramUniform4f_remap_index },
   { 45237, ProgramUniform4fv_remap_index },
   { 31277, ProgramUniform4i_remap_index },
   {  2063, ProgramUniform4iv_remap_index },
   { 43813, ProgramUniform4ui_remap_index },
   { 35887, ProgramUniform4uiv_remap_index },
   { 14633, ProgramUniformMatrix2dv_remap_index },
   { 21603, ProgramUniformMatrix2fv_remap_index },
   { 17624, ProgramUniformMatrix2x3dv_remap_index },
   { 24184, ProgramUniformMatrix2x3fv_remap_index },
   {  1995, ProgramUniformMatrix2x4dv_remap_index },
   {  8733, ProgramUniformMatrix2x4fv_remap_index },
   { 48501, ProgramUniformMatrix3dv_remap_index },
   { 41919, ProgramUniformMatrix3fv_remap_index },
   { 29044, ProgramUniformMatrix3x2dv_remap_index },
   { 36925, ProgramUniformMatrix3x2fv_remap_index },
   { 24655, ProgramUniformMatrix3x4dv_remap_index },
   { 29703, ProgramUniformMatrix3x4fv_remap_index },
   { 42106, ProgramUniformMatrix4dv_remap_index },
   { 35031, ProgramUniformMatrix4fv_remap_index },
   { 43330, ProgramUniformMatrix4x2dv_remap_index },
   {  2469, ProgramUniformMatrix4x2fv_remap_index },
   { 15220, ProgramUniformMatrix4x3dv_remap_index },
   {  8275, ProgramUniformMatrix4x3fv_remap_index },
   { 42715, UnlockArraysEXT_remap_index },
   { 34985, UseProgramStages_remap_index },
   {  1808, ValidateProgramPipeline_remap_index },
   { 18356, DebugMessageCallback_remap_index },
   { 35474, DebugMessageControl_remap_index },
   { 17438, DebugMessageInsert_remap_index },
   {  7817, GetDebugMessageLog_remap_index },
   {  7574, GetObjectLabel_remap_index },
   { 13695, GetObjectPtrLabel_remap_index },
   {  6760, ObjectLabel_remap_index },
   { 48621, ObjectPtrLabel_remap_index },
   { 20230, PopDebugGroup_remap_index },
   { 16095, PushDebugGroup_remap_index },
   {  9480, SecondaryColor3fEXT_remap_index },
   {  8929, SecondaryColor3fvEXT_remap_index },
   { 32152, MultiDrawElementsEXT_remap_index },
   { 12197, FogCoordfEXT_remap_index },
   { 20555, FogCoordfvEXT_remap_index },
   {  4671, ResizeBuffersMESA_remap_index },
   { 38171, WindowPos4dMESA_remap_index },
   { 30626, WindowPos4dvMESA_remap_index },
   {  4829, WindowPos4fMESA_remap_index },
   { 12882, WindowPos4fvMESA_remap_index },
   { 10309, WindowPos4iMESA_remap_index },
   {  4149, WindowPos4ivMESA_remap_index },
   { 31460, WindowPos4sMESA_remap_index },
   {  6738, WindowPos4svMESA_remap_index },
   { 32252, MultiModeDrawArraysIBM_remap_index },
   { 22296, MultiModeDrawElementsIBM_remap_index },
   { 36836, AreProgramsResidentNV_remap_index },
   { 44652, ExecuteProgramNV_remap_index },
   { 33082, GetProgramParameterdvNV_remap_index },
   { 40941, GetProgramParameterfvNV_remap_index },
   { 21700, GetProgramStringNV_remap_index },
   { 17875, GetProgramivNV_remap_index },
   { 20838, GetTrackMatrixivNV_remap_index },
   { 21280, GetVertexAttribdvNV_remap_index },
   { 19344, GetVertexAttribfvNV_remap_index },
   { 18173, GetVertexAttribivNV_remap_index },
   { 40586, LoadProgramNV_remap_index },
   { 22396, ProgramParameters4dvNV_remap_index },
   { 23058, ProgramParameters4fvNV_remap_index },
   {  7111, RequestResidentProgramsNV_remap_index },
   { 32638, TrackMatrixNV_remap_index },
   { 48925, VertexAttrib1dNV_remap_index },
   { 31528, VertexAttrib1dvNV_remap_index },
   { 31923, VertexAttrib1fNV_remap_index },
   { 46815, VertexAttrib1fvNV_remap_index },
   { 23688, VertexAttrib1sNV_remap_index },
   { 42735, VertexAttrib1svNV_remap_index },
   { 20814, VertexAttrib2dNV_remap_index },
   { 38482, VertexAttrib2dvNV_remap_index },
   { 30510, VertexAttrib2fNV_remap_index },
   { 29079, VertexAttrib2fvNV_remap_index },
   { 14439, VertexAttrib2sNV_remap_index },
   {  6499, VertexAttrib2svNV_remap_index },
   { 41350, VertexAttrib3dNV_remap_index },
   { 43439, VertexAttrib3dvNV_remap_index },
   {  5637, VertexAttrib3fNV_remap_index },
   { 46052, VertexAttrib3fvNV_remap_index },
   {  8219, VertexAttrib3sNV_remap_index },
   { 20865, VertexAttrib3svNV_remap_index },
   {  9708, VertexAttrib4dNV_remap_index },
   {  3867, VertexAttrib4dvNV_remap_index },
   {  9779, VertexAttrib4fNV_remap_index },
   { 46190, VertexAttrib4fvNV_remap_index },
   { 19823, VertexAttrib4sNV_remap_index },
   { 12409, VertexAttrib4svNV_remap_index },
   {  1781, VertexAttrib4ubNV_remap_index },
   { 12172, VertexAttrib4ubvNV_remap_index },
   { 32403, VertexAttribPointerNV_remap_index },
   { 30673, VertexAttribs1dvNV_remap_index },
   { 45155, VertexAttribs1fvNV_remap_index },
   {  7051, VertexAttribs1svNV_remap_index },
   { 47531, VertexAttribs2dvNV_remap_index },
   {  8661, VertexAttribs2fvNV_remap_index },
   { 30751, VertexAttribs2svNV_remap_index },
   {  1969, VertexAttribs3dvNV_remap_index },
   { 39804, VertexAttribs3fvNV_remap_index },
   { 15842, VertexAttribs3svNV_remap_index },
   { 27804, VertexAttribs4dvNV_remap_index },
   { 27337, VertexAttribs4fvNV_remap_index },
   { 11558, VertexAttribs4svNV_remap_index },
   { 36111, VertexAttribs4ubvNV_remap_index },
   { 46693, GetTexBumpParameterfvATI_remap_index },
   { 11825, GetTexBumpParameterivATI_remap_index },
   { 39904, TexBumpParameterfvATI_remap_index },
   {  9751, TexBumpParameterivATI_remap_index },
   { 10530, AlphaFragmentOp1ATI_remap_index },
   {  3891, AlphaFragmentOp2ATI_remap_index },
   { 11012, AlphaFragmentOp3ATI_remap_index },
   { 37355, BeginFragmentShaderATI_remap_index },
   {  4196, BindFragmentShaderATI_remap_index },
   {  8244, ColorFragmentOp1ATI_remap_index },
   { 14405, ColorFragmentOp2ATI_remap_index },
   { 26817, ColorFragmentOp3ATI_remap_index },
   { 19143, DeleteFragmentShaderATI_remap_index },
   { 49068, EndFragmentShaderATI_remap_index },
   { 25761, GenFragmentShadersATI_remap_index },
   { 46272, PassTexCoordATI_remap_index },
   { 39884, SampleMapATI_remap_index },
   { 39019, SetFragmentShaderConstantATI_remap_index },
   {  9682, ActiveStencilFaceEXT_remap_index },
   {  9249, BindVertexArrayAPPLE_remap_index },
   { 18648, GenVertexArraysAPPLE_remap_index },
   { 39424, GetProgramNamedParameterdvNV_remap_index },
   { 25511, GetProgramNamedParameterfvNV_remap_index },
   { 46724, ProgramNamedParameter4dNV_remap_index },
   { 42501, ProgramNamedParameter4dvNV_remap_index },
   { 27517, ProgramNamedParameter4fNV_remap_index },
   { 28606, ProgramNamedParameter4fvNV_remap_index },
   { 27244, PrimitiveRestartNV_remap_index },
   { 27782, GetTexGenxvOES_remap_index },
   { 16493, TexGenxOES_remap_index },
   { 36316, TexGenxvOES_remap_index },
   {  9173, DepthBoundsEXT_remap_index },
   { 42622, BindFramebufferEXT_remap_index },
   { 46330, BindRenderbufferEXT_remap_index },
   { 34890, BufferParameteriAPPLE_remap_index },
   { 44161, FlushMappedBufferRangeAPPLE_remap_index },
   { 31020, VertexAttribI1iEXT_remap_index },
   { 12821, VertexAttribI1uiEXT_remap_index },
   { 22729, VertexAttribI2iEXT_remap_index },
   { 45795, VertexAttribI2ivEXT_remap_index },
   { 28737, VertexAttribI2uiEXT_remap_index },
   { 39521, VertexAttribI2uivEXT_remap_index },
   { 21799, VertexAttribI3iEXT_remap_index },
   { 48060, VertexAttribI3ivEXT_remap_index },
   { 25443, VertexAttribI3uiEXT_remap_index },
   { 23592, VertexAttribI3uivEXT_remap_index },
   { 42546, VertexAttribI4iEXT_remap_index },
   {  7724, VertexAttribI4ivEXT_remap_index },
   {  2864, VertexAttribI4uiEXT_remap_index },
   { 30898, VertexAttribI4uivEXT_remap_index },
   {  3400, ClearColorIiEXT_remap_index },
   {  1248, ClearColorIuiEXT_remap_index },
   { 27267, BindBufferOffsetEXT_remap_index },
   { 20587, BeginPerfMonitorAMD_remap_index },
   { 36794, DeletePerfMonitorsAMD_remap_index },
   {  6198, EndPerfMonitorAMD_remap_index },
   { 30242, GenPerfMonitorsAMD_remap_index },
   { 14176, GetPerfMonitorCounterDataAMD_remap_index },
   { 38851, GetPerfMonitorCounterInfoAMD_remap_index },
   { 49028, GetPerfMonitorCounterStringAMD_remap_index },
   { 48586, GetPerfMonitorCountersAMD_remap_index },
   { 16186, GetPerfMonitorGroupStringAMD_remap_index },
   { 33300, GetPerfMonitorGroupsAMD_remap_index },
   { 15600, SelectPerfMonitorCountersAMD_remap_index },
   { 16306, GetObjectParameterivAPPLE_remap_index },
   { 48394, ObjectPurgeableAPPLE_remap_index },
   {  2112, ObjectUnpurgeableAPPLE_remap_index },
   { 47297, ActiveProgramEXT_remap_index },
   { 38210, CreateShaderProgramEXT_remap_index },
   { 41458, UseShaderProgramEXT_remap_index },
   { 34135, TextureBarrierNV_remap_index },
   {  2426, VDPAUFiniNV_remap_index },
   {   875, VDPAUGetSurfaceivNV_remap_index },
   { 26690, VDPAUInitNV_remap_index },
   { 24162, VDPAUIsSurfaceNV_remap_index },
   { 24356, VDPAUMapSurfacesNV_remap_index },
   {  3441, VDPAURegisterOutputSurfaceNV_remap_index },
   { 14074, VDPAURegisterVideoSurfaceNV_remap_index },
   { 12534, VDPAUSurfaceAccessNV_remap_index },
   {  5431, VDPAUUnmapSurfacesNV_remap_index },
   { 42400, VDPAUUnregisterSurfaceNV_remap_index },
   { 10238, BeginPerfQueryINTEL_remap_index },
   { 37676, CreatePerfQueryINTEL_remap_index },
   { 18579, DeletePerfQueryINTEL_remap_index },
   { 45625, EndPerfQueryINTEL_remap_index },
   { 47267, GetFirstPerfQueryIdINTEL_remap_index },
   { 34088, GetNextPerfQueryIdINTEL_remap_index },
   { 35938, GetPerfCounterInfoINTEL_remap_index },
   {   786, GetPerfQueryDataINTEL_remap_index },
   { 25041, GetPerfQueryIdByNameINTEL_remap_index },
   { 22490, GetPerfQueryInfoINTEL_remap_index },
   { 42879, PolygonOffsetClampEXT_remap_index },
   { 22941, StencilFuncSeparateATI_remap_index },
   {  6464, ProgramEnvParameters4fvEXT_remap_index },
   {  7404, ProgramLocalParameters4fvEXT_remap_index },
   {  4337, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  4101, EGLImageTargetTexture2DOES_remap_index },
   { 45762, AlphaFuncx_remap_index },
   { 21844, ClearColorx_remap_index },
   { 44922, ClearDepthx_remap_index },
   { 40303, Color4x_remap_index },
   { 26628, DepthRangex_remap_index },
   {  2654, Fogx_remap_index },
   { 16837, Fogxv_remap_index },
   {  9953, Frustumf_remap_index },
   { 10084, Frustumx_remap_index },
   { 21764, LightModelx_remap_index },
   { 36156, LightModelxv_remap_index },
   { 32457, Lightx_remap_index },
   { 46576, Lightxv_remap_index },
   {  3999, LineWidthx_remap_index },
   {  3756, LoadMatrixx_remap_index },
   { 47626, Materialx_remap_index },
   { 28556, Materialxv_remap_index },
   { 48179, MultMatrixx_remap_index },
   { 11475, MultiTexCoord4x_remap_index },
   { 28388, Normal3x_remap_index },
   { 17515, Orthof_remap_index },
   { 17755, Orthox_remap_index },
   { 30945, PointSizex_remap_index },
   {    70, PolygonOffsetx_remap_index },
   { 41783, Rotatex_remap_index },
   { 22447, SampleCoveragex_remap_index },
   { 14289, Scalex_remap_index },
   { 42993, TexEnvx_remap_index },
   { 48345, TexEnvxv_remap_index },
   {  2234, TexParameterx_remap_index },
   { 35640, Translatex_remap_index },
   { 37134, ClipPlanef_remap_index },
   { 37036, ClipPlanex_remap_index },
   {   747, GetClipPlanef_remap_index },
   {   610, GetClipPlanex_remap_index },
   { 22365, GetFixedv_remap_index },
   {  1307, GetLightxv_remap_index },
   { 25548, GetMaterialxv_remap_index },
   { 24008, GetTexEnvxv_remap_index },
   { 19028, GetTexParameterxv_remap_index },
   { 32710, PointParameterx_remap_index },
   { 41738, PointParameterxv_remap_index },
   { 21489, TexParameterxv_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   /* from GL_EXT_blend_color */
   { 38430, _gloffset_BlendColor },
   /* from GL_EXT_blend_minmax */
   { 41293, _gloffset_BlendEquation },
   /* from GL_EXT_color_subtable */
   {  6121, _gloffset_ColorSubTable },
   { 23909, _gloffset_CopyColorSubTable },
   /* from GL_EXT_convolution */
   {  1341, _gloffset_GetConvolutionParameteriv },
   { 15408, _gloffset_ConvolutionParameterfv },
   { 18865, _gloffset_CopyConvolutionFilter1D },
   { 21001, _gloffset_SeparableFilter2D },
   { 22241, _gloffset_GetConvolutionFilter },
   { 26331, _gloffset_ConvolutionFilter1D },
   { 28944, _gloffset_ConvolutionFilter2D },
   { 32010, _gloffset_GetSeparableFilter },
   { 33746, _gloffset_ConvolutionParameteri },
   { 33868, _gloffset_ConvolutionParameterf },
   { 40332, _gloffset_ConvolutionParameteriv },
   { 46629, _gloffset_GetConvolutionParameterfv },
   { 47911, _gloffset_CopyConvolutionFilter2D },
   /* from GL_EXT_copy_texture */
   { 30564, _gloffset_CopyTexImage2D },
   { 33474, _gloffset_CopyTexImage1D },
   { 36193, _gloffset_CopyTexSubImage1D },
   { 42802, _gloffset_CopyTexSubImage3D },
   { 46885, _gloffset_CopyTexSubImage2D },
   /* from GL_EXT_draw_range_elements */
   { 27731, _gloffset_DrawRangeElements },
   /* from GL_EXT_histogram */
   {  5330, _gloffset_GetHistogramParameterfv },
   {  9275, _gloffset_GetHistogramParameteriv },
   { 10796, _gloffset_Minmax },
   { 15782, _gloffset_GetMinmax },
   { 24558, _gloffset_Histogram },
   { 33402, _gloffset_GetMinmaxParameteriv },
   { 34547, _gloffset_ResetMinmax },
   { 35600, _gloffset_GetHistogram },
   { 37449, _gloffset_GetMinmaxParameterfv },
   { 38541, _gloffset_ResetHistogram },
   /* from GL_EXT_paletted_texture */
   { 15270, _gloffset_ColorTable },
   { 20612, _gloffset_GetColorTableParameterfv },
   { 29833, _gloffset_GetColorTable },
   { 33924, _gloffset_GetColorTableParameteriv },
   /* from GL_EXT_subtexture */
   {  2686, _gloffset_TexSubImage1D },
   { 41008, _gloffset_TexSubImage2D },
   /* from GL_EXT_texture3D */
   { 24984, _gloffset_TexImage3D },
   { 45436, _gloffset_TexSubImage3D },
   /* from GL_EXT_texture_object */
   {  4717, _gloffset_GenTextures },
   { 10131, _gloffset_BindTexture },
   { 19687, _gloffset_IsTexture },
   { 25393, _gloffset_PrioritizeTextures },
   { 30062, _gloffset_DeleteTextures },
   { 45892, _gloffset_AreTexturesResident },
   /* from GL_EXT_vertex_array */
   { 21099, _gloffset_ArrayElement },
   { 32753, _gloffset_DrawArrays },
   { 43522, _gloffset_GetPointerv },
   /* from GL_NV_read_buffer */
   { 33715, _gloffset_ReadBuffer },
   /* from GL_OES_blend_subtract */
   { 41293, _gloffset_BlendEquation },
   /* from GL_OES_texture_3D */
   { 24984, _gloffset_TexImage3D },
   { 42802, _gloffset_CopyTexSubImage3D },
   { 45436, _gloffset_TexSubImage3D },
   /* from GL_OES_texture_cube_map */
   { 19273, _gloffset_TexGeni },
   { 19301, _gloffset_TexGenf },
   { 22830, _gloffset_GetTexGenfv },
   { 38381, _gloffset_TexGeniv },
   { 41117, _gloffset_TexGenfv },
   { 47231, _gloffset_GetTexGeniv },
   /* from GL_SGI_color_table */
   {  3024, _gloffset_ColorTableParameteriv },
   { 15270, _gloffset_ColorTable },
   { 19631, _gloffset_ColorTableParameterfv },
   { 20612, _gloffset_GetColorTableParameterfv },
   { 29833, _gloffset_GetColorTable },
   { 29911, _gloffset_CopyColorTable },
   { 33924, _gloffset_GetColorTableParameteriv },
   {    -1, -1 }
};

