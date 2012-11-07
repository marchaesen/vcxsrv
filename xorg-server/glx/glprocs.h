/* DO NOT EDIT - This file generated automatically by gl_procs.py (from Mesa) script */

/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 * (C) Copyright IBM Corporation 2004, 2006
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
 * BRIAN PAUL, IBM,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


/* This file is only included by glapi.c and is used for
 * the GetProcAddress() function
 */

typedef struct {
    GLint Name_offset;
#if defined(NEED_FUNCTION_POINTER) || defined(GLX_INDIRECT_RENDERING)
    _glapi_proc Address;
#endif
    GLuint Offset;
} glprocs_table_t;

#if   !defined(NEED_FUNCTION_POINTER) && !defined(GLX_INDIRECT_RENDERING)
#  define NAME_FUNC_OFFSET(n,f1,f2,f3,o) { n , o }
#elif  defined(NEED_FUNCTION_POINTER) && !defined(GLX_INDIRECT_RENDERING)
#  define NAME_FUNC_OFFSET(n,f1,f2,f3,o) { n , (_glapi_proc) f1 , o }
#elif  defined(NEED_FUNCTION_POINTER) &&  defined(GLX_INDIRECT_RENDERING)
#  define NAME_FUNC_OFFSET(n,f1,f2,f3,o) { n , (_glapi_proc) f2 , o }
#elif !defined(NEED_FUNCTION_POINTER) &&  defined(GLX_INDIRECT_RENDERING)
#  define NAME_FUNC_OFFSET(n,f1,f2,f3,o) { n , (_glapi_proc) f3 , o }
#endif



