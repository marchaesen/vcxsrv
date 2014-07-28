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
   /* _mesa_function_pool[391]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[417]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[458]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[473]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[512]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[532]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[570]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[609]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[629]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[707]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[746]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[777]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[802]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[835]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[864]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[889]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[922]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[949]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[971]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[986]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1007]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1024]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1080]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[1102]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1133]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysAPPLE\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1208]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1233]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1267]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1301]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1365]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1410]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1439]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1487]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1512]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1543]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1558]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1589]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1601]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1628]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[1686]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "\0"
   /* _mesa_function_pool[1722]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[1741]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[1769]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[1789]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[1815]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "\0"
   /* _mesa_function_pool[1848]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[1897]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[1927]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[1944]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[1976]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2016]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2032]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2053]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2074]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2112]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2142]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2158]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2185]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2251]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2298]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[2334]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2381]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2402]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2413]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[2457]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[2472]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2483]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[2512]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[2527]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[2557]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[2605]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "\0"
   /* _mesa_function_pool[2660]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[2717]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[2773]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[2793]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[2827]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[2845]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[2889]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[2909]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[2954]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[2965]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[2983]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3007]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3024]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3061]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3078]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[3106]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[3136]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "\0"
   /* _mesa_function_pool[3161]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[3187]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[3283]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[3299]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[3331]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "\0"
   /* _mesa_function_pool[3353]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[3369]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[3392]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[3408]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[3432]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[3465]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[3483]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[3540]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[3572]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[3585]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[3610]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[3643]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[3658]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[3680]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[3705]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[3732]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "\0"
   /* _mesa_function_pool[3793]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[3825]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[3868]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[3896]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[3918]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[3974]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[4009]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[4026]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[4078]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[4104]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[4119]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4151]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[4173]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[4197]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[4232]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4260]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[4284]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[4310]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[4335]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[4369]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[4398]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[4432]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[4480]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[4528]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[4618]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[4678]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[4693]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[4719]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[4746]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[4773]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[4826]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[4872]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[4898]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[4925]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[4950]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[4996]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[5019]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[5046]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[5092]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5118]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[5151]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5176]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[5234]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[5249]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[5375]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[5418]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[5441]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[5474]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[5498]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[5522]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[5540]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[5553]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[5631]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[5666]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[5690]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[5716]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[5742]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[5773]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[5818]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[5915]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[5940]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[5954]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "\0"
   /* _mesa_function_pool[5987]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[6005]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[6054]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[6072]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[6127]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[6145]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[6171]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[6203]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[6230]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[6276]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[6360]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[6389]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[6415]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[6440]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[6497]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[6533]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[6550]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "\0"
   /* _mesa_function_pool[6574]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[6595]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[6612]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[6639]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[6666]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[6711]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[6759]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "\0"
   /* _mesa_function_pool[6814]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[6853]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[6877]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[6924]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[6956]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[7005]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7034]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7061]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[7106]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[7143]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[7168]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[7199]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[7265]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[7285]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[7370]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "\0"
   /* _mesa_function_pool[7418]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[7463]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[7494]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[7515]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[7529]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[7589]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[7635]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[7701]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "\0"
   /* _mesa_function_pool[7747]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[7789]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[7835]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[7877]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[7911]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[7976]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[8001]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[8022]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[8077]: BindVertexArrayAPPLE (will be remapped) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[8103]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[8163]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[8207]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[8230]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[8275]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[8321]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[8374]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[8421]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "\0"
   /* _mesa_function_pool[8451]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[8477]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[8503]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[8520]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[8548]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[8574]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[8608]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[8634]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[8661]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[8694]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[8722]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[8755]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[8806]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[8819]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[8852]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[8866]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[8901]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[8947]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[8973]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[8999]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[9020]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[9045]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[9069]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[9093]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[9136]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[9163]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[9211]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[9232]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[9245]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[9275]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[9301]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[9317]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[9362]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[9411]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[9456]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[9473]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[9485]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[9511]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[9556]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[9578]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[9620]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[9645]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[9660]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[9701]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[9737]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[9748]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[9766]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[9813]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[9831]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[9856]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[9882]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[9921]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[9950]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[9995]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[10040]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[10066]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[10084]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[10109]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[10146]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[10191]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[10235]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[10282]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[10309]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[10356]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[10406]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[10432]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[10463]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[10484]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[10512]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[10557]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[10574]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[10625]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[10650]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[10681]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[10706]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[10736]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[10752]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[10778]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[10806]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[10852]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[10872]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[10890]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[10914]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[10963]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[10990]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[11012]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[11065]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[11118]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[11171]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[11224]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[11269]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[11285]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[11307]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[11393]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[11440]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[11486]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[11516]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[11542]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[11585]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[11612]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[11633]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "\0"
   /* _mesa_function_pool[11667]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[11710]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[11744]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[11790]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[11835]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[11876]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[11901]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[11937]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[11978]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[12023]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[12044]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[12070]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteriARB\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[12141]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[12179]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[12193]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[12219]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[12240]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[12284]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[12328]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[12356]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[12400]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[12436]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12489]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[12531]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[12569]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[12614]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[12666]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[12689]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[12715]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[12729]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[12743]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[12767]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[12805]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[12839]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[12863]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[12885]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[12914]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "\0"
   /* _mesa_function_pool[12933]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[12958]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[13007]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[13039]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[13074]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[13106]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[13124]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "\0"
   /* _mesa_function_pool[13166]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[13202]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[13221]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[13240]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[13259]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[13301]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[13320]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[13372]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[13417]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[13460]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[13490]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13532]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[13547]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[13600]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[13614]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13643]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "\0"
   /* _mesa_function_pool[13669]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[13685]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[13743]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[13764]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[13783]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[13808]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[13827]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[13842]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[13880]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[13923]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[13941]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[13975]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[14001]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[14027]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[14056]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[14099]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[14134]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[14157]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[14205]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[14242]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[14255]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[14272]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[14302]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[14336]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[14364]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[14391]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[14434]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[14489]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[14507]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[14528]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[14566]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[14583]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[14604]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[14653]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[14703]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[14749]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[14768]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[14791]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[14818]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[14843]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[14861]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[14874]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[14950]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[15004]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "\0"
   /* _mesa_function_pool[15037]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[15091]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[15128]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[15146]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[15200]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[15245]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[15262]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[15316]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[15335]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "\0"
   /* _mesa_function_pool[15388]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[15417]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[15476]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[15497]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[15509]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[15524]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[15569]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[15598]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[15614]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[15632]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[15648]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15689]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[15711]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[15729]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[15747]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[15765]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[15801]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[15818]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[15836]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[15871]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[15898]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[15933]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[15978]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[16028]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[16054]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "\0"
   /* _mesa_function_pool[16107]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[16150]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[16205]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[16251]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[16277]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[16320]: GenVertexArraysAPPLE (will be remapped) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[16347]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[16390]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[16446]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[16484]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[16503]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[16537]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[16599]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[16620]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[16648]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[16675]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[16723]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[16750]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[16772]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[16790]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[16819]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[16838]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "\0"
   /* _mesa_function_pool[16880]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[16904]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[16932]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[16960]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[16975]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[17002]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[17031]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[17045]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[17084]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[17136]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[17188]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[17204]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[17262]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[17318]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[17348]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[17399]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[17448]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[17483]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[17509]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[17525]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[17568]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17617]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[17645]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[17661]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[17688]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[17708]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[17727]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[17744]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[17770]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[17798]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[17832]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[17868]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "\0"
   /* _mesa_function_pool[17886]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[17927]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[17985]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "\0"
   /* _mesa_function_pool[18009]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[18050]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[18085]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[18117]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[18142]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[18234]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[18279]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[18310]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[18325]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[18346]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[18370]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[18397]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[18421]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[18440]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "\0"
   /* _mesa_function_pool[18462]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[18533]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[18586]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[18631]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[18667]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[18691]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[18720]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[18738]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[18750]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "\0"
   /* _mesa_function_pool[18778]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[18795]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[18812]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[18839]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[18850]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[18893]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[18909]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[18945]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[18999]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[19021]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[19063]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[19100]: FramebufferTextureFaceARB (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureFaceARB\0"
   "\0"
   /* _mesa_function_pool[19135]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[19197]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[19232]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[19258]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[19278]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[19313]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[19358]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[19395]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[19446]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[19472]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[19520]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "\0"
   /* _mesa_function_pool[19544]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[19558]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[19576]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[19595]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[19634]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[19678]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[19700]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[19728]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[19783]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[19818]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[19852]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[19883]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[19914]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[19934]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[19977]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[20010]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[20083]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[20109]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[20154]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[20199]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[20216]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[20260]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[20277]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[20294]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[20330]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[20347]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[20378]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[20409]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[20461]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[20480]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[20495]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[20526]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[20548]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[20565]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[20577]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[20598]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[20643]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[20692]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[20718]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[20762]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[20810]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[20849]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[20871]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[20919]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[20967]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[21014]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[21034]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[21063]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[21086]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[21106]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[21136]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[21150]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[21165]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "\0"
   /* _mesa_function_pool[21204]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[21233]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[21244]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[21263]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[21313]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[21331]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[21362]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[21398]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[21416]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[21429]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[21448]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[21467]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[21485]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[21507]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[21573]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[21598]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[21619]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[21639]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[21659]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[21679]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[21705]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[21723]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[21743]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[21760]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[21780]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[21813]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[21856]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[21889]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[21923]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[21953]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "\0"
   /* _mesa_function_pool[22021]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[22049]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[22064]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[22082]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[22113]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[22155]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[22210]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[22230]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[22287]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[22319]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[22373]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[22392]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[22407]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[22432]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[22447]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[22464]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[22491]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[22508]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[22535]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[22582]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[22632]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[22679]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[22700]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[22737]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[22777]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[22801]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[22849]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[22899]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[22936]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[22950]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[22977]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "\0"
   /* _mesa_function_pool[23044]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[23061]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[23106]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[23128]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[23164]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[23246]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[23275]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[23318]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[23333]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[23383]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[23416]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[23441]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[23496]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[23524]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[23579]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[23609]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[23626]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[23642]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[23656]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[23694]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[23709]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[23744]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[23771]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[23789]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[23819]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[23855]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[23890]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[23918]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[23955]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[23997]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[24023]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[24039]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[24095]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[24112]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[24129]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[24146]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[24163]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[24180]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[24207]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[24224]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[24238]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[24261]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[24289]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[24331]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[24357]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[24389]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[24404]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[24418]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[24443]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[24461]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[24478]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[24499]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[24544]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[24601]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[24619]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[24649]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[24664]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[24676]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[24727]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[24749]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[24767]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[24785]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "\0"
   /* _mesa_function_pool[24828]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[24855]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[24889]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "\0"
   /* _mesa_function_pool[24913]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[24921]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[24948]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[24977]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[25028]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[25040]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[25086]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[25108]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[25141]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[25185]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[25215]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[25259]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "\0"
   /* _mesa_function_pool[25286]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[25320]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[25336]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[25370]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[25386]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[25421]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[25437]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[25453]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[25469]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[25481]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[25517]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[25563]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[25623]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[25641]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[25653]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[25665]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[25686]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[25698]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[25724]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[25780]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[25795]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[25810]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[25824]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[25848]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[25863]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[25901]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[25928]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[25945]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[25966]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[25983]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[26016]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[26041]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[26088]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[26125]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[26141]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[26186]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[26201]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[26217]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[26235]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26254]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[26274]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[26292]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[26311]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[26333]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[26376]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[26442]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[26477]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[26506]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[26566]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[26584]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[26628]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[26646]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[26664]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[26682]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[26706]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[26747]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[26775]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[26793]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[26827]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[26861]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[26886]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[26905]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[26921]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[26938]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[26961]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[27044]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[27075]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[27129]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[27153]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[27183]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[27230]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[27245]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[27267]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[27296]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[27322]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "\0"
   /* _mesa_function_pool[27348]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[27374]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[27400]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[27454]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "\0"
   /* _mesa_function_pool[27504]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[27551]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[27583]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[27601]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[27647]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[27690]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[27735]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[27769]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[27794]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[27828]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[27877]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[27926]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[27953]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[27997]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[28021]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[28065]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[28089]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[28122]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[28157]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[28177]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[28203]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[28229]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "\0"
   /* _mesa_function_pool[28251]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[28281]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[28313]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[28330]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[28373]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[28391]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[28414]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[28478]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[28531]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[28557]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[28579]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[28597]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[28620]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[28670]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[28705]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[28720]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[28752]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[28771]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[28791]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[28806]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[28820]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[28836]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[28857]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[28871]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[28902]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "\0"
   /* _mesa_function_pool[28925]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[28951]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[29000]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[29017]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[29034]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[29079]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[29106]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[29128]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[29162]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[29205]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[29239]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[29281]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[29367]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[29394]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[29420]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[29467]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[29485]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[29517]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[29552]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[29591]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[29638]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[29669]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[29689]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[29714]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[29768]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[29786]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[29832]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[29859]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[29887]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[29903]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[29919]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[29935]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[29989]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[30020]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[30076]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[30126]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[30142]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[30198]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[30290]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[30362]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[30392]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[30409]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[30430]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[30489]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[30526]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "\0"
   /* _mesa_function_pool[30546]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[30573]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[30591]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[30629]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[30644]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[30660]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[30697]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[30731]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[30758]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[30801]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[30817]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[30843]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[30861]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[30892]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[30937]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[30974]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[31017]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[31040]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[31086]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[31148]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[31169]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[31259]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[31291]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[31311]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[31331]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[31356]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[31402]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[31448]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "\0"
   /* _mesa_function_pool[31474]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[31520]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[31541]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "\0"
   /* _mesa_function_pool[31596]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[31642]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[31682]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[31716]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[31741]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[31754]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[31797]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[31814]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[31832]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[31850]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[31902]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[31953]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[31992]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[32008]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[32051]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[32097]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[32124]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[32142]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[32191]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[32228]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[32279]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "\0"
   /* _mesa_function_pool[32302]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[32321]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[32341]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[32360]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[32407]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[32427]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[32445]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[32500]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[32515]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[32541]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[32569]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[32600]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[32641]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[32655]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[32697]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[32732]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[32760]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[32774]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[32803]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[32863]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[32929]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[32949]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[32974]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[33007]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[33054]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[33072]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[33105]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[33153]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[33210]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[33229]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[33249]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[33262]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[33288]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[33315]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[33382]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[33436]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "\0"
   /* _mesa_function_pool[33475]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[33505]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[33521]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[33609]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[33636]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[33650]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "\0"
   /* _mesa_function_pool[33682]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[33699]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[33715]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[33731]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[33747]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[33772]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[33788]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[33815]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[33836]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[33887]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[33943]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[33959]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[33978]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[34021]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[34045]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[34060]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[34085]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[34106]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[34130]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[34178]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[34204]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[34234]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[34253]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[34288]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[34305]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[34329]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[34369]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[34435]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[34452]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[34478]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[34523]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[34550]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[34567]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[34612]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[34639]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[34676]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[34703]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[34719]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[34766]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[34801]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[34818]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[34837]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[34887]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[34917]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[34943]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[34981]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[35000]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[35028]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[35065]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[35110]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[35125]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[35172]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[35190]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "\0"
   /* _mesa_function_pool[35209]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[35254]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[35274]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[35334]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[35362]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[35388]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[35402]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[35422]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[35442]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[35462]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[35490]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[35538]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[35576]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[35594]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[35619]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[35634]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[35683]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[35701]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[35722]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[35744]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[35774]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[35786]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[35807]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[35836]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[35894]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[35910]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[35935]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[35975]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[35991]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[36007]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[36023]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[36039]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[36061]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[36091]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[36109]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[36137]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[36165]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[36204]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[36219]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[36254]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[36271]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[36316]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[36348]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[36383]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[36429]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[36456]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[36492]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[36522]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[36570]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[36590]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[36634]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[36668]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[36725]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[36750]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[36814]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[36833]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[36859]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[36873]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[36891]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[36916]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[36992]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[37035]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[37069]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[37083]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[37099]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[37113]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "\0"
   /* _mesa_function_pool[37148]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[37177]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerARB\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[37268]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[37284]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[37346]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[37363]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[37400]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[37434]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[37473]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[37497]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[37517]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[37563]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[37591]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[37630]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[37648]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[37670]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[37685]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[37702]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[37734]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[37764]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[37780]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[37802]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[37835]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[37870]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[37880]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[37926]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[37956]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[37990]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[38024]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[38044]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[38068]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[38111]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[38188]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[38208]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[38230]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[38243]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[38258]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[38273]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[38301]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[38326]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[38382]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[38413]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[38440]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[38463]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[38483]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[38508]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[38534]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[38564]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[38622]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[38638]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[38662]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[38676]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[38700]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[38721]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[38756]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[38771]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[38796]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[38861]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[38904]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayAPPLE\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[38963]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[38985]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[39036]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[39067]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[39096]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[39127]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[39150]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[39171]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[39198]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[39236]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[39261]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[39296]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[39347]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[39366]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[39389]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[39409]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[39464]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[39487]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[39530]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[39554]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[39569]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[39593]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[39608]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[39626]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[39644]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[39659]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[39683]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[39698]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[39713]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[39761]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[39832]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[39896]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[39929]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[39963]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[40072]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[40107]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[40132]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[40188]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[40246]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[40265]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[40283]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[40302]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[40321]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[40345]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[40361]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[40428]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[40446]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[40497]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[40532]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[40550]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[40573]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[40600]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40637]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[40687]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[40720]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[40765]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[40785]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[40801]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[40817]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[40869]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[40906]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[40927]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[40951]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[40993]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[41018]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[41038]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[41065]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[41089]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[41102]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[41147]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[41170]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[41190]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[41205]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[41231]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[41246]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[41263]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[41292]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[41324]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[41349]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[41364]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[41396]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[41434]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[41462]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[41487]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[41551]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[41582]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[41619]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[41640]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[41673]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[41697]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[41724]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[41743]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[41796]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[41837]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[41868]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[41882]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[41903]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "\0"
   /* _mesa_function_pool[41924]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[41953]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[41970]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[41986]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[42000]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[42019]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[42038]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[42074]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[42104]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[42126]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[42162]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[42191]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[42242]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[42259]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[42276]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[42299]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[42316]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[42342]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[42393]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[42411]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[42443]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[42461]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[42494]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[42540]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[42586]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[42641]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[42704]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[42731]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[42749]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[42776]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[42790]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[42835]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[42856]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[42881]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[42895]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[42909]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[42934]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[42948]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[42975]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[43025]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[43047]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[43066]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[43096]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[43115]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[43143]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[43162]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[43181]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[43199]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[43222]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[43242]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[43274]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[43309]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[43331]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[43344]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[43372]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[43406]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[43424]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "\0"
   /* _mesa_function_pool[43468]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[43566]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[43596]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[43651]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[43676]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[43716]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[43741]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 17927, CompressedTexImage1D_remap_index },
   { 15417, CompressedTexImage2D_remap_index },
   { 11307, CompressedTexImage3D_remap_index },
   { 28414, CompressedTexSubImage1D_remap_index },
   { 34369, CompressedTexSubImage2D_remap_index },
   {  5818, CompressedTexSubImage3D_remap_index },
   {  3918, GetCompressedTexImage_remap_index },
   { 17136, LoadTransposeMatrixd_remap_index },
   { 17084, LoadTransposeMatrixf_remap_index },
   { 31850, MultTransposeMatrixd_remap_index },
   { 12614, MultTransposeMatrixf_remap_index },
   { 41796, SampleCoverage_remap_index },
   {  3187, BlendFuncSeparate_remap_index },
   { 20718, FogCoordPointer_remap_index },
   { 37926, FogCoordd_remap_index },
   { 37702, FogCoorddv_remap_index },
   { 30892, MultiDrawArrays_remap_index },
   { 29281, PointParameterf_remap_index },
   {  4528, PointParameterfv_remap_index },
   { 29239, PointParameteri_remap_index },
   {  8163, PointParameteriv_remap_index },
   {  4950, SecondaryColor3b_remap_index },
   { 37517, SecondaryColor3bv_remap_index },
   { 32051, SecondaryColor3d_remap_index },
   { 11440, SecondaryColor3dv_remap_index },
   {  5046, SecondaryColor3i_remap_index },
   { 27601, SecondaryColor3iv_remap_index },
   {  4826, SecondaryColor3s_remap_index },
   { 14703, SecondaryColor3sv_remap_index },
   { 20871, SecondaryColor3ub_remap_index },
   {  6711, SecondaryColor3ubv_remap_index },
   { 20919, SecondaryColor3ui_remap_index },
   { 22801, SecondaryColor3uiv_remap_index },
   { 20762, SecondaryColor3us_remap_index },
   {  9163, SecondaryColor3usv_remap_index },
   { 33153, SecondaryColorPointer_remap_index },
   { 11065, WindowPos2d_remap_index },
   { 16150, WindowPos2dv_remap_index },
   { 11012, WindowPos2f_remap_index },
   { 22155, WindowPos2fv_remap_index },
   { 11118, WindowPos2i_remap_index },
   {  6072, WindowPos2iv_remap_index },
   { 11171, WindowPos2s_remap_index },
   { 43596, WindowPos2sv_remap_index },
   { 14950, WindowPos3d_remap_index },
   { 14434, WindowPos3dv_remap_index },
   { 15037, WindowPos3f_remap_index },
   {  8022, WindowPos3fv_remap_index },
   { 15146, WindowPos3i_remap_index },
   { 42586, WindowPos3iv_remap_index },
   { 15262, WindowPos3s_remap_index },
   { 23524, WindowPos3sv_remap_index },
   {  5954, BeginQuery_remap_index },
   { 42461, BindBuffer_remap_index },
   { 36348, BufferData_remap_index },
   {  9660, BufferSubData_remap_index },
   { 29552, DeleteBuffers_remap_index },
   { 21165, DeleteQueries_remap_index },
   { 18750, EndQuery_remap_index },
   { 39896, GenBuffers_remap_index },
   {  1815, GenQueries_remap_index },
   { 27075, GetBufferParameteriv_remap_index },
   { 39761, GetBufferPointerv_remap_index },
   { 29591, GetBufferSubData_remap_index },
   {  7701, GetQueryObjectiv_remap_index },
   {  7370, GetQueryObjectuiv_remap_index },
   { 11633, GetQueryiv_remap_index },
   { 17617, IsBuffer_remap_index },
   { 27322, IsQuery_remap_index },
   { 11744, MapBuffer_remap_index },
   { 42191, UnmapBuffer_remap_index },
   {   315, AttachShader_remap_index },
   { 34837, BindAttribLocation_remap_index },
   { 39963, BlendEquationSeparate_remap_index },
   { 30591, CompileShader_remap_index },
   { 13923, CreateProgram_remap_index },
   { 29467, CreateShader_remap_index },
   { 19576, DeleteProgram_remap_index },
   { 30573, DeleteShader_remap_index },
   { 41724, DetachShader_remap_index },
   { 32803, DisableVertexAttribArray_remap_index },
   { 21953, DrawBuffers_remap_index },
   { 40188, EnableVertexAttribArray_remap_index },
   { 35490, GetActiveAttrib_remap_index },
   { 40637, GetActiveUniform_remap_index },
   { 16723, GetAttachedShaders_remap_index },
   { 26041, GetAttribLocation_remap_index },
   { 10752, GetProgramInfoLog_remap_index },
   { 21723, GetProgramiv_remap_index },
   {  3680, GetShaderInfoLog_remap_index },
   {  7061, GetShaderSource_remap_index },
   { 16484, GetShaderiv_remap_index },
   {  6005, GetUniformLocation_remap_index },
   { 12767, GetUniformfv_remap_index },
   {  2074, GetUniformiv_remap_index },
   { 33521, GetVertexAttribPointerv_remap_index },
   { 39713, GetVertexAttribdv_remap_index },
   { 34130, GetVertexAttribfv_remap_index },
   { 36522, GetVertexAttribiv_remap_index },
   {  4104, IsProgram_remap_index },
   { 37069, IsShader_remap_index },
   { 27794, LinkProgram_remap_index },
   { 36165, ShaderSource_remap_index },
   { 36137, StencilFuncSeparate_remap_index },
   { 34452, StencilMaskSeparate_remap_index },
   { 35634, StencilOpSeparate_remap_index },
   { 39036, Uniform1f_remap_index },
   {  7877, Uniform1fv_remap_index },
   { 39096, Uniform1i_remap_index },
   { 17798, Uniform1iv_remap_index },
   { 41292, Uniform2f_remap_index },
   { 21889, Uniform2fv_remap_index },
   { 41364, Uniform2i_remap_index },
   { 19818, Uniform2iv_remap_index },
   {   889, Uniform3f_remap_index },
   { 36634, Uniform3fv_remap_index },
   {   802, Uniform3i_remap_index },
   { 37956, Uniform3iv_remap_index },
   {  4398, Uniform4f_remap_index },
   {  8574, Uniform4fv_remap_index },
   {  4335, Uniform4i_remap_index },
   { 43741, Uniform4iv_remap_index },
   {  9766, UniformMatrix2fv_remap_index },
   { 22535, UniformMatrix3fv_remap_index },
   { 10235, UniformMatrix4fv_remap_index },
   { 39198, UseProgram_remap_index },
   { 23955, ValidateProgram_remap_index },
   { 17886, VertexAttrib1d_remap_index },
   { 36992, VertexAttrib1dv_remap_index },
   { 18009, VertexAttrib1s_remap_index },
   { 33978, VertexAttrib1sv_remap_index },
   {  7835, VertexAttrib2d_remap_index },
   { 23275, VertexAttrib2dv_remap_index },
   {  7747, VertexAttrib2s_remap_index },
   { 14056, VertexAttrib2sv_remap_index },
   { 11667, VertexAttrib3d_remap_index },
   { 21813, VertexAttrib3dv_remap_index },
   { 11542, VertexAttrib3s_remap_index },
   { 38861, VertexAttrib3sv_remap_index },
   { 11790, VertexAttrib4Nbv_remap_index },
   { 27690, VertexAttrib4Niv_remap_index },
   { 20154, VertexAttrib4Nsv_remap_index },
   {  1439, VertexAttrib4Nub_remap_index },
   { 32360, VertexAttrib4Nubv_remap_index },
   { 10309, VertexAttrib4Nuiv_remap_index },
   { 34719, VertexAttrib4Nusv_remap_index },
   {  9093, VertexAttrib4bv_remap_index },
   { 27953, VertexAttrib4d_remap_index },
   { 28330, VertexAttrib4dv_remap_index },
   { 38068, VertexAttrib4iv_remap_index },
   { 28021, VertexAttrib4s_remap_index },
   { 18850, VertexAttrib4sv_remap_index },
   {  9995, VertexAttrib4ubv_remap_index },
   { 20109, VertexAttrib4uiv_remap_index },
   {  1365, VertexAttrib4usv_remap_index },
   { 32445, VertexAttribPointer_remap_index },
   { 29079, UniformMatrix2x3fv_remap_index },
   {   922, UniformMatrix2x4fv_remap_index },
   { 10282, UniformMatrix3x2fv_remap_index },
   { 42749, UniformMatrix3x4fv_remap_index },
   { 38413, UniformMatrix4x2fv_remap_index },
   { 11585, UniformMatrix4x3fv_remap_index },
   { 16390, BeginConditionalRender_remap_index },
   { 24039, BeginTransformFeedback_remap_index },
   { 12489, BindBufferBase_remap_index },
   {  7589, BindBufferRange_remap_index },
   { 22319, BindFragDataLocation_remap_index },
   { 23383, ClampColor_remap_index },
   { 16750, ClearBufferfi_remap_index },
   { 16599, ClearBufferfv_remap_index },
   { 20577, ClearBufferiv_remap_index },
   { 37780, ClearBufferuiv_remap_index },
   { 13124, ColorMaski_remap_index },
   { 37113, Disablei_remap_index },
   { 15004, Enablei_remap_index },
   { 22849, EndConditionalRender_remap_index },
   { 19395, EndTransformFeedback_remap_index },
   { 11978, GetBooleani_v_remap_index },
   { 39296, GetFragDataLocation_remap_index },
   { 20598, GetIntegeri_v_remap_index },
   { 28313, GetStringi_remap_index },
   { 30076, GetTexParameterIiv_remap_index },
   { 13320, GetTexParameterIuiv_remap_index },
   { 30290, GetTransformFeedbackVarying_remap_index },
   { 35935, GetUniformuiv_remap_index },
   { 42975, GetVertexAttribIiv_remap_index },
   { 20409, GetVertexAttribIuiv_remap_index },
   { 33436, IsEnabledi_remap_index },
   { 19634, TexParameterIiv_remap_index },
   { 16205, TexParameterIuiv_remap_index },
   { 38796, TransformFeedbackVaryings_remap_index },
   {  5441, Uniform1ui_remap_index },
   { 25481, Uniform1uiv_remap_index },
   { 24855, Uniform2ui_remap_index },
   { 13166, Uniform2uiv_remap_index },
   { 32697, Uniform3ui_remap_index },
   { 18909, Uniform3uiv_remap_index },
   { 11901, Uniform4ui_remap_index },
   { 17832, Uniform4uiv_remap_index },
   { 35065, VertexAttribI1iv_remap_index },
   { 11393, VertexAttribI1uiv_remap_index },
   {  7418, VertexAttribI4bv_remap_index },
   { 10146, VertexAttribI4sv_remap_index },
   {  8374, VertexAttribI4ubv_remap_index },
   {  6877, VertexAttribI4usv_remap_index },
   { 40132, VertexAttribIPointer_remap_index },
   {  8321, PrimitiveRestartIndex_remap_index },
   { 33650, TexBuffer_remap_index },
   {   111, FramebufferTexture_remap_index },
   { 23789, GetBufferParameteri64v_remap_index },
   { 39464, GetInteger64i_v_remap_index },
   { 40446, VertexAttribDivisor_remap_index },
   { 43424, MinSampleShading_remap_index },
   {  7106, BindProgramARB_remap_index },
   { 30758, DeleteProgramsARB_remap_index },
   { 15091, GenProgramsARB_remap_index },
   { 14099, GetProgramEnvParameterdvARB_remap_index },
   { 29517, GetProgramEnvParameterfvARB_remap_index },
   { 30937, GetProgramLocalParameterdvARB_remap_index },
   { 37363, GetProgramLocalParameterfvARB_remap_index },
   { 22464, GetProgramStringARB_remap_index },
   {  8207, GetProgramivARB_remap_index },
   { 31259, IsProgramARB_remap_index },
   { 17204, ProgramEnvParameter4dARB_remap_index },
   {  2660, ProgramEnvParameter4dvARB_remap_index },
   { 38564, ProgramEnvParameter4fARB_remap_index },
   { 24544, ProgramEnvParameter4fvARB_remap_index },
   { 22899, ProgramLocalParameter4dARB_remap_index },
   {  3974, ProgramLocalParameter4dvARB_remap_index },
   { 30489, ProgramLocalParameter4fARB_remap_index },
   { 19197, ProgramLocalParameter4fvARB_remap_index },
   { 31331, ProgramStringARB_remap_index },
   { 11937, VertexAttrib1fARB_remap_index },
   { 31754, VertexAttrib1fvARB_remap_index },
   { 22113, VertexAttrib2fARB_remap_index },
   { 13417, VertexAttrib2fvARB_remap_index },
   {   334, VertexAttrib3fARB_remap_index },
   { 26333, VertexAttrib3fvARB_remap_index },
   { 25215, VertexAttrib4fARB_remap_index },
   { 14391, VertexAttrib4fvARB_remap_index },
   { 35722, AttachObjectARB_remap_index },
   { 22508, CreateProgramObjectARB_remap_index },
   { 16648, CreateShaderObjectARB_remap_index },
   { 15476, DeleteObjectARB_remap_index },
   { 38208, DetachObjectARB_remap_index },
   { 36061, GetAttachedObjectsARB_remap_index },
   { 24749, GetHandleARB_remap_index },
   { 20526, GetInfoLogARB_remap_index },
   { 21331, GetObjectParameterfvARB_remap_index },
   { 41837, GetObjectParameterivARB_remap_index },
   {  5553, DrawArraysInstancedARB_remap_index },
   {  7285, DrawElementsInstancedARB_remap_index },
   { 13880, BindFramebuffer_remap_index },
   {  8230, BindRenderbuffer_remap_index },
   { 33836, BlitFramebuffer_remap_index },
   {  6276, CheckFramebufferStatus_remap_index },
   { 20010, DeleteFramebuffers_remap_index },
   { 36916, DeleteRenderbuffers_remap_index },
   { 31169, FramebufferRenderbuffer_remap_index },
   { 33887, FramebufferTexture1D_remap_index },
   { 23164, FramebufferTexture2D_remap_index },
   { 26961, FramebufferTexture3D_remap_index },
   { 37177, FramebufferTextureLayer_remap_index },
   { 39832, GenFramebuffers_remap_index },
   { 33315, GenRenderbuffers_remap_index },
   {  7529, GenerateMipmap_remap_index },
   {  5249, GetFramebufferAttachmentParameteriv_remap_index },
   { 43468, GetRenderbufferParameteriv_remap_index },
   {  6440, IsFramebuffer_remap_index },
   { 25563, IsRenderbuffer_remap_index },
   {   629, RenderbufferStorage_remap_index },
   { 14874, RenderbufferStorageMultisample_remap_index },
   { 19100, FramebufferTextureFaceARB_remap_index },
   {  5176, FlushMappedBufferRange_remap_index },
   { 30974, MapBufferRange_remap_index },
   { 13259, BindVertexArray_remap_index },
   {  1133, DeleteVertexArrays_remap_index },
   { 17525, GenVertexArrays_remap_index },
   { 38904, IsVertexArray_remap_index },
   { 13039, GetActiveUniformBlockName_remap_index },
   { 43242, GetActiveUniformBlockiv_remap_index },
   {  3106, GetActiveUniformName_remap_index },
   { 14027, GetActiveUniformsiv_remap_index },
   { 41697, GetUniformBlockIndex_remap_index },
   { 10406, GetUniformIndices_remap_index },
   { 34676, UniformBlockBinding_remap_index },
   { 42704, CopyBufferSubData_remap_index },
   { 43025, ClientWaitSync_remap_index },
   { 11269, DeleteSync_remap_index },
   { 34703, FenceSync_remap_index },
   { 38188, GetInteger64v_remap_index },
   { 40302, GetSynciv_remap_index },
   { 15497, IsSync_remap_index },
   { 33505, WaitSync_remap_index },
   { 13074, DrawElementsBaseVertex_remap_index },
   { 16838, DrawElementsInstancedBaseVertex_remap_index },
   { 37434, DrawRangeElementsBaseVertex_remap_index },
   { 41396, MultiDrawElementsBaseVertex_remap_index },
   { 24289, ProvokingVertex_remap_index },
   {  5498, GetMultisamplefv_remap_index },
   { 35172, SampleMaski_remap_index },
   {  1944, TexImage2DMultisample_remap_index },
   { 41640, TexImage3DMultisample_remap_index },
   { 22977, BlendEquationSeparateiARB_remap_index },
   { 27454, BlendEquationiARB_remap_index },
   {  3732, BlendFuncSeparateiARB_remap_index },
   { 24785, BlendFunciARB_remap_index },
   {  1686, BindFragDataLocationIndexed_remap_index },
   { 28902, GetFragDataIndex_remap_index },
   {  2827, BindSampler_remap_index },
   { 42835, DeleteSamplers_remap_index },
   { 35683, GenSamplers_remap_index },
   {  2527, GetSamplerParameterIiv_remap_index },
   {  5742, GetSamplerParameterIuiv_remap_index },
   { 23246, GetSamplerParameterfv_remap_index },
   { 24948, GetSamplerParameteriv_remap_index },
   { 26186, IsSampler_remap_index },
   {  8634, SamplerParameterIiv_remap_index },
   { 12328, SamplerParameterIuiv_remap_index },
   { 42909, SamplerParameterf_remap_index },
   { 38508, SamplerParameterfv_remap_index },
   { 42856, SamplerParameteri_remap_index },
   { 28177, SamplerParameteriv_remap_index },
   { 23333, GetQueryObjecti64v_remap_index },
   {  4026, GetQueryObjectui64v_remap_index },
   { 12914, QueryCounter_remap_index },
   { 37764, ColorP3ui_remap_index },
   {  6595, ColorP3uiv_remap_index },
   { 17645, ColorP4ui_remap_index },
   { 25928, ColorP4uiv_remap_index },
   { 13783, MultiTexCoordP1ui_remap_index },
   { 25698, MultiTexCoordP1uiv_remap_index },
   { 34060, MultiTexCoordP2ui_remap_index },
   {  8947, MultiTexCoordP2uiv_remap_index },
   { 26016, MultiTexCoordP3ui_remap_index },
   {   391, MultiTexCoordP3uiv_remap_index },
   { 40107, MultiTexCoordP4ui_remap_index },
   { 34178, MultiTexCoordP4uiv_remap_index },
   { 36254, NormalP3ui_remap_index },
   { 25623, NormalP3uiv_remap_index },
   { 40993, SecondaryColorP3ui_remap_index },
   {  5690, SecondaryColorP3uiv_remap_index },
   {   162, TexCoordP1ui_remap_index },
   {   609, TexCoordP1uiv_remap_index },
   { 26292, TexCoordP2ui_remap_index },
   { 36570, TexCoordP2uiv_remap_index },
   { 14749, TexCoordP3ui_remap_index },
   { 17688, TexCoordP3uiv_remap_index },
   { 33959, TexCoordP4ui_remap_index },
   {  1769, TexCoordP4uiv_remap_index },
   { 14818, VertexAttribP1ui_remap_index },
   {  4078, VertexAttribP1uiv_remap_index },
   { 29689, VertexAttribP2ui_remap_index },
   {  4872, VertexAttribP2uiv_remap_index },
   {  1487, VertexAttribP3ui_remap_index },
   { 28203, VertexAttribP3uiv_remap_index },
   {  4310, VertexAttribP4ui_remap_index },
   { 16028, VertexAttribP4uiv_remap_index },
   { 34801, VertexP2ui_remap_index },
   { 32427, VertexP2uiv_remap_index },
   { 22491, VertexP3ui_remap_index },
   {  5987, VertexP3uiv_remap_index },
   {  3061, VertexP4ui_remap_index },
   { 43406, VertexP4uiv_remap_index },
   {   777, DrawArraysIndirect_remap_index },
   { 23496, DrawElementsIndirect_remap_index },
   { 10778, BindTransformFeedback_remap_index },
   { 10650, DeleteTransformFeedbacks_remap_index },
   { 35334, DrawTransformFeedback_remap_index },
   {  3868, GenTransformFeedbacks_remap_index },
   { 32949, IsTransformFeedback_remap_index },
   { 30546, PauseTransformFeedback_remap_index },
   { 35000, ResumeTransformFeedback_remap_index },
   { 22407, BeginQueryIndexed_remap_index },
   { 40497, DrawTransformFeedbackStream_remap_index },
   { 18999, EndQueryIndexed_remap_index },
   { 21679, GetQueryIndexediv_remap_index },
   { 43372, ClearDepthf_remap_index },
   { 23855, DepthRangef_remap_index },
   { 37802, GetShaderPrecisionFormat_remap_index },
   {  3161, ReleaseShaderCompiler_remap_index },
   { 25086, ShaderBinary_remap_index },
   { 19472, GetProgramBinary_remap_index },
   { 11835, ProgramBinary_remap_index },
   { 12070, ProgramParameteri_remap_index },
   { 26682, DepthRangeArrayv_remap_index },
   { 41462, DepthRangeIndexed_remap_index },
   { 32929, GetDoublei_v_remap_index },
   { 35190, GetFloati_v_remap_index },
   { 41903, ScissorArrayv_remap_index },
   { 24889, ScissorIndexed_remap_index },
   { 28229, ScissorIndexedv_remap_index },
   { 18440, ViewportArrayv_remap_index },
   { 31448, ViewportIndexedf_remap_index },
   { 19520, ViewportIndexedfv_remap_index },
   {  8421, GetGraphicsResetStatusARB_remap_index },
   { 29832, GetnColorTableARB_remap_index },
   {  2793, GetnCompressedTexImageARB_remap_index },
   {  1233, GetnConvolutionFilterARB_remap_index },
   {  4746, GetnHistogramARB_remap_index },
   { 18325, GetnMapdvARB_remap_index },
   { 12219, GetnMapfvARB_remap_index },
   { 34085, GetnMapivARB_remap_index },
   { 38676, GetnMinmaxARB_remap_index },
   {  3585, GetnPixelMapfvARB_remap_index },
   {  5716, GetnPixelMapuivARB_remap_index },
   { 11516, GetnPixelMapusvARB_remap_index },
   { 22021, GetnPolygonStippleARB_remap_index },
   { 28670, GetnSeparableFilterARB_remap_index },
   { 10040, GetnTexImageARB_remap_index },
   { 27769, GetnUniformdvARB_remap_index },
   { 33747, GetnUniformfvARB_remap_index },
   {  3136, GetnUniformivARB_remap_index },
   { 13643, GetnUniformuivARB_remap_index },
   { 25259, ReadnPixelsARB_remap_index },
   { 32600, DrawArraysInstancedBaseInstance_remap_index },
   { 10191, DrawElementsInstancedBaseInstance_remap_index },
   {  2605, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 34943, DrawTransformFeedbackInstanced_remap_index },
   { 13372, DrawTransformFeedbackStreamInstanced_remap_index },
   { 41924, GetInternalformativ_remap_index },
   { 19595, GetActiveAtomicCounterBufferiv_remap_index },
   { 43344, BindImageTexture_remap_index },
   { 21244, MemoryBarrier_remap_index },
   { 33815, TexStorage1D_remap_index },
   { 23106, TexStorage2D_remap_index },
   { 26938, TexStorage3D_remap_index },
   {  1410, TextureStorage1DEXT_remap_index },
   { 34887, TextureStorage2DEXT_remap_index },
   { 22082, TextureStorage3DEXT_remap_index },
   { 35594, ClearBufferData_remap_index },
   {  2112, ClearBufferSubData_remap_index },
   { 31017, DispatchCompute_remap_index },
   {  6360, DispatchComputeIndirect_remap_index },
   { 40321, TextureView_remap_index },
   { 21573, BindVertexBuffer_remap_index },
   { 29394, VertexAttribBinding_remap_index },
   { 29859, VertexAttribFormat_remap_index },
   { 32541, VertexAttribIFormat_remap_index },
   { 36109, VertexAttribLFormat_remap_index },
   { 34612, VertexBindingDivisor_remap_index },
   {  3299, MultiDrawArraysIndirect_remap_index },
   { 18050, MultiDrawElementsIndirect_remap_index },
   { 17985, TexBufferRange_remap_index },
   { 37990, TexStorage2DMultisample_remap_index },
   { 28122, TexStorage3DMultisample_remap_index },
   {  3331, BufferStorage_remap_index },
   { 38440, ClearTexImage_remap_index },
   { 13007, ClearTexSubImage_remap_index },
   {  4173, BindBuffersBase_remap_index },
   { 14364, BindBuffersRange_remap_index },
   { 10625, BindImageTextures_remap_index },
   {  2773, BindSamplers_remap_index },
   { 41018, BindTextures_remap_index },
   { 24921, BindVertexBuffers_remap_index },
   {  6389, InvalidateBufferData_remap_index },
   { 38382, InvalidateBufferSubData_remap_index },
   { 21204, InvalidateFramebuffer_remap_index },
   { 15765, InvalidateSubFramebuffer_remap_index },
   { 11876, InvalidateTexImage_remap_index },
   { 25286, InvalidateTexSubImage_remap_index },
   { 12666, PolygonOffsetEXT_remap_index },
   { 35701, DrawTexfOES_remap_index },
   { 24767, DrawTexfvOES_remap_index },
   {   986, DrawTexiOES_remap_index },
   { 29768, DrawTexivOES_remap_index },
   { 12023, DrawTexsOES_remap_index },
   { 21398, DrawTexsvOES_remap_index },
   { 25945, DrawTexxOES_remap_index },
   { 37630, DrawTexxvOES_remap_index },
   { 24180, PointSizePointerOES_remap_index },
   {   949, QueryMatrixxOES_remap_index },
   { 19063, SampleMaskSGIS_remap_index },
   { 32655, SamplePatternSGIS_remap_index },
   { 41324, ColorPointerEXT_remap_index },
   { 27348, EdgeFlagPointerEXT_remap_index },
   { 12743, IndexPointerEXT_remap_index },
   { 12933, NormalPointerEXT_remap_index },
   { 26747, TexCoordPointerEXT_remap_index },
   { 23997, VertexPointerEXT_remap_index },
   { 41263, DiscardFramebufferEXT_remap_index },
   { 10574, ActiveShaderProgram_remap_index },
   { 15978, BindProgramPipeline_remap_index },
   { 27400, CreateShaderProgramv_remap_index },
   {  3483, DeleteProgramPipelines_remap_index },
   { 24977, GenProgramPipelines_remap_index },
   {  7911, GetProgramPipelineInfoLog_remap_index },
   { 29935, GetProgramPipelineiv_remap_index },
   { 25040, IsProgramPipeline_remap_index },
   { 43222, LockArraysEXT_remap_index },
   { 42540, ProgramUniform1f_remap_index },
   {  9362, ProgramUniform1fv_remap_index },
   { 42494, ProgramUniform1i_remap_index },
   { 14604, ProgramUniform1iv_remap_index },
   { 33105, ProgramUniform1ui_remap_index },
   { 42342, ProgramUniform1uiv_remap_index },
   {  2251, ProgramUniform2f_remap_index },
   { 17399, ProgramUniform2fv_remap_index },
   {  2334, ProgramUniform2i_remap_index },
   { 20643, ProgramUniform2iv_remap_index },
   {  6956, ProgramUniform2ui_remap_index },
   {  8755, ProgramUniform2uiv_remap_index },
   {  4432, ProgramUniform3f_remap_index },
   { 28951, ProgramUniform3fv_remap_index },
   {  4480, ProgramUniform3i_remap_index },
   { 12958, ProgramUniform3iv_remap_index },
   { 14653, ProgramUniform3ui_remap_index },
   { 17348, ProgramUniform3uiv_remap_index },
   { 27828, ProgramUniform4f_remap_index },
   { 32142, ProgramUniform4fv_remap_index },
   { 27877, ProgramUniform4i_remap_index },
   {  1848, ProgramUniform4iv_remap_index },
   { 38985, ProgramUniform4ui_remap_index },
   { 31902, ProgramUniform4uiv_remap_index },
   { 19135, ProgramUniformMatrix2fv_remap_index },
   { 21507, ProgramUniformMatrix2x3fv_remap_index },
   {  7635, ProgramUniformMatrix2x4fv_remap_index },
   { 37284, ProgramUniformMatrix3fv_remap_index },
   { 32863, ProgramUniformMatrix3x2fv_remap_index },
   { 26376, ProgramUniformMatrix3x4fv_remap_index },
   { 31086, ProgramUniformMatrix4fv_remap_index },
   {  2185, ProgramUniformMatrix4x2fv_remap_index },
   {  7199, ProgramUniformMatrix4x3fv_remap_index },
   { 38024, UnlockArraysEXT_remap_index },
   { 31040, UseProgramStages_remap_index },
   {  1628, ValidateProgramPipeline_remap_index },
   { 16054, DebugMessageCallback_remap_index },
   { 31541, DebugMessageControl_remap_index },
   { 15335, DebugMessageInsert_remap_index },
   {  6759, GetDebugMessageLog_remap_index },
   {  6550, GetObjectLabel_remap_index },
   { 12044, GetObjectPtrLabel_remap_index },
   { 30526, ObjectLabel_remap_index },
   { 43309, ObjectPtrLabel_remap_index },
   { 17868, PopDebugGroup_remap_index },
   { 32279, PushDebugGroup_remap_index },
   {  8275, SecondaryColor3fEXT_remap_index },
   {  7789, SecondaryColor3fvEXT_remap_index },
   { 28620, MultiDrawElementsEXT_remap_index },
   { 10706, FogCoordfEXT_remap_index },
   { 18085, FogCoordfvEXT_remap_index },
   {  4151, ResizeBuffersMESA_remap_index },
   { 34021, WindowPos4dMESA_remap_index },
   { 27245, WindowPos4dvMESA_remap_index },
   {  4260, WindowPos4fMESA_remap_index },
   { 11285, WindowPos4fvMESA_remap_index },
   {  9045, WindowPos4iMESA_remap_index },
   {  3658, WindowPos4ivMESA_remap_index },
   { 27997, WindowPos4sMESA_remap_index },
   {  1080, WindowPos4svMESA_remap_index },
   { 28720, MultiModeDrawArraysIBM_remap_index },
   { 19783, MultiModeDrawElementsIBM_remap_index },
   { 32774, AreProgramsResidentNV_remap_index },
   { 39659, ExecuteProgramNV_remap_index },
   { 29485, GetProgramParameterdvNV_remap_index },
   { 36316, GetProgramParameterfvNV_remap_index },
   { 19232, GetProgramStringNV_remap_index },
   { 15689, GetProgramivNV_remap_index },
   { 18370, GetTrackMatrixivNV_remap_index },
   { 18812, GetVertexAttribdvNV_remap_index },
   { 16975, GetVertexAttribfvNV_remap_index },
   { 15871, GetVertexAttribivNV_remap_index },
   { 36039, LoadProgramNV_remap_index },
   { 19883, ProgramParameters4dvNV_remap_index },
   { 20495, ProgramParameters4fvNV_remap_index },
   {  6171, RequestResidentProgramsNV_remap_index },
   { 29106, TrackMatrixNV_remap_index },
   { 14134, VertexAttrib1dNV_remap_index },
   { 28065, VertexAttrib1dvNV_remap_index },
   { 28391, VertexAttrib1fNV_remap_index },
   { 41673, VertexAttrib1fvNV_remap_index },
   { 21063, VertexAttrib1sNV_remap_index },
   { 38044, VertexAttrib1svNV_remap_index },
   { 18346, VertexAttrib2dNV_remap_index },
   { 34305, VertexAttrib2dvNV_remap_index },
   { 27129, VertexAttrib2fNV_remap_index },
   { 25824, VertexAttrib2fvNV_remap_index },
   { 12839, VertexAttrib2sNV_remap_index },
   {  5666, VertexAttrib2svNV_remap_index },
   { 36725, VertexAttrib3dNV_remap_index },
   { 38638, VertexAttrib3dvNV_remap_index },
   {  4925, VertexAttrib3fNV_remap_index },
   { 40927, VertexAttrib3fvNV_remap_index },
   {  7143, VertexAttrib3sNV_remap_index },
   { 18397, VertexAttrib3svNV_remap_index },
   {  8477, VertexAttrib4dNV_remap_index },
   {  3408, VertexAttrib4dvNV_remap_index },
   {  8548, VertexAttrib4fNV_remap_index },
   { 41065, VertexAttrib4fvNV_remap_index },
   { 17483, VertexAttrib4sNV_remap_index },
   { 10890, VertexAttrib4svNV_remap_index },
   {  1601, VertexAttrib4ubNV_remap_index },
   { 10681, VertexAttrib4ubvNV_remap_index },
   { 28871, VertexAttribPointerNV_remap_index },
   { 27296, VertexAttribs1dvNV_remap_index },
   { 30817, VertexAttribs1fvNV_remap_index },
   {  6145, VertexAttribs1svNV_remap_index },
   { 42316, VertexAttribs2dvNV_remap_index },
   {  4284, VertexAttribs2fvNV_remap_index },
   { 27374, VertexAttribs2svNV_remap_index },
   {  1789, VertexAttribs3dvNV_remap_index },
   { 35362, VertexAttribs3fvNV_remap_index },
   { 14001, VertexAttribs3svNV_remap_index },
   { 19446, VertexAttribs4dvNV_remap_index },
   { 24331, VertexAttribs4fvNV_remap_index },
   { 20692, VertexAttribs4svNV_remap_index },
   { 32097, VertexAttribs4ubvNV_remap_index },
   { 41551, GetTexBumpParameterfvATI_remap_index },
   { 10432, GetTexBumpParameterivATI_remap_index },
   { 35462, TexBumpParameterfvATI_remap_index },
   {  8520, TexBumpParameterivATI_remap_index },
   {  9245, AlphaFragmentOp1ATI_remap_index },
   {  3432, AlphaFragmentOp2ATI_remap_index },
   {  9701, AlphaFragmentOp3ATI_remap_index },
   { 33288, BeginFragmentShaderATI_remap_index },
   {  3705, BindFragmentShaderATI_remap_index },
   {  7168, ColorFragmentOp1ATI_remap_index },
   { 12805, ColorFragmentOp2ATI_remap_index },
   { 23918, ColorFragmentOp3ATI_remap_index },
   { 16790, DeleteFragmentShaderATI_remap_index },
   { 43716, EndFragmentShaderATI_remap_index },
   { 22950, GenFragmentShadersATI_remap_index },
   { 41147, PassTexCoordATI_remap_index },
   { 35442, SampleMapATI_remap_index },
   { 34766, SetFragmentShaderConstantATI_remap_index },
   {  8451, ActiveStencilFaceEXT_remap_index },
   {  8077, BindVertexArrayAPPLE_remap_index },
   { 16320, GenVertexArraysAPPLE_remap_index },
   { 35028, GetProgramNamedParameterdvNV_remap_index },
   { 22700, GetProgramNamedParameterfvNV_remap_index },
   { 41582, ProgramNamedParameter4dNV_remap_index },
   { 37835, ProgramNamedParameter4dvNV_remap_index },
   { 40869, ProgramNamedParameter4fNV_remap_index },
   { 25386, ProgramNamedParameter4fvNV_remap_index },
   { 24238, PrimitiveRestartNV_remap_index },
   { 24727, GetTexGenxvOES_remap_index },
   { 14489, TexGenxOES_remap_index },
   { 32341, TexGenxvOES_remap_index },
   {  8001, DepthBoundsEXT_remap_index },
   {  5915, BindFramebufferEXT_remap_index },
   { 41205, BindRenderbufferEXT_remap_index },
   {  4369, BufferParameteriAPPLE_remap_index },
   { 39261, FlushMappedBufferRangeAPPLE_remap_index },
   { 27647, VertexAttribI1iEXT_remap_index },
   { 11224, VertexAttribI1uiEXT_remap_index },
   { 20216, VertexAttribI2iEXT_remap_index },
   { 40720, VertexAttribI2ivEXT_remap_index },
   { 25517, VertexAttribI2uiEXT_remap_index },
   { 35125, VertexAttribI2uivEXT_remap_index },
   { 19313, VertexAttribI3iEXT_remap_index },
   { 42790, VertexAttribI3ivEXT_remap_index },
   { 22632, VertexAttribI3uiEXT_remap_index },
   { 20967, VertexAttribI3uivEXT_remap_index },
   { 37880, VertexAttribI4iEXT_remap_index },
   {  6666, VertexAttribI4ivEXT_remap_index },
   {  2557, VertexAttribI4uiEXT_remap_index },
   { 27504, VertexAttribI4uivEXT_remap_index },
   {  2983, ClearColorIiEXT_remap_index },
   {  1208, ClearColorIuiEXT_remap_index },
   { 24261, BindBufferOffsetEXT_remap_index },
   { 18117, BeginPerfMonitorAMD_remap_index },
   { 32732, DeletePerfMonitorsAMD_remap_index },
   {  5418, EndPerfMonitorAMD_remap_index },
   { 26861, GenPerfMonitorsAMD_remap_index },
   { 12531, GetPerfMonitorCounterDataAMD_remap_index },
   { 34639, GetPerfMonitorCounterInfoAMD_remap_index },
   { 43676, GetPerfMonitorCounterStringAMD_remap_index },
   { 43274, GetPerfMonitorCountersAMD_remap_index },
   { 14205, GetPerfMonitorGroupStringAMD_remap_index },
   { 29638, GetPerfMonitorGroupsAMD_remap_index },
   { 13842, SelectPerfMonitorCountersAMD_remap_index },
   { 14302, GetObjectParameterivAPPLE_remap_index },
   { 43115, ObjectPurgeableAPPLE_remap_index },
   {  1897, ObjectUnpurgeableAPPLE_remap_index },
   { 42104, ActiveProgramEXT_remap_index },
   { 27267, CreateShaderProgramEXT_remap_index },
   { 36833, UseShaderProgramEXT_remap_index },
   { 30409, TextureBarrierNV_remap_index },
   {  2142, VDPAUFiniNV_remap_index },
   {   835, VDPAUGetSurfaceivNV_remap_index },
   { 23771, VDPAUInitNV_remap_index },
   { 21485, VDPAUIsSurfaceNV_remap_index },
   {  6415, VDPAUMapSurfacesNV_remap_index },
   {  3024, VDPAURegisterOutputSurfaceNV_remap_index },
   { 12400, VDPAURegisterVideoSurfaceNV_remap_index },
   { 10963, VDPAUSurfaceAccessNV_remap_index },
   {  4719, VDPAUUnmapSurfacesNV_remap_index },
   { 37734, VDPAUUnregisterSurfaceNV_remap_index },
   { 38483, BeginPerfQueryINTEL_remap_index },
   { 33609, CreatePerfQueryINTEL_remap_index },
   { 16251, DeletePerfQueryINTEL_remap_index },
   { 40550, EndPerfQueryINTEL_remap_index },
   { 42074, GetFirstPerfQueryIdINTEL_remap_index },
   { 30362, GetNextPerfQueryIdINTEL_remap_index },
   { 31953, GetPerfCounterInfoINTEL_remap_index },
   {   746, GetPerfQueryDataINTEL_remap_index },
   { 22287, GetPerfQueryIdByNameINTEL_remap_index },
   { 19977, GetPerfQueryInfoINTEL_remap_index },
   { 20378, StencilFuncSeparateATI_remap_index },
   {  5631, ProgramEnvParameters4fvEXT_remap_index },
   { 30660, ProgramLocalParameters4fvEXT_remap_index },
   {  3825, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  3610, EGLImageTargetTexture2DOES_remap_index },
   { 40687, AlphaFuncx_remap_index },
   { 19358, ClearColorx_remap_index },
   { 39929, ClearDepthx_remap_index },
   { 35807, Color4x_remap_index },
   { 23709, DepthRangex_remap_index },
   {  2381, Fogx_remap_index },
   { 14768, Fogxv_remap_index },
   {  8722, Frustumf_remap_index },
   {  8819, Frustumx_remap_index },
   { 19278, LightModelx_remap_index },
   { 32191, LightModelxv_remap_index },
   { 28925, Lightx_remap_index },
   { 41434, Lightxv_remap_index },
   {  3540, LineWidthx_remap_index },
   { 37400, LoadMatrixx_remap_index },
   { 42411, Materialx_remap_index },
   { 25336, Materialxv_remap_index },
   { 29128, MultMatrixx_remap_index },
   { 31356, MultiTexCoord4x_remap_index },
   { 25185, Normal3x_remap_index },
   { 15388, Orthof_remap_index },
   { 15569, Orthox_remap_index },
   { 27551, PointSizex_remap_index },
   {    70, PolygonOffsetx_remap_index },
   { 37148, Rotatex_remap_index },
   { 19934, SampleCoveragex_remap_index },
   { 12689, Scalex_remap_index },
   { 38273, TexEnvx_remap_index },
   { 43066, TexEnvxv_remap_index },
   {  1976, TexParameterx_remap_index },
   { 31682, Translatex_remap_index },
   { 33072, ClipPlanef_remap_index },
   { 32974, ClipPlanex_remap_index },
   {   707, GetClipPlanef_remap_index },
   {   570, GetClipPlanex_remap_index },
   { 19852, GetFixedv_remap_index },
   {  1267, GetLightxv_remap_index },
   { 22737, GetMaterialxv_remap_index },
   { 21362, GetTexEnvxv_remap_index },
   { 16675, GetTexParameterxv_remap_index },
   { 29162, PointParameterx_remap_index },
   {  5773, PointParameterxv_remap_index },
   { 19021, TexParameterxv_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   /* from GL_EXT_blend_color */
   { 34253, _gloffset_BlendColor },
   /* from GL_EXT_blend_minmax */
   { 36668, _gloffset_BlendEquation },
   /* from GL_EXT_color_subtable */
   {  5375, _gloffset_ColorSubTable },
   { 21263, _gloffset_CopyColorSubTable },
   /* from GL_EXT_convolution */
   {  1301, _gloffset_GetConvolutionParameteriv },
   { 13685, _gloffset_ConvolutionParameterfv },
   { 16537, _gloffset_CopyConvolutionFilter1D },
   { 18533, _gloffset_SeparableFilter2D },
   { 19728, _gloffset_GetConvolutionFilter },
   { 23441, _gloffset_ConvolutionFilter1D },
   { 25724, _gloffset_ConvolutionFilter2D },
   { 28478, _gloffset_GetSeparableFilter },
   { 30020, _gloffset_ConvolutionParameteri },
   { 30142, _gloffset_ConvolutionParameterf },
   { 35836, _gloffset_ConvolutionParameteriv },
   { 41487, _gloffset_GetConvolutionParameterfv },
   { 42641, _gloffset_CopyConvolutionFilter2D },
   /* from GL_EXT_copy_texture */
   { 27183, _gloffset_CopyTexImage2D },
   { 29786, _gloffset_CopyTexImage1D },
   { 32228, _gloffset_CopyTexSubImage1D },
   { 38111, _gloffset_CopyTexSubImage3D },
   { 41743, _gloffset_CopyTexSubImage2D },
   /* from GL_EXT_draw_range_elements */
   { 24676, _gloffset_DrawRangeElements },
   /* from GL_EXT_histogram */
   {  4618, _gloffset_GetHistogramParameterfv },
   {  8103, _gloffset_GetHistogramParameteriv },
   {  9485, _gloffset_Minmax },
   { 13941, _gloffset_GetMinmax },
   { 21856, _gloffset_Histogram },
   { 29714, _gloffset_GetMinmaxParameteriv },
   { 30697, _gloffset_ResetMinmax },
   { 31642, _gloffset_GetHistogram },
   { 33382, _gloffset_GetMinmaxParameterfv },
   { 34329, _gloffset_ResetHistogram },
   /* from GL_EXT_paletted_texture */
   { 13547, _gloffset_ColorTable },
   { 18142, _gloffset_GetColorTableParameterfv },
   { 26506, _gloffset_GetColorTable },
   { 30198, _gloffset_GetColorTableParameteriv },
   /* from GL_EXT_subtexture */
   {  2413, _gloffset_TexSubImage1D },
   { 36383, _gloffset_TexSubImage2D },
   /* from GL_EXT_texture3D */
   { 22230, _gloffset_TexImage3D },
   { 40361, _gloffset_TexSubImage3D },
   /* from GL_EXT_texture_object */
   {  4197, _gloffset_GenTextures },
   {  8866, _gloffset_BindTexture },
   { 17318, _gloffset_IsTexture },
   { 22582, _gloffset_PrioritizeTextures },
   { 26706, _gloffset_DeleteTextures },
   { 40817, _gloffset_AreTexturesResident },
   /* from GL_EXT_vertex_array */
   { 18631, _gloffset_ArrayElement },
   { 29205, _gloffset_DrawArrays },
   { 38721, _gloffset_GetPointerv },
   /* from GL_NV_read_buffer */
   { 29989, _gloffset_ReadBuffer },
   /* from GL_OES_blend_subtract */
   { 36668, _gloffset_BlendEquation },
   /* from GL_OES_texture_3D */
   { 22230, _gloffset_TexImage3D },
   { 38111, _gloffset_CopyTexSubImage3D },
   { 40361, _gloffset_TexSubImage3D },
   /* from GL_OES_texture_cube_map */
   { 16904, _gloffset_TexGeni },
   { 16932, _gloffset_TexGenf },
   { 20294, _gloffset_GetTexGenfv },
   { 34204, _gloffset_TexGeniv },
   { 36492, _gloffset_TexGenfv },
   { 42038, _gloffset_GetTexGeniv },
   /* from GL_SGI_color_table */
   {  2717, _gloffset_ColorTableParameteriv },
   { 13547, _gloffset_ColorTable },
   { 17262, _gloffset_ColorTableParameterfv },
   { 18142, _gloffset_GetColorTableParameterfv },
   { 26506, _gloffset_GetColorTable },
   { 26584, _gloffset_CopyColorTable },
   { 30198, _gloffset_GetColorTableParameteriv },
   {    -1, -1 }
};