static const char gl_string_table[] =
    "glNewList\0"
    "glEndList\0"
    "glCallList\0"
    "glCallLists\0"
    "glDeleteLists\0"
    "glGenLists\0"
    "glListBase\0"
    "glBegin\0"
    "glBitmap\0"
    "glColor3b\0"
    "glColor3bv\0"
    "glColor3d\0"
    "glColor3dv\0"
    "glColor3f\0"
    "glColor3fv\0"
    "glColor3i\0"
    "glColor3iv\0"
    "glColor3s\0"
    "glColor3sv\0"
    "glColor3ub\0"
    "glColor3ubv\0"
    "glColor3ui\0"
    "glColor3uiv\0"
    "glColor3us\0"
    "glColor3usv\0"
    "glColor4b\0"
    "glColor4bv\0"
    "glColor4d\0"
    "glColor4dv\0"
    "glColor4f\0"
    "glColor4fv\0"
    "glColor4i\0"
    "glColor4iv\0"
    "glColor4s\0"
    "glColor4sv\0"
    "glColor4ub\0"
    "glColor4ubv\0"
    "glColor4ui\0"
    "glColor4uiv\0"
    "glColor4us\0"
    "glColor4usv\0"
    "glEdgeFlag\0"
    "glEdgeFlagv\0"
    "glEnd\0"
    "glIndexd\0"
    "glIndexdv\0"
    "glIndexf\0"
    "glIndexfv\0"
    "glIndexi\0"
    "glIndexiv\0"
    "glIndexs\0"
    "glIndexsv\0"
    "glNormal3b\0"
    "glNormal3bv\0"
    "glNormal3d\0"
    "glNormal3dv\0"
    "glNormal3f\0"
    "glNormal3fv\0"
    "glNormal3i\0"
    "glNormal3iv\0"
    "glNormal3s\0"
    "glNormal3sv\0"
    "glRasterPos2d\0"
    "glRasterPos2dv\0"
    "glRasterPos2f\0"
    "glRasterPos2fv\0"
    "glRasterPos2i\0"
    "glRasterPos2iv\0"
    "glRasterPos2s\0"
    "glRasterPos2sv\0"
    "glRasterPos3d\0"
    "glRasterPos3dv\0"
    "glRasterPos3f\0"
    "glRasterPos3fv\0"
    "glRasterPos3i\0"
    "glRasterPos3iv\0"
    "glRasterPos3s\0"
    "glRasterPos3sv\0"
    "glRasterPos4d\0"
    "glRasterPos4dv\0"
    "glRasterPos4f\0"
    "glRasterPos4fv\0"
    "glRasterPos4i\0"
    "glRasterPos4iv\0"
    "glRasterPos4s\0"
    "glRasterPos4sv\0"
    "glRectd\0"
    "glRectdv\0"
    "glRectf\0"
    "glRectfv\0"
    "glRecti\0"
    "glRectiv\0"
    "glRects\0"
    "glRectsv\0"
    "glTexCoord1d\0"
    "glTexCoord1dv\0"
    "glTexCoord1f\0"
    "glTexCoord1fv\0"
    "glTexCoord1i\0"
    "glTexCoord1iv\0"
    "glTexCoord1s\0"
    "glTexCoord1sv\0"
    "glTexCoord2d\0"
    "glTexCoord2dv\0"
    "glTexCoord2f\0"
    "glTexCoord2fv\0"
    "glTexCoord2i\0"
    "glTexCoord2iv\0"
    "glTexCoord2s\0"
    "glTexCoord2sv\0"
    "glTexCoord3d\0"
    "glTexCoord3dv\0"
    "glTexCoord3f\0"
    "glTexCoord3fv\0"
    "glTexCoord3i\0"
    "glTexCoord3iv\0"
    "glTexCoord3s\0"
    "glTexCoord3sv\0"
    "glTexCoord4d\0"
    "glTexCoord4dv\0"
    "glTexCoord4f\0"
    "glTexCoord4fv\0"
    "glTexCoord4i\0"
    "glTexCoord4iv\0"
    "glTexCoord4s\0"
    "glTexCoord4sv\0"
    "glVertex2d\0"
    "glVertex2dv\0"
    "glVertex2f\0"
    "glVertex2fv\0"
    "glVertex2i\0"
    "glVertex2iv\0"
    "glVertex2s\0"
    "glVertex2sv\0"
    "glVertex3d\0"
    "glVertex3dv\0"
    "glVertex3f\0"
    "glVertex3fv\0"
    "glVertex3i\0"
    "glVertex3iv\0"
    "glVertex3s\0"
    "glVertex3sv\0"
    "glVertex4d\0"
    "glVertex4dv\0"
    "glVertex4f\0"
    "glVertex4fv\0"
    "glVertex4i\0"
    "glVertex4iv\0"
    "glVertex4s\0"
    "glVertex4sv\0"
    "glClipPlane\0"
    "glColorMaterial\0"
    "glCullFace\0"
    "glFogf\0"
    "glFogfv\0"
    "glFogi\0"
    "glFogiv\0"
    "glFrontFace\0"
    "glHint\0"
    "glLightf\0"
    "glLightfv\0"
    "glLighti\0"
    "glLightiv\0"
    "glLightModelf\0"
    "glLightModelfv\0"
    "glLightModeli\0"
    "glLightModeliv\0"
    "glLineStipple\0"
    "glLineWidth\0"
    "glMaterialf\0"
    "glMaterialfv\0"
    "glMateriali\0"
    "glMaterialiv\0"
    "glPointSize\0"
    "glPolygonMode\0"
    "glPolygonStipple\0"
    "glScissor\0"
    "glShadeModel\0"
    "glTexParameterf\0"
    "glTexParameterfv\0"
    "glTexParameteri\0"
    "glTexParameteriv\0"
    "glTexImage1D\0"
    "glTexImage2D\0"
    "glTexEnvf\0"
    "glTexEnvfv\0"
    "glTexEnvi\0"
    "glTexEnviv\0"
    "glTexGend\0"
    "glTexGendv\0"
    "glTexGenf\0"
    "glTexGenfv\0"
    "glTexGeni\0"
    "glTexGeniv\0"
    "glFeedbackBuffer\0"
    "glSelectBuffer\0"
    "glRenderMode\0"
    "glInitNames\0"
    "glLoadName\0"
    "glPassThrough\0"
    "glPopName\0"
    "glPushName\0"
    "glDrawBuffer\0"
    "glClear\0"
    "glClearAccum\0"
    "glClearIndex\0"
    "glClearColor\0"
    "glClearStencil\0"
    "glClearDepth\0"
    "glStencilMask\0"
    "glColorMask\0"
    "glDepthMask\0"
    "glIndexMask\0"
    "glAccum\0"
    "glDisable\0"
    "glEnable\0"
    "glFinish\0"
    "glFlush\0"
    "glPopAttrib\0"
    "glPushAttrib\0"
    "glMap1d\0"
    "glMap1f\0"
    "glMap2d\0"
    "glMap2f\0"
    "glMapGrid1d\0"
    "glMapGrid1f\0"
    "glMapGrid2d\0"
    "glMapGrid2f\0"
    "glEvalCoord1d\0"
    "glEvalCoord1dv\0"
    "glEvalCoord1f\0"
    "glEvalCoord1fv\0"
    "glEvalCoord2d\0"
    "glEvalCoord2dv\0"
    "glEvalCoord2f\0"
    "glEvalCoord2fv\0"
    "glEvalMesh1\0"
    "glEvalPoint1\0"
    "glEvalMesh2\0"
    "glEvalPoint2\0"
    "glAlphaFunc\0"
    "glBlendFunc\0"
    "glLogicOp\0"
    "glStencilFunc\0"
    "glStencilOp\0"
    "glDepthFunc\0"
    "glPixelZoom\0"
    "glPixelTransferf\0"
    "glPixelTransferi\0"
    "glPixelStoref\0"
    "glPixelStorei\0"
    "glPixelMapfv\0"
    "glPixelMapuiv\0"
    "glPixelMapusv\0"
    "glReadBuffer\0"
    "glCopyPixels\0"
    "glReadPixels\0"
    "glDrawPixels\0"
    "glGetBooleanv\0"
    "glGetClipPlane\0"
    "glGetDoublev\0"
    "glGetError\0"
    "glGetFloatv\0"
    "glGetIntegerv\0"
    "glGetLightfv\0"
    "glGetLightiv\0"
    "glGetMapdv\0"
    "glGetMapfv\0"
    "glGetMapiv\0"
    "glGetMaterialfv\0"
    "glGetMaterialiv\0"
    "glGetPixelMapfv\0"
    "glGetPixelMapuiv\0"
    "glGetPixelMapusv\0"
    "glGetPolygonStipple\0"
    "glGetString\0"
    "glGetTexEnvfv\0"
    "glGetTexEnviv\0"
    "glGetTexGendv\0"
    "glGetTexGenfv\0"
    "glGetTexGeniv\0"
    "glGetTexImage\0"
    "glGetTexParameterfv\0"
    "glGetTexParameteriv\0"
    "glGetTexLevelParameterfv\0"
    "glGetTexLevelParameteriv\0"
    "glIsEnabled\0"
    "glIsList\0"
    "glDepthRange\0"
    "glFrustum\0"
    "glLoadIdentity\0"
    "glLoadMatrixf\0"
    "glLoadMatrixd\0"
    "glMatrixMode\0"
    "glMultMatrixf\0"
    "glMultMatrixd\0"
    "glOrtho\0"
    "glPopMatrix\0"
    "glPushMatrix\0"
    "glRotated\0"
    "glRotatef\0"
    "glScaled\0"
    "glScalef\0"
    "glTranslated\0"
    "glTranslatef\0"
    "glViewport\0"
    "glArrayElement\0"
    "glBindTexture\0"
    "glColorPointer\0"
    "glDisableClientState\0"
    "glDrawArrays\0"
    "glDrawElements\0"
    "glEdgeFlagPointer\0"
    "glEnableClientState\0"
    "glIndexPointer\0"
    "glIndexub\0"
    "glIndexubv\0"
    "glInterleavedArrays\0"
    "glNormalPointer\0"
    "glPolygonOffset\0"
    "glTexCoordPointer\0"
    "glVertexPointer\0"
    "glAreTexturesResident\0"
    "glCopyTexImage1D\0"
    "glCopyTexImage2D\0"
    "glCopyTexSubImage1D\0"
    "glCopyTexSubImage2D\0"
    "glDeleteTextures\0"
    "glGenTextures\0"
    "glGetPointerv\0"
    "glIsTexture\0"
    "glPrioritizeTextures\0"
    "glTexSubImage1D\0"
    "glTexSubImage2D\0"
    "glPopClientAttrib\0"
    "glPushClientAttrib\0"
    "glBlendColor\0"
    "glBlendEquation\0"
    "glDrawRangeElements\0"
    "glColorTable\0"
    "glColorTableParameterfv\0"
    "glColorTableParameteriv\0"
    "glCopyColorTable\0"
    "glGetColorTable\0"
    "glGetColorTableParameterfv\0"
    "glGetColorTableParameteriv\0"
    "glColorSubTable\0"
    "glCopyColorSubTable\0"
    "glConvolutionFilter1D\0"
    "glConvolutionFilter2D\0"
    "glConvolutionParameterf\0"
    "glConvolutionParameterfv\0"
    "glConvolutionParameteri\0"
    "glConvolutionParameteriv\0"
    "glCopyConvolutionFilter1D\0"
    "glCopyConvolutionFilter2D\0"
    "glGetConvolutionFilter\0"
    "glGetConvolutionParameterfv\0"
    "glGetConvolutionParameteriv\0"
    "glGetSeparableFilter\0"
    "glSeparableFilter2D\0"
    "glGetHistogram\0"
    "glGetHistogramParameterfv\0"
    "glGetHistogramParameteriv\0"
    "glGetMinmax\0"
    "glGetMinmaxParameterfv\0"
    "glGetMinmaxParameteriv\0"
    "glHistogram\0"
    "glMinmax\0"
    "glResetHistogram\0"
    "glResetMinmax\0"
    "glTexImage3D\0"
    "glTexSubImage3D\0"
    "glCopyTexSubImage3D\0"
    "glActiveTextureARB\0"
    "glClientActiveTextureARB\0"
    "glMultiTexCoord1dARB\0"
    "glMultiTexCoord1dvARB\0"
    "glMultiTexCoord1fARB\0"
    "glMultiTexCoord1fvARB\0"
    "glMultiTexCoord1iARB\0"
    "glMultiTexCoord1ivARB\0"
    "glMultiTexCoord1sARB\0"
    "glMultiTexCoord1svARB\0"
    "glMultiTexCoord2dARB\0"
    "glMultiTexCoord2dvARB\0"
    "glMultiTexCoord2fARB\0"
    "glMultiTexCoord2fvARB\0"
    "glMultiTexCoord2iARB\0"
    "glMultiTexCoord2ivARB\0"
    "glMultiTexCoord2sARB\0"
    "glMultiTexCoord2svARB\0"
    "glMultiTexCoord3dARB\0"
    "glMultiTexCoord3dvARB\0"
    "glMultiTexCoord3fARB\0"
    "glMultiTexCoord3fvARB\0"
    "glMultiTexCoord3iARB\0"
    "glMultiTexCoord3ivARB\0"
    "glMultiTexCoord3sARB\0"
    "glMultiTexCoord3svARB\0"
    "glMultiTexCoord4dARB\0"
    "glMultiTexCoord4dvARB\0"
    "glMultiTexCoord4fARB\0"
    "glMultiTexCoord4fvARB\0"
    "glMultiTexCoord4iARB\0"
    "glMultiTexCoord4ivARB\0"
    "glMultiTexCoord4sARB\0"
    "glMultiTexCoord4svARB\0"
    "glAttachShader\0"
    "glCreateProgram\0"
    "glCreateShader\0"
    "glDeleteProgram\0"
    "glDeleteShader\0"
    "glDetachShader\0"
    "glGetAttachedShaders\0"
    "glGetProgramInfoLog\0"
    "glGetProgramiv\0"
    "glGetShaderInfoLog\0"
    "glGetShaderiv\0"
    "glIsProgram\0"
    "glIsShader\0"
    "glStencilFuncSeparate\0"
    "glStencilMaskSeparate\0"
    "glStencilOpSeparate\0"
    "glUniformMatrix2x3fv\0"
    "glUniformMatrix2x4fv\0"
    "glUniformMatrix3x2fv\0"
    "glUniformMatrix3x4fv\0"
    "glUniformMatrix4x2fv\0"
    "glUniformMatrix4x3fv\0"
    "glClearBufferfi\0"
    "glClearBufferfv\0"
    "glClearBufferiv\0"
    "glClearBufferuiv\0"
    "glGetStringi\0"
    "glFramebufferTexture\0"
    "glGetBufferParameteri64v\0"
    "glGetInteger64i_v\0"
    "glLoadTransposeMatrixdARB\0"
    "glLoadTransposeMatrixfARB\0"
    "glMultTransposeMatrixdARB\0"
    "glMultTransposeMatrixfARB\0"
    "glSampleCoverageARB\0"
    "glCompressedTexImage1DARB\0"
    "glCompressedTexImage2DARB\0"
    "glCompressedTexImage3DARB\0"
    "glCompressedTexSubImage1DARB\0"
    "glCompressedTexSubImage2DARB\0"
    "glCompressedTexSubImage3DARB\0"
    "glGetCompressedTexImageARB\0"
    "glDisableVertexAttribArrayARB\0"
    "glEnableVertexAttribArrayARB\0"
    "glGetProgramEnvParameterdvARB\0"
    "glGetProgramEnvParameterfvARB\0"
    "glGetProgramLocalParameterdvARB\0"
    "glGetProgramLocalParameterfvARB\0"
    "glGetProgramStringARB\0"
    "glGetProgramivARB\0"
    "glGetVertexAttribdvARB\0"
    "glGetVertexAttribfvARB\0"
    "glGetVertexAttribivARB\0"
    "glProgramEnvParameter4dARB\0"
    "glProgramEnvParameter4dvARB\0"
    "glProgramEnvParameter4fARB\0"
    "glProgramEnvParameter4fvARB\0"
    "glProgramLocalParameter4dARB\0"
    "glProgramLocalParameter4dvARB\0"
    "glProgramLocalParameter4fARB\0"
    "glProgramLocalParameter4fvARB\0"
    "glProgramStringARB\0"
    "glVertexAttrib1dARB\0"
    "glVertexAttrib1dvARB\0"
    "glVertexAttrib1fARB\0"
    "glVertexAttrib1fvARB\0"
    "glVertexAttrib1sARB\0"
    "glVertexAttrib1svARB\0"
    "glVertexAttrib2dARB\0"
    "glVertexAttrib2dvARB\0"
    "glVertexAttrib2fARB\0"
    "glVertexAttrib2fvARB\0"
    "glVertexAttrib2sARB\0"
    "glVertexAttrib2svARB\0"
    "glVertexAttrib3dARB\0"
    "glVertexAttrib3dvARB\0"
    "glVertexAttrib3fARB\0"
    "glVertexAttrib3fvARB\0"
    "glVertexAttrib3sARB\0"
    "glVertexAttrib3svARB\0"
    "glVertexAttrib4NbvARB\0"
    "glVertexAttrib4NivARB\0"
    "glVertexAttrib4NsvARB\0"
    "glVertexAttrib4NubARB\0"
    "glVertexAttrib4NubvARB\0"
    "glVertexAttrib4NuivARB\0"
    "glVertexAttrib4NusvARB\0"
    "glVertexAttrib4bvARB\0"
    "glVertexAttrib4dARB\0"
    "glVertexAttrib4dvARB\0"
    "glVertexAttrib4fARB\0"
    "glVertexAttrib4fvARB\0"
    "glVertexAttrib4ivARB\0"
    "glVertexAttrib4sARB\0"
    "glVertexAttrib4svARB\0"
    "glVertexAttrib4ubvARB\0"
    "glVertexAttrib4uivARB\0"
    "glVertexAttrib4usvARB\0"
    "glVertexAttribPointerARB\0"
    "glBindBufferARB\0"
    "glBufferDataARB\0"
    "glBufferSubDataARB\0"
    "glDeleteBuffersARB\0"
    "glGenBuffersARB\0"
    "glGetBufferParameterivARB\0"
    "glGetBufferPointervARB\0"
    "glGetBufferSubDataARB\0"
    "glIsBufferARB\0"
    "glMapBufferARB\0"
    "glUnmapBufferARB\0"
    "glBeginQueryARB\0"
    "glDeleteQueriesARB\0"
    "glEndQueryARB\0"
    "glGenQueriesARB\0"
    "glGetQueryObjectivARB\0"
    "glGetQueryObjectuivARB\0"
    "glGetQueryivARB\0"
    "glIsQueryARB\0"
    "glAttachObjectARB\0"
    "glCompileShaderARB\0"
    "glCreateProgramObjectARB\0"
    "glCreateShaderObjectARB\0"
    "glDeleteObjectARB\0"
    "glDetachObjectARB\0"
    "glGetActiveUniformARB\0"
    "glGetAttachedObjectsARB\0"
    "glGetHandleARB\0"
    "glGetInfoLogARB\0"
    "glGetObjectParameterfvARB\0"
    "glGetObjectParameterivARB\0"
    "glGetShaderSourceARB\0"
    "glGetUniformLocationARB\0"
    "glGetUniformfvARB\0"
    "glGetUniformivARB\0"
    "glLinkProgramARB\0"
    "glShaderSourceARB\0"
    "glUniform1fARB\0"
    "glUniform1fvARB\0"
    "glUniform1iARB\0"
    "glUniform1ivARB\0"
    "glUniform2fARB\0"
    "glUniform2fvARB\0"
    "glUniform2iARB\0"
    "glUniform2ivARB\0"
    "glUniform3fARB\0"
    "glUniform3fvARB\0"
    "glUniform3iARB\0"
    "glUniform3ivARB\0"
    "glUniform4fARB\0"
    "glUniform4fvARB\0"
    "glUniform4iARB\0"
    "glUniform4ivARB\0"
    "glUniformMatrix2fvARB\0"
    "glUniformMatrix3fvARB\0"
    "glUniformMatrix4fvARB\0"
    "glUseProgramObjectARB\0"
    "glValidateProgramARB\0"
    "glBindAttribLocationARB\0"
    "glGetActiveAttribARB\0"
    "glGetAttribLocationARB\0"
    "glDrawBuffersARB\0"
    "glClampColorARB\0"
    "glDrawArraysInstancedARB\0"
    "glDrawElementsInstancedARB\0"
    "glRenderbufferStorageMultisample\0"
    "glFramebufferTextureFaceARB\0"
    "glVertexAttribDivisorARB\0"
    "glFlushMappedBufferRange\0"
    "glMapBufferRange\0"
    "glTexBufferARB\0"
    "glBindVertexArray\0"
    "glGenVertexArrays\0"
    "glGetActiveUniformBlockName\0"
    "glGetActiveUniformBlockiv\0"
    "glGetActiveUniformName\0"
    "glGetActiveUniformsiv\0"
    "glGetUniformBlockIndex\0"
    "glGetUniformIndices\0"
    "glUniformBlockBinding\0"
    "glCopyBufferSubData\0"
    "glClientWaitSync\0"
    "glDeleteSync\0"
    "glFenceSync\0"
    "glGetInteger64v\0"
    "glGetSynciv\0"
    "glIsSync\0"
    "glWaitSync\0"
    "glDrawElementsBaseVertex\0"
    "glDrawElementsInstancedBaseVertex\0"
    "glDrawRangeElementsBaseVertex\0"
    "glMultiDrawElementsBaseVertex\0"
    "glBlendEquationSeparateiARB\0"
    "glBlendEquationiARB\0"
    "glBlendFuncSeparateiARB\0"
    "glBlendFunciARB\0"
    "glBindFragDataLocationIndexed\0"
    "glGetFragDataIndex\0"
    "glBindSampler\0"
    "glDeleteSamplers\0"
    "glGenSamplers\0"
    "glGetSamplerParameterIiv\0"
    "glGetSamplerParameterIuiv\0"
    "glGetSamplerParameterfv\0"
    "glGetSamplerParameteriv\0"
    "glIsSampler\0"
    "glSamplerParameterIiv\0"
    "glSamplerParameterIuiv\0"
    "glSamplerParameterf\0"
    "glSamplerParameterfv\0"
    "glSamplerParameteri\0"
    "glSamplerParameteriv\0"
    "glQueryCounter\0"
    "glColorP3ui\0"
    "glColorP3uiv\0"
    "glColorP4ui\0"
    "glColorP4uiv\0"
    "glMultiTexCoordP1ui\0"
    "glMultiTexCoordP1uiv\0"
    "glMultiTexCoordP2ui\0"
    "glMultiTexCoordP2uiv\0"
    "glMultiTexCoordP3ui\0"
    "glMultiTexCoordP3uiv\0"
    "glMultiTexCoordP4ui\0"
    "glMultiTexCoordP4uiv\0"
    "glNormalP3ui\0"
    "glNormalP3uiv\0"
    "glSecondaryColorP3ui\0"
    "glSecondaryColorP3uiv\0"
    "glTexCoordP1ui\0"
    "glTexCoordP1uiv\0"
    "glTexCoordP2ui\0"
    "glTexCoordP2uiv\0"
    "glTexCoordP3ui\0"
    "glTexCoordP3uiv\0"
    "glTexCoordP4ui\0"
    "glTexCoordP4uiv\0"
    "glVertexAttribP1ui\0"
    "glVertexAttribP1uiv\0"
    "glVertexAttribP2ui\0"
    "glVertexAttribP2uiv\0"
    "glVertexAttribP3ui\0"
    "glVertexAttribP3uiv\0"
    "glVertexAttribP4ui\0"
    "glVertexAttribP4uiv\0"
    "glVertexP2ui\0"
    "glVertexP2uiv\0"
    "glVertexP3ui\0"
    "glVertexP3uiv\0"
    "glVertexP4ui\0"
    "glVertexP4uiv\0"
    "glBindTransformFeedback\0"
    "glDeleteTransformFeedbacks\0"
    "glDrawTransformFeedback\0"
    "glGenTransformFeedbacks\0"
    "glIsTransformFeedback\0"
    "glPauseTransformFeedback\0"
    "glResumeTransformFeedback\0"
    "glBeginQueryIndexed\0"
    "glDrawTransformFeedbackStream\0"
    "glEndQueryIndexed\0"
    "glGetQueryIndexediv\0"
    "glClearDepthf\0"
    "glDepthRangef\0"
    "glGetShaderPrecisionFormat\0"
    "glReleaseShaderCompiler\0"
    "glShaderBinary\0"
    "glGetProgramBinary\0"
    "glProgramBinary\0"
    "glProgramParameteri\0"
    "glDebugMessageCallbackARB\0"
    "glDebugMessageControlARB\0"
    "glDebugMessageInsertARB\0"
    "glGetDebugMessageLogARB\0"
    "glGetGraphicsResetStatusARB\0"
    "glGetnColorTableARB\0"
    "glGetnCompressedTexImageARB\0"
    "glGetnConvolutionFilterARB\0"
    "glGetnHistogramARB\0"
    "glGetnMapdvARB\0"
    "glGetnMapfvARB\0"
    "glGetnMapivARB\0"
    "glGetnMinmaxARB\0"
    "glGetnPixelMapfvARB\0"
    "glGetnPixelMapuivARB\0"
    "glGetnPixelMapusvARB\0"
    "glGetnPolygonStippleARB\0"
    "glGetnSeparableFilterARB\0"
    "glGetnTexImageARB\0"
    "glGetnUniformdvARB\0"
    "glGetnUniformfvARB\0"
    "glGetnUniformivARB\0"
    "glGetnUniformuivARB\0"
    "glReadnPixelsARB\0"
    "glDrawArraysInstancedBaseInstance\0"
    "glDrawElementsInstancedBaseInstance\0"
    "glDrawElementsInstancedBaseVertexBaseInstance\0"
    "glDrawTransformFeedbackInstanced\0"
    "glDrawTransformFeedbackStreamInstanced\0"
    "glTexStorage1D\0"
    "glTexStorage2D\0"
    "glTexStorage3D\0"
    "glTextureStorage1DEXT\0"
    "glTextureStorage2DEXT\0"
    "glTextureStorage3DEXT\0"
    "glInvalidateBufferData\0"
    "glInvalidateBufferSubData\0"
    "glInvalidateFramebuffer\0"
    "glInvalidateSubFramebuffer\0"
    "glInvalidateTexImage\0"
    "glInvalidateTexSubImage\0"
    "glPolygonOffsetEXT\0"
    "glDrawTexfOES\0"
    "glDrawTexfvOES\0"
    "glDrawTexiOES\0"
    "glDrawTexivOES\0"
    "glDrawTexsOES\0"
    "glDrawTexsvOES\0"
    "glDrawTexxOES\0"
    "glDrawTexxvOES\0"
    "glPointSizePointerOES\0"
    "glQueryMatrixxOES\0"
    "glSampleMaskSGIS\0"
    "glSamplePatternSGIS\0"
    "glColorPointerEXT\0"
    "glEdgeFlagPointerEXT\0"
    "glIndexPointerEXT\0"
    "glNormalPointerEXT\0"
    "glTexCoordPointerEXT\0"
    "glVertexPointerEXT\0"
    "glPointParameterfEXT\0"
    "glPointParameterfvEXT\0"
    "glLockArraysEXT\0"
    "glUnlockArraysEXT\0"
    "glSecondaryColor3bEXT\0"
    "glSecondaryColor3bvEXT\0"
    "glSecondaryColor3dEXT\0"
    "glSecondaryColor3dvEXT\0"
    "glSecondaryColor3fEXT\0"
    "glSecondaryColor3fvEXT\0"
    "glSecondaryColor3iEXT\0"
    "glSecondaryColor3ivEXT\0"
    "glSecondaryColor3sEXT\0"
    "glSecondaryColor3svEXT\0"
    "glSecondaryColor3ubEXT\0"
    "glSecondaryColor3ubvEXT\0"
    "glSecondaryColor3uiEXT\0"
    "glSecondaryColor3uivEXT\0"
    "glSecondaryColor3usEXT\0"
    "glSecondaryColor3usvEXT\0"
    "glSecondaryColorPointerEXT\0"
    "glMultiDrawArraysEXT\0"
    "glMultiDrawElementsEXT\0"
    "glFogCoordPointerEXT\0"
    "glFogCoorddEXT\0"
    "glFogCoorddvEXT\0"
    "glFogCoordfEXT\0"
    "glFogCoordfvEXT\0"
    "glBlendFuncSeparateEXT\0"
    "glResizeBuffersMESA\0"
    "glWindowPos2dMESA\0"
    "glWindowPos2dvMESA\0"
    "glWindowPos2fMESA\0"
    "glWindowPos2fvMESA\0"
    "glWindowPos2iMESA\0"
    "glWindowPos2ivMESA\0"
    "glWindowPos2sMESA\0"
    "glWindowPos2svMESA\0"
    "glWindowPos3dMESA\0"
    "glWindowPos3dvMESA\0"
    "glWindowPos3fMESA\0"
    "glWindowPos3fvMESA\0"
    "glWindowPos3iMESA\0"
    "glWindowPos3ivMESA\0"
    "glWindowPos3sMESA\0"
    "glWindowPos3svMESA\0"
    "glWindowPos4dMESA\0"
    "glWindowPos4dvMESA\0"
    "glWindowPos4fMESA\0"
    "glWindowPos4fvMESA\0"
    "glWindowPos4iMESA\0"
    "glWindowPos4ivMESA\0"
    "glWindowPos4sMESA\0"
    "glWindowPos4svMESA\0"
    "glMultiModeDrawArraysIBM\0"
    "glMultiModeDrawElementsIBM\0"
    "glAreProgramsResidentNV\0"
    "glBindProgramNV\0"
    "glDeleteProgramsNV\0"
    "glExecuteProgramNV\0"
    "glGenProgramsNV\0"
    "glGetProgramParameterdvNV\0"
    "glGetProgramParameterfvNV\0"
    "glGetProgramStringNV\0"
    "glGetProgramivNV\0"
    "glGetTrackMatrixivNV\0"
    "glGetVertexAttribPointervNV\0"
    "glGetVertexAttribdvNV\0"
    "glGetVertexAttribfvNV\0"
    "glGetVertexAttribivNV\0"
    "glIsProgramNV\0"
    "glLoadProgramNV\0"
    "glProgramParameters4dvNV\0"
    "glProgramParameters4fvNV\0"
    "glRequestResidentProgramsNV\0"
    "glTrackMatrixNV\0"
    "glVertexAttrib1dNV\0"
    "glVertexAttrib1dvNV\0"
    "glVertexAttrib1fNV\0"
    "glVertexAttrib1fvNV\0"
    "glVertexAttrib1sNV\0"
    "glVertexAttrib1svNV\0"
    "glVertexAttrib2dNV\0"
    "glVertexAttrib2dvNV\0"
    "glVertexAttrib2fNV\0"
    "glVertexAttrib2fvNV\0"
    "glVertexAttrib2sNV\0"
    "glVertexAttrib2svNV\0"
    "glVertexAttrib3dNV\0"
    "glVertexAttrib3dvNV\0"
    "glVertexAttrib3fNV\0"
    "glVertexAttrib3fvNV\0"
    "glVertexAttrib3sNV\0"
    "glVertexAttrib3svNV\0"
    "glVertexAttrib4dNV\0"
    "glVertexAttrib4dvNV\0"
    "glVertexAttrib4fNV\0"
    "glVertexAttrib4fvNV\0"
    "glVertexAttrib4sNV\0"
    "glVertexAttrib4svNV\0"
    "glVertexAttrib4ubNV\0"
    "glVertexAttrib4ubvNV\0"
    "glVertexAttribPointerNV\0"
    "glVertexAttribs1dvNV\0"
    "glVertexAttribs1fvNV\0"
    "glVertexAttribs1svNV\0"
    "glVertexAttribs2dvNV\0"
    "glVertexAttribs2fvNV\0"
    "glVertexAttribs2svNV\0"
    "glVertexAttribs3dvNV\0"
    "glVertexAttribs3fvNV\0"
    "glVertexAttribs3svNV\0"
    "glVertexAttribs4dvNV\0"
    "glVertexAttribs4fvNV\0"
    "glVertexAttribs4svNV\0"
    "glVertexAttribs4ubvNV\0"
    "glGetTexBumpParameterfvATI\0"
    "glGetTexBumpParameterivATI\0"
    "glTexBumpParameterfvATI\0"
    "glTexBumpParameterivATI\0"
    "glAlphaFragmentOp1ATI\0"
    "glAlphaFragmentOp2ATI\0"
    "glAlphaFragmentOp3ATI\0"
    "glBeginFragmentShaderATI\0"
    "glBindFragmentShaderATI\0"
    "glColorFragmentOp1ATI\0"
    "glColorFragmentOp2ATI\0"
    "glColorFragmentOp3ATI\0"
    "glDeleteFragmentShaderATI\0"
    "glEndFragmentShaderATI\0"
    "glGenFragmentShadersATI\0"
    "glPassTexCoordATI\0"
    "glSampleMapATI\0"
    "glSetFragmentShaderConstantATI\0"
    "glPointParameteriNV\0"
    "glPointParameterivNV\0"
    "glActiveStencilFaceEXT\0"
    "glBindVertexArrayAPPLE\0"
    "glDeleteVertexArraysAPPLE\0"
    "glGenVertexArraysAPPLE\0"
    "glIsVertexArrayAPPLE\0"
    "glGetProgramNamedParameterdvNV\0"
    "glGetProgramNamedParameterfvNV\0"
    "glProgramNamedParameter4dNV\0"
    "glProgramNamedParameter4dvNV\0"
    "glProgramNamedParameter4fNV\0"
    "glProgramNamedParameter4fvNV\0"
    "glPrimitiveRestartIndexNV\0"
    "glPrimitiveRestartNV\0"
    "glAlphaFuncxOES\0"
    "glClearColorxOES\0"
    "glClearDepthxOES\0"
    "glClipPlanexOES\0"
    "glColor4xOES\0"
    "glDepthRangexOES\0"
    "glFogxOES\0"
    "glFogxvOES\0"
    "glFrustumxOES\0"
    "glGetClipPlanexOES\0"
    "glGetFixedvOES\0"
    "glGetLightxvOES\0"
    "glGetMaterialxvOES\0"
    "glGetTexEnvxvOES\0"
    "glGetTexGenxvOES\0"
    "glGetTexParameterxvOES\0"
    "glLightModelxOES\0"
    "glLightModelxvOES\0"
    "glLightxOES\0"
    "glLightxvOES\0"
    "glLineWidthxOES\0"
    "glLoadMatrixxOES\0"
    "glMaterialxOES\0"
    "glMaterialxvOES\0"
    "glMultMatrixxOES\0"
    "glMultiTexCoord4xOES\0"
    "glNormal3xOES\0"
    "glOrthoxOES\0"
    "glPointParameterxOES\0"
    "glPointParameterxvOES\0"
    "glPointSizexOES\0"
    "glPolygonOffsetxOES\0"
    "glRotatexOES\0"
    "glSampleCoveragexOES\0"
    "glScalexOES\0"
    "glTexEnvxOES\0"
    "glTexEnvxvOES\0"
    "glTexGenxOES\0"
    "glTexGenxvOES\0"
    "glTexParameterxOES\0"
    "glTexParameterxvOES\0"
    "glTranslatexOES\0"
    "glClipPlanefOES\0"
    "glFrustumfOES\0"
    "glGetClipPlanefOES\0"
    "glOrthofOES\0"
    "glDepthBoundsEXT\0"
    "glBlendEquationSeparateEXT\0"
    "glBindFramebufferEXT\0"
    "glBindRenderbufferEXT\0"
    "glCheckFramebufferStatusEXT\0"
    "glDeleteFramebuffersEXT\0"
    "glDeleteRenderbuffersEXT\0"
    "glFramebufferRenderbufferEXT\0"
    "glFramebufferTexture1DEXT\0"
    "glFramebufferTexture2DEXT\0"
    "glFramebufferTexture3DEXT\0"
    "glGenFramebuffersEXT\0"
    "glGenRenderbuffersEXT\0"
    "glGenerateMipmapEXT\0"
    "glGetFramebufferAttachmentParameterivEXT\0"
    "glGetRenderbufferParameterivEXT\0"
    "glIsFramebufferEXT\0"
    "glIsRenderbufferEXT\0"
    "glRenderbufferStorageEXT\0"
    "glBlitFramebufferEXT\0"
    "glBufferParameteriAPPLE\0"
    "glFlushMappedBufferRangeAPPLE\0"
    "glBindFragDataLocationEXT\0"
    "glGetFragDataLocationEXT\0"
    "glGetUniformuivEXT\0"
    "glGetVertexAttribIivEXT\0"
    "glGetVertexAttribIuivEXT\0"
    "glUniform1uiEXT\0"
    "glUniform1uivEXT\0"
    "glUniform2uiEXT\0"
    "glUniform2uivEXT\0"
    "glUniform3uiEXT\0"
    "glUniform3uivEXT\0"
    "glUniform4uiEXT\0"
    "glUniform4uivEXT\0"
    "glVertexAttribI1iEXT\0"
    "glVertexAttribI1ivEXT\0"
    "glVertexAttribI1uiEXT\0"
    "glVertexAttribI1uivEXT\0"
    "glVertexAttribI2iEXT\0"
    "glVertexAttribI2ivEXT\0"
    "glVertexAttribI2uiEXT\0"
    "glVertexAttribI2uivEXT\0"
    "glVertexAttribI3iEXT\0"
    "glVertexAttribI3ivEXT\0"
    "glVertexAttribI3uiEXT\0"
    "glVertexAttribI3uivEXT\0"
    "glVertexAttribI4bvEXT\0"
    "glVertexAttribI4iEXT\0"
    "glVertexAttribI4ivEXT\0"
    "glVertexAttribI4svEXT\0"
    "glVertexAttribI4ubvEXT\0"
    "glVertexAttribI4uiEXT\0"
    "glVertexAttribI4uivEXT\0"
    "glVertexAttribI4usvEXT\0"
    "glVertexAttribIPointerEXT\0"
    "glFramebufferTextureLayerEXT\0"
    "glColorMaskIndexedEXT\0"
    "glDisableIndexedEXT\0"
    "glEnableIndexedEXT\0"
    "glGetBooleanIndexedvEXT\0"
    "glGetIntegerIndexedvEXT\0"
    "glIsEnabledIndexedEXT\0"
    "glClearColorIiEXT\0"
    "glClearColorIuiEXT\0"
    "glGetTexParameterIivEXT\0"
    "glGetTexParameterIuivEXT\0"
    "glTexParameterIivEXT\0"
    "glTexParameterIuivEXT\0"
    "glBeginConditionalRenderNV\0"
    "glEndConditionalRenderNV\0"
    "glBeginTransformFeedbackEXT\0"
    "glBindBufferBaseEXT\0"
    "glBindBufferOffsetEXT\0"
    "glBindBufferRangeEXT\0"
    "glEndTransformFeedbackEXT\0"
    "glGetTransformFeedbackVaryingEXT\0"
    "glTransformFeedbackVaryingsEXT\0"
    "glProvokingVertexEXT\0"
    "glGetObjectParameterivAPPLE\0"
    "glObjectPurgeableAPPLE\0"
    "glObjectUnpurgeableAPPLE\0"
    "glActiveProgramEXT\0"
    "glCreateShaderProgramEXT\0"
    "glUseShaderProgramEXT\0"
    "glTextureBarrierNV\0"
    "glStencilFuncSeparateATI\0"
    "glProgramEnvParameters4fvEXT\0"
    "glProgramLocalParameters4fvEXT\0"
    "glGetQueryObjecti64vEXT\0"
    "glGetQueryObjectui64vEXT\0"
    "glEGLImageTargetRenderbufferStorageOES\0"
    "glEGLImageTargetTexture2DOES\0"
    "glTexGenfOES\0"
    "glTexGenfvOES\0"
    "glTexGeniOES\0"
    "glTexGenivOES\0"
    "glReadBufferNV\0"
    "glGetTexGenfvOES\0"
    "glGetTexGenivOES\0"
    "glArrayElementEXT\0"
    "glBindTextureEXT\0"
    "glDrawArraysEXT\0"
    "glAreTexturesResidentEXT\0"
    "glCopyTexImage1DEXT\0"
    "glCopyTexImage2DEXT\0"
    "glCopyTexSubImage1DEXT\0"
    "glCopyTexSubImage2DEXT\0"
    "glDeleteTexturesEXT\0"
    "glGenTexturesEXT\0"
    "glGetPointervEXT\0"
    "glIsTextureEXT\0"
    "glPrioritizeTexturesEXT\0"
    "glTexSubImage1DEXT\0"
    "glTexSubImage2DEXT\0"
    "glBlendColorEXT\0"
    "glBlendEquationEXT\0"
    "glBlendEquationOES\0"
    "glDrawRangeElementsEXT\0"
    "glColorTableSGI\0"
    "glColorTableEXT\0"
    "glColorTableParameterfvSGI\0"
    "glColorTableParameterivSGI\0"
    "glCopyColorTableSGI\0"
    "glGetColorTableSGI\0"
    "glGetColorTableEXT\0"
    "glGetColorTableParameterfvSGI\0"
    "glGetColorTableParameterfvEXT\0"
    "glGetColorTableParameterivSGI\0"
    "glGetColorTableParameterivEXT\0"
    "glColorSubTableEXT\0"
    "glCopyColorSubTableEXT\0"
    "glConvolutionFilter1DEXT\0"
    "glConvolutionFilter2DEXT\0"
    "glConvolutionParameterfEXT\0"
    "glConvolutionParameterfvEXT\0"
    "glConvolutionParameteriEXT\0"
    "glConvolutionParameterivEXT\0"
    "glCopyConvolutionFilter1DEXT\0"
    "glCopyConvolutionFilter2DEXT\0"
    "glGetConvolutionFilterEXT\0"
    "glGetConvolutionParameterfvEXT\0"
    "glGetConvolutionParameterivEXT\0"
    "glGetSeparableFilterEXT\0"
    "glSeparableFilter2DEXT\0"
    "glGetHistogramEXT\0"
    "glGetHistogramParameterfvEXT\0"
    "glGetHistogramParameterivEXT\0"
    "glGetMinmaxEXT\0"
    "glGetMinmaxParameterfvEXT\0"
    "glGetMinmaxParameterivEXT\0"
    "glHistogramEXT\0"
    "glMinmaxEXT\0"
    "glResetHistogramEXT\0"
    "glResetMinmaxEXT\0"
    "glTexImage3DEXT\0"
    "glTexImage3DOES\0"
    "glTexSubImage3DEXT\0"
    "glTexSubImage3DOES\0"
    "glCopyTexSubImage3DEXT\0"
    "glCopyTexSubImage3DOES\0"
    "glActiveTexture\0"
    "glClientActiveTexture\0"
    "glMultiTexCoord1d\0"
    "glMultiTexCoord1dv\0"
    "glMultiTexCoord1f\0"
    "glMultiTexCoord1fv\0"
    "glMultiTexCoord1i\0"
    "glMultiTexCoord1iv\0"
    "glMultiTexCoord1s\0"
    "glMultiTexCoord1sv\0"
    "glMultiTexCoord2d\0"
    "glMultiTexCoord2dv\0"
    "glMultiTexCoord2f\0"
    "glMultiTexCoord2fv\0"
    "glMultiTexCoord2i\0"
    "glMultiTexCoord2iv\0"
    "glMultiTexCoord2s\0"
    "glMultiTexCoord2sv\0"
    "glMultiTexCoord3d\0"
    "glMultiTexCoord3dv\0"
    "glMultiTexCoord3f\0"
    "glMultiTexCoord3fv\0"
    "glMultiTexCoord3i\0"
    "glMultiTexCoord3iv\0"
    "glMultiTexCoord3s\0"
    "glMultiTexCoord3sv\0"
    "glMultiTexCoord4d\0"
    "glMultiTexCoord4dv\0"
    "glMultiTexCoord4f\0"
    "glMultiTexCoord4fv\0"
    "glMultiTexCoord4i\0"
    "glMultiTexCoord4iv\0"
    "glMultiTexCoord4s\0"
    "glMultiTexCoord4sv\0"
    "glStencilOpSeparateATI\0"
    "glFramebufferTextureARB\0"
    "glLoadTransposeMatrixd\0"
    "glLoadTransposeMatrixf\0"
    "glMultTransposeMatrixd\0"
    "glMultTransposeMatrixf\0"
    "glSampleCoverage\0"
    "glCompressedTexImage1D\0"
    "glCompressedTexImage2D\0"
    "glCompressedTexImage3D\0"
    "glCompressedTexImage3DOES\0"
    "glCompressedTexSubImage1D\0"
    "glCompressedTexSubImage2D\0"
    "glCompressedTexSubImage3D\0"
    "glCompressedTexSubImage3DOES\0"
    "glGetCompressedTexImage\0"
    "glDisableVertexAttribArray\0"
    "glEnableVertexAttribArray\0"
    "glGetVertexAttribdv\0"
    "glGetVertexAttribfv\0"
    "glGetVertexAttribiv\0"
    "glProgramParameter4dNV\0"
    "glProgramParameter4dvNV\0"
    "glProgramParameter4fNV\0"
    "glProgramParameter4fvNV\0"
    "glVertexAttrib1d\0"
    "glVertexAttrib1dv\0"
    "glVertexAttrib1f\0"
    "glVertexAttrib1fv\0"
    "glVertexAttrib1s\0"
    "glVertexAttrib1sv\0"
    "glVertexAttrib2d\0"
    "glVertexAttrib2dv\0"
    "glVertexAttrib2f\0"
    "glVertexAttrib2fv\0"
    "glVertexAttrib2s\0"
    "glVertexAttrib2sv\0"
    "glVertexAttrib3d\0"
    "glVertexAttrib3dv\0"
    "glVertexAttrib3f\0"
    "glVertexAttrib3fv\0"
    "glVertexAttrib3s\0"
    "glVertexAttrib3sv\0"
    "glVertexAttrib4Nbv\0"
    "glVertexAttrib4Niv\0"
    "glVertexAttrib4Nsv\0"
    "glVertexAttrib4Nub\0"
    "glVertexAttrib4Nubv\0"
    "glVertexAttrib4Nuiv\0"
    "glVertexAttrib4Nusv\0"
    "glVertexAttrib4bv\0"
    "glVertexAttrib4d\0"
    "glVertexAttrib4dv\0"
    "glVertexAttrib4f\0"
    "glVertexAttrib4fv\0"
    "glVertexAttrib4iv\0"
    "glVertexAttrib4s\0"
    "glVertexAttrib4sv\0"
    "glVertexAttrib4ubv\0"
    "glVertexAttrib4uiv\0"
    "glVertexAttrib4usv\0"
    "glVertexAttribPointer\0"
    "glBindBuffer\0"
    "glBufferData\0"
    "glBufferSubData\0"
    "glDeleteBuffers\0"
    "glGenBuffers\0"
    "glGetBufferParameteriv\0"
    "glGetBufferPointerv\0"
    "glGetBufferPointervOES\0"
    "glGetBufferSubData\0"
    "glIsBuffer\0"
    "glMapBuffer\0"
    "glMapBufferOES\0"
    "glUnmapBuffer\0"
    "glUnmapBufferOES\0"
    "glBeginQuery\0"
    "glDeleteQueries\0"
    "glEndQuery\0"
    "glGenQueries\0"
    "glGetQueryObjectiv\0"
    "glGetQueryObjectuiv\0"
    "glGetQueryiv\0"
    "glIsQuery\0"
    "glCompileShader\0"
    "glGetActiveUniform\0"
    "glGetShaderSource\0"
    "glGetUniformLocation\0"
    "glGetUniformfv\0"
    "glGetUniformiv\0"
    "glLinkProgram\0"
    "glShaderSource\0"
    "glUniform1f\0"
    "glUniform1fv\0"
    "glUniform1i\0"
    "glUniform1iv\0"
    "glUniform2f\0"
    "glUniform2fv\0"
    "glUniform2i\0"
    "glUniform2iv\0"
    "glUniform3f\0"
    "glUniform3fv\0"
    "glUniform3i\0"
    "glUniform3iv\0"
    "glUniform4f\0"
    "glUniform4fv\0"
    "glUniform4i\0"
    "glUniform4iv\0"
    "glUniformMatrix2fv\0"
    "glUniformMatrix3fv\0"
    "glUniformMatrix4fv\0"
    "glUseProgram\0"
    "glValidateProgram\0"
    "glBindAttribLocation\0"
    "glGetActiveAttrib\0"
    "glGetAttribLocation\0"
    "glDrawBuffers\0"
    "glDrawBuffersATI\0"
    "glDrawBuffersNV\0"
    "glClampColor\0"
    "glDrawArraysInstancedEXT\0"
    "glDrawArraysInstanced\0"
    "glDrawElementsInstancedEXT\0"
    "glDrawElementsInstanced\0"
    "glRenderbufferStorageMultisampleEXT\0"
    "glVertexAttribDivisor\0"
    "glFlushMappedBufferRangeEXT\0"
    "glMapBufferRangeEXT\0"
    "glTexBuffer\0"
    "glBindVertexArrayOES\0"
    "glGenVertexArraysOES\0"
    "glBlendEquationSeparateIndexedAMD\0"
    "glBlendEquationIndexedAMD\0"
    "glBlendFuncSeparateIndexedAMD\0"
    "glBlendFuncIndexedAMD\0"
    "glClearDepthfOES\0"
    "glDepthRangefOES\0"
    "glGetProgramBinaryOES\0"
    "glProgramBinaryOES\0"
    "glProgramParameteriARB\0"
    "glSampleMaskEXT\0"
    "glSamplePatternEXT\0"
    "glPointParameterf\0"
    "glPointParameterfARB\0"
    "glPointParameterfSGIS\0"
    "glPointParameterfv\0"
    "glPointParameterfvARB\0"
    "glPointParameterfvSGIS\0"
    "glSecondaryColor3b\0"
    "glSecondaryColor3bv\0"
    "glSecondaryColor3d\0"
    "glSecondaryColor3dv\0"
    "glSecondaryColor3f\0"
    "glSecondaryColor3fv\0"
    "glSecondaryColor3i\0"
    "glSecondaryColor3iv\0"
    "glSecondaryColor3s\0"
    "glSecondaryColor3sv\0"
    "glSecondaryColor3ub\0"
    "glSecondaryColor3ubv\0"
    "glSecondaryColor3ui\0"
    "glSecondaryColor3uiv\0"
    "glSecondaryColor3us\0"
    "glSecondaryColor3usv\0"
    "glSecondaryColorPointer\0"
    "glMultiDrawArrays\0"
    "glMultiDrawElements\0"
    "glFogCoordPointer\0"
    "glFogCoordd\0"
    "glFogCoorddv\0"
    "glFogCoordf\0"
    "glFogCoordfv\0"
    "glBlendFuncSeparate\0"
    "glBlendFuncSeparateINGR\0"
    "glBlendFuncSeparateOES\0"
    "glWindowPos2d\0"
    "glWindowPos2dARB\0"
    "glWindowPos2dv\0"
    "glWindowPos2dvARB\0"
    "glWindowPos2f\0"
    "glWindowPos2fARB\0"
    "glWindowPos2fv\0"
    "glWindowPos2fvARB\0"
    "glWindowPos2i\0"
    "glWindowPos2iARB\0"
    "glWindowPos2iv\0"
    "glWindowPos2ivARB\0"
    "glWindowPos2s\0"
    "glWindowPos2sARB\0"
    "glWindowPos2sv\0"
    "glWindowPos2svARB\0"
    "glWindowPos3d\0"
    "glWindowPos3dARB\0"
    "glWindowPos3dv\0"
    "glWindowPos3dvARB\0"
    "glWindowPos3f\0"
    "glWindowPos3fARB\0"
    "glWindowPos3fv\0"
    "glWindowPos3fvARB\0"
    "glWindowPos3i\0"
    "glWindowPos3iARB\0"
    "glWindowPos3iv\0"
    "glWindowPos3ivARB\0"
    "glWindowPos3s\0"
    "glWindowPos3sARB\0"
    "glWindowPos3sv\0"
    "glWindowPos3svARB\0"
    "glBindProgramARB\0"
    "glDeleteProgramsARB\0"
    "glGenProgramsARB\0"
    "glGetVertexAttribPointerv\0"
    "glGetVertexAttribPointervARB\0"
    "glIsProgramARB\0"
    "glPointParameteri\0"
    "glPointParameteriv\0"
    "glDeleteVertexArrays\0"
    "glDeleteVertexArraysOES\0"
    "glIsVertexArray\0"
    "glIsVertexArrayOES\0"
    "glPrimitiveRestartIndex\0"
    "glAlphaFuncx\0"
    "glClearColorx\0"
    "glClearDepthx\0"
    "glClipPlanex\0"
    "glColor4x\0"
    "glDepthRangex\0"
    "glFogx\0"
    "glFogxv\0"
    "glFrustumx\0"
    "glGetClipPlanex\0"
    "glGetFixedv\0"
    "glGetLightxv\0"
    "glGetMaterialxv\0"
    "glGetTexEnvxv\0"
    "glGetTexParameterxv\0"
    "glLightModelx\0"
    "glLightModelxv\0"
    "glLightx\0"
    "glLightxv\0"
    "glLineWidthx\0"
    "glLoadMatrixx\0"
    "glMaterialx\0"
    "glMaterialxv\0"
    "glMultMatrixx\0"
    "glMultiTexCoord4x\0"
    "glNormal3x\0"
    "glOrthox\0"
    "glPointParameterx\0"
    "glPointParameterxv\0"
    "glPointSizex\0"
    "glPolygonOffsetx\0"
    "glRotatex\0"
    "glSampleCoveragex\0"
    "glScalex\0"
    "glTexEnvx\0"
    "glTexEnvxv\0"
    "glTexParameterx\0"
    "glTexParameterxv\0"
    "glTranslatex\0"
    "glClipPlanef\0"
    "glFrustumf\0"
    "glGetClipPlanef\0"
    "glOrthof\0"
    "glBlendEquationSeparate\0"
    "glBlendEquationSeparateATI\0"
    "glBlendEquationSeparateOES\0"
    "glBindFramebuffer\0"
    "glBindFramebufferOES\0"
    "glBindRenderbuffer\0"
    "glBindRenderbufferOES\0"
    "glCheckFramebufferStatus\0"
    "glCheckFramebufferStatusOES\0"
    "glDeleteFramebuffers\0"
    "glDeleteFramebuffersOES\0"
    "glDeleteRenderbuffers\0"
    "glDeleteRenderbuffersOES\0"
    "glFramebufferRenderbuffer\0"
    "glFramebufferRenderbufferOES\0"
    "glFramebufferTexture1D\0"
    "glFramebufferTexture2D\0"
    "glFramebufferTexture2DOES\0"
    "glFramebufferTexture3D\0"
    "glFramebufferTexture3DOES\0"
    "glGenFramebuffers\0"
    "glGenFramebuffersOES\0"
    "glGenRenderbuffers\0"
    "glGenRenderbuffersOES\0"
    "glGenerateMipmap\0"
    "glGenerateMipmapOES\0"
    "glGetFramebufferAttachmentParameteriv\0"
    "glGetFramebufferAttachmentParameterivOES\0"
    "glGetRenderbufferParameteriv\0"
    "glGetRenderbufferParameterivOES\0"
    "glIsFramebuffer\0"
    "glIsFramebufferOES\0"
    "glIsRenderbuffer\0"
    "glIsRenderbufferOES\0"
    "glRenderbufferStorage\0"
    "glRenderbufferStorageOES\0"
    "glBlitFramebuffer\0"
    "glBindFragDataLocation\0"
    "glGetFragDataLocation\0"
    "glGetUniformuiv\0"
    "glGetVertexAttribIiv\0"
    "glGetVertexAttribIuiv\0"
    "glUniform1ui\0"
    "glUniform1uiv\0"
    "glUniform2ui\0"
    "glUniform2uiv\0"
    "glUniform3ui\0"
    "glUniform3uiv\0"
    "glUniform4ui\0"
    "glUniform4uiv\0"
    "glVertexAttribI1i\0"
    "glVertexAttribI1iv\0"
    "glVertexAttribI1ui\0"
    "glVertexAttribI1uiv\0"
    "glVertexAttribI2i\0"
    "glVertexAttribI2iv\0"
    "glVertexAttribI2ui\0"
    "glVertexAttribI2uiv\0"
    "glVertexAttribI3i\0"
    "glVertexAttribI3iv\0"
    "glVertexAttribI3ui\0"
    "glVertexAttribI3uiv\0"
    "glVertexAttribI4bv\0"
    "glVertexAttribI4i\0"
    "glVertexAttribI4iv\0"
    "glVertexAttribI4sv\0"
    "glVertexAttribI4ubv\0"
    "glVertexAttribI4ui\0"
    "glVertexAttribI4uiv\0"
    "glVertexAttribI4usv\0"
    "glVertexAttribIPointer\0"
    "glFramebufferTextureLayer\0"
    "glFramebufferTextureLayerARB\0"
    "glColorMaski\0"
    "glDisablei\0"
    "glEnablei\0"
    "glGetBooleani_v\0"
    "glGetIntegeri_v\0"
    "glIsEnabledi\0"
    "glGetTexParameterIiv\0"
    "glGetTexParameterIuiv\0"
    "glTexParameterIiv\0"
    "glTexParameterIuiv\0"
    "glBeginConditionalRender\0"
    "glEndConditionalRender\0"
    "glBeginTransformFeedback\0"
    "glBindBufferBase\0"
    "glBindBufferRange\0"
    "glEndTransformFeedback\0"
    "glGetTransformFeedbackVarying\0"
    "glTransformFeedbackVaryings\0"
    "glProvokingVertex\0"
    "glGetQueryObjecti64v\0"
    "glGetQueryObjectui64v\0"
    ;


#ifdef USE_MGL_NAMESPACE
#define gl_dispatch_stub_343 mgl_dispatch_stub_343
#define gl_dispatch_stub_344 mgl_dispatch_stub_344
#define gl_dispatch_stub_345 mgl_dispatch_stub_345
#define gl_dispatch_stub_356 mgl_dispatch_stub_356
#define gl_dispatch_stub_357 mgl_dispatch_stub_357
#define gl_dispatch_stub_358 mgl_dispatch_stub_358
#define gl_dispatch_stub_359 mgl_dispatch_stub_359
#define gl_dispatch_stub_361 mgl_dispatch_stub_361
#define gl_dispatch_stub_362 mgl_dispatch_stub_362
#define gl_dispatch_stub_363 mgl_dispatch_stub_363
#define gl_dispatch_stub_364 mgl_dispatch_stub_364
#define gl_dispatch_stub_365 mgl_dispatch_stub_365
#define gl_dispatch_stub_366 mgl_dispatch_stub_366
#define gl_dispatch_stub_619 mgl_dispatch_stub_619
#define gl_dispatch_stub_674 mgl_dispatch_stub_674
#define gl_dispatch_stub_675 mgl_dispatch_stub_675
#define gl_dispatch_stub_676 mgl_dispatch_stub_676
#define gl_dispatch_stub_719 mgl_dispatch_stub_719
#define gl_dispatch_stub_720 mgl_dispatch_stub_720
#define gl_dispatch_stub_721 mgl_dispatch_stub_721
#define gl_dispatch_stub_722 mgl_dispatch_stub_722
#define gl_dispatch_stub_723 mgl_dispatch_stub_723
#define gl_dispatch_stub_724 mgl_dispatch_stub_724
#define gl_dispatch_stub_725 mgl_dispatch_stub_725
#define gl_dispatch_stub_726 mgl_dispatch_stub_726
#define gl_dispatch_stub_727 mgl_dispatch_stub_727
#define gl_dispatch_stub_728 mgl_dispatch_stub_728
#define gl_dispatch_stub_729 mgl_dispatch_stub_729
#define gl_dispatch_stub_730 mgl_dispatch_stub_730
#define gl_dispatch_stub_791 mgl_dispatch_stub_791
#define gl_dispatch_stub_792 mgl_dispatch_stub_792
#define gl_dispatch_stub_873 mgl_dispatch_stub_873
#define gl_dispatch_stub_874 mgl_dispatch_stub_874
#define gl_dispatch_stub_875 mgl_dispatch_stub_875
#define gl_dispatch_stub_876 mgl_dispatch_stub_876
#define gl_dispatch_stub_877 mgl_dispatch_stub_877
#define gl_dispatch_stub_886 mgl_dispatch_stub_886
#define gl_dispatch_stub_887 mgl_dispatch_stub_887
#define gl_dispatch_stub_888 mgl_dispatch_stub_888
#define gl_dispatch_stub_889 mgl_dispatch_stub_889
#define gl_dispatch_stub_890 mgl_dispatch_stub_890
#define gl_dispatch_stub_891 mgl_dispatch_stub_891
#define gl_dispatch_stub_892 mgl_dispatch_stub_892
#define gl_dispatch_stub_893 mgl_dispatch_stub_893
#define gl_dispatch_stub_894 mgl_dispatch_stub_894
#define gl_dispatch_stub_895 mgl_dispatch_stub_895
#define gl_dispatch_stub_896 mgl_dispatch_stub_896
#define gl_dispatch_stub_897 mgl_dispatch_stub_897
#define gl_dispatch_stub_898 mgl_dispatch_stub_898
#define gl_dispatch_stub_899 mgl_dispatch_stub_899
#define gl_dispatch_stub_900 mgl_dispatch_stub_900
#define gl_dispatch_stub_901 mgl_dispatch_stub_901
#define gl_dispatch_stub_902 mgl_dispatch_stub_902
#define gl_dispatch_stub_903 mgl_dispatch_stub_903
#define gl_dispatch_stub_904 mgl_dispatch_stub_904
#define gl_dispatch_stub_905 mgl_dispatch_stub_905
#define gl_dispatch_stub_906 mgl_dispatch_stub_906
#define gl_dispatch_stub_907 mgl_dispatch_stub_907
#define gl_dispatch_stub_908 mgl_dispatch_stub_908
#define gl_dispatch_stub_909 mgl_dispatch_stub_909
#define gl_dispatch_stub_910 mgl_dispatch_stub_910
#define gl_dispatch_stub_911 mgl_dispatch_stub_911
#define gl_dispatch_stub_912 mgl_dispatch_stub_912
#define gl_dispatch_stub_913 mgl_dispatch_stub_913
#define gl_dispatch_stub_914 mgl_dispatch_stub_914
#define gl_dispatch_stub_915 mgl_dispatch_stub_915
#define gl_dispatch_stub_916 mgl_dispatch_stub_916
#define gl_dispatch_stub_917 mgl_dispatch_stub_917
#define gl_dispatch_stub_918 mgl_dispatch_stub_918
#define gl_dispatch_stub_919 mgl_dispatch_stub_919
#define gl_dispatch_stub_920 mgl_dispatch_stub_920
#define gl_dispatch_stub_921 mgl_dispatch_stub_921
#define gl_dispatch_stub_922 mgl_dispatch_stub_922
#define gl_dispatch_stub_923 mgl_dispatch_stub_923
#define gl_dispatch_stub_924 mgl_dispatch_stub_924
#define gl_dispatch_stub_925 mgl_dispatch_stub_925
#define gl_dispatch_stub_926 mgl_dispatch_stub_926
#define gl_dispatch_stub_927 mgl_dispatch_stub_927
#define gl_dispatch_stub_928 mgl_dispatch_stub_928
#define gl_dispatch_stub_929 mgl_dispatch_stub_929
#define gl_dispatch_stub_930 mgl_dispatch_stub_930
#define gl_dispatch_stub_931 mgl_dispatch_stub_931
#define gl_dispatch_stub_932 mgl_dispatch_stub_932
#define gl_dispatch_stub_933 mgl_dispatch_stub_933
#define gl_dispatch_stub_951 mgl_dispatch_stub_951
#define gl_dispatch_stub_952 mgl_dispatch_stub_952
#define gl_dispatch_stub_953 mgl_dispatch_stub_953
#define gl_dispatch_stub_1018 mgl_dispatch_stub_1018
#define gl_dispatch_stub_1019 mgl_dispatch_stub_1019
#define gl_dispatch_stub_1020 mgl_dispatch_stub_1020
#define gl_dispatch_stub_1021 mgl_dispatch_stub_1021
#define gl_dispatch_stub_1022 mgl_dispatch_stub_1022
#endif /* USE_MGL_NAMESPACE */


#if defined(NEED_FUNCTION_POINTER) || defined(GLX_INDIRECT_RENDERING)
void GLAPIENTRY gl_dispatch_stub_343(GLenum target, GLenum format, GLenum type, GLvoid * table);
void GLAPIENTRY gl_dispatch_stub_344(GLenum target, GLenum pname, GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_345(GLenum target, GLenum pname, GLint * params);
void GLAPIENTRY gl_dispatch_stub_356(GLenum target, GLenum format, GLenum type, GLvoid * image);
void GLAPIENTRY gl_dispatch_stub_357(GLenum target, GLenum pname, GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_358(GLenum target, GLenum pname, GLint * params);
void GLAPIENTRY gl_dispatch_stub_359(GLenum target, GLenum format, GLenum type, GLvoid * row, GLvoid * column, GLvoid * span);
void GLAPIENTRY gl_dispatch_stub_361(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid * values);
void GLAPIENTRY gl_dispatch_stub_362(GLenum target, GLenum pname, GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_363(GLenum target, GLenum pname, GLint * params);
void GLAPIENTRY gl_dispatch_stub_364(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid * values);
void GLAPIENTRY gl_dispatch_stub_365(GLenum target, GLenum pname, GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_366(GLenum target, GLenum pname, GLint * params);
void GLAPIENTRY gl_dispatch_stub_619(GLuint id, GLenum target);
void GLAPIENTRY gl_dispatch_stub_674(GLuint program, GLsizei bufSize, GLsizei * length, GLenum * binaryFormat, GLvoid * binary);
void GLAPIENTRY gl_dispatch_stub_675(GLuint program, GLenum binaryFormat, const GLvoid * binary, GLsizei length);
void GLAPIENTRY gl_dispatch_stub_676(GLuint program, GLenum pname, GLint value);
void GLAPIENTRY gl_dispatch_stub_719(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void GLAPIENTRY gl_dispatch_stub_720(const GLfloat * coords);
void GLAPIENTRY gl_dispatch_stub_721(GLint x, GLint y, GLint z, GLint width, GLint height);
void GLAPIENTRY gl_dispatch_stub_722(const GLint * coords);
void GLAPIENTRY gl_dispatch_stub_723(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void GLAPIENTRY gl_dispatch_stub_724(const GLshort * coords);
void GLAPIENTRY gl_dispatch_stub_725(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void GLAPIENTRY gl_dispatch_stub_726(const GLfixed * coords);
void GLAPIENTRY gl_dispatch_stub_727(GLenum type, GLsizei stride, const GLvoid * pointer);
GLbitfield GLAPIENTRY gl_dispatch_stub_728(GLfixed * mantissa, GLint * exponent);
void GLAPIENTRY gl_dispatch_stub_729(GLclampf value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_730(GLenum pattern);
void GLAPIENTRY gl_dispatch_stub_791(const GLenum * mode, const GLint * first, const GLsizei * count, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_792(const GLenum * mode, const GLsizei * count, GLenum type, const GLvoid * const * indices, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_873(GLenum face);
void GLAPIENTRY gl_dispatch_stub_874(GLuint array);
void GLAPIENTRY gl_dispatch_stub_875(GLsizei n, const GLuint * arrays);
void GLAPIENTRY gl_dispatch_stub_876(GLsizei n, GLuint * arrays);
GLboolean GLAPIENTRY gl_dispatch_stub_877(GLuint array);
void GLAPIENTRY gl_dispatch_stub_886(GLenum func, GLclampx ref);
void GLAPIENTRY gl_dispatch_stub_887(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void GLAPIENTRY gl_dispatch_stub_888(GLclampx depth);
void GLAPIENTRY gl_dispatch_stub_889(GLenum plane, const GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_890(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void GLAPIENTRY gl_dispatch_stub_891(GLclampx zNear, GLclampx zFar);
void GLAPIENTRY gl_dispatch_stub_892(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_893(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_894(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_895(GLenum plane, GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_896(GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_897(GLenum light, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_898(GLenum face, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_899(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_900(GLenum coord, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_901(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_902(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_903(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_904(GLenum light, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_905(GLenum light, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_906(GLfixed width);
void GLAPIENTRY gl_dispatch_stub_907(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_908(GLenum face, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_909(GLenum face, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_910(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_911(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
void GLAPIENTRY gl_dispatch_stub_912(GLfixed nx, GLfixed ny, GLfixed nz);
void GLAPIENTRY gl_dispatch_stub_913(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_914(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_915(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_916(GLfixed size);
void GLAPIENTRY gl_dispatch_stub_917(GLfixed factor, GLfixed units);
void GLAPIENTRY gl_dispatch_stub_918(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_919(GLclampx value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_920(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_921(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_922(GLenum target, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_923(GLenum coord, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_924(GLenum coord, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_925(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_926(GLenum target, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_927(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_928(GLenum plane, const GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_929(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_930(GLenum plane, GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_931(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_932(GLclampd zmin, GLclampd zmax);
void GLAPIENTRY gl_dispatch_stub_933(GLenum modeRGB, GLenum modeA);
void GLAPIENTRY gl_dispatch_stub_951(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
void GLAPIENTRY gl_dispatch_stub_952(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_953(GLenum target, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1018(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
void GLAPIENTRY gl_dispatch_stub_1019(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_1020(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_1021(GLuint id, GLenum pname, GLint64EXT * params);
void GLAPIENTRY gl_dispatch_stub_1022(GLuint id, GLenum pname, GLuint64EXT * params);
#endif /* defined(NEED_FUNCTION_POINTER) || defined(GLX_INDIRECT_RENDERING) */

static const glprocs_table_t static_functions[] = {
    NAME_FUNC_OFFSET(    0, glNewList, glNewList, NULL, 0),
    NAME_FUNC_OFFSET(   10, glEndList, glEndList, NULL, 1),
    NAME_FUNC_OFFSET(   20, glCallList, glCallList, NULL, 2),
    NAME_FUNC_OFFSET(   31, glCallLists, glCallLists, NULL, 3),
    NAME_FUNC_OFFSET(   43, glDeleteLists, glDeleteLists, NULL, 4),
    NAME_FUNC_OFFSET(   57, glGenLists, glGenLists, NULL, 5),
    NAME_FUNC_OFFSET(   68, glListBase, glListBase, NULL, 6),
    NAME_FUNC_OFFSET(   79, glBegin, glBegin, NULL, 7),
    NAME_FUNC_OFFSET(   87, glBitmap, glBitmap, NULL, 8),
    NAME_FUNC_OFFSET(   96, glColor3b, glColor3b, NULL, 9),
    NAME_FUNC_OFFSET(  106, glColor3bv, glColor3bv, NULL, 10),
    NAME_FUNC_OFFSET(  117, glColor3d, glColor3d, NULL, 11),
    NAME_FUNC_OFFSET(  127, glColor3dv, glColor3dv, NULL, 12),
    NAME_FUNC_OFFSET(  138, glColor3f, glColor3f, NULL, 13),
    NAME_FUNC_OFFSET(  148, glColor3fv, glColor3fv, NULL, 14),
    NAME_FUNC_OFFSET(  159, glColor3i, glColor3i, NULL, 15),
    NAME_FUNC_OFFSET(  169, glColor3iv, glColor3iv, NULL, 16),
    NAME_FUNC_OFFSET(  180, glColor3s, glColor3s, NULL, 17),
    NAME_FUNC_OFFSET(  190, glColor3sv, glColor3sv, NULL, 18),
    NAME_FUNC_OFFSET(  201, glColor3ub, glColor3ub, NULL, 19),
    NAME_FUNC_OFFSET(  212, glColor3ubv, glColor3ubv, NULL, 20),
    NAME_FUNC_OFFSET(  224, glColor3ui, glColor3ui, NULL, 21),
    NAME_FUNC_OFFSET(  235, glColor3uiv, glColor3uiv, NULL, 22),
    NAME_FUNC_OFFSET(  247, glColor3us, glColor3us, NULL, 23),
    NAME_FUNC_OFFSET(  258, glColor3usv, glColor3usv, NULL, 24),
    NAME_FUNC_OFFSET(  270, glColor4b, glColor4b, NULL, 25),
    NAME_FUNC_OFFSET(  280, glColor4bv, glColor4bv, NULL, 26),
    NAME_FUNC_OFFSET(  291, glColor4d, glColor4d, NULL, 27),
    NAME_FUNC_OFFSET(  301, glColor4dv, glColor4dv, NULL, 28),
    NAME_FUNC_OFFSET(  312, glColor4f, glColor4f, NULL, 29),
    NAME_FUNC_OFFSET(  322, glColor4fv, glColor4fv, NULL, 30),
    NAME_FUNC_OFFSET(  333, glColor4i, glColor4i, NULL, 31),
    NAME_FUNC_OFFSET(  343, glColor4iv, glColor4iv, NULL, 32),
    NAME_FUNC_OFFSET(  354, glColor4s, glColor4s, NULL, 33),
    NAME_FUNC_OFFSET(  364, glColor4sv, glColor4sv, NULL, 34),
    NAME_FUNC_OFFSET(  375, glColor4ub, glColor4ub, NULL, 35),
    NAME_FUNC_OFFSET(  386, glColor4ubv, glColor4ubv, NULL, 36),
    NAME_FUNC_OFFSET(  398, glColor4ui, glColor4ui, NULL, 37),
    NAME_FUNC_OFFSET(  409, glColor4uiv, glColor4uiv, NULL, 38),
    NAME_FUNC_OFFSET(  421, glColor4us, glColor4us, NULL, 39),
    NAME_FUNC_OFFSET(  432, glColor4usv, glColor4usv, NULL, 40),
    NAME_FUNC_OFFSET(  444, glEdgeFlag, glEdgeFlag, NULL, 41),
    NAME_FUNC_OFFSET(  455, glEdgeFlagv, glEdgeFlagv, NULL, 42),
    NAME_FUNC_OFFSET(  467, glEnd, glEnd, NULL, 43),
    NAME_FUNC_OFFSET(  473, glIndexd, glIndexd, NULL, 44),
    NAME_FUNC_OFFSET(  482, glIndexdv, glIndexdv, NULL, 45),
    NAME_FUNC_OFFSET(  492, glIndexf, glIndexf, NULL, 46),
    NAME_FUNC_OFFSET(  501, glIndexfv, glIndexfv, NULL, 47),
    NAME_FUNC_OFFSET(  511, glIndexi, glIndexi, NULL, 48),
    NAME_FUNC_OFFSET(  520, glIndexiv, glIndexiv, NULL, 49),
    NAME_FUNC_OFFSET(  530, glIndexs, glIndexs, NULL, 50),
    NAME_FUNC_OFFSET(  539, glIndexsv, glIndexsv, NULL, 51),
    NAME_FUNC_OFFSET(  549, glNormal3b, glNormal3b, NULL, 52),
    NAME_FUNC_OFFSET(  560, glNormal3bv, glNormal3bv, NULL, 53),
    NAME_FUNC_OFFSET(  572, glNormal3d, glNormal3d, NULL, 54),
    NAME_FUNC_OFFSET(  583, glNormal3dv, glNormal3dv, NULL, 55),
    NAME_FUNC_OFFSET(  595, glNormal3f, glNormal3f, NULL, 56),
    NAME_FUNC_OFFSET(  606, glNormal3fv, glNormal3fv, NULL, 57),
    NAME_FUNC_OFFSET(  618, glNormal3i, glNormal3i, NULL, 58),
    NAME_FUNC_OFFSET(  629, glNormal3iv, glNormal3iv, NULL, 59),
    NAME_FUNC_OFFSET(  641, glNormal3s, glNormal3s, NULL, 60),
    NAME_FUNC_OFFSET(  652, glNormal3sv, glNormal3sv, NULL, 61),
    NAME_FUNC_OFFSET(  664, glRasterPos2d, glRasterPos2d, NULL, 62),
    NAME_FUNC_OFFSET(  678, glRasterPos2dv, glRasterPos2dv, NULL, 63),
    NAME_FUNC_OFFSET(  693, glRasterPos2f, glRasterPos2f, NULL, 64),
    NAME_FUNC_OFFSET(  707, glRasterPos2fv, glRasterPos2fv, NULL, 65),
    NAME_FUNC_OFFSET(  722, glRasterPos2i, glRasterPos2i, NULL, 66),
    NAME_FUNC_OFFSET(  736, glRasterPos2iv, glRasterPos2iv, NULL, 67),
    NAME_FUNC_OFFSET(  751, glRasterPos2s, glRasterPos2s, NULL, 68),
    NAME_FUNC_OFFSET(  765, glRasterPos2sv, glRasterPos2sv, NULL, 69),
    NAME_FUNC_OFFSET(  780, glRasterPos3d, glRasterPos3d, NULL, 70),
    NAME_FUNC_OFFSET(  794, glRasterPos3dv, glRasterPos3dv, NULL, 71),
    NAME_FUNC_OFFSET(  809, glRasterPos3f, glRasterPos3f, NULL, 72),
    NAME_FUNC_OFFSET(  823, glRasterPos3fv, glRasterPos3fv, NULL, 73),
    NAME_FUNC_OFFSET(  838, glRasterPos3i, glRasterPos3i, NULL, 74),
    NAME_FUNC_OFFSET(  852, glRasterPos3iv, glRasterPos3iv, NULL, 75),
    NAME_FUNC_OFFSET(  867, glRasterPos3s, glRasterPos3s, NULL, 76),
    NAME_FUNC_OFFSET(  881, glRasterPos3sv, glRasterPos3sv, NULL, 77),
    NAME_FUNC_OFFSET(  896, glRasterPos4d, glRasterPos4d, NULL, 78),
    NAME_FUNC_OFFSET(  910, glRasterPos4dv, glRasterPos4dv, NULL, 79),
    NAME_FUNC_OFFSET(  925, glRasterPos4f, glRasterPos4f, NULL, 80),
    NAME_FUNC_OFFSET(  939, glRasterPos4fv, glRasterPos4fv, NULL, 81),
    NAME_FUNC_OFFSET(  954, glRasterPos4i, glRasterPos4i, NULL, 82),
    NAME_FUNC_OFFSET(  968, glRasterPos4iv, glRasterPos4iv, NULL, 83),
    NAME_FUNC_OFFSET(  983, glRasterPos4s, glRasterPos4s, NULL, 84),
    NAME_FUNC_OFFSET(  997, glRasterPos4sv, glRasterPos4sv, NULL, 85),
    NAME_FUNC_OFFSET( 1012, glRectd, glRectd, NULL, 86),
    NAME_FUNC_OFFSET( 1020, glRectdv, glRectdv, NULL, 87),
    NAME_FUNC_OFFSET( 1029, glRectf, glRectf, NULL, 88),
    NAME_FUNC_OFFSET( 1037, glRectfv, glRectfv, NULL, 89),
    NAME_FUNC_OFFSET( 1046, glRecti, glRecti, NULL, 90),
    NAME_FUNC_OFFSET( 1054, glRectiv, glRectiv, NULL, 91),
    NAME_FUNC_OFFSET( 1063, glRects, glRects, NULL, 92),
    NAME_FUNC_OFFSET( 1071, glRectsv, glRectsv, NULL, 93),
    NAME_FUNC_OFFSET( 1080, glTexCoord1d, glTexCoord1d, NULL, 94),
    NAME_FUNC_OFFSET( 1093, glTexCoord1dv, glTexCoord1dv, NULL, 95),
    NAME_FUNC_OFFSET( 1107, glTexCoord1f, glTexCoord1f, NULL, 96),
    NAME_FUNC_OFFSET( 1120, glTexCoord1fv, glTexCoord1fv, NULL, 97),
    NAME_FUNC_OFFSET( 1134, glTexCoord1i, glTexCoord1i, NULL, 98),
    NAME_FUNC_OFFSET( 1147, glTexCoord1iv, glTexCoord1iv, NULL, 99),
    NAME_FUNC_OFFSET( 1161, glTexCoord1s, glTexCoord1s, NULL, 100),
    NAME_FUNC_OFFSET( 1174, glTexCoord1sv, glTexCoord1sv, NULL, 101),
    NAME_FUNC_OFFSET( 1188, glTexCoord2d, glTexCoord2d, NULL, 102),
    NAME_FUNC_OFFSET( 1201, glTexCoord2dv, glTexCoord2dv, NULL, 103),
    NAME_FUNC_OFFSET( 1215, glTexCoord2f, glTexCoord2f, NULL, 104),
    NAME_FUNC_OFFSET( 1228, glTexCoord2fv, glTexCoord2fv, NULL, 105),
    NAME_FUNC_OFFSET( 1242, glTexCoord2i, glTexCoord2i, NULL, 106),
    NAME_FUNC_OFFSET( 1255, glTexCoord2iv, glTexCoord2iv, NULL, 107),
    NAME_FUNC_OFFSET( 1269, glTexCoord2s, glTexCoord2s, NULL, 108),
    NAME_FUNC_OFFSET( 1282, glTexCoord2sv, glTexCoord2sv, NULL, 109),
    NAME_FUNC_OFFSET( 1296, glTexCoord3d, glTexCoord3d, NULL, 110),
    NAME_FUNC_OFFSET( 1309, glTexCoord3dv, glTexCoord3dv, NULL, 111),
    NAME_FUNC_OFFSET( 1323, glTexCoord3f, glTexCoord3f, NULL, 112),
    NAME_FUNC_OFFSET( 1336, glTexCoord3fv, glTexCoord3fv, NULL, 113),
    NAME_FUNC_OFFSET( 1350, glTexCoord3i, glTexCoord3i, NULL, 114),
    NAME_FUNC_OFFSET( 1363, glTexCoord3iv, glTexCoord3iv, NULL, 115),
    NAME_FUNC_OFFSET( 1377, glTexCoord3s, glTexCoord3s, NULL, 116),
    NAME_FUNC_OFFSET( 1390, glTexCoord3sv, glTexCoord3sv, NULL, 117),
    NAME_FUNC_OFFSET( 1404, glTexCoord4d, glTexCoord4d, NULL, 118),
    NAME_FUNC_OFFSET( 1417, glTexCoord4dv, glTexCoord4dv, NULL, 119),
    NAME_FUNC_OFFSET( 1431, glTexCoord4f, glTexCoord4f, NULL, 120),
    NAME_FUNC_OFFSET( 1444, glTexCoord4fv, glTexCoord4fv, NULL, 121),
    NAME_FUNC_OFFSET( 1458, glTexCoord4i, glTexCoord4i, NULL, 122),
    NAME_FUNC_OFFSET( 1471, glTexCoord4iv, glTexCoord4iv, NULL, 123),
    NAME_FUNC_OFFSET( 1485, glTexCoord4s, glTexCoord4s, NULL, 124),
    NAME_FUNC_OFFSET( 1498, glTexCoord4sv, glTexCoord4sv, NULL, 125),
    NAME_FUNC_OFFSET( 1512, glVertex2d, glVertex2d, NULL, 126),
    NAME_FUNC_OFFSET( 1523, glVertex2dv, glVertex2dv, NULL, 127),
    NAME_FUNC_OFFSET( 1535, glVertex2f, glVertex2f, NULL, 128),
    NAME_FUNC_OFFSET( 1546, glVertex2fv, glVertex2fv, NULL, 129),
    NAME_FUNC_OFFSET( 1558, glVertex2i, glVertex2i, NULL, 130),
    NAME_FUNC_OFFSET( 1569, glVertex2iv, glVertex2iv, NULL, 131),
    NAME_FUNC_OFFSET( 1581, glVertex2s, glVertex2s, NULL, 132),
    NAME_FUNC_OFFSET( 1592, glVertex2sv, glVertex2sv, NULL, 133),
    NAME_FUNC_OFFSET( 1604, glVertex3d, glVertex3d, NULL, 134),
    NAME_FUNC_OFFSET( 1615, glVertex3dv, glVertex3dv, NULL, 135),
    NAME_FUNC_OFFSET( 1627, glVertex3f, glVertex3f, NULL, 136),
    NAME_FUNC_OFFSET( 1638, glVertex3fv, glVertex3fv, NULL, 137),
    NAME_FUNC_OFFSET( 1650, glVertex3i, glVertex3i, NULL, 138),
    NAME_FUNC_OFFSET( 1661, glVertex3iv, glVertex3iv, NULL, 139),
    NAME_FUNC_OFFSET( 1673, glVertex3s, glVertex3s, NULL, 140),
    NAME_FUNC_OFFSET( 1684, glVertex3sv, glVertex3sv, NULL, 141),
    NAME_FUNC_OFFSET( 1696, glVertex4d, glVertex4d, NULL, 142),
    NAME_FUNC_OFFSET( 1707, glVertex4dv, glVertex4dv, NULL, 143),
    NAME_FUNC_OFFSET( 1719, glVertex4f, glVertex4f, NULL, 144),
    NAME_FUNC_OFFSET( 1730, glVertex4fv, glVertex4fv, NULL, 145),
    NAME_FUNC_OFFSET( 1742, glVertex4i, glVertex4i, NULL, 146),
    NAME_FUNC_OFFSET( 1753, glVertex4iv, glVertex4iv, NULL, 147),
    NAME_FUNC_OFFSET( 1765, glVertex4s, glVertex4s, NULL, 148),
    NAME_FUNC_OFFSET( 1776, glVertex4sv, glVertex4sv, NULL, 149),
    NAME_FUNC_OFFSET( 1788, glClipPlane, glClipPlane, NULL, 150),
    NAME_FUNC_OFFSET( 1800, glColorMaterial, glColorMaterial, NULL, 151),
    NAME_FUNC_OFFSET( 1816, glCullFace, glCullFace, NULL, 152),
    NAME_FUNC_OFFSET( 1827, glFogf, glFogf, NULL, 153),
    NAME_FUNC_OFFSET( 1834, glFogfv, glFogfv, NULL, 154),
    NAME_FUNC_OFFSET( 1842, glFogi, glFogi, NULL, 155),
    NAME_FUNC_OFFSET( 1849, glFogiv, glFogiv, NULL, 156),
    NAME_FUNC_OFFSET( 1857, glFrontFace, glFrontFace, NULL, 157),
    NAME_FUNC_OFFSET( 1869, glHint, glHint, NULL, 158),
    NAME_FUNC_OFFSET( 1876, glLightf, glLightf, NULL, 159),
    NAME_FUNC_OFFSET( 1885, glLightfv, glLightfv, NULL, 160),
    NAME_FUNC_OFFSET( 1895, glLighti, glLighti, NULL, 161),
    NAME_FUNC_OFFSET( 1904, glLightiv, glLightiv, NULL, 162),
    NAME_FUNC_OFFSET( 1914, glLightModelf, glLightModelf, NULL, 163),
    NAME_FUNC_OFFSET( 1928, glLightModelfv, glLightModelfv, NULL, 164),
    NAME_FUNC_OFFSET( 1943, glLightModeli, glLightModeli, NULL, 165),
    NAME_FUNC_OFFSET( 1957, glLightModeliv, glLightModeliv, NULL, 166),
    NAME_FUNC_OFFSET( 1972, glLineStipple, glLineStipple, NULL, 167),
    NAME_FUNC_OFFSET( 1986, glLineWidth, glLineWidth, NULL, 168),
    NAME_FUNC_OFFSET( 1998, glMaterialf, glMaterialf, NULL, 169),
    NAME_FUNC_OFFSET( 2010, glMaterialfv, glMaterialfv, NULL, 170),
    NAME_FUNC_OFFSET( 2023, glMateriali, glMateriali, NULL, 171),
    NAME_FUNC_OFFSET( 2035, glMaterialiv, glMaterialiv, NULL, 172),
    NAME_FUNC_OFFSET( 2048, glPointSize, glPointSize, NULL, 173),
    NAME_FUNC_OFFSET( 2060, glPolygonMode, glPolygonMode, NULL, 174),
    NAME_FUNC_OFFSET( 2074, glPolygonStipple, glPolygonStipple, NULL, 175),
    NAME_FUNC_OFFSET( 2091, glScissor, glScissor, NULL, 176),
    NAME_FUNC_OFFSET( 2101, glShadeModel, glShadeModel, NULL, 177),
    NAME_FUNC_OFFSET( 2114, glTexParameterf, glTexParameterf, NULL, 178),
    NAME_FUNC_OFFSET( 2130, glTexParameterfv, glTexParameterfv, NULL, 179),
    NAME_FUNC_OFFSET( 2147, glTexParameteri, glTexParameteri, NULL, 180),
    NAME_FUNC_OFFSET( 2163, glTexParameteriv, glTexParameteriv, NULL, 181),
    NAME_FUNC_OFFSET( 2180, glTexImage1D, glTexImage1D, NULL, 182),
    NAME_FUNC_OFFSET( 2193, glTexImage2D, glTexImage2D, NULL, 183),
    NAME_FUNC_OFFSET( 2206, glTexEnvf, glTexEnvf, NULL, 184),
    NAME_FUNC_OFFSET( 2216, glTexEnvfv, glTexEnvfv, NULL, 185),
    NAME_FUNC_OFFSET( 2227, glTexEnvi, glTexEnvi, NULL, 186),
    NAME_FUNC_OFFSET( 2237, glTexEnviv, glTexEnviv, NULL, 187),
    NAME_FUNC_OFFSET( 2248, glTexGend, glTexGend, NULL, 188),
    NAME_FUNC_OFFSET( 2258, glTexGendv, glTexGendv, NULL, 189),
    NAME_FUNC_OFFSET( 2269, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET( 2279, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET( 2290, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET( 2300, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET( 2311, glFeedbackBuffer, glFeedbackBuffer, NULL, 194),
    NAME_FUNC_OFFSET( 2328, glSelectBuffer, glSelectBuffer, NULL, 195),
    NAME_FUNC_OFFSET( 2343, glRenderMode, glRenderMode, NULL, 196),
    NAME_FUNC_OFFSET( 2356, glInitNames, glInitNames, NULL, 197),
    NAME_FUNC_OFFSET( 2368, glLoadName, glLoadName, NULL, 198),
    NAME_FUNC_OFFSET( 2379, glPassThrough, glPassThrough, NULL, 199),
    NAME_FUNC_OFFSET( 2393, glPopName, glPopName, NULL, 200),
    NAME_FUNC_OFFSET( 2403, glPushName, glPushName, NULL, 201),
    NAME_FUNC_OFFSET( 2414, glDrawBuffer, glDrawBuffer, NULL, 202),
    NAME_FUNC_OFFSET( 2427, glClear, glClear, NULL, 203),
    NAME_FUNC_OFFSET( 2435, glClearAccum, glClearAccum, NULL, 204),
    NAME_FUNC_OFFSET( 2448, glClearIndex, glClearIndex, NULL, 205),
    NAME_FUNC_OFFSET( 2461, glClearColor, glClearColor, NULL, 206),
    NAME_FUNC_OFFSET( 2474, glClearStencil, glClearStencil, NULL, 207),
    NAME_FUNC_OFFSET( 2489, glClearDepth, glClearDepth, NULL, 208),
    NAME_FUNC_OFFSET( 2502, glStencilMask, glStencilMask, NULL, 209),
    NAME_FUNC_OFFSET( 2516, glColorMask, glColorMask, NULL, 210),
    NAME_FUNC_OFFSET( 2528, glDepthMask, glDepthMask, NULL, 211),
    NAME_FUNC_OFFSET( 2540, glIndexMask, glIndexMask, NULL, 212),
    NAME_FUNC_OFFSET( 2552, glAccum, glAccum, NULL, 213),
    NAME_FUNC_OFFSET( 2560, glDisable, glDisable, NULL, 214),
    NAME_FUNC_OFFSET( 2570, glEnable, glEnable, NULL, 215),
    NAME_FUNC_OFFSET( 2579, glFinish, glFinish, NULL, 216),
    NAME_FUNC_OFFSET( 2588, glFlush, glFlush, NULL, 217),
    NAME_FUNC_OFFSET( 2596, glPopAttrib, glPopAttrib, NULL, 218),
    NAME_FUNC_OFFSET( 2608, glPushAttrib, glPushAttrib, NULL, 219),
    NAME_FUNC_OFFSET( 2621, glMap1d, glMap1d, NULL, 220),
    NAME_FUNC_OFFSET( 2629, glMap1f, glMap1f, NULL, 221),
    NAME_FUNC_OFFSET( 2637, glMap2d, glMap2d, NULL, 222),
    NAME_FUNC_OFFSET( 2645, glMap2f, glMap2f, NULL, 223),
    NAME_FUNC_OFFSET( 2653, glMapGrid1d, glMapGrid1d, NULL, 224),
    NAME_FUNC_OFFSET( 2665, glMapGrid1f, glMapGrid1f, NULL, 225),
    NAME_FUNC_OFFSET( 2677, glMapGrid2d, glMapGrid2d, NULL, 226),
    NAME_FUNC_OFFSET( 2689, glMapGrid2f, glMapGrid2f, NULL, 227),
    NAME_FUNC_OFFSET( 2701, glEvalCoord1d, glEvalCoord1d, NULL, 228),
    NAME_FUNC_OFFSET( 2715, glEvalCoord1dv, glEvalCoord1dv, NULL, 229),
    NAME_FUNC_OFFSET( 2730, glEvalCoord1f, glEvalCoord1f, NULL, 230),
    NAME_FUNC_OFFSET( 2744, glEvalCoord1fv, glEvalCoord1fv, NULL, 231),
    NAME_FUNC_OFFSET( 2759, glEvalCoord2d, glEvalCoord2d, NULL, 232),
    NAME_FUNC_OFFSET( 2773, glEvalCoord2dv, glEvalCoord2dv, NULL, 233),
    NAME_FUNC_OFFSET( 2788, glEvalCoord2f, glEvalCoord2f, NULL, 234),
    NAME_FUNC_OFFSET( 2802, glEvalCoord2fv, glEvalCoord2fv, NULL, 235),
    NAME_FUNC_OFFSET( 2817, glEvalMesh1, glEvalMesh1, NULL, 236),
    NAME_FUNC_OFFSET( 2829, glEvalPoint1, glEvalPoint1, NULL, 237),
    NAME_FUNC_OFFSET( 2842, glEvalMesh2, glEvalMesh2, NULL, 238),
    NAME_FUNC_OFFSET( 2854, glEvalPoint2, glEvalPoint2, NULL, 239),
    NAME_FUNC_OFFSET( 2867, glAlphaFunc, glAlphaFunc, NULL, 240),
    NAME_FUNC_OFFSET( 2879, glBlendFunc, glBlendFunc, NULL, 241),
    NAME_FUNC_OFFSET( 2891, glLogicOp, glLogicOp, NULL, 242),
    NAME_FUNC_OFFSET( 2901, glStencilFunc, glStencilFunc, NULL, 243),
    NAME_FUNC_OFFSET( 2915, glStencilOp, glStencilOp, NULL, 244),
    NAME_FUNC_OFFSET( 2927, glDepthFunc, glDepthFunc, NULL, 245),
    NAME_FUNC_OFFSET( 2939, glPixelZoom, glPixelZoom, NULL, 246),
    NAME_FUNC_OFFSET( 2951, glPixelTransferf, glPixelTransferf, NULL, 247),
    NAME_FUNC_OFFSET( 2968, glPixelTransferi, glPixelTransferi, NULL, 248),
    NAME_FUNC_OFFSET( 2985, glPixelStoref, glPixelStoref, NULL, 249),
    NAME_FUNC_OFFSET( 2999, glPixelStorei, glPixelStorei, NULL, 250),
    NAME_FUNC_OFFSET( 3013, glPixelMapfv, glPixelMapfv, NULL, 251),
    NAME_FUNC_OFFSET( 3026, glPixelMapuiv, glPixelMapuiv, NULL, 252),
    NAME_FUNC_OFFSET( 3040, glPixelMapusv, glPixelMapusv, NULL, 253),
    NAME_FUNC_OFFSET( 3054, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET( 3067, glCopyPixels, glCopyPixels, NULL, 255),
    NAME_FUNC_OFFSET( 3080, glReadPixels, glReadPixels, NULL, 256),
    NAME_FUNC_OFFSET( 3093, glDrawPixels, glDrawPixels, NULL, 257),
    NAME_FUNC_OFFSET( 3106, glGetBooleanv, glGetBooleanv, NULL, 258),
    NAME_FUNC_OFFSET( 3120, glGetClipPlane, glGetClipPlane, NULL, 259),
    NAME_FUNC_OFFSET( 3135, glGetDoublev, glGetDoublev, NULL, 260),
    NAME_FUNC_OFFSET( 3148, glGetError, glGetError, NULL, 261),
    NAME_FUNC_OFFSET( 3159, glGetFloatv, glGetFloatv, NULL, 262),
    NAME_FUNC_OFFSET( 3171, glGetIntegerv, glGetIntegerv, NULL, 263),
    NAME_FUNC_OFFSET( 3185, glGetLightfv, glGetLightfv, NULL, 264),
    NAME_FUNC_OFFSET( 3198, glGetLightiv, glGetLightiv, NULL, 265),
    NAME_FUNC_OFFSET( 3211, glGetMapdv, glGetMapdv, NULL, 266),
    NAME_FUNC_OFFSET( 3222, glGetMapfv, glGetMapfv, NULL, 267),
    NAME_FUNC_OFFSET( 3233, glGetMapiv, glGetMapiv, NULL, 268),
    NAME_FUNC_OFFSET( 3244, glGetMaterialfv, glGetMaterialfv, NULL, 269),
    NAME_FUNC_OFFSET( 3260, glGetMaterialiv, glGetMaterialiv, NULL, 270),
    NAME_FUNC_OFFSET( 3276, glGetPixelMapfv, glGetPixelMapfv, NULL, 271),
    NAME_FUNC_OFFSET( 3292, glGetPixelMapuiv, glGetPixelMapuiv, NULL, 272),
    NAME_FUNC_OFFSET( 3309, glGetPixelMapusv, glGetPixelMapusv, NULL, 273),
    NAME_FUNC_OFFSET( 3326, glGetPolygonStipple, glGetPolygonStipple, NULL, 274),
    NAME_FUNC_OFFSET( 3346, glGetString, glGetString, NULL, 275),
    NAME_FUNC_OFFSET( 3358, glGetTexEnvfv, glGetTexEnvfv, NULL, 276),
    NAME_FUNC_OFFSET( 3372, glGetTexEnviv, glGetTexEnviv, NULL, 277),
    NAME_FUNC_OFFSET( 3386, glGetTexGendv, glGetTexGendv, NULL, 278),
    NAME_FUNC_OFFSET( 3400, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET( 3414, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET( 3428, glGetTexImage, glGetTexImage, NULL, 281),
    NAME_FUNC_OFFSET( 3442, glGetTexParameterfv, glGetTexParameterfv, NULL, 282),
    NAME_FUNC_OFFSET( 3462, glGetTexParameteriv, glGetTexParameteriv, NULL, 283),
    NAME_FUNC_OFFSET( 3482, glGetTexLevelParameterfv, glGetTexLevelParameterfv, NULL, 284),
    NAME_FUNC_OFFSET( 3507, glGetTexLevelParameteriv, glGetTexLevelParameteriv, NULL, 285),
    NAME_FUNC_OFFSET( 3532, glIsEnabled, glIsEnabled, NULL, 286),
    NAME_FUNC_OFFSET( 3544, glIsList, glIsList, NULL, 287),
    NAME_FUNC_OFFSET( 3553, glDepthRange, glDepthRange, NULL, 288),
    NAME_FUNC_OFFSET( 3566, glFrustum, glFrustum, NULL, 289),
    NAME_FUNC_OFFSET( 3576, glLoadIdentity, glLoadIdentity, NULL, 290),
    NAME_FUNC_OFFSET( 3591, glLoadMatrixf, glLoadMatrixf, NULL, 291),
    NAME_FUNC_OFFSET( 3605, glLoadMatrixd, glLoadMatrixd, NULL, 292),
    NAME_FUNC_OFFSET( 3619, glMatrixMode, glMatrixMode, NULL, 293),
    NAME_FUNC_OFFSET( 3632, glMultMatrixf, glMultMatrixf, NULL, 294),
    NAME_FUNC_OFFSET( 3646, glMultMatrixd, glMultMatrixd, NULL, 295),
    NAME_FUNC_OFFSET( 3660, glOrtho, glOrtho, NULL, 296),
    NAME_FUNC_OFFSET( 3668, glPopMatrix, glPopMatrix, NULL, 297),
    NAME_FUNC_OFFSET( 3680, glPushMatrix, glPushMatrix, NULL, 298),
    NAME_FUNC_OFFSET( 3693, glRotated, glRotated, NULL, 299),
    NAME_FUNC_OFFSET( 3703, glRotatef, glRotatef, NULL, 300),
    NAME_FUNC_OFFSET( 3713, glScaled, glScaled, NULL, 301),
    NAME_FUNC_OFFSET( 3722, glScalef, glScalef, NULL, 302),
    NAME_FUNC_OFFSET( 3731, glTranslated, glTranslated, NULL, 303),
    NAME_FUNC_OFFSET( 3744, glTranslatef, glTranslatef, NULL, 304),
    NAME_FUNC_OFFSET( 3757, glViewport, glViewport, NULL, 305),
    NAME_FUNC_OFFSET( 3768, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET( 3783, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET( 3797, glColorPointer, glColorPointer, NULL, 308),
    NAME_FUNC_OFFSET( 3812, glDisableClientState, glDisableClientState, NULL, 309),
    NAME_FUNC_OFFSET( 3833, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET( 3846, glDrawElements, glDrawElements, NULL, 311),
    NAME_FUNC_OFFSET( 3861, glEdgeFlagPointer, glEdgeFlagPointer, NULL, 312),
    NAME_FUNC_OFFSET( 3879, glEnableClientState, glEnableClientState, NULL, 313),
    NAME_FUNC_OFFSET( 3899, glIndexPointer, glIndexPointer, NULL, 314),
    NAME_FUNC_OFFSET( 3914, glIndexub, glIndexub, NULL, 315),
    NAME_FUNC_OFFSET( 3924, glIndexubv, glIndexubv, NULL, 316),
    NAME_FUNC_OFFSET( 3935, glInterleavedArrays, glInterleavedArrays, NULL, 317),
    NAME_FUNC_OFFSET( 3955, glNormalPointer, glNormalPointer, NULL, 318),
    NAME_FUNC_OFFSET( 3971, glPolygonOffset, glPolygonOffset, NULL, 319),
    NAME_FUNC_OFFSET( 3987, glTexCoordPointer, glTexCoordPointer, NULL, 320),
    NAME_FUNC_OFFSET( 4005, glVertexPointer, glVertexPointer, NULL, 321),
    NAME_FUNC_OFFSET( 4021, glAreTexturesResident, glAreTexturesResident, NULL, 322),
    NAME_FUNC_OFFSET( 4043, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET( 4060, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET( 4077, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET( 4097, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET( 4117, glDeleteTextures, glDeleteTextures, NULL, 327),
    NAME_FUNC_OFFSET( 4134, glGenTextures, glGenTextures, NULL, 328),
    NAME_FUNC_OFFSET( 4148, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET( 4162, glIsTexture, glIsTexture, NULL, 330),
    NAME_FUNC_OFFSET( 4174, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET( 4195, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET( 4211, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET( 4227, glPopClientAttrib, glPopClientAttrib, NULL, 334),
    NAME_FUNC_OFFSET( 4245, glPushClientAttrib, glPushClientAttrib, NULL, 335),
    NAME_FUNC_OFFSET( 4264, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET( 4277, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET( 4293, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET( 4313, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET( 4326, glColorTableParameterfv, glColorTableParameterfv, NULL, 340),
    NAME_FUNC_OFFSET( 4350, glColorTableParameteriv, glColorTableParameteriv, NULL, 341),
    NAME_FUNC_OFFSET( 4374, glCopyColorTable, glCopyColorTable, NULL, 342),
    NAME_FUNC_OFFSET( 4391, glGetColorTable, glGetColorTable, NULL, 343),
    NAME_FUNC_OFFSET( 4407, glGetColorTableParameterfv, glGetColorTableParameterfv, NULL, 344),
    NAME_FUNC_OFFSET( 4434, glGetColorTableParameteriv, glGetColorTableParameteriv, NULL, 345),
    NAME_FUNC_OFFSET( 4461, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET( 4477, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET( 4497, glConvolutionFilter1D, glConvolutionFilter1D, NULL, 348),
    NAME_FUNC_OFFSET( 4519, glConvolutionFilter2D, glConvolutionFilter2D, NULL, 349),
    NAME_FUNC_OFFSET( 4541, glConvolutionParameterf, glConvolutionParameterf, NULL, 350),
    NAME_FUNC_OFFSET( 4565, glConvolutionParameterfv, glConvolutionParameterfv, NULL, 351),
    NAME_FUNC_OFFSET( 4590, glConvolutionParameteri, glConvolutionParameteri, NULL, 352),
    NAME_FUNC_OFFSET( 4614, glConvolutionParameteriv, glConvolutionParameteriv, NULL, 353),
    NAME_FUNC_OFFSET( 4639, glCopyConvolutionFilter1D, glCopyConvolutionFilter1D, NULL, 354),
    NAME_FUNC_OFFSET( 4665, glCopyConvolutionFilter2D, glCopyConvolutionFilter2D, NULL, 355),
    NAME_FUNC_OFFSET( 4691, glGetConvolutionFilter, glGetConvolutionFilter, NULL, 356),
    NAME_FUNC_OFFSET( 4714, glGetConvolutionParameterfv, glGetConvolutionParameterfv, NULL, 357),
    NAME_FUNC_OFFSET( 4742, glGetConvolutionParameteriv, glGetConvolutionParameteriv, NULL, 358),
    NAME_FUNC_OFFSET( 4770, glGetSeparableFilter, glGetSeparableFilter, NULL, 359),
    NAME_FUNC_OFFSET( 4791, glSeparableFilter2D, glSeparableFilter2D, NULL, 360),
    NAME_FUNC_OFFSET( 4811, glGetHistogram, glGetHistogram, NULL, 361),
    NAME_FUNC_OFFSET( 4826, glGetHistogramParameterfv, glGetHistogramParameterfv, NULL, 362),
    NAME_FUNC_OFFSET( 4852, glGetHistogramParameteriv, glGetHistogramParameteriv, NULL, 363),
    NAME_FUNC_OFFSET( 4878, glGetMinmax, glGetMinmax, NULL, 364),
    NAME_FUNC_OFFSET( 4890, glGetMinmaxParameterfv, glGetMinmaxParameterfv, NULL, 365),
    NAME_FUNC_OFFSET( 4913, glGetMinmaxParameteriv, glGetMinmaxParameteriv, NULL, 366),
    NAME_FUNC_OFFSET( 4936, glHistogram, glHistogram, NULL, 367),
    NAME_FUNC_OFFSET( 4948, glMinmax, glMinmax, NULL, 368),
    NAME_FUNC_OFFSET( 4957, glResetHistogram, glResetHistogram, NULL, 369),
    NAME_FUNC_OFFSET( 4974, glResetMinmax, glResetMinmax, NULL, 370),
    NAME_FUNC_OFFSET( 4988, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET( 5001, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET( 5017, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET( 5037, glActiveTextureARB, glActiveTextureARB, NULL, 374),
    NAME_FUNC_OFFSET( 5056, glClientActiveTextureARB, glClientActiveTextureARB, NULL, 375),
    NAME_FUNC_OFFSET( 5081, glMultiTexCoord1dARB, glMultiTexCoord1dARB, NULL, 376),
    NAME_FUNC_OFFSET( 5102, glMultiTexCoord1dvARB, glMultiTexCoord1dvARB, NULL, 377),
    NAME_FUNC_OFFSET( 5124, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET( 5145, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET( 5167, glMultiTexCoord1iARB, glMultiTexCoord1iARB, NULL, 380),
    NAME_FUNC_OFFSET( 5188, glMultiTexCoord1ivARB, glMultiTexCoord1ivARB, NULL, 381),
    NAME_FUNC_OFFSET( 5210, glMultiTexCoord1sARB, glMultiTexCoord1sARB, NULL, 382),
    NAME_FUNC_OFFSET( 5231, glMultiTexCoord1svARB, glMultiTexCoord1svARB, NULL, 383),
    NAME_FUNC_OFFSET( 5253, glMultiTexCoord2dARB, glMultiTexCoord2dARB, NULL, 384),
    NAME_FUNC_OFFSET( 5274, glMultiTexCoord2dvARB, glMultiTexCoord2dvARB, NULL, 385),
    NAME_FUNC_OFFSET( 5296, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET( 5317, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET( 5339, glMultiTexCoord2iARB, glMultiTexCoord2iARB, NULL, 388),
    NAME_FUNC_OFFSET( 5360, glMultiTexCoord2ivARB, glMultiTexCoord2ivARB, NULL, 389),
    NAME_FUNC_OFFSET( 5382, glMultiTexCoord2sARB, glMultiTexCoord2sARB, NULL, 390),
    NAME_FUNC_OFFSET( 5403, glMultiTexCoord2svARB, glMultiTexCoord2svARB, NULL, 391),
    NAME_FUNC_OFFSET( 5425, glMultiTexCoord3dARB, glMultiTexCoord3dARB, NULL, 392),
    NAME_FUNC_OFFSET( 5446, glMultiTexCoord3dvARB, glMultiTexCoord3dvARB, NULL, 393),
    NAME_FUNC_OFFSET( 5468, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET( 5489, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET( 5511, glMultiTexCoord3iARB, glMultiTexCoord3iARB, NULL, 396),
    NAME_FUNC_OFFSET( 5532, glMultiTexCoord3ivARB, glMultiTexCoord3ivARB, NULL, 397),
    NAME_FUNC_OFFSET( 5554, glMultiTexCoord3sARB, glMultiTexCoord3sARB, NULL, 398),
    NAME_FUNC_OFFSET( 5575, glMultiTexCoord3svARB, glMultiTexCoord3svARB, NULL, 399),
    NAME_FUNC_OFFSET( 5597, glMultiTexCoord4dARB, glMultiTexCoord4dARB, NULL, 400),
    NAME_FUNC_OFFSET( 5618, glMultiTexCoord4dvARB, glMultiTexCoord4dvARB, NULL, 401),
    NAME_FUNC_OFFSET( 5640, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET( 5661, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET( 5683, glMultiTexCoord4iARB, glMultiTexCoord4iARB, NULL, 404),
    NAME_FUNC_OFFSET( 5704, glMultiTexCoord4ivARB, glMultiTexCoord4ivARB, NULL, 405),
    NAME_FUNC_OFFSET( 5726, glMultiTexCoord4sARB, glMultiTexCoord4sARB, NULL, 406),
    NAME_FUNC_OFFSET( 5747, glMultiTexCoord4svARB, glMultiTexCoord4svARB, NULL, 407),
    NAME_FUNC_OFFSET( 5769, glAttachShader, glAttachShader, NULL, 408),
    NAME_FUNC_OFFSET( 5784, glCreateProgram, glCreateProgram, NULL, 409),
    NAME_FUNC_OFFSET( 5800, glCreateShader, glCreateShader, NULL, 410),
    NAME_FUNC_OFFSET( 5815, glDeleteProgram, glDeleteProgram, NULL, 411),
    NAME_FUNC_OFFSET( 5831, glDeleteShader, glDeleteShader, NULL, 412),
    NAME_FUNC_OFFSET( 5846, glDetachShader, glDetachShader, NULL, 413),
    NAME_FUNC_OFFSET( 5861, glGetAttachedShaders, glGetAttachedShaders, NULL, 414),
    NAME_FUNC_OFFSET( 5882, glGetProgramInfoLog, glGetProgramInfoLog, NULL, 415),
    NAME_FUNC_OFFSET( 5902, glGetProgramiv, glGetProgramiv, NULL, 416),
    NAME_FUNC_OFFSET( 5917, glGetShaderInfoLog, glGetShaderInfoLog, NULL, 417),
    NAME_FUNC_OFFSET( 5936, glGetShaderiv, glGetShaderiv, NULL, 418),
    NAME_FUNC_OFFSET( 5950, glIsProgram, glIsProgram, NULL, 419),
    NAME_FUNC_OFFSET( 5962, glIsShader, glIsShader, NULL, 420),
    NAME_FUNC_OFFSET( 5973, glStencilFuncSeparate, glStencilFuncSeparate, NULL, 421),
    NAME_FUNC_OFFSET( 5995, glStencilMaskSeparate, glStencilMaskSeparate, NULL, 422),
    NAME_FUNC_OFFSET( 6017, glStencilOpSeparate, glStencilOpSeparate, NULL, 423),
    NAME_FUNC_OFFSET( 6037, glUniformMatrix2x3fv, glUniformMatrix2x3fv, NULL, 424),
    NAME_FUNC_OFFSET( 6058, glUniformMatrix2x4fv, glUniformMatrix2x4fv, NULL, 425),
    NAME_FUNC_OFFSET( 6079, glUniformMatrix3x2fv, glUniformMatrix3x2fv, NULL, 426),
    NAME_FUNC_OFFSET( 6100, glUniformMatrix3x4fv, glUniformMatrix3x4fv, NULL, 427),
    NAME_FUNC_OFFSET( 6121, glUniformMatrix4x2fv, glUniformMatrix4x2fv, NULL, 428),
    NAME_FUNC_OFFSET( 6142, glUniformMatrix4x3fv, glUniformMatrix4x3fv, NULL, 429),
    NAME_FUNC_OFFSET( 6163, glClearBufferfi, glClearBufferfi, NULL, 430),
    NAME_FUNC_OFFSET( 6179, glClearBufferfv, glClearBufferfv, NULL, 431),
    NAME_FUNC_OFFSET( 6195, glClearBufferiv, glClearBufferiv, NULL, 432),
    NAME_FUNC_OFFSET( 6211, glClearBufferuiv, glClearBufferuiv, NULL, 433),
    NAME_FUNC_OFFSET( 6228, glGetStringi, glGetStringi, NULL, 434),
    NAME_FUNC_OFFSET( 6241, glFramebufferTexture, glFramebufferTexture, NULL, 435),
    NAME_FUNC_OFFSET( 6262, glGetBufferParameteri64v, glGetBufferParameteri64v, NULL, 436),
    NAME_FUNC_OFFSET( 6287, glGetInteger64i_v, glGetInteger64i_v, NULL, 437),
    NAME_FUNC_OFFSET( 6305, glLoadTransposeMatrixdARB, glLoadTransposeMatrixdARB, NULL, 438),
    NAME_FUNC_OFFSET( 6331, glLoadTransposeMatrixfARB, glLoadTransposeMatrixfARB, NULL, 439),
    NAME_FUNC_OFFSET( 6357, glMultTransposeMatrixdARB, glMultTransposeMatrixdARB, NULL, 440),
    NAME_FUNC_OFFSET( 6383, glMultTransposeMatrixfARB, glMultTransposeMatrixfARB, NULL, 441),
    NAME_FUNC_OFFSET( 6409, glSampleCoverageARB, glSampleCoverageARB, NULL, 442),
    NAME_FUNC_OFFSET( 6429, glCompressedTexImage1DARB, glCompressedTexImage1DARB, NULL, 443),
    NAME_FUNC_OFFSET( 6455, glCompressedTexImage2DARB, glCompressedTexImage2DARB, NULL, 444),
    NAME_FUNC_OFFSET( 6481, glCompressedTexImage3DARB, glCompressedTexImage3DARB, NULL, 445),
    NAME_FUNC_OFFSET( 6507, glCompressedTexSubImage1DARB, glCompressedTexSubImage1DARB, NULL, 446),
    NAME_FUNC_OFFSET( 6536, glCompressedTexSubImage2DARB, glCompressedTexSubImage2DARB, NULL, 447),
    NAME_FUNC_OFFSET( 6565, glCompressedTexSubImage3DARB, glCompressedTexSubImage3DARB, NULL, 448),
    NAME_FUNC_OFFSET( 6594, glGetCompressedTexImageARB, glGetCompressedTexImageARB, NULL, 449),
    NAME_FUNC_OFFSET( 6621, glDisableVertexAttribArrayARB, glDisableVertexAttribArrayARB, NULL, 450),
    NAME_FUNC_OFFSET( 6651, glEnableVertexAttribArrayARB, glEnableVertexAttribArrayARB, NULL, 451),
    NAME_FUNC_OFFSET( 6680, glGetProgramEnvParameterdvARB, glGetProgramEnvParameterdvARB, NULL, 452),
    NAME_FUNC_OFFSET( 6710, glGetProgramEnvParameterfvARB, glGetProgramEnvParameterfvARB, NULL, 453),
    NAME_FUNC_OFFSET( 6740, glGetProgramLocalParameterdvARB, glGetProgramLocalParameterdvARB, NULL, 454),
    NAME_FUNC_OFFSET( 6772, glGetProgramLocalParameterfvARB, glGetProgramLocalParameterfvARB, NULL, 455),
    NAME_FUNC_OFFSET( 6804, glGetProgramStringARB, glGetProgramStringARB, NULL, 456),
    NAME_FUNC_OFFSET( 6826, glGetProgramivARB, glGetProgramivARB, NULL, 457),
    NAME_FUNC_OFFSET( 6844, glGetVertexAttribdvARB, glGetVertexAttribdvARB, NULL, 458),
    NAME_FUNC_OFFSET( 6867, glGetVertexAttribfvARB, glGetVertexAttribfvARB, NULL, 459),
    NAME_FUNC_OFFSET( 6890, glGetVertexAttribivARB, glGetVertexAttribivARB, NULL, 460),
    NAME_FUNC_OFFSET( 6913, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 461),
    NAME_FUNC_OFFSET( 6940, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 462),
    NAME_FUNC_OFFSET( 6968, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 463),
    NAME_FUNC_OFFSET( 6995, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 464),
    NAME_FUNC_OFFSET( 7023, glProgramLocalParameter4dARB, glProgramLocalParameter4dARB, NULL, 465),
    NAME_FUNC_OFFSET( 7052, glProgramLocalParameter4dvARB, glProgramLocalParameter4dvARB, NULL, 466),
    NAME_FUNC_OFFSET( 7082, glProgramLocalParameter4fARB, glProgramLocalParameter4fARB, NULL, 467),
    NAME_FUNC_OFFSET( 7111, glProgramLocalParameter4fvARB, glProgramLocalParameter4fvARB, NULL, 468),
    NAME_FUNC_OFFSET( 7141, glProgramStringARB, glProgramStringARB, NULL, 469),
    NAME_FUNC_OFFSET( 7160, glVertexAttrib1dARB, glVertexAttrib1dARB, NULL, 470),
    NAME_FUNC_OFFSET( 7180, glVertexAttrib1dvARB, glVertexAttrib1dvARB, NULL, 471),
    NAME_FUNC_OFFSET( 7201, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 472),
    NAME_FUNC_OFFSET( 7221, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 473),
    NAME_FUNC_OFFSET( 7242, glVertexAttrib1sARB, glVertexAttrib1sARB, NULL, 474),
    NAME_FUNC_OFFSET( 7262, glVertexAttrib1svARB, glVertexAttrib1svARB, NULL, 475),
    NAME_FUNC_OFFSET( 7283, glVertexAttrib2dARB, glVertexAttrib2dARB, NULL, 476),
    NAME_FUNC_OFFSET( 7303, glVertexAttrib2dvARB, glVertexAttrib2dvARB, NULL, 477),
    NAME_FUNC_OFFSET( 7324, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 478),
    NAME_FUNC_OFFSET( 7344, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 479),
    NAME_FUNC_OFFSET( 7365, glVertexAttrib2sARB, glVertexAttrib2sARB, NULL, 480),
    NAME_FUNC_OFFSET( 7385, glVertexAttrib2svARB, glVertexAttrib2svARB, NULL, 481),
    NAME_FUNC_OFFSET( 7406, glVertexAttrib3dARB, glVertexAttrib3dARB, NULL, 482),
    NAME_FUNC_OFFSET( 7426, glVertexAttrib3dvARB, glVertexAttrib3dvARB, NULL, 483),
    NAME_FUNC_OFFSET( 7447, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 484),
    NAME_FUNC_OFFSET( 7467, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 485),
    NAME_FUNC_OFFSET( 7488, glVertexAttrib3sARB, glVertexAttrib3sARB, NULL, 486),
    NAME_FUNC_OFFSET( 7508, glVertexAttrib3svARB, glVertexAttrib3svARB, NULL, 487),
    NAME_FUNC_OFFSET( 7529, glVertexAttrib4NbvARB, glVertexAttrib4NbvARB, NULL, 488),
    NAME_FUNC_OFFSET( 7551, glVertexAttrib4NivARB, glVertexAttrib4NivARB, NULL, 489),
    NAME_FUNC_OFFSET( 7573, glVertexAttrib4NsvARB, glVertexAttrib4NsvARB, NULL, 490),
    NAME_FUNC_OFFSET( 7595, glVertexAttrib4NubARB, glVertexAttrib4NubARB, NULL, 491),
    NAME_FUNC_OFFSET( 7617, glVertexAttrib4NubvARB, glVertexAttrib4NubvARB, NULL, 492),
    NAME_FUNC_OFFSET( 7640, glVertexAttrib4NuivARB, glVertexAttrib4NuivARB, NULL, 493),
    NAME_FUNC_OFFSET( 7663, glVertexAttrib4NusvARB, glVertexAttrib4NusvARB, NULL, 494),
    NAME_FUNC_OFFSET( 7686, glVertexAttrib4bvARB, glVertexAttrib4bvARB, NULL, 495),
    NAME_FUNC_OFFSET( 7707, glVertexAttrib4dARB, glVertexAttrib4dARB, NULL, 496),
    NAME_FUNC_OFFSET( 7727, glVertexAttrib4dvARB, glVertexAttrib4dvARB, NULL, 497),
    NAME_FUNC_OFFSET( 7748, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 498),
    NAME_FUNC_OFFSET( 7768, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 499),
    NAME_FUNC_OFFSET( 7789, glVertexAttrib4ivARB, glVertexAttrib4ivARB, NULL, 500),
    NAME_FUNC_OFFSET( 7810, glVertexAttrib4sARB, glVertexAttrib4sARB, NULL, 501),
    NAME_FUNC_OFFSET( 7830, glVertexAttrib4svARB, glVertexAttrib4svARB, NULL, 502),
    NAME_FUNC_OFFSET( 7851, glVertexAttrib4ubvARB, glVertexAttrib4ubvARB, NULL, 503),
    NAME_FUNC_OFFSET( 7873, glVertexAttrib4uivARB, glVertexAttrib4uivARB, NULL, 504),
    NAME_FUNC_OFFSET( 7895, glVertexAttrib4usvARB, glVertexAttrib4usvARB, NULL, 505),
    NAME_FUNC_OFFSET( 7917, glVertexAttribPointerARB, glVertexAttribPointerARB, NULL, 506),
    NAME_FUNC_OFFSET( 7942, glBindBufferARB, glBindBufferARB, NULL, 507),
    NAME_FUNC_OFFSET( 7958, glBufferDataARB, glBufferDataARB, NULL, 508),
    NAME_FUNC_OFFSET( 7974, glBufferSubDataARB, glBufferSubDataARB, NULL, 509),
    NAME_FUNC_OFFSET( 7993, glDeleteBuffersARB, glDeleteBuffersARB, NULL, 510),
    NAME_FUNC_OFFSET( 8012, glGenBuffersARB, glGenBuffersARB, NULL, 511),
    NAME_FUNC_OFFSET( 8028, glGetBufferParameterivARB, glGetBufferParameterivARB, NULL, 512),
    NAME_FUNC_OFFSET( 8054, glGetBufferPointervARB, glGetBufferPointervARB, NULL, 513),
    NAME_FUNC_OFFSET( 8077, glGetBufferSubDataARB, glGetBufferSubDataARB, NULL, 514),
    NAME_FUNC_OFFSET( 8099, glIsBufferARB, glIsBufferARB, NULL, 515),
    NAME_FUNC_OFFSET( 8113, glMapBufferARB, glMapBufferARB, NULL, 516),
    NAME_FUNC_OFFSET( 8128, glUnmapBufferARB, glUnmapBufferARB, NULL, 517),
    NAME_FUNC_OFFSET( 8145, glBeginQueryARB, glBeginQueryARB, NULL, 518),
    NAME_FUNC_OFFSET( 8161, glDeleteQueriesARB, glDeleteQueriesARB, NULL, 519),
    NAME_FUNC_OFFSET( 8180, glEndQueryARB, glEndQueryARB, NULL, 520),
    NAME_FUNC_OFFSET( 8194, glGenQueriesARB, glGenQueriesARB, NULL, 521),
    NAME_FUNC_OFFSET( 8210, glGetQueryObjectivARB, glGetQueryObjectivARB, NULL, 522),
    NAME_FUNC_OFFSET( 8232, glGetQueryObjectuivARB, glGetQueryObjectuivARB, NULL, 523),
    NAME_FUNC_OFFSET( 8255, glGetQueryivARB, glGetQueryivARB, NULL, 524),
    NAME_FUNC_OFFSET( 8271, glIsQueryARB, glIsQueryARB, NULL, 525),
    NAME_FUNC_OFFSET( 8284, glAttachObjectARB, glAttachObjectARB, NULL, 526),
    NAME_FUNC_OFFSET( 8302, glCompileShaderARB, glCompileShaderARB, NULL, 527),
    NAME_FUNC_OFFSET( 8321, glCreateProgramObjectARB, glCreateProgramObjectARB, NULL, 528),
    NAME_FUNC_OFFSET( 8346, glCreateShaderObjectARB, glCreateShaderObjectARB, NULL, 529),
    NAME_FUNC_OFFSET( 8370, glDeleteObjectARB, glDeleteObjectARB, NULL, 530),
    NAME_FUNC_OFFSET( 8388, glDetachObjectARB, glDetachObjectARB, NULL, 531),
    NAME_FUNC_OFFSET( 8406, glGetActiveUniformARB, glGetActiveUniformARB, NULL, 532),
    NAME_FUNC_OFFSET( 8428, glGetAttachedObjectsARB, glGetAttachedObjectsARB, NULL, 533),
    NAME_FUNC_OFFSET( 8452, glGetHandleARB, glGetHandleARB, NULL, 534),
    NAME_FUNC_OFFSET( 8467, glGetInfoLogARB, glGetInfoLogARB, NULL, 535),
    NAME_FUNC_OFFSET( 8483, glGetObjectParameterfvARB, glGetObjectParameterfvARB, NULL, 536),
    NAME_FUNC_OFFSET( 8509, glGetObjectParameterivARB, glGetObjectParameterivARB, NULL, 537),
    NAME_FUNC_OFFSET( 8535, glGetShaderSourceARB, glGetShaderSourceARB, NULL, 538),
    NAME_FUNC_OFFSET( 8556, glGetUniformLocationARB, glGetUniformLocationARB, NULL, 539),
    NAME_FUNC_OFFSET( 8580, glGetUniformfvARB, glGetUniformfvARB, NULL, 540),
    NAME_FUNC_OFFSET( 8598, glGetUniformivARB, glGetUniformivARB, NULL, 541),
    NAME_FUNC_OFFSET( 8616, glLinkProgramARB, glLinkProgramARB, NULL, 542),
    NAME_FUNC_OFFSET( 8633, glShaderSourceARB, glShaderSourceARB, NULL, 543),
    NAME_FUNC_OFFSET( 8651, glUniform1fARB, glUniform1fARB, NULL, 544),
    NAME_FUNC_OFFSET( 8666, glUniform1fvARB, glUniform1fvARB, NULL, 545),
    NAME_FUNC_OFFSET( 8682, glUniform1iARB, glUniform1iARB, NULL, 546),
    NAME_FUNC_OFFSET( 8697, glUniform1ivARB, glUniform1ivARB, NULL, 547),
    NAME_FUNC_OFFSET( 8713, glUniform2fARB, glUniform2fARB, NULL, 548),
    NAME_FUNC_OFFSET( 8728, glUniform2fvARB, glUniform2fvARB, NULL, 549),
    NAME_FUNC_OFFSET( 8744, glUniform2iARB, glUniform2iARB, NULL, 550),
    NAME_FUNC_OFFSET( 8759, glUniform2ivARB, glUniform2ivARB, NULL, 551),
    NAME_FUNC_OFFSET( 8775, glUniform3fARB, glUniform3fARB, NULL, 552),
    NAME_FUNC_OFFSET( 8790, glUniform3fvARB, glUniform3fvARB, NULL, 553),
    NAME_FUNC_OFFSET( 8806, glUniform3iARB, glUniform3iARB, NULL, 554),
    NAME_FUNC_OFFSET( 8821, glUniform3ivARB, glUniform3ivARB, NULL, 555),
    NAME_FUNC_OFFSET( 8837, glUniform4fARB, glUniform4fARB, NULL, 556),
    NAME_FUNC_OFFSET( 8852, glUniform4fvARB, glUniform4fvARB, NULL, 557),
    NAME_FUNC_OFFSET( 8868, glUniform4iARB, glUniform4iARB, NULL, 558),
    NAME_FUNC_OFFSET( 8883, glUniform4ivARB, glUniform4ivARB, NULL, 559),
    NAME_FUNC_OFFSET( 8899, glUniformMatrix2fvARB, glUniformMatrix2fvARB, NULL, 560),
    NAME_FUNC_OFFSET( 8921, glUniformMatrix3fvARB, glUniformMatrix3fvARB, NULL, 561),
    NAME_FUNC_OFFSET( 8943, glUniformMatrix4fvARB, glUniformMatrix4fvARB, NULL, 562),
    NAME_FUNC_OFFSET( 8965, glUseProgramObjectARB, glUseProgramObjectARB, NULL, 563),
    NAME_FUNC_OFFSET( 8987, glValidateProgramARB, glValidateProgramARB, NULL, 564),
    NAME_FUNC_OFFSET( 9008, glBindAttribLocationARB, glBindAttribLocationARB, NULL, 565),
    NAME_FUNC_OFFSET( 9032, glGetActiveAttribARB, glGetActiveAttribARB, NULL, 566),
    NAME_FUNC_OFFSET( 9053, glGetAttribLocationARB, glGetAttribLocationARB, NULL, 567),
    NAME_FUNC_OFFSET( 9076, glDrawBuffersARB, glDrawBuffersARB, NULL, 568),
    NAME_FUNC_OFFSET( 9093, glClampColorARB, glClampColorARB, NULL, 569),
    NAME_FUNC_OFFSET( 9109, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 570),
    NAME_FUNC_OFFSET( 9134, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 571),
    NAME_FUNC_OFFSET( 9161, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 572),
    NAME_FUNC_OFFSET( 9194, glFramebufferTextureFaceARB, glFramebufferTextureFaceARB, NULL, 573),
    NAME_FUNC_OFFSET( 9222, glVertexAttribDivisorARB, glVertexAttribDivisorARB, NULL, 574),
    NAME_FUNC_OFFSET( 9247, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 575),
    NAME_FUNC_OFFSET( 9272, glMapBufferRange, glMapBufferRange, NULL, 576),
    NAME_FUNC_OFFSET( 9289, glTexBufferARB, glTexBufferARB, NULL, 577),
    NAME_FUNC_OFFSET( 9304, glBindVertexArray, glBindVertexArray, NULL, 578),
    NAME_FUNC_OFFSET( 9322, glGenVertexArrays, glGenVertexArrays, NULL, 579),
    NAME_FUNC_OFFSET( 9340, glGetActiveUniformBlockName, glGetActiveUniformBlockName, NULL, 580),
    NAME_FUNC_OFFSET( 9368, glGetActiveUniformBlockiv, glGetActiveUniformBlockiv, NULL, 581),
    NAME_FUNC_OFFSET( 9394, glGetActiveUniformName, glGetActiveUniformName, NULL, 582),
    NAME_FUNC_OFFSET( 9417, glGetActiveUniformsiv, glGetActiveUniformsiv, NULL, 583),
    NAME_FUNC_OFFSET( 9439, glGetUniformBlockIndex, glGetUniformBlockIndex, NULL, 584),
    NAME_FUNC_OFFSET( 9462, glGetUniformIndices, glGetUniformIndices, NULL, 585),
    NAME_FUNC_OFFSET( 9482, glUniformBlockBinding, glUniformBlockBinding, NULL, 586),
    NAME_FUNC_OFFSET( 9504, glCopyBufferSubData, glCopyBufferSubData, NULL, 587),
    NAME_FUNC_OFFSET( 9524, glClientWaitSync, glClientWaitSync, NULL, 588),
    NAME_FUNC_OFFSET( 9541, glDeleteSync, glDeleteSync, NULL, 589),
    NAME_FUNC_OFFSET( 9554, glFenceSync, glFenceSync, NULL, 590),
    NAME_FUNC_OFFSET( 9566, glGetInteger64v, glGetInteger64v, NULL, 591),
    NAME_FUNC_OFFSET( 9582, glGetSynciv, glGetSynciv, NULL, 592),
    NAME_FUNC_OFFSET( 9594, glIsSync, glIsSync, NULL, 593),
    NAME_FUNC_OFFSET( 9603, glWaitSync, glWaitSync, NULL, 594),
    NAME_FUNC_OFFSET( 9614, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 595),
    NAME_FUNC_OFFSET( 9639, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 596),
    NAME_FUNC_OFFSET( 9673, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 597),
    NAME_FUNC_OFFSET( 9703, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 598),
    NAME_FUNC_OFFSET( 9733, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 599),
    NAME_FUNC_OFFSET( 9761, glBlendEquationiARB, glBlendEquationiARB, NULL, 600),
    NAME_FUNC_OFFSET( 9781, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 601),
    NAME_FUNC_OFFSET( 9805, glBlendFunciARB, glBlendFunciARB, NULL, 602),
    NAME_FUNC_OFFSET( 9821, glBindFragDataLocationIndexed, glBindFragDataLocationIndexed, NULL, 603),
    NAME_FUNC_OFFSET( 9851, glGetFragDataIndex, glGetFragDataIndex, NULL, 604),
    NAME_FUNC_OFFSET( 9870, glBindSampler, glBindSampler, NULL, 605),
    NAME_FUNC_OFFSET( 9884, glDeleteSamplers, glDeleteSamplers, NULL, 606),
    NAME_FUNC_OFFSET( 9901, glGenSamplers, glGenSamplers, NULL, 607),
    NAME_FUNC_OFFSET( 9915, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 608),
    NAME_FUNC_OFFSET( 9940, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 609),
    NAME_FUNC_OFFSET( 9966, glGetSamplerParameterfv, glGetSamplerParameterfv, NULL, 610),
    NAME_FUNC_OFFSET( 9990, glGetSamplerParameteriv, glGetSamplerParameteriv, NULL, 611),
    NAME_FUNC_OFFSET(10014, glIsSampler, glIsSampler, NULL, 612),
    NAME_FUNC_OFFSET(10026, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 613),
    NAME_FUNC_OFFSET(10048, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 614),
    NAME_FUNC_OFFSET(10071, glSamplerParameterf, glSamplerParameterf, NULL, 615),
    NAME_FUNC_OFFSET(10091, glSamplerParameterfv, glSamplerParameterfv, NULL, 616),
    NAME_FUNC_OFFSET(10112, glSamplerParameteri, glSamplerParameteri, NULL, 617),
    NAME_FUNC_OFFSET(10132, glSamplerParameteriv, glSamplerParameteriv, NULL, 618),
    NAME_FUNC_OFFSET(10153, gl_dispatch_stub_619, gl_dispatch_stub_619, NULL, 619),
    NAME_FUNC_OFFSET(10168, glColorP3ui, glColorP3ui, NULL, 620),
    NAME_FUNC_OFFSET(10180, glColorP3uiv, glColorP3uiv, NULL, 621),
    NAME_FUNC_OFFSET(10193, glColorP4ui, glColorP4ui, NULL, 622),
    NAME_FUNC_OFFSET(10205, glColorP4uiv, glColorP4uiv, NULL, 623),
    NAME_FUNC_OFFSET(10218, glMultiTexCoordP1ui, glMultiTexCoordP1ui, NULL, 624),
    NAME_FUNC_OFFSET(10238, glMultiTexCoordP1uiv, glMultiTexCoordP1uiv, NULL, 625),
    NAME_FUNC_OFFSET(10259, glMultiTexCoordP2ui, glMultiTexCoordP2ui, NULL, 626),
    NAME_FUNC_OFFSET(10279, glMultiTexCoordP2uiv, glMultiTexCoordP2uiv, NULL, 627),
    NAME_FUNC_OFFSET(10300, glMultiTexCoordP3ui, glMultiTexCoordP3ui, NULL, 628),
    NAME_FUNC_OFFSET(10320, glMultiTexCoordP3uiv, glMultiTexCoordP3uiv, NULL, 629),
    NAME_FUNC_OFFSET(10341, glMultiTexCoordP4ui, glMultiTexCoordP4ui, NULL, 630),
    NAME_FUNC_OFFSET(10361, glMultiTexCoordP4uiv, glMultiTexCoordP4uiv, NULL, 631),
    NAME_FUNC_OFFSET(10382, glNormalP3ui, glNormalP3ui, NULL, 632),
    NAME_FUNC_OFFSET(10395, glNormalP3uiv, glNormalP3uiv, NULL, 633),
    NAME_FUNC_OFFSET(10409, glSecondaryColorP3ui, glSecondaryColorP3ui, NULL, 634),
    NAME_FUNC_OFFSET(10430, glSecondaryColorP3uiv, glSecondaryColorP3uiv, NULL, 635),
    NAME_FUNC_OFFSET(10452, glTexCoordP1ui, glTexCoordP1ui, NULL, 636),
    NAME_FUNC_OFFSET(10467, glTexCoordP1uiv, glTexCoordP1uiv, NULL, 637),
    NAME_FUNC_OFFSET(10483, glTexCoordP2ui, glTexCoordP2ui, NULL, 638),
    NAME_FUNC_OFFSET(10498, glTexCoordP2uiv, glTexCoordP2uiv, NULL, 639),
    NAME_FUNC_OFFSET(10514, glTexCoordP3ui, glTexCoordP3ui, NULL, 640),
    NAME_FUNC_OFFSET(10529, glTexCoordP3uiv, glTexCoordP3uiv, NULL, 641),
    NAME_FUNC_OFFSET(10545, glTexCoordP4ui, glTexCoordP4ui, NULL, 642),
    NAME_FUNC_OFFSET(10560, glTexCoordP4uiv, glTexCoordP4uiv, NULL, 643),
    NAME_FUNC_OFFSET(10576, glVertexAttribP1ui, glVertexAttribP1ui, NULL, 644),
    NAME_FUNC_OFFSET(10595, glVertexAttribP1uiv, glVertexAttribP1uiv, NULL, 645),
    NAME_FUNC_OFFSET(10615, glVertexAttribP2ui, glVertexAttribP2ui, NULL, 646),
    NAME_FUNC_OFFSET(10634, glVertexAttribP2uiv, glVertexAttribP2uiv, NULL, 647),
    NAME_FUNC_OFFSET(10654, glVertexAttribP3ui, glVertexAttribP3ui, NULL, 648),
    NAME_FUNC_OFFSET(10673, glVertexAttribP3uiv, glVertexAttribP3uiv, NULL, 649),
    NAME_FUNC_OFFSET(10693, glVertexAttribP4ui, glVertexAttribP4ui, NULL, 650),
    NAME_FUNC_OFFSET(10712, glVertexAttribP4uiv, glVertexAttribP4uiv, NULL, 651),
    NAME_FUNC_OFFSET(10732, glVertexP2ui, glVertexP2ui, NULL, 652),
    NAME_FUNC_OFFSET(10745, glVertexP2uiv, glVertexP2uiv, NULL, 653),
    NAME_FUNC_OFFSET(10759, glVertexP3ui, glVertexP3ui, NULL, 654),
    NAME_FUNC_OFFSET(10772, glVertexP3uiv, glVertexP3uiv, NULL, 655),
    NAME_FUNC_OFFSET(10786, glVertexP4ui, glVertexP4ui, NULL, 656),
    NAME_FUNC_OFFSET(10799, glVertexP4uiv, glVertexP4uiv, NULL, 657),
    NAME_FUNC_OFFSET(10813, glBindTransformFeedback, glBindTransformFeedback, NULL, 658),
    NAME_FUNC_OFFSET(10837, glDeleteTransformFeedbacks, glDeleteTransformFeedbacks, NULL, 659),
    NAME_FUNC_OFFSET(10864, glDrawTransformFeedback, glDrawTransformFeedback, NULL, 660),
    NAME_FUNC_OFFSET(10888, glGenTransformFeedbacks, glGenTransformFeedbacks, NULL, 661),
    NAME_FUNC_OFFSET(10912, glIsTransformFeedback, glIsTransformFeedback, NULL, 662),
    NAME_FUNC_OFFSET(10934, glPauseTransformFeedback, glPauseTransformFeedback, NULL, 663),
    NAME_FUNC_OFFSET(10959, glResumeTransformFeedback, glResumeTransformFeedback, NULL, 664),
    NAME_FUNC_OFFSET(10985, glBeginQueryIndexed, glBeginQueryIndexed, NULL, 665),
    NAME_FUNC_OFFSET(11005, glDrawTransformFeedbackStream, glDrawTransformFeedbackStream, NULL, 666),
    NAME_FUNC_OFFSET(11035, glEndQueryIndexed, glEndQueryIndexed, NULL, 667),
    NAME_FUNC_OFFSET(11053, glGetQueryIndexediv, glGetQueryIndexediv, NULL, 668),
    NAME_FUNC_OFFSET(11073, glClearDepthf, glClearDepthf, NULL, 669),
    NAME_FUNC_OFFSET(11087, glDepthRangef, glDepthRangef, NULL, 670),
    NAME_FUNC_OFFSET(11101, glGetShaderPrecisionFormat, glGetShaderPrecisionFormat, NULL, 671),
    NAME_FUNC_OFFSET(11128, glReleaseShaderCompiler, glReleaseShaderCompiler, NULL, 672),
    NAME_FUNC_OFFSET(11152, glShaderBinary, glShaderBinary, NULL, 673),
    NAME_FUNC_OFFSET(11167, gl_dispatch_stub_674, gl_dispatch_stub_674, NULL, 674),
    NAME_FUNC_OFFSET(11186, gl_dispatch_stub_675, gl_dispatch_stub_675, NULL, 675),
    NAME_FUNC_OFFSET(11202, gl_dispatch_stub_676, gl_dispatch_stub_676, NULL, 676),
    NAME_FUNC_OFFSET(11222, glDebugMessageCallbackARB, glDebugMessageCallbackARB, NULL, 677),
    NAME_FUNC_OFFSET(11248, glDebugMessageControlARB, glDebugMessageControlARB, NULL, 678),
    NAME_FUNC_OFFSET(11273, glDebugMessageInsertARB, glDebugMessageInsertARB, NULL, 679),
    NAME_FUNC_OFFSET(11297, glGetDebugMessageLogARB, glGetDebugMessageLogARB, NULL, 680),
    NAME_FUNC_OFFSET(11321, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 681),
    NAME_FUNC_OFFSET(11349, glGetnColorTableARB, glGetnColorTableARB, NULL, 682),
    NAME_FUNC_OFFSET(11369, glGetnCompressedTexImageARB, glGetnCompressedTexImageARB, NULL, 683),
    NAME_FUNC_OFFSET(11397, glGetnConvolutionFilterARB, glGetnConvolutionFilterARB, NULL, 684),
    NAME_FUNC_OFFSET(11424, glGetnHistogramARB, glGetnHistogramARB, NULL, 685),
    NAME_FUNC_OFFSET(11443, glGetnMapdvARB, glGetnMapdvARB, NULL, 686),
    NAME_FUNC_OFFSET(11458, glGetnMapfvARB, glGetnMapfvARB, NULL, 687),
    NAME_FUNC_OFFSET(11473, glGetnMapivARB, glGetnMapivARB, NULL, 688),
    NAME_FUNC_OFFSET(11488, glGetnMinmaxARB, glGetnMinmaxARB, NULL, 689),
    NAME_FUNC_OFFSET(11504, glGetnPixelMapfvARB, glGetnPixelMapfvARB, NULL, 690),
    NAME_FUNC_OFFSET(11524, glGetnPixelMapuivARB, glGetnPixelMapuivARB, NULL, 691),
    NAME_FUNC_OFFSET(11545, glGetnPixelMapusvARB, glGetnPixelMapusvARB, NULL, 692),
    NAME_FUNC_OFFSET(11566, glGetnPolygonStippleARB, glGetnPolygonStippleARB, NULL, 693),
    NAME_FUNC_OFFSET(11590, glGetnSeparableFilterARB, glGetnSeparableFilterARB, NULL, 694),
    NAME_FUNC_OFFSET(11615, glGetnTexImageARB, glGetnTexImageARB, NULL, 695),
    NAME_FUNC_OFFSET(11633, glGetnUniformdvARB, glGetnUniformdvARB, NULL, 696),
    NAME_FUNC_OFFSET(11652, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 697),
    NAME_FUNC_OFFSET(11671, glGetnUniformivARB, glGetnUniformivARB, NULL, 698),
    NAME_FUNC_OFFSET(11690, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 699),
    NAME_FUNC_OFFSET(11710, glReadnPixelsARB, glReadnPixelsARB, NULL, 700),
    NAME_FUNC_OFFSET(11727, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 701),
    NAME_FUNC_OFFSET(11761, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 702),
    NAME_FUNC_OFFSET(11797, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 703),
    NAME_FUNC_OFFSET(11843, glDrawTransformFeedbackInstanced, glDrawTransformFeedbackInstanced, NULL, 704),
    NAME_FUNC_OFFSET(11876, glDrawTransformFeedbackStreamInstanced, glDrawTransformFeedbackStreamInstanced, NULL, 705),
    NAME_FUNC_OFFSET(11915, glTexStorage1D, glTexStorage1D, NULL, 706),
    NAME_FUNC_OFFSET(11930, glTexStorage2D, glTexStorage2D, NULL, 707),
    NAME_FUNC_OFFSET(11945, glTexStorage3D, glTexStorage3D, NULL, 708),
    NAME_FUNC_OFFSET(11960, glTextureStorage1DEXT, glTextureStorage1DEXT, NULL, 709),
    NAME_FUNC_OFFSET(11982, glTextureStorage2DEXT, glTextureStorage2DEXT, NULL, 710),
    NAME_FUNC_OFFSET(12004, glTextureStorage3DEXT, glTextureStorage3DEXT, NULL, 711),
    NAME_FUNC_OFFSET(12026, glInvalidateBufferData, glInvalidateBufferData, NULL, 712),
    NAME_FUNC_OFFSET(12049, glInvalidateBufferSubData, glInvalidateBufferSubData, NULL, 713),
    NAME_FUNC_OFFSET(12075, glInvalidateFramebuffer, glInvalidateFramebuffer, NULL, 714),
    NAME_FUNC_OFFSET(12099, glInvalidateSubFramebuffer, glInvalidateSubFramebuffer, NULL, 715),
    NAME_FUNC_OFFSET(12126, glInvalidateTexImage, glInvalidateTexImage, NULL, 716),
    NAME_FUNC_OFFSET(12147, glInvalidateTexSubImage, glInvalidateTexSubImage, NULL, 717),
    NAME_FUNC_OFFSET(12171, glPolygonOffsetEXT, glPolygonOffsetEXT, NULL, 718),
    NAME_FUNC_OFFSET(12190, gl_dispatch_stub_719, gl_dispatch_stub_719, NULL, 719),
    NAME_FUNC_OFFSET(12204, gl_dispatch_stub_720, gl_dispatch_stub_720, NULL, 720),
    NAME_FUNC_OFFSET(12219, gl_dispatch_stub_721, gl_dispatch_stub_721, NULL, 721),
    NAME_FUNC_OFFSET(12233, gl_dispatch_stub_722, gl_dispatch_stub_722, NULL, 722),
    NAME_FUNC_OFFSET(12248, gl_dispatch_stub_723, gl_dispatch_stub_723, NULL, 723),
    NAME_FUNC_OFFSET(12262, gl_dispatch_stub_724, gl_dispatch_stub_724, NULL, 724),
    NAME_FUNC_OFFSET(12277, gl_dispatch_stub_725, gl_dispatch_stub_725, NULL, 725),
    NAME_FUNC_OFFSET(12291, gl_dispatch_stub_726, gl_dispatch_stub_726, NULL, 726),
    NAME_FUNC_OFFSET(12306, gl_dispatch_stub_727, gl_dispatch_stub_727, NULL, 727),
    NAME_FUNC_OFFSET(12328, gl_dispatch_stub_728, gl_dispatch_stub_728, NULL, 728),
    NAME_FUNC_OFFSET(12346, gl_dispatch_stub_729, gl_dispatch_stub_729, NULL, 729),
    NAME_FUNC_OFFSET(12363, gl_dispatch_stub_730, gl_dispatch_stub_730, NULL, 730),
    NAME_FUNC_OFFSET(12383, glColorPointerEXT, glColorPointerEXT, NULL, 731),
    NAME_FUNC_OFFSET(12401, glEdgeFlagPointerEXT, glEdgeFlagPointerEXT, NULL, 732),
    NAME_FUNC_OFFSET(12422, glIndexPointerEXT, glIndexPointerEXT, NULL, 733),
    NAME_FUNC_OFFSET(12440, glNormalPointerEXT, glNormalPointerEXT, NULL, 734),
    NAME_FUNC_OFFSET(12459, glTexCoordPointerEXT, glTexCoordPointerEXT, NULL, 735),
    NAME_FUNC_OFFSET(12480, glVertexPointerEXT, glVertexPointerEXT, NULL, 736),
    NAME_FUNC_OFFSET(12499, glPointParameterfEXT, glPointParameterfEXT, NULL, 737),
    NAME_FUNC_OFFSET(12520, glPointParameterfvEXT, glPointParameterfvEXT, NULL, 738),
    NAME_FUNC_OFFSET(12542, glLockArraysEXT, glLockArraysEXT, NULL, 739),
    NAME_FUNC_OFFSET(12558, glUnlockArraysEXT, glUnlockArraysEXT, NULL, 740),
    NAME_FUNC_OFFSET(12576, glSecondaryColor3bEXT, glSecondaryColor3bEXT, NULL, 741),
    NAME_FUNC_OFFSET(12598, glSecondaryColor3bvEXT, glSecondaryColor3bvEXT, NULL, 742),
    NAME_FUNC_OFFSET(12621, glSecondaryColor3dEXT, glSecondaryColor3dEXT, NULL, 743),
    NAME_FUNC_OFFSET(12643, glSecondaryColor3dvEXT, glSecondaryColor3dvEXT, NULL, 744),
    NAME_FUNC_OFFSET(12666, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 745),
    NAME_FUNC_OFFSET(12688, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 746),
    NAME_FUNC_OFFSET(12711, glSecondaryColor3iEXT, glSecondaryColor3iEXT, NULL, 747),
    NAME_FUNC_OFFSET(12733, glSecondaryColor3ivEXT, glSecondaryColor3ivEXT, NULL, 748),
    NAME_FUNC_OFFSET(12756, glSecondaryColor3sEXT, glSecondaryColor3sEXT, NULL, 749),
    NAME_FUNC_OFFSET(12778, glSecondaryColor3svEXT, glSecondaryColor3svEXT, NULL, 750),
    NAME_FUNC_OFFSET(12801, glSecondaryColor3ubEXT, glSecondaryColor3ubEXT, NULL, 751),
    NAME_FUNC_OFFSET(12824, glSecondaryColor3ubvEXT, glSecondaryColor3ubvEXT, NULL, 752),
    NAME_FUNC_OFFSET(12848, glSecondaryColor3uiEXT, glSecondaryColor3uiEXT, NULL, 753),
    NAME_FUNC_OFFSET(12871, glSecondaryColor3uivEXT, glSecondaryColor3uivEXT, NULL, 754),
    NAME_FUNC_OFFSET(12895, glSecondaryColor3usEXT, glSecondaryColor3usEXT, NULL, 755),
    NAME_FUNC_OFFSET(12918, glSecondaryColor3usvEXT, glSecondaryColor3usvEXT, NULL, 756),
    NAME_FUNC_OFFSET(12942, glSecondaryColorPointerEXT, glSecondaryColorPointerEXT, NULL, 757),
    NAME_FUNC_OFFSET(12969, glMultiDrawArraysEXT, glMultiDrawArraysEXT, NULL, 758),
    NAME_FUNC_OFFSET(12990, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 759),
    NAME_FUNC_OFFSET(13013, glFogCoordPointerEXT, glFogCoordPointerEXT, NULL, 760),
    NAME_FUNC_OFFSET(13034, glFogCoorddEXT, glFogCoorddEXT, NULL, 761),
    NAME_FUNC_OFFSET(13049, glFogCoorddvEXT, glFogCoorddvEXT, NULL, 762),
    NAME_FUNC_OFFSET(13065, glFogCoordfEXT, glFogCoordfEXT, NULL, 763),
    NAME_FUNC_OFFSET(13080, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 764),
    NAME_FUNC_OFFSET(13096, glBlendFuncSeparateEXT, glBlendFuncSeparateEXT, NULL, 765),
    NAME_FUNC_OFFSET(13119, glResizeBuffersMESA, glResizeBuffersMESA, NULL, 766),
    NAME_FUNC_OFFSET(13139, glWindowPos2dMESA, glWindowPos2dMESA, NULL, 767),
    NAME_FUNC_OFFSET(13157, glWindowPos2dvMESA, glWindowPos2dvMESA, NULL, 768),
    NAME_FUNC_OFFSET(13176, glWindowPos2fMESA, glWindowPos2fMESA, NULL, 769),
    NAME_FUNC_OFFSET(13194, glWindowPos2fvMESA, glWindowPos2fvMESA, NULL, 770),
    NAME_FUNC_OFFSET(13213, glWindowPos2iMESA, glWindowPos2iMESA, NULL, 771),
    NAME_FUNC_OFFSET(13231, glWindowPos2ivMESA, glWindowPos2ivMESA, NULL, 772),
    NAME_FUNC_OFFSET(13250, glWindowPos2sMESA, glWindowPos2sMESA, NULL, 773),
    NAME_FUNC_OFFSET(13268, glWindowPos2svMESA, glWindowPos2svMESA, NULL, 774),
    NAME_FUNC_OFFSET(13287, glWindowPos3dMESA, glWindowPos3dMESA, NULL, 775),
    NAME_FUNC_OFFSET(13305, glWindowPos3dvMESA, glWindowPos3dvMESA, NULL, 776),
    NAME_FUNC_OFFSET(13324, glWindowPos3fMESA, glWindowPos3fMESA, NULL, 777),
    NAME_FUNC_OFFSET(13342, glWindowPos3fvMESA, glWindowPos3fvMESA, NULL, 778),
    NAME_FUNC_OFFSET(13361, glWindowPos3iMESA, glWindowPos3iMESA, NULL, 779),
    NAME_FUNC_OFFSET(13379, glWindowPos3ivMESA, glWindowPos3ivMESA, NULL, 780),
    NAME_FUNC_OFFSET(13398, glWindowPos3sMESA, glWindowPos3sMESA, NULL, 781),
    NAME_FUNC_OFFSET(13416, glWindowPos3svMESA, glWindowPos3svMESA, NULL, 782),
    NAME_FUNC_OFFSET(13435, glWindowPos4dMESA, glWindowPos4dMESA, NULL, 783),
    NAME_FUNC_OFFSET(13453, glWindowPos4dvMESA, glWindowPos4dvMESA, NULL, 784),
    NAME_FUNC_OFFSET(13472, glWindowPos4fMESA, glWindowPos4fMESA, NULL, 785),
    NAME_FUNC_OFFSET(13490, glWindowPos4fvMESA, glWindowPos4fvMESA, NULL, 786),
    NAME_FUNC_OFFSET(13509, glWindowPos4iMESA, glWindowPos4iMESA, NULL, 787),
    NAME_FUNC_OFFSET(13527, glWindowPos4ivMESA, glWindowPos4ivMESA, NULL, 788),
    NAME_FUNC_OFFSET(13546, glWindowPos4sMESA, glWindowPos4sMESA, NULL, 789),
    NAME_FUNC_OFFSET(13564, glWindowPos4svMESA, glWindowPos4svMESA, NULL, 790),
    NAME_FUNC_OFFSET(13583, gl_dispatch_stub_791, gl_dispatch_stub_791, NULL, 791),
    NAME_FUNC_OFFSET(13608, gl_dispatch_stub_792, gl_dispatch_stub_792, NULL, 792),
    NAME_FUNC_OFFSET(13635, glAreProgramsResidentNV, glAreProgramsResidentNV, NULL, 793),
    NAME_FUNC_OFFSET(13659, glBindProgramNV, glBindProgramNV, NULL, 794),
    NAME_FUNC_OFFSET(13675, glDeleteProgramsNV, glDeleteProgramsNV, NULL, 795),
    NAME_FUNC_OFFSET(13694, glExecuteProgramNV, glExecuteProgramNV, NULL, 796),
    NAME_FUNC_OFFSET(13713, glGenProgramsNV, glGenProgramsNV, NULL, 797),
    NAME_FUNC_OFFSET(13729, glGetProgramParameterdvNV, glGetProgramParameterdvNV, NULL, 798),
    NAME_FUNC_OFFSET(13755, glGetProgramParameterfvNV, glGetProgramParameterfvNV, NULL, 799),
    NAME_FUNC_OFFSET(13781, glGetProgramStringNV, glGetProgramStringNV, NULL, 800),
    NAME_FUNC_OFFSET(13802, glGetProgramivNV, glGetProgramivNV, NULL, 801),
    NAME_FUNC_OFFSET(13819, glGetTrackMatrixivNV, glGetTrackMatrixivNV, NULL, 802),
    NAME_FUNC_OFFSET(13840, glGetVertexAttribPointervNV, glGetVertexAttribPointervNV, NULL, 803),
    NAME_FUNC_OFFSET(13868, glGetVertexAttribdvNV, glGetVertexAttribdvNV, NULL, 804),
    NAME_FUNC_OFFSET(13890, glGetVertexAttribfvNV, glGetVertexAttribfvNV, NULL, 805),
    NAME_FUNC_OFFSET(13912, glGetVertexAttribivNV, glGetVertexAttribivNV, NULL, 806),
    NAME_FUNC_OFFSET(13934, glIsProgramNV, glIsProgramNV, NULL, 807),
    NAME_FUNC_OFFSET(13948, glLoadProgramNV, glLoadProgramNV, NULL, 808),
    NAME_FUNC_OFFSET(13964, glProgramParameters4dvNV, glProgramParameters4dvNV, NULL, 809),
    NAME_FUNC_OFFSET(13989, glProgramParameters4fvNV, glProgramParameters4fvNV, NULL, 810),
    NAME_FUNC_OFFSET(14014, glRequestResidentProgramsNV, glRequestResidentProgramsNV, NULL, 811),
    NAME_FUNC_OFFSET(14042, glTrackMatrixNV, glTrackMatrixNV, NULL, 812),
    NAME_FUNC_OFFSET(14058, glVertexAttrib1dNV, glVertexAttrib1dNV, NULL, 813),
    NAME_FUNC_OFFSET(14077, glVertexAttrib1dvNV, glVertexAttrib1dvNV, NULL, 814),
    NAME_FUNC_OFFSET(14097, glVertexAttrib1fNV, glVertexAttrib1fNV, NULL, 815),
    NAME_FUNC_OFFSET(14116, glVertexAttrib1fvNV, glVertexAttrib1fvNV, NULL, 816),
    NAME_FUNC_OFFSET(14136, glVertexAttrib1sNV, glVertexAttrib1sNV, NULL, 817),
    NAME_FUNC_OFFSET(14155, glVertexAttrib1svNV, glVertexAttrib1svNV, NULL, 818),
    NAME_FUNC_OFFSET(14175, glVertexAttrib2dNV, glVertexAttrib2dNV, NULL, 819),
    NAME_FUNC_OFFSET(14194, glVertexAttrib2dvNV, glVertexAttrib2dvNV, NULL, 820),
    NAME_FUNC_OFFSET(14214, glVertexAttrib2fNV, glVertexAttrib2fNV, NULL, 821),
    NAME_FUNC_OFFSET(14233, glVertexAttrib2fvNV, glVertexAttrib2fvNV, NULL, 822),
    NAME_FUNC_OFFSET(14253, glVertexAttrib2sNV, glVertexAttrib2sNV, NULL, 823),
    NAME_FUNC_OFFSET(14272, glVertexAttrib2svNV, glVertexAttrib2svNV, NULL, 824),
    NAME_FUNC_OFFSET(14292, glVertexAttrib3dNV, glVertexAttrib3dNV, NULL, 825),
    NAME_FUNC_OFFSET(14311, glVertexAttrib3dvNV, glVertexAttrib3dvNV, NULL, 826),
    NAME_FUNC_OFFSET(14331, glVertexAttrib3fNV, glVertexAttrib3fNV, NULL, 827),
    NAME_FUNC_OFFSET(14350, glVertexAttrib3fvNV, glVertexAttrib3fvNV, NULL, 828),
    NAME_FUNC_OFFSET(14370, glVertexAttrib3sNV, glVertexAttrib3sNV, NULL, 829),
    NAME_FUNC_OFFSET(14389, glVertexAttrib3svNV, glVertexAttrib3svNV, NULL, 830),
    NAME_FUNC_OFFSET(14409, glVertexAttrib4dNV, glVertexAttrib4dNV, NULL, 831),
    NAME_FUNC_OFFSET(14428, glVertexAttrib4dvNV, glVertexAttrib4dvNV, NULL, 832),
    NAME_FUNC_OFFSET(14448, glVertexAttrib4fNV, glVertexAttrib4fNV, NULL, 833),
    NAME_FUNC_OFFSET(14467, glVertexAttrib4fvNV, glVertexAttrib4fvNV, NULL, 834),
    NAME_FUNC_OFFSET(14487, glVertexAttrib4sNV, glVertexAttrib4sNV, NULL, 835),
    NAME_FUNC_OFFSET(14506, glVertexAttrib4svNV, glVertexAttrib4svNV, NULL, 836),
    NAME_FUNC_OFFSET(14526, glVertexAttrib4ubNV, glVertexAttrib4ubNV, NULL, 837),
    NAME_FUNC_OFFSET(14546, glVertexAttrib4ubvNV, glVertexAttrib4ubvNV, NULL, 838),
    NAME_FUNC_OFFSET(14567, glVertexAttribPointerNV, glVertexAttribPointerNV, NULL, 839),
    NAME_FUNC_OFFSET(14591, glVertexAttribs1dvNV, glVertexAttribs1dvNV, NULL, 840),
    NAME_FUNC_OFFSET(14612, glVertexAttribs1fvNV, glVertexAttribs1fvNV, NULL, 841),
    NAME_FUNC_OFFSET(14633, glVertexAttribs1svNV, glVertexAttribs1svNV, NULL, 842),
    NAME_FUNC_OFFSET(14654, glVertexAttribs2dvNV, glVertexAttribs2dvNV, NULL, 843),
    NAME_FUNC_OFFSET(14675, glVertexAttribs2fvNV, glVertexAttribs2fvNV, NULL, 844),
    NAME_FUNC_OFFSET(14696, glVertexAttribs2svNV, glVertexAttribs2svNV, NULL, 845),
    NAME_FUNC_OFFSET(14717, glVertexAttribs3dvNV, glVertexAttribs3dvNV, NULL, 846),
    NAME_FUNC_OFFSET(14738, glVertexAttribs3fvNV, glVertexAttribs3fvNV, NULL, 847),
    NAME_FUNC_OFFSET(14759, glVertexAttribs3svNV, glVertexAttribs3svNV, NULL, 848),
    NAME_FUNC_OFFSET(14780, glVertexAttribs4dvNV, glVertexAttribs4dvNV, NULL, 849),
    NAME_FUNC_OFFSET(14801, glVertexAttribs4fvNV, glVertexAttribs4fvNV, NULL, 850),
    NAME_FUNC_OFFSET(14822, glVertexAttribs4svNV, glVertexAttribs4svNV, NULL, 851),
    NAME_FUNC_OFFSET(14843, glVertexAttribs4ubvNV, glVertexAttribs4ubvNV, NULL, 852),
    NAME_FUNC_OFFSET(14865, glGetTexBumpParameterfvATI, glGetTexBumpParameterfvATI, NULL, 853),
    NAME_FUNC_OFFSET(14892, glGetTexBumpParameterivATI, glGetTexBumpParameterivATI, NULL, 854),
    NAME_FUNC_OFFSET(14919, glTexBumpParameterfvATI, glTexBumpParameterfvATI, NULL, 855),
    NAME_FUNC_OFFSET(14943, glTexBumpParameterivATI, glTexBumpParameterivATI, NULL, 856),
    NAME_FUNC_OFFSET(14967, glAlphaFragmentOp1ATI, glAlphaFragmentOp1ATI, NULL, 857),
    NAME_FUNC_OFFSET(14989, glAlphaFragmentOp2ATI, glAlphaFragmentOp2ATI, NULL, 858),
    NAME_FUNC_OFFSET(15011, glAlphaFragmentOp3ATI, glAlphaFragmentOp3ATI, NULL, 859),
    NAME_FUNC_OFFSET(15033, glBeginFragmentShaderATI, glBeginFragmentShaderATI, NULL, 860),
    NAME_FUNC_OFFSET(15058, glBindFragmentShaderATI, glBindFragmentShaderATI, NULL, 861),
    NAME_FUNC_OFFSET(15082, glColorFragmentOp1ATI, glColorFragmentOp1ATI, NULL, 862),
    NAME_FUNC_OFFSET(15104, glColorFragmentOp2ATI, glColorFragmentOp2ATI, NULL, 863),
    NAME_FUNC_OFFSET(15126, glColorFragmentOp3ATI, glColorFragmentOp3ATI, NULL, 864),
    NAME_FUNC_OFFSET(15148, glDeleteFragmentShaderATI, glDeleteFragmentShaderATI, NULL, 865),
    NAME_FUNC_OFFSET(15174, glEndFragmentShaderATI, glEndFragmentShaderATI, NULL, 866),
    NAME_FUNC_OFFSET(15197, glGenFragmentShadersATI, glGenFragmentShadersATI, NULL, 867),
    NAME_FUNC_OFFSET(15221, glPassTexCoordATI, glPassTexCoordATI, NULL, 868),
    NAME_FUNC_OFFSET(15239, glSampleMapATI, glSampleMapATI, NULL, 869),
    NAME_FUNC_OFFSET(15254, glSetFragmentShaderConstantATI, glSetFragmentShaderConstantATI, NULL, 870),
    NAME_FUNC_OFFSET(15285, glPointParameteriNV, glPointParameteriNV, NULL, 871),
    NAME_FUNC_OFFSET(15305, glPointParameterivNV, glPointParameterivNV, NULL, 872),
    NAME_FUNC_OFFSET(15326, gl_dispatch_stub_873, gl_dispatch_stub_873, NULL, 873),
    NAME_FUNC_OFFSET(15349, gl_dispatch_stub_874, gl_dispatch_stub_874, NULL, 874),
    NAME_FUNC_OFFSET(15372, gl_dispatch_stub_875, gl_dispatch_stub_875, NULL, 875),
    NAME_FUNC_OFFSET(15398, gl_dispatch_stub_876, gl_dispatch_stub_876, NULL, 876),
    NAME_FUNC_OFFSET(15421, gl_dispatch_stub_877, gl_dispatch_stub_877, NULL, 877),
    NAME_FUNC_OFFSET(15442, glGetProgramNamedParameterdvNV, glGetProgramNamedParameterdvNV, NULL, 878),
    NAME_FUNC_OFFSET(15473, glGetProgramNamedParameterfvNV, glGetProgramNamedParameterfvNV, NULL, 879),
    NAME_FUNC_OFFSET(15504, glProgramNamedParameter4dNV, glProgramNamedParameter4dNV, NULL, 880),
    NAME_FUNC_OFFSET(15532, glProgramNamedParameter4dvNV, glProgramNamedParameter4dvNV, NULL, 881),
    NAME_FUNC_OFFSET(15561, glProgramNamedParameter4fNV, glProgramNamedParameter4fNV, NULL, 882),
    NAME_FUNC_OFFSET(15589, glProgramNamedParameter4fvNV, glProgramNamedParameter4fvNV, NULL, 883),
    NAME_FUNC_OFFSET(15618, glPrimitiveRestartIndexNV, glPrimitiveRestartIndexNV, NULL, 884),
    NAME_FUNC_OFFSET(15644, glPrimitiveRestartNV, glPrimitiveRestartNV, NULL, 885),
    NAME_FUNC_OFFSET(15665, gl_dispatch_stub_886, gl_dispatch_stub_886, NULL, 886),
    NAME_FUNC_OFFSET(15681, gl_dispatch_stub_887, gl_dispatch_stub_887, NULL, 887),
    NAME_FUNC_OFFSET(15698, gl_dispatch_stub_888, gl_dispatch_stub_888, NULL, 888),
    NAME_FUNC_OFFSET(15715, gl_dispatch_stub_889, gl_dispatch_stub_889, NULL, 889),
    NAME_FUNC_OFFSET(15731, gl_dispatch_stub_890, gl_dispatch_stub_890, NULL, 890),
    NAME_FUNC_OFFSET(15744, gl_dispatch_stub_891, gl_dispatch_stub_891, NULL, 891),
    NAME_FUNC_OFFSET(15761, gl_dispatch_stub_892, gl_dispatch_stub_892, NULL, 892),
    NAME_FUNC_OFFSET(15771, gl_dispatch_stub_893, gl_dispatch_stub_893, NULL, 893),
    NAME_FUNC_OFFSET(15782, gl_dispatch_stub_894, gl_dispatch_stub_894, NULL, 894),
    NAME_FUNC_OFFSET(15796, gl_dispatch_stub_895, gl_dispatch_stub_895, NULL, 895),
    NAME_FUNC_OFFSET(15815, gl_dispatch_stub_896, gl_dispatch_stub_896, NULL, 896),
    NAME_FUNC_OFFSET(15830, gl_dispatch_stub_897, gl_dispatch_stub_897, NULL, 897),
    NAME_FUNC_OFFSET(15846, gl_dispatch_stub_898, gl_dispatch_stub_898, NULL, 898),
    NAME_FUNC_OFFSET(15865, gl_dispatch_stub_899, gl_dispatch_stub_899, NULL, 899),
    NAME_FUNC_OFFSET(15882, gl_dispatch_stub_900, gl_dispatch_stub_900, NULL, 900),
    NAME_FUNC_OFFSET(15899, gl_dispatch_stub_901, gl_dispatch_stub_901, NULL, 901),
    NAME_FUNC_OFFSET(15922, gl_dispatch_stub_902, gl_dispatch_stub_902, NULL, 902),
    NAME_FUNC_OFFSET(15939, gl_dispatch_stub_903, gl_dispatch_stub_903, NULL, 903),
    NAME_FUNC_OFFSET(15957, gl_dispatch_stub_904, gl_dispatch_stub_904, NULL, 904),
    NAME_FUNC_OFFSET(15969, gl_dispatch_stub_905, gl_dispatch_stub_905, NULL, 905),
    NAME_FUNC_OFFSET(15982, gl_dispatch_stub_906, gl_dispatch_stub_906, NULL, 906),
    NAME_FUNC_OFFSET(15998, gl_dispatch_stub_907, gl_dispatch_stub_907, NULL, 907),
    NAME_FUNC_OFFSET(16015, gl_dispatch_stub_908, gl_dispatch_stub_908, NULL, 908),
    NAME_FUNC_OFFSET(16030, gl_dispatch_stub_909, gl_dispatch_stub_909, NULL, 909),
    NAME_FUNC_OFFSET(16046, gl_dispatch_stub_910, gl_dispatch_stub_910, NULL, 910),
    NAME_FUNC_OFFSET(16063, gl_dispatch_stub_911, gl_dispatch_stub_911, NULL, 911),
    NAME_FUNC_OFFSET(16084, gl_dispatch_stub_912, gl_dispatch_stub_912, NULL, 912),
    NAME_FUNC_OFFSET(16098, gl_dispatch_stub_913, gl_dispatch_stub_913, NULL, 913),
    NAME_FUNC_OFFSET(16110, gl_dispatch_stub_914, gl_dispatch_stub_914, NULL, 914),
    NAME_FUNC_OFFSET(16131, gl_dispatch_stub_915, gl_dispatch_stub_915, NULL, 915),
    NAME_FUNC_OFFSET(16153, gl_dispatch_stub_916, gl_dispatch_stub_916, NULL, 916),
    NAME_FUNC_OFFSET(16169, gl_dispatch_stub_917, gl_dispatch_stub_917, NULL, 917),
    NAME_FUNC_OFFSET(16189, gl_dispatch_stub_918, gl_dispatch_stub_918, NULL, 918),
    NAME_FUNC_OFFSET(16202, gl_dispatch_stub_919, gl_dispatch_stub_919, NULL, 919),
    NAME_FUNC_OFFSET(16223, gl_dispatch_stub_920, gl_dispatch_stub_920, NULL, 920),
    NAME_FUNC_OFFSET(16235, gl_dispatch_stub_921, gl_dispatch_stub_921, NULL, 921),
    NAME_FUNC_OFFSET(16248, gl_dispatch_stub_922, gl_dispatch_stub_922, NULL, 922),
    NAME_FUNC_OFFSET(16262, gl_dispatch_stub_923, gl_dispatch_stub_923, NULL, 923),
    NAME_FUNC_OFFSET(16275, gl_dispatch_stub_924, gl_dispatch_stub_924, NULL, 924),
    NAME_FUNC_OFFSET(16289, gl_dispatch_stub_925, gl_dispatch_stub_925, NULL, 925),
    NAME_FUNC_OFFSET(16308, gl_dispatch_stub_926, gl_dispatch_stub_926, NULL, 926),
    NAME_FUNC_OFFSET(16328, gl_dispatch_stub_927, gl_dispatch_stub_927, NULL, 927),
    NAME_FUNC_OFFSET(16344, gl_dispatch_stub_928, gl_dispatch_stub_928, NULL, 928),
    NAME_FUNC_OFFSET(16360, gl_dispatch_stub_929, gl_dispatch_stub_929, NULL, 929),
    NAME_FUNC_OFFSET(16374, gl_dispatch_stub_930, gl_dispatch_stub_930, NULL, 930),
    NAME_FUNC_OFFSET(16393, gl_dispatch_stub_931, gl_dispatch_stub_931, NULL, 931),
    NAME_FUNC_OFFSET(16405, gl_dispatch_stub_932, gl_dispatch_stub_932, NULL, 932),
    NAME_FUNC_OFFSET(16422, gl_dispatch_stub_933, gl_dispatch_stub_933, NULL, 933),
    NAME_FUNC_OFFSET(16449, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 934),
    NAME_FUNC_OFFSET(16470, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 935),
    NAME_FUNC_OFFSET(16492, glCheckFramebufferStatusEXT, glCheckFramebufferStatusEXT, NULL, 936),
    NAME_FUNC_OFFSET(16520, glDeleteFramebuffersEXT, glDeleteFramebuffersEXT, NULL, 937),
    NAME_FUNC_OFFSET(16544, glDeleteRenderbuffersEXT, glDeleteRenderbuffersEXT, NULL, 938),
    NAME_FUNC_OFFSET(16569, glFramebufferRenderbufferEXT, glFramebufferRenderbufferEXT, NULL, 939),
    NAME_FUNC_OFFSET(16598, glFramebufferTexture1DEXT, glFramebufferTexture1DEXT, NULL, 940),
    NAME_FUNC_OFFSET(16624, glFramebufferTexture2DEXT, glFramebufferTexture2DEXT, NULL, 941),
    NAME_FUNC_OFFSET(16650, glFramebufferTexture3DEXT, glFramebufferTexture3DEXT, NULL, 942),
    NAME_FUNC_OFFSET(16676, glGenFramebuffersEXT, glGenFramebuffersEXT, NULL, 943),
    NAME_FUNC_OFFSET(16697, glGenRenderbuffersEXT, glGenRenderbuffersEXT, NULL, 944),
    NAME_FUNC_OFFSET(16719, glGenerateMipmapEXT, glGenerateMipmapEXT, NULL, 945),
    NAME_FUNC_OFFSET(16739, glGetFramebufferAttachmentParameterivEXT, glGetFramebufferAttachmentParameterivEXT, NULL, 946),
    NAME_FUNC_OFFSET(16780, glGetRenderbufferParameterivEXT, glGetRenderbufferParameterivEXT, NULL, 947),
    NAME_FUNC_OFFSET(16812, glIsFramebufferEXT, glIsFramebufferEXT, NULL, 948),
    NAME_FUNC_OFFSET(16831, glIsRenderbufferEXT, glIsRenderbufferEXT, NULL, 949),
    NAME_FUNC_OFFSET(16851, glRenderbufferStorageEXT, glRenderbufferStorageEXT, NULL, 950),
    NAME_FUNC_OFFSET(16876, gl_dispatch_stub_951, gl_dispatch_stub_951, NULL, 951),
    NAME_FUNC_OFFSET(16897, gl_dispatch_stub_952, gl_dispatch_stub_952, NULL, 952),
    NAME_FUNC_OFFSET(16921, gl_dispatch_stub_953, gl_dispatch_stub_953, NULL, 953),
    NAME_FUNC_OFFSET(16951, glBindFragDataLocationEXT, glBindFragDataLocationEXT, NULL, 954),
    NAME_FUNC_OFFSET(16977, glGetFragDataLocationEXT, glGetFragDataLocationEXT, NULL, 955),
    NAME_FUNC_OFFSET(17002, glGetUniformuivEXT, glGetUniformuivEXT, NULL, 956),
    NAME_FUNC_OFFSET(17021, glGetVertexAttribIivEXT, glGetVertexAttribIivEXT, NULL, 957),
    NAME_FUNC_OFFSET(17045, glGetVertexAttribIuivEXT, glGetVertexAttribIuivEXT, NULL, 958),
    NAME_FUNC_OFFSET(17070, glUniform1uiEXT, glUniform1uiEXT, NULL, 959),
    NAME_FUNC_OFFSET(17086, glUniform1uivEXT, glUniform1uivEXT, NULL, 960),
    NAME_FUNC_OFFSET(17103, glUniform2uiEXT, glUniform2uiEXT, NULL, 961),
    NAME_FUNC_OFFSET(17119, glUniform2uivEXT, glUniform2uivEXT, NULL, 962),
    NAME_FUNC_OFFSET(17136, glUniform3uiEXT, glUniform3uiEXT, NULL, 963),
    NAME_FUNC_OFFSET(17152, glUniform3uivEXT, glUniform3uivEXT, NULL, 964),
    NAME_FUNC_OFFSET(17169, glUniform4uiEXT, glUniform4uiEXT, NULL, 965),
    NAME_FUNC_OFFSET(17185, glUniform4uivEXT, glUniform4uivEXT, NULL, 966),
    NAME_FUNC_OFFSET(17202, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 967),
    NAME_FUNC_OFFSET(17223, glVertexAttribI1ivEXT, glVertexAttribI1ivEXT, NULL, 968),
    NAME_FUNC_OFFSET(17245, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 969),
    NAME_FUNC_OFFSET(17267, glVertexAttribI1uivEXT, glVertexAttribI1uivEXT, NULL, 970),
    NAME_FUNC_OFFSET(17290, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 971),
    NAME_FUNC_OFFSET(17311, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 972),
    NAME_FUNC_OFFSET(17333, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 973),
    NAME_FUNC_OFFSET(17355, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 974),
    NAME_FUNC_OFFSET(17378, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 975),
    NAME_FUNC_OFFSET(17399, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 976),
    NAME_FUNC_OFFSET(17421, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 977),
    NAME_FUNC_OFFSET(17443, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 978),
    NAME_FUNC_OFFSET(17466, glVertexAttribI4bvEXT, glVertexAttribI4bvEXT, NULL, 979),
    NAME_FUNC_OFFSET(17488, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 980),
    NAME_FUNC_OFFSET(17509, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 981),
    NAME_FUNC_OFFSET(17531, glVertexAttribI4svEXT, glVertexAttribI4svEXT, NULL, 982),
    NAME_FUNC_OFFSET(17553, glVertexAttribI4ubvEXT, glVertexAttribI4ubvEXT, NULL, 983),
    NAME_FUNC_OFFSET(17576, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 984),
    NAME_FUNC_OFFSET(17598, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 985),
    NAME_FUNC_OFFSET(17621, glVertexAttribI4usvEXT, glVertexAttribI4usvEXT, NULL, 986),
    NAME_FUNC_OFFSET(17644, glVertexAttribIPointerEXT, glVertexAttribIPointerEXT, NULL, 987),
    NAME_FUNC_OFFSET(17670, glFramebufferTextureLayerEXT, glFramebufferTextureLayerEXT, NULL, 988),
    NAME_FUNC_OFFSET(17699, glColorMaskIndexedEXT, glColorMaskIndexedEXT, NULL, 989),
    NAME_FUNC_OFFSET(17721, glDisableIndexedEXT, glDisableIndexedEXT, NULL, 990),
    NAME_FUNC_OFFSET(17741, glEnableIndexedEXT, glEnableIndexedEXT, NULL, 991),
    NAME_FUNC_OFFSET(17760, glGetBooleanIndexedvEXT, glGetBooleanIndexedvEXT, NULL, 992),
    NAME_FUNC_OFFSET(17784, glGetIntegerIndexedvEXT, glGetIntegerIndexedvEXT, NULL, 993),
    NAME_FUNC_OFFSET(17808, glIsEnabledIndexedEXT, glIsEnabledIndexedEXT, NULL, 994),
    NAME_FUNC_OFFSET(17830, glClearColorIiEXT, glClearColorIiEXT, NULL, 995),
    NAME_FUNC_OFFSET(17848, glClearColorIuiEXT, glClearColorIuiEXT, NULL, 996),
    NAME_FUNC_OFFSET(17867, glGetTexParameterIivEXT, glGetTexParameterIivEXT, NULL, 997),
    NAME_FUNC_OFFSET(17891, glGetTexParameterIuivEXT, glGetTexParameterIuivEXT, NULL, 998),
    NAME_FUNC_OFFSET(17916, glTexParameterIivEXT, glTexParameterIivEXT, NULL, 999),
    NAME_FUNC_OFFSET(17937, glTexParameterIuivEXT, glTexParameterIuivEXT, NULL, 1000),
    NAME_FUNC_OFFSET(17959, glBeginConditionalRenderNV, glBeginConditionalRenderNV, NULL, 1001),
    NAME_FUNC_OFFSET(17986, glEndConditionalRenderNV, glEndConditionalRenderNV, NULL, 1002),
    NAME_FUNC_OFFSET(18011, glBeginTransformFeedbackEXT, glBeginTransformFeedbackEXT, NULL, 1003),
    NAME_FUNC_OFFSET(18039, glBindBufferBaseEXT, glBindBufferBaseEXT, NULL, 1004),
    NAME_FUNC_OFFSET(18059, glBindBufferOffsetEXT, glBindBufferOffsetEXT, NULL, 1005),
    NAME_FUNC_OFFSET(18081, glBindBufferRangeEXT, glBindBufferRangeEXT, NULL, 1006),
    NAME_FUNC_OFFSET(18102, glEndTransformFeedbackEXT, glEndTransformFeedbackEXT, NULL, 1007),
    NAME_FUNC_OFFSET(18128, glGetTransformFeedbackVaryingEXT, glGetTransformFeedbackVaryingEXT, NULL, 1008),
    NAME_FUNC_OFFSET(18161, glTransformFeedbackVaryingsEXT, glTransformFeedbackVaryingsEXT, NULL, 1009),
    NAME_FUNC_OFFSET(18192, glProvokingVertexEXT, glProvokingVertexEXT, NULL, 1010),
    NAME_FUNC_OFFSET(18213, glGetObjectParameterivAPPLE, glGetObjectParameterivAPPLE, NULL, 1011),
    NAME_FUNC_OFFSET(18241, glObjectPurgeableAPPLE, glObjectPurgeableAPPLE, NULL, 1012),
    NAME_FUNC_OFFSET(18264, glObjectUnpurgeableAPPLE, glObjectUnpurgeableAPPLE, NULL, 1013),
    NAME_FUNC_OFFSET(18289, glActiveProgramEXT, glActiveProgramEXT, NULL, 1014),
    NAME_FUNC_OFFSET(18308, glCreateShaderProgramEXT, glCreateShaderProgramEXT, NULL, 1015),
    NAME_FUNC_OFFSET(18333, glUseShaderProgramEXT, glUseShaderProgramEXT, NULL, 1016),
    NAME_FUNC_OFFSET(18355, glTextureBarrierNV, glTextureBarrierNV, NULL, 1017),
    NAME_FUNC_OFFSET(18374, gl_dispatch_stub_1018, gl_dispatch_stub_1018, NULL, 1018),
    NAME_FUNC_OFFSET(18399, gl_dispatch_stub_1019, gl_dispatch_stub_1019, NULL, 1019),
    NAME_FUNC_OFFSET(18428, gl_dispatch_stub_1020, gl_dispatch_stub_1020, NULL, 1020),
    NAME_FUNC_OFFSET(18459, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(18483, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(18508, glEGLImageTargetRenderbufferStorageOES, glEGLImageTargetRenderbufferStorageOES, NULL, 1023),
    NAME_FUNC_OFFSET(18547, glEGLImageTargetTexture2DOES, glEGLImageTargetTexture2DOES, NULL, 1024),
    NAME_FUNC_OFFSET(18576, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET(18589, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET(18603, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET(18616, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET(18630, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET(18645, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET(18662, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET(18679, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET(18697, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET(18714, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET(18730, glAreTexturesResident, glAreTexturesResidentEXT, glAreTexturesResidentEXT, 322),
    NAME_FUNC_OFFSET(18755, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET(18775, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET(18795, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET(18818, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET(18841, glDeleteTextures, glDeleteTexturesEXT, glDeleteTexturesEXT, 327),
    NAME_FUNC_OFFSET(18861, glGenTextures, glGenTexturesEXT, glGenTexturesEXT, 328),
    NAME_FUNC_OFFSET(18878, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(18895, glIsTexture, glIsTextureEXT, glIsTextureEXT, 330),
    NAME_FUNC_OFFSET(18910, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET(18934, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET(18953, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET(18972, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET(18988, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(19007, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(19026, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET(19049, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(19065, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(19081, glColorTableParameterfv, glColorTableParameterfv, NULL, 340),
    NAME_FUNC_OFFSET(19108, glColorTableParameteriv, glColorTableParameteriv, NULL, 341),
    NAME_FUNC_OFFSET(19135, glCopyColorTable, glCopyColorTable, NULL, 342),
    NAME_FUNC_OFFSET(19155, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(19174, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(19193, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(19223, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(19253, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(19283, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(19313, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET(19332, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET(19355, glConvolutionFilter1D, glConvolutionFilter1D, NULL, 348),
    NAME_FUNC_OFFSET(19380, glConvolutionFilter2D, glConvolutionFilter2D, NULL, 349),
    NAME_FUNC_OFFSET(19405, glConvolutionParameterf, glConvolutionParameterf, NULL, 350),
    NAME_FUNC_OFFSET(19432, glConvolutionParameterfv, glConvolutionParameterfv, NULL, 351),
    NAME_FUNC_OFFSET(19460, glConvolutionParameteri, glConvolutionParameteri, NULL, 352),
    NAME_FUNC_OFFSET(19487, glConvolutionParameteriv, glConvolutionParameteriv, NULL, 353),
    NAME_FUNC_OFFSET(19515, glCopyConvolutionFilter1D, glCopyConvolutionFilter1D, NULL, 354),
    NAME_FUNC_OFFSET(19544, glCopyConvolutionFilter2D, glCopyConvolutionFilter2D, NULL, 355),
    NAME_FUNC_OFFSET(19573, glGetConvolutionFilter, gl_dispatch_stub_356, gl_dispatch_stub_356, 356),
    NAME_FUNC_OFFSET(19599, glGetConvolutionParameterfv, gl_dispatch_stub_357, gl_dispatch_stub_357, 357),
    NAME_FUNC_OFFSET(19630, glGetConvolutionParameteriv, gl_dispatch_stub_358, gl_dispatch_stub_358, 358),
    NAME_FUNC_OFFSET(19661, glGetSeparableFilter, gl_dispatch_stub_359, gl_dispatch_stub_359, 359),
    NAME_FUNC_OFFSET(19685, glSeparableFilter2D, glSeparableFilter2D, NULL, 360),
    NAME_FUNC_OFFSET(19708, glGetHistogram, gl_dispatch_stub_361, gl_dispatch_stub_361, 361),
    NAME_FUNC_OFFSET(19726, glGetHistogramParameterfv, gl_dispatch_stub_362, gl_dispatch_stub_362, 362),
    NAME_FUNC_OFFSET(19755, glGetHistogramParameteriv, gl_dispatch_stub_363, gl_dispatch_stub_363, 363),
    NAME_FUNC_OFFSET(19784, glGetMinmax, gl_dispatch_stub_364, gl_dispatch_stub_364, 364),
    NAME_FUNC_OFFSET(19799, glGetMinmaxParameterfv, gl_dispatch_stub_365, gl_dispatch_stub_365, 365),
    NAME_FUNC_OFFSET(19825, glGetMinmaxParameteriv, gl_dispatch_stub_366, gl_dispatch_stub_366, 366),
    NAME_FUNC_OFFSET(19851, glHistogram, glHistogram, NULL, 367),
    NAME_FUNC_OFFSET(19866, glMinmax, glMinmax, NULL, 368),
    NAME_FUNC_OFFSET(19878, glResetHistogram, glResetHistogram, NULL, 369),
    NAME_FUNC_OFFSET(19898, glResetMinmax, glResetMinmax, NULL, 370),
    NAME_FUNC_OFFSET(19915, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(19931, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(19947, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(19966, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(19985, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(20008, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(20031, glActiveTextureARB, glActiveTextureARB, NULL, 374),
    NAME_FUNC_OFFSET(20047, glClientActiveTextureARB, glClientActiveTextureARB, NULL, 375),
    NAME_FUNC_OFFSET(20069, glMultiTexCoord1dARB, glMultiTexCoord1dARB, NULL, 376),
    NAME_FUNC_OFFSET(20087, glMultiTexCoord1dvARB, glMultiTexCoord1dvARB, NULL, 377),
    NAME_FUNC_OFFSET(20106, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET(20124, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET(20143, glMultiTexCoord1iARB, glMultiTexCoord1iARB, NULL, 380),
    NAME_FUNC_OFFSET(20161, glMultiTexCoord1ivARB, glMultiTexCoord1ivARB, NULL, 381),
    NAME_FUNC_OFFSET(20180, glMultiTexCoord1sARB, glMultiTexCoord1sARB, NULL, 382),
    NAME_FUNC_OFFSET(20198, glMultiTexCoord1svARB, glMultiTexCoord1svARB, NULL, 383),
    NAME_FUNC_OFFSET(20217, glMultiTexCoord2dARB, glMultiTexCoord2dARB, NULL, 384),
    NAME_FUNC_OFFSET(20235, glMultiTexCoord2dvARB, glMultiTexCoord2dvARB, NULL, 385),
    NAME_FUNC_OFFSET(20254, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET(20272, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET(20291, glMultiTexCoord2iARB, glMultiTexCoord2iARB, NULL, 388),
    NAME_FUNC_OFFSET(20309, glMultiTexCoord2ivARB, glMultiTexCoord2ivARB, NULL, 389),
    NAME_FUNC_OFFSET(20328, glMultiTexCoord2sARB, glMultiTexCoord2sARB, NULL, 390),
    NAME_FUNC_OFFSET(20346, glMultiTexCoord2svARB, glMultiTexCoord2svARB, NULL, 391),
    NAME_FUNC_OFFSET(20365, glMultiTexCoord3dARB, glMultiTexCoord3dARB, NULL, 392),
    NAME_FUNC_OFFSET(20383, glMultiTexCoord3dvARB, glMultiTexCoord3dvARB, NULL, 393),
    NAME_FUNC_OFFSET(20402, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET(20420, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET(20439, glMultiTexCoord3iARB, glMultiTexCoord3iARB, NULL, 396),
    NAME_FUNC_OFFSET(20457, glMultiTexCoord3ivARB, glMultiTexCoord3ivARB, NULL, 397),
    NAME_FUNC_OFFSET(20476, glMultiTexCoord3sARB, glMultiTexCoord3sARB, NULL, 398),
    NAME_FUNC_OFFSET(20494, glMultiTexCoord3svARB, glMultiTexCoord3svARB, NULL, 399),
    NAME_FUNC_OFFSET(20513, glMultiTexCoord4dARB, glMultiTexCoord4dARB, NULL, 400),
    NAME_FUNC_OFFSET(20531, glMultiTexCoord4dvARB, glMultiTexCoord4dvARB, NULL, 401),
    NAME_FUNC_OFFSET(20550, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET(20568, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET(20587, glMultiTexCoord4iARB, glMultiTexCoord4iARB, NULL, 404),
    NAME_FUNC_OFFSET(20605, glMultiTexCoord4ivARB, glMultiTexCoord4ivARB, NULL, 405),
    NAME_FUNC_OFFSET(20624, glMultiTexCoord4sARB, glMultiTexCoord4sARB, NULL, 406),
    NAME_FUNC_OFFSET(20642, glMultiTexCoord4svARB, glMultiTexCoord4svARB, NULL, 407),
    NAME_FUNC_OFFSET(20661, glStencilOpSeparate, glStencilOpSeparate, NULL, 423),
    NAME_FUNC_OFFSET(20684, glFramebufferTexture, glFramebufferTexture, NULL, 435),
    NAME_FUNC_OFFSET(20708, glLoadTransposeMatrixdARB, glLoadTransposeMatrixdARB, NULL, 438),
    NAME_FUNC_OFFSET(20731, glLoadTransposeMatrixfARB, glLoadTransposeMatrixfARB, NULL, 439),
    NAME_FUNC_OFFSET(20754, glMultTransposeMatrixdARB, glMultTransposeMatrixdARB, NULL, 440),
    NAME_FUNC_OFFSET(20777, glMultTransposeMatrixfARB, glMultTransposeMatrixfARB, NULL, 441),
    NAME_FUNC_OFFSET(20800, glSampleCoverageARB, glSampleCoverageARB, NULL, 442),
    NAME_FUNC_OFFSET(20817, glCompressedTexImage1DARB, glCompressedTexImage1DARB, NULL, 443),
    NAME_FUNC_OFFSET(20840, glCompressedTexImage2DARB, glCompressedTexImage2DARB, NULL, 444),
    NAME_FUNC_OFFSET(20863, glCompressedTexImage3DARB, glCompressedTexImage3DARB, NULL, 445),
    NAME_FUNC_OFFSET(20886, glCompressedTexImage3DARB, glCompressedTexImage3DARB, NULL, 445),
    NAME_FUNC_OFFSET(20912, glCompressedTexSubImage1DARB, glCompressedTexSubImage1DARB, NULL, 446),
    NAME_FUNC_OFFSET(20938, glCompressedTexSubImage2DARB, glCompressedTexSubImage2DARB, NULL, 447),
    NAME_FUNC_OFFSET(20964, glCompressedTexSubImage3DARB, glCompressedTexSubImage3DARB, NULL, 448),
    NAME_FUNC_OFFSET(20990, glCompressedTexSubImage3DARB, glCompressedTexSubImage3DARB, NULL, 448),
    NAME_FUNC_OFFSET(21019, glGetCompressedTexImageARB, glGetCompressedTexImageARB, NULL, 449),
    NAME_FUNC_OFFSET(21043, glDisableVertexAttribArrayARB, glDisableVertexAttribArrayARB, NULL, 450),
    NAME_FUNC_OFFSET(21070, glEnableVertexAttribArrayARB, glEnableVertexAttribArrayARB, NULL, 451),
    NAME_FUNC_OFFSET(21096, glGetVertexAttribdvARB, glGetVertexAttribdvARB, NULL, 458),
    NAME_FUNC_OFFSET(21116, glGetVertexAttribfvARB, glGetVertexAttribfvARB, NULL, 459),
    NAME_FUNC_OFFSET(21136, glGetVertexAttribivARB, glGetVertexAttribivARB, NULL, 460),
    NAME_FUNC_OFFSET(21156, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 461),
    NAME_FUNC_OFFSET(21179, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 462),
    NAME_FUNC_OFFSET(21203, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 463),
    NAME_FUNC_OFFSET(21226, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 464),
    NAME_FUNC_OFFSET(21250, glVertexAttrib1dARB, glVertexAttrib1dARB, NULL, 470),
    NAME_FUNC_OFFSET(21267, glVertexAttrib1dvARB, glVertexAttrib1dvARB, NULL, 471),
    NAME_FUNC_OFFSET(21285, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 472),
    NAME_FUNC_OFFSET(21302, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 473),
    NAME_FUNC_OFFSET(21320, glVertexAttrib1sARB, glVertexAttrib1sARB, NULL, 474),
    NAME_FUNC_OFFSET(21337, glVertexAttrib1svARB, glVertexAttrib1svARB, NULL, 475),
    NAME_FUNC_OFFSET(21355, glVertexAttrib2dARB, glVertexAttrib2dARB, NULL, 476),
    NAME_FUNC_OFFSET(21372, glVertexAttrib2dvARB, glVertexAttrib2dvARB, NULL, 477),
    NAME_FUNC_OFFSET(21390, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 478),
    NAME_FUNC_OFFSET(21407, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 479),
    NAME_FUNC_OFFSET(21425, glVertexAttrib2sARB, glVertexAttrib2sARB, NULL, 480),
    NAME_FUNC_OFFSET(21442, glVertexAttrib2svARB, glVertexAttrib2svARB, NULL, 481),
    NAME_FUNC_OFFSET(21460, glVertexAttrib3dARB, glVertexAttrib3dARB, NULL, 482),
    NAME_FUNC_OFFSET(21477, glVertexAttrib3dvARB, glVertexAttrib3dvARB, NULL, 483),
    NAME_FUNC_OFFSET(21495, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 484),
    NAME_FUNC_OFFSET(21512, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 485),
    NAME_FUNC_OFFSET(21530, glVertexAttrib3sARB, glVertexAttrib3sARB, NULL, 486),
    NAME_FUNC_OFFSET(21547, glVertexAttrib3svARB, glVertexAttrib3svARB, NULL, 487),
    NAME_FUNC_OFFSET(21565, glVertexAttrib4NbvARB, glVertexAttrib4NbvARB, NULL, 488),
    NAME_FUNC_OFFSET(21584, glVertexAttrib4NivARB, glVertexAttrib4NivARB, NULL, 489),
    NAME_FUNC_OFFSET(21603, glVertexAttrib4NsvARB, glVertexAttrib4NsvARB, NULL, 490),
    NAME_FUNC_OFFSET(21622, glVertexAttrib4NubARB, glVertexAttrib4NubARB, NULL, 491),
    NAME_FUNC_OFFSET(21641, glVertexAttrib4NubvARB, glVertexAttrib4NubvARB, NULL, 492),
    NAME_FUNC_OFFSET(21661, glVertexAttrib4NuivARB, glVertexAttrib4NuivARB, NULL, 493),
    NAME_FUNC_OFFSET(21681, glVertexAttrib4NusvARB, glVertexAttrib4NusvARB, NULL, 494),
    NAME_FUNC_OFFSET(21701, glVertexAttrib4bvARB, glVertexAttrib4bvARB, NULL, 495),
    NAME_FUNC_OFFSET(21719, glVertexAttrib4dARB, glVertexAttrib4dARB, NULL, 496),
    NAME_FUNC_OFFSET(21736, glVertexAttrib4dvARB, glVertexAttrib4dvARB, NULL, 497),
    NAME_FUNC_OFFSET(21754, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 498),
    NAME_FUNC_OFFSET(21771, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 499),
    NAME_FUNC_OFFSET(21789, glVertexAttrib4ivARB, glVertexAttrib4ivARB, NULL, 500),
    NAME_FUNC_OFFSET(21807, glVertexAttrib4sARB, glVertexAttrib4sARB, NULL, 501),
    NAME_FUNC_OFFSET(21824, glVertexAttrib4svARB, glVertexAttrib4svARB, NULL, 502),
    NAME_FUNC_OFFSET(21842, glVertexAttrib4ubvARB, glVertexAttrib4ubvARB, NULL, 503),
    NAME_FUNC_OFFSET(21861, glVertexAttrib4uivARB, glVertexAttrib4uivARB, NULL, 504),
    NAME_FUNC_OFFSET(21880, glVertexAttrib4usvARB, glVertexAttrib4usvARB, NULL, 505),
    NAME_FUNC_OFFSET(21899, glVertexAttribPointerARB, glVertexAttribPointerARB, NULL, 506),
    NAME_FUNC_OFFSET(21921, glBindBufferARB, glBindBufferARB, NULL, 507),
    NAME_FUNC_OFFSET(21934, glBufferDataARB, glBufferDataARB, NULL, 508),
    NAME_FUNC_OFFSET(21947, glBufferSubDataARB, glBufferSubDataARB, NULL, 509),
    NAME_FUNC_OFFSET(21963, glDeleteBuffersARB, glDeleteBuffersARB, NULL, 510),
    NAME_FUNC_OFFSET(21979, glGenBuffersARB, glGenBuffersARB, NULL, 511),
    NAME_FUNC_OFFSET(21992, glGetBufferParameterivARB, glGetBufferParameterivARB, NULL, 512),
    NAME_FUNC_OFFSET(22015, glGetBufferPointervARB, glGetBufferPointervARB, NULL, 513),
    NAME_FUNC_OFFSET(22035, glGetBufferPointervARB, glGetBufferPointervARB, NULL, 513),
    NAME_FUNC_OFFSET(22058, glGetBufferSubDataARB, glGetBufferSubDataARB, NULL, 514),
    NAME_FUNC_OFFSET(22077, glIsBufferARB, glIsBufferARB, NULL, 515),
    NAME_FUNC_OFFSET(22088, glMapBufferARB, glMapBufferARB, NULL, 516),
    NAME_FUNC_OFFSET(22100, glMapBufferARB, glMapBufferARB, NULL, 516),
    NAME_FUNC_OFFSET(22115, glUnmapBufferARB, glUnmapBufferARB, NULL, 517),
    NAME_FUNC_OFFSET(22129, glUnmapBufferARB, glUnmapBufferARB, NULL, 517),
    NAME_FUNC_OFFSET(22146, glBeginQueryARB, glBeginQueryARB, NULL, 518),
    NAME_FUNC_OFFSET(22159, glDeleteQueriesARB, glDeleteQueriesARB, NULL, 519),
    NAME_FUNC_OFFSET(22175, glEndQueryARB, glEndQueryARB, NULL, 520),
    NAME_FUNC_OFFSET(22186, glGenQueriesARB, glGenQueriesARB, NULL, 521),
    NAME_FUNC_OFFSET(22199, glGetQueryObjectivARB, glGetQueryObjectivARB, NULL, 522),
    NAME_FUNC_OFFSET(22218, glGetQueryObjectuivARB, glGetQueryObjectuivARB, NULL, 523),
    NAME_FUNC_OFFSET(22238, glGetQueryivARB, glGetQueryivARB, NULL, 524),
    NAME_FUNC_OFFSET(22251, glIsQueryARB, glIsQueryARB, NULL, 525),
    NAME_FUNC_OFFSET(22261, glCompileShaderARB, glCompileShaderARB, NULL, 527),
    NAME_FUNC_OFFSET(22277, glGetActiveUniformARB, glGetActiveUniformARB, NULL, 532),
    NAME_FUNC_OFFSET(22296, glGetShaderSourceARB, glGetShaderSourceARB, NULL, 538),
    NAME_FUNC_OFFSET(22314, glGetUniformLocationARB, glGetUniformLocationARB, NULL, 539),
    NAME_FUNC_OFFSET(22335, glGetUniformfvARB, glGetUniformfvARB, NULL, 540),
    NAME_FUNC_OFFSET(22350, glGetUniformivARB, glGetUniformivARB, NULL, 541),
    NAME_FUNC_OFFSET(22365, glLinkProgramARB, glLinkProgramARB, NULL, 542),
    NAME_FUNC_OFFSET(22379, glShaderSourceARB, glShaderSourceARB, NULL, 543),
    NAME_FUNC_OFFSET(22394, glUniform1fARB, glUniform1fARB, NULL, 544),
    NAME_FUNC_OFFSET(22406, glUniform1fvARB, glUniform1fvARB, NULL, 545),
    NAME_FUNC_OFFSET(22419, glUniform1iARB, glUniform1iARB, NULL, 546),
    NAME_FUNC_OFFSET(22431, glUniform1ivARB, glUniform1ivARB, NULL, 547),
    NAME_FUNC_OFFSET(22444, glUniform2fARB, glUniform2fARB, NULL, 548),
    NAME_FUNC_OFFSET(22456, glUniform2fvARB, glUniform2fvARB, NULL, 549),
    NAME_FUNC_OFFSET(22469, glUniform2iARB, glUniform2iARB, NULL, 550),
    NAME_FUNC_OFFSET(22481, glUniform2ivARB, glUniform2ivARB, NULL, 551),
    NAME_FUNC_OFFSET(22494, glUniform3fARB, glUniform3fARB, NULL, 552),
    NAME_FUNC_OFFSET(22506, glUniform3fvARB, glUniform3fvARB, NULL, 553),
    NAME_FUNC_OFFSET(22519, glUniform3iARB, glUniform3iARB, NULL, 554),
    NAME_FUNC_OFFSET(22531, glUniform3ivARB, glUniform3ivARB, NULL, 555),
    NAME_FUNC_OFFSET(22544, glUniform4fARB, glUniform4fARB, NULL, 556),
    NAME_FUNC_OFFSET(22556, glUniform4fvARB, glUniform4fvARB, NULL, 557),
    NAME_FUNC_OFFSET(22569, glUniform4iARB, glUniform4iARB, NULL, 558),
    NAME_FUNC_OFFSET(22581, glUniform4ivARB, glUniform4ivARB, NULL, 559),
    NAME_FUNC_OFFSET(22594, glUniformMatrix2fvARB, glUniformMatrix2fvARB, NULL, 560),
    NAME_FUNC_OFFSET(22613, glUniformMatrix3fvARB, glUniformMatrix3fvARB, NULL, 561),
    NAME_FUNC_OFFSET(22632, glUniformMatrix4fvARB, glUniformMatrix4fvARB, NULL, 562),
    NAME_FUNC_OFFSET(22651, glUseProgramObjectARB, glUseProgramObjectARB, NULL, 563),
    NAME_FUNC_OFFSET(22664, glValidateProgramARB, glValidateProgramARB, NULL, 564),
    NAME_FUNC_OFFSET(22682, glBindAttribLocationARB, glBindAttribLocationARB, NULL, 565),
    NAME_FUNC_OFFSET(22703, glGetActiveAttribARB, glGetActiveAttribARB, NULL, 566),
    NAME_FUNC_OFFSET(22721, glGetAttribLocationARB, glGetAttribLocationARB, NULL, 567),
    NAME_FUNC_OFFSET(22741, glDrawBuffersARB, glDrawBuffersARB, NULL, 568),
    NAME_FUNC_OFFSET(22755, glDrawBuffersARB, glDrawBuffersARB, NULL, 568),
    NAME_FUNC_OFFSET(22772, glDrawBuffersARB, glDrawBuffersARB, NULL, 568),
    NAME_FUNC_OFFSET(22788, glClampColorARB, glClampColorARB, NULL, 569),
    NAME_FUNC_OFFSET(22801, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 570),
    NAME_FUNC_OFFSET(22826, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 570),
    NAME_FUNC_OFFSET(22848, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 571),
    NAME_FUNC_OFFSET(22875, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 571),
    NAME_FUNC_OFFSET(22899, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 572),
    NAME_FUNC_OFFSET(22935, glVertexAttribDivisorARB, glVertexAttribDivisorARB, NULL, 574),
    NAME_FUNC_OFFSET(22957, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 575),
    NAME_FUNC_OFFSET(22985, glMapBufferRange, glMapBufferRange, NULL, 576),
    NAME_FUNC_OFFSET(23005, glTexBufferARB, glTexBufferARB, NULL, 577),
    NAME_FUNC_OFFSET(23017, glBindVertexArray, glBindVertexArray, NULL, 578),
    NAME_FUNC_OFFSET(23038, glGenVertexArrays, glGenVertexArrays, NULL, 579),
    NAME_FUNC_OFFSET(23059, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 599),
    NAME_FUNC_OFFSET(23093, glBlendEquationiARB, glBlendEquationiARB, NULL, 600),
    NAME_FUNC_OFFSET(23119, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 601),
    NAME_FUNC_OFFSET(23149, glBlendFunciARB, glBlendFunciARB, NULL, 602),
    NAME_FUNC_OFFSET(23171, glClearDepthf, glClearDepthf, NULL, 669),
    NAME_FUNC_OFFSET(23188, glDepthRangef, glDepthRangef, NULL, 670),
    NAME_FUNC_OFFSET(23205, gl_dispatch_stub_674, gl_dispatch_stub_674, NULL, 674),
    NAME_FUNC_OFFSET(23227, gl_dispatch_stub_675, gl_dispatch_stub_675, NULL, 675),
    NAME_FUNC_OFFSET(23246, gl_dispatch_stub_676, gl_dispatch_stub_676, NULL, 676),
    NAME_FUNC_OFFSET(23269, gl_dispatch_stub_729, gl_dispatch_stub_729, NULL, 729),
    NAME_FUNC_OFFSET(23285, gl_dispatch_stub_730, gl_dispatch_stub_730, NULL, 730),
    NAME_FUNC_OFFSET(23304, glPointParameterfEXT, glPointParameterfEXT, NULL, 737),
    NAME_FUNC_OFFSET(23322, glPointParameterfEXT, glPointParameterfEXT, NULL, 737),
    NAME_FUNC_OFFSET(23343, glPointParameterfEXT, glPointParameterfEXT, NULL, 737),
    NAME_FUNC_OFFSET(23365, glPointParameterfvEXT, glPointParameterfvEXT, NULL, 738),
    NAME_FUNC_OFFSET(23384, glPointParameterfvEXT, glPointParameterfvEXT, NULL, 738),
    NAME_FUNC_OFFSET(23406, glPointParameterfvEXT, glPointParameterfvEXT, NULL, 738),
    NAME_FUNC_OFFSET(23429, glSecondaryColor3bEXT, glSecondaryColor3bEXT, NULL, 741),
    NAME_FUNC_OFFSET(23448, glSecondaryColor3bvEXT, glSecondaryColor3bvEXT, NULL, 742),
    NAME_FUNC_OFFSET(23468, glSecondaryColor3dEXT, glSecondaryColor3dEXT, NULL, 743),
    NAME_FUNC_OFFSET(23487, glSecondaryColor3dvEXT, glSecondaryColor3dvEXT, NULL, 744),
    NAME_FUNC_OFFSET(23507, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 745),
    NAME_FUNC_OFFSET(23526, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 746),
    NAME_FUNC_OFFSET(23546, glSecondaryColor3iEXT, glSecondaryColor3iEXT, NULL, 747),
    NAME_FUNC_OFFSET(23565, glSecondaryColor3ivEXT, glSecondaryColor3ivEXT, NULL, 748),
    NAME_FUNC_OFFSET(23585, glSecondaryColor3sEXT, glSecondaryColor3sEXT, NULL, 749),
    NAME_FUNC_OFFSET(23604, glSecondaryColor3svEXT, glSecondaryColor3svEXT, NULL, 750),
    NAME_FUNC_OFFSET(23624, glSecondaryColor3ubEXT, glSecondaryColor3ubEXT, NULL, 751),
    NAME_FUNC_OFFSET(23644, glSecondaryColor3ubvEXT, glSecondaryColor3ubvEXT, NULL, 752),
    NAME_FUNC_OFFSET(23665, glSecondaryColor3uiEXT, glSecondaryColor3uiEXT, NULL, 753),
    NAME_FUNC_OFFSET(23685, glSecondaryColor3uivEXT, glSecondaryColor3uivEXT, NULL, 754),
    NAME_FUNC_OFFSET(23706, glSecondaryColor3usEXT, glSecondaryColor3usEXT, NULL, 755),
    NAME_FUNC_OFFSET(23726, glSecondaryColor3usvEXT, glSecondaryColor3usvEXT, NULL, 756),
    NAME_FUNC_OFFSET(23747, glSecondaryColorPointerEXT, glSecondaryColorPointerEXT, NULL, 757),
    NAME_FUNC_OFFSET(23771, glMultiDrawArraysEXT, glMultiDrawArraysEXT, NULL, 758),
    NAME_FUNC_OFFSET(23789, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 759),
    NAME_FUNC_OFFSET(23809, glFogCoordPointerEXT, glFogCoordPointerEXT, NULL, 760),
    NAME_FUNC_OFFSET(23827, glFogCoorddEXT, glFogCoorddEXT, NULL, 761),
    NAME_FUNC_OFFSET(23839, glFogCoorddvEXT, glFogCoorddvEXT, NULL, 762),
    NAME_FUNC_OFFSET(23852, glFogCoordfEXT, glFogCoordfEXT, NULL, 763),
    NAME_FUNC_OFFSET(23864, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 764),
    NAME_FUNC_OFFSET(23877, glBlendFuncSeparateEXT, glBlendFuncSeparateEXT, NULL, 765),
    NAME_FUNC_OFFSET(23897, glBlendFuncSeparateEXT, glBlendFuncSeparateEXT, NULL, 765),
    NAME_FUNC_OFFSET(23921, glBlendFuncSeparateEXT, glBlendFuncSeparateEXT, NULL, 765),
    NAME_FUNC_OFFSET(23944, glWindowPos2dMESA, glWindowPos2dMESA, NULL, 767),
    NAME_FUNC_OFFSET(23958, glWindowPos2dMESA, glWindowPos2dMESA, NULL, 767),
    NAME_FUNC_OFFSET(23975, glWindowPos2dvMESA, glWindowPos2dvMESA, NULL, 768),
    NAME_FUNC_OFFSET(23990, glWindowPos2dvMESA, glWindowPos2dvMESA, NULL, 768),
    NAME_FUNC_OFFSET(24008, glWindowPos2fMESA, glWindowPos2fMESA, NULL, 769),
    NAME_FUNC_OFFSET(24022, glWindowPos2fMESA, glWindowPos2fMESA, NULL, 769),
    NAME_FUNC_OFFSET(24039, glWindowPos2fvMESA, glWindowPos2fvMESA, NULL, 770),
    NAME_FUNC_OFFSET(24054, glWindowPos2fvMESA, glWindowPos2fvMESA, NULL, 770),
    NAME_FUNC_OFFSET(24072, glWindowPos2iMESA, glWindowPos2iMESA, NULL, 771),
    NAME_FUNC_OFFSET(24086, glWindowPos2iMESA, glWindowPos2iMESA, NULL, 771),
    NAME_FUNC_OFFSET(24103, glWindowPos2ivMESA, glWindowPos2ivMESA, NULL, 772),
    NAME_FUNC_OFFSET(24118, glWindowPos2ivMESA, glWindowPos2ivMESA, NULL, 772),
    NAME_FUNC_OFFSET(24136, glWindowPos2sMESA, glWindowPos2sMESA, NULL, 773),
    NAME_FUNC_OFFSET(24150, glWindowPos2sMESA, glWindowPos2sMESA, NULL, 773),
    NAME_FUNC_OFFSET(24167, glWindowPos2svMESA, glWindowPos2svMESA, NULL, 774),
    NAME_FUNC_OFFSET(24182, glWindowPos2svMESA, glWindowPos2svMESA, NULL, 774),
    NAME_FUNC_OFFSET(24200, glWindowPos3dMESA, glWindowPos3dMESA, NULL, 775),
    NAME_FUNC_OFFSET(24214, glWindowPos3dMESA, glWindowPos3dMESA, NULL, 775),
    NAME_FUNC_OFFSET(24231, glWindowPos3dvMESA, glWindowPos3dvMESA, NULL, 776),
    NAME_FUNC_OFFSET(24246, glWindowPos3dvMESA, glWindowPos3dvMESA, NULL, 776),
    NAME_FUNC_OFFSET(24264, glWindowPos3fMESA, glWindowPos3fMESA, NULL, 777),
    NAME_FUNC_OFFSET(24278, glWindowPos3fMESA, glWindowPos3fMESA, NULL, 777),
    NAME_FUNC_OFFSET(24295, glWindowPos3fvMESA, glWindowPos3fvMESA, NULL, 778),
    NAME_FUNC_OFFSET(24310, glWindowPos3fvMESA, glWindowPos3fvMESA, NULL, 778),
    NAME_FUNC_OFFSET(24328, glWindowPos3iMESA, glWindowPos3iMESA, NULL, 779),
    NAME_FUNC_OFFSET(24342, glWindowPos3iMESA, glWindowPos3iMESA, NULL, 779),
    NAME_FUNC_OFFSET(24359, glWindowPos3ivMESA, glWindowPos3ivMESA, NULL, 780),
    NAME_FUNC_OFFSET(24374, glWindowPos3ivMESA, glWindowPos3ivMESA, NULL, 780),
    NAME_FUNC_OFFSET(24392, glWindowPos3sMESA, glWindowPos3sMESA, NULL, 781),
    NAME_FUNC_OFFSET(24406, glWindowPos3sMESA, glWindowPos3sMESA, NULL, 781),
    NAME_FUNC_OFFSET(24423, glWindowPos3svMESA, glWindowPos3svMESA, NULL, 782),
    NAME_FUNC_OFFSET(24438, glWindowPos3svMESA, glWindowPos3svMESA, NULL, 782),
    NAME_FUNC_OFFSET(24456, glBindProgramNV, glBindProgramNV, NULL, 794),
    NAME_FUNC_OFFSET(24473, glDeleteProgramsNV, glDeleteProgramsNV, NULL, 795),
    NAME_FUNC_OFFSET(24493, glGenProgramsNV, glGenProgramsNV, NULL, 797),
    NAME_FUNC_OFFSET(24510, glGetVertexAttribPointervNV, glGetVertexAttribPointervNV, NULL, 803),
    NAME_FUNC_OFFSET(24536, glGetVertexAttribPointervNV, glGetVertexAttribPointervNV, NULL, 803),
    NAME_FUNC_OFFSET(24565, glIsProgramNV, glIsProgramNV, NULL, 807),
    NAME_FUNC_OFFSET(24580, glPointParameteriNV, glPointParameteriNV, NULL, 871),
    NAME_FUNC_OFFSET(24598, glPointParameterivNV, glPointParameterivNV, NULL, 872),
    NAME_FUNC_OFFSET(24617, gl_dispatch_stub_875, gl_dispatch_stub_875, NULL, 875),
    NAME_FUNC_OFFSET(24638, gl_dispatch_stub_875, gl_dispatch_stub_875, NULL, 875),
    NAME_FUNC_OFFSET(24662, gl_dispatch_stub_877, gl_dispatch_stub_877, NULL, 877),
    NAME_FUNC_OFFSET(24678, gl_dispatch_stub_877, gl_dispatch_stub_877, NULL, 877),
    NAME_FUNC_OFFSET(24697, glPrimitiveRestartIndexNV, glPrimitiveRestartIndexNV, NULL, 884),
    NAME_FUNC_OFFSET(24721, gl_dispatch_stub_886, gl_dispatch_stub_886, NULL, 886),
    NAME_FUNC_OFFSET(24734, gl_dispatch_stub_887, gl_dispatch_stub_887, NULL, 887),
    NAME_FUNC_OFFSET(24748, gl_dispatch_stub_888, gl_dispatch_stub_888, NULL, 888),
    NAME_FUNC_OFFSET(24762, gl_dispatch_stub_889, gl_dispatch_stub_889, NULL, 889),
    NAME_FUNC_OFFSET(24775, gl_dispatch_stub_890, gl_dispatch_stub_890, NULL, 890),
    NAME_FUNC_OFFSET(24785, gl_dispatch_stub_891, gl_dispatch_stub_891, NULL, 891),
    NAME_FUNC_OFFSET(24799, gl_dispatch_stub_892, gl_dispatch_stub_892, NULL, 892),
    NAME_FUNC_OFFSET(24806, gl_dispatch_stub_893, gl_dispatch_stub_893, NULL, 893),
    NAME_FUNC_OFFSET(24814, gl_dispatch_stub_894, gl_dispatch_stub_894, NULL, 894),
    NAME_FUNC_OFFSET(24825, gl_dispatch_stub_895, gl_dispatch_stub_895, NULL, 895),
    NAME_FUNC_OFFSET(24841, gl_dispatch_stub_896, gl_dispatch_stub_896, NULL, 896),
    NAME_FUNC_OFFSET(24853, gl_dispatch_stub_897, gl_dispatch_stub_897, NULL, 897),
    NAME_FUNC_OFFSET(24866, gl_dispatch_stub_898, gl_dispatch_stub_898, NULL, 898),
    NAME_FUNC_OFFSET(24882, gl_dispatch_stub_899, gl_dispatch_stub_899, NULL, 899),
    NAME_FUNC_OFFSET(24896, gl_dispatch_stub_901, gl_dispatch_stub_901, NULL, 901),
    NAME_FUNC_OFFSET(24916, gl_dispatch_stub_902, gl_dispatch_stub_902, NULL, 902),
    NAME_FUNC_OFFSET(24930, gl_dispatch_stub_903, gl_dispatch_stub_903, NULL, 903),
    NAME_FUNC_OFFSET(24945, gl_dispatch_stub_904, gl_dispatch_stub_904, NULL, 904),
    NAME_FUNC_OFFSET(24954, gl_dispatch_stub_905, gl_dispatch_stub_905, NULL, 905),
    NAME_FUNC_OFFSET(24964, gl_dispatch_stub_906, gl_dispatch_stub_906, NULL, 906),
    NAME_FUNC_OFFSET(24977, gl_dispatch_stub_907, gl_dispatch_stub_907, NULL, 907),
    NAME_FUNC_OFFSET(24991, gl_dispatch_stub_908, gl_dispatch_stub_908, NULL, 908),
    NAME_FUNC_OFFSET(25003, gl_dispatch_stub_909, gl_dispatch_stub_909, NULL, 909),
    NAME_FUNC_OFFSET(25016, gl_dispatch_stub_910, gl_dispatch_stub_910, NULL, 910),
    NAME_FUNC_OFFSET(25030, gl_dispatch_stub_911, gl_dispatch_stub_911, NULL, 911),
    NAME_FUNC_OFFSET(25048, gl_dispatch_stub_912, gl_dispatch_stub_912, NULL, 912),
    NAME_FUNC_OFFSET(25059, gl_dispatch_stub_913, gl_dispatch_stub_913, NULL, 913),
    NAME_FUNC_OFFSET(25068, gl_dispatch_stub_914, gl_dispatch_stub_914, NULL, 914),
    NAME_FUNC_OFFSET(25086, gl_dispatch_stub_915, gl_dispatch_stub_915, NULL, 915),
    NAME_FUNC_OFFSET(25105, gl_dispatch_stub_916, gl_dispatch_stub_916, NULL, 916),
    NAME_FUNC_OFFSET(25118, gl_dispatch_stub_917, gl_dispatch_stub_917, NULL, 917),
    NAME_FUNC_OFFSET(25135, gl_dispatch_stub_918, gl_dispatch_stub_918, NULL, 918),
    NAME_FUNC_OFFSET(25145, gl_dispatch_stub_919, gl_dispatch_stub_919, NULL, 919),
    NAME_FUNC_OFFSET(25163, gl_dispatch_stub_920, gl_dispatch_stub_920, NULL, 920),
    NAME_FUNC_OFFSET(25172, gl_dispatch_stub_921, gl_dispatch_stub_921, NULL, 921),
    NAME_FUNC_OFFSET(25182, gl_dispatch_stub_922, gl_dispatch_stub_922, NULL, 922),
    NAME_FUNC_OFFSET(25193, gl_dispatch_stub_925, gl_dispatch_stub_925, NULL, 925),
    NAME_FUNC_OFFSET(25209, gl_dispatch_stub_926, gl_dispatch_stub_926, NULL, 926),
    NAME_FUNC_OFFSET(25226, gl_dispatch_stub_927, gl_dispatch_stub_927, NULL, 927),
    NAME_FUNC_OFFSET(25239, gl_dispatch_stub_928, gl_dispatch_stub_928, NULL, 928),
    NAME_FUNC_OFFSET(25252, gl_dispatch_stub_929, gl_dispatch_stub_929, NULL, 929),
    NAME_FUNC_OFFSET(25263, gl_dispatch_stub_930, gl_dispatch_stub_930, NULL, 930),
    NAME_FUNC_OFFSET(25279, gl_dispatch_stub_931, gl_dispatch_stub_931, NULL, 931),
    NAME_FUNC_OFFSET(25288, gl_dispatch_stub_933, gl_dispatch_stub_933, NULL, 933),
    NAME_FUNC_OFFSET(25312, gl_dispatch_stub_933, gl_dispatch_stub_933, NULL, 933),
    NAME_FUNC_OFFSET(25339, gl_dispatch_stub_933, gl_dispatch_stub_933, NULL, 933),
    NAME_FUNC_OFFSET(25366, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 934),
    NAME_FUNC_OFFSET(25384, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 934),
    NAME_FUNC_OFFSET(25405, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 935),
    NAME_FUNC_OFFSET(25424, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 935),
    NAME_FUNC_OFFSET(25446, glCheckFramebufferStatusEXT, glCheckFramebufferStatusEXT, NULL, 936),
    NAME_FUNC_OFFSET(25471, glCheckFramebufferStatusEXT, glCheckFramebufferStatusEXT, NULL, 936),
    NAME_FUNC_OFFSET(25499, glDeleteFramebuffersEXT, glDeleteFramebuffersEXT, NULL, 937),
    NAME_FUNC_OFFSET(25520, glDeleteFramebuffersEXT, glDeleteFramebuffersEXT, NULL, 937),
    NAME_FUNC_OFFSET(25544, glDeleteRenderbuffersEXT, glDeleteRenderbuffersEXT, NULL, 938),
    NAME_FUNC_OFFSET(25566, glDeleteRenderbuffersEXT, glDeleteRenderbuffersEXT, NULL, 938),
    NAME_FUNC_OFFSET(25591, glFramebufferRenderbufferEXT, glFramebufferRenderbufferEXT, NULL, 939),
    NAME_FUNC_OFFSET(25617, glFramebufferRenderbufferEXT, glFramebufferRenderbufferEXT, NULL, 939),
    NAME_FUNC_OFFSET(25646, glFramebufferTexture1DEXT, glFramebufferTexture1DEXT, NULL, 940),
    NAME_FUNC_OFFSET(25669, glFramebufferTexture2DEXT, glFramebufferTexture2DEXT, NULL, 941),
    NAME_FUNC_OFFSET(25692, glFramebufferTexture2DEXT, glFramebufferTexture2DEXT, NULL, 941),
    NAME_FUNC_OFFSET(25718, glFramebufferTexture3DEXT, glFramebufferTexture3DEXT, NULL, 942),
    NAME_FUNC_OFFSET(25741, glFramebufferTexture3DEXT, glFramebufferTexture3DEXT, NULL, 942),
    NAME_FUNC_OFFSET(25767, glGenFramebuffersEXT, glGenFramebuffersEXT, NULL, 943),
    NAME_FUNC_OFFSET(25785, glGenFramebuffersEXT, glGenFramebuffersEXT, NULL, 943),
    NAME_FUNC_OFFSET(25806, glGenRenderbuffersEXT, glGenRenderbuffersEXT, NULL, 944),
    NAME_FUNC_OFFSET(25825, glGenRenderbuffersEXT, glGenRenderbuffersEXT, NULL, 944),
    NAME_FUNC_OFFSET(25847, glGenerateMipmapEXT, glGenerateMipmapEXT, NULL, 945),
    NAME_FUNC_OFFSET(25864, glGenerateMipmapEXT, glGenerateMipmapEXT, NULL, 945),
    NAME_FUNC_OFFSET(25884, glGetFramebufferAttachmentParameterivEXT, glGetFramebufferAttachmentParameterivEXT, NULL, 946),
    NAME_FUNC_OFFSET(25922, glGetFramebufferAttachmentParameterivEXT, glGetFramebufferAttachmentParameterivEXT, NULL, 946),
    NAME_FUNC_OFFSET(25963, glGetRenderbufferParameterivEXT, glGetRenderbufferParameterivEXT, NULL, 947),
    NAME_FUNC_OFFSET(25992, glGetRenderbufferParameterivEXT, glGetRenderbufferParameterivEXT, NULL, 947),
    NAME_FUNC_OFFSET(26024, glIsFramebufferEXT, glIsFramebufferEXT, NULL, 948),
    NAME_FUNC_OFFSET(26040, glIsFramebufferEXT, glIsFramebufferEXT, NULL, 948),
    NAME_FUNC_OFFSET(26059, glIsRenderbufferEXT, glIsRenderbufferEXT, NULL, 949),
    NAME_FUNC_OFFSET(26076, glIsRenderbufferEXT, glIsRenderbufferEXT, NULL, 949),
    NAME_FUNC_OFFSET(26096, glRenderbufferStorageEXT, glRenderbufferStorageEXT, NULL, 950),
    NAME_FUNC_OFFSET(26118, glRenderbufferStorageEXT, glRenderbufferStorageEXT, NULL, 950),
    NAME_FUNC_OFFSET(26143, gl_dispatch_stub_951, gl_dispatch_stub_951, NULL, 951),
    NAME_FUNC_OFFSET(26161, glBindFragDataLocationEXT, glBindFragDataLocationEXT, NULL, 954),
    NAME_FUNC_OFFSET(26184, glGetFragDataLocationEXT, glGetFragDataLocationEXT, NULL, 955),
    NAME_FUNC_OFFSET(26206, glGetUniformuivEXT, glGetUniformuivEXT, NULL, 956),
    NAME_FUNC_OFFSET(26222, glGetVertexAttribIivEXT, glGetVertexAttribIivEXT, NULL, 957),
    NAME_FUNC_OFFSET(26243, glGetVertexAttribIuivEXT, glGetVertexAttribIuivEXT, NULL, 958),
    NAME_FUNC_OFFSET(26265, glUniform1uiEXT, glUniform1uiEXT, NULL, 959),
    NAME_FUNC_OFFSET(26278, glUniform1uivEXT, glUniform1uivEXT, NULL, 960),
    NAME_FUNC_OFFSET(26292, glUniform2uiEXT, glUniform2uiEXT, NULL, 961),
    NAME_FUNC_OFFSET(26305, glUniform2uivEXT, glUniform2uivEXT, NULL, 962),
    NAME_FUNC_OFFSET(26319, glUniform3uiEXT, glUniform3uiEXT, NULL, 963),
    NAME_FUNC_OFFSET(26332, glUniform3uivEXT, glUniform3uivEXT, NULL, 964),
    NAME_FUNC_OFFSET(26346, glUniform4uiEXT, glUniform4uiEXT, NULL, 965),
    NAME_FUNC_OFFSET(26359, glUniform4uivEXT, glUniform4uivEXT, NULL, 966),
    NAME_FUNC_OFFSET(26373, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 967),
    NAME_FUNC_OFFSET(26391, glVertexAttribI1ivEXT, glVertexAttribI1ivEXT, NULL, 968),
    NAME_FUNC_OFFSET(26410, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 969),
    NAME_FUNC_OFFSET(26429, glVertexAttribI1uivEXT, glVertexAttribI1uivEXT, NULL, 970),
    NAME_FUNC_OFFSET(26449, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 971),
    NAME_FUNC_OFFSET(26467, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 972),
    NAME_FUNC_OFFSET(26486, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 973),
    NAME_FUNC_OFFSET(26505, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 974),
    NAME_FUNC_OFFSET(26525, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 975),
    NAME_FUNC_OFFSET(26543, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 976),
    NAME_FUNC_OFFSET(26562, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 977),
    NAME_FUNC_OFFSET(26581, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 978),
    NAME_FUNC_OFFSET(26601, glVertexAttribI4bvEXT, glVertexAttribI4bvEXT, NULL, 979),
    NAME_FUNC_OFFSET(26620, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 980),
    NAME_FUNC_OFFSET(26638, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 981),
    NAME_FUNC_OFFSET(26657, glVertexAttribI4svEXT, glVertexAttribI4svEXT, NULL, 982),
    NAME_FUNC_OFFSET(26676, glVertexAttribI4ubvEXT, glVertexAttribI4ubvEXT, NULL, 983),
    NAME_FUNC_OFFSET(26696, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 984),
    NAME_FUNC_OFFSET(26715, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 985),
    NAME_FUNC_OFFSET(26735, glVertexAttribI4usvEXT, glVertexAttribI4usvEXT, NULL, 986),
    NAME_FUNC_OFFSET(26755, glVertexAttribIPointerEXT, glVertexAttribIPointerEXT, NULL, 987),
    NAME_FUNC_OFFSET(26778, glFramebufferTextureLayerEXT, glFramebufferTextureLayerEXT, NULL, 988),
    NAME_FUNC_OFFSET(26804, glFramebufferTextureLayerEXT, glFramebufferTextureLayerEXT, NULL, 988),
    NAME_FUNC_OFFSET(26833, glColorMaskIndexedEXT, glColorMaskIndexedEXT, NULL, 989),
    NAME_FUNC_OFFSET(26846, glDisableIndexedEXT, glDisableIndexedEXT, NULL, 990),
    NAME_FUNC_OFFSET(26857, glEnableIndexedEXT, glEnableIndexedEXT, NULL, 991),
    NAME_FUNC_OFFSET(26867, glGetBooleanIndexedvEXT, glGetBooleanIndexedvEXT, NULL, 992),
    NAME_FUNC_OFFSET(26883, glGetIntegerIndexedvEXT, glGetIntegerIndexedvEXT, NULL, 993),
    NAME_FUNC_OFFSET(26899, glIsEnabledIndexedEXT, glIsEnabledIndexedEXT, NULL, 994),
    NAME_FUNC_OFFSET(26912, glGetTexParameterIivEXT, glGetTexParameterIivEXT, NULL, 997),
    NAME_FUNC_OFFSET(26933, glGetTexParameterIuivEXT, glGetTexParameterIuivEXT, NULL, 998),
    NAME_FUNC_OFFSET(26955, glTexParameterIivEXT, glTexParameterIivEXT, NULL, 999),
    NAME_FUNC_OFFSET(26973, glTexParameterIuivEXT, glTexParameterIuivEXT, NULL, 1000),
    NAME_FUNC_OFFSET(26992, glBeginConditionalRenderNV, glBeginConditionalRenderNV, NULL, 1001),
    NAME_FUNC_OFFSET(27017, glEndConditionalRenderNV, glEndConditionalRenderNV, NULL, 1002),
    NAME_FUNC_OFFSET(27040, glBeginTransformFeedbackEXT, glBeginTransformFeedbackEXT, NULL, 1003),
    NAME_FUNC_OFFSET(27065, glBindBufferBaseEXT, glBindBufferBaseEXT, NULL, 1004),
    NAME_FUNC_OFFSET(27082, glBindBufferRangeEXT, glBindBufferRangeEXT, NULL, 1006),
    NAME_FUNC_OFFSET(27100, glEndTransformFeedbackEXT, glEndTransformFeedbackEXT, NULL, 1007),
    NAME_FUNC_OFFSET(27123, glGetTransformFeedbackVaryingEXT, glGetTransformFeedbackVaryingEXT, NULL, 1008),
    NAME_FUNC_OFFSET(27153, glTransformFeedbackVaryingsEXT, glTransformFeedbackVaryingsEXT, NULL, 1009),
    NAME_FUNC_OFFSET(27181, glProvokingVertexEXT, glProvokingVertexEXT, NULL, 1010),
    NAME_FUNC_OFFSET(27199, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(27220, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(-1, NULL, NULL, NULL, 0)
};

#undef NAME_FUNC_OFFSET
