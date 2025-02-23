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
    "glActiveTexture\0"
    "glClientActiveTexture\0"
    "glMultiTexCoord1d\0"
    "glMultiTexCoord1dv\0"
    "glMultiTexCoord1fARB\0"
    "glMultiTexCoord1fvARB\0"
    "glMultiTexCoord1i\0"
    "glMultiTexCoord1iv\0"
    "glMultiTexCoord1s\0"
    "glMultiTexCoord1sv\0"
    "glMultiTexCoord2d\0"
    "glMultiTexCoord2dv\0"
    "glMultiTexCoord2fARB\0"
    "glMultiTexCoord2fvARB\0"
    "glMultiTexCoord2i\0"
    "glMultiTexCoord2iv\0"
    "glMultiTexCoord2s\0"
    "glMultiTexCoord2sv\0"
    "glMultiTexCoord3d\0"
    "glMultiTexCoord3dv\0"
    "glMultiTexCoord3fARB\0"
    "glMultiTexCoord3fvARB\0"
    "glMultiTexCoord3i\0"
    "glMultiTexCoord3iv\0"
    "glMultiTexCoord3s\0"
    "glMultiTexCoord3sv\0"
    "glMultiTexCoord4d\0"
    "glMultiTexCoord4dv\0"
    "glMultiTexCoord4fARB\0"
    "glMultiTexCoord4fvARB\0"
    "glMultiTexCoord4i\0"
    "glMultiTexCoord4iv\0"
    "glMultiTexCoord4s\0"
    "glMultiTexCoord4sv\0"
    "glCompressedTexImage1D\0"
    "glCompressedTexImage2D\0"
    "glCompressedTexImage3D\0"
    "glCompressedTexSubImage1D\0"
    "glCompressedTexSubImage2D\0"
    "glCompressedTexSubImage3D\0"
    "glGetCompressedTexImage\0"
    "glLoadTransposeMatrixd\0"
    "glLoadTransposeMatrixf\0"
    "glMultTransposeMatrixd\0"
    "glMultTransposeMatrixf\0"
    "glSampleCoverage\0"
    "glBlendFuncSeparate\0"
    "glFogCoordPointer\0"
    "glFogCoordd\0"
    "glFogCoorddv\0"
    "glMultiDrawArrays\0"
    "glPointParameterf\0"
    "glPointParameterfv\0"
    "glPointParameteri\0"
    "glPointParameteriv\0"
    "glSecondaryColor3b\0"
    "glSecondaryColor3bv\0"
    "glSecondaryColor3d\0"
    "glSecondaryColor3dv\0"
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
    "glWindowPos2d\0"
    "glWindowPos2dv\0"
    "glWindowPos2f\0"
    "glWindowPos2fv\0"
    "glWindowPos2i\0"
    "glWindowPos2iv\0"
    "glWindowPos2s\0"
    "glWindowPos2sv\0"
    "glWindowPos3d\0"
    "glWindowPos3dv\0"
    "glWindowPos3f\0"
    "glWindowPos3fv\0"
    "glWindowPos3i\0"
    "glWindowPos3iv\0"
    "glWindowPos3s\0"
    "glWindowPos3sv\0"
    "glBeginQuery\0"
    "glBindBuffer\0"
    "glBufferData\0"
    "glBufferSubData\0"
    "glDeleteBuffers\0"
    "glDeleteQueries\0"
    "glEndQuery\0"
    "glGenBuffers\0"
    "glGenQueries\0"
    "glGetBufferParameteriv\0"
    "glGetBufferPointerv\0"
    "glGetBufferSubData\0"
    "glGetQueryObjectiv\0"
    "glGetQueryObjectuiv\0"
    "glGetQueryiv\0"
    "glIsBuffer\0"
    "glIsQuery\0"
    "glMapBuffer\0"
    "glUnmapBuffer\0"
    "glAttachShader\0"
    "glBindAttribLocation\0"
    "glBlendEquationSeparate\0"
    "glCompileShader\0"
    "glCreateProgram\0"
    "glCreateShader\0"
    "glDeleteProgram\0"
    "glDeleteShader\0"
    "glDetachShader\0"
    "glDisableVertexAttribArray\0"
    "glDrawBuffers\0"
    "glEnableVertexAttribArray\0"
    "glGetActiveAttrib\0"
    "glGetActiveUniform\0"
    "glGetAttachedShaders\0"
    "glGetAttribLocation\0"
    "glGetProgramInfoLog\0"
    "glGetProgramiv\0"
    "glGetShaderInfoLog\0"
    "glGetShaderSource\0"
    "glGetShaderiv\0"
    "glGetUniformLocation\0"
    "glGetUniformfv\0"
    "glGetUniformiv\0"
    "glGetVertexAttribPointerv\0"
    "glGetVertexAttribdv\0"
    "glGetVertexAttribfv\0"
    "glGetVertexAttribiv\0"
    "glIsProgram\0"
    "glIsShader\0"
    "glLinkProgram\0"
    "glShaderSource\0"
    "glStencilFuncSeparate\0"
    "glStencilMaskSeparate\0"
    "glStencilOpSeparate\0"
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
    "glVertexAttrib1d\0"
    "glVertexAttrib1dv\0"
    "glVertexAttrib1s\0"
    "glVertexAttrib1sv\0"
    "glVertexAttrib2d\0"
    "glVertexAttrib2dv\0"
    "glVertexAttrib2s\0"
    "glVertexAttrib2sv\0"
    "glVertexAttrib3d\0"
    "glVertexAttrib3dv\0"
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
    "glVertexAttrib4iv\0"
    "glVertexAttrib4s\0"
    "glVertexAttrib4sv\0"
    "glVertexAttrib4ubv\0"
    "glVertexAttrib4uiv\0"
    "glVertexAttrib4usv\0"
    "glVertexAttribPointer\0"
    "glUniformMatrix2x3fv\0"
    "glUniformMatrix2x4fv\0"
    "glUniformMatrix3x2fv\0"
    "glUniformMatrix3x4fv\0"
    "glUniformMatrix4x2fv\0"
    "glUniformMatrix4x3fv\0"
    "glBeginConditionalRender\0"
    "glBeginTransformFeedback\0"
    "glBindBufferBase\0"
    "glBindBufferRange\0"
    "glBindFragDataLocation\0"
    "glClampColor\0"
    "glClearBufferfi\0"
    "glClearBufferfv\0"
    "glClearBufferiv\0"
    "glClearBufferuiv\0"
    "glColorMaski\0"
    "glDisablei\0"
    "glEnablei\0"
    "glEndConditionalRender\0"
    "glEndTransformFeedback\0"
    "glGetBooleani_v\0"
    "glGetFragDataLocation\0"
    "glGetIntegeri_v\0"
    "glGetStringi\0"
    "glGetTexParameterIiv\0"
    "glGetTexParameterIuiv\0"
    "glGetTransformFeedbackVarying\0"
    "glGetUniformuiv\0"
    "glGetVertexAttribIiv\0"
    "glGetVertexAttribIuiv\0"
    "glIsEnabledi\0"
    "glTexParameterIiv\0"
    "glTexParameterIuiv\0"
    "glTransformFeedbackVaryings\0"
    "glUniform1ui\0"
    "glUniform1uiv\0"
    "glUniform2ui\0"
    "glUniform2uiv\0"
    "glUniform3ui\0"
    "glUniform3uiv\0"
    "glUniform4ui\0"
    "glUniform4uiv\0"
    "glVertexAttribI1iv\0"
    "glVertexAttribI1uiv\0"
    "glVertexAttribI4bv\0"
    "glVertexAttribI4sv\0"
    "glVertexAttribI4ubv\0"
    "glVertexAttribI4usv\0"
    "glVertexAttribIPointer\0"
    "glPrimitiveRestartIndex\0"
    "glTexBuffer\0"
    "glFramebufferTexture\0"
    "glGetBufferParameteri64v\0"
    "glGetInteger64i_v\0"
    "glVertexAttribDivisor\0"
    "glMinSampleShading\0"
    "glMemoryBarrierByRegion\0"
    "glBindProgramARB\0"
    "glDeleteProgramsARB\0"
    "glGenProgramsARB\0"
    "glGetProgramEnvParameterdvARB\0"
    "glGetProgramEnvParameterfvARB\0"
    "glGetProgramLocalParameterdvARB\0"
    "glGetProgramLocalParameterfvARB\0"
    "glGetProgramStringARB\0"
    "glGetProgramivARB\0"
    "glIsProgramARB\0"
    "glProgramEnvParameter4dARB\0"
    "glProgramEnvParameter4dvARB\0"
    "glProgramEnvParameter4fARB\0"
    "glProgramEnvParameter4fvARB\0"
    "glProgramLocalParameter4dARB\0"
    "glProgramLocalParameter4dvARB\0"
    "glProgramLocalParameter4fARB\0"
    "glProgramLocalParameter4fvARB\0"
    "glProgramStringARB\0"
    "glVertexAttrib1fARB\0"
    "glVertexAttrib1fvARB\0"
    "glVertexAttrib2fARB\0"
    "glVertexAttrib2fvARB\0"
    "glVertexAttrib3fARB\0"
    "glVertexAttrib3fvARB\0"
    "glVertexAttrib4fARB\0"
    "glVertexAttrib4fvARB\0"
    "glAttachObjectARB\0"
    "glCreateProgramObjectARB\0"
    "glCreateShaderObjectARB\0"
    "glDeleteObjectARB\0"
    "glDetachObjectARB\0"
    "glGetAttachedObjectsARB\0"
    "glGetHandleARB\0"
    "glGetInfoLogARB\0"
    "glGetObjectParameterfvARB\0"
    "glGetObjectParameterivARB\0"
    "glDrawArraysInstanced\0"
    "glDrawElementsInstanced\0"
    "glBindFramebuffer\0"
    "glBindRenderbuffer\0"
    "glBlitFramebuffer\0"
    "glCheckFramebufferStatus\0"
    "glDeleteFramebuffers\0"
    "glDeleteRenderbuffers\0"
    "glFramebufferRenderbuffer\0"
    "glFramebufferTexture1D\0"
    "glFramebufferTexture2D\0"
    "glFramebufferTexture3D\0"
    "glFramebufferTextureLayer\0"
    "glGenFramebuffers\0"
    "glGenRenderbuffers\0"
    "glGenerateMipmap\0"
    "glGetFramebufferAttachmentParameteriv\0"
    "glGetRenderbufferParameteriv\0"
    "glIsFramebuffer\0"
    "glIsRenderbuffer\0"
    "glRenderbufferStorage\0"
    "glRenderbufferStorageMultisample\0"
    "glFlushMappedBufferRange\0"
    "glMapBufferRange\0"
    "glBindVertexArray\0"
    "glDeleteVertexArrays\0"
    "glGenVertexArrays\0"
    "glIsVertexArray\0"
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
    "glProvokingVertex\0"
    "glGetMultisamplefv\0"
    "glSampleMaski\0"
    "glTexImage2DMultisample\0"
    "glTexImage3DMultisample\0"
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
    "glGetQueryObjecti64v\0"
    "glGetQueryObjectui64v\0"
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
    "glDrawArraysIndirect\0"
    "glDrawElementsIndirect\0"
    "glGetUniformdv\0"
    "glUniform1d\0"
    "glUniform1dv\0"
    "glUniform2d\0"
    "glUniform2dv\0"
    "glUniform3d\0"
    "glUniform3dv\0"
    "glUniform4d\0"
    "glUniform4dv\0"
    "glUniformMatrix2dv\0"
    "glUniformMatrix2x3dv\0"
    "glUniformMatrix2x4dv\0"
    "glUniformMatrix3dv\0"
    "glUniformMatrix3x2dv\0"
    "glUniformMatrix3x4dv\0"
    "glUniformMatrix4dv\0"
    "glUniformMatrix4x2dv\0"
    "glUniformMatrix4x3dv\0"
    "glGetActiveSubroutineName\0"
    "glGetActiveSubroutineUniformName\0"
    "glGetActiveSubroutineUniformiv\0"
    "glGetProgramStageiv\0"
    "glGetSubroutineIndex\0"
    "glGetSubroutineUniformLocation\0"
    "glGetUniformSubroutineuiv\0"
    "glUniformSubroutinesuiv\0"
    "glPatchParameterfv\0"
    "glPatchParameteri\0"
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
    "glGetVertexAttribLdv\0"
    "glVertexAttribL1d\0"
    "glVertexAttribL1dv\0"
    "glVertexAttribL2d\0"
    "glVertexAttribL2dv\0"
    "glVertexAttribL3d\0"
    "glVertexAttribL3dv\0"
    "glVertexAttribL4d\0"
    "glVertexAttribL4dv\0"
    "glVertexAttribLPointer\0"
    "glDepthRangeArrayv\0"
    "glDepthRangeIndexed\0"
    "glGetDoublei_v\0"
    "glGetFloati_v\0"
    "glScissorArrayv\0"
    "glScissorIndexed\0"
    "glScissorIndexedv\0"
    "glViewportArrayv\0"
    "glViewportIndexedf\0"
    "glViewportIndexedfv\0"
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
    "glGetInternalformativ\0"
    "glGetActiveAtomicCounterBufferiv\0"
    "glBindImageTexture\0"
    "glMemoryBarrier\0"
    "glTexStorage1D\0"
    "glTexStorage2D\0"
    "glTexStorage3D\0"
    "glTextureStorage1DEXT\0"
    "glTextureStorage2DEXT\0"
    "glTextureStorage3DEXT\0"
    "glClearBufferData\0"
    "glClearBufferSubData\0"
    "glDispatchCompute\0"
    "glDispatchComputeIndirect\0"
    "glCopyImageSubData\0"
    "glTextureView\0"
    "glBindVertexBuffer\0"
    "glVertexAttribBinding\0"
    "glVertexAttribFormat\0"
    "glVertexAttribIFormat\0"
    "glVertexAttribLFormat\0"
    "glVertexBindingDivisor\0"
    "glFramebufferParameteri\0"
    "glGetFramebufferParameteriv\0"
    "glGetInternalformati64v\0"
    "glMultiDrawArraysIndirect\0"
    "glMultiDrawElementsIndirect\0"
    "glGetProgramInterfaceiv\0"
    "glGetProgramResourceIndex\0"
    "glGetProgramResourceLocation\0"
    "glGetProgramResourceLocationIndex\0"
    "glGetProgramResourceName\0"
    "glGetProgramResourceiv\0"
    "glShaderStorageBlockBinding\0"
    "glTexBufferRange\0"
    "glTexStorage2DMultisample\0"
    "glTexStorage3DMultisample\0"
    "glBufferStorage\0"
    "glClearTexImage\0"
    "glClearTexSubImage\0"
    "glBindBuffersBase\0"
    "glBindBuffersRange\0"
    "glBindImageTextures\0"
    "glBindSamplers\0"
    "glBindTextures\0"
    "glBindVertexBuffers\0"
    "glGetImageHandleARB\0"
    "glGetTextureHandleARB\0"
    "glGetTextureSamplerHandleARB\0"
    "glGetVertexAttribLui64vARB\0"
    "glIsImageHandleResidentARB\0"
    "glIsTextureHandleResidentARB\0"
    "glMakeImageHandleNonResidentARB\0"
    "glMakeImageHandleResidentARB\0"
    "glMakeTextureHandleNonResidentARB\0"
    "glMakeTextureHandleResidentARB\0"
    "glProgramUniformHandleui64ARB\0"
    "glProgramUniformHandleui64vARB\0"
    "glUniformHandleui64ARB\0"
    "glUniformHandleui64vARB\0"
    "glVertexAttribL1ui64ARB\0"
    "glVertexAttribL1ui64vARB\0"
    "glDispatchComputeGroupSizeARB\0"
    "glMultiDrawArraysIndirectCountARB\0"
    "glMultiDrawElementsIndirectCountARB\0"
    "glClipControl\0"
    "glBindTextureUnit\0"
    "glBlitNamedFramebuffer\0"
    "glCheckNamedFramebufferStatus\0"
    "glClearNamedBufferData\0"
    "glClearNamedBufferSubData\0"
    "glClearNamedFramebufferfi\0"
    "glClearNamedFramebufferfv\0"
    "glClearNamedFramebufferiv\0"
    "glClearNamedFramebufferuiv\0"
    "glCompressedTextureSubImage1D\0"
    "glCompressedTextureSubImage2D\0"
    "glCompressedTextureSubImage3D\0"
    "glCopyNamedBufferSubData\0"
    "glCopyTextureSubImage1D\0"
    "glCopyTextureSubImage2D\0"
    "glCopyTextureSubImage3D\0"
    "glCreateBuffers\0"
    "glCreateFramebuffers\0"
    "glCreateProgramPipelines\0"
    "glCreateQueries\0"
    "glCreateRenderbuffers\0"
    "glCreateSamplers\0"
    "glCreateTextures\0"
    "glCreateTransformFeedbacks\0"
    "glCreateVertexArrays\0"
    "glDisableVertexArrayAttrib\0"
    "glEnableVertexArrayAttrib\0"
    "glFlushMappedNamedBufferRange\0"
    "glGenerateTextureMipmap\0"
    "glGetCompressedTextureImage\0"
    "glGetNamedBufferParameteri64v\0"
    "glGetNamedBufferParameteriv\0"
    "glGetNamedBufferPointerv\0"
    "glGetNamedBufferSubData\0"
    "glGetNamedFramebufferAttachmentParameteriv\0"
    "glGetNamedFramebufferParameteriv\0"
    "glGetNamedRenderbufferParameteriv\0"
    "glGetQueryBufferObjecti64v\0"
    "glGetQueryBufferObjectiv\0"
    "glGetQueryBufferObjectui64v\0"
    "glGetQueryBufferObjectuiv\0"
    "glGetTextureImage\0"
    "glGetTextureLevelParameterfv\0"
    "glGetTextureLevelParameteriv\0"
    "glGetTextureParameterIiv\0"
    "glGetTextureParameterIuiv\0"
    "glGetTextureParameterfv\0"
    "glGetTextureParameteriv\0"
    "glGetTransformFeedbacki64_v\0"
    "glGetTransformFeedbacki_v\0"
    "glGetTransformFeedbackiv\0"
    "glGetVertexArrayIndexed64iv\0"
    "glGetVertexArrayIndexediv\0"
    "glGetVertexArrayiv\0"
    "glInvalidateNamedFramebufferData\0"
    "glInvalidateNamedFramebufferSubData\0"
    "glMapNamedBuffer\0"
    "glMapNamedBufferRange\0"
    "glNamedBufferData\0"
    "glNamedBufferStorage\0"
    "glNamedBufferSubData\0"
    "glNamedFramebufferDrawBuffer\0"
    "glNamedFramebufferDrawBuffers\0"
    "glNamedFramebufferParameteri\0"
    "glNamedFramebufferReadBuffer\0"
    "glNamedFramebufferRenderbuffer\0"
    "glNamedFramebufferTexture\0"
    "glNamedFramebufferTextureLayer\0"
    "glNamedRenderbufferStorage\0"
    "glNamedRenderbufferStorageMultisample\0"
    "glTextureBuffer\0"
    "glTextureBufferRange\0"
    "glTextureParameterIiv\0"
    "glTextureParameterIuiv\0"
    "glTextureParameterf\0"
    "glTextureParameterfv\0"
    "glTextureParameteri\0"
    "glTextureParameteriv\0"
    "glTextureStorage1D\0"
    "glTextureStorage2D\0"
    "glTextureStorage2DMultisample\0"
    "glTextureStorage3D\0"
    "glTextureStorage3DMultisample\0"
    "glTextureSubImage1D\0"
    "glTextureSubImage2D\0"
    "glTextureSubImage3D\0"
    "glTransformFeedbackBufferBase\0"
    "glTransformFeedbackBufferRange\0"
    "glUnmapNamedBufferEXT\0"
    "glVertexArrayAttribBinding\0"
    "glVertexArrayAttribFormat\0"
    "glVertexArrayAttribIFormat\0"
    "glVertexArrayAttribLFormat\0"
    "glVertexArrayBindingDivisor\0"
    "glVertexArrayElementBuffer\0"
    "glVertexArrayVertexBuffer\0"
    "glVertexArrayVertexBuffers\0"
    "glGetCompressedTextureSubImage\0"
    "glGetTextureSubImage\0"
    "glBufferPageCommitmentARB\0"
    "glNamedBufferPageCommitmentARB\0"
    "glGetUniformi64vARB\0"
    "glGetUniformui64vARB\0"
    "glGetnUniformi64vARB\0"
    "glGetnUniformui64vARB\0"
    "glProgramUniform1i64ARB\0"
    "glProgramUniform1i64vARB\0"
    "glProgramUniform1ui64ARB\0"
    "glProgramUniform1ui64vARB\0"
    "glProgramUniform2i64ARB\0"
    "glProgramUniform2i64vARB\0"
    "glProgramUniform2ui64ARB\0"
    "glProgramUniform2ui64vARB\0"
    "glProgramUniform3i64ARB\0"
    "glProgramUniform3i64vARB\0"
    "glProgramUniform3ui64ARB\0"
    "glProgramUniform3ui64vARB\0"
    "glProgramUniform4i64ARB\0"
    "glProgramUniform4i64vARB\0"
    "glProgramUniform4ui64ARB\0"
    "glProgramUniform4ui64vARB\0"
    "glUniform1i64ARB\0"
    "glUniform1i64vARB\0"
    "glUniform1ui64ARB\0"
    "glUniform1ui64vARB\0"
    "glUniform2i64ARB\0"
    "glUniform2i64vARB\0"
    "glUniform2ui64ARB\0"
    "glUniform2ui64vARB\0"
    "glUniform3i64ARB\0"
    "glUniform3i64vARB\0"
    "glUniform3ui64ARB\0"
    "glUniform3ui64vARB\0"
    "glUniform4i64ARB\0"
    "glUniform4i64vARB\0"
    "glUniform4ui64ARB\0"
    "glUniform4ui64vARB\0"
    "glEvaluateDepthValuesARB\0"
    "glFramebufferSampleLocationsfvARB\0"
    "glNamedFramebufferSampleLocationsfvARB\0"
    "glSpecializeShaderARB\0"
    "glInvalidateBufferData\0"
    "glInvalidateBufferSubData\0"
    "glInvalidateFramebuffer\0"
    "glInvalidateSubFramebuffer\0"
    "glInvalidateTexImage\0"
    "glInvalidateTexSubImage\0"
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
    "glDiscardFramebufferEXT\0"
    "glActiveShaderProgram\0"
    "glBindProgramPipeline\0"
    "glCreateShaderProgramv\0"
    "glDeleteProgramPipelines\0"
    "glGenProgramPipelines\0"
    "glGetProgramPipelineInfoLog\0"
    "glGetProgramPipelineiv\0"
    "glIsProgramPipeline\0"
    "glLockArraysEXT\0"
    "glProgramUniform1d\0"
    "glProgramUniform1dv\0"
    "glProgramUniform1f\0"
    "glProgramUniform1fv\0"
    "glProgramUniform1i\0"
    "glProgramUniform1iv\0"
    "glProgramUniform1ui\0"
    "glProgramUniform1uiv\0"
    "glProgramUniform2d\0"
    "glProgramUniform2dv\0"
    "glProgramUniform2f\0"
    "glProgramUniform2fv\0"
    "glProgramUniform2i\0"
    "glProgramUniform2iv\0"
    "glProgramUniform2ui\0"
    "glProgramUniform2uiv\0"
    "glProgramUniform3d\0"
    "glProgramUniform3dv\0"
    "glProgramUniform3f\0"
    "glProgramUniform3fv\0"
    "glProgramUniform3i\0"
    "glProgramUniform3iv\0"
    "glProgramUniform3ui\0"
    "glProgramUniform3uiv\0"
    "glProgramUniform4d\0"
    "glProgramUniform4dv\0"
    "glProgramUniform4f\0"
    "glProgramUniform4fv\0"
    "glProgramUniform4i\0"
    "glProgramUniform4iv\0"
    "glProgramUniform4ui\0"
    "glProgramUniform4uiv\0"
    "glProgramUniformMatrix2dv\0"
    "glProgramUniformMatrix2fv\0"
    "glProgramUniformMatrix2x3dv\0"
    "glProgramUniformMatrix2x3fv\0"
    "glProgramUniformMatrix2x4dv\0"
    "glProgramUniformMatrix2x4fv\0"
    "glProgramUniformMatrix3dv\0"
    "glProgramUniformMatrix3fv\0"
    "glProgramUniformMatrix3x2dv\0"
    "glProgramUniformMatrix3x2fv\0"
    "glProgramUniformMatrix3x4dv\0"
    "glProgramUniformMatrix3x4fv\0"
    "glProgramUniformMatrix4dv\0"
    "glProgramUniformMatrix4fv\0"
    "glProgramUniformMatrix4x2dv\0"
    "glProgramUniformMatrix4x2fv\0"
    "glProgramUniformMatrix4x3dv\0"
    "glProgramUniformMatrix4x3fv\0"
    "glUnlockArraysEXT\0"
    "glUseProgramStages\0"
    "glValidateProgramPipeline\0"
    "glFramebufferTexture2DMultisampleEXT\0"
    "glDebugMessageCallback\0"
    "glDebugMessageControl\0"
    "glDebugMessageInsert\0"
    "glGetDebugMessageLog\0"
    "glGetObjectLabel\0"
    "glGetObjectPtrLabel\0"
    "glObjectLabel\0"
    "glObjectPtrLabel\0"
    "glPopDebugGroup\0"
    "glPushDebugGroup\0"
    "glSecondaryColor3fEXT\0"
    "glSecondaryColor3fvEXT\0"
    "glMultiDrawElements\0"
    "glFogCoordfEXT\0"
    "glFogCoordfvEXT\0"
    "glResizeBuffersMESA\0"
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
    "glExecuteProgramNV\0"
    "glGetProgramParameterdvNV\0"
    "glGetProgramParameterfvNV\0"
    "glGetProgramStringNV\0"
    "glGetProgramivNV\0"
    "glGetTrackMatrixivNV\0"
    "glGetVertexAttribdvNV\0"
    "glGetVertexAttribfvNV\0"
    "glGetVertexAttribivNV\0"
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
    "glDepthRangeArrayfvOES\0"
    "glDepthRangeIndexedfOES\0"
    "glActiveStencilFaceEXT\0"
    "glGetProgramNamedParameterdvNV\0"
    "glGetProgramNamedParameterfvNV\0"
    "glProgramNamedParameter4dNV\0"
    "glProgramNamedParameter4dvNV\0"
    "glProgramNamedParameter4fNV\0"
    "glProgramNamedParameter4fvNV\0"
    "glPrimitiveRestartNV\0"
    "glGetTexGenxvOES\0"
    "glTexGenxOES\0"
    "glTexGenxvOES\0"
    "glDepthBoundsEXT\0"
    "glBindFramebufferEXT\0"
    "glBindRenderbufferEXT\0"
    "glStringMarkerGREMEDY\0"
    "glBufferParameteriAPPLE\0"
    "glFlushMappedBufferRangeAPPLE\0"
    "glVertexAttribI1iEXT\0"
    "glVertexAttribI1uiEXT\0"
    "glVertexAttribI2iEXT\0"
    "glVertexAttribI2ivEXT\0"
    "glVertexAttribI2uiEXT\0"
    "glVertexAttribI2uivEXT\0"
    "glVertexAttribI3iEXT\0"
    "glVertexAttribI3ivEXT\0"
    "glVertexAttribI3uiEXT\0"
    "glVertexAttribI3uivEXT\0"
    "glVertexAttribI4iEXT\0"
    "glVertexAttribI4ivEXT\0"
    "glVertexAttribI4uiEXT\0"
    "glVertexAttribI4uivEXT\0"
    "glClearColorIiEXT\0"
    "glClearColorIuiEXT\0"
    "glBindBufferOffsetEXT\0"
    "glBeginPerfMonitorAMD\0"
    "glDeletePerfMonitorsAMD\0"
    "glEndPerfMonitorAMD\0"
    "glGenPerfMonitorsAMD\0"
    "glGetPerfMonitorCounterDataAMD\0"
    "glGetPerfMonitorCounterInfoAMD\0"
    "glGetPerfMonitorCounterStringAMD\0"
    "glGetPerfMonitorCountersAMD\0"
    "glGetPerfMonitorGroupStringAMD\0"
    "glGetPerfMonitorGroupsAMD\0"
    "glSelectPerfMonitorCountersAMD\0"
    "glGetObjectParameterivAPPLE\0"
    "glObjectPurgeableAPPLE\0"
    "glObjectUnpurgeableAPPLE\0"
    "glActiveProgramEXT\0"
    "glCreateShaderProgramEXT\0"
    "glUseShaderProgramEXT\0"
    "glTextureBarrierNV\0"
    "glVDPAUFiniNV\0"
    "glVDPAUGetSurfaceivNV\0"
    "glVDPAUInitNV\0"
    "glVDPAUIsSurfaceNV\0"
    "glVDPAUMapSurfacesNV\0"
    "glVDPAURegisterOutputSurfaceNV\0"
    "glVDPAURegisterVideoSurfaceNV\0"
    "glVDPAUSurfaceAccessNV\0"
    "glVDPAUUnmapSurfacesNV\0"
    "glVDPAUUnregisterSurfaceNV\0"
    "glBeginPerfQueryINTEL\0"
    "glCreatePerfQueryINTEL\0"
    "glDeletePerfQueryINTEL\0"
    "glEndPerfQueryINTEL\0"
    "glGetFirstPerfQueryIdINTEL\0"
    "glGetNextPerfQueryIdINTEL\0"
    "glGetPerfCounterInfoINTEL\0"
    "glGetPerfQueryDataINTEL\0"
    "glGetPerfQueryIdByNameINTEL\0"
    "glGetPerfQueryInfoINTEL\0"
    "glPolygonOffsetClampEXT\0"
    "glSubpixelPrecisionBiasNV\0"
    "glConservativeRasterParameterfNV\0"
    "glConservativeRasterParameteriNV\0"
    "glWindowRectanglesEXT\0"
    "glBufferStorageMemEXT\0"
    "glCreateMemoryObjectsEXT\0"
    "glDeleteMemoryObjectsEXT\0"
    "glDeleteSemaphoresEXT\0"
    "glGenSemaphoresEXT\0"
    "glGetMemoryObjectParameterivEXT\0"
    "glGetSemaphoreParameterui64vEXT\0"
    "glGetUnsignedBytei_vEXT\0"
    "glGetUnsignedBytevEXT\0"
    "glIsMemoryObjectEXT\0"
    "glIsSemaphoreEXT\0"
    "glMemoryObjectParameterivEXT\0"
    "glNamedBufferStorageMemEXT\0"
    "glSemaphoreParameterui64vEXT\0"
    "glSignalSemaphoreEXT\0"
    "glTexStorageMem1DEXT\0"
    "glTexStorageMem2DEXT\0"
    "glTexStorageMem2DMultisampleEXT\0"
    "glTexStorageMem3DEXT\0"
    "glTexStorageMem3DMultisampleEXT\0"
    "glTextureStorageMem1DEXT\0"
    "glTextureStorageMem2DEXT\0"
    "glTextureStorageMem2DMultisampleEXT\0"
    "glTextureStorageMem3DEXT\0"
    "glTextureStorageMem3DMultisampleEXT\0"
    "glWaitSemaphoreEXT\0"
    "glImportMemoryFdEXT\0"
    "glImportSemaphoreFdEXT\0"
    "glFramebufferFetchBarrierEXT\0"
    "glNamedRenderbufferStorageMultisampleAdvancedAMD\0"
    "glRenderbufferStorageMultisampleAdvancedAMD\0"
    "glStencilFuncSeparateATI\0"
    "glProgramEnvParameters4fvEXT\0"
    "glProgramLocalParameters4fvEXT\0"
    "glEGLImageTargetRenderbufferStorageOES\0"
    "glEGLImageTargetTexture2DOES\0"
    "glAlphaFuncx\0"
    "glClearColorx\0"
    "glClearDepthx\0"
    "glColor4x\0"
    "glDepthRangex\0"
    "glFogx\0"
    "glFogxv\0"
    "glFrustumf\0"
    "glFrustumx\0"
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
    "glOrthof\0"
    "glOrthox\0"
    "glPointSizex\0"
    "glPolygonOffsetx\0"
    "glRotatex\0"
    "glSampleCoveragex\0"
    "glScalex\0"
    "glTexEnvx\0"
    "glTexEnvxv\0"
    "glTexParameterx\0"
    "glTranslatex\0"
    "glClipPlanef\0"
    "glClipPlanex\0"
    "glGetClipPlanef\0"
    "glGetClipPlanex\0"
    "glGetFixedv\0"
    "glGetLightxv\0"
    "glGetMaterialxv\0"
    "glGetTexEnvxv\0"
    "glGetTexParameterxv\0"
    "glPointParameterx\0"
    "glPointParameterxv\0"
    "glTexParameterxv\0"
    "glBlendBarrier\0"
    "glPrimitiveBoundingBox\0"
    "glMaxShaderCompilerThreadsKHR\0"
    "glMatrixLoadfEXT\0"
    "glMatrixLoaddEXT\0"
    "glMatrixMultfEXT\0"
    "glMatrixMultdEXT\0"
    "glMatrixLoadIdentityEXT\0"
    "glMatrixRotatefEXT\0"
    "glMatrixRotatedEXT\0"
    "glMatrixScalefEXT\0"
    "glMatrixScaledEXT\0"
    "glMatrixTranslatefEXT\0"
    "glMatrixTranslatedEXT\0"
    "glMatrixOrthoEXT\0"
    "glMatrixFrustumEXT\0"
    "glMatrixPushEXT\0"
    "glMatrixPopEXT\0"
    "glMatrixLoadTransposefEXT\0"
    "glMatrixLoadTransposedEXT\0"
    "glMatrixMultTransposefEXT\0"
    "glMatrixMultTransposedEXT\0"
    "glBindMultiTextureEXT\0"
    "glNamedBufferDataEXT\0"
    "glNamedBufferSubDataEXT\0"
    "glNamedBufferStorageEXT\0"
    "glMapNamedBufferRangeEXT\0"
    "glTextureImage1DEXT\0"
    "glTextureImage2DEXT\0"
    "glTextureImage3DEXT\0"
    "glTextureSubImage1DEXT\0"
    "glTextureSubImage2DEXT\0"
    "glTextureSubImage3DEXT\0"
    "glCopyTextureImage1DEXT\0"
    "glCopyTextureImage2DEXT\0"
    "glCopyTextureSubImage1DEXT\0"
    "glCopyTextureSubImage2DEXT\0"
    "glCopyTextureSubImage3DEXT\0"
    "glMapNamedBufferEXT\0"
    "glGetTextureParameterivEXT\0"
    "glGetTextureParameterfvEXT\0"
    "glTextureParameteriEXT\0"
    "glTextureParameterivEXT\0"
    "glTextureParameterfEXT\0"
    "glTextureParameterfvEXT\0"
    "glGetTextureImageEXT\0"
    "glGetTextureLevelParameterivEXT\0"
    "glGetTextureLevelParameterfvEXT\0"
    "glGetNamedBufferSubDataEXT\0"
    "glGetNamedBufferPointervEXT\0"
    "glGetNamedBufferParameterivEXT\0"
    "glFlushMappedNamedBufferRangeEXT\0"
    "glFramebufferDrawBufferEXT\0"
    "glFramebufferDrawBuffersEXT\0"
    "glFramebufferReadBufferEXT\0"
    "glGetFramebufferParameterivEXT\0"
    "glCheckNamedFramebufferStatusEXT\0"
    "glNamedFramebufferTexture1DEXT\0"
    "glNamedFramebufferTexture2DEXT\0"
    "glNamedFramebufferTexture3DEXT\0"
    "glNamedFramebufferRenderbufferEXT\0"
    "glGetNamedFramebufferAttachmentParameterivEXT\0"
    "glEnableClientStateiEXT\0"
    "glDisableClientStateiEXT\0"
    "glGetPointerIndexedvEXT\0"
    "glMultiTexEnviEXT\0"
    "glMultiTexEnvivEXT\0"
    "glMultiTexEnvfEXT\0"
    "glMultiTexEnvfvEXT\0"
    "glGetMultiTexEnvivEXT\0"
    "glGetMultiTexEnvfvEXT\0"
    "glMultiTexParameteriEXT\0"
    "glMultiTexParameterivEXT\0"
    "glMultiTexParameterfEXT\0"
    "glMultiTexParameterfvEXT\0"
    "glGetMultiTexImageEXT\0"
    "glMultiTexImage1DEXT\0"
    "glMultiTexImage2DEXT\0"
    "glMultiTexImage3DEXT\0"
    "glMultiTexSubImage1DEXT\0"
    "glMultiTexSubImage2DEXT\0"
    "glMultiTexSubImage3DEXT\0"
    "glGetMultiTexParameterivEXT\0"
    "glGetMultiTexParameterfvEXT\0"
    "glCopyMultiTexImage1DEXT\0"
    "glCopyMultiTexImage2DEXT\0"
    "glCopyMultiTexSubImage1DEXT\0"
    "glCopyMultiTexSubImage2DEXT\0"
    "glCopyMultiTexSubImage3DEXT\0"
    "glMultiTexGendEXT\0"
    "glMultiTexGendvEXT\0"
    "glMultiTexGenfEXT\0"
    "glMultiTexGenfvEXT\0"
    "glMultiTexGeniEXT\0"
    "glMultiTexGenivEXT\0"
    "glGetMultiTexGendvEXT\0"
    "glGetMultiTexGenfvEXT\0"
    "glGetMultiTexGenivEXT\0"
    "glMultiTexCoordPointerEXT\0"
    "glBindImageTextureEXT\0"
    "glCompressedTextureImage1DEXT\0"
    "glCompressedTextureImage2DEXT\0"
    "glCompressedTextureImage3DEXT\0"
    "glCompressedTextureSubImage1DEXT\0"
    "glCompressedTextureSubImage2DEXT\0"
    "glCompressedTextureSubImage3DEXT\0"
    "glGetCompressedTextureImageEXT\0"
    "glCompressedMultiTexImage1DEXT\0"
    "glCompressedMultiTexImage2DEXT\0"
    "glCompressedMultiTexImage3DEXT\0"
    "glCompressedMultiTexSubImage1DEXT\0"
    "glCompressedMultiTexSubImage2DEXT\0"
    "glCompressedMultiTexSubImage3DEXT\0"
    "glGetCompressedMultiTexImageEXT\0"
    "glGetMultiTexLevelParameterivEXT\0"
    "glGetMultiTexLevelParameterfvEXT\0"
    "glFramebufferParameteriMESA\0"
    "glGetFramebufferParameterivMESA\0"
    "glNamedRenderbufferStorageEXT\0"
    "glGetNamedRenderbufferParameterivEXT\0"
    "glClientAttribDefaultEXT\0"
    "glPushClientAttribDefaultEXT\0"
    "glNamedProgramStringEXT\0"
    "glGetNamedProgramStringEXT\0"
    "glNamedProgramLocalParameter4fEXT\0"
    "glNamedProgramLocalParameter4fvEXT\0"
    "glGetNamedProgramLocalParameterfvEXT\0"
    "glNamedProgramLocalParameter4dEXT\0"
    "glNamedProgramLocalParameter4dvEXT\0"
    "glGetNamedProgramLocalParameterdvEXT\0"
    "glGetNamedProgramivEXT\0"
    "glTextureBufferEXT\0"
    "glMultiTexBufferEXT\0"
    "glTextureParameterIivEXT\0"
    "glTextureParameterIuivEXT\0"
    "glGetTextureParameterIivEXT\0"
    "glGetTextureParameterIuivEXT\0"
    "glMultiTexParameterIivEXT\0"
    "glMultiTexParameterIuivEXT\0"
    "glGetMultiTexParameterIivEXT\0"
    "glGetMultiTexParameterIuivEXT\0"
    "glNamedProgramLocalParameters4fvEXT\0"
    "glGenerateTextureMipmapEXT\0"
    "glGenerateMultiTexMipmapEXT\0"
    "glNamedRenderbufferStorageMultisampleEXT\0"
    "glNamedCopyBufferSubDataEXT\0"
    "glVertexArrayVertexOffsetEXT\0"
    "glVertexArrayColorOffsetEXT\0"
    "glVertexArrayEdgeFlagOffsetEXT\0"
    "glVertexArrayIndexOffsetEXT\0"
    "glVertexArrayNormalOffsetEXT\0"
    "glVertexArrayTexCoordOffsetEXT\0"
    "glVertexArrayMultiTexCoordOffsetEXT\0"
    "glVertexArrayFogCoordOffsetEXT\0"
    "glVertexArraySecondaryColorOffsetEXT\0"
    "glVertexArrayVertexAttribOffsetEXT\0"
    "glVertexArrayVertexAttribIOffsetEXT\0"
    "glEnableVertexArrayEXT\0"
    "glDisableVertexArrayEXT\0"
    "glEnableVertexArrayAttribEXT\0"
    "glDisableVertexArrayAttribEXT\0"
    "glGetVertexArrayIntegervEXT\0"
    "glGetVertexArrayPointervEXT\0"
    "glGetVertexArrayIntegeri_vEXT\0"
    "glGetVertexArrayPointeri_vEXT\0"
    "glClearNamedBufferDataEXT\0"
    "glClearNamedBufferSubDataEXT\0"
    "glNamedFramebufferParameteriEXT\0"
    "glGetNamedFramebufferParameterivEXT\0"
    "glVertexArrayVertexAttribLOffsetEXT\0"
    "glVertexArrayVertexAttribDivisorEXT\0"
    "glTextureBufferRangeEXT\0"
    "glTextureStorage2DMultisampleEXT\0"
    "glTextureStorage3DMultisampleEXT\0"
    "glVertexArrayBindVertexBufferEXT\0"
    "glVertexArrayVertexAttribFormatEXT\0"
    "glVertexArrayVertexAttribIFormatEXT\0"
    "glVertexArrayVertexAttribLFormatEXT\0"
    "glVertexArrayVertexAttribBindingEXT\0"
    "glVertexArrayVertexBindingDivisorEXT\0"
    "glNamedBufferPageCommitmentEXT\0"
    "glNamedStringARB\0"
    "glDeleteNamedStringARB\0"
    "glCompileShaderIncludeARB\0"
    "glIsNamedStringARB\0"
    "glGetNamedStringARB\0"
    "glGetNamedStringivARB\0"
    "glEGLImageTargetTexStorageEXT\0"
    "glEGLImageTargetTextureStorageEXT\0"
    "glCopyImageSubDataNV\0"
    "glViewportSwizzleNV\0"
    "glAlphaToCoverageDitherControlNV\0"
    "glInternalBufferSubDataCopyMESA\0"
    "glVertex2hNV\0"
    "glVertex2hvNV\0"
    "glVertex3hNV\0"
    "glVertex3hvNV\0"
    "glVertex4hNV\0"
    "glVertex4hvNV\0"
    "glNormal3hNV\0"
    "glNormal3hvNV\0"
    "glColor3hNV\0"
    "glColor3hvNV\0"
    "glColor4hNV\0"
    "glColor4hvNV\0"
    "glTexCoord1hNV\0"
    "glTexCoord1hvNV\0"
    "glTexCoord2hNV\0"
    "glTexCoord2hvNV\0"
    "glTexCoord3hNV\0"
    "glTexCoord3hvNV\0"
    "glTexCoord4hNV\0"
    "glTexCoord4hvNV\0"
    "glMultiTexCoord1hNV\0"
    "glMultiTexCoord1hvNV\0"
    "glMultiTexCoord2hNV\0"
    "glMultiTexCoord2hvNV\0"
    "glMultiTexCoord3hNV\0"
    "glMultiTexCoord3hvNV\0"
    "glMultiTexCoord4hNV\0"
    "glMultiTexCoord4hvNV\0"
    "glFogCoordhNV\0"
    "glFogCoordhvNV\0"
    "glSecondaryColor3hNV\0"
    "glSecondaryColor3hvNV\0"
    "glInternalSetError\0"
    "glVertexAttrib1hNV\0"
    "glVertexAttrib1hvNV\0"
    "glVertexAttrib2hNV\0"
    "glVertexAttrib2hvNV\0"
    "glVertexAttrib3hNV\0"
    "glVertexAttrib3hvNV\0"
    "glVertexAttrib4hNV\0"
    "glVertexAttrib4hvNV\0"
    "glVertexAttribs1hvNV\0"
    "glVertexAttribs2hvNV\0"
    "glVertexAttribs3hvNV\0"
    "glVertexAttribs4hvNV\0"
    "glTexPageCommitmentARB\0"
    "glTexturePageCommitmentEXT\0"
    "glImportMemoryWin32HandleEXT\0"
    "glImportSemaphoreWin32HandleEXT\0"
    "glImportMemoryWin32NameEXT\0"
    "glImportSemaphoreWin32NameEXT\0"
    "glGetObjectLabelEXT\0"
    "glLabelObjectEXT\0"
    "glDrawArraysUserBuf\0"
    "glDrawElementsUserBuf\0"
    "glMultiDrawArraysUserBuf\0"
    "glMultiDrawElementsUserBuf\0"
    "glDrawArraysInstancedBaseInstanceDrawID\0"
    "glDrawElementsInstancedBaseVertexBaseInstanceDrawID\0"
    "glInternalInvalidateFramebufferAncillaryMESA\0"
    "glDrawElementsPacked\0"
    "glDrawElementsUserBufPacked\0"
    "glTexStorageAttribs2DEXT\0"
    "glTexStorageAttribs3DEXT\0"
    "glFramebufferTextureMultiviewOVR\0"
    "glNamedFramebufferTextureMultiviewOVR\0"
    "glFramebufferTextureMultisampleMultiviewOVR\0"
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
    "glGetPointervKHR\0"
    "glGetPointervEXT\0"
    "glIsTextureEXT\0"
    "glPrioritizeTexturesEXT\0"
    "glTexSubImage1DEXT\0"
    "glTexSubImage2DEXT\0"
    "glBlendColorEXT\0"
    "glBlendEquationEXT\0"
    "glBlendEquationOES\0"
    "glDrawRangeElementsEXT\0"
    "glColorSubTableEXT\0"
    "glCopyColorSubTableEXT\0"
    "glTexImage3DEXT\0"
    "glTexImage3DOES\0"
    "glTexSubImage3DEXT\0"
    "glTexSubImage3DOES\0"
    "glCopyTexSubImage3DEXT\0"
    "glCopyTexSubImage3DOES\0"
    "glActiveTextureARB\0"
    "glClientActiveTextureARB\0"
    "glMultiTexCoord1dARB\0"
    "glMultiTexCoord1dvARB\0"
    "glMultiTexCoord1f\0"
    "glMultiTexCoord1fv\0"
    "glMultiTexCoord1iARB\0"
    "glMultiTexCoord1ivARB\0"
    "glMultiTexCoord1sARB\0"
    "glMultiTexCoord1svARB\0"
    "glMultiTexCoord2dARB\0"
    "glMultiTexCoord2dvARB\0"
    "glMultiTexCoord2f\0"
    "glMultiTexCoord2fv\0"
    "glMultiTexCoord2iARB\0"
    "glMultiTexCoord2ivARB\0"
    "glMultiTexCoord2sARB\0"
    "glMultiTexCoord2svARB\0"
    "glMultiTexCoord3dARB\0"
    "glMultiTexCoord3dvARB\0"
    "glMultiTexCoord3f\0"
    "glMultiTexCoord3fv\0"
    "glMultiTexCoord3iARB\0"
    "glMultiTexCoord3ivARB\0"
    "glMultiTexCoord3sARB\0"
    "glMultiTexCoord3svARB\0"
    "glMultiTexCoord4dARB\0"
    "glMultiTexCoord4dvARB\0"
    "glMultiTexCoord4f\0"
    "glMultiTexCoord4fv\0"
    "glMultiTexCoord4iARB\0"
    "glMultiTexCoord4ivARB\0"
    "glMultiTexCoord4sARB\0"
    "glMultiTexCoord4svARB\0"
    "glCompressedTexImage1DARB\0"
    "glCompressedTexImage2DARB\0"
    "glCompressedTexImage3DARB\0"
    "glCompressedTexImage3DOES\0"
    "glCompressedTexSubImage1DARB\0"
    "glCompressedTexSubImage2DARB\0"
    "glCompressedTexSubImage3DARB\0"
    "glCompressedTexSubImage3DOES\0"
    "glGetCompressedTexImageARB\0"
    "glLoadTransposeMatrixdARB\0"
    "glLoadTransposeMatrixfARB\0"
    "glMultTransposeMatrixdARB\0"
    "glMultTransposeMatrixfARB\0"
    "glSampleCoverageARB\0"
    "glBlendFuncSeparateEXT\0"
    "glBlendFuncSeparateINGR\0"
    "glBlendFuncSeparateOES\0"
    "glFogCoordPointerEXT\0"
    "glFogCoorddEXT\0"
    "glFogCoorddvEXT\0"
    "glMultiDrawArraysEXT\0"
    "glPointParameterfARB\0"
    "glPointParameterfEXT\0"
    "glPointParameterfSGIS\0"
    "glPointParameterfvARB\0"
    "glPointParameterfvEXT\0"
    "glPointParameterfvSGIS\0"
    "glPointParameteriNV\0"
    "glPointParameterivNV\0"
    "glSecondaryColor3bEXT\0"
    "glSecondaryColor3bvEXT\0"
    "glSecondaryColor3dEXT\0"
    "glSecondaryColor3dvEXT\0"
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
    "glWindowPos2dARB\0"
    "glWindowPos2dMESA\0"
    "glWindowPos2dvARB\0"
    "glWindowPos2dvMESA\0"
    "glWindowPos2fARB\0"
    "glWindowPos2fMESA\0"
    "glWindowPos2fvARB\0"
    "glWindowPos2fvMESA\0"
    "glWindowPos2iARB\0"
    "glWindowPos2iMESA\0"
    "glWindowPos2ivARB\0"
    "glWindowPos2ivMESA\0"
    "glWindowPos2sARB\0"
    "glWindowPos2sMESA\0"
    "glWindowPos2svARB\0"
    "glWindowPos2svMESA\0"
    "glWindowPos3dARB\0"
    "glWindowPos3dMESA\0"
    "glWindowPos3dvARB\0"
    "glWindowPos3dvMESA\0"
    "glWindowPos3fARB\0"
    "glWindowPos3fMESA\0"
    "glWindowPos3fvARB\0"
    "glWindowPos3fvMESA\0"
    "glWindowPos3iARB\0"
    "glWindowPos3iMESA\0"
    "glWindowPos3ivARB\0"
    "glWindowPos3ivMESA\0"
    "glWindowPos3sARB\0"
    "glWindowPos3sMESA\0"
    "glWindowPos3svARB\0"
    "glWindowPos3svMESA\0"
    "glBeginQueryARB\0"
    "glBeginQueryEXT\0"
    "glBindBufferARB\0"
    "glBufferDataARB\0"
    "glBufferSubDataARB\0"
    "glDeleteBuffersARB\0"
    "glDeleteQueriesARB\0"
    "glDeleteQueriesEXT\0"
    "glEndQueryARB\0"
    "glEndQueryEXT\0"
    "glGenBuffersARB\0"
    "glGenQueriesARB\0"
    "glGenQueriesEXT\0"
    "glGetBufferParameterivARB\0"
    "glGetBufferPointervARB\0"
    "glGetBufferPointervOES\0"
    "glGetBufferSubDataARB\0"
    "glGetQueryObjectivARB\0"
    "glGetQueryObjectivEXT\0"
    "glGetQueryObjectuivARB\0"
    "glGetQueryObjectuivEXT\0"
    "glGetQueryivARB\0"
    "glGetQueryivEXT\0"
    "glIsBufferARB\0"
    "glIsQueryARB\0"
    "glIsQueryEXT\0"
    "glMapBufferARB\0"
    "glMapBufferOES\0"
    "glUnmapBufferARB\0"
    "glUnmapBufferOES\0"
    "glBindAttribLocationARB\0"
    "glBlendEquationSeparateEXT\0"
    "glBlendEquationSeparateATI\0"
    "glBlendEquationSeparateOES\0"
    "glCompileShaderARB\0"
    "glDisableVertexAttribArrayARB\0"
    "glDrawBuffersARB\0"
    "glDrawBuffersATI\0"
    "glDrawBuffersNV\0"
    "glDrawBuffersEXT\0"
    "glEnableVertexAttribArrayARB\0"
    "glGetActiveAttribARB\0"
    "glGetActiveUniformARB\0"
    "glGetAttribLocationARB\0"
    "glGetShaderSourceARB\0"
    "glGetUniformLocationARB\0"
    "glGetUniformfvARB\0"
    "glGetUniformivARB\0"
    "glGetVertexAttribPointervARB\0"
    "glGetVertexAttribPointervNV\0"
    "glGetVertexAttribdvARB\0"
    "glGetVertexAttribfvARB\0"
    "glGetVertexAttribivARB\0"
    "glLinkProgramARB\0"
    "glShaderSourceARB\0"
    "glStencilOpSeparateATI\0"
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
    "glVertexAttrib1dARB\0"
    "glVertexAttrib1dvARB\0"
    "glVertexAttrib1sARB\0"
    "glVertexAttrib1svARB\0"
    "glVertexAttrib2dARB\0"
    "glVertexAttrib2dvARB\0"
    "glVertexAttrib2sARB\0"
    "glVertexAttrib2svARB\0"
    "glVertexAttrib3dARB\0"
    "glVertexAttrib3dvARB\0"
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
    "glVertexAttrib4ivARB\0"
    "glVertexAttrib4sARB\0"
    "glVertexAttrib4svARB\0"
    "glVertexAttrib4ubvARB\0"
    "glVertexAttrib4uivARB\0"
    "glVertexAttrib4usvARB\0"
    "glVertexAttribPointerARB\0"
    "glBeginConditionalRenderNV\0"
    "glBeginTransformFeedbackEXT\0"
    "glBindBufferBaseEXT\0"
    "glBindBufferRangeEXT\0"
    "glBindFragDataLocationEXT\0"
    "glClampColorARB\0"
    "glColorMaskIndexedEXT\0"
    "glColorMaskiEXT\0"
    "glColorMaskiOES\0"
    "glDisableIndexedEXT\0"
    "glDisableiEXT\0"
    "glDisableiOES\0"
    "glEnableIndexedEXT\0"
    "glEnableiEXT\0"
    "glEnableiOES\0"
    "glEndConditionalRenderNV\0"
    "glEndTransformFeedbackEXT\0"
    "glGetBooleanIndexedvEXT\0"
    "glGetFragDataLocationEXT\0"
    "glGetIntegerIndexedvEXT\0"
    "glGetTexParameterIivEXT\0"
    "glGetTexParameterIivOES\0"
    "glGetTexParameterIuivEXT\0"
    "glGetTexParameterIuivOES\0"
    "glGetTransformFeedbackVaryingEXT\0"
    "glGetUniformuivEXT\0"
    "glGetVertexAttribIivEXT\0"
    "glGetVertexAttribIuivEXT\0"
    "glIsEnabledIndexedEXT\0"
    "glIsEnablediEXT\0"
    "glIsEnablediOES\0"
    "glTexParameterIivEXT\0"
    "glTexParameterIivOES\0"
    "glTexParameterIuivEXT\0"
    "glTexParameterIuivOES\0"
    "glTransformFeedbackVaryingsEXT\0"
    "glUniform1uiEXT\0"
    "glUniform1uivEXT\0"
    "glUniform2uiEXT\0"
    "glUniform2uivEXT\0"
    "glUniform3uiEXT\0"
    "glUniform3uivEXT\0"
    "glUniform4uiEXT\0"
    "glUniform4uivEXT\0"
    "glVertexAttribI1ivEXT\0"
    "glVertexAttribI1uivEXT\0"
    "glVertexAttribI4bvEXT\0"
    "glVertexAttribI4svEXT\0"
    "glVertexAttribI4ubvEXT\0"
    "glVertexAttribI4usvEXT\0"
    "glVertexAttribIPointerEXT\0"
    "glPrimitiveRestartIndexNV\0"
    "glTexBufferARB\0"
    "glTexBufferEXT\0"
    "glTexBufferOES\0"
    "glFramebufferTextureEXT\0"
    "glFramebufferTextureOES\0"
    "glVertexAttribDivisorARB\0"
    "glVertexAttribDivisorEXT\0"
    "glMinSampleShadingARB\0"
    "glMinSampleShadingOES\0"
    "glBindProgramNV\0"
    "glDeleteProgramsNV\0"
    "glGenProgramsNV\0"
    "glIsProgramNV\0"
    "glProgramParameter4dNV\0"
    "glProgramParameter4dvNV\0"
    "glProgramParameter4fNV\0"
    "glProgramParameter4fvNV\0"
    "glVertexAttrib1f\0"
    "glVertexAttrib1fv\0"
    "glVertexAttrib2f\0"
    "glVertexAttrib2fv\0"
    "glVertexAttrib3f\0"
    "glVertexAttrib3fv\0"
    "glVertexAttrib4f\0"
    "glVertexAttrib4fv\0"
    "glDrawArraysInstancedEXT\0"
    "glDrawArraysInstancedARB\0"
    "glDrawElementsInstancedEXT\0"
    "glDrawElementsInstancedARB\0"
    "glBindFramebufferOES\0"
    "glBindRenderbufferOES\0"
    "glBlitFramebufferEXT\0"
    "glCheckFramebufferStatusEXT\0"
    "glCheckFramebufferStatusOES\0"
    "glDeleteFramebuffersEXT\0"
    "glDeleteFramebuffersOES\0"
    "glDeleteRenderbuffersEXT\0"
    "glDeleteRenderbuffersOES\0"
    "glFramebufferRenderbufferEXT\0"
    "glFramebufferRenderbufferOES\0"
    "glFramebufferTexture1DEXT\0"
    "glFramebufferTexture2DEXT\0"
    "glFramebufferTexture2DOES\0"
    "glFramebufferTexture3DEXT\0"
    "glFramebufferTexture3DOES\0"
    "glFramebufferTextureLayerEXT\0"
    "glGenFramebuffersEXT\0"
    "glGenFramebuffersOES\0"
    "glGenRenderbuffersEXT\0"
    "glGenRenderbuffersOES\0"
    "glGenerateMipmapEXT\0"
    "glGenerateMipmapOES\0"
    "glGetFramebufferAttachmentParameterivEXT\0"
    "glGetFramebufferAttachmentParameterivOES\0"
    "glGetRenderbufferParameterivEXT\0"
    "glGetRenderbufferParameterivOES\0"
    "glIsFramebufferEXT\0"
    "glIsFramebufferOES\0"
    "glIsRenderbufferEXT\0"
    "glIsRenderbufferOES\0"
    "glRenderbufferStorageEXT\0"
    "glRenderbufferStorageOES\0"
    "glRenderbufferStorageMultisampleEXT\0"
    "glFlushMappedBufferRangeEXT\0"
    "glMapBufferRangeEXT\0"
    "glBindVertexArrayOES\0"
    "glDeleteVertexArraysOES\0"
    "glGenVertexArraysOES\0"
    "glIsVertexArrayOES\0"
    "glClientWaitSyncAPPLE\0"
    "glDeleteSyncAPPLE\0"
    "glFenceSyncAPPLE\0"
    "glGetInteger64vAPPLE\0"
    "glGetInteger64vEXT\0"
    "glGetSyncivAPPLE\0"
    "glIsSyncAPPLE\0"
    "glWaitSyncAPPLE\0"
    "glDrawElementsBaseVertexEXT\0"
    "glDrawElementsBaseVertexOES\0"
    "glDrawElementsInstancedBaseVertexEXT\0"
    "glDrawElementsInstancedBaseVertexOES\0"
    "glInternalDrawRangeElementsBaseVertex\0"
    "glDrawRangeElementsBaseVertexEXT\0"
    "glDrawRangeElementsBaseVertexOES\0"
    "glInternalMultiDrawElementsBaseVertex\0"
    "glMultiDrawElementsBaseVertexEXT\0"
    "glProvokingVertexEXT\0"
    "glBlendEquationSeparateIndexedAMD\0"
    "glBlendEquationSeparatei\0"
    "glBlendEquationSeparateiEXT\0"
    "glBlendEquationSeparateiOES\0"
    "glBlendEquationIndexedAMD\0"
    "glBlendEquationi\0"
    "glBlendEquationiEXT\0"
    "glBlendEquationiOES\0"
    "glBlendFuncSeparateIndexedAMD\0"
    "glBlendFuncSeparatei\0"
    "glBlendFuncSeparateiEXT\0"
    "glBlendFuncSeparateiOES\0"
    "glBlendFuncIndexedAMD\0"
    "glBlendFunci\0"
    "glBlendFunciEXT\0"
    "glBlendFunciOES\0"
    "glBindFragDataLocationIndexedEXT\0"
    "glGetFragDataIndexEXT\0"
    "glGetSamplerParameterIivEXT\0"
    "glGetSamplerParameterIivOES\0"
    "glGetSamplerParameterIuivEXT\0"
    "glGetSamplerParameterIuivOES\0"
    "glSamplerParameterIivEXT\0"
    "glSamplerParameterIivOES\0"
    "glSamplerParameterIuivEXT\0"
    "glSamplerParameterIuivOES\0"
    "glGetQueryObjecti64vEXT\0"
    "glGetQueryObjectui64vEXT\0"
    "glQueryCounterEXT\0"
    "glPatchParameteriEXT\0"
    "glPatchParameteriOES\0"
    "glClearDepthfOES\0"
    "glDepthRangefOES\0"
    "glGetProgramBinaryOES\0"
    "glProgramBinaryOES\0"
    "glProgramParameteriEXT\0"
    "glGetVertexAttribLdvEXT\0"
    "glVertexAttribL1dEXT\0"
    "glVertexAttribL1dvEXT\0"
    "glVertexAttribL2dEXT\0"
    "glVertexAttribL2dvEXT\0"
    "glVertexAttribL3dEXT\0"
    "glVertexAttribL3dvEXT\0"
    "glVertexAttribL4dEXT\0"
    "glVertexAttribL4dvEXT\0"
    "glVertexAttribLPointerEXT\0"
    "glGetDoubleIndexedvEXT\0"
    "glGetDoublei_vEXT\0"
    "glGetFloatIndexedvEXT\0"
    "glGetFloati_vEXT\0"
    "glGetFloati_vOES\0"
    "glScissorArrayvOES\0"
    "glScissorIndexedOES\0"
    "glScissorIndexedvOES\0"
    "glViewportArrayvOES\0"
    "glViewportIndexedfOES\0"
    "glViewportIndexedfvOES\0"
    "glGetGraphicsResetStatus\0"
    "glGetGraphicsResetStatusKHR\0"
    "glGetGraphicsResetStatusEXT\0"
    "glGetnUniformfv\0"
    "glGetnUniformfvKHR\0"
    "glGetnUniformfvEXT\0"
    "glGetnUniformiv\0"
    "glGetnUniformivKHR\0"
    "glGetnUniformivEXT\0"
    "glGetnUniformuiv\0"
    "glGetnUniformuivKHR\0"
    "glReadnPixels\0"
    "glReadnPixelsKHR\0"
    "glReadnPixelsEXT\0"
    "glInternalDrawArraysInstancedBaseInstance\0"
    "glDrawArraysInstancedBaseInstanceEXT\0"
    "glDrawElementsInstancedBaseInstanceEXT\0"
    "glInternalDrawElementsInstancedBaseVertexBaseInstance\0"
    "glDrawElementsInstancedBaseVertexBaseInstanceEXT\0"
    "glMemoryBarrierEXT\0"
    "glTexStorage1DEXT\0"
    "glTexStorage2DEXT\0"
    "glTexStorage3DEXT\0"
    "glCopyImageSubDataEXT\0"
    "glCopyImageSubDataOES\0"
    "glTextureViewOES\0"
    "glTextureViewEXT\0"
    "glMultiDrawArraysIndirectEXT\0"
    "glMultiDrawArraysIndirectAMD\0"
    "glMultiDrawElementsIndirectEXT\0"
    "glMultiDrawElementsIndirectAMD\0"
    "glGetProgramResourceLocationIndexEXT\0"
    "glTexBufferRangeEXT\0"
    "glTexBufferRangeOES\0"
    "glTexStorage3DMultisampleOES\0"
    "glBufferStorageEXT\0"
    "glClearTexImageEXT\0"
    "glClearTexSubImageEXT\0"
    "glMultiDrawArraysIndirectCount\0"
    "glMultiDrawElementsIndirectCount\0"
    "glClipControlEXT\0"
    "glUnmapNamedBuffer\0"
    "glGetUniformi64vNV\0"
    "glGetUniformui64vNV\0"
    "glProgramUniform1i64NV\0"
    "glProgramUniform1i64vNV\0"
    "glProgramUniform1ui64NV\0"
    "glProgramUniform1ui64vNV\0"
    "glProgramUniform2i64NV\0"
    "glProgramUniform2i64vNV\0"
    "glProgramUniform2ui64NV\0"
    "glProgramUniform2ui64vNV\0"
    "glProgramUniform3i64NV\0"
    "glProgramUniform3i64vNV\0"
    "glProgramUniform3ui64NV\0"
    "glProgramUniform3ui64vNV\0"
    "glProgramUniform4i64NV\0"
    "glProgramUniform4i64vNV\0"
    "glProgramUniform4ui64NV\0"
    "glProgramUniform4ui64vNV\0"
    "glUniform1i64NV\0"
    "glUniform1i64vNV\0"
    "glUniform1ui64NV\0"
    "glUniform1ui64vNV\0"
    "glUniform2i64NV\0"
    "glUniform2i64vNV\0"
    "glUniform2ui64NV\0"
    "glUniform2ui64vNV\0"
    "glUniform3i64NV\0"
    "glUniform3i64vNV\0"
    "glUniform3ui64NV\0"
    "glUniform3ui64vNV\0"
    "glUniform4i64NV\0"
    "glUniform4i64vNV\0"
    "glUniform4ui64NV\0"
    "glUniform4ui64vNV\0"
    "glResolveDepthValuesNV\0"
    "glFramebufferSampleLocationsfvNV\0"
    "glNamedFramebufferSampleLocationsfvNV\0"
    "glSpecializeShader\0"
    "glSampleMaskEXT\0"
    "glSamplePatternEXT\0"
    "glActiveShaderProgramEXT\0"
    "glBindProgramPipelineEXT\0"
    "glCreateShaderProgramvEXT\0"
    "glDeleteProgramPipelinesEXT\0"
    "glGenProgramPipelinesEXT\0"
    "glGetProgramPipelineInfoLogEXT\0"
    "glGetProgramPipelineivEXT\0"
    "glIsProgramPipelineEXT\0"
    "glProgramUniform1dEXT\0"
    "glProgramUniform1dvEXT\0"
    "glProgramUniform1fEXT\0"
    "glProgramUniform1fvEXT\0"
    "glProgramUniform1iEXT\0"
    "glProgramUniform1ivEXT\0"
    "glProgramUniform1uiEXT\0"
    "glProgramUniform1uivEXT\0"
    "glProgramUniform2dEXT\0"
    "glProgramUniform2dvEXT\0"
    "glProgramUniform2fEXT\0"
    "glProgramUniform2fvEXT\0"
    "glProgramUniform2iEXT\0"
    "glProgramUniform2ivEXT\0"
    "glProgramUniform2uiEXT\0"
    "glProgramUniform2uivEXT\0"
    "glProgramUniform3dEXT\0"
    "glProgramUniform3dvEXT\0"
    "glProgramUniform3fEXT\0"
    "glProgramUniform3fvEXT\0"
    "glProgramUniform3iEXT\0"
    "glProgramUniform3ivEXT\0"
    "glProgramUniform3uiEXT\0"
    "glProgramUniform3uivEXT\0"
    "glProgramUniform4dEXT\0"
    "glProgramUniform4dvEXT\0"
    "glProgramUniform4fEXT\0"
    "glProgramUniform4fvEXT\0"
    "glProgramUniform4iEXT\0"
    "glProgramUniform4ivEXT\0"
    "glProgramUniform4uiEXT\0"
    "glProgramUniform4uivEXT\0"
    "glProgramUniformMatrix2dvEXT\0"
    "glProgramUniformMatrix2fvEXT\0"
    "glProgramUniformMatrix2x3dvEXT\0"
    "glProgramUniformMatrix2x3fvEXT\0"
    "glProgramUniformMatrix2x4dvEXT\0"
    "glProgramUniformMatrix2x4fvEXT\0"
    "glProgramUniformMatrix3dvEXT\0"
    "glProgramUniformMatrix3fvEXT\0"
    "glProgramUniformMatrix3x2dvEXT\0"
    "glProgramUniformMatrix3x2fvEXT\0"
    "glProgramUniformMatrix3x4dvEXT\0"
    "glProgramUniformMatrix3x4fvEXT\0"
    "glProgramUniformMatrix4dvEXT\0"
    "glProgramUniformMatrix4fvEXT\0"
    "glProgramUniformMatrix4x2dvEXT\0"
    "glProgramUniformMatrix4x2fvEXT\0"
    "glProgramUniformMatrix4x3dvEXT\0"
    "glProgramUniformMatrix4x3fvEXT\0"
    "glUseProgramStagesEXT\0"
    "glValidateProgramPipelineEXT\0"
    "glDebugMessageCallbackARB\0"
    "glDebugMessageCallbackKHR\0"
    "glDebugMessageControlARB\0"
    "glDebugMessageControlKHR\0"
    "glDebugMessageInsertARB\0"
    "glDebugMessageInsertKHR\0"
    "glGetDebugMessageLogARB\0"
    "glGetDebugMessageLogKHR\0"
    "glGetObjectLabelKHR\0"
    "glGetObjectPtrLabelKHR\0"
    "glObjectLabelKHR\0"
    "glObjectPtrLabelKHR\0"
    "glPopDebugGroupKHR\0"
    "glPushDebugGroupKHR\0"
    "glSecondaryColor3f\0"
    "glSecondaryColor3fv\0"
    "glMultiDrawElementsEXT\0"
    "glFogCoordf\0"
    "glFogCoordfv\0"
    "glVertexAttribI1i\0"
    "glVertexAttribI1ui\0"
    "glVertexAttribI2i\0"
    "glVertexAttribI2iv\0"
    "glVertexAttribI2ui\0"
    "glVertexAttribI2uiv\0"
    "glVertexAttribI3i\0"
    "glVertexAttribI3iv\0"
    "glVertexAttribI3ui\0"
    "glVertexAttribI3uiv\0"
    "glVertexAttribI4i\0"
    "glVertexAttribI4iv\0"
    "glVertexAttribI4ui\0"
    "glVertexAttribI4uiv\0"
    "glTextureBarrier\0"
    "glPolygonOffsetClamp\0"
    "glAlphaFuncxOES\0"
    "glClearColorxOES\0"
    "glClearDepthxOES\0"
    "glColor4xOES\0"
    "glDepthRangexOES\0"
    "glFogxOES\0"
    "glFogxvOES\0"
    "glFrustumfOES\0"
    "glFrustumxOES\0"
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
    "glOrthofOES\0"
    "glOrthoxOES\0"
    "glPointSizexOES\0"
    "glPolygonOffsetxOES\0"
    "glRotatexOES\0"
    "glSampleCoveragexOES\0"
    "glScalexOES\0"
    "glTexEnvxOES\0"
    "glTexEnvxvOES\0"
    "glTexParameterxOES\0"
    "glTranslatexOES\0"
    "glClipPlanefOES\0"
    "glClipPlanexOES\0"
    "glGetClipPlanefOES\0"
    "glGetClipPlanexOES\0"
    "glGetFixedvOES\0"
    "glGetLightxvOES\0"
    "glGetMaterialxvOES\0"
    "glGetTexEnvxvOES\0"
    "glGetTexParameterxvOES\0"
    "glPointParameterxOES\0"
    "glPointParameterxvOES\0"
    "glTexParameterxvOES\0"
    "glBlendBarrierKHR\0"
    "glPrimitiveBoundingBoxARB\0"
    "glPrimitiveBoundingBoxEXT\0"
    "glPrimitiveBoundingBoxOES\0"
    "glMaxShaderCompilerThreadsARB\0"
    "glEnableClientStateIndexedEXT\0"
    "glDisableClientStateIndexedEXT\0"
    "glGetPointeri_vEXT\0"
    ;


#if defined(NEED_FUNCTION_POINTER) || defined(GLX_INDIRECT_RENDERING)
void GLAPIENTRY gl_dispatch_stub_731(GLuint id, GLenum pname, GLint64 *params);
void GLAPIENTRY gl_dispatch_stub_732(GLuint id, GLenum pname, GLuint64 *params);
void GLAPIENTRY gl_dispatch_stub_733(GLuint id, GLenum target);
void GLAPIENTRY gl_dispatch_stub_774(GLuint program, GLint location, GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_775(GLint location, GLdouble x);
void GLAPIENTRY gl_dispatch_stub_776(GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_777(GLint location, GLdouble x, GLdouble y);
void GLAPIENTRY gl_dispatch_stub_778(GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_779(GLint location, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_780(GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_781(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_782(GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_783(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_784(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_785(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_786(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_787(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_788(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_789(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_790(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_791(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_792(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name);
void GLAPIENTRY gl_dispatch_stub_793(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name);
void GLAPIENTRY gl_dispatch_stub_794(GLuint program, GLenum shadertype, GLuint index, GLenum pname, GLint *values);
void GLAPIENTRY gl_dispatch_stub_795(GLuint program, GLenum shadertype, GLenum pname, GLint *values);
GLuint GLAPIENTRY gl_dispatch_stub_796(GLuint program, GLenum shadertype, const GLchar *name);
GLint GLAPIENTRY gl_dispatch_stub_797(GLuint program, GLenum shadertype, const GLchar *name);
void GLAPIENTRY gl_dispatch_stub_798(GLenum shadertype, GLint location, GLuint *params);
void GLAPIENTRY gl_dispatch_stub_799(GLenum shadertype, GLsizei count, const GLuint *indices);
void GLAPIENTRY gl_dispatch_stub_800(GLenum pname, const GLfloat *values);
void GLAPIENTRY gl_dispatch_stub_821(GLuint index, GLenum pname, GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_822(GLuint index, GLdouble x);
void GLAPIENTRY gl_dispatch_stub_823(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_824(GLuint index, GLdouble x, GLdouble y);
void GLAPIENTRY gl_dispatch_stub_825(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_826(GLuint index, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_827(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_828(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_829(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_830(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GLAPIENTRY gl_dispatch_stub_866(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params);
void GLAPIENTRY gl_dispatch_stub_890(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint64 *params);
GLint GLAPIENTRY gl_dispatch_stub_896(GLuint program, GLenum programInterface, const GLchar *name);
void GLAPIENTRY gl_dispatch_stub_899(GLuint program, GLuint shaderStorageBlockIndex, GLuint shaderStorageBlockBinding);
GLuint64 GLAPIENTRY gl_dispatch_stub_912(GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum format);
GLuint64 GLAPIENTRY gl_dispatch_stub_913(GLuint texture);
GLuint64 GLAPIENTRY gl_dispatch_stub_914(GLuint texture, GLuint sampler);
void GLAPIENTRY gl_dispatch_stub_915(GLuint index, GLenum pname, GLuint64EXT *params);
GLboolean GLAPIENTRY gl_dispatch_stub_916(GLuint64 handle);
GLboolean GLAPIENTRY gl_dispatch_stub_917(GLuint64 handle);
void GLAPIENTRY gl_dispatch_stub_918(GLuint64 handle);
void GLAPIENTRY gl_dispatch_stub_919(GLuint64 handle, GLenum access);
void GLAPIENTRY gl_dispatch_stub_920(GLuint64 handle);
void GLAPIENTRY gl_dispatch_stub_921(GLuint64 handle);
void GLAPIENTRY gl_dispatch_stub_922(GLuint program, GLint location, GLuint64 value);
void GLAPIENTRY gl_dispatch_stub_923(GLuint program, GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_924(GLint location, GLuint64 value);
void GLAPIENTRY gl_dispatch_stub_925(GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_926(GLuint index, GLuint64EXT x);
void GLAPIENTRY gl_dispatch_stub_927(GLuint index, const GLuint64EXT *v);
void GLAPIENTRY gl_dispatch_stub_928(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z, GLuint group_size_x, GLuint group_size_y, GLuint group_size_z);
void GLAPIENTRY gl_dispatch_stub_929(GLenum mode, GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride);
void GLAPIENTRY gl_dispatch_stub_930(GLenum mode, GLenum type, GLintptr indirect, GLintptr drawcount, GLsizei maxdrawcount, GLsizei stride);
void GLAPIENTRY gl_dispatch_stub_931(GLenum origin, GLenum depth);
void GLAPIENTRY gl_dispatch_stub_932(GLuint unit, GLuint texture);
void GLAPIENTRY gl_dispatch_stub_933(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
GLenum GLAPIENTRY gl_dispatch_stub_934(GLuint framebuffer, GLenum target);
void GLAPIENTRY gl_dispatch_stub_935(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_936(GLuint buffer, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_937(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
void GLAPIENTRY gl_dispatch_stub_938(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value);
void GLAPIENTRY gl_dispatch_stub_939(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value);
void GLAPIENTRY gl_dispatch_stub_940(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint *value);
void GLAPIENTRY gl_dispatch_stub_941(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_942(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_943(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_944(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_945(GLuint texture, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void GLAPIENTRY gl_dispatch_stub_946(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_947(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_948(GLsizei n, GLuint *buffers);
void GLAPIENTRY gl_dispatch_stub_949(GLsizei n, GLuint *framebuffers);
void GLAPIENTRY gl_dispatch_stub_950(GLsizei n, GLuint *pipelines);
void GLAPIENTRY gl_dispatch_stub_951(GLenum target, GLsizei n, GLuint *ids);
void GLAPIENTRY gl_dispatch_stub_952(GLsizei n, GLuint *renderbuffers);
void GLAPIENTRY gl_dispatch_stub_953(GLsizei n, GLuint *samplers);
void GLAPIENTRY gl_dispatch_stub_954(GLenum target, GLsizei n, GLuint *textures);
void GLAPIENTRY gl_dispatch_stub_955(GLsizei n, GLuint *ids);
void GLAPIENTRY gl_dispatch_stub_956(GLsizei n, GLuint *arrays);
void GLAPIENTRY gl_dispatch_stub_957(GLuint vaobj, GLuint index);
void GLAPIENTRY gl_dispatch_stub_958(GLuint vaobj, GLuint index);
void GLAPIENTRY gl_dispatch_stub_959(GLuint buffer, GLintptr offset, GLsizeiptr length);
void GLAPIENTRY gl_dispatch_stub_960(GLuint texture);
void GLAPIENTRY gl_dispatch_stub_961(GLuint texture, GLint level, GLsizei bufSize, GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_962(GLuint buffer, GLenum pname, GLint64 *params);
void GLAPIENTRY gl_dispatch_stub_963(GLuint buffer, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_964(GLuint buffer, GLenum pname, GLvoid **params);
void GLAPIENTRY gl_dispatch_stub_965(GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_966(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_967(GLuint framebuffer, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_968(GLuint renderbuffer, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_969(GLuint id, GLuint buffer, GLenum pname, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_970(GLuint id, GLuint buffer, GLenum pname, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_971(GLuint id, GLuint buffer, GLenum pname, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_972(GLuint id, GLuint buffer, GLenum pname, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_973(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_974(GLuint texture, GLint level, GLenum pname, GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_975(GLuint texture, GLint level, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_976(GLuint texture, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_977(GLuint texture, GLenum pname, GLuint *params);
void GLAPIENTRY gl_dispatch_stub_978(GLuint texture, GLenum pname, GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_979(GLuint texture, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_980(GLuint xfb, GLenum pname, GLuint index, GLint64 *param);
void GLAPIENTRY gl_dispatch_stub_981(GLuint xfb, GLenum pname, GLuint index, GLint *param);
void GLAPIENTRY gl_dispatch_stub_982(GLuint xfb, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_983(GLuint vaobj, GLuint index, GLenum pname, GLint64 *param);
void GLAPIENTRY gl_dispatch_stub_984(GLuint vaobj, GLuint index, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_985(GLuint vaobj, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_986(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments);
void GLAPIENTRY gl_dispatch_stub_987(GLuint framebuffer, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height);
GLvoid * GLAPIENTRY gl_dispatch_stub_988(GLuint buffer, GLenum access);
GLvoid * GLAPIENTRY gl_dispatch_stub_989(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access);
void GLAPIENTRY gl_dispatch_stub_990(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLenum usage);
void GLAPIENTRY gl_dispatch_stub_991(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLbitfield flags);
void GLAPIENTRY gl_dispatch_stub_992(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_993(GLuint framebuffer, GLenum buf);
void GLAPIENTRY gl_dispatch_stub_994(GLuint framebuffer, GLsizei n, const GLenum *bufs);
void GLAPIENTRY gl_dispatch_stub_995(GLuint framebuffer, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_996(GLuint framebuffer, GLenum buf);
void GLAPIENTRY gl_dispatch_stub_997(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void GLAPIENTRY gl_dispatch_stub_998(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level);
void GLAPIENTRY gl_dispatch_stub_999(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer);
void GLAPIENTRY gl_dispatch_stub_1000(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1001(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1002(GLuint texture, GLenum internalformat, GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1003(GLuint texture, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1004(GLuint texture, GLenum pname, const GLint *params);
void GLAPIENTRY gl_dispatch_stub_1005(GLuint texture, GLenum pname, const GLuint *params);
void GLAPIENTRY gl_dispatch_stub_1006(GLuint texture, GLenum pname, GLfloat param);
void GLAPIENTRY gl_dispatch_stub_1007(GLuint texture, GLenum pname, const GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1008(GLuint texture, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1009(GLuint texture, GLenum pname, const GLint *param);
void GLAPIENTRY gl_dispatch_stub_1010(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width);
void GLAPIENTRY gl_dispatch_stub_1011(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1012(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
void GLAPIENTRY gl_dispatch_stub_1013(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
void GLAPIENTRY gl_dispatch_stub_1014(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations);
void GLAPIENTRY gl_dispatch_stub_1015(GLuint texture, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1016(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1017(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1018(GLuint xfb, GLuint index, GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1019(GLuint xfb, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
GLboolean GLAPIENTRY gl_dispatch_stub_1020(GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1021(GLuint vaobj, GLuint attribindex, GLuint bindingindex);
void GLAPIENTRY gl_dispatch_stub_1022(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1023(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1024(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1025(GLuint vaobj, GLuint bindingindex, GLuint divisor);
void GLAPIENTRY gl_dispatch_stub_1026(GLuint vaobj, GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1027(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);
void GLAPIENTRY gl_dispatch_stub_1028(GLuint vaobj, GLuint first, GLsizei count, const GLuint *buffers, const GLintptr *offsets, const GLsizei *strides);
void GLAPIENTRY gl_dispatch_stub_1029(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLsizei bufSize, GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1030(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1031(GLenum target, GLintptr offset, GLsizeiptr size, GLboolean commit);
void GLAPIENTRY gl_dispatch_stub_1032(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit);
void GLAPIENTRY gl_dispatch_stub_1033(GLuint program, GLint location, GLint64 *params);
void GLAPIENTRY gl_dispatch_stub_1034(GLuint program, GLint location, GLuint64 *params);
void GLAPIENTRY gl_dispatch_stub_1035(GLuint program, GLint location, GLsizei bufSize, GLint64 *params);
void GLAPIENTRY gl_dispatch_stub_1036(GLuint program, GLint location, GLsizei bufSize, GLuint64 *params);
void GLAPIENTRY gl_dispatch_stub_1037(GLuint program, GLint location, GLint64 x);
void GLAPIENTRY gl_dispatch_stub_1038(GLuint program, GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1039(GLuint program, GLint location, GLuint64 x);
void GLAPIENTRY gl_dispatch_stub_1040(GLuint program, GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1041(GLuint program, GLint location, GLint64 x, GLint64 y);
void GLAPIENTRY gl_dispatch_stub_1042(GLuint program, GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1043(GLuint program, GLint location, GLuint64 x, GLuint64 y);
void GLAPIENTRY gl_dispatch_stub_1044(GLuint program, GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1045(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z);
void GLAPIENTRY gl_dispatch_stub_1046(GLuint program, GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1047(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z);
void GLAPIENTRY gl_dispatch_stub_1048(GLuint program, GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1049(GLuint program, GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w);
void GLAPIENTRY gl_dispatch_stub_1050(GLuint program, GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1051(GLuint program, GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w);
void GLAPIENTRY gl_dispatch_stub_1052(GLuint program, GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1053(GLint location, GLint64 x);
void GLAPIENTRY gl_dispatch_stub_1054(GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1055(GLint location, GLuint64 x);
void GLAPIENTRY gl_dispatch_stub_1056(GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1057(GLint location, GLint64 x, GLint64 y);
void GLAPIENTRY gl_dispatch_stub_1058(GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1059(GLint location, GLuint64 x, GLuint64 y);
void GLAPIENTRY gl_dispatch_stub_1060(GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1061(GLint location, GLint64 x, GLint64 y, GLint64 z);
void GLAPIENTRY gl_dispatch_stub_1062(GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1063(GLint location, GLuint64 x, GLuint64 y, GLuint64 z);
void GLAPIENTRY gl_dispatch_stub_1064(GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1065(GLint location, GLint64 x, GLint64 y, GLint64 z, GLint64 w);
void GLAPIENTRY gl_dispatch_stub_1066(GLint location, GLsizei count, const GLint64 *value);
void GLAPIENTRY gl_dispatch_stub_1067(GLint location, GLuint64 x, GLuint64 y, GLuint64 z, GLuint64 w);
void GLAPIENTRY gl_dispatch_stub_1068(GLint location, GLsizei count, const GLuint64 *value);
void GLAPIENTRY gl_dispatch_stub_1069(void);
void GLAPIENTRY gl_dispatch_stub_1070(GLenum target, GLuint start, GLsizei count, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1071(GLuint framebuffer, GLuint start, GLsizei count, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1072(GLuint shader, const GLchar *pEntryPoint, GLuint numSpecializationConstants, const GLuint *pConstantIndex, const GLuint *pConstantValue);
void GLAPIENTRY gl_dispatch_stub_1079(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void GLAPIENTRY gl_dispatch_stub_1080(const GLfloat *coords);
void GLAPIENTRY gl_dispatch_stub_1081(GLint x, GLint y, GLint z, GLint width, GLint height);
void GLAPIENTRY gl_dispatch_stub_1082(const GLint *coords);
void GLAPIENTRY gl_dispatch_stub_1083(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void GLAPIENTRY gl_dispatch_stub_1084(const GLshort *coords);
void GLAPIENTRY gl_dispatch_stub_1085(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void GLAPIENTRY gl_dispatch_stub_1086(const GLfixed *coords);
GLbitfield GLAPIENTRY gl_dispatch_stub_1088(GLfixed *mantissa, GLint *exponent);
void GLAPIENTRY gl_dispatch_stub_1089(GLclampf value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_1090(GLenum pattern);
void GLAPIENTRY gl_dispatch_stub_1097(GLenum target, GLsizei numAttachments, const GLenum *attachments);
void GLAPIENTRY gl_dispatch_stub_1107(GLuint program, GLint location, GLdouble x);
void GLAPIENTRY gl_dispatch_stub_1108(GLuint program, GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1115(GLuint program, GLint location, GLdouble x, GLdouble y);
void GLAPIENTRY gl_dispatch_stub_1116(GLuint program, GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1123(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1124(GLuint program, GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1131(GLuint program, GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1132(GLuint program, GLint location, GLsizei count, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1139(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1141(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1143(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1145(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1147(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1149(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1151(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1153(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1155(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLdouble *value);
void GLAPIENTRY gl_dispatch_stub_1160(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLsizei samples);
void GLAPIENTRY gl_dispatch_stub_1176(void);
void GLAPIENTRY gl_dispatch_stub_1177(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1178(const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1179(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void GLAPIENTRY gl_dispatch_stub_1180(const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1181(GLint x, GLint y, GLint z, GLint w);
void GLAPIENTRY gl_dispatch_stub_1182(const GLint *v);
void GLAPIENTRY gl_dispatch_stub_1183(GLshort x, GLshort y, GLshort z, GLshort w);
void GLAPIENTRY gl_dispatch_stub_1184(const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1185(const GLenum *mode, const GLint *first, const GLsizei *count, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_1186(const GLenum *mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount, GLint modestride);
GLboolean GLAPIENTRY gl_dispatch_stub_1187(GLsizei n, const GLuint *ids, GLboolean *residences);
void GLAPIENTRY gl_dispatch_stub_1188(GLenum target, GLuint id, const GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1189(GLenum target, GLuint index, GLenum pname, GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_1190(GLenum target, GLuint index, GLenum pname, GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1191(GLuint id, GLenum pname, GLubyte *program);
void GLAPIENTRY gl_dispatch_stub_1192(GLuint id, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1193(GLenum target, GLuint address, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1194(GLuint index, GLenum pname, GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_1195(GLuint index, GLenum pname, GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1196(GLuint index, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1197(GLenum target, GLuint id, GLsizei len, const GLubyte *program);
void GLAPIENTRY gl_dispatch_stub_1198(GLenum target, GLuint index, GLsizei num, const GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_1199(GLenum target, GLuint index, GLsizei num, const GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1200(GLsizei n, const GLuint *ids);
void GLAPIENTRY gl_dispatch_stub_1201(GLenum target, GLuint address, GLenum matrix, GLenum transform);
void GLAPIENTRY gl_dispatch_stub_1202(GLuint index, GLdouble x);
void GLAPIENTRY gl_dispatch_stub_1203(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1204(GLuint index, GLfloat x);
void GLAPIENTRY gl_dispatch_stub_1205(GLuint index, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1206(GLuint index, GLshort x);
void GLAPIENTRY gl_dispatch_stub_1207(GLuint index, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1208(GLuint index, GLdouble x, GLdouble y);
void GLAPIENTRY gl_dispatch_stub_1209(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1210(GLuint index, GLfloat x, GLfloat y);
void GLAPIENTRY gl_dispatch_stub_1211(GLuint index, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1212(GLuint index, GLshort x, GLshort y);
void GLAPIENTRY gl_dispatch_stub_1213(GLuint index, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1214(GLuint index, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1215(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1216(GLuint index, GLfloat x, GLfloat y, GLfloat z);
void GLAPIENTRY gl_dispatch_stub_1217(GLuint index, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1218(GLuint index, GLshort x, GLshort y, GLshort z);
void GLAPIENTRY gl_dispatch_stub_1219(GLuint index, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1220(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1221(GLuint index, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1222(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void GLAPIENTRY gl_dispatch_stub_1223(GLuint index, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1224(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w);
void GLAPIENTRY gl_dispatch_stub_1225(GLuint index, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1226(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
void GLAPIENTRY gl_dispatch_stub_1227(GLuint index, const GLubyte *v);
void GLAPIENTRY gl_dispatch_stub_1228(GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GLAPIENTRY gl_dispatch_stub_1229(GLuint index, GLsizei n, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1230(GLuint index, GLsizei n, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1231(GLuint index, GLsizei n, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1232(GLuint index, GLsizei n, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1233(GLuint index, GLsizei n, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1234(GLuint index, GLsizei n, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1235(GLuint index, GLsizei n, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1236(GLuint index, GLsizei n, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1237(GLuint index, GLsizei n, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1238(GLuint index, GLsizei n, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1239(GLuint index, GLsizei n, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1240(GLuint index, GLsizei n, const GLshort *v);
void GLAPIENTRY gl_dispatch_stub_1241(GLuint index, GLsizei n, const GLubyte *v);
void GLAPIENTRY gl_dispatch_stub_1242(GLenum pname, GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1243(GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_1244(GLenum pname, const GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1245(GLenum pname, const GLint *param);
void GLAPIENTRY gl_dispatch_stub_1246(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod);
void GLAPIENTRY gl_dispatch_stub_1247(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod);
void GLAPIENTRY gl_dispatch_stub_1248(GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod);
void GLAPIENTRY gl_dispatch_stub_1249(void);
void GLAPIENTRY gl_dispatch_stub_1250(GLuint id);
void GLAPIENTRY gl_dispatch_stub_1251(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod);
void GLAPIENTRY gl_dispatch_stub_1252(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod);
void GLAPIENTRY gl_dispatch_stub_1253(GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod);
void GLAPIENTRY gl_dispatch_stub_1254(GLuint id);
void GLAPIENTRY gl_dispatch_stub_1255(void);
GLuint GLAPIENTRY gl_dispatch_stub_1256(GLuint range);
void GLAPIENTRY gl_dispatch_stub_1257(GLuint dst, GLuint coord, GLenum swizzle);
void GLAPIENTRY gl_dispatch_stub_1258(GLuint dst, GLuint interp, GLenum swizzle);
void GLAPIENTRY gl_dispatch_stub_1259(GLuint dst, const GLfloat *value);
void GLAPIENTRY gl_dispatch_stub_1260(GLuint first, GLsizei count, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1261(GLuint index, GLfloat n, GLfloat f);
void GLAPIENTRY gl_dispatch_stub_1262(GLenum face);
void GLAPIENTRY gl_dispatch_stub_1263(GLuint id, GLsizei len, const GLubyte *name, GLdouble *params);
void GLAPIENTRY gl_dispatch_stub_1264(GLuint id, GLsizei len, const GLubyte *name, GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1265(GLuint id, GLsizei len, const GLubyte *name, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1266(GLuint id, GLsizei len, const GLubyte *name, const GLdouble *v);
void GLAPIENTRY gl_dispatch_stub_1267(GLuint id, GLsizei len, const GLubyte *name, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void GLAPIENTRY gl_dispatch_stub_1268(GLuint id, GLsizei len, const GLubyte *name, const GLfloat *v);
void GLAPIENTRY gl_dispatch_stub_1270(GLenum coord, GLenum pname, GLfixed *params);
void GLAPIENTRY gl_dispatch_stub_1271(GLenum coord, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1272(GLenum coord, GLenum pname, const GLfixed *params);
void GLAPIENTRY gl_dispatch_stub_1273(GLclampd zmin, GLclampd zmax);
void GLAPIENTRY gl_dispatch_stub_1276(GLsizei len, const GLvoid *string);
void GLAPIENTRY gl_dispatch_stub_1277(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1278(GLenum target, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1295(GLenum target, GLuint index, GLuint buffer, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1296(GLuint monitor);
void GLAPIENTRY gl_dispatch_stub_1297(GLsizei n, GLuint *monitors);
void GLAPIENTRY gl_dispatch_stub_1298(GLuint monitor);
void GLAPIENTRY gl_dispatch_stub_1299(GLsizei n, GLuint *monitors);
void GLAPIENTRY gl_dispatch_stub_1300(GLuint monitor, GLenum pname, GLsizei dataSize, GLuint *data, GLint *bytesWritten);
void GLAPIENTRY gl_dispatch_stub_1301(GLuint group, GLuint counter, GLenum pname, GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1302(GLuint group, GLuint counter, GLsizei bufSize, GLsizei *length, GLchar *counterString);
void GLAPIENTRY gl_dispatch_stub_1303(GLuint group, GLint *numCounters, GLint *maxActiveCounters, GLsizei countersSize, GLuint *counters);
void GLAPIENTRY gl_dispatch_stub_1304(GLuint group, GLsizei bufSize, GLsizei *length, GLchar *groupString);
void GLAPIENTRY gl_dispatch_stub_1305(GLint *numGroups, GLsizei groupsSize, GLuint *groups);
void GLAPIENTRY gl_dispatch_stub_1306(GLuint monitor, GLboolean enable, GLuint group, GLint numCounters, GLuint *counterList);
void GLAPIENTRY gl_dispatch_stub_1307(GLenum objectType, GLuint name, GLenum pname, GLint *value);
GLenum GLAPIENTRY gl_dispatch_stub_1308(GLenum objectType, GLuint name, GLenum option);
GLenum GLAPIENTRY gl_dispatch_stub_1309(GLenum objectType, GLuint name, GLenum option);
void GLAPIENTRY gl_dispatch_stub_1310(GLuint program);
GLuint GLAPIENTRY gl_dispatch_stub_1311(GLenum type, const GLchar *string);
void GLAPIENTRY gl_dispatch_stub_1312(GLenum type, GLuint program);
void GLAPIENTRY gl_dispatch_stub_1314(void);
void GLAPIENTRY gl_dispatch_stub_1315(GLintptr surface, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values);
void GLAPIENTRY gl_dispatch_stub_1316(const GLvoid *vdpDevice, const GLvoid *getProcAddress);
GLboolean GLAPIENTRY gl_dispatch_stub_1317(GLintptr surface);
void GLAPIENTRY gl_dispatch_stub_1318(GLsizei numSurfaces, const GLintptr *surfaces);
GLintptr GLAPIENTRY gl_dispatch_stub_1319(const GLvoid *vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames);
GLintptr GLAPIENTRY gl_dispatch_stub_1320(const GLvoid *vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames);
void GLAPIENTRY gl_dispatch_stub_1321(GLintptr surface, GLenum access);
void GLAPIENTRY gl_dispatch_stub_1322(GLsizei numSurfaces, const GLintptr *surfaces);
void GLAPIENTRY gl_dispatch_stub_1323(GLintptr surface);
void GLAPIENTRY gl_dispatch_stub_1324(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1325(GLuint queryId, GLuint *queryHandle);
void GLAPIENTRY gl_dispatch_stub_1326(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1327(GLuint queryHandle);
void GLAPIENTRY gl_dispatch_stub_1328(GLuint *queryId);
void GLAPIENTRY gl_dispatch_stub_1329(GLuint queryId, GLuint *nextQueryId);
void GLAPIENTRY gl_dispatch_stub_1330(GLuint queryId, GLuint counterId, GLuint counterNameLength, GLchar *counterName, GLuint counterDescLength, GLchar *counterDesc, GLuint *counterOffset, GLuint *counterDataSize, GLuint *counterTypeEnum, GLuint *counterDataTypeEnum, GLuint64 *rawCounterMaxValue);
void GLAPIENTRY gl_dispatch_stub_1331(GLuint queryHandle, GLuint flags, GLsizei dataSize, GLvoid *data, GLuint *bytesWritten);
void GLAPIENTRY gl_dispatch_stub_1332(GLchar *queryName, GLuint *queryId);
void GLAPIENTRY gl_dispatch_stub_1333(GLuint queryId, GLuint queryNameLength, GLchar *queryName, GLuint *dataSize, GLuint *noCounters, GLuint *noInstances, GLuint *capsMask);
void GLAPIENTRY gl_dispatch_stub_1334(GLfloat factor, GLfloat units, GLfloat clamp);
void GLAPIENTRY gl_dispatch_stub_1335(GLuint xbits, GLuint ybits);
void GLAPIENTRY gl_dispatch_stub_1336(GLenum pname, GLfloat param);
void GLAPIENTRY gl_dispatch_stub_1337(GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1338(GLenum mode, GLsizei count, const GLint *box);
void GLAPIENTRY gl_dispatch_stub_1339(GLenum target, GLsizeiptr size, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1340(GLsizei n, GLuint *memoryObjects);
void GLAPIENTRY gl_dispatch_stub_1341(GLsizei n, const GLuint *memoryObjects);
void GLAPIENTRY gl_dispatch_stub_1342(GLsizei n, const GLuint *semaphores);
void GLAPIENTRY gl_dispatch_stub_1343(GLsizei n, GLuint *semaphores);
void GLAPIENTRY gl_dispatch_stub_1344(GLuint memoryObject, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1345(GLuint semaphore, GLenum pname, GLuint64 *params);
void GLAPIENTRY gl_dispatch_stub_1346(GLenum target, GLuint index, GLubyte *data);
void GLAPIENTRY gl_dispatch_stub_1347(GLenum pname, GLubyte *data);
GLboolean GLAPIENTRY gl_dispatch_stub_1348(GLuint memoryObject);
GLboolean GLAPIENTRY gl_dispatch_stub_1349(GLuint semaphore);
void GLAPIENTRY gl_dispatch_stub_1350(GLuint memoryObject, GLenum pname, const GLint *params);
void GLAPIENTRY gl_dispatch_stub_1351(GLuint buffer, GLsizeiptr size, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1352(GLuint semaphore, GLenum pname, const GLuint64 *params);
void GLAPIENTRY gl_dispatch_stub_1353(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *dstLayouts);
void GLAPIENTRY gl_dispatch_stub_1354(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1355(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1356(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1357(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1358(GLenum target, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1359(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1360(GLenum texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1361(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1362(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1363(GLuint texture, GLsizei samples, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedSampleLocations, GLuint memory, GLuint64 offset);
void GLAPIENTRY gl_dispatch_stub_1364(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers, GLuint numTextureBarriers, const GLuint *textures, const GLenum *srcLayouts);
void GLAPIENTRY gl_dispatch_stub_1365(GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
void GLAPIENTRY gl_dispatch_stub_1366(GLuint semaphore, GLenum handleType, GLint fd);
void GLAPIENTRY gl_dispatch_stub_1367(void);
void GLAPIENTRY gl_dispatch_stub_1368(GLuint renderbuffer, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1369(GLenum target, GLsizei samples, GLsizei storageSamples, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1370(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
void GLAPIENTRY gl_dispatch_stub_1371(GLenum target, GLuint index, GLsizei count, const GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1372(GLenum target, GLuint index, GLsizei count, const GLfloat *params);
void GLAPIENTRY gl_dispatch_stub_1373(GLenum target, GLvoid *writeOffset);
void GLAPIENTRY gl_dispatch_stub_1374(GLenum target, GLvoid *writeOffset);
void GLAPIENTRY gl_dispatch_stub_1420(GLuint count);
void GLAPIENTRY gl_dispatch_stub_1421(GLenum matrixMode, const GLfloat *m);
void GLAPIENTRY gl_dispatch_stub_1422(GLenum matrixMode, const GLdouble *m);
void GLAPIENTRY gl_dispatch_stub_1423(GLenum matrixMode, const GLfloat *m);
void GLAPIENTRY gl_dispatch_stub_1424(GLenum matrixMode, const GLdouble *m);
void GLAPIENTRY gl_dispatch_stub_1425(GLenum matrixMode);
void GLAPIENTRY gl_dispatch_stub_1426(GLenum matrixMode, GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void GLAPIENTRY gl_dispatch_stub_1427(GLenum matrixMode, GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1428(GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z);
void GLAPIENTRY gl_dispatch_stub_1429(GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1430(GLenum matrixMode, GLfloat x, GLfloat y, GLfloat z);
void GLAPIENTRY gl_dispatch_stub_1431(GLenum matrixMode, GLdouble x, GLdouble y, GLdouble z);
void GLAPIENTRY gl_dispatch_stub_1432(GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void GLAPIENTRY gl_dispatch_stub_1433(GLenum matrixMode, GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void GLAPIENTRY gl_dispatch_stub_1434(GLenum matrixMode);
void GLAPIENTRY gl_dispatch_stub_1435(GLenum matrixMode);
void GLAPIENTRY gl_dispatch_stub_1436(GLenum matrixMode, const GLfloat *m);
void GLAPIENTRY gl_dispatch_stub_1437(GLenum matrixMode, const GLdouble *m);
void GLAPIENTRY gl_dispatch_stub_1438(GLenum matrixMode, const GLfloat *m);
void GLAPIENTRY gl_dispatch_stub_1439(GLenum matrixMode, const GLdouble *m);
void GLAPIENTRY gl_dispatch_stub_1440(GLenum texunit, GLenum target, GLuint texture);
void GLAPIENTRY gl_dispatch_stub_1441(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLenum usage);
void GLAPIENTRY gl_dispatch_stub_1442(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1443(GLuint buffer, GLsizeiptr size, const GLvoid *data, GLbitfield flags);
GLvoid * GLAPIENTRY gl_dispatch_stub_1444(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access);
void GLAPIENTRY gl_dispatch_stub_1445(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1446(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1447(GLuint texture, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1448(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1449(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1450(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1451(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, int border);
void GLAPIENTRY gl_dispatch_stub_1452(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, int border);
void GLAPIENTRY gl_dispatch_stub_1453(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void GLAPIENTRY gl_dispatch_stub_1454(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1455(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
GLvoid * GLAPIENTRY gl_dispatch_stub_1456(GLuint buffer, GLenum access);
void GLAPIENTRY gl_dispatch_stub_1457(GLuint texture, GLenum target, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1458(GLuint texture, GLenum target, GLenum pname, float *params);
void GLAPIENTRY gl_dispatch_stub_1459(GLuint texture, GLenum target, GLenum pname, int param);
void GLAPIENTRY gl_dispatch_stub_1460(GLuint texture, GLenum target, GLenum pname, const GLint *params);
void GLAPIENTRY gl_dispatch_stub_1461(GLuint texture, GLenum target, GLenum pname, float param);
void GLAPIENTRY gl_dispatch_stub_1462(GLuint texture, GLenum target, GLenum pname, const float *params);
void GLAPIENTRY gl_dispatch_stub_1463(GLuint texture, GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void GLAPIENTRY gl_dispatch_stub_1464(GLuint texture, GLenum target, GLint level, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1465(GLuint texture, GLenum target, GLint level, GLenum pname, float *params);
void GLAPIENTRY gl_dispatch_stub_1466(GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1467(GLuint buffer, GLenum pname, GLvoid **params);
void GLAPIENTRY gl_dispatch_stub_1468(GLuint buffer, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1469(GLuint buffer, GLintptr offset, GLsizeiptr length);
void GLAPIENTRY gl_dispatch_stub_1470(GLuint framebuffer, GLenum mode);
void GLAPIENTRY gl_dispatch_stub_1471(GLuint framebuffer, GLsizei n, const GLenum *bufs);
void GLAPIENTRY gl_dispatch_stub_1472(GLuint framebuffer, GLenum mode);
void GLAPIENTRY gl_dispatch_stub_1473(GLuint framebuffer, GLenum pname, GLint *param);
GLenum GLAPIENTRY gl_dispatch_stub_1474(GLuint framebuffer, GLenum target);
void GLAPIENTRY gl_dispatch_stub_1475(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void GLAPIENTRY gl_dispatch_stub_1476(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void GLAPIENTRY gl_dispatch_stub_1477(GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
void GLAPIENTRY gl_dispatch_stub_1478(GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
void GLAPIENTRY gl_dispatch_stub_1479(GLuint framebuffer, GLenum attachment, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1480(GLenum array, GLuint index);
void GLAPIENTRY gl_dispatch_stub_1481(GLenum array, GLuint index);
void GLAPIENTRY gl_dispatch_stub_1482(GLenum target, GLuint index, GLvoid**params);
void GLAPIENTRY gl_dispatch_stub_1483(GLenum texunit, GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1484(GLenum texunit, GLenum target, GLenum pname, const GLint *param);
void GLAPIENTRY gl_dispatch_stub_1485(GLenum texunit, GLenum target, GLenum pname, GLfloat param);
void GLAPIENTRY gl_dispatch_stub_1486(GLenum texunit, GLenum target, GLenum pname, const GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1487(GLenum texunit, GLenum target, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_1488(GLenum texunit, GLenum target, GLenum pname, GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1489(GLenum texunit, GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1490(GLenum texunit, GLenum target, GLenum pname, const GLint*param);
void GLAPIENTRY gl_dispatch_stub_1491(GLenum texunit, GLenum target, GLenum pname, GLfloat param);
void GLAPIENTRY gl_dispatch_stub_1492(GLenum texunit, GLenum target, GLenum pname, const GLfloat*param);
void GLAPIENTRY gl_dispatch_stub_1493(GLenum texunit, GLenum target, GLint level, GLenum format, GLenum type, GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1494(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1495(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1496(GLenum texunit, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1497(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1498(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1499(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid*pixels);
void GLAPIENTRY gl_dispatch_stub_1500(GLenum texunit, GLenum target, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1501(GLenum texunit, GLenum target, GLenum pname, GLfloat*params);
void GLAPIENTRY gl_dispatch_stub_1502(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
void GLAPIENTRY gl_dispatch_stub_1503(GLenum texunit, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void GLAPIENTRY gl_dispatch_stub_1504(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void GLAPIENTRY gl_dispatch_stub_1505(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1506(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1507(GLenum texunit, GLenum coord, GLenum pname, GLdouble param);
void GLAPIENTRY gl_dispatch_stub_1508(GLenum texunit, GLenum coord, GLenum pname, const GLdouble*param);
void GLAPIENTRY gl_dispatch_stub_1509(GLenum texunit, GLenum coord, GLenum pname, GLfloat param);
void GLAPIENTRY gl_dispatch_stub_1510(GLenum texunit, GLenum coord, GLenum pname, const GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1511(GLenum texunit, GLenum coord, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1512(GLenum texunit, GLenum coord, GLenum pname, const GLint *param);
void GLAPIENTRY gl_dispatch_stub_1513(GLenum texunit, GLenum coord, GLenum pname, GLdouble *param);
void GLAPIENTRY gl_dispatch_stub_1514(GLenum texunit, GLenum coord, GLenum pname, GLfloat *param);
void GLAPIENTRY gl_dispatch_stub_1515(GLenum texunit, GLenum coord, GLenum pname, GLint *param);
void GLAPIENTRY gl_dispatch_stub_1516(GLenum texunit, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GLAPIENTRY gl_dispatch_stub_1517(GLuint index, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLint format);
void GLAPIENTRY gl_dispatch_stub_1518(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1519(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1520(GLuint texture, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1521(GLuint texture, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1522(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1523(GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1524(GLuint texture, GLenum target, GLint level, GLvoid *img);
void GLAPIENTRY gl_dispatch_stub_1525(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1526(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1527(GLenum texunit, GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLsizei border, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1528(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1529(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1530(GLenum texunit, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1531(GLenum texunit, GLenum target, GLint level, GLvoid *img);
void GLAPIENTRY gl_dispatch_stub_1532(GLenum texunit, GLenum target, GLint level, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1533(GLenum texunit, GLenum target, GLint level, GLenum pname, GLfloat*params);
void GLAPIENTRY gl_dispatch_stub_1534(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1535(GLenum target, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1536(GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1537(GLuint renderbuffer, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1538(GLbitfield mask);
void GLAPIENTRY gl_dispatch_stub_1539(GLbitfield mask);
void GLAPIENTRY gl_dispatch_stub_1540(GLuint program, GLenum target, GLenum format, GLsizei len, const GLvoid*string);
void GLAPIENTRY gl_dispatch_stub_1541(GLuint program, GLenum target, GLenum pname, GLvoid*string);
void GLAPIENTRY gl_dispatch_stub_1542(GLuint program, GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void GLAPIENTRY gl_dispatch_stub_1543(GLuint program, GLenum target, GLuint index, const GLfloat*params);
void GLAPIENTRY gl_dispatch_stub_1544(GLuint program, GLenum target, GLuint index, GLfloat*params);
void GLAPIENTRY gl_dispatch_stub_1545(GLuint program, GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void GLAPIENTRY gl_dispatch_stub_1546(GLuint program, GLenum target, GLuint index, const GLdouble*params);
void GLAPIENTRY gl_dispatch_stub_1547(GLuint program, GLenum target, GLuint index, GLdouble*params);
void GLAPIENTRY gl_dispatch_stub_1548(GLuint program, GLenum target, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1549(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1550(GLenum texunit, GLenum target, GLenum internalformat, GLuint buffer);
void GLAPIENTRY gl_dispatch_stub_1551(GLuint texture, GLenum target, GLenum pname, const GLint*params);
void GLAPIENTRY gl_dispatch_stub_1552(GLuint texture, GLenum target, GLenum pname, const GLuint*params);
void GLAPIENTRY gl_dispatch_stub_1553(GLuint texture, GLenum target, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1554(GLuint texture, GLenum target, GLenum pname, GLuint*params);
void GLAPIENTRY gl_dispatch_stub_1555(GLenum texunit, GLenum target, GLenum pname, const GLint*params);
void GLAPIENTRY gl_dispatch_stub_1556(GLenum texunit, GLenum target, GLenum pname, const GLuint*params);
void GLAPIENTRY gl_dispatch_stub_1557(GLenum texunit, GLenum target, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1558(GLenum texunit, GLenum target, GLenum pname, GLuint*params);
void GLAPIENTRY gl_dispatch_stub_1559(GLuint program, GLenum target, GLuint index, GLsizei count, const GLfloat*params);
void GLAPIENTRY gl_dispatch_stub_1560(GLuint texture, GLenum target);
void GLAPIENTRY gl_dispatch_stub_1561(GLenum texunit, GLenum target);
void GLAPIENTRY gl_dispatch_stub_1562(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
void GLAPIENTRY gl_dispatch_stub_1563(GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1564(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1565(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1566(GLuint vaobj, GLuint buffer, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1567(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1568(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1569(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1570(GLuint vaobj, GLuint buffer, GLenum texunit, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1571(GLuint vaobj, GLuint buffer, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1572(GLuint vaobj, GLuint buffer, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1573(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1574(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1575(GLuint vaobj, GLenum array);
void GLAPIENTRY gl_dispatch_stub_1576(GLuint vaobj, GLenum array);
void GLAPIENTRY gl_dispatch_stub_1577(GLuint vaobj, GLuint index);
void GLAPIENTRY gl_dispatch_stub_1578(GLuint vaobj, GLuint index);
void GLAPIENTRY gl_dispatch_stub_1579(GLuint vaobj, GLenum pname, GLint*param);
void GLAPIENTRY gl_dispatch_stub_1580(GLuint vaobj, GLenum pname, GLvoid**param);
void GLAPIENTRY gl_dispatch_stub_1581(GLuint vaobj, GLuint index, GLenum pname, GLint*param);
void GLAPIENTRY gl_dispatch_stub_1582(GLuint vaobj, GLuint index, GLenum pname, GLvoid**param);
void GLAPIENTRY gl_dispatch_stub_1583(GLuint buffer, GLenum internalformat, GLenum format, GLenum type, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1584(GLuint buffer, GLenum internalformat, GLintptr offset, GLsizeiptr size, GLenum format, GLenum type, const GLvoid *data);
void GLAPIENTRY gl_dispatch_stub_1585(GLuint framebuffer, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_1586(GLuint framebuffer, GLenum pname, GLint*params);
void GLAPIENTRY gl_dispatch_stub_1587(GLuint vaobj, GLuint buffer, GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset);
void GLAPIENTRY gl_dispatch_stub_1588(GLuint vaobj, GLuint index, GLuint divisor);
void GLAPIENTRY gl_dispatch_stub_1589(GLuint texture, GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_1590(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
void GLAPIENTRY gl_dispatch_stub_1591(GLuint texture, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLboolean fixedsamplelocations);
void GLAPIENTRY gl_dispatch_stub_1592(GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);
void GLAPIENTRY gl_dispatch_stub_1593(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1594(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1595(GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
void GLAPIENTRY gl_dispatch_stub_1596(GLuint vaobj, GLuint attribindex, GLuint bindingindex);
void GLAPIENTRY gl_dispatch_stub_1597(GLuint vaobj, GLuint bindingindex, GLuint divisor);
void GLAPIENTRY gl_dispatch_stub_1598(GLuint buffer, GLintptr offset, GLsizeiptr size, GLboolean commit);
void GLAPIENTRY gl_dispatch_stub_1599(GLenum type, GLint namelen, const GLchar *name, GLint stringlen, const GLchar *string);
void GLAPIENTRY gl_dispatch_stub_1600(GLint namelen, const GLchar *name);
void GLAPIENTRY gl_dispatch_stub_1601(GLuint shader, GLsizei count, const GLchar * const *path, const GLint *length);
GLboolean GLAPIENTRY gl_dispatch_stub_1602(GLint namelen, const GLchar *name);
void GLAPIENTRY gl_dispatch_stub_1603(GLint namelen, const GLchar *name, GLsizei bufSize, GLint *stringlen, GLchar *string);
void GLAPIENTRY gl_dispatch_stub_1604(GLint namelen, const GLchar *name, GLenum pname, GLint *params);
void GLAPIENTRY gl_dispatch_stub_1605(GLenum target, GLvoid *image, const GLint *attrib_list);
void GLAPIENTRY gl_dispatch_stub_1606(GLuint texture, GLvoid *image, const GLint *attrib_list);
void GLAPIENTRY gl_dispatch_stub_1607(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei width, GLsizei height, GLsizei depth);
void GLAPIENTRY gl_dispatch_stub_1608(GLuint index, GLenum swizzlex, GLenum swizzley, GLenum swizzlez, GLenum swizzlew);
void GLAPIENTRY gl_dispatch_stub_1609(GLenum mode);
void GLAPIENTRY gl_dispatch_stub_1610(GLintptr srcBuffer, GLuint srcOffset, GLuint dstTargetOrName, GLintptr dstOffset, GLsizeiptr size, GLboolean named, GLboolean ext_dsa);
void GLAPIENTRY gl_dispatch_stub_1611(GLhalfNV x, GLhalfNV y);
void GLAPIENTRY gl_dispatch_stub_1612(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1613(GLhalfNV x, GLhalfNV y, GLhalfNV z);
void GLAPIENTRY gl_dispatch_stub_1614(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1615(GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w);
void GLAPIENTRY gl_dispatch_stub_1616(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1617(GLhalfNV nx, GLhalfNV ny, GLhalfNV nz);
void GLAPIENTRY gl_dispatch_stub_1618(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1619(GLhalfNV red, GLhalfNV green, GLhalfNV blue);
void GLAPIENTRY gl_dispatch_stub_1620(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1621(GLhalfNV red, GLhalfNV green, GLhalfNV blue, GLhalfNV alpha);
void GLAPIENTRY gl_dispatch_stub_1622(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1623(GLhalfNV s);
void GLAPIENTRY gl_dispatch_stub_1624(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1625(GLhalfNV s, GLhalfNV t);
void GLAPIENTRY gl_dispatch_stub_1626(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1627(GLhalfNV s, GLhalfNV t, GLhalfNV r);
void GLAPIENTRY gl_dispatch_stub_1628(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1629(GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q);
void GLAPIENTRY gl_dispatch_stub_1630(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1631(GLenum target, GLhalfNV s);
void GLAPIENTRY gl_dispatch_stub_1632(GLenum target, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1633(GLenum target, GLhalfNV s, GLhalfNV t);
void GLAPIENTRY gl_dispatch_stub_1634(GLenum target, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1635(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r);
void GLAPIENTRY gl_dispatch_stub_1636(GLenum target, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1637(GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q);
void GLAPIENTRY gl_dispatch_stub_1638(GLenum target, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1639(GLhalfNV x);
void GLAPIENTRY gl_dispatch_stub_1640(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1641(GLhalfNV red, GLhalfNV green, GLhalfNV blue);
void GLAPIENTRY gl_dispatch_stub_1642(const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1643(GLenum error);
void GLAPIENTRY gl_dispatch_stub_1644(GLuint index, GLhalfNV x);
void GLAPIENTRY gl_dispatch_stub_1645(GLuint index, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1646(GLuint index, GLhalfNV x, GLhalfNV y);
void GLAPIENTRY gl_dispatch_stub_1647(GLuint index, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1648(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z);
void GLAPIENTRY gl_dispatch_stub_1649(GLuint index, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1650(GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w);
void GLAPIENTRY gl_dispatch_stub_1651(GLuint index, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1652(GLuint index, GLsizei n, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1653(GLuint index, GLsizei n, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1654(GLuint index, GLsizei n, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1655(GLuint index, GLsizei n, const GLhalfNV *v);
void GLAPIENTRY gl_dispatch_stub_1656(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit);
void GLAPIENTRY gl_dispatch_stub_1657(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLboolean commit);
void GLAPIENTRY gl_dispatch_stub_1658(GLuint memory, GLuint64 size, GLenum handleType, GLvoid *handle);
void GLAPIENTRY gl_dispatch_stub_1659(GLuint semaphore, GLenum handleType, GLvoid *handle);
void GLAPIENTRY gl_dispatch_stub_1660(GLuint memory, GLuint64 size, GLenum handleType, const GLvoid *name);
void GLAPIENTRY gl_dispatch_stub_1661(GLuint semaphore, GLenum handleType, const GLvoid *handle);
void GLAPIENTRY gl_dispatch_stub_1664(void);
void GLAPIENTRY gl_dispatch_stub_1665(const GLvoid *cmd);
void GLAPIENTRY gl_dispatch_stub_1666(void);
void GLAPIENTRY gl_dispatch_stub_1667(GLintptr indexBuf, GLenum mode, const GLsizei *count, GLenum type, const GLvoid * const *indices, GLsizei primcount, const GLint *basevertex);
void GLAPIENTRY gl_dispatch_stub_1668(void);
void GLAPIENTRY gl_dispatch_stub_1669(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instance_count, GLint basevertex, GLuint baseinstance, GLuint drawid);
void GLAPIENTRY gl_dispatch_stub_1670(void);
void GLAPIENTRY gl_dispatch_stub_1671(GLenum mode, GLenum type, GLushort count, GLushort indices);
void GLAPIENTRY gl_dispatch_stub_1672(const GLvoid *cmd);
void GLAPIENTRY gl_dispatch_stub_1676(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint baseviewindex, GLsizei numviews);
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
    NAME_FUNC_OFFSET( 5037, glActiveTexture, glActiveTexture, NULL, 374),
    NAME_FUNC_OFFSET( 5053, glClientActiveTexture, glClientActiveTexture, NULL, 375),
    NAME_FUNC_OFFSET( 5075, glMultiTexCoord1d, glMultiTexCoord1d, NULL, 376),
    NAME_FUNC_OFFSET( 5093, glMultiTexCoord1dv, glMultiTexCoord1dv, NULL, 377),
    NAME_FUNC_OFFSET( 5112, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET( 5133, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET( 5155, glMultiTexCoord1i, glMultiTexCoord1i, NULL, 380),
    NAME_FUNC_OFFSET( 5173, glMultiTexCoord1iv, glMultiTexCoord1iv, NULL, 381),
    NAME_FUNC_OFFSET( 5192, glMultiTexCoord1s, glMultiTexCoord1s, NULL, 382),
    NAME_FUNC_OFFSET( 5210, glMultiTexCoord1sv, glMultiTexCoord1sv, NULL, 383),
    NAME_FUNC_OFFSET( 5229, glMultiTexCoord2d, glMultiTexCoord2d, NULL, 384),
    NAME_FUNC_OFFSET( 5247, glMultiTexCoord2dv, glMultiTexCoord2dv, NULL, 385),
    NAME_FUNC_OFFSET( 5266, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET( 5287, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET( 5309, glMultiTexCoord2i, glMultiTexCoord2i, NULL, 388),
    NAME_FUNC_OFFSET( 5327, glMultiTexCoord2iv, glMultiTexCoord2iv, NULL, 389),
    NAME_FUNC_OFFSET( 5346, glMultiTexCoord2s, glMultiTexCoord2s, NULL, 390),
    NAME_FUNC_OFFSET( 5364, glMultiTexCoord2sv, glMultiTexCoord2sv, NULL, 391),
    NAME_FUNC_OFFSET( 5383, glMultiTexCoord3d, glMultiTexCoord3d, NULL, 392),
    NAME_FUNC_OFFSET( 5401, glMultiTexCoord3dv, glMultiTexCoord3dv, NULL, 393),
    NAME_FUNC_OFFSET( 5420, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET( 5441, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET( 5463, glMultiTexCoord3i, glMultiTexCoord3i, NULL, 396),
    NAME_FUNC_OFFSET( 5481, glMultiTexCoord3iv, glMultiTexCoord3iv, NULL, 397),
    NAME_FUNC_OFFSET( 5500, glMultiTexCoord3s, glMultiTexCoord3s, NULL, 398),
    NAME_FUNC_OFFSET( 5518, glMultiTexCoord3sv, glMultiTexCoord3sv, NULL, 399),
    NAME_FUNC_OFFSET( 5537, glMultiTexCoord4d, glMultiTexCoord4d, NULL, 400),
    NAME_FUNC_OFFSET( 5555, glMultiTexCoord4dv, glMultiTexCoord4dv, NULL, 401),
    NAME_FUNC_OFFSET( 5574, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET( 5595, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET( 5617, glMultiTexCoord4i, glMultiTexCoord4i, NULL, 404),
    NAME_FUNC_OFFSET( 5635, glMultiTexCoord4iv, glMultiTexCoord4iv, NULL, 405),
    NAME_FUNC_OFFSET( 5654, glMultiTexCoord4s, glMultiTexCoord4s, NULL, 406),
    NAME_FUNC_OFFSET( 5672, glMultiTexCoord4sv, glMultiTexCoord4sv, NULL, 407),
    NAME_FUNC_OFFSET( 5691, glCompressedTexImage1D, glCompressedTexImage1D, NULL, 408),
    NAME_FUNC_OFFSET( 5714, glCompressedTexImage2D, glCompressedTexImage2D, NULL, 409),
    NAME_FUNC_OFFSET( 5737, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET( 5760, glCompressedTexSubImage1D, glCompressedTexSubImage1D, NULL, 411),
    NAME_FUNC_OFFSET( 5786, glCompressedTexSubImage2D, glCompressedTexSubImage2D, NULL, 412),
    NAME_FUNC_OFFSET( 5812, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET( 5838, glGetCompressedTexImage, glGetCompressedTexImage, NULL, 414),
    NAME_FUNC_OFFSET( 5862, glLoadTransposeMatrixd, glLoadTransposeMatrixd, NULL, 415),
    NAME_FUNC_OFFSET( 5885, glLoadTransposeMatrixf, glLoadTransposeMatrixf, NULL, 416),
    NAME_FUNC_OFFSET( 5908, glMultTransposeMatrixd, glMultTransposeMatrixd, NULL, 417),
    NAME_FUNC_OFFSET( 5931, glMultTransposeMatrixf, glMultTransposeMatrixf, NULL, 418),
    NAME_FUNC_OFFSET( 5954, glSampleCoverage, glSampleCoverage, NULL, 419),
    NAME_FUNC_OFFSET( 5971, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET( 5991, glFogCoordPointer, glFogCoordPointer, NULL, 421),
    NAME_FUNC_OFFSET( 6009, glFogCoordd, glFogCoordd, NULL, 422),
    NAME_FUNC_OFFSET( 6021, glFogCoorddv, glFogCoorddv, NULL, 423),
    NAME_FUNC_OFFSET( 6034, glMultiDrawArrays, glMultiDrawArrays, NULL, 424),
    NAME_FUNC_OFFSET( 6052, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET( 6070, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET( 6089, glPointParameteri, glPointParameteri, NULL, 427),
    NAME_FUNC_OFFSET( 6107, glPointParameteriv, glPointParameteriv, NULL, 428),
    NAME_FUNC_OFFSET( 6126, glSecondaryColor3b, glSecondaryColor3b, NULL, 429),
    NAME_FUNC_OFFSET( 6145, glSecondaryColor3bv, glSecondaryColor3bv, NULL, 430),
    NAME_FUNC_OFFSET( 6165, glSecondaryColor3d, glSecondaryColor3d, NULL, 431),
    NAME_FUNC_OFFSET( 6184, glSecondaryColor3dv, glSecondaryColor3dv, NULL, 432),
    NAME_FUNC_OFFSET( 6204, glSecondaryColor3i, glSecondaryColor3i, NULL, 433),
    NAME_FUNC_OFFSET( 6223, glSecondaryColor3iv, glSecondaryColor3iv, NULL, 434),
    NAME_FUNC_OFFSET( 6243, glSecondaryColor3s, glSecondaryColor3s, NULL, 435),
    NAME_FUNC_OFFSET( 6262, glSecondaryColor3sv, glSecondaryColor3sv, NULL, 436),
    NAME_FUNC_OFFSET( 6282, glSecondaryColor3ub, glSecondaryColor3ub, NULL, 437),
    NAME_FUNC_OFFSET( 6302, glSecondaryColor3ubv, glSecondaryColor3ubv, NULL, 438),
    NAME_FUNC_OFFSET( 6323, glSecondaryColor3ui, glSecondaryColor3ui, NULL, 439),
    NAME_FUNC_OFFSET( 6343, glSecondaryColor3uiv, glSecondaryColor3uiv, NULL, 440),
    NAME_FUNC_OFFSET( 6364, glSecondaryColor3us, glSecondaryColor3us, NULL, 441),
    NAME_FUNC_OFFSET( 6384, glSecondaryColor3usv, glSecondaryColor3usv, NULL, 442),
    NAME_FUNC_OFFSET( 6405, glSecondaryColorPointer, glSecondaryColorPointer, NULL, 443),
    NAME_FUNC_OFFSET( 6429, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET( 6443, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET( 6458, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET( 6472, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET( 6487, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET( 6501, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET( 6516, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET( 6530, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET( 6545, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET( 6559, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET( 6574, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET( 6588, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET( 6603, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET( 6617, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET( 6632, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET( 6646, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET( 6661, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET( 6674, glBindBuffer, glBindBuffer, NULL, 461),
    NAME_FUNC_OFFSET( 6687, glBufferData, glBufferData, NULL, 462),
    NAME_FUNC_OFFSET( 6700, glBufferSubData, glBufferSubData, NULL, 463),
    NAME_FUNC_OFFSET( 6716, glDeleteBuffers, glDeleteBuffers, NULL, 464),
    NAME_FUNC_OFFSET( 6732, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET( 6748, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET( 6759, glGenBuffers, glGenBuffers, NULL, 467),
    NAME_FUNC_OFFSET( 6772, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET( 6785, glGetBufferParameteriv, glGetBufferParameteriv, NULL, 469),
    NAME_FUNC_OFFSET( 6808, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET( 6828, glGetBufferSubData, glGetBufferSubData, NULL, 471),
    NAME_FUNC_OFFSET( 6847, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET( 6866, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET( 6886, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET( 6899, glIsBuffer, glIsBuffer, NULL, 475),
    NAME_FUNC_OFFSET( 6910, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET( 6920, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET( 6932, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET( 6946, glAttachShader, glAttachShader, NULL, 479),
    NAME_FUNC_OFFSET( 6961, glBindAttribLocation, glBindAttribLocation, NULL, 480),
    NAME_FUNC_OFFSET( 6982, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET( 7006, glCompileShader, glCompileShader, NULL, 482),
    NAME_FUNC_OFFSET( 7022, glCreateProgram, glCreateProgram, NULL, 483),
    NAME_FUNC_OFFSET( 7038, glCreateShader, glCreateShader, NULL, 484),
    NAME_FUNC_OFFSET( 7053, glDeleteProgram, glDeleteProgram, NULL, 485),
    NAME_FUNC_OFFSET( 7069, glDeleteShader, glDeleteShader, NULL, 486),
    NAME_FUNC_OFFSET( 7084, glDetachShader, glDetachShader, NULL, 487),
    NAME_FUNC_OFFSET( 7099, glDisableVertexAttribArray, glDisableVertexAttribArray, NULL, 488),
    NAME_FUNC_OFFSET( 7126, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET( 7140, glEnableVertexAttribArray, glEnableVertexAttribArray, NULL, 490),
    NAME_FUNC_OFFSET( 7166, glGetActiveAttrib, glGetActiveAttrib, NULL, 491),
    NAME_FUNC_OFFSET( 7184, glGetActiveUniform, glGetActiveUniform, NULL, 492),
    NAME_FUNC_OFFSET( 7203, glGetAttachedShaders, glGetAttachedShaders, NULL, 493),
    NAME_FUNC_OFFSET( 7224, glGetAttribLocation, glGetAttribLocation, NULL, 494),
    NAME_FUNC_OFFSET( 7244, glGetProgramInfoLog, glGetProgramInfoLog, NULL, 495),
    NAME_FUNC_OFFSET( 7264, glGetProgramiv, glGetProgramiv, NULL, 496),
    NAME_FUNC_OFFSET( 7279, glGetShaderInfoLog, glGetShaderInfoLog, NULL, 497),
    NAME_FUNC_OFFSET( 7298, glGetShaderSource, glGetShaderSource, NULL, 498),
    NAME_FUNC_OFFSET( 7316, glGetShaderiv, glGetShaderiv, NULL, 499),
    NAME_FUNC_OFFSET( 7330, glGetUniformLocation, glGetUniformLocation, NULL, 500),
    NAME_FUNC_OFFSET( 7351, glGetUniformfv, glGetUniformfv, NULL, 501),
    NAME_FUNC_OFFSET( 7366, glGetUniformiv, glGetUniformiv, NULL, 502),
    NAME_FUNC_OFFSET( 7381, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET( 7407, glGetVertexAttribdv, glGetVertexAttribdv, NULL, 504),
    NAME_FUNC_OFFSET( 7427, glGetVertexAttribfv, glGetVertexAttribfv, NULL, 505),
    NAME_FUNC_OFFSET( 7447, glGetVertexAttribiv, glGetVertexAttribiv, NULL, 506),
    NAME_FUNC_OFFSET( 7467, glIsProgram, glIsProgram, NULL, 507),
    NAME_FUNC_OFFSET( 7479, glIsShader, glIsShader, NULL, 508),
    NAME_FUNC_OFFSET( 7490, glLinkProgram, glLinkProgram, NULL, 509),
    NAME_FUNC_OFFSET( 7504, glShaderSource, glShaderSource, NULL, 510),
    NAME_FUNC_OFFSET( 7519, glStencilFuncSeparate, glStencilFuncSeparate, NULL, 511),
    NAME_FUNC_OFFSET( 7541, glStencilMaskSeparate, glStencilMaskSeparate, NULL, 512),
    NAME_FUNC_OFFSET( 7563, glStencilOpSeparate, glStencilOpSeparate, NULL, 513),
    NAME_FUNC_OFFSET( 7583, glUniform1f, glUniform1f, NULL, 514),
    NAME_FUNC_OFFSET( 7595, glUniform1fv, glUniform1fv, NULL, 515),
    NAME_FUNC_OFFSET( 7608, glUniform1i, glUniform1i, NULL, 516),
    NAME_FUNC_OFFSET( 7620, glUniform1iv, glUniform1iv, NULL, 517),
    NAME_FUNC_OFFSET( 7633, glUniform2f, glUniform2f, NULL, 518),
    NAME_FUNC_OFFSET( 7645, glUniform2fv, glUniform2fv, NULL, 519),
    NAME_FUNC_OFFSET( 7658, glUniform2i, glUniform2i, NULL, 520),
    NAME_FUNC_OFFSET( 7670, glUniform2iv, glUniform2iv, NULL, 521),
    NAME_FUNC_OFFSET( 7683, glUniform3f, glUniform3f, NULL, 522),
    NAME_FUNC_OFFSET( 7695, glUniform3fv, glUniform3fv, NULL, 523),
    NAME_FUNC_OFFSET( 7708, glUniform3i, glUniform3i, NULL, 524),
    NAME_FUNC_OFFSET( 7720, glUniform3iv, glUniform3iv, NULL, 525),
    NAME_FUNC_OFFSET( 7733, glUniform4f, glUniform4f, NULL, 526),
    NAME_FUNC_OFFSET( 7745, glUniform4fv, glUniform4fv, NULL, 527),
    NAME_FUNC_OFFSET( 7758, glUniform4i, glUniform4i, NULL, 528),
    NAME_FUNC_OFFSET( 7770, glUniform4iv, glUniform4iv, NULL, 529),
    NAME_FUNC_OFFSET( 7783, glUniformMatrix2fv, glUniformMatrix2fv, NULL, 530),
    NAME_FUNC_OFFSET( 7802, glUniformMatrix3fv, glUniformMatrix3fv, NULL, 531),
    NAME_FUNC_OFFSET( 7821, glUniformMatrix4fv, glUniformMatrix4fv, NULL, 532),
    NAME_FUNC_OFFSET( 7840, glUseProgram, glUseProgram, NULL, 533),
    NAME_FUNC_OFFSET( 7853, glValidateProgram, glValidateProgram, NULL, 534),
    NAME_FUNC_OFFSET( 7871, glVertexAttrib1d, glVertexAttrib1d, NULL, 535),
    NAME_FUNC_OFFSET( 7888, glVertexAttrib1dv, glVertexAttrib1dv, NULL, 536),
    NAME_FUNC_OFFSET( 7906, glVertexAttrib1s, glVertexAttrib1s, NULL, 537),
    NAME_FUNC_OFFSET( 7923, glVertexAttrib1sv, glVertexAttrib1sv, NULL, 538),
    NAME_FUNC_OFFSET( 7941, glVertexAttrib2d, glVertexAttrib2d, NULL, 539),
    NAME_FUNC_OFFSET( 7958, glVertexAttrib2dv, glVertexAttrib2dv, NULL, 540),
    NAME_FUNC_OFFSET( 7976, glVertexAttrib2s, glVertexAttrib2s, NULL, 541),
    NAME_FUNC_OFFSET( 7993, glVertexAttrib2sv, glVertexAttrib2sv, NULL, 542),
    NAME_FUNC_OFFSET( 8011, glVertexAttrib3d, glVertexAttrib3d, NULL, 543),
    NAME_FUNC_OFFSET( 8028, glVertexAttrib3dv, glVertexAttrib3dv, NULL, 544),
    NAME_FUNC_OFFSET( 8046, glVertexAttrib3s, glVertexAttrib3s, NULL, 545),
    NAME_FUNC_OFFSET( 8063, glVertexAttrib3sv, glVertexAttrib3sv, NULL, 546),
    NAME_FUNC_OFFSET( 8081, glVertexAttrib4Nbv, glVertexAttrib4Nbv, NULL, 547),
    NAME_FUNC_OFFSET( 8100, glVertexAttrib4Niv, glVertexAttrib4Niv, NULL, 548),
    NAME_FUNC_OFFSET( 8119, glVertexAttrib4Nsv, glVertexAttrib4Nsv, NULL, 549),
    NAME_FUNC_OFFSET( 8138, glVertexAttrib4Nub, glVertexAttrib4Nub, NULL, 550),
    NAME_FUNC_OFFSET( 8157, glVertexAttrib4Nubv, glVertexAttrib4Nubv, NULL, 551),
    NAME_FUNC_OFFSET( 8177, glVertexAttrib4Nuiv, glVertexAttrib4Nuiv, NULL, 552),
    NAME_FUNC_OFFSET( 8197, glVertexAttrib4Nusv, glVertexAttrib4Nusv, NULL, 553),
    NAME_FUNC_OFFSET( 8217, glVertexAttrib4bv, glVertexAttrib4bv, NULL, 554),
    NAME_FUNC_OFFSET( 8235, glVertexAttrib4d, glVertexAttrib4d, NULL, 555),
    NAME_FUNC_OFFSET( 8252, glVertexAttrib4dv, glVertexAttrib4dv, NULL, 556),
    NAME_FUNC_OFFSET( 8270, glVertexAttrib4iv, glVertexAttrib4iv, NULL, 557),
    NAME_FUNC_OFFSET( 8288, glVertexAttrib4s, glVertexAttrib4s, NULL, 558),
    NAME_FUNC_OFFSET( 8305, glVertexAttrib4sv, glVertexAttrib4sv, NULL, 559),
    NAME_FUNC_OFFSET( 8323, glVertexAttrib4ubv, glVertexAttrib4ubv, NULL, 560),
    NAME_FUNC_OFFSET( 8342, glVertexAttrib4uiv, glVertexAttrib4uiv, NULL, 561),
    NAME_FUNC_OFFSET( 8361, glVertexAttrib4usv, glVertexAttrib4usv, NULL, 562),
    NAME_FUNC_OFFSET( 8380, glVertexAttribPointer, glVertexAttribPointer, NULL, 563),
    NAME_FUNC_OFFSET( 8402, glUniformMatrix2x3fv, glUniformMatrix2x3fv, NULL, 564),
    NAME_FUNC_OFFSET( 8423, glUniformMatrix2x4fv, glUniformMatrix2x4fv, NULL, 565),
    NAME_FUNC_OFFSET( 8444, glUniformMatrix3x2fv, glUniformMatrix3x2fv, NULL, 566),
    NAME_FUNC_OFFSET( 8465, glUniformMatrix3x4fv, glUniformMatrix3x4fv, NULL, 567),
    NAME_FUNC_OFFSET( 8486, glUniformMatrix4x2fv, glUniformMatrix4x2fv, NULL, 568),
    NAME_FUNC_OFFSET( 8507, glUniformMatrix4x3fv, glUniformMatrix4x3fv, NULL, 569),
    NAME_FUNC_OFFSET( 8528, glBeginConditionalRender, glBeginConditionalRender, NULL, 570),
    NAME_FUNC_OFFSET( 8553, glBeginTransformFeedback, glBeginTransformFeedback, NULL, 571),
    NAME_FUNC_OFFSET( 8578, glBindBufferBase, glBindBufferBase, NULL, 572),
    NAME_FUNC_OFFSET( 8595, glBindBufferRange, glBindBufferRange, NULL, 573),
    NAME_FUNC_OFFSET( 8613, glBindFragDataLocation, glBindFragDataLocation, NULL, 574),
    NAME_FUNC_OFFSET( 8636, glClampColor, glClampColor, NULL, 575),
    NAME_FUNC_OFFSET( 8649, glClearBufferfi, glClearBufferfi, NULL, 576),
    NAME_FUNC_OFFSET( 8665, glClearBufferfv, glClearBufferfv, NULL, 577),
    NAME_FUNC_OFFSET( 8681, glClearBufferiv, glClearBufferiv, NULL, 578),
    NAME_FUNC_OFFSET( 8697, glClearBufferuiv, glClearBufferuiv, NULL, 579),
    NAME_FUNC_OFFSET( 8714, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET( 8727, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET( 8738, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET( 8748, glEndConditionalRender, glEndConditionalRender, NULL, 583),
    NAME_FUNC_OFFSET( 8771, glEndTransformFeedback, glEndTransformFeedback, NULL, 584),
    NAME_FUNC_OFFSET( 8794, glGetBooleani_v, glGetBooleani_v, NULL, 585),
    NAME_FUNC_OFFSET( 8810, glGetFragDataLocation, glGetFragDataLocation, NULL, 586),
    NAME_FUNC_OFFSET( 8832, glGetIntegeri_v, glGetIntegeri_v, NULL, 587),
    NAME_FUNC_OFFSET( 8848, glGetStringi, glGetStringi, NULL, 588),
    NAME_FUNC_OFFSET( 8861, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET( 8882, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET( 8904, glGetTransformFeedbackVarying, glGetTransformFeedbackVarying, NULL, 591),
    NAME_FUNC_OFFSET( 8934, glGetUniformuiv, glGetUniformuiv, NULL, 592),
    NAME_FUNC_OFFSET( 8950, glGetVertexAttribIiv, glGetVertexAttribIiv, NULL, 593),
    NAME_FUNC_OFFSET( 8971, glGetVertexAttribIuiv, glGetVertexAttribIuiv, NULL, 594),
    NAME_FUNC_OFFSET( 8993, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET( 9006, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET( 9024, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET( 9043, glTransformFeedbackVaryings, glTransformFeedbackVaryings, NULL, 598),
    NAME_FUNC_OFFSET( 9071, glUniform1ui, glUniform1ui, NULL, 599),
    NAME_FUNC_OFFSET( 9084, glUniform1uiv, glUniform1uiv, NULL, 600),
    NAME_FUNC_OFFSET( 9098, glUniform2ui, glUniform2ui, NULL, 601),
    NAME_FUNC_OFFSET( 9111, glUniform2uiv, glUniform2uiv, NULL, 602),
    NAME_FUNC_OFFSET( 9125, glUniform3ui, glUniform3ui, NULL, 603),
    NAME_FUNC_OFFSET( 9138, glUniform3uiv, glUniform3uiv, NULL, 604),
    NAME_FUNC_OFFSET( 9152, glUniform4ui, glUniform4ui, NULL, 605),
    NAME_FUNC_OFFSET( 9165, glUniform4uiv, glUniform4uiv, NULL, 606),
    NAME_FUNC_OFFSET( 9179, glVertexAttribI1iv, glVertexAttribI1iv, NULL, 607),
    NAME_FUNC_OFFSET( 9198, glVertexAttribI1uiv, glVertexAttribI1uiv, NULL, 608),
    NAME_FUNC_OFFSET( 9218, glVertexAttribI4bv, glVertexAttribI4bv, NULL, 609),
    NAME_FUNC_OFFSET( 9237, glVertexAttribI4sv, glVertexAttribI4sv, NULL, 610),
    NAME_FUNC_OFFSET( 9256, glVertexAttribI4ubv, glVertexAttribI4ubv, NULL, 611),
    NAME_FUNC_OFFSET( 9276, glVertexAttribI4usv, glVertexAttribI4usv, NULL, 612),
    NAME_FUNC_OFFSET( 9296, glVertexAttribIPointer, glVertexAttribIPointer, NULL, 613),
    NAME_FUNC_OFFSET( 9319, glPrimitiveRestartIndex, glPrimitiveRestartIndex, NULL, 614),
    NAME_FUNC_OFFSET( 9343, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET( 9355, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET( 9376, glGetBufferParameteri64v, glGetBufferParameteri64v, NULL, 617),
    NAME_FUNC_OFFSET( 9401, glGetInteger64i_v, glGetInteger64i_v, NULL, 618),
    NAME_FUNC_OFFSET( 9419, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET( 9441, glMinSampleShading, glMinSampleShading, NULL, 620),
    NAME_FUNC_OFFSET( 9460, glMemoryBarrierByRegion, glMemoryBarrierByRegion, NULL, 621),
    NAME_FUNC_OFFSET( 9484, glBindProgramARB, glBindProgramARB, NULL, 622),
    NAME_FUNC_OFFSET( 9501, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 623),
    NAME_FUNC_OFFSET( 9521, glGenProgramsARB, glGenProgramsARB, NULL, 624),
    NAME_FUNC_OFFSET( 9538, glGetProgramEnvParameterdvARB, glGetProgramEnvParameterdvARB, NULL, 625),
    NAME_FUNC_OFFSET( 9568, glGetProgramEnvParameterfvARB, glGetProgramEnvParameterfvARB, NULL, 626),
    NAME_FUNC_OFFSET( 9598, glGetProgramLocalParameterdvARB, glGetProgramLocalParameterdvARB, NULL, 627),
    NAME_FUNC_OFFSET( 9630, glGetProgramLocalParameterfvARB, glGetProgramLocalParameterfvARB, NULL, 628),
    NAME_FUNC_OFFSET( 9662, glGetProgramStringARB, glGetProgramStringARB, NULL, 629),
    NAME_FUNC_OFFSET( 9684, glGetProgramivARB, glGetProgramivARB, NULL, 630),
    NAME_FUNC_OFFSET( 9702, glIsProgramARB, glIsProgramARB, NULL, 631),
    NAME_FUNC_OFFSET( 9717, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 632),
    NAME_FUNC_OFFSET( 9744, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 633),
    NAME_FUNC_OFFSET( 9772, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 634),
    NAME_FUNC_OFFSET( 9799, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 635),
    NAME_FUNC_OFFSET( 9827, glProgramLocalParameter4dARB, glProgramLocalParameter4dARB, NULL, 636),
    NAME_FUNC_OFFSET( 9856, glProgramLocalParameter4dvARB, glProgramLocalParameter4dvARB, NULL, 637),
    NAME_FUNC_OFFSET( 9886, glProgramLocalParameter4fARB, glProgramLocalParameter4fARB, NULL, 638),
    NAME_FUNC_OFFSET( 9915, glProgramLocalParameter4fvARB, glProgramLocalParameter4fvARB, NULL, 639),
    NAME_FUNC_OFFSET( 9945, glProgramStringARB, glProgramStringARB, NULL, 640),
    NAME_FUNC_OFFSET( 9964, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 641),
    NAME_FUNC_OFFSET( 9984, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 642),
    NAME_FUNC_OFFSET(10005, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 643),
    NAME_FUNC_OFFSET(10025, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 644),
    NAME_FUNC_OFFSET(10046, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 645),
    NAME_FUNC_OFFSET(10066, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 646),
    NAME_FUNC_OFFSET(10087, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 647),
    NAME_FUNC_OFFSET(10107, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 648),
    NAME_FUNC_OFFSET(10128, glAttachObjectARB, glAttachObjectARB, NULL, 649),
    NAME_FUNC_OFFSET(10146, glCreateProgramObjectARB, glCreateProgramObjectARB, NULL, 650),
    NAME_FUNC_OFFSET(10171, glCreateShaderObjectARB, glCreateShaderObjectARB, NULL, 651),
    NAME_FUNC_OFFSET(10195, glDeleteObjectARB, glDeleteObjectARB, NULL, 652),
    NAME_FUNC_OFFSET(10213, glDetachObjectARB, glDetachObjectARB, NULL, 653),
    NAME_FUNC_OFFSET(10231, glGetAttachedObjectsARB, glGetAttachedObjectsARB, NULL, 654),
    NAME_FUNC_OFFSET(10255, glGetHandleARB, glGetHandleARB, NULL, 655),
    NAME_FUNC_OFFSET(10270, glGetInfoLogARB, glGetInfoLogARB, NULL, 656),
    NAME_FUNC_OFFSET(10286, glGetObjectParameterfvARB, glGetObjectParameterfvARB, NULL, 657),
    NAME_FUNC_OFFSET(10312, glGetObjectParameterivARB, glGetObjectParameterivARB, NULL, 658),
    NAME_FUNC_OFFSET(10338, glDrawArraysInstanced, glDrawArraysInstanced, NULL, 659),
    NAME_FUNC_OFFSET(10360, glDrawElementsInstanced, glDrawElementsInstanced, NULL, 660),
    NAME_FUNC_OFFSET(10384, glBindFramebuffer, glBindFramebuffer, NULL, 661),
    NAME_FUNC_OFFSET(10402, glBindRenderbuffer, glBindRenderbuffer, NULL, 662),
    NAME_FUNC_OFFSET(10421, glBlitFramebuffer, glBlitFramebuffer, NULL, 663),
    NAME_FUNC_OFFSET(10439, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 664),
    NAME_FUNC_OFFSET(10464, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 665),
    NAME_FUNC_OFFSET(10485, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 666),
    NAME_FUNC_OFFSET(10507, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 667),
    NAME_FUNC_OFFSET(10533, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 668),
    NAME_FUNC_OFFSET(10556, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 669),
    NAME_FUNC_OFFSET(10579, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 670),
    NAME_FUNC_OFFSET(10602, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 671),
    NAME_FUNC_OFFSET(10628, glGenFramebuffers, glGenFramebuffers, NULL, 672),
    NAME_FUNC_OFFSET(10646, glGenRenderbuffers, glGenRenderbuffers, NULL, 673),
    NAME_FUNC_OFFSET(10665, glGenerateMipmap, glGenerateMipmap, NULL, 674),
    NAME_FUNC_OFFSET(10682, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(10720, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 676),
    NAME_FUNC_OFFSET(10749, glIsFramebuffer, glIsFramebuffer, NULL, 677),
    NAME_FUNC_OFFSET(10765, glIsRenderbuffer, glIsRenderbuffer, NULL, 678),
    NAME_FUNC_OFFSET(10782, glRenderbufferStorage, glRenderbufferStorage, NULL, 679),
    NAME_FUNC_OFFSET(10804, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 680),
    NAME_FUNC_OFFSET(10837, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(10862, glMapBufferRange, glMapBufferRange, NULL, 682),
    NAME_FUNC_OFFSET(10879, glBindVertexArray, glBindVertexArray, NULL, 683),
    NAME_FUNC_OFFSET(10897, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(10918, glGenVertexArrays, glGenVertexArrays, NULL, 685),
    NAME_FUNC_OFFSET(10936, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(10952, glGetActiveUniformBlockName, glGetActiveUniformBlockName, NULL, 687),
    NAME_FUNC_OFFSET(10980, glGetActiveUniformBlockiv, glGetActiveUniformBlockiv, NULL, 688),
    NAME_FUNC_OFFSET(11006, glGetActiveUniformName, glGetActiveUniformName, NULL, 689),
    NAME_FUNC_OFFSET(11029, glGetActiveUniformsiv, glGetActiveUniformsiv, NULL, 690),
    NAME_FUNC_OFFSET(11051, glGetUniformBlockIndex, glGetUniformBlockIndex, NULL, 691),
    NAME_FUNC_OFFSET(11074, glGetUniformIndices, glGetUniformIndices, NULL, 692),
    NAME_FUNC_OFFSET(11094, glUniformBlockBinding, glUniformBlockBinding, NULL, 693),
    NAME_FUNC_OFFSET(11116, glCopyBufferSubData, glCopyBufferSubData, NULL, 694),
    NAME_FUNC_OFFSET(11136, glClientWaitSync, glClientWaitSync, NULL, 695),
    NAME_FUNC_OFFSET(11153, glDeleteSync, glDeleteSync, NULL, 696),
    NAME_FUNC_OFFSET(11166, glFenceSync, glFenceSync, NULL, 697),
    NAME_FUNC_OFFSET(11178, glGetInteger64v, glGetInteger64v, NULL, 698),
    NAME_FUNC_OFFSET(11194, glGetSynciv, glGetSynciv, NULL, 699),
    NAME_FUNC_OFFSET(11206, glIsSync, glIsSync, NULL, 700),
    NAME_FUNC_OFFSET(11215, glWaitSync, glWaitSync, NULL, 701),
    NAME_FUNC_OFFSET(11226, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 702),
    NAME_FUNC_OFFSET(11251, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 703),
    NAME_FUNC_OFFSET(11285, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(11315, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 705),
    NAME_FUNC_OFFSET(11345, glProvokingVertex, glProvokingVertex, NULL, 706),
    NAME_FUNC_OFFSET(11363, glGetMultisamplefv, glGetMultisamplefv, NULL, 707),
    NAME_FUNC_OFFSET(11382, glSampleMaski, glSampleMaski, NULL, 708),
    NAME_FUNC_OFFSET(11396, glTexImage2DMultisample, glTexImage2DMultisample, NULL, 709),
    NAME_FUNC_OFFSET(11420, glTexImage3DMultisample, glTexImage3DMultisample, NULL, 710),
    NAME_FUNC_OFFSET(11444, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(11472, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(11492, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(11516, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(11532, glBindFragDataLocationIndexed, glBindFragDataLocationIndexed, NULL, 715),
    NAME_FUNC_OFFSET(11562, glGetFragDataIndex, glGetFragDataIndex, NULL, 716),
    NAME_FUNC_OFFSET(11581, glBindSampler, glBindSampler, NULL, 717),
    NAME_FUNC_OFFSET(11595, glDeleteSamplers, glDeleteSamplers, NULL, 718),
    NAME_FUNC_OFFSET(11612, glGenSamplers, glGenSamplers, NULL, 719),
    NAME_FUNC_OFFSET(11626, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 720),
    NAME_FUNC_OFFSET(11651, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 721),
    NAME_FUNC_OFFSET(11677, glGetSamplerParameterfv, glGetSamplerParameterfv, NULL, 722),
    NAME_FUNC_OFFSET(11701, glGetSamplerParameteriv, glGetSamplerParameteriv, NULL, 723),
    NAME_FUNC_OFFSET(11725, glIsSampler, glIsSampler, NULL, 724),
    NAME_FUNC_OFFSET(11737, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 725),
    NAME_FUNC_OFFSET(11759, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 726),
    NAME_FUNC_OFFSET(11782, glSamplerParameterf, glSamplerParameterf, NULL, 727),
    NAME_FUNC_OFFSET(11802, glSamplerParameterfv, glSamplerParameterfv, NULL, 728),
    NAME_FUNC_OFFSET(11823, glSamplerParameteri, glSamplerParameteri, NULL, 729),
    NAME_FUNC_OFFSET(11843, glSamplerParameteriv, glSamplerParameteriv, NULL, 730),
    NAME_FUNC_OFFSET(11864, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(11885, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(11907, gl_dispatch_stub_733, gl_dispatch_stub_733, NULL, 733),
    NAME_FUNC_OFFSET(11922, glColorP3ui, glColorP3ui, NULL, 734),
    NAME_FUNC_OFFSET(11934, glColorP3uiv, glColorP3uiv, NULL, 735),
    NAME_FUNC_OFFSET(11947, glColorP4ui, glColorP4ui, NULL, 736),
    NAME_FUNC_OFFSET(11959, glColorP4uiv, glColorP4uiv, NULL, 737),
    NAME_FUNC_OFFSET(11972, glMultiTexCoordP1ui, glMultiTexCoordP1ui, NULL, 738),
    NAME_FUNC_OFFSET(11992, glMultiTexCoordP1uiv, glMultiTexCoordP1uiv, NULL, 739),
    NAME_FUNC_OFFSET(12013, glMultiTexCoordP2ui, glMultiTexCoordP2ui, NULL, 740),
    NAME_FUNC_OFFSET(12033, glMultiTexCoordP2uiv, glMultiTexCoordP2uiv, NULL, 741),
    NAME_FUNC_OFFSET(12054, glMultiTexCoordP3ui, glMultiTexCoordP3ui, NULL, 742),
    NAME_FUNC_OFFSET(12074, glMultiTexCoordP3uiv, glMultiTexCoordP3uiv, NULL, 743),
    NAME_FUNC_OFFSET(12095, glMultiTexCoordP4ui, glMultiTexCoordP4ui, NULL, 744),
    NAME_FUNC_OFFSET(12115, glMultiTexCoordP4uiv, glMultiTexCoordP4uiv, NULL, 745),
    NAME_FUNC_OFFSET(12136, glNormalP3ui, glNormalP3ui, NULL, 746),
    NAME_FUNC_OFFSET(12149, glNormalP3uiv, glNormalP3uiv, NULL, 747),
    NAME_FUNC_OFFSET(12163, glSecondaryColorP3ui, glSecondaryColorP3ui, NULL, 748),
    NAME_FUNC_OFFSET(12184, glSecondaryColorP3uiv, glSecondaryColorP3uiv, NULL, 749),
    NAME_FUNC_OFFSET(12206, glTexCoordP1ui, glTexCoordP1ui, NULL, 750),
    NAME_FUNC_OFFSET(12221, glTexCoordP1uiv, glTexCoordP1uiv, NULL, 751),
    NAME_FUNC_OFFSET(12237, glTexCoordP2ui, glTexCoordP2ui, NULL, 752),
    NAME_FUNC_OFFSET(12252, glTexCoordP2uiv, glTexCoordP2uiv, NULL, 753),
    NAME_FUNC_OFFSET(12268, glTexCoordP3ui, glTexCoordP3ui, NULL, 754),
    NAME_FUNC_OFFSET(12283, glTexCoordP3uiv, glTexCoordP3uiv, NULL, 755),
    NAME_FUNC_OFFSET(12299, glTexCoordP4ui, glTexCoordP4ui, NULL, 756),
    NAME_FUNC_OFFSET(12314, glTexCoordP4uiv, glTexCoordP4uiv, NULL, 757),
    NAME_FUNC_OFFSET(12330, glVertexAttribP1ui, glVertexAttribP1ui, NULL, 758),
    NAME_FUNC_OFFSET(12349, glVertexAttribP1uiv, glVertexAttribP1uiv, NULL, 759),
    NAME_FUNC_OFFSET(12369, glVertexAttribP2ui, glVertexAttribP2ui, NULL, 760),
    NAME_FUNC_OFFSET(12388, glVertexAttribP2uiv, glVertexAttribP2uiv, NULL, 761),
    NAME_FUNC_OFFSET(12408, glVertexAttribP3ui, glVertexAttribP3ui, NULL, 762),
    NAME_FUNC_OFFSET(12427, glVertexAttribP3uiv, glVertexAttribP3uiv, NULL, 763),
    NAME_FUNC_OFFSET(12447, glVertexAttribP4ui, glVertexAttribP4ui, NULL, 764),
    NAME_FUNC_OFFSET(12466, glVertexAttribP4uiv, glVertexAttribP4uiv, NULL, 765),
    NAME_FUNC_OFFSET(12486, glVertexP2ui, glVertexP2ui, NULL, 766),
    NAME_FUNC_OFFSET(12499, glVertexP2uiv, glVertexP2uiv, NULL, 767),
    NAME_FUNC_OFFSET(12513, glVertexP3ui, glVertexP3ui, NULL, 768),
    NAME_FUNC_OFFSET(12526, glVertexP3uiv, glVertexP3uiv, NULL, 769),
    NAME_FUNC_OFFSET(12540, glVertexP4ui, glVertexP4ui, NULL, 770),
    NAME_FUNC_OFFSET(12553, glVertexP4uiv, glVertexP4uiv, NULL, 771),
    NAME_FUNC_OFFSET(12567, glDrawArraysIndirect, glDrawArraysIndirect, NULL, 772),
    NAME_FUNC_OFFSET(12588, glDrawElementsIndirect, glDrawElementsIndirect, NULL, 773),
    NAME_FUNC_OFFSET(12611, gl_dispatch_stub_774, gl_dispatch_stub_774, NULL, 774),
    NAME_FUNC_OFFSET(12626, gl_dispatch_stub_775, gl_dispatch_stub_775, NULL, 775),
    NAME_FUNC_OFFSET(12638, gl_dispatch_stub_776, gl_dispatch_stub_776, NULL, 776),
    NAME_FUNC_OFFSET(12651, gl_dispatch_stub_777, gl_dispatch_stub_777, NULL, 777),
    NAME_FUNC_OFFSET(12663, gl_dispatch_stub_778, gl_dispatch_stub_778, NULL, 778),
    NAME_FUNC_OFFSET(12676, gl_dispatch_stub_779, gl_dispatch_stub_779, NULL, 779),
    NAME_FUNC_OFFSET(12688, gl_dispatch_stub_780, gl_dispatch_stub_780, NULL, 780),
    NAME_FUNC_OFFSET(12701, gl_dispatch_stub_781, gl_dispatch_stub_781, NULL, 781),
    NAME_FUNC_OFFSET(12713, gl_dispatch_stub_782, gl_dispatch_stub_782, NULL, 782),
    NAME_FUNC_OFFSET(12726, gl_dispatch_stub_783, gl_dispatch_stub_783, NULL, 783),
    NAME_FUNC_OFFSET(12745, gl_dispatch_stub_784, gl_dispatch_stub_784, NULL, 784),
    NAME_FUNC_OFFSET(12766, gl_dispatch_stub_785, gl_dispatch_stub_785, NULL, 785),
    NAME_FUNC_OFFSET(12787, gl_dispatch_stub_786, gl_dispatch_stub_786, NULL, 786),
    NAME_FUNC_OFFSET(12806, gl_dispatch_stub_787, gl_dispatch_stub_787, NULL, 787),
    NAME_FUNC_OFFSET(12827, gl_dispatch_stub_788, gl_dispatch_stub_788, NULL, 788),
    NAME_FUNC_OFFSET(12848, gl_dispatch_stub_789, gl_dispatch_stub_789, NULL, 789),
    NAME_FUNC_OFFSET(12867, gl_dispatch_stub_790, gl_dispatch_stub_790, NULL, 790),
    NAME_FUNC_OFFSET(12888, gl_dispatch_stub_791, gl_dispatch_stub_791, NULL, 791),
    NAME_FUNC_OFFSET(12909, gl_dispatch_stub_792, gl_dispatch_stub_792, NULL, 792),
    NAME_FUNC_OFFSET(12935, gl_dispatch_stub_793, gl_dispatch_stub_793, NULL, 793),
    NAME_FUNC_OFFSET(12968, gl_dispatch_stub_794, gl_dispatch_stub_794, NULL, 794),
    NAME_FUNC_OFFSET(12999, gl_dispatch_stub_795, gl_dispatch_stub_795, NULL, 795),
    NAME_FUNC_OFFSET(13019, gl_dispatch_stub_796, gl_dispatch_stub_796, NULL, 796),
    NAME_FUNC_OFFSET(13040, gl_dispatch_stub_797, gl_dispatch_stub_797, NULL, 797),
    NAME_FUNC_OFFSET(13071, gl_dispatch_stub_798, gl_dispatch_stub_798, NULL, 798),
    NAME_FUNC_OFFSET(13097, gl_dispatch_stub_799, gl_dispatch_stub_799, NULL, 799),
    NAME_FUNC_OFFSET(13121, gl_dispatch_stub_800, gl_dispatch_stub_800, NULL, 800),
    NAME_FUNC_OFFSET(13140, glPatchParameteri, glPatchParameteri, NULL, 801),
    NAME_FUNC_OFFSET(13158, glBindTransformFeedback, glBindTransformFeedback, NULL, 802),
    NAME_FUNC_OFFSET(13182, glDeleteTransformFeedbacks, glDeleteTransformFeedbacks, NULL, 803),
    NAME_FUNC_OFFSET(13209, glDrawTransformFeedback, glDrawTransformFeedback, NULL, 804),
    NAME_FUNC_OFFSET(13233, glGenTransformFeedbacks, glGenTransformFeedbacks, NULL, 805),
    NAME_FUNC_OFFSET(13257, glIsTransformFeedback, glIsTransformFeedback, NULL, 806),
    NAME_FUNC_OFFSET(13279, glPauseTransformFeedback, glPauseTransformFeedback, NULL, 807),
    NAME_FUNC_OFFSET(13304, glResumeTransformFeedback, glResumeTransformFeedback, NULL, 808),
    NAME_FUNC_OFFSET(13330, glBeginQueryIndexed, glBeginQueryIndexed, NULL, 809),
    NAME_FUNC_OFFSET(13350, glDrawTransformFeedbackStream, glDrawTransformFeedbackStream, NULL, 810),
    NAME_FUNC_OFFSET(13380, glEndQueryIndexed, glEndQueryIndexed, NULL, 811),
    NAME_FUNC_OFFSET(13398, glGetQueryIndexediv, glGetQueryIndexediv, NULL, 812),
    NAME_FUNC_OFFSET(13418, glClearDepthf, glClearDepthf, NULL, 813),
    NAME_FUNC_OFFSET(13432, glDepthRangef, glDepthRangef, NULL, 814),
    NAME_FUNC_OFFSET(13446, glGetShaderPrecisionFormat, glGetShaderPrecisionFormat, NULL, 815),
    NAME_FUNC_OFFSET(13473, glReleaseShaderCompiler, glReleaseShaderCompiler, NULL, 816),
    NAME_FUNC_OFFSET(13497, glShaderBinary, glShaderBinary, NULL, 817),
    NAME_FUNC_OFFSET(13512, glGetProgramBinary, glGetProgramBinary, NULL, 818),
    NAME_FUNC_OFFSET(13531, glProgramBinary, glProgramBinary, NULL, 819),
    NAME_FUNC_OFFSET(13547, glProgramParameteri, glProgramParameteri, NULL, 820),
    NAME_FUNC_OFFSET(13567, gl_dispatch_stub_821, gl_dispatch_stub_821, NULL, 821),
    NAME_FUNC_OFFSET(13588, gl_dispatch_stub_822, gl_dispatch_stub_822, NULL, 822),
    NAME_FUNC_OFFSET(13606, gl_dispatch_stub_823, gl_dispatch_stub_823, NULL, 823),
    NAME_FUNC_OFFSET(13625, gl_dispatch_stub_824, gl_dispatch_stub_824, NULL, 824),
    NAME_FUNC_OFFSET(13643, gl_dispatch_stub_825, gl_dispatch_stub_825, NULL, 825),
    NAME_FUNC_OFFSET(13662, gl_dispatch_stub_826, gl_dispatch_stub_826, NULL, 826),
    NAME_FUNC_OFFSET(13680, gl_dispatch_stub_827, gl_dispatch_stub_827, NULL, 827),
    NAME_FUNC_OFFSET(13699, gl_dispatch_stub_828, gl_dispatch_stub_828, NULL, 828),
    NAME_FUNC_OFFSET(13717, gl_dispatch_stub_829, gl_dispatch_stub_829, NULL, 829),
    NAME_FUNC_OFFSET(13736, gl_dispatch_stub_830, gl_dispatch_stub_830, NULL, 830),
    NAME_FUNC_OFFSET(13759, glDepthRangeArrayv, glDepthRangeArrayv, NULL, 831),
    NAME_FUNC_OFFSET(13778, glDepthRangeIndexed, glDepthRangeIndexed, NULL, 832),
    NAME_FUNC_OFFSET(13798, glGetDoublei_v, glGetDoublei_v, NULL, 833),
    NAME_FUNC_OFFSET(13813, glGetFloati_v, glGetFloati_v, NULL, 834),
    NAME_FUNC_OFFSET(13827, glScissorArrayv, glScissorArrayv, NULL, 835),
    NAME_FUNC_OFFSET(13843, glScissorIndexed, glScissorIndexed, NULL, 836),
    NAME_FUNC_OFFSET(13860, glScissorIndexedv, glScissorIndexedv, NULL, 837),
    NAME_FUNC_OFFSET(13878, glViewportArrayv, glViewportArrayv, NULL, 838),
    NAME_FUNC_OFFSET(13895, glViewportIndexedf, glViewportIndexedf, NULL, 839),
    NAME_FUNC_OFFSET(13914, glViewportIndexedfv, glViewportIndexedfv, NULL, 840),
    NAME_FUNC_OFFSET(13934, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 841),
    NAME_FUNC_OFFSET(13962, glGetnColorTableARB, glGetnColorTableARB, NULL, 842),
    NAME_FUNC_OFFSET(13982, glGetnCompressedTexImageARB, glGetnCompressedTexImageARB, NULL, 843),
    NAME_FUNC_OFFSET(14010, glGetnConvolutionFilterARB, glGetnConvolutionFilterARB, NULL, 844),
    NAME_FUNC_OFFSET(14037, glGetnHistogramARB, glGetnHistogramARB, NULL, 845),
    NAME_FUNC_OFFSET(14056, glGetnMapdvARB, glGetnMapdvARB, NULL, 846),
    NAME_FUNC_OFFSET(14071, glGetnMapfvARB, glGetnMapfvARB, NULL, 847),
    NAME_FUNC_OFFSET(14086, glGetnMapivARB, glGetnMapivARB, NULL, 848),
    NAME_FUNC_OFFSET(14101, glGetnMinmaxARB, glGetnMinmaxARB, NULL, 849),
    NAME_FUNC_OFFSET(14117, glGetnPixelMapfvARB, glGetnPixelMapfvARB, NULL, 850),
    NAME_FUNC_OFFSET(14137, glGetnPixelMapuivARB, glGetnPixelMapuivARB, NULL, 851),
    NAME_FUNC_OFFSET(14158, glGetnPixelMapusvARB, glGetnPixelMapusvARB, NULL, 852),
    NAME_FUNC_OFFSET(14179, glGetnPolygonStippleARB, glGetnPolygonStippleARB, NULL, 853),
    NAME_FUNC_OFFSET(14203, glGetnSeparableFilterARB, glGetnSeparableFilterARB, NULL, 854),
    NAME_FUNC_OFFSET(14228, glGetnTexImageARB, glGetnTexImageARB, NULL, 855),
    NAME_FUNC_OFFSET(14246, glGetnUniformdvARB, glGetnUniformdvARB, NULL, 856),
    NAME_FUNC_OFFSET(14265, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 857),
    NAME_FUNC_OFFSET(14284, glGetnUniformivARB, glGetnUniformivARB, NULL, 858),
    NAME_FUNC_OFFSET(14303, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 859),
    NAME_FUNC_OFFSET(14323, glReadnPixelsARB, glReadnPixelsARB, NULL, 860),
    NAME_FUNC_OFFSET(14340, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 861),
    NAME_FUNC_OFFSET(14374, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 862),
    NAME_FUNC_OFFSET(14410, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 863),
    NAME_FUNC_OFFSET(14456, glDrawTransformFeedbackInstanced, glDrawTransformFeedbackInstanced, NULL, 864),
    NAME_FUNC_OFFSET(14489, glDrawTransformFeedbackStreamInstanced, glDrawTransformFeedbackStreamInstanced, NULL, 865),
    NAME_FUNC_OFFSET(14528, gl_dispatch_stub_866, gl_dispatch_stub_866, NULL, 866),
    NAME_FUNC_OFFSET(14550, glGetActiveAtomicCounterBufferiv, glGetActiveAtomicCounterBufferiv, NULL, 867),
    NAME_FUNC_OFFSET(14583, glBindImageTexture, glBindImageTexture, NULL, 868),
    NAME_FUNC_OFFSET(14602, glMemoryBarrier, glMemoryBarrier, NULL, 869),
    NAME_FUNC_OFFSET(14618, glTexStorage1D, glTexStorage1D, NULL, 870),
    NAME_FUNC_OFFSET(14633, glTexStorage2D, glTexStorage2D, NULL, 871),
    NAME_FUNC_OFFSET(14648, glTexStorage3D, glTexStorage3D, NULL, 872),
    NAME_FUNC_OFFSET(14663, glTextureStorage1DEXT, glTextureStorage1DEXT, NULL, 873),
    NAME_FUNC_OFFSET(14685, glTextureStorage2DEXT, glTextureStorage2DEXT, NULL, 874),
    NAME_FUNC_OFFSET(14707, glTextureStorage3DEXT, glTextureStorage3DEXT, NULL, 875),
    NAME_FUNC_OFFSET(14729, glClearBufferData, glClearBufferData, NULL, 876),
    NAME_FUNC_OFFSET(14747, glClearBufferSubData, glClearBufferSubData, NULL, 877),
    NAME_FUNC_OFFSET(14768, glDispatchCompute, glDispatchCompute, NULL, 878),
    NAME_FUNC_OFFSET(14786, glDispatchComputeIndirect, glDispatchComputeIndirect, NULL, 879),
    NAME_FUNC_OFFSET(14812, glCopyImageSubData, glCopyImageSubData, NULL, 880),
    NAME_FUNC_OFFSET(14831, glTextureView, glTextureView, NULL, 881),
    NAME_FUNC_OFFSET(14845, glBindVertexBuffer, glBindVertexBuffer, NULL, 882),
    NAME_FUNC_OFFSET(14864, glVertexAttribBinding, glVertexAttribBinding, NULL, 883),
    NAME_FUNC_OFFSET(14886, glVertexAttribFormat, glVertexAttribFormat, NULL, 884),
    NAME_FUNC_OFFSET(14907, glVertexAttribIFormat, glVertexAttribIFormat, NULL, 885),
    NAME_FUNC_OFFSET(14929, glVertexAttribLFormat, glVertexAttribLFormat, NULL, 886),
    NAME_FUNC_OFFSET(14951, glVertexBindingDivisor, glVertexBindingDivisor, NULL, 887),
    NAME_FUNC_OFFSET(14974, glFramebufferParameteri, glFramebufferParameteri, NULL, 888),
    NAME_FUNC_OFFSET(14998, glGetFramebufferParameteriv, glGetFramebufferParameteriv, NULL, 889),
    NAME_FUNC_OFFSET(15026, gl_dispatch_stub_890, gl_dispatch_stub_890, NULL, 890),
    NAME_FUNC_OFFSET(15050, glMultiDrawArraysIndirect, glMultiDrawArraysIndirect, NULL, 891),
    NAME_FUNC_OFFSET(15076, glMultiDrawElementsIndirect, glMultiDrawElementsIndirect, NULL, 892),
    NAME_FUNC_OFFSET(15104, glGetProgramInterfaceiv, glGetProgramInterfaceiv, NULL, 893),
    NAME_FUNC_OFFSET(15128, glGetProgramResourceIndex, glGetProgramResourceIndex, NULL, 894),
    NAME_FUNC_OFFSET(15154, glGetProgramResourceLocation, glGetProgramResourceLocation, NULL, 895),
    NAME_FUNC_OFFSET(15183, gl_dispatch_stub_896, gl_dispatch_stub_896, NULL, 896),
    NAME_FUNC_OFFSET(15217, glGetProgramResourceName, glGetProgramResourceName, NULL, 897),
    NAME_FUNC_OFFSET(15242, glGetProgramResourceiv, glGetProgramResourceiv, NULL, 898),
    NAME_FUNC_OFFSET(15265, gl_dispatch_stub_899, gl_dispatch_stub_899, NULL, 899),
    NAME_FUNC_OFFSET(15293, glTexBufferRange, glTexBufferRange, NULL, 900),
    NAME_FUNC_OFFSET(15310, glTexStorage2DMultisample, glTexStorage2DMultisample, NULL, 901),
    NAME_FUNC_OFFSET(15336, glTexStorage3DMultisample, glTexStorage3DMultisample, NULL, 902),
    NAME_FUNC_OFFSET(15362, glBufferStorage, glBufferStorage, NULL, 903),
    NAME_FUNC_OFFSET(15378, glClearTexImage, glClearTexImage, NULL, 904),
    NAME_FUNC_OFFSET(15394, glClearTexSubImage, glClearTexSubImage, NULL, 905),
    NAME_FUNC_OFFSET(15413, glBindBuffersBase, glBindBuffersBase, NULL, 906),
    NAME_FUNC_OFFSET(15431, glBindBuffersRange, glBindBuffersRange, NULL, 907),
    NAME_FUNC_OFFSET(15450, glBindImageTextures, glBindImageTextures, NULL, 908),
    NAME_FUNC_OFFSET(15470, glBindSamplers, glBindSamplers, NULL, 909),
    NAME_FUNC_OFFSET(15485, glBindTextures, glBindTextures, NULL, 910),
    NAME_FUNC_OFFSET(15500, glBindVertexBuffers, glBindVertexBuffers, NULL, 911),
    NAME_FUNC_OFFSET(15520, gl_dispatch_stub_912, gl_dispatch_stub_912, NULL, 912),
    NAME_FUNC_OFFSET(15540, gl_dispatch_stub_913, gl_dispatch_stub_913, NULL, 913),
    NAME_FUNC_OFFSET(15562, gl_dispatch_stub_914, gl_dispatch_stub_914, NULL, 914),
    NAME_FUNC_OFFSET(15591, gl_dispatch_stub_915, gl_dispatch_stub_915, NULL, 915),
    NAME_FUNC_OFFSET(15618, gl_dispatch_stub_916, gl_dispatch_stub_916, NULL, 916),
    NAME_FUNC_OFFSET(15645, gl_dispatch_stub_917, gl_dispatch_stub_917, NULL, 917),
    NAME_FUNC_OFFSET(15674, gl_dispatch_stub_918, gl_dispatch_stub_918, NULL, 918),
    NAME_FUNC_OFFSET(15706, gl_dispatch_stub_919, gl_dispatch_stub_919, NULL, 919),
    NAME_FUNC_OFFSET(15735, gl_dispatch_stub_920, gl_dispatch_stub_920, NULL, 920),
    NAME_FUNC_OFFSET(15769, gl_dispatch_stub_921, gl_dispatch_stub_921, NULL, 921),
    NAME_FUNC_OFFSET(15800, gl_dispatch_stub_922, gl_dispatch_stub_922, NULL, 922),
    NAME_FUNC_OFFSET(15830, gl_dispatch_stub_923, gl_dispatch_stub_923, NULL, 923),
    NAME_FUNC_OFFSET(15861, gl_dispatch_stub_924, gl_dispatch_stub_924, NULL, 924),
    NAME_FUNC_OFFSET(15884, gl_dispatch_stub_925, gl_dispatch_stub_925, NULL, 925),
    NAME_FUNC_OFFSET(15908, gl_dispatch_stub_926, gl_dispatch_stub_926, NULL, 926),
    NAME_FUNC_OFFSET(15932, gl_dispatch_stub_927, gl_dispatch_stub_927, NULL, 927),
    NAME_FUNC_OFFSET(15957, gl_dispatch_stub_928, gl_dispatch_stub_928, NULL, 928),
    NAME_FUNC_OFFSET(15987, gl_dispatch_stub_929, gl_dispatch_stub_929, NULL, 929),
    NAME_FUNC_OFFSET(16021, gl_dispatch_stub_930, gl_dispatch_stub_930, NULL, 930),
    NAME_FUNC_OFFSET(16057, gl_dispatch_stub_931, gl_dispatch_stub_931, NULL, 931),
    NAME_FUNC_OFFSET(16071, gl_dispatch_stub_932, gl_dispatch_stub_932, NULL, 932),
    NAME_FUNC_OFFSET(16089, gl_dispatch_stub_933, gl_dispatch_stub_933, NULL, 933),
    NAME_FUNC_OFFSET(16112, gl_dispatch_stub_934, gl_dispatch_stub_934, NULL, 934),
    NAME_FUNC_OFFSET(16142, gl_dispatch_stub_935, gl_dispatch_stub_935, NULL, 935),
    NAME_FUNC_OFFSET(16165, gl_dispatch_stub_936, gl_dispatch_stub_936, NULL, 936),
    NAME_FUNC_OFFSET(16191, gl_dispatch_stub_937, gl_dispatch_stub_937, NULL, 937),
    NAME_FUNC_OFFSET(16217, gl_dispatch_stub_938, gl_dispatch_stub_938, NULL, 938),
    NAME_FUNC_OFFSET(16243, gl_dispatch_stub_939, gl_dispatch_stub_939, NULL, 939),
    NAME_FUNC_OFFSET(16269, gl_dispatch_stub_940, gl_dispatch_stub_940, NULL, 940),
    NAME_FUNC_OFFSET(16296, gl_dispatch_stub_941, gl_dispatch_stub_941, NULL, 941),
    NAME_FUNC_OFFSET(16326, gl_dispatch_stub_942, gl_dispatch_stub_942, NULL, 942),
    NAME_FUNC_OFFSET(16356, gl_dispatch_stub_943, gl_dispatch_stub_943, NULL, 943),
    NAME_FUNC_OFFSET(16386, gl_dispatch_stub_944, gl_dispatch_stub_944, NULL, 944),
    NAME_FUNC_OFFSET(16411, gl_dispatch_stub_945, gl_dispatch_stub_945, NULL, 945),
    NAME_FUNC_OFFSET(16435, gl_dispatch_stub_946, gl_dispatch_stub_946, NULL, 946),
    NAME_FUNC_OFFSET(16459, gl_dispatch_stub_947, gl_dispatch_stub_947, NULL, 947),
    NAME_FUNC_OFFSET(16483, gl_dispatch_stub_948, gl_dispatch_stub_948, NULL, 948),
    NAME_FUNC_OFFSET(16499, gl_dispatch_stub_949, gl_dispatch_stub_949, NULL, 949),
    NAME_FUNC_OFFSET(16520, gl_dispatch_stub_950, gl_dispatch_stub_950, NULL, 950),
    NAME_FUNC_OFFSET(16545, gl_dispatch_stub_951, gl_dispatch_stub_951, NULL, 951),
    NAME_FUNC_OFFSET(16561, gl_dispatch_stub_952, gl_dispatch_stub_952, NULL, 952),
    NAME_FUNC_OFFSET(16583, gl_dispatch_stub_953, gl_dispatch_stub_953, NULL, 953),
    NAME_FUNC_OFFSET(16600, gl_dispatch_stub_954, gl_dispatch_stub_954, NULL, 954),
    NAME_FUNC_OFFSET(16617, gl_dispatch_stub_955, gl_dispatch_stub_955, NULL, 955),
    NAME_FUNC_OFFSET(16644, gl_dispatch_stub_956, gl_dispatch_stub_956, NULL, 956),
    NAME_FUNC_OFFSET(16665, gl_dispatch_stub_957, gl_dispatch_stub_957, NULL, 957),
    NAME_FUNC_OFFSET(16692, gl_dispatch_stub_958, gl_dispatch_stub_958, NULL, 958),
    NAME_FUNC_OFFSET(16718, gl_dispatch_stub_959, gl_dispatch_stub_959, NULL, 959),
    NAME_FUNC_OFFSET(16748, gl_dispatch_stub_960, gl_dispatch_stub_960, NULL, 960),
    NAME_FUNC_OFFSET(16772, gl_dispatch_stub_961, gl_dispatch_stub_961, NULL, 961),
    NAME_FUNC_OFFSET(16800, gl_dispatch_stub_962, gl_dispatch_stub_962, NULL, 962),
    NAME_FUNC_OFFSET(16830, gl_dispatch_stub_963, gl_dispatch_stub_963, NULL, 963),
    NAME_FUNC_OFFSET(16858, gl_dispatch_stub_964, gl_dispatch_stub_964, NULL, 964),
    NAME_FUNC_OFFSET(16883, gl_dispatch_stub_965, gl_dispatch_stub_965, NULL, 965),
    NAME_FUNC_OFFSET(16907, gl_dispatch_stub_966, gl_dispatch_stub_966, NULL, 966),
    NAME_FUNC_OFFSET(16950, gl_dispatch_stub_967, gl_dispatch_stub_967, NULL, 967),
    NAME_FUNC_OFFSET(16983, gl_dispatch_stub_968, gl_dispatch_stub_968, NULL, 968),
    NAME_FUNC_OFFSET(17017, gl_dispatch_stub_969, gl_dispatch_stub_969, NULL, 969),
    NAME_FUNC_OFFSET(17044, gl_dispatch_stub_970, gl_dispatch_stub_970, NULL, 970),
    NAME_FUNC_OFFSET(17069, gl_dispatch_stub_971, gl_dispatch_stub_971, NULL, 971),
    NAME_FUNC_OFFSET(17097, gl_dispatch_stub_972, gl_dispatch_stub_972, NULL, 972),
    NAME_FUNC_OFFSET(17123, gl_dispatch_stub_973, gl_dispatch_stub_973, NULL, 973),
    NAME_FUNC_OFFSET(17141, gl_dispatch_stub_974, gl_dispatch_stub_974, NULL, 974),
    NAME_FUNC_OFFSET(17170, gl_dispatch_stub_975, gl_dispatch_stub_975, NULL, 975),
    NAME_FUNC_OFFSET(17199, gl_dispatch_stub_976, gl_dispatch_stub_976, NULL, 976),
    NAME_FUNC_OFFSET(17224, gl_dispatch_stub_977, gl_dispatch_stub_977, NULL, 977),
    NAME_FUNC_OFFSET(17250, gl_dispatch_stub_978, gl_dispatch_stub_978, NULL, 978),
    NAME_FUNC_OFFSET(17274, gl_dispatch_stub_979, gl_dispatch_stub_979, NULL, 979),
    NAME_FUNC_OFFSET(17298, gl_dispatch_stub_980, gl_dispatch_stub_980, NULL, 980),
    NAME_FUNC_OFFSET(17326, gl_dispatch_stub_981, gl_dispatch_stub_981, NULL, 981),
    NAME_FUNC_OFFSET(17352, gl_dispatch_stub_982, gl_dispatch_stub_982, NULL, 982),
    NAME_FUNC_OFFSET(17377, gl_dispatch_stub_983, gl_dispatch_stub_983, NULL, 983),
    NAME_FUNC_OFFSET(17405, gl_dispatch_stub_984, gl_dispatch_stub_984, NULL, 984),
    NAME_FUNC_OFFSET(17431, gl_dispatch_stub_985, gl_dispatch_stub_985, NULL, 985),
    NAME_FUNC_OFFSET(17450, gl_dispatch_stub_986, gl_dispatch_stub_986, NULL, 986),
    NAME_FUNC_OFFSET(17483, gl_dispatch_stub_987, gl_dispatch_stub_987, NULL, 987),
    NAME_FUNC_OFFSET(17519, gl_dispatch_stub_988, gl_dispatch_stub_988, NULL, 988),
    NAME_FUNC_OFFSET(17536, gl_dispatch_stub_989, gl_dispatch_stub_989, NULL, 989),
    NAME_FUNC_OFFSET(17558, gl_dispatch_stub_990, gl_dispatch_stub_990, NULL, 990),
    NAME_FUNC_OFFSET(17576, gl_dispatch_stub_991, gl_dispatch_stub_991, NULL, 991),
    NAME_FUNC_OFFSET(17597, gl_dispatch_stub_992, gl_dispatch_stub_992, NULL, 992),
    NAME_FUNC_OFFSET(17618, gl_dispatch_stub_993, gl_dispatch_stub_993, NULL, 993),
    NAME_FUNC_OFFSET(17647, gl_dispatch_stub_994, gl_dispatch_stub_994, NULL, 994),
    NAME_FUNC_OFFSET(17677, gl_dispatch_stub_995, gl_dispatch_stub_995, NULL, 995),
    NAME_FUNC_OFFSET(17706, gl_dispatch_stub_996, gl_dispatch_stub_996, NULL, 996),
    NAME_FUNC_OFFSET(17735, gl_dispatch_stub_997, gl_dispatch_stub_997, NULL, 997),
    NAME_FUNC_OFFSET(17766, gl_dispatch_stub_998, gl_dispatch_stub_998, NULL, 998),
    NAME_FUNC_OFFSET(17792, gl_dispatch_stub_999, gl_dispatch_stub_999, NULL, 999),
    NAME_FUNC_OFFSET(17823, gl_dispatch_stub_1000, gl_dispatch_stub_1000, NULL, 1000),
    NAME_FUNC_OFFSET(17850, gl_dispatch_stub_1001, gl_dispatch_stub_1001, NULL, 1001),
    NAME_FUNC_OFFSET(17888, gl_dispatch_stub_1002, gl_dispatch_stub_1002, NULL, 1002),
    NAME_FUNC_OFFSET(17904, gl_dispatch_stub_1003, gl_dispatch_stub_1003, NULL, 1003),
    NAME_FUNC_OFFSET(17925, gl_dispatch_stub_1004, gl_dispatch_stub_1004, NULL, 1004),
    NAME_FUNC_OFFSET(17947, gl_dispatch_stub_1005, gl_dispatch_stub_1005, NULL, 1005),
    NAME_FUNC_OFFSET(17970, gl_dispatch_stub_1006, gl_dispatch_stub_1006, NULL, 1006),
    NAME_FUNC_OFFSET(17990, gl_dispatch_stub_1007, gl_dispatch_stub_1007, NULL, 1007),
    NAME_FUNC_OFFSET(18011, gl_dispatch_stub_1008, gl_dispatch_stub_1008, NULL, 1008),
    NAME_FUNC_OFFSET(18031, gl_dispatch_stub_1009, gl_dispatch_stub_1009, NULL, 1009),
    NAME_FUNC_OFFSET(18052, gl_dispatch_stub_1010, gl_dispatch_stub_1010, NULL, 1010),
    NAME_FUNC_OFFSET(18071, gl_dispatch_stub_1011, gl_dispatch_stub_1011, NULL, 1011),
    NAME_FUNC_OFFSET(18090, gl_dispatch_stub_1012, gl_dispatch_stub_1012, NULL, 1012),
    NAME_FUNC_OFFSET(18120, gl_dispatch_stub_1013, gl_dispatch_stub_1013, NULL, 1013),
    NAME_FUNC_OFFSET(18139, gl_dispatch_stub_1014, gl_dispatch_stub_1014, NULL, 1014),
    NAME_FUNC_OFFSET(18169, gl_dispatch_stub_1015, gl_dispatch_stub_1015, NULL, 1015),
    NAME_FUNC_OFFSET(18189, gl_dispatch_stub_1016, gl_dispatch_stub_1016, NULL, 1016),
    NAME_FUNC_OFFSET(18209, gl_dispatch_stub_1017, gl_dispatch_stub_1017, NULL, 1017),
    NAME_FUNC_OFFSET(18229, gl_dispatch_stub_1018, gl_dispatch_stub_1018, NULL, 1018),
    NAME_FUNC_OFFSET(18259, gl_dispatch_stub_1019, gl_dispatch_stub_1019, NULL, 1019),
    NAME_FUNC_OFFSET(18290, gl_dispatch_stub_1020, gl_dispatch_stub_1020, NULL, 1020),
    NAME_FUNC_OFFSET(18312, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(18339, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(18365, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(18392, gl_dispatch_stub_1024, gl_dispatch_stub_1024, NULL, 1024),
    NAME_FUNC_OFFSET(18419, gl_dispatch_stub_1025, gl_dispatch_stub_1025, NULL, 1025),
    NAME_FUNC_OFFSET(18447, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(18474, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(18500, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(18527, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(18558, gl_dispatch_stub_1030, gl_dispatch_stub_1030, NULL, 1030),
    NAME_FUNC_OFFSET(18579, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(18605, gl_dispatch_stub_1032, gl_dispatch_stub_1032, NULL, 1032),
    NAME_FUNC_OFFSET(18636, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(18656, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(18677, gl_dispatch_stub_1035, gl_dispatch_stub_1035, NULL, 1035),
    NAME_FUNC_OFFSET(18698, gl_dispatch_stub_1036, gl_dispatch_stub_1036, NULL, 1036),
    NAME_FUNC_OFFSET(18720, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(18744, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(18769, gl_dispatch_stub_1039, gl_dispatch_stub_1039, NULL, 1039),
    NAME_FUNC_OFFSET(18794, gl_dispatch_stub_1040, gl_dispatch_stub_1040, NULL, 1040),
    NAME_FUNC_OFFSET(18820, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(18844, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(18869, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(18894, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(18920, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(18944, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(18969, gl_dispatch_stub_1047, gl_dispatch_stub_1047, NULL, 1047),
    NAME_FUNC_OFFSET(18994, gl_dispatch_stub_1048, gl_dispatch_stub_1048, NULL, 1048),
    NAME_FUNC_OFFSET(19020, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(19044, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(19069, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(19094, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(19120, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(19137, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(19155, gl_dispatch_stub_1055, gl_dispatch_stub_1055, NULL, 1055),
    NAME_FUNC_OFFSET(19173, gl_dispatch_stub_1056, gl_dispatch_stub_1056, NULL, 1056),
    NAME_FUNC_OFFSET(19192, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(19209, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(19227, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(19245, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(19264, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(19281, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(19299, gl_dispatch_stub_1063, gl_dispatch_stub_1063, NULL, 1063),
    NAME_FUNC_OFFSET(19317, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(19336, gl_dispatch_stub_1065, gl_dispatch_stub_1065, NULL, 1065),
    NAME_FUNC_OFFSET(19353, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(19371, gl_dispatch_stub_1067, gl_dispatch_stub_1067, NULL, 1067),
    NAME_FUNC_OFFSET(19389, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(19408, gl_dispatch_stub_1069, gl_dispatch_stub_1069, NULL, 1069),
    NAME_FUNC_OFFSET(19433, gl_dispatch_stub_1070, gl_dispatch_stub_1070, NULL, 1070),
    NAME_FUNC_OFFSET(19467, gl_dispatch_stub_1071, gl_dispatch_stub_1071, NULL, 1071),
    NAME_FUNC_OFFSET(19506, gl_dispatch_stub_1072, gl_dispatch_stub_1072, NULL, 1072),
    NAME_FUNC_OFFSET(19528, glInvalidateBufferData, glInvalidateBufferData, NULL, 1073),
    NAME_FUNC_OFFSET(19551, glInvalidateBufferSubData, glInvalidateBufferSubData, NULL, 1074),
    NAME_FUNC_OFFSET(19577, glInvalidateFramebuffer, glInvalidateFramebuffer, NULL, 1075),
    NAME_FUNC_OFFSET(19601, glInvalidateSubFramebuffer, glInvalidateSubFramebuffer, NULL, 1076),
    NAME_FUNC_OFFSET(19628, glInvalidateTexImage, glInvalidateTexImage, NULL, 1077),
    NAME_FUNC_OFFSET(19649, glInvalidateTexSubImage, glInvalidateTexSubImage, NULL, 1078),
    NAME_FUNC_OFFSET(19673, gl_dispatch_stub_1079, gl_dispatch_stub_1079, NULL, 1079),
    NAME_FUNC_OFFSET(19687, gl_dispatch_stub_1080, gl_dispatch_stub_1080, NULL, 1080),
    NAME_FUNC_OFFSET(19702, gl_dispatch_stub_1081, gl_dispatch_stub_1081, NULL, 1081),
    NAME_FUNC_OFFSET(19716, gl_dispatch_stub_1082, gl_dispatch_stub_1082, NULL, 1082),
    NAME_FUNC_OFFSET(19731, gl_dispatch_stub_1083, gl_dispatch_stub_1083, NULL, 1083),
    NAME_FUNC_OFFSET(19745, gl_dispatch_stub_1084, gl_dispatch_stub_1084, NULL, 1084),
    NAME_FUNC_OFFSET(19760, gl_dispatch_stub_1085, gl_dispatch_stub_1085, NULL, 1085),
    NAME_FUNC_OFFSET(19774, gl_dispatch_stub_1086, gl_dispatch_stub_1086, NULL, 1086),
    NAME_FUNC_OFFSET(19789, glPointSizePointerOES, glPointSizePointerOES, NULL, 1087),
    NAME_FUNC_OFFSET(19811, gl_dispatch_stub_1088, gl_dispatch_stub_1088, NULL, 1088),
    NAME_FUNC_OFFSET(19829, gl_dispatch_stub_1089, gl_dispatch_stub_1089, NULL, 1089),
    NAME_FUNC_OFFSET(19846, gl_dispatch_stub_1090, gl_dispatch_stub_1090, NULL, 1090),
    NAME_FUNC_OFFSET(19866, glColorPointerEXT, glColorPointerEXT, NULL, 1091),
    NAME_FUNC_OFFSET(19884, glEdgeFlagPointerEXT, glEdgeFlagPointerEXT, NULL, 1092),
    NAME_FUNC_OFFSET(19905, glIndexPointerEXT, glIndexPointerEXT, NULL, 1093),
    NAME_FUNC_OFFSET(19923, glNormalPointerEXT, glNormalPointerEXT, NULL, 1094),
    NAME_FUNC_OFFSET(19942, glTexCoordPointerEXT, glTexCoordPointerEXT, NULL, 1095),
    NAME_FUNC_OFFSET(19963, glVertexPointerEXT, glVertexPointerEXT, NULL, 1096),
    NAME_FUNC_OFFSET(19982, gl_dispatch_stub_1097, gl_dispatch_stub_1097, NULL, 1097),
    NAME_FUNC_OFFSET(20006, glActiveShaderProgram, glActiveShaderProgram, NULL, 1098),
    NAME_FUNC_OFFSET(20028, glBindProgramPipeline, glBindProgramPipeline, NULL, 1099),
    NAME_FUNC_OFFSET(20050, glCreateShaderProgramv, glCreateShaderProgramv, NULL, 1100),
    NAME_FUNC_OFFSET(20073, glDeleteProgramPipelines, glDeleteProgramPipelines, NULL, 1101),
    NAME_FUNC_OFFSET(20098, glGenProgramPipelines, glGenProgramPipelines, NULL, 1102),
    NAME_FUNC_OFFSET(20120, glGetProgramPipelineInfoLog, glGetProgramPipelineInfoLog, NULL, 1103),
    NAME_FUNC_OFFSET(20148, glGetProgramPipelineiv, glGetProgramPipelineiv, NULL, 1104),
    NAME_FUNC_OFFSET(20171, glIsProgramPipeline, glIsProgramPipeline, NULL, 1105),
    NAME_FUNC_OFFSET(20191, glLockArraysEXT, glLockArraysEXT, NULL, 1106),
    NAME_FUNC_OFFSET(20207, gl_dispatch_stub_1107, gl_dispatch_stub_1107, NULL, 1107),
    NAME_FUNC_OFFSET(20226, gl_dispatch_stub_1108, gl_dispatch_stub_1108, NULL, 1108),
    NAME_FUNC_OFFSET(20246, glProgramUniform1f, glProgramUniform1f, NULL, 1109),
    NAME_FUNC_OFFSET(20265, glProgramUniform1fv, glProgramUniform1fv, NULL, 1110),
    NAME_FUNC_OFFSET(20285, glProgramUniform1i, glProgramUniform1i, NULL, 1111),
    NAME_FUNC_OFFSET(20304, glProgramUniform1iv, glProgramUniform1iv, NULL, 1112),
    NAME_FUNC_OFFSET(20324, glProgramUniform1ui, glProgramUniform1ui, NULL, 1113),
    NAME_FUNC_OFFSET(20344, glProgramUniform1uiv, glProgramUniform1uiv, NULL, 1114),
    NAME_FUNC_OFFSET(20365, gl_dispatch_stub_1115, gl_dispatch_stub_1115, NULL, 1115),
    NAME_FUNC_OFFSET(20384, gl_dispatch_stub_1116, gl_dispatch_stub_1116, NULL, 1116),
    NAME_FUNC_OFFSET(20404, glProgramUniform2f, glProgramUniform2f, NULL, 1117),
    NAME_FUNC_OFFSET(20423, glProgramUniform2fv, glProgramUniform2fv, NULL, 1118),
    NAME_FUNC_OFFSET(20443, glProgramUniform2i, glProgramUniform2i, NULL, 1119),
    NAME_FUNC_OFFSET(20462, glProgramUniform2iv, glProgramUniform2iv, NULL, 1120),
    NAME_FUNC_OFFSET(20482, glProgramUniform2ui, glProgramUniform2ui, NULL, 1121),
    NAME_FUNC_OFFSET(20502, glProgramUniform2uiv, glProgramUniform2uiv, NULL, 1122),
    NAME_FUNC_OFFSET(20523, gl_dispatch_stub_1123, gl_dispatch_stub_1123, NULL, 1123),
    NAME_FUNC_OFFSET(20542, gl_dispatch_stub_1124, gl_dispatch_stub_1124, NULL, 1124),
    NAME_FUNC_OFFSET(20562, glProgramUniform3f, glProgramUniform3f, NULL, 1125),
    NAME_FUNC_OFFSET(20581, glProgramUniform3fv, glProgramUniform3fv, NULL, 1126),
    NAME_FUNC_OFFSET(20601, glProgramUniform3i, glProgramUniform3i, NULL, 1127),
    NAME_FUNC_OFFSET(20620, glProgramUniform3iv, glProgramUniform3iv, NULL, 1128),
    NAME_FUNC_OFFSET(20640, glProgramUniform3ui, glProgramUniform3ui, NULL, 1129),
    NAME_FUNC_OFFSET(20660, glProgramUniform3uiv, glProgramUniform3uiv, NULL, 1130),
    NAME_FUNC_OFFSET(20681, gl_dispatch_stub_1131, gl_dispatch_stub_1131, NULL, 1131),
    NAME_FUNC_OFFSET(20700, gl_dispatch_stub_1132, gl_dispatch_stub_1132, NULL, 1132),
    NAME_FUNC_OFFSET(20720, glProgramUniform4f, glProgramUniform4f, NULL, 1133),
    NAME_FUNC_OFFSET(20739, glProgramUniform4fv, glProgramUniform4fv, NULL, 1134),
    NAME_FUNC_OFFSET(20759, glProgramUniform4i, glProgramUniform4i, NULL, 1135),
    NAME_FUNC_OFFSET(20778, glProgramUniform4iv, glProgramUniform4iv, NULL, 1136),
    NAME_FUNC_OFFSET(20798, glProgramUniform4ui, glProgramUniform4ui, NULL, 1137),
    NAME_FUNC_OFFSET(20818, glProgramUniform4uiv, glProgramUniform4uiv, NULL, 1138),
    NAME_FUNC_OFFSET(20839, gl_dispatch_stub_1139, gl_dispatch_stub_1139, NULL, 1139),
    NAME_FUNC_OFFSET(20865, glProgramUniformMatrix2fv, glProgramUniformMatrix2fv, NULL, 1140),
    NAME_FUNC_OFFSET(20891, gl_dispatch_stub_1141, gl_dispatch_stub_1141, NULL, 1141),
    NAME_FUNC_OFFSET(20919, glProgramUniformMatrix2x3fv, glProgramUniformMatrix2x3fv, NULL, 1142),
    NAME_FUNC_OFFSET(20947, gl_dispatch_stub_1143, gl_dispatch_stub_1143, NULL, 1143),
    NAME_FUNC_OFFSET(20975, glProgramUniformMatrix2x4fv, glProgramUniformMatrix2x4fv, NULL, 1144),
    NAME_FUNC_OFFSET(21003, gl_dispatch_stub_1145, gl_dispatch_stub_1145, NULL, 1145),
    NAME_FUNC_OFFSET(21029, glProgramUniformMatrix3fv, glProgramUniformMatrix3fv, NULL, 1146),
    NAME_FUNC_OFFSET(21055, gl_dispatch_stub_1147, gl_dispatch_stub_1147, NULL, 1147),
    NAME_FUNC_OFFSET(21083, glProgramUniformMatrix3x2fv, glProgramUniformMatrix3x2fv, NULL, 1148),
    NAME_FUNC_OFFSET(21111, gl_dispatch_stub_1149, gl_dispatch_stub_1149, NULL, 1149),
    NAME_FUNC_OFFSET(21139, glProgramUniformMatrix3x4fv, glProgramUniformMatrix3x4fv, NULL, 1150),
    NAME_FUNC_OFFSET(21167, gl_dispatch_stub_1151, gl_dispatch_stub_1151, NULL, 1151),
    NAME_FUNC_OFFSET(21193, glProgramUniformMatrix4fv, glProgramUniformMatrix4fv, NULL, 1152),
    NAME_FUNC_OFFSET(21219, gl_dispatch_stub_1153, gl_dispatch_stub_1153, NULL, 1153),
    NAME_FUNC_OFFSET(21247, glProgramUniformMatrix4x2fv, glProgramUniformMatrix4x2fv, NULL, 1154),
    NAME_FUNC_OFFSET(21275, gl_dispatch_stub_1155, gl_dispatch_stub_1155, NULL, 1155),
    NAME_FUNC_OFFSET(21303, glProgramUniformMatrix4x3fv, glProgramUniformMatrix4x3fv, NULL, 1156),
    NAME_FUNC_OFFSET(21331, glUnlockArraysEXT, glUnlockArraysEXT, NULL, 1157),
    NAME_FUNC_OFFSET(21349, glUseProgramStages, glUseProgramStages, NULL, 1158),
    NAME_FUNC_OFFSET(21368, glValidateProgramPipeline, glValidateProgramPipeline, NULL, 1159),
    NAME_FUNC_OFFSET(21394, gl_dispatch_stub_1160, gl_dispatch_stub_1160, NULL, 1160),
    NAME_FUNC_OFFSET(21431, glDebugMessageCallback, glDebugMessageCallback, NULL, 1161),
    NAME_FUNC_OFFSET(21454, glDebugMessageControl, glDebugMessageControl, NULL, 1162),
    NAME_FUNC_OFFSET(21476, glDebugMessageInsert, glDebugMessageInsert, NULL, 1163),
    NAME_FUNC_OFFSET(21497, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 1164),
    NAME_FUNC_OFFSET(21518, glGetObjectLabel, glGetObjectLabel, NULL, 1165),
    NAME_FUNC_OFFSET(21535, glGetObjectPtrLabel, glGetObjectPtrLabel, NULL, 1166),
    NAME_FUNC_OFFSET(21555, glObjectLabel, glObjectLabel, NULL, 1167),
    NAME_FUNC_OFFSET(21569, glObjectPtrLabel, glObjectPtrLabel, NULL, 1168),
    NAME_FUNC_OFFSET(21586, glPopDebugGroup, glPopDebugGroup, NULL, 1169),
    NAME_FUNC_OFFSET(21602, glPushDebugGroup, glPushDebugGroup, NULL, 1170),
    NAME_FUNC_OFFSET(21619, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 1171),
    NAME_FUNC_OFFSET(21641, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 1172),
    NAME_FUNC_OFFSET(21664, glMultiDrawElements, glMultiDrawElements, NULL, 1173),
    NAME_FUNC_OFFSET(21684, glFogCoordfEXT, glFogCoordfEXT, NULL, 1174),
    NAME_FUNC_OFFSET(21699, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 1175),
    NAME_FUNC_OFFSET(21715, gl_dispatch_stub_1176, gl_dispatch_stub_1176, NULL, 1176),
    NAME_FUNC_OFFSET(21735, gl_dispatch_stub_1177, gl_dispatch_stub_1177, NULL, 1177),
    NAME_FUNC_OFFSET(21753, gl_dispatch_stub_1178, gl_dispatch_stub_1178, NULL, 1178),
    NAME_FUNC_OFFSET(21772, gl_dispatch_stub_1179, gl_dispatch_stub_1179, NULL, 1179),
    NAME_FUNC_OFFSET(21790, gl_dispatch_stub_1180, gl_dispatch_stub_1180, NULL, 1180),
    NAME_FUNC_OFFSET(21809, gl_dispatch_stub_1181, gl_dispatch_stub_1181, NULL, 1181),
    NAME_FUNC_OFFSET(21827, gl_dispatch_stub_1182, gl_dispatch_stub_1182, NULL, 1182),
    NAME_FUNC_OFFSET(21846, gl_dispatch_stub_1183, gl_dispatch_stub_1183, NULL, 1183),
    NAME_FUNC_OFFSET(21864, gl_dispatch_stub_1184, gl_dispatch_stub_1184, NULL, 1184),
    NAME_FUNC_OFFSET(21883, gl_dispatch_stub_1185, gl_dispatch_stub_1185, NULL, 1185),
    NAME_FUNC_OFFSET(21908, gl_dispatch_stub_1186, gl_dispatch_stub_1186, NULL, 1186),
    NAME_FUNC_OFFSET(21935, gl_dispatch_stub_1187, gl_dispatch_stub_1187, NULL, 1187),
    NAME_FUNC_OFFSET(21959, gl_dispatch_stub_1188, gl_dispatch_stub_1188, NULL, 1188),
    NAME_FUNC_OFFSET(21978, gl_dispatch_stub_1189, gl_dispatch_stub_1189, NULL, 1189),
    NAME_FUNC_OFFSET(22004, gl_dispatch_stub_1190, gl_dispatch_stub_1190, NULL, 1190),
    NAME_FUNC_OFFSET(22030, gl_dispatch_stub_1191, gl_dispatch_stub_1191, NULL, 1191),
    NAME_FUNC_OFFSET(22051, gl_dispatch_stub_1192, gl_dispatch_stub_1192, NULL, 1192),
    NAME_FUNC_OFFSET(22068, gl_dispatch_stub_1193, gl_dispatch_stub_1193, NULL, 1193),
    NAME_FUNC_OFFSET(22089, gl_dispatch_stub_1194, gl_dispatch_stub_1194, NULL, 1194),
    NAME_FUNC_OFFSET(22111, gl_dispatch_stub_1195, gl_dispatch_stub_1195, NULL, 1195),
    NAME_FUNC_OFFSET(22133, gl_dispatch_stub_1196, gl_dispatch_stub_1196, NULL, 1196),
    NAME_FUNC_OFFSET(22155, gl_dispatch_stub_1197, gl_dispatch_stub_1197, NULL, 1197),
    NAME_FUNC_OFFSET(22171, gl_dispatch_stub_1198, gl_dispatch_stub_1198, NULL, 1198),
    NAME_FUNC_OFFSET(22196, gl_dispatch_stub_1199, gl_dispatch_stub_1199, NULL, 1199),
    NAME_FUNC_OFFSET(22221, gl_dispatch_stub_1200, gl_dispatch_stub_1200, NULL, 1200),
    NAME_FUNC_OFFSET(22249, gl_dispatch_stub_1201, gl_dispatch_stub_1201, NULL, 1201),
    NAME_FUNC_OFFSET(22265, gl_dispatch_stub_1202, gl_dispatch_stub_1202, NULL, 1202),
    NAME_FUNC_OFFSET(22284, gl_dispatch_stub_1203, gl_dispatch_stub_1203, NULL, 1203),
    NAME_FUNC_OFFSET(22304, gl_dispatch_stub_1204, gl_dispatch_stub_1204, NULL, 1204),
    NAME_FUNC_OFFSET(22323, gl_dispatch_stub_1205, gl_dispatch_stub_1205, NULL, 1205),
    NAME_FUNC_OFFSET(22343, gl_dispatch_stub_1206, gl_dispatch_stub_1206, NULL, 1206),
    NAME_FUNC_OFFSET(22362, gl_dispatch_stub_1207, gl_dispatch_stub_1207, NULL, 1207),
    NAME_FUNC_OFFSET(22382, gl_dispatch_stub_1208, gl_dispatch_stub_1208, NULL, 1208),
    NAME_FUNC_OFFSET(22401, gl_dispatch_stub_1209, gl_dispatch_stub_1209, NULL, 1209),
    NAME_FUNC_OFFSET(22421, gl_dispatch_stub_1210, gl_dispatch_stub_1210, NULL, 1210),
    NAME_FUNC_OFFSET(22440, gl_dispatch_stub_1211, gl_dispatch_stub_1211, NULL, 1211),
    NAME_FUNC_OFFSET(22460, gl_dispatch_stub_1212, gl_dispatch_stub_1212, NULL, 1212),
    NAME_FUNC_OFFSET(22479, gl_dispatch_stub_1213, gl_dispatch_stub_1213, NULL, 1213),
    NAME_FUNC_OFFSET(22499, gl_dispatch_stub_1214, gl_dispatch_stub_1214, NULL, 1214),
    NAME_FUNC_OFFSET(22518, gl_dispatch_stub_1215, gl_dispatch_stub_1215, NULL, 1215),
    NAME_FUNC_OFFSET(22538, gl_dispatch_stub_1216, gl_dispatch_stub_1216, NULL, 1216),
    NAME_FUNC_OFFSET(22557, gl_dispatch_stub_1217, gl_dispatch_stub_1217, NULL, 1217),
    NAME_FUNC_OFFSET(22577, gl_dispatch_stub_1218, gl_dispatch_stub_1218, NULL, 1218),
    NAME_FUNC_OFFSET(22596, gl_dispatch_stub_1219, gl_dispatch_stub_1219, NULL, 1219),
    NAME_FUNC_OFFSET(22616, gl_dispatch_stub_1220, gl_dispatch_stub_1220, NULL, 1220),
    NAME_FUNC_OFFSET(22635, gl_dispatch_stub_1221, gl_dispatch_stub_1221, NULL, 1221),
    NAME_FUNC_OFFSET(22655, gl_dispatch_stub_1222, gl_dispatch_stub_1222, NULL, 1222),
    NAME_FUNC_OFFSET(22674, gl_dispatch_stub_1223, gl_dispatch_stub_1223, NULL, 1223),
    NAME_FUNC_OFFSET(22694, gl_dispatch_stub_1224, gl_dispatch_stub_1224, NULL, 1224),
    NAME_FUNC_OFFSET(22713, gl_dispatch_stub_1225, gl_dispatch_stub_1225, NULL, 1225),
    NAME_FUNC_OFFSET(22733, gl_dispatch_stub_1226, gl_dispatch_stub_1226, NULL, 1226),
    NAME_FUNC_OFFSET(22753, gl_dispatch_stub_1227, gl_dispatch_stub_1227, NULL, 1227),
    NAME_FUNC_OFFSET(22774, gl_dispatch_stub_1228, gl_dispatch_stub_1228, NULL, 1228),
    NAME_FUNC_OFFSET(22798, gl_dispatch_stub_1229, gl_dispatch_stub_1229, NULL, 1229),
    NAME_FUNC_OFFSET(22819, gl_dispatch_stub_1230, gl_dispatch_stub_1230, NULL, 1230),
    NAME_FUNC_OFFSET(22840, gl_dispatch_stub_1231, gl_dispatch_stub_1231, NULL, 1231),
    NAME_FUNC_OFFSET(22861, gl_dispatch_stub_1232, gl_dispatch_stub_1232, NULL, 1232),
    NAME_FUNC_OFFSET(22882, gl_dispatch_stub_1233, gl_dispatch_stub_1233, NULL, 1233),
    NAME_FUNC_OFFSET(22903, gl_dispatch_stub_1234, gl_dispatch_stub_1234, NULL, 1234),
    NAME_FUNC_OFFSET(22924, gl_dispatch_stub_1235, gl_dispatch_stub_1235, NULL, 1235),
    NAME_FUNC_OFFSET(22945, gl_dispatch_stub_1236, gl_dispatch_stub_1236, NULL, 1236),
    NAME_FUNC_OFFSET(22966, gl_dispatch_stub_1237, gl_dispatch_stub_1237, NULL, 1237),
    NAME_FUNC_OFFSET(22987, gl_dispatch_stub_1238, gl_dispatch_stub_1238, NULL, 1238),
    NAME_FUNC_OFFSET(23008, gl_dispatch_stub_1239, gl_dispatch_stub_1239, NULL, 1239),
    NAME_FUNC_OFFSET(23029, gl_dispatch_stub_1240, gl_dispatch_stub_1240, NULL, 1240),
    NAME_FUNC_OFFSET(23050, gl_dispatch_stub_1241, gl_dispatch_stub_1241, NULL, 1241),
    NAME_FUNC_OFFSET(23072, gl_dispatch_stub_1242, gl_dispatch_stub_1242, NULL, 1242),
    NAME_FUNC_OFFSET(23099, gl_dispatch_stub_1243, gl_dispatch_stub_1243, NULL, 1243),
    NAME_FUNC_OFFSET(23126, gl_dispatch_stub_1244, gl_dispatch_stub_1244, NULL, 1244),
    NAME_FUNC_OFFSET(23150, gl_dispatch_stub_1245, gl_dispatch_stub_1245, NULL, 1245),
    NAME_FUNC_OFFSET(23174, gl_dispatch_stub_1246, gl_dispatch_stub_1246, NULL, 1246),
    NAME_FUNC_OFFSET(23196, gl_dispatch_stub_1247, gl_dispatch_stub_1247, NULL, 1247),
    NAME_FUNC_OFFSET(23218, gl_dispatch_stub_1248, gl_dispatch_stub_1248, NULL, 1248),
    NAME_FUNC_OFFSET(23240, gl_dispatch_stub_1249, gl_dispatch_stub_1249, NULL, 1249),
    NAME_FUNC_OFFSET(23265, gl_dispatch_stub_1250, gl_dispatch_stub_1250, NULL, 1250),
    NAME_FUNC_OFFSET(23289, gl_dispatch_stub_1251, gl_dispatch_stub_1251, NULL, 1251),
    NAME_FUNC_OFFSET(23311, gl_dispatch_stub_1252, gl_dispatch_stub_1252, NULL, 1252),
    NAME_FUNC_OFFSET(23333, gl_dispatch_stub_1253, gl_dispatch_stub_1253, NULL, 1253),
    NAME_FUNC_OFFSET(23355, gl_dispatch_stub_1254, gl_dispatch_stub_1254, NULL, 1254),
    NAME_FUNC_OFFSET(23381, gl_dispatch_stub_1255, gl_dispatch_stub_1255, NULL, 1255),
    NAME_FUNC_OFFSET(23404, gl_dispatch_stub_1256, gl_dispatch_stub_1256, NULL, 1256),
    NAME_FUNC_OFFSET(23428, gl_dispatch_stub_1257, gl_dispatch_stub_1257, NULL, 1257),
    NAME_FUNC_OFFSET(23446, gl_dispatch_stub_1258, gl_dispatch_stub_1258, NULL, 1258),
    NAME_FUNC_OFFSET(23461, gl_dispatch_stub_1259, gl_dispatch_stub_1259, NULL, 1259),
    NAME_FUNC_OFFSET(23492, gl_dispatch_stub_1260, gl_dispatch_stub_1260, NULL, 1260),
    NAME_FUNC_OFFSET(23515, gl_dispatch_stub_1261, gl_dispatch_stub_1261, NULL, 1261),
    NAME_FUNC_OFFSET(23539, gl_dispatch_stub_1262, gl_dispatch_stub_1262, NULL, 1262),
    NAME_FUNC_OFFSET(23562, gl_dispatch_stub_1263, gl_dispatch_stub_1263, NULL, 1263),
    NAME_FUNC_OFFSET(23593, gl_dispatch_stub_1264, gl_dispatch_stub_1264, NULL, 1264),
    NAME_FUNC_OFFSET(23624, gl_dispatch_stub_1265, gl_dispatch_stub_1265, NULL, 1265),
    NAME_FUNC_OFFSET(23652, gl_dispatch_stub_1266, gl_dispatch_stub_1266, NULL, 1266),
    NAME_FUNC_OFFSET(23681, gl_dispatch_stub_1267, gl_dispatch_stub_1267, NULL, 1267),
    NAME_FUNC_OFFSET(23709, gl_dispatch_stub_1268, gl_dispatch_stub_1268, NULL, 1268),
    NAME_FUNC_OFFSET(23738, glPrimitiveRestartNV, glPrimitiveRestartNV, NULL, 1269),
    NAME_FUNC_OFFSET(23759, gl_dispatch_stub_1270, gl_dispatch_stub_1270, NULL, 1270),
    NAME_FUNC_OFFSET(23776, gl_dispatch_stub_1271, gl_dispatch_stub_1271, NULL, 1271),
    NAME_FUNC_OFFSET(23789, gl_dispatch_stub_1272, gl_dispatch_stub_1272, NULL, 1272),
    NAME_FUNC_OFFSET(23803, gl_dispatch_stub_1273, gl_dispatch_stub_1273, NULL, 1273),
    NAME_FUNC_OFFSET(23820, glBindFramebufferEXT, glBindFramebufferEXT, NULL, 1274),
    NAME_FUNC_OFFSET(23841, glBindRenderbufferEXT, glBindRenderbufferEXT, NULL, 1275),
    NAME_FUNC_OFFSET(23863, gl_dispatch_stub_1276, gl_dispatch_stub_1276, NULL, 1276),
    NAME_FUNC_OFFSET(23885, gl_dispatch_stub_1277, gl_dispatch_stub_1277, NULL, 1277),
    NAME_FUNC_OFFSET(23909, gl_dispatch_stub_1278, gl_dispatch_stub_1278, NULL, 1278),
    NAME_FUNC_OFFSET(23939, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 1279),
    NAME_FUNC_OFFSET(23960, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 1280),
    NAME_FUNC_OFFSET(23982, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 1281),
    NAME_FUNC_OFFSET(24003, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 1282),
    NAME_FUNC_OFFSET(24025, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 1283),
    NAME_FUNC_OFFSET(24047, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 1284),
    NAME_FUNC_OFFSET(24070, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 1285),
    NAME_FUNC_OFFSET(24091, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 1286),
    NAME_FUNC_OFFSET(24113, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 1287),
    NAME_FUNC_OFFSET(24135, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 1288),
    NAME_FUNC_OFFSET(24158, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 1289),
    NAME_FUNC_OFFSET(24179, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 1290),
    NAME_FUNC_OFFSET(24201, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 1291),
    NAME_FUNC_OFFSET(24223, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 1292),
    NAME_FUNC_OFFSET(24246, glClearColorIiEXT, glClearColorIiEXT, NULL, 1293),
    NAME_FUNC_OFFSET(24264, glClearColorIuiEXT, glClearColorIuiEXT, NULL, 1294),
    NAME_FUNC_OFFSET(24283, gl_dispatch_stub_1295, gl_dispatch_stub_1295, NULL, 1295),
    NAME_FUNC_OFFSET(24305, gl_dispatch_stub_1296, gl_dispatch_stub_1296, NULL, 1296),
    NAME_FUNC_OFFSET(24327, gl_dispatch_stub_1297, gl_dispatch_stub_1297, NULL, 1297),
    NAME_FUNC_OFFSET(24351, gl_dispatch_stub_1298, gl_dispatch_stub_1298, NULL, 1298),
    NAME_FUNC_OFFSET(24371, gl_dispatch_stub_1299, gl_dispatch_stub_1299, NULL, 1299),
    NAME_FUNC_OFFSET(24392, gl_dispatch_stub_1300, gl_dispatch_stub_1300, NULL, 1300),
    NAME_FUNC_OFFSET(24423, gl_dispatch_stub_1301, gl_dispatch_stub_1301, NULL, 1301),
    NAME_FUNC_OFFSET(24454, gl_dispatch_stub_1302, gl_dispatch_stub_1302, NULL, 1302),
    NAME_FUNC_OFFSET(24487, gl_dispatch_stub_1303, gl_dispatch_stub_1303, NULL, 1303),
    NAME_FUNC_OFFSET(24515, gl_dispatch_stub_1304, gl_dispatch_stub_1304, NULL, 1304),
    NAME_FUNC_OFFSET(24546, gl_dispatch_stub_1305, gl_dispatch_stub_1305, NULL, 1305),
    NAME_FUNC_OFFSET(24572, gl_dispatch_stub_1306, gl_dispatch_stub_1306, NULL, 1306),
    NAME_FUNC_OFFSET(24603, gl_dispatch_stub_1307, gl_dispatch_stub_1307, NULL, 1307),
    NAME_FUNC_OFFSET(24631, gl_dispatch_stub_1308, gl_dispatch_stub_1308, NULL, 1308),
    NAME_FUNC_OFFSET(24654, gl_dispatch_stub_1309, gl_dispatch_stub_1309, NULL, 1309),
    NAME_FUNC_OFFSET(24679, gl_dispatch_stub_1310, gl_dispatch_stub_1310, NULL, 1310),
    NAME_FUNC_OFFSET(24698, gl_dispatch_stub_1311, gl_dispatch_stub_1311, NULL, 1311),
    NAME_FUNC_OFFSET(24723, gl_dispatch_stub_1312, gl_dispatch_stub_1312, NULL, 1312),
    NAME_FUNC_OFFSET(24745, glTextureBarrierNV, glTextureBarrierNV, NULL, 1313),
    NAME_FUNC_OFFSET(24764, gl_dispatch_stub_1314, gl_dispatch_stub_1314, NULL, 1314),
    NAME_FUNC_OFFSET(24778, gl_dispatch_stub_1315, gl_dispatch_stub_1315, NULL, 1315),
    NAME_FUNC_OFFSET(24800, gl_dispatch_stub_1316, gl_dispatch_stub_1316, NULL, 1316),
    NAME_FUNC_OFFSET(24814, gl_dispatch_stub_1317, gl_dispatch_stub_1317, NULL, 1317),
    NAME_FUNC_OFFSET(24833, gl_dispatch_stub_1318, gl_dispatch_stub_1318, NULL, 1318),
    NAME_FUNC_OFFSET(24854, gl_dispatch_stub_1319, gl_dispatch_stub_1319, NULL, 1319),
    NAME_FUNC_OFFSET(24885, gl_dispatch_stub_1320, gl_dispatch_stub_1320, NULL, 1320),
    NAME_FUNC_OFFSET(24915, gl_dispatch_stub_1321, gl_dispatch_stub_1321, NULL, 1321),
    NAME_FUNC_OFFSET(24938, gl_dispatch_stub_1322, gl_dispatch_stub_1322, NULL, 1322),
    NAME_FUNC_OFFSET(24961, gl_dispatch_stub_1323, gl_dispatch_stub_1323, NULL, 1323),
    NAME_FUNC_OFFSET(24988, gl_dispatch_stub_1324, gl_dispatch_stub_1324, NULL, 1324),
    NAME_FUNC_OFFSET(25010, gl_dispatch_stub_1325, gl_dispatch_stub_1325, NULL, 1325),
    NAME_FUNC_OFFSET(25033, gl_dispatch_stub_1326, gl_dispatch_stub_1326, NULL, 1326),
    NAME_FUNC_OFFSET(25056, gl_dispatch_stub_1327, gl_dispatch_stub_1327, NULL, 1327),
    NAME_FUNC_OFFSET(25076, gl_dispatch_stub_1328, gl_dispatch_stub_1328, NULL, 1328),
    NAME_FUNC_OFFSET(25103, gl_dispatch_stub_1329, gl_dispatch_stub_1329, NULL, 1329),
    NAME_FUNC_OFFSET(25129, gl_dispatch_stub_1330, gl_dispatch_stub_1330, NULL, 1330),
    NAME_FUNC_OFFSET(25155, gl_dispatch_stub_1331, gl_dispatch_stub_1331, NULL, 1331),
    NAME_FUNC_OFFSET(25179, gl_dispatch_stub_1332, gl_dispatch_stub_1332, NULL, 1332),
    NAME_FUNC_OFFSET(25207, gl_dispatch_stub_1333, gl_dispatch_stub_1333, NULL, 1333),
    NAME_FUNC_OFFSET(25231, gl_dispatch_stub_1334, gl_dispatch_stub_1334, NULL, 1334),
    NAME_FUNC_OFFSET(25255, gl_dispatch_stub_1335, gl_dispatch_stub_1335, NULL, 1335),
    NAME_FUNC_OFFSET(25281, gl_dispatch_stub_1336, gl_dispatch_stub_1336, NULL, 1336),
    NAME_FUNC_OFFSET(25314, gl_dispatch_stub_1337, gl_dispatch_stub_1337, NULL, 1337),
    NAME_FUNC_OFFSET(25347, gl_dispatch_stub_1338, gl_dispatch_stub_1338, NULL, 1338),
    NAME_FUNC_OFFSET(25369, gl_dispatch_stub_1339, gl_dispatch_stub_1339, NULL, 1339),
    NAME_FUNC_OFFSET(25391, gl_dispatch_stub_1340, gl_dispatch_stub_1340, NULL, 1340),
    NAME_FUNC_OFFSET(25416, gl_dispatch_stub_1341, gl_dispatch_stub_1341, NULL, 1341),
    NAME_FUNC_OFFSET(25441, gl_dispatch_stub_1342, gl_dispatch_stub_1342, NULL, 1342),
    NAME_FUNC_OFFSET(25463, gl_dispatch_stub_1343, gl_dispatch_stub_1343, NULL, 1343),
    NAME_FUNC_OFFSET(25482, gl_dispatch_stub_1344, gl_dispatch_stub_1344, NULL, 1344),
    NAME_FUNC_OFFSET(25514, gl_dispatch_stub_1345, gl_dispatch_stub_1345, NULL, 1345),
    NAME_FUNC_OFFSET(25546, gl_dispatch_stub_1346, gl_dispatch_stub_1346, NULL, 1346),
    NAME_FUNC_OFFSET(25570, gl_dispatch_stub_1347, gl_dispatch_stub_1347, NULL, 1347),
    NAME_FUNC_OFFSET(25592, gl_dispatch_stub_1348, gl_dispatch_stub_1348, NULL, 1348),
    NAME_FUNC_OFFSET(25612, gl_dispatch_stub_1349, gl_dispatch_stub_1349, NULL, 1349),
    NAME_FUNC_OFFSET(25629, gl_dispatch_stub_1350, gl_dispatch_stub_1350, NULL, 1350),
    NAME_FUNC_OFFSET(25658, gl_dispatch_stub_1351, gl_dispatch_stub_1351, NULL, 1351),
    NAME_FUNC_OFFSET(25685, gl_dispatch_stub_1352, gl_dispatch_stub_1352, NULL, 1352),
    NAME_FUNC_OFFSET(25714, gl_dispatch_stub_1353, gl_dispatch_stub_1353, NULL, 1353),
    NAME_FUNC_OFFSET(25735, gl_dispatch_stub_1354, gl_dispatch_stub_1354, NULL, 1354),
    NAME_FUNC_OFFSET(25756, gl_dispatch_stub_1355, gl_dispatch_stub_1355, NULL, 1355),
    NAME_FUNC_OFFSET(25777, gl_dispatch_stub_1356, gl_dispatch_stub_1356, NULL, 1356),
    NAME_FUNC_OFFSET(25809, gl_dispatch_stub_1357, gl_dispatch_stub_1357, NULL, 1357),
    NAME_FUNC_OFFSET(25830, gl_dispatch_stub_1358, gl_dispatch_stub_1358, NULL, 1358),
    NAME_FUNC_OFFSET(25862, gl_dispatch_stub_1359, gl_dispatch_stub_1359, NULL, 1359),
    NAME_FUNC_OFFSET(25887, gl_dispatch_stub_1360, gl_dispatch_stub_1360, NULL, 1360),
    NAME_FUNC_OFFSET(25912, gl_dispatch_stub_1361, gl_dispatch_stub_1361, NULL, 1361),
    NAME_FUNC_OFFSET(25948, gl_dispatch_stub_1362, gl_dispatch_stub_1362, NULL, 1362),
    NAME_FUNC_OFFSET(25973, gl_dispatch_stub_1363, gl_dispatch_stub_1363, NULL, 1363),
    NAME_FUNC_OFFSET(26009, gl_dispatch_stub_1364, gl_dispatch_stub_1364, NULL, 1364),
    NAME_FUNC_OFFSET(26028, gl_dispatch_stub_1365, gl_dispatch_stub_1365, NULL, 1365),
    NAME_FUNC_OFFSET(26048, gl_dispatch_stub_1366, gl_dispatch_stub_1366, NULL, 1366),
    NAME_FUNC_OFFSET(26071, gl_dispatch_stub_1367, gl_dispatch_stub_1367, NULL, 1367),
    NAME_FUNC_OFFSET(26100, gl_dispatch_stub_1368, gl_dispatch_stub_1368, NULL, 1368),
    NAME_FUNC_OFFSET(26149, gl_dispatch_stub_1369, gl_dispatch_stub_1369, NULL, 1369),
    NAME_FUNC_OFFSET(26193, gl_dispatch_stub_1370, gl_dispatch_stub_1370, NULL, 1370),
    NAME_FUNC_OFFSET(26218, gl_dispatch_stub_1371, gl_dispatch_stub_1371, NULL, 1371),
    NAME_FUNC_OFFSET(26247, gl_dispatch_stub_1372, gl_dispatch_stub_1372, NULL, 1372),
    NAME_FUNC_OFFSET(26278, gl_dispatch_stub_1373, gl_dispatch_stub_1373, NULL, 1373),
    NAME_FUNC_OFFSET(26317, gl_dispatch_stub_1374, gl_dispatch_stub_1374, NULL, 1374),
    NAME_FUNC_OFFSET(26346, glAlphaFuncx, glAlphaFuncx, NULL, 1375),
    NAME_FUNC_OFFSET(26359, glClearColorx, glClearColorx, NULL, 1376),
    NAME_FUNC_OFFSET(26373, glClearDepthx, glClearDepthx, NULL, 1377),
    NAME_FUNC_OFFSET(26387, glColor4x, glColor4x, NULL, 1378),
    NAME_FUNC_OFFSET(26397, glDepthRangex, glDepthRangex, NULL, 1379),
    NAME_FUNC_OFFSET(26411, glFogx, glFogx, NULL, 1380),
    NAME_FUNC_OFFSET(26418, glFogxv, glFogxv, NULL, 1381),
    NAME_FUNC_OFFSET(26426, glFrustumf, glFrustumf, NULL, 1382),
    NAME_FUNC_OFFSET(26437, glFrustumx, glFrustumx, NULL, 1383),
    NAME_FUNC_OFFSET(26448, glLightModelx, glLightModelx, NULL, 1384),
    NAME_FUNC_OFFSET(26462, glLightModelxv, glLightModelxv, NULL, 1385),
    NAME_FUNC_OFFSET(26477, glLightx, glLightx, NULL, 1386),
    NAME_FUNC_OFFSET(26486, glLightxv, glLightxv, NULL, 1387),
    NAME_FUNC_OFFSET(26496, glLineWidthx, glLineWidthx, NULL, 1388),
    NAME_FUNC_OFFSET(26509, glLoadMatrixx, glLoadMatrixx, NULL, 1389),
    NAME_FUNC_OFFSET(26523, glMaterialx, glMaterialx, NULL, 1390),
    NAME_FUNC_OFFSET(26535, glMaterialxv, glMaterialxv, NULL, 1391),
    NAME_FUNC_OFFSET(26548, glMultMatrixx, glMultMatrixx, NULL, 1392),
    NAME_FUNC_OFFSET(26562, glMultiTexCoord4x, glMultiTexCoord4x, NULL, 1393),
    NAME_FUNC_OFFSET(26580, glNormal3x, glNormal3x, NULL, 1394),
    NAME_FUNC_OFFSET(26591, glOrthof, glOrthof, NULL, 1395),
    NAME_FUNC_OFFSET(26600, glOrthox, glOrthox, NULL, 1396),
    NAME_FUNC_OFFSET(26609, glPointSizex, glPointSizex, NULL, 1397),
    NAME_FUNC_OFFSET(26622, glPolygonOffsetx, glPolygonOffsetx, NULL, 1398),
    NAME_FUNC_OFFSET(26639, glRotatex, glRotatex, NULL, 1399),
    NAME_FUNC_OFFSET(26649, glSampleCoveragex, glSampleCoveragex, NULL, 1400),
    NAME_FUNC_OFFSET(26667, glScalex, glScalex, NULL, 1401),
    NAME_FUNC_OFFSET(26676, glTexEnvx, glTexEnvx, NULL, 1402),
    NAME_FUNC_OFFSET(26686, glTexEnvxv, glTexEnvxv, NULL, 1403),
    NAME_FUNC_OFFSET(26697, glTexParameterx, glTexParameterx, NULL, 1404),
    NAME_FUNC_OFFSET(26713, glTranslatex, glTranslatex, NULL, 1405),
    NAME_FUNC_OFFSET(26726, glClipPlanef, glClipPlanef, NULL, 1406),
    NAME_FUNC_OFFSET(26739, glClipPlanex, glClipPlanex, NULL, 1407),
    NAME_FUNC_OFFSET(26752, glGetClipPlanef, glGetClipPlanef, NULL, 1408),
    NAME_FUNC_OFFSET(26768, glGetClipPlanex, glGetClipPlanex, NULL, 1409),
    NAME_FUNC_OFFSET(26784, glGetFixedv, glGetFixedv, NULL, 1410),
    NAME_FUNC_OFFSET(26796, glGetLightxv, glGetLightxv, NULL, 1411),
    NAME_FUNC_OFFSET(26809, glGetMaterialxv, glGetMaterialxv, NULL, 1412),
    NAME_FUNC_OFFSET(26825, glGetTexEnvxv, glGetTexEnvxv, NULL, 1413),
    NAME_FUNC_OFFSET(26839, glGetTexParameterxv, glGetTexParameterxv, NULL, 1414),
    NAME_FUNC_OFFSET(26859, glPointParameterx, glPointParameterx, NULL, 1415),
    NAME_FUNC_OFFSET(26877, glPointParameterxv, glPointParameterxv, NULL, 1416),
    NAME_FUNC_OFFSET(26896, glTexParameterxv, glTexParameterxv, NULL, 1417),
    NAME_FUNC_OFFSET(26913, glBlendBarrier, glBlendBarrier, NULL, 1418),
    NAME_FUNC_OFFSET(26928, glPrimitiveBoundingBox, glPrimitiveBoundingBox, NULL, 1419),
    NAME_FUNC_OFFSET(26951, gl_dispatch_stub_1420, gl_dispatch_stub_1420, NULL, 1420),
    NAME_FUNC_OFFSET(26981, gl_dispatch_stub_1421, gl_dispatch_stub_1421, NULL, 1421),
    NAME_FUNC_OFFSET(26998, gl_dispatch_stub_1422, gl_dispatch_stub_1422, NULL, 1422),
    NAME_FUNC_OFFSET(27015, gl_dispatch_stub_1423, gl_dispatch_stub_1423, NULL, 1423),
    NAME_FUNC_OFFSET(27032, gl_dispatch_stub_1424, gl_dispatch_stub_1424, NULL, 1424),
    NAME_FUNC_OFFSET(27049, gl_dispatch_stub_1425, gl_dispatch_stub_1425, NULL, 1425),
    NAME_FUNC_OFFSET(27073, gl_dispatch_stub_1426, gl_dispatch_stub_1426, NULL, 1426),
    NAME_FUNC_OFFSET(27092, gl_dispatch_stub_1427, gl_dispatch_stub_1427, NULL, 1427),
    NAME_FUNC_OFFSET(27111, gl_dispatch_stub_1428, gl_dispatch_stub_1428, NULL, 1428),
    NAME_FUNC_OFFSET(27129, gl_dispatch_stub_1429, gl_dispatch_stub_1429, NULL, 1429),
    NAME_FUNC_OFFSET(27147, gl_dispatch_stub_1430, gl_dispatch_stub_1430, NULL, 1430),
    NAME_FUNC_OFFSET(27169, gl_dispatch_stub_1431, gl_dispatch_stub_1431, NULL, 1431),
    NAME_FUNC_OFFSET(27191, gl_dispatch_stub_1432, gl_dispatch_stub_1432, NULL, 1432),
    NAME_FUNC_OFFSET(27208, gl_dispatch_stub_1433, gl_dispatch_stub_1433, NULL, 1433),
    NAME_FUNC_OFFSET(27227, gl_dispatch_stub_1434, gl_dispatch_stub_1434, NULL, 1434),
    NAME_FUNC_OFFSET(27243, gl_dispatch_stub_1435, gl_dispatch_stub_1435, NULL, 1435),
    NAME_FUNC_OFFSET(27258, gl_dispatch_stub_1436, gl_dispatch_stub_1436, NULL, 1436),
    NAME_FUNC_OFFSET(27284, gl_dispatch_stub_1437, gl_dispatch_stub_1437, NULL, 1437),
    NAME_FUNC_OFFSET(27310, gl_dispatch_stub_1438, gl_dispatch_stub_1438, NULL, 1438),
    NAME_FUNC_OFFSET(27336, gl_dispatch_stub_1439, gl_dispatch_stub_1439, NULL, 1439),
    NAME_FUNC_OFFSET(27362, gl_dispatch_stub_1440, gl_dispatch_stub_1440, NULL, 1440),
    NAME_FUNC_OFFSET(27384, gl_dispatch_stub_1441, gl_dispatch_stub_1441, NULL, 1441),
    NAME_FUNC_OFFSET(27405, gl_dispatch_stub_1442, gl_dispatch_stub_1442, NULL, 1442),
    NAME_FUNC_OFFSET(27429, gl_dispatch_stub_1443, gl_dispatch_stub_1443, NULL, 1443),
    NAME_FUNC_OFFSET(27453, gl_dispatch_stub_1444, gl_dispatch_stub_1444, NULL, 1444),
    NAME_FUNC_OFFSET(27478, gl_dispatch_stub_1445, gl_dispatch_stub_1445, NULL, 1445),
    NAME_FUNC_OFFSET(27498, gl_dispatch_stub_1446, gl_dispatch_stub_1446, NULL, 1446),
    NAME_FUNC_OFFSET(27518, gl_dispatch_stub_1447, gl_dispatch_stub_1447, NULL, 1447),
    NAME_FUNC_OFFSET(27538, gl_dispatch_stub_1448, gl_dispatch_stub_1448, NULL, 1448),
    NAME_FUNC_OFFSET(27561, gl_dispatch_stub_1449, gl_dispatch_stub_1449, NULL, 1449),
    NAME_FUNC_OFFSET(27584, gl_dispatch_stub_1450, gl_dispatch_stub_1450, NULL, 1450),
    NAME_FUNC_OFFSET(27607, gl_dispatch_stub_1451, gl_dispatch_stub_1451, NULL, 1451),
    NAME_FUNC_OFFSET(27631, gl_dispatch_stub_1452, gl_dispatch_stub_1452, NULL, 1452),
    NAME_FUNC_OFFSET(27655, gl_dispatch_stub_1453, gl_dispatch_stub_1453, NULL, 1453),
    NAME_FUNC_OFFSET(27682, gl_dispatch_stub_1454, gl_dispatch_stub_1454, NULL, 1454),
    NAME_FUNC_OFFSET(27709, gl_dispatch_stub_1455, gl_dispatch_stub_1455, NULL, 1455),
    NAME_FUNC_OFFSET(27736, gl_dispatch_stub_1456, gl_dispatch_stub_1456, NULL, 1456),
    NAME_FUNC_OFFSET(27756, gl_dispatch_stub_1457, gl_dispatch_stub_1457, NULL, 1457),
    NAME_FUNC_OFFSET(27783, gl_dispatch_stub_1458, gl_dispatch_stub_1458, NULL, 1458),
    NAME_FUNC_OFFSET(27810, gl_dispatch_stub_1459, gl_dispatch_stub_1459, NULL, 1459),
    NAME_FUNC_OFFSET(27833, gl_dispatch_stub_1460, gl_dispatch_stub_1460, NULL, 1460),
    NAME_FUNC_OFFSET(27857, gl_dispatch_stub_1461, gl_dispatch_stub_1461, NULL, 1461),
    NAME_FUNC_OFFSET(27880, gl_dispatch_stub_1462, gl_dispatch_stub_1462, NULL, 1462),
    NAME_FUNC_OFFSET(27904, gl_dispatch_stub_1463, gl_dispatch_stub_1463, NULL, 1463),
    NAME_FUNC_OFFSET(27925, gl_dispatch_stub_1464, gl_dispatch_stub_1464, NULL, 1464),
    NAME_FUNC_OFFSET(27957, gl_dispatch_stub_1465, gl_dispatch_stub_1465, NULL, 1465),
    NAME_FUNC_OFFSET(27989, gl_dispatch_stub_1466, gl_dispatch_stub_1466, NULL, 1466),
    NAME_FUNC_OFFSET(28016, gl_dispatch_stub_1467, gl_dispatch_stub_1467, NULL, 1467),
    NAME_FUNC_OFFSET(28044, gl_dispatch_stub_1468, gl_dispatch_stub_1468, NULL, 1468),
    NAME_FUNC_OFFSET(28075, gl_dispatch_stub_1469, gl_dispatch_stub_1469, NULL, 1469),
    NAME_FUNC_OFFSET(28108, gl_dispatch_stub_1470, gl_dispatch_stub_1470, NULL, 1470),
    NAME_FUNC_OFFSET(28135, gl_dispatch_stub_1471, gl_dispatch_stub_1471, NULL, 1471),
    NAME_FUNC_OFFSET(28163, gl_dispatch_stub_1472, gl_dispatch_stub_1472, NULL, 1472),
    NAME_FUNC_OFFSET(28190, gl_dispatch_stub_1473, gl_dispatch_stub_1473, NULL, 1473),
    NAME_FUNC_OFFSET(28221, gl_dispatch_stub_1474, gl_dispatch_stub_1474, NULL, 1474),
    NAME_FUNC_OFFSET(28254, gl_dispatch_stub_1475, gl_dispatch_stub_1475, NULL, 1475),
    NAME_FUNC_OFFSET(28285, gl_dispatch_stub_1476, gl_dispatch_stub_1476, NULL, 1476),
    NAME_FUNC_OFFSET(28316, gl_dispatch_stub_1477, gl_dispatch_stub_1477, NULL, 1477),
    NAME_FUNC_OFFSET(28347, gl_dispatch_stub_1478, gl_dispatch_stub_1478, NULL, 1478),
    NAME_FUNC_OFFSET(28381, gl_dispatch_stub_1479, gl_dispatch_stub_1479, NULL, 1479),
    NAME_FUNC_OFFSET(28427, gl_dispatch_stub_1480, gl_dispatch_stub_1480, NULL, 1480),
    NAME_FUNC_OFFSET(28451, gl_dispatch_stub_1481, gl_dispatch_stub_1481, NULL, 1481),
    NAME_FUNC_OFFSET(28476, gl_dispatch_stub_1482, gl_dispatch_stub_1482, NULL, 1482),
    NAME_FUNC_OFFSET(28500, gl_dispatch_stub_1483, gl_dispatch_stub_1483, NULL, 1483),
    NAME_FUNC_OFFSET(28518, gl_dispatch_stub_1484, gl_dispatch_stub_1484, NULL, 1484),
    NAME_FUNC_OFFSET(28537, gl_dispatch_stub_1485, gl_dispatch_stub_1485, NULL, 1485),
    NAME_FUNC_OFFSET(28555, gl_dispatch_stub_1486, gl_dispatch_stub_1486, NULL, 1486),
    NAME_FUNC_OFFSET(28574, gl_dispatch_stub_1487, gl_dispatch_stub_1487, NULL, 1487),
    NAME_FUNC_OFFSET(28596, gl_dispatch_stub_1488, gl_dispatch_stub_1488, NULL, 1488),
    NAME_FUNC_OFFSET(28618, gl_dispatch_stub_1489, gl_dispatch_stub_1489, NULL, 1489),
    NAME_FUNC_OFFSET(28642, gl_dispatch_stub_1490, gl_dispatch_stub_1490, NULL, 1490),
    NAME_FUNC_OFFSET(28667, gl_dispatch_stub_1491, gl_dispatch_stub_1491, NULL, 1491),
    NAME_FUNC_OFFSET(28691, gl_dispatch_stub_1492, gl_dispatch_stub_1492, NULL, 1492),
    NAME_FUNC_OFFSET(28716, gl_dispatch_stub_1493, gl_dispatch_stub_1493, NULL, 1493),
    NAME_FUNC_OFFSET(28738, gl_dispatch_stub_1494, gl_dispatch_stub_1494, NULL, 1494),
    NAME_FUNC_OFFSET(28759, gl_dispatch_stub_1495, gl_dispatch_stub_1495, NULL, 1495),
    NAME_FUNC_OFFSET(28780, gl_dispatch_stub_1496, gl_dispatch_stub_1496, NULL, 1496),
    NAME_FUNC_OFFSET(28801, gl_dispatch_stub_1497, gl_dispatch_stub_1497, NULL, 1497),
    NAME_FUNC_OFFSET(28825, gl_dispatch_stub_1498, gl_dispatch_stub_1498, NULL, 1498),
    NAME_FUNC_OFFSET(28849, gl_dispatch_stub_1499, gl_dispatch_stub_1499, NULL, 1499),
    NAME_FUNC_OFFSET(28873, gl_dispatch_stub_1500, gl_dispatch_stub_1500, NULL, 1500),
    NAME_FUNC_OFFSET(28901, gl_dispatch_stub_1501, gl_dispatch_stub_1501, NULL, 1501),
    NAME_FUNC_OFFSET(28929, gl_dispatch_stub_1502, gl_dispatch_stub_1502, NULL, 1502),
    NAME_FUNC_OFFSET(28954, gl_dispatch_stub_1503, gl_dispatch_stub_1503, NULL, 1503),
    NAME_FUNC_OFFSET(28979, gl_dispatch_stub_1504, gl_dispatch_stub_1504, NULL, 1504),
    NAME_FUNC_OFFSET(29007, gl_dispatch_stub_1505, gl_dispatch_stub_1505, NULL, 1505),
    NAME_FUNC_OFFSET(29035, gl_dispatch_stub_1506, gl_dispatch_stub_1506, NULL, 1506),
    NAME_FUNC_OFFSET(29063, gl_dispatch_stub_1507, gl_dispatch_stub_1507, NULL, 1507),
    NAME_FUNC_OFFSET(29081, gl_dispatch_stub_1508, gl_dispatch_stub_1508, NULL, 1508),
    NAME_FUNC_OFFSET(29100, gl_dispatch_stub_1509, gl_dispatch_stub_1509, NULL, 1509),
    NAME_FUNC_OFFSET(29118, gl_dispatch_stub_1510, gl_dispatch_stub_1510, NULL, 1510),
    NAME_FUNC_OFFSET(29137, gl_dispatch_stub_1511, gl_dispatch_stub_1511, NULL, 1511),
    NAME_FUNC_OFFSET(29155, gl_dispatch_stub_1512, gl_dispatch_stub_1512, NULL, 1512),
    NAME_FUNC_OFFSET(29174, gl_dispatch_stub_1513, gl_dispatch_stub_1513, NULL, 1513),
    NAME_FUNC_OFFSET(29196, gl_dispatch_stub_1514, gl_dispatch_stub_1514, NULL, 1514),
    NAME_FUNC_OFFSET(29218, gl_dispatch_stub_1515, gl_dispatch_stub_1515, NULL, 1515),
    NAME_FUNC_OFFSET(29240, gl_dispatch_stub_1516, gl_dispatch_stub_1516, NULL, 1516),
    NAME_FUNC_OFFSET(29266, gl_dispatch_stub_1517, gl_dispatch_stub_1517, NULL, 1517),
    NAME_FUNC_OFFSET(29288, gl_dispatch_stub_1518, gl_dispatch_stub_1518, NULL, 1518),
    NAME_FUNC_OFFSET(29318, gl_dispatch_stub_1519, gl_dispatch_stub_1519, NULL, 1519),
    NAME_FUNC_OFFSET(29348, gl_dispatch_stub_1520, gl_dispatch_stub_1520, NULL, 1520),
    NAME_FUNC_OFFSET(29378, gl_dispatch_stub_1521, gl_dispatch_stub_1521, NULL, 1521),
    NAME_FUNC_OFFSET(29411, gl_dispatch_stub_1522, gl_dispatch_stub_1522, NULL, 1522),
    NAME_FUNC_OFFSET(29444, gl_dispatch_stub_1523, gl_dispatch_stub_1523, NULL, 1523),
    NAME_FUNC_OFFSET(29477, gl_dispatch_stub_1524, gl_dispatch_stub_1524, NULL, 1524),
    NAME_FUNC_OFFSET(29508, gl_dispatch_stub_1525, gl_dispatch_stub_1525, NULL, 1525),
    NAME_FUNC_OFFSET(29539, gl_dispatch_stub_1526, gl_dispatch_stub_1526, NULL, 1526),
    NAME_FUNC_OFFSET(29570, gl_dispatch_stub_1527, gl_dispatch_stub_1527, NULL, 1527),
    NAME_FUNC_OFFSET(29601, gl_dispatch_stub_1528, gl_dispatch_stub_1528, NULL, 1528),
    NAME_FUNC_OFFSET(29635, gl_dispatch_stub_1529, gl_dispatch_stub_1529, NULL, 1529),
    NAME_FUNC_OFFSET(29669, gl_dispatch_stub_1530, gl_dispatch_stub_1530, NULL, 1530),
    NAME_FUNC_OFFSET(29703, gl_dispatch_stub_1531, gl_dispatch_stub_1531, NULL, 1531),
    NAME_FUNC_OFFSET(29735, gl_dispatch_stub_1532, gl_dispatch_stub_1532, NULL, 1532),
    NAME_FUNC_OFFSET(29768, gl_dispatch_stub_1533, gl_dispatch_stub_1533, NULL, 1533),
    NAME_FUNC_OFFSET(29801, gl_dispatch_stub_1534, gl_dispatch_stub_1534, NULL, 1534),
    NAME_FUNC_OFFSET(29829, gl_dispatch_stub_1535, gl_dispatch_stub_1535, NULL, 1535),
    NAME_FUNC_OFFSET(29861, gl_dispatch_stub_1536, gl_dispatch_stub_1536, NULL, 1536),
    NAME_FUNC_OFFSET(29891, gl_dispatch_stub_1537, gl_dispatch_stub_1537, NULL, 1537),
    NAME_FUNC_OFFSET(29928, gl_dispatch_stub_1538, gl_dispatch_stub_1538, NULL, 1538),
    NAME_FUNC_OFFSET(29953, gl_dispatch_stub_1539, gl_dispatch_stub_1539, NULL, 1539),
    NAME_FUNC_OFFSET(29982, gl_dispatch_stub_1540, gl_dispatch_stub_1540, NULL, 1540),
    NAME_FUNC_OFFSET(30006, gl_dispatch_stub_1541, gl_dispatch_stub_1541, NULL, 1541),
    NAME_FUNC_OFFSET(30033, gl_dispatch_stub_1542, gl_dispatch_stub_1542, NULL, 1542),
    NAME_FUNC_OFFSET(30067, gl_dispatch_stub_1543, gl_dispatch_stub_1543, NULL, 1543),
    NAME_FUNC_OFFSET(30102, gl_dispatch_stub_1544, gl_dispatch_stub_1544, NULL, 1544),
    NAME_FUNC_OFFSET(30139, gl_dispatch_stub_1545, gl_dispatch_stub_1545, NULL, 1545),
    NAME_FUNC_OFFSET(30173, gl_dispatch_stub_1546, gl_dispatch_stub_1546, NULL, 1546),
    NAME_FUNC_OFFSET(30208, gl_dispatch_stub_1547, gl_dispatch_stub_1547, NULL, 1547),
    NAME_FUNC_OFFSET(30245, gl_dispatch_stub_1548, gl_dispatch_stub_1548, NULL, 1548),
    NAME_FUNC_OFFSET(30268, gl_dispatch_stub_1549, gl_dispatch_stub_1549, NULL, 1549),
    NAME_FUNC_OFFSET(30287, gl_dispatch_stub_1550, gl_dispatch_stub_1550, NULL, 1550),
    NAME_FUNC_OFFSET(30307, gl_dispatch_stub_1551, gl_dispatch_stub_1551, NULL, 1551),
    NAME_FUNC_OFFSET(30332, gl_dispatch_stub_1552, gl_dispatch_stub_1552, NULL, 1552),
    NAME_FUNC_OFFSET(30358, gl_dispatch_stub_1553, gl_dispatch_stub_1553, NULL, 1553),
    NAME_FUNC_OFFSET(30386, gl_dispatch_stub_1554, gl_dispatch_stub_1554, NULL, 1554),
    NAME_FUNC_OFFSET(30415, gl_dispatch_stub_1555, gl_dispatch_stub_1555, NULL, 1555),
    NAME_FUNC_OFFSET(30441, gl_dispatch_stub_1556, gl_dispatch_stub_1556, NULL, 1556),
    NAME_FUNC_OFFSET(30468, gl_dispatch_stub_1557, gl_dispatch_stub_1557, NULL, 1557),
    NAME_FUNC_OFFSET(30497, gl_dispatch_stub_1558, gl_dispatch_stub_1558, NULL, 1558),
    NAME_FUNC_OFFSET(30527, gl_dispatch_stub_1559, gl_dispatch_stub_1559, NULL, 1559),
    NAME_FUNC_OFFSET(30563, gl_dispatch_stub_1560, gl_dispatch_stub_1560, NULL, 1560),
    NAME_FUNC_OFFSET(30590, gl_dispatch_stub_1561, gl_dispatch_stub_1561, NULL, 1561),
    NAME_FUNC_OFFSET(30618, gl_dispatch_stub_1562, gl_dispatch_stub_1562, NULL, 1562),
    NAME_FUNC_OFFSET(30659, gl_dispatch_stub_1563, gl_dispatch_stub_1563, NULL, 1563),
    NAME_FUNC_OFFSET(30687, gl_dispatch_stub_1564, gl_dispatch_stub_1564, NULL, 1564),
    NAME_FUNC_OFFSET(30716, gl_dispatch_stub_1565, gl_dispatch_stub_1565, NULL, 1565),
    NAME_FUNC_OFFSET(30744, gl_dispatch_stub_1566, gl_dispatch_stub_1566, NULL, 1566),
    NAME_FUNC_OFFSET(30775, gl_dispatch_stub_1567, gl_dispatch_stub_1567, NULL, 1567),
    NAME_FUNC_OFFSET(30803, gl_dispatch_stub_1568, gl_dispatch_stub_1568, NULL, 1568),
    NAME_FUNC_OFFSET(30832, gl_dispatch_stub_1569, gl_dispatch_stub_1569, NULL, 1569),
    NAME_FUNC_OFFSET(30863, gl_dispatch_stub_1570, gl_dispatch_stub_1570, NULL, 1570),
    NAME_FUNC_OFFSET(30899, gl_dispatch_stub_1571, gl_dispatch_stub_1571, NULL, 1571),
    NAME_FUNC_OFFSET(30930, gl_dispatch_stub_1572, gl_dispatch_stub_1572, NULL, 1572),
    NAME_FUNC_OFFSET(30967, gl_dispatch_stub_1573, gl_dispatch_stub_1573, NULL, 1573),
    NAME_FUNC_OFFSET(31002, gl_dispatch_stub_1574, gl_dispatch_stub_1574, NULL, 1574),
    NAME_FUNC_OFFSET(31038, gl_dispatch_stub_1575, gl_dispatch_stub_1575, NULL, 1575),
    NAME_FUNC_OFFSET(31061, gl_dispatch_stub_1576, gl_dispatch_stub_1576, NULL, 1576),
    NAME_FUNC_OFFSET(31085, gl_dispatch_stub_1577, gl_dispatch_stub_1577, NULL, 1577),
    NAME_FUNC_OFFSET(31114, gl_dispatch_stub_1578, gl_dispatch_stub_1578, NULL, 1578),
    NAME_FUNC_OFFSET(31144, gl_dispatch_stub_1579, gl_dispatch_stub_1579, NULL, 1579),
    NAME_FUNC_OFFSET(31172, gl_dispatch_stub_1580, gl_dispatch_stub_1580, NULL, 1580),
    NAME_FUNC_OFFSET(31200, gl_dispatch_stub_1581, gl_dispatch_stub_1581, NULL, 1581),
    NAME_FUNC_OFFSET(31230, gl_dispatch_stub_1582, gl_dispatch_stub_1582, NULL, 1582),
    NAME_FUNC_OFFSET(31260, gl_dispatch_stub_1583, gl_dispatch_stub_1583, NULL, 1583),
    NAME_FUNC_OFFSET(31286, gl_dispatch_stub_1584, gl_dispatch_stub_1584, NULL, 1584),
    NAME_FUNC_OFFSET(31315, gl_dispatch_stub_1585, gl_dispatch_stub_1585, NULL, 1585),
    NAME_FUNC_OFFSET(31347, gl_dispatch_stub_1586, gl_dispatch_stub_1586, NULL, 1586),
    NAME_FUNC_OFFSET(31383, gl_dispatch_stub_1587, gl_dispatch_stub_1587, NULL, 1587),
    NAME_FUNC_OFFSET(31419, gl_dispatch_stub_1588, gl_dispatch_stub_1588, NULL, 1588),
    NAME_FUNC_OFFSET(31455, gl_dispatch_stub_1589, gl_dispatch_stub_1589, NULL, 1589),
    NAME_FUNC_OFFSET(31479, gl_dispatch_stub_1590, gl_dispatch_stub_1590, NULL, 1590),
    NAME_FUNC_OFFSET(31512, gl_dispatch_stub_1591, gl_dispatch_stub_1591, NULL, 1591),
    NAME_FUNC_OFFSET(31545, gl_dispatch_stub_1592, gl_dispatch_stub_1592, NULL, 1592),
    NAME_FUNC_OFFSET(31578, gl_dispatch_stub_1593, gl_dispatch_stub_1593, NULL, 1593),
    NAME_FUNC_OFFSET(31613, gl_dispatch_stub_1594, gl_dispatch_stub_1594, NULL, 1594),
    NAME_FUNC_OFFSET(31649, gl_dispatch_stub_1595, gl_dispatch_stub_1595, NULL, 1595),
    NAME_FUNC_OFFSET(31685, gl_dispatch_stub_1596, gl_dispatch_stub_1596, NULL, 1596),
    NAME_FUNC_OFFSET(31721, gl_dispatch_stub_1597, gl_dispatch_stub_1597, NULL, 1597),
    NAME_FUNC_OFFSET(31758, gl_dispatch_stub_1598, gl_dispatch_stub_1598, NULL, 1598),
    NAME_FUNC_OFFSET(31789, gl_dispatch_stub_1599, gl_dispatch_stub_1599, NULL, 1599),
    NAME_FUNC_OFFSET(31806, gl_dispatch_stub_1600, gl_dispatch_stub_1600, NULL, 1600),
    NAME_FUNC_OFFSET(31829, gl_dispatch_stub_1601, gl_dispatch_stub_1601, NULL, 1601),
    NAME_FUNC_OFFSET(31855, gl_dispatch_stub_1602, gl_dispatch_stub_1602, NULL, 1602),
    NAME_FUNC_OFFSET(31874, gl_dispatch_stub_1603, gl_dispatch_stub_1603, NULL, 1603),
    NAME_FUNC_OFFSET(31894, gl_dispatch_stub_1604, gl_dispatch_stub_1604, NULL, 1604),
    NAME_FUNC_OFFSET(31916, gl_dispatch_stub_1605, gl_dispatch_stub_1605, NULL, 1605),
    NAME_FUNC_OFFSET(31946, gl_dispatch_stub_1606, gl_dispatch_stub_1606, NULL, 1606),
    NAME_FUNC_OFFSET(31980, gl_dispatch_stub_1607, gl_dispatch_stub_1607, NULL, 1607),
    NAME_FUNC_OFFSET(32001, gl_dispatch_stub_1608, gl_dispatch_stub_1608, NULL, 1608),
    NAME_FUNC_OFFSET(32021, gl_dispatch_stub_1609, gl_dispatch_stub_1609, NULL, 1609),
    NAME_FUNC_OFFSET(32054, gl_dispatch_stub_1610, gl_dispatch_stub_1610, NULL, 1610),
    NAME_FUNC_OFFSET(32086, gl_dispatch_stub_1611, gl_dispatch_stub_1611, NULL, 1611),
    NAME_FUNC_OFFSET(32099, gl_dispatch_stub_1612, gl_dispatch_stub_1612, NULL, 1612),
    NAME_FUNC_OFFSET(32113, gl_dispatch_stub_1613, gl_dispatch_stub_1613, NULL, 1613),
    NAME_FUNC_OFFSET(32126, gl_dispatch_stub_1614, gl_dispatch_stub_1614, NULL, 1614),
    NAME_FUNC_OFFSET(32140, gl_dispatch_stub_1615, gl_dispatch_stub_1615, NULL, 1615),
    NAME_FUNC_OFFSET(32153, gl_dispatch_stub_1616, gl_dispatch_stub_1616, NULL, 1616),
    NAME_FUNC_OFFSET(32167, gl_dispatch_stub_1617, gl_dispatch_stub_1617, NULL, 1617),
    NAME_FUNC_OFFSET(32180, gl_dispatch_stub_1618, gl_dispatch_stub_1618, NULL, 1618),
    NAME_FUNC_OFFSET(32194, gl_dispatch_stub_1619, gl_dispatch_stub_1619, NULL, 1619),
    NAME_FUNC_OFFSET(32206, gl_dispatch_stub_1620, gl_dispatch_stub_1620, NULL, 1620),
    NAME_FUNC_OFFSET(32219, gl_dispatch_stub_1621, gl_dispatch_stub_1621, NULL, 1621),
    NAME_FUNC_OFFSET(32231, gl_dispatch_stub_1622, gl_dispatch_stub_1622, NULL, 1622),
    NAME_FUNC_OFFSET(32244, gl_dispatch_stub_1623, gl_dispatch_stub_1623, NULL, 1623),
    NAME_FUNC_OFFSET(32259, gl_dispatch_stub_1624, gl_dispatch_stub_1624, NULL, 1624),
    NAME_FUNC_OFFSET(32275, gl_dispatch_stub_1625, gl_dispatch_stub_1625, NULL, 1625),
    NAME_FUNC_OFFSET(32290, gl_dispatch_stub_1626, gl_dispatch_stub_1626, NULL, 1626),
    NAME_FUNC_OFFSET(32306, gl_dispatch_stub_1627, gl_dispatch_stub_1627, NULL, 1627),
    NAME_FUNC_OFFSET(32321, gl_dispatch_stub_1628, gl_dispatch_stub_1628, NULL, 1628),
    NAME_FUNC_OFFSET(32337, gl_dispatch_stub_1629, gl_dispatch_stub_1629, NULL, 1629),
    NAME_FUNC_OFFSET(32352, gl_dispatch_stub_1630, gl_dispatch_stub_1630, NULL, 1630),
    NAME_FUNC_OFFSET(32368, gl_dispatch_stub_1631, gl_dispatch_stub_1631, NULL, 1631),
    NAME_FUNC_OFFSET(32388, gl_dispatch_stub_1632, gl_dispatch_stub_1632, NULL, 1632),
    NAME_FUNC_OFFSET(32409, gl_dispatch_stub_1633, gl_dispatch_stub_1633, NULL, 1633),
    NAME_FUNC_OFFSET(32429, gl_dispatch_stub_1634, gl_dispatch_stub_1634, NULL, 1634),
    NAME_FUNC_OFFSET(32450, gl_dispatch_stub_1635, gl_dispatch_stub_1635, NULL, 1635),
    NAME_FUNC_OFFSET(32470, gl_dispatch_stub_1636, gl_dispatch_stub_1636, NULL, 1636),
    NAME_FUNC_OFFSET(32491, gl_dispatch_stub_1637, gl_dispatch_stub_1637, NULL, 1637),
    NAME_FUNC_OFFSET(32511, gl_dispatch_stub_1638, gl_dispatch_stub_1638, NULL, 1638),
    NAME_FUNC_OFFSET(32532, gl_dispatch_stub_1639, gl_dispatch_stub_1639, NULL, 1639),
    NAME_FUNC_OFFSET(32546, gl_dispatch_stub_1640, gl_dispatch_stub_1640, NULL, 1640),
    NAME_FUNC_OFFSET(32561, gl_dispatch_stub_1641, gl_dispatch_stub_1641, NULL, 1641),
    NAME_FUNC_OFFSET(32582, gl_dispatch_stub_1642, gl_dispatch_stub_1642, NULL, 1642),
    NAME_FUNC_OFFSET(32604, gl_dispatch_stub_1643, gl_dispatch_stub_1643, NULL, 1643),
    NAME_FUNC_OFFSET(32623, gl_dispatch_stub_1644, gl_dispatch_stub_1644, NULL, 1644),
    NAME_FUNC_OFFSET(32642, gl_dispatch_stub_1645, gl_dispatch_stub_1645, NULL, 1645),
    NAME_FUNC_OFFSET(32662, gl_dispatch_stub_1646, gl_dispatch_stub_1646, NULL, 1646),
    NAME_FUNC_OFFSET(32681, gl_dispatch_stub_1647, gl_dispatch_stub_1647, NULL, 1647),
    NAME_FUNC_OFFSET(32701, gl_dispatch_stub_1648, gl_dispatch_stub_1648, NULL, 1648),
    NAME_FUNC_OFFSET(32720, gl_dispatch_stub_1649, gl_dispatch_stub_1649, NULL, 1649),
    NAME_FUNC_OFFSET(32740, gl_dispatch_stub_1650, gl_dispatch_stub_1650, NULL, 1650),
    NAME_FUNC_OFFSET(32759, gl_dispatch_stub_1651, gl_dispatch_stub_1651, NULL, 1651),
    NAME_FUNC_OFFSET(32779, gl_dispatch_stub_1652, gl_dispatch_stub_1652, NULL, 1652),
    NAME_FUNC_OFFSET(32800, gl_dispatch_stub_1653, gl_dispatch_stub_1653, NULL, 1653),
    NAME_FUNC_OFFSET(32821, gl_dispatch_stub_1654, gl_dispatch_stub_1654, NULL, 1654),
    NAME_FUNC_OFFSET(32842, gl_dispatch_stub_1655, gl_dispatch_stub_1655, NULL, 1655),
    NAME_FUNC_OFFSET(32863, gl_dispatch_stub_1656, gl_dispatch_stub_1656, NULL, 1656),
    NAME_FUNC_OFFSET(32886, gl_dispatch_stub_1657, gl_dispatch_stub_1657, NULL, 1657),
    NAME_FUNC_OFFSET(32913, gl_dispatch_stub_1658, gl_dispatch_stub_1658, NULL, 1658),
    NAME_FUNC_OFFSET(32942, gl_dispatch_stub_1659, gl_dispatch_stub_1659, NULL, 1659),
    NAME_FUNC_OFFSET(32974, gl_dispatch_stub_1660, gl_dispatch_stub_1660, NULL, 1660),
    NAME_FUNC_OFFSET(33001, gl_dispatch_stub_1661, gl_dispatch_stub_1661, NULL, 1661),
    NAME_FUNC_OFFSET(33031, glGetObjectLabelEXT, glGetObjectLabelEXT, NULL, 1662),
    NAME_FUNC_OFFSET(33051, glLabelObjectEXT, glLabelObjectEXT, NULL, 1663),
    NAME_FUNC_OFFSET(33068, gl_dispatch_stub_1664, gl_dispatch_stub_1664, NULL, 1664),
    NAME_FUNC_OFFSET(33088, gl_dispatch_stub_1665, gl_dispatch_stub_1665, NULL, 1665),
    NAME_FUNC_OFFSET(33110, gl_dispatch_stub_1666, gl_dispatch_stub_1666, NULL, 1666),
    NAME_FUNC_OFFSET(33135, gl_dispatch_stub_1667, gl_dispatch_stub_1667, NULL, 1667),
    NAME_FUNC_OFFSET(33162, gl_dispatch_stub_1668, gl_dispatch_stub_1668, NULL, 1668),
    NAME_FUNC_OFFSET(33202, gl_dispatch_stub_1669, gl_dispatch_stub_1669, NULL, 1669),
    NAME_FUNC_OFFSET(33254, gl_dispatch_stub_1670, gl_dispatch_stub_1670, NULL, 1670),
    NAME_FUNC_OFFSET(33299, gl_dispatch_stub_1671, gl_dispatch_stub_1671, NULL, 1671),
    NAME_FUNC_OFFSET(33320, gl_dispatch_stub_1672, gl_dispatch_stub_1672, NULL, 1672),
    NAME_FUNC_OFFSET(33348, glTexStorageAttribs2DEXT, glTexStorageAttribs2DEXT, NULL, 1673),
    NAME_FUNC_OFFSET(33373, glTexStorageAttribs3DEXT, glTexStorageAttribs3DEXT, NULL, 1674),
    NAME_FUNC_OFFSET(33398, glFramebufferTextureMultiviewOVR, glFramebufferTextureMultiviewOVR, NULL, 1675),
    NAME_FUNC_OFFSET(33431, gl_dispatch_stub_1676, gl_dispatch_stub_1676, NULL, 1676),
    NAME_FUNC_OFFSET(33469, glFramebufferTextureMultisampleMultiviewOVR, glFramebufferTextureMultisampleMultiviewOVR, NULL, 1677),
    NAME_FUNC_OFFSET(33513, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET(33526, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET(33540, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET(33553, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET(33567, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET(33582, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET(33599, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET(33616, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET(33634, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET(33651, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET(33667, glAreTexturesResident, glAreTexturesResidentEXT, glAreTexturesResidentEXT, 322),
    NAME_FUNC_OFFSET(33692, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET(33712, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET(33732, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET(33755, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET(33778, glDeleteTextures, glDeleteTexturesEXT, glDeleteTexturesEXT, 327),
    NAME_FUNC_OFFSET(33798, glGenTextures, glGenTexturesEXT, glGenTexturesEXT, 328),
    NAME_FUNC_OFFSET(33815, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(33832, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(33849, glIsTexture, glIsTextureEXT, glIsTextureEXT, 330),
    NAME_FUNC_OFFSET(33864, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET(33888, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET(33907, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET(33926, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET(33942, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(33961, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(33980, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET(34003, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET(34022, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET(34045, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(34061, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(34077, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(34096, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(34115, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(34138, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(34161, glActiveTexture, glActiveTexture, NULL, 374),
    NAME_FUNC_OFFSET(34180, glClientActiveTexture, glClientActiveTexture, NULL, 375),
    NAME_FUNC_OFFSET(34205, glMultiTexCoord1d, glMultiTexCoord1d, NULL, 376),
    NAME_FUNC_OFFSET(34226, glMultiTexCoord1dv, glMultiTexCoord1dv, NULL, 377),
    NAME_FUNC_OFFSET(34248, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET(34266, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET(34285, glMultiTexCoord1i, glMultiTexCoord1i, NULL, 380),
    NAME_FUNC_OFFSET(34306, glMultiTexCoord1iv, glMultiTexCoord1iv, NULL, 381),
    NAME_FUNC_OFFSET(34328, glMultiTexCoord1s, glMultiTexCoord1s, NULL, 382),
    NAME_FUNC_OFFSET(34349, glMultiTexCoord1sv, glMultiTexCoord1sv, NULL, 383),
    NAME_FUNC_OFFSET(34371, glMultiTexCoord2d, glMultiTexCoord2d, NULL, 384),
    NAME_FUNC_OFFSET(34392, glMultiTexCoord2dv, glMultiTexCoord2dv, NULL, 385),
    NAME_FUNC_OFFSET(34414, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET(34432, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET(34451, glMultiTexCoord2i, glMultiTexCoord2i, NULL, 388),
    NAME_FUNC_OFFSET(34472, glMultiTexCoord2iv, glMultiTexCoord2iv, NULL, 389),
    NAME_FUNC_OFFSET(34494, glMultiTexCoord2s, glMultiTexCoord2s, NULL, 390),
    NAME_FUNC_OFFSET(34515, glMultiTexCoord2sv, glMultiTexCoord2sv, NULL, 391),
    NAME_FUNC_OFFSET(34537, glMultiTexCoord3d, glMultiTexCoord3d, NULL, 392),
    NAME_FUNC_OFFSET(34558, glMultiTexCoord3dv, glMultiTexCoord3dv, NULL, 393),
    NAME_FUNC_OFFSET(34580, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET(34598, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET(34617, glMultiTexCoord3i, glMultiTexCoord3i, NULL, 396),
    NAME_FUNC_OFFSET(34638, glMultiTexCoord3iv, glMultiTexCoord3iv, NULL, 397),
    NAME_FUNC_OFFSET(34660, glMultiTexCoord3s, glMultiTexCoord3s, NULL, 398),
    NAME_FUNC_OFFSET(34681, glMultiTexCoord3sv, glMultiTexCoord3sv, NULL, 399),
    NAME_FUNC_OFFSET(34703, glMultiTexCoord4d, glMultiTexCoord4d, NULL, 400),
    NAME_FUNC_OFFSET(34724, glMultiTexCoord4dv, glMultiTexCoord4dv, NULL, 401),
    NAME_FUNC_OFFSET(34746, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET(34764, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET(34783, glMultiTexCoord4i, glMultiTexCoord4i, NULL, 404),
    NAME_FUNC_OFFSET(34804, glMultiTexCoord4iv, glMultiTexCoord4iv, NULL, 405),
    NAME_FUNC_OFFSET(34826, glMultiTexCoord4s, glMultiTexCoord4s, NULL, 406),
    NAME_FUNC_OFFSET(34847, glMultiTexCoord4sv, glMultiTexCoord4sv, NULL, 407),
    NAME_FUNC_OFFSET(34869, glCompressedTexImage1D, glCompressedTexImage1D, NULL, 408),
    NAME_FUNC_OFFSET(34895, glCompressedTexImage2D, glCompressedTexImage2D, NULL, 409),
    NAME_FUNC_OFFSET(34921, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(34947, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(34973, glCompressedTexSubImage1D, glCompressedTexSubImage1D, NULL, 411),
    NAME_FUNC_OFFSET(35002, glCompressedTexSubImage2D, glCompressedTexSubImage2D, NULL, 412),
    NAME_FUNC_OFFSET(35031, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(35060, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(35089, glGetCompressedTexImage, glGetCompressedTexImage, NULL, 414),
    NAME_FUNC_OFFSET(35116, glLoadTransposeMatrixd, glLoadTransposeMatrixd, NULL, 415),
    NAME_FUNC_OFFSET(35142, glLoadTransposeMatrixf, glLoadTransposeMatrixf, NULL, 416),
    NAME_FUNC_OFFSET(35168, glMultTransposeMatrixd, glMultTransposeMatrixd, NULL, 417),
    NAME_FUNC_OFFSET(35194, glMultTransposeMatrixf, glMultTransposeMatrixf, NULL, 418),
    NAME_FUNC_OFFSET(35220, glSampleCoverage, glSampleCoverage, NULL, 419),
    NAME_FUNC_OFFSET(35240, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(35263, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(35287, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(35310, glFogCoordPointer, glFogCoordPointer, NULL, 421),
    NAME_FUNC_OFFSET(35331, glFogCoordd, glFogCoordd, NULL, 422),
    NAME_FUNC_OFFSET(35346, glFogCoorddv, glFogCoorddv, NULL, 423),
    NAME_FUNC_OFFSET(35362, glMultiDrawArrays, glMultiDrawArrays, NULL, 424),
    NAME_FUNC_OFFSET(35383, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(35404, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(35425, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(35447, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(35469, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(35491, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(35514, glPointParameteri, glPointParameteri, NULL, 427),
    NAME_FUNC_OFFSET(35534, glPointParameteriv, glPointParameteriv, NULL, 428),
    NAME_FUNC_OFFSET(35555, glSecondaryColor3b, glSecondaryColor3b, NULL, 429),
    NAME_FUNC_OFFSET(35577, glSecondaryColor3bv, glSecondaryColor3bv, NULL, 430),
    NAME_FUNC_OFFSET(35600, glSecondaryColor3d, glSecondaryColor3d, NULL, 431),
    NAME_FUNC_OFFSET(35622, glSecondaryColor3dv, glSecondaryColor3dv, NULL, 432),
    NAME_FUNC_OFFSET(35645, glSecondaryColor3i, glSecondaryColor3i, NULL, 433),
    NAME_FUNC_OFFSET(35667, glSecondaryColor3iv, glSecondaryColor3iv, NULL, 434),
    NAME_FUNC_OFFSET(35690, glSecondaryColor3s, glSecondaryColor3s, NULL, 435),
    NAME_FUNC_OFFSET(35712, glSecondaryColor3sv, glSecondaryColor3sv, NULL, 436),
    NAME_FUNC_OFFSET(35735, glSecondaryColor3ub, glSecondaryColor3ub, NULL, 437),
    NAME_FUNC_OFFSET(35758, glSecondaryColor3ubv, glSecondaryColor3ubv, NULL, 438),
    NAME_FUNC_OFFSET(35782, glSecondaryColor3ui, glSecondaryColor3ui, NULL, 439),
    NAME_FUNC_OFFSET(35805, glSecondaryColor3uiv, glSecondaryColor3uiv, NULL, 440),
    NAME_FUNC_OFFSET(35829, glSecondaryColor3us, glSecondaryColor3us, NULL, 441),
    NAME_FUNC_OFFSET(35852, glSecondaryColor3usv, glSecondaryColor3usv, NULL, 442),
    NAME_FUNC_OFFSET(35876, glSecondaryColorPointer, glSecondaryColorPointer, NULL, 443),
    NAME_FUNC_OFFSET(35903, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(35920, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(35938, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(35956, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(35975, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(35992, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(36010, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(36028, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(36047, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(36064, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(36082, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(36100, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(36119, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(36136, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(36154, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(36172, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(36191, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(36208, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(36226, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(36244, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(36263, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(36280, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(36298, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(36316, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(36335, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(36352, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(36370, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(36388, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(36407, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(36424, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(36442, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(36460, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(36479, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET(36495, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET(36511, glBindBuffer, glBindBuffer, NULL, 461),
    NAME_FUNC_OFFSET(36527, glBufferData, glBufferData, NULL, 462),
    NAME_FUNC_OFFSET(36543, glBufferSubData, glBufferSubData, NULL, 463),
    NAME_FUNC_OFFSET(36562, glDeleteBuffers, glDeleteBuffers, NULL, 464),
    NAME_FUNC_OFFSET(36581, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET(36600, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET(36619, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET(36633, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET(36647, glGenBuffers, glGenBuffers, NULL, 467),
    NAME_FUNC_OFFSET(36663, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET(36679, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET(36695, glGetBufferParameteriv, glGetBufferParameteriv, NULL, 469),
    NAME_FUNC_OFFSET(36721, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(36744, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(36767, glGetBufferSubData, glGetBufferSubData, NULL, 471),
    NAME_FUNC_OFFSET(36789, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET(36811, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET(36833, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET(36856, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET(36879, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET(36895, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET(36911, glIsBuffer, glIsBuffer, NULL, 475),
    NAME_FUNC_OFFSET(36925, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET(36938, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET(36951, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(36966, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(36981, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(36998, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(37015, glBindAttribLocation, glBindAttribLocation, NULL, 480),
    NAME_FUNC_OFFSET(37039, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(37066, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(37093, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(37120, glCompileShader, glCompileShader, NULL, 482),
    NAME_FUNC_OFFSET(37139, glDisableVertexAttribArray, glDisableVertexAttribArray, NULL, 488),
    NAME_FUNC_OFFSET(37169, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(37186, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(37203, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(37219, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(37236, glEnableVertexAttribArray, glEnableVertexAttribArray, NULL, 490),
    NAME_FUNC_OFFSET(37265, glGetActiveAttrib, glGetActiveAttrib, NULL, 491),
    NAME_FUNC_OFFSET(37286, glGetActiveUniform, glGetActiveUniform, NULL, 492),
    NAME_FUNC_OFFSET(37308, glGetAttribLocation, glGetAttribLocation, NULL, 494),
    NAME_FUNC_OFFSET(37331, glGetShaderSource, glGetShaderSource, NULL, 498),
    NAME_FUNC_OFFSET(37352, glGetUniformLocation, glGetUniformLocation, NULL, 500),
    NAME_FUNC_OFFSET(37376, glGetUniformfv, glGetUniformfv, NULL, 501),
    NAME_FUNC_OFFSET(37394, glGetUniformiv, glGetUniformiv, NULL, 502),
    NAME_FUNC_OFFSET(37412, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(37441, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(37469, glGetVertexAttribdv, glGetVertexAttribdv, NULL, 504),
    NAME_FUNC_OFFSET(37492, glGetVertexAttribfv, glGetVertexAttribfv, NULL, 505),
    NAME_FUNC_OFFSET(37515, glGetVertexAttribiv, glGetVertexAttribiv, NULL, 506),
    NAME_FUNC_OFFSET(37538, glLinkProgram, glLinkProgram, NULL, 509),
    NAME_FUNC_OFFSET(37555, glShaderSource, glShaderSource, NULL, 510),
    NAME_FUNC_OFFSET(37573, glStencilOpSeparate, glStencilOpSeparate, NULL, 513),
    NAME_FUNC_OFFSET(37596, glUniform1f, glUniform1f, NULL, 514),
    NAME_FUNC_OFFSET(37611, glUniform1fv, glUniform1fv, NULL, 515),
    NAME_FUNC_OFFSET(37627, glUniform1i, glUniform1i, NULL, 516),
    NAME_FUNC_OFFSET(37642, glUniform1iv, glUniform1iv, NULL, 517),
    NAME_FUNC_OFFSET(37658, glUniform2f, glUniform2f, NULL, 518),
    NAME_FUNC_OFFSET(37673, glUniform2fv, glUniform2fv, NULL, 519),
    NAME_FUNC_OFFSET(37689, glUniform2i, glUniform2i, NULL, 520),
    NAME_FUNC_OFFSET(37704, glUniform2iv, glUniform2iv, NULL, 521),
    NAME_FUNC_OFFSET(37720, glUniform3f, glUniform3f, NULL, 522),
    NAME_FUNC_OFFSET(37735, glUniform3fv, glUniform3fv, NULL, 523),
    NAME_FUNC_OFFSET(37751, glUniform3i, glUniform3i, NULL, 524),
    NAME_FUNC_OFFSET(37766, glUniform3iv, glUniform3iv, NULL, 525),
    NAME_FUNC_OFFSET(37782, glUniform4f, glUniform4f, NULL, 526),
    NAME_FUNC_OFFSET(37797, glUniform4fv, glUniform4fv, NULL, 527),
    NAME_FUNC_OFFSET(37813, glUniform4i, glUniform4i, NULL, 528),
    NAME_FUNC_OFFSET(37828, glUniform4iv, glUniform4iv, NULL, 529),
    NAME_FUNC_OFFSET(37844, glUniformMatrix2fv, glUniformMatrix2fv, NULL, 530),
    NAME_FUNC_OFFSET(37866, glUniformMatrix3fv, glUniformMatrix3fv, NULL, 531),
    NAME_FUNC_OFFSET(37888, glUniformMatrix4fv, glUniformMatrix4fv, NULL, 532),
    NAME_FUNC_OFFSET(37910, glUseProgram, glUseProgram, NULL, 533),
    NAME_FUNC_OFFSET(37932, glValidateProgram, glValidateProgram, NULL, 534),
    NAME_FUNC_OFFSET(37953, glVertexAttrib1d, glVertexAttrib1d, NULL, 535),
    NAME_FUNC_OFFSET(37973, glVertexAttrib1dv, glVertexAttrib1dv, NULL, 536),
    NAME_FUNC_OFFSET(37994, glVertexAttrib1s, glVertexAttrib1s, NULL, 537),
    NAME_FUNC_OFFSET(38014, glVertexAttrib1sv, glVertexAttrib1sv, NULL, 538),
    NAME_FUNC_OFFSET(38035, glVertexAttrib2d, glVertexAttrib2d, NULL, 539),
    NAME_FUNC_OFFSET(38055, glVertexAttrib2dv, glVertexAttrib2dv, NULL, 540),
    NAME_FUNC_OFFSET(38076, glVertexAttrib2s, glVertexAttrib2s, NULL, 541),
    NAME_FUNC_OFFSET(38096, glVertexAttrib2sv, glVertexAttrib2sv, NULL, 542),
    NAME_FUNC_OFFSET(38117, glVertexAttrib3d, glVertexAttrib3d, NULL, 543),
    NAME_FUNC_OFFSET(38137, glVertexAttrib3dv, glVertexAttrib3dv, NULL, 544),
    NAME_FUNC_OFFSET(38158, glVertexAttrib3s, glVertexAttrib3s, NULL, 545),
    NAME_FUNC_OFFSET(38178, glVertexAttrib3sv, glVertexAttrib3sv, NULL, 546),
    NAME_FUNC_OFFSET(38199, glVertexAttrib4Nbv, glVertexAttrib4Nbv, NULL, 547),
    NAME_FUNC_OFFSET(38221, glVertexAttrib4Niv, glVertexAttrib4Niv, NULL, 548),
    NAME_FUNC_OFFSET(38243, glVertexAttrib4Nsv, glVertexAttrib4Nsv, NULL, 549),
    NAME_FUNC_OFFSET(38265, glVertexAttrib4Nub, glVertexAttrib4Nub, NULL, 550),
    NAME_FUNC_OFFSET(38287, glVertexAttrib4Nubv, glVertexAttrib4Nubv, NULL, 551),
    NAME_FUNC_OFFSET(38310, glVertexAttrib4Nuiv, glVertexAttrib4Nuiv, NULL, 552),
    NAME_FUNC_OFFSET(38333, glVertexAttrib4Nusv, glVertexAttrib4Nusv, NULL, 553),
    NAME_FUNC_OFFSET(38356, glVertexAttrib4bv, glVertexAttrib4bv, NULL, 554),
    NAME_FUNC_OFFSET(38377, glVertexAttrib4d, glVertexAttrib4d, NULL, 555),
    NAME_FUNC_OFFSET(38397, glVertexAttrib4dv, glVertexAttrib4dv, NULL, 556),
    NAME_FUNC_OFFSET(38418, glVertexAttrib4iv, glVertexAttrib4iv, NULL, 557),
    NAME_FUNC_OFFSET(38439, glVertexAttrib4s, glVertexAttrib4s, NULL, 558),
    NAME_FUNC_OFFSET(38459, glVertexAttrib4sv, glVertexAttrib4sv, NULL, 559),
    NAME_FUNC_OFFSET(38480, glVertexAttrib4ubv, glVertexAttrib4ubv, NULL, 560),
    NAME_FUNC_OFFSET(38502, glVertexAttrib4uiv, glVertexAttrib4uiv, NULL, 561),
    NAME_FUNC_OFFSET(38524, glVertexAttrib4usv, glVertexAttrib4usv, NULL, 562),
    NAME_FUNC_OFFSET(38546, glVertexAttribPointer, glVertexAttribPointer, NULL, 563),
    NAME_FUNC_OFFSET(38571, glBeginConditionalRender, glBeginConditionalRender, NULL, 570),
    NAME_FUNC_OFFSET(38598, glBeginTransformFeedback, glBeginTransformFeedback, NULL, 571),
    NAME_FUNC_OFFSET(38626, glBindBufferBase, glBindBufferBase, NULL, 572),
    NAME_FUNC_OFFSET(38646, glBindBufferRange, glBindBufferRange, NULL, 573),
    NAME_FUNC_OFFSET(38667, glBindFragDataLocation, glBindFragDataLocation, NULL, 574),
    NAME_FUNC_OFFSET(38693, glClampColor, glClampColor, NULL, 575),
    NAME_FUNC_OFFSET(38709, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(38731, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(38747, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(38763, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(38783, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(38797, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(38811, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(38830, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(38843, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(38856, glEndConditionalRender, glEndConditionalRender, NULL, 583),
    NAME_FUNC_OFFSET(38881, glEndTransformFeedback, glEndTransformFeedback, NULL, 584),
    NAME_FUNC_OFFSET(38907, glGetBooleani_v, glGetBooleani_v, NULL, 585),
    NAME_FUNC_OFFSET(38931, glGetFragDataLocation, glGetFragDataLocation, NULL, 586),
    NAME_FUNC_OFFSET(38956, glGetIntegeri_v, glGetIntegeri_v, NULL, 587),
    NAME_FUNC_OFFSET(38980, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET(39004, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET(39028, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET(39053, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET(39078, glGetTransformFeedbackVarying, glGetTransformFeedbackVarying, NULL, 591),
    NAME_FUNC_OFFSET(39111, glGetUniformuiv, glGetUniformuiv, NULL, 592),
    NAME_FUNC_OFFSET(39130, glGetVertexAttribIiv, glGetVertexAttribIiv, NULL, 593),
    NAME_FUNC_OFFSET(39154, glGetVertexAttribIuiv, glGetVertexAttribIuiv, NULL, 594),
    NAME_FUNC_OFFSET(39179, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(39201, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(39217, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(39233, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET(39254, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET(39275, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET(39297, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET(39319, glTransformFeedbackVaryings, glTransformFeedbackVaryings, NULL, 598),
    NAME_FUNC_OFFSET(39350, glUniform1ui, glUniform1ui, NULL, 599),
    NAME_FUNC_OFFSET(39366, glUniform1uiv, glUniform1uiv, NULL, 600),
    NAME_FUNC_OFFSET(39383, glUniform2ui, glUniform2ui, NULL, 601),
    NAME_FUNC_OFFSET(39399, glUniform2uiv, glUniform2uiv, NULL, 602),
    NAME_FUNC_OFFSET(39416, glUniform3ui, glUniform3ui, NULL, 603),
    NAME_FUNC_OFFSET(39432, glUniform3uiv, glUniform3uiv, NULL, 604),
    NAME_FUNC_OFFSET(39449, glUniform4ui, glUniform4ui, NULL, 605),
    NAME_FUNC_OFFSET(39465, glUniform4uiv, glUniform4uiv, NULL, 606),
    NAME_FUNC_OFFSET(39482, glVertexAttribI1iv, glVertexAttribI1iv, NULL, 607),
    NAME_FUNC_OFFSET(39504, glVertexAttribI1uiv, glVertexAttribI1uiv, NULL, 608),
    NAME_FUNC_OFFSET(39527, glVertexAttribI4bv, glVertexAttribI4bv, NULL, 609),
    NAME_FUNC_OFFSET(39549, glVertexAttribI4sv, glVertexAttribI4sv, NULL, 610),
    NAME_FUNC_OFFSET(39571, glVertexAttribI4ubv, glVertexAttribI4ubv, NULL, 611),
    NAME_FUNC_OFFSET(39594, glVertexAttribI4usv, glVertexAttribI4usv, NULL, 612),
    NAME_FUNC_OFFSET(39617, glVertexAttribIPointer, glVertexAttribIPointer, NULL, 613),
    NAME_FUNC_OFFSET(39643, glPrimitiveRestartIndex, glPrimitiveRestartIndex, NULL, 614),
    NAME_FUNC_OFFSET(39669, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(39684, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(39699, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(39714, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET(39738, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET(39762, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET(39787, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET(39812, glMinSampleShading, glMinSampleShading, NULL, 620),
    NAME_FUNC_OFFSET(39834, glMinSampleShading, glMinSampleShading, NULL, 620),
    NAME_FUNC_OFFSET(39856, glBindProgramARB, glBindProgramARB, NULL, 622),
    NAME_FUNC_OFFSET(39872, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 623),
    NAME_FUNC_OFFSET(39891, glGenProgramsARB, glGenProgramsARB, NULL, 624),
    NAME_FUNC_OFFSET(39907, glIsProgramARB, glIsProgramARB, NULL, 631),
    NAME_FUNC_OFFSET(39921, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 632),
    NAME_FUNC_OFFSET(39944, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 633),
    NAME_FUNC_OFFSET(39968, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 634),
    NAME_FUNC_OFFSET(39991, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 635),
    NAME_FUNC_OFFSET(40015, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 641),
    NAME_FUNC_OFFSET(40032, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 642),
    NAME_FUNC_OFFSET(40050, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 643),
    NAME_FUNC_OFFSET(40067, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 644),
    NAME_FUNC_OFFSET(40085, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 645),
    NAME_FUNC_OFFSET(40102, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 646),
    NAME_FUNC_OFFSET(40120, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 647),
    NAME_FUNC_OFFSET(40137, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 648),
    NAME_FUNC_OFFSET(40155, glDrawArraysInstanced, glDrawArraysInstanced, NULL, 659),
    NAME_FUNC_OFFSET(40180, glDrawArraysInstanced, glDrawArraysInstanced, NULL, 659),
    NAME_FUNC_OFFSET(40205, glDrawElementsInstanced, glDrawElementsInstanced, NULL, 660),
    NAME_FUNC_OFFSET(40232, glDrawElementsInstanced, glDrawElementsInstanced, NULL, 660),
    NAME_FUNC_OFFSET(40259, glBindFramebuffer, glBindFramebuffer, NULL, 661),
    NAME_FUNC_OFFSET(40280, glBindRenderbuffer, glBindRenderbuffer, NULL, 662),
    NAME_FUNC_OFFSET(40302, glBlitFramebuffer, glBlitFramebuffer, NULL, 663),
    NAME_FUNC_OFFSET(40323, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 664),
    NAME_FUNC_OFFSET(40351, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 664),
    NAME_FUNC_OFFSET(40379, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 665),
    NAME_FUNC_OFFSET(40403, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 665),
    NAME_FUNC_OFFSET(40427, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 666),
    NAME_FUNC_OFFSET(40452, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 666),
    NAME_FUNC_OFFSET(40477, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 667),
    NAME_FUNC_OFFSET(40506, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 667),
    NAME_FUNC_OFFSET(40535, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 668),
    NAME_FUNC_OFFSET(40561, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 669),
    NAME_FUNC_OFFSET(40587, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 669),
    NAME_FUNC_OFFSET(40613, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 670),
    NAME_FUNC_OFFSET(40639, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 670),
    NAME_FUNC_OFFSET(40665, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 671),
    NAME_FUNC_OFFSET(40694, glGenFramebuffers, glGenFramebuffers, NULL, 672),
    NAME_FUNC_OFFSET(40715, glGenFramebuffers, glGenFramebuffers, NULL, 672),
    NAME_FUNC_OFFSET(40736, glGenRenderbuffers, glGenRenderbuffers, NULL, 673),
    NAME_FUNC_OFFSET(40758, glGenRenderbuffers, glGenRenderbuffers, NULL, 673),
    NAME_FUNC_OFFSET(40780, glGenerateMipmap, glGenerateMipmap, NULL, 674),
    NAME_FUNC_OFFSET(40800, glGenerateMipmap, glGenerateMipmap, NULL, 674),
    NAME_FUNC_OFFSET(40820, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(40861, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 675),
    NAME_FUNC_OFFSET(40902, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 676),
    NAME_FUNC_OFFSET(40934, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 676),
    NAME_FUNC_OFFSET(40966, glIsFramebuffer, glIsFramebuffer, NULL, 677),
    NAME_FUNC_OFFSET(40985, glIsFramebuffer, glIsFramebuffer, NULL, 677),
    NAME_FUNC_OFFSET(41004, glIsRenderbuffer, glIsRenderbuffer, NULL, 678),
    NAME_FUNC_OFFSET(41024, glIsRenderbuffer, glIsRenderbuffer, NULL, 678),
    NAME_FUNC_OFFSET(41044, glRenderbufferStorage, glRenderbufferStorage, NULL, 679),
    NAME_FUNC_OFFSET(41069, glRenderbufferStorage, glRenderbufferStorage, NULL, 679),
    NAME_FUNC_OFFSET(41094, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 680),
    NAME_FUNC_OFFSET(41130, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(41158, glMapBufferRange, glMapBufferRange, NULL, 682),
    NAME_FUNC_OFFSET(41178, glBindVertexArray, glBindVertexArray, NULL, 683),
    NAME_FUNC_OFFSET(41199, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(41223, glGenVertexArrays, glGenVertexArrays, NULL, 685),
    NAME_FUNC_OFFSET(41244, glIsVertexArray, glIsVertexArray, NULL, 686),
    NAME_FUNC_OFFSET(41263, glClientWaitSync, glClientWaitSync, NULL, 695),
    NAME_FUNC_OFFSET(41285, glDeleteSync, glDeleteSync, NULL, 696),
    NAME_FUNC_OFFSET(41303, glFenceSync, glFenceSync, NULL, 697),
    NAME_FUNC_OFFSET(41320, glGetInteger64v, glGetInteger64v, NULL, 698),
    NAME_FUNC_OFFSET(41341, glGetInteger64v, glGetInteger64v, NULL, 698),
    NAME_FUNC_OFFSET(41360, glGetSynciv, glGetSynciv, NULL, 699),
    NAME_FUNC_OFFSET(41377, glIsSync, glIsSync, NULL, 700),
    NAME_FUNC_OFFSET(41391, glWaitSync, glWaitSync, NULL, 701),
    NAME_FUNC_OFFSET(41407, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 702),
    NAME_FUNC_OFFSET(41435, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 702),
    NAME_FUNC_OFFSET(41463, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 703),
    NAME_FUNC_OFFSET(41500, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 703),
    NAME_FUNC_OFFSET(41537, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(41575, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(41608, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(41641, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 705),
    NAME_FUNC_OFFSET(41679, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 705),
    NAME_FUNC_OFFSET(41712, glProvokingVertex, glProvokingVertex, NULL, 706),
    NAME_FUNC_OFFSET(41733, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(41767, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(41792, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(41820, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 711),
    NAME_FUNC_OFFSET(41848, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(41874, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(41891, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(41911, glBlendEquationiARB, glBlendEquationiARB, NULL, 712),
    NAME_FUNC_OFFSET(41931, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(41961, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(41982, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(42006, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 713),
    NAME_FUNC_OFFSET(42030, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(42052, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(42065, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(42081, glBlendFunciARB, glBlendFunciARB, NULL, 714),
    NAME_FUNC_OFFSET(42097, glBindFragDataLocationIndexed, glBindFragDataLocationIndexed, NULL, 715),
    NAME_FUNC_OFFSET(42130, glGetFragDataIndex, glGetFragDataIndex, NULL, 716),
    NAME_FUNC_OFFSET(42152, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 720),
    NAME_FUNC_OFFSET(42180, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 720),
    NAME_FUNC_OFFSET(42208, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 721),
    NAME_FUNC_OFFSET(42237, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 721),
    NAME_FUNC_OFFSET(42266, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 725),
    NAME_FUNC_OFFSET(42291, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 725),
    NAME_FUNC_OFFSET(42316, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 726),
    NAME_FUNC_OFFSET(42342, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 726),
    NAME_FUNC_OFFSET(42368, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(42392, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(42417, gl_dispatch_stub_733, gl_dispatch_stub_733, NULL, 733),
    NAME_FUNC_OFFSET(42435, glPatchParameteri, glPatchParameteri, NULL, 801),
    NAME_FUNC_OFFSET(42456, glPatchParameteri, glPatchParameteri, NULL, 801),
    NAME_FUNC_OFFSET(42477, glClearDepthf, glClearDepthf, NULL, 813),
    NAME_FUNC_OFFSET(42494, glDepthRangef, glDepthRangef, NULL, 814),
    NAME_FUNC_OFFSET(42511, glGetProgramBinary, glGetProgramBinary, NULL, 818),
    NAME_FUNC_OFFSET(42533, glProgramBinary, glProgramBinary, NULL, 819),
    NAME_FUNC_OFFSET(42552, glProgramParameteri, glProgramParameteri, NULL, 820),
    NAME_FUNC_OFFSET(42575, gl_dispatch_stub_821, gl_dispatch_stub_821, NULL, 821),
    NAME_FUNC_OFFSET(42599, gl_dispatch_stub_822, gl_dispatch_stub_822, NULL, 822),
    NAME_FUNC_OFFSET(42620, gl_dispatch_stub_823, gl_dispatch_stub_823, NULL, 823),
    NAME_FUNC_OFFSET(42642, gl_dispatch_stub_824, gl_dispatch_stub_824, NULL, 824),
    NAME_FUNC_OFFSET(42663, gl_dispatch_stub_825, gl_dispatch_stub_825, NULL, 825),
    NAME_FUNC_OFFSET(42685, gl_dispatch_stub_826, gl_dispatch_stub_826, NULL, 826),
    NAME_FUNC_OFFSET(42706, gl_dispatch_stub_827, gl_dispatch_stub_827, NULL, 827),
    NAME_FUNC_OFFSET(42728, gl_dispatch_stub_828, gl_dispatch_stub_828, NULL, 828),
    NAME_FUNC_OFFSET(42749, gl_dispatch_stub_829, gl_dispatch_stub_829, NULL, 829),
    NAME_FUNC_OFFSET(42771, gl_dispatch_stub_830, gl_dispatch_stub_830, NULL, 830),
    NAME_FUNC_OFFSET(42797, glGetDoublei_v, glGetDoublei_v, NULL, 833),
    NAME_FUNC_OFFSET(42820, glGetDoublei_v, glGetDoublei_v, NULL, 833),
    NAME_FUNC_OFFSET(42838, glGetFloati_v, glGetFloati_v, NULL, 834),
    NAME_FUNC_OFFSET(42860, glGetFloati_v, glGetFloati_v, NULL, 834),
    NAME_FUNC_OFFSET(42877, glGetFloati_v, glGetFloati_v, NULL, 834),
    NAME_FUNC_OFFSET(42894, glScissorArrayv, glScissorArrayv, NULL, 835),
    NAME_FUNC_OFFSET(42913, glScissorIndexed, glScissorIndexed, NULL, 836),
    NAME_FUNC_OFFSET(42933, glScissorIndexedv, glScissorIndexedv, NULL, 837),
    NAME_FUNC_OFFSET(42954, glViewportArrayv, glViewportArrayv, NULL, 838),
    NAME_FUNC_OFFSET(42974, glViewportIndexedf, glViewportIndexedf, NULL, 839),
    NAME_FUNC_OFFSET(42996, glViewportIndexedfv, glViewportIndexedfv, NULL, 840),
    NAME_FUNC_OFFSET(43019, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 841),
    NAME_FUNC_OFFSET(43044, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 841),
    NAME_FUNC_OFFSET(43072, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 841),
    NAME_FUNC_OFFSET(43100, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 857),
    NAME_FUNC_OFFSET(43116, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 857),
    NAME_FUNC_OFFSET(43135, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 857),
    NAME_FUNC_OFFSET(43154, glGetnUniformivARB, glGetnUniformivARB, NULL, 858),
    NAME_FUNC_OFFSET(43170, glGetnUniformivARB, glGetnUniformivARB, NULL, 858),
    NAME_FUNC_OFFSET(43189, glGetnUniformivARB, glGetnUniformivARB, NULL, 858),
    NAME_FUNC_OFFSET(43208, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 859),
    NAME_FUNC_OFFSET(43225, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 859),
    NAME_FUNC_OFFSET(43245, glReadnPixelsARB, glReadnPixelsARB, NULL, 860),
    NAME_FUNC_OFFSET(43259, glReadnPixelsARB, glReadnPixelsARB, NULL, 860),
    NAME_FUNC_OFFSET(43276, glReadnPixelsARB, glReadnPixelsARB, NULL, 860),
    NAME_FUNC_OFFSET(43293, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 861),
    NAME_FUNC_OFFSET(43335, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 861),
    NAME_FUNC_OFFSET(43372, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 862),
    NAME_FUNC_OFFSET(43411, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 863),
    NAME_FUNC_OFFSET(43465, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 863),
    NAME_FUNC_OFFSET(43514, glMemoryBarrier, glMemoryBarrier, NULL, 869),
    NAME_FUNC_OFFSET(43533, glTexStorage1D, glTexStorage1D, NULL, 870),
    NAME_FUNC_OFFSET(43551, glTexStorage2D, glTexStorage2D, NULL, 871),
    NAME_FUNC_OFFSET(43569, glTexStorage3D, glTexStorage3D, NULL, 872),
    NAME_FUNC_OFFSET(43587, glCopyImageSubData, glCopyImageSubData, NULL, 880),
    NAME_FUNC_OFFSET(43609, glCopyImageSubData, glCopyImageSubData, NULL, 880),
    NAME_FUNC_OFFSET(43631, glTextureView, glTextureView, NULL, 881),
    NAME_FUNC_OFFSET(43648, glTextureView, glTextureView, NULL, 881),
    NAME_FUNC_OFFSET(43665, glMultiDrawArraysIndirect, glMultiDrawArraysIndirect, NULL, 891),
    NAME_FUNC_OFFSET(43694, glMultiDrawArraysIndirect, glMultiDrawArraysIndirect, NULL, 891),
    NAME_FUNC_OFFSET(43723, glMultiDrawElementsIndirect, glMultiDrawElementsIndirect, NULL, 892),
    NAME_FUNC_OFFSET(43754, glMultiDrawElementsIndirect, glMultiDrawElementsIndirect, NULL, 892),
    NAME_FUNC_OFFSET(43785, gl_dispatch_stub_896, gl_dispatch_stub_896, NULL, 896),
    NAME_FUNC_OFFSET(43822, glTexBufferRange, glTexBufferRange, NULL, 900),
    NAME_FUNC_OFFSET(43842, glTexBufferRange, glTexBufferRange, NULL, 900),
    NAME_FUNC_OFFSET(43862, glTexStorage3DMultisample, glTexStorage3DMultisample, NULL, 902),
    NAME_FUNC_OFFSET(43891, glBufferStorage, glBufferStorage, NULL, 903),
    NAME_FUNC_OFFSET(43910, glClearTexImage, glClearTexImage, NULL, 904),
    NAME_FUNC_OFFSET(43929, glClearTexSubImage, glClearTexSubImage, NULL, 905),
    NAME_FUNC_OFFSET(43951, gl_dispatch_stub_929, gl_dispatch_stub_929, NULL, 929),
    NAME_FUNC_OFFSET(43982, gl_dispatch_stub_930, gl_dispatch_stub_930, NULL, 930),
    NAME_FUNC_OFFSET(44015, gl_dispatch_stub_931, gl_dispatch_stub_931, NULL, 931),
    NAME_FUNC_OFFSET(44032, gl_dispatch_stub_1020, gl_dispatch_stub_1020, NULL, 1020),
    NAME_FUNC_OFFSET(44051, gl_dispatch_stub_1033, gl_dispatch_stub_1033, NULL, 1033),
    NAME_FUNC_OFFSET(44070, gl_dispatch_stub_1034, gl_dispatch_stub_1034, NULL, 1034),
    NAME_FUNC_OFFSET(44090, gl_dispatch_stub_1037, gl_dispatch_stub_1037, NULL, 1037),
    NAME_FUNC_OFFSET(44113, gl_dispatch_stub_1038, gl_dispatch_stub_1038, NULL, 1038),
    NAME_FUNC_OFFSET(44137, gl_dispatch_stub_1039, gl_dispatch_stub_1039, NULL, 1039),
    NAME_FUNC_OFFSET(44161, gl_dispatch_stub_1040, gl_dispatch_stub_1040, NULL, 1040),
    NAME_FUNC_OFFSET(44186, gl_dispatch_stub_1041, gl_dispatch_stub_1041, NULL, 1041),
    NAME_FUNC_OFFSET(44209, gl_dispatch_stub_1042, gl_dispatch_stub_1042, NULL, 1042),
    NAME_FUNC_OFFSET(44233, gl_dispatch_stub_1043, gl_dispatch_stub_1043, NULL, 1043),
    NAME_FUNC_OFFSET(44257, gl_dispatch_stub_1044, gl_dispatch_stub_1044, NULL, 1044),
    NAME_FUNC_OFFSET(44282, gl_dispatch_stub_1045, gl_dispatch_stub_1045, NULL, 1045),
    NAME_FUNC_OFFSET(44305, gl_dispatch_stub_1046, gl_dispatch_stub_1046, NULL, 1046),
    NAME_FUNC_OFFSET(44329, gl_dispatch_stub_1047, gl_dispatch_stub_1047, NULL, 1047),
    NAME_FUNC_OFFSET(44353, gl_dispatch_stub_1048, gl_dispatch_stub_1048, NULL, 1048),
    NAME_FUNC_OFFSET(44378, gl_dispatch_stub_1049, gl_dispatch_stub_1049, NULL, 1049),
    NAME_FUNC_OFFSET(44401, gl_dispatch_stub_1050, gl_dispatch_stub_1050, NULL, 1050),
    NAME_FUNC_OFFSET(44425, gl_dispatch_stub_1051, gl_dispatch_stub_1051, NULL, 1051),
    NAME_FUNC_OFFSET(44449, gl_dispatch_stub_1052, gl_dispatch_stub_1052, NULL, 1052),
    NAME_FUNC_OFFSET(44474, gl_dispatch_stub_1053, gl_dispatch_stub_1053, NULL, 1053),
    NAME_FUNC_OFFSET(44490, gl_dispatch_stub_1054, gl_dispatch_stub_1054, NULL, 1054),
    NAME_FUNC_OFFSET(44507, gl_dispatch_stub_1055, gl_dispatch_stub_1055, NULL, 1055),
    NAME_FUNC_OFFSET(44524, gl_dispatch_stub_1056, gl_dispatch_stub_1056, NULL, 1056),
    NAME_FUNC_OFFSET(44542, gl_dispatch_stub_1057, gl_dispatch_stub_1057, NULL, 1057),
    NAME_FUNC_OFFSET(44558, gl_dispatch_stub_1058, gl_dispatch_stub_1058, NULL, 1058),
    NAME_FUNC_OFFSET(44575, gl_dispatch_stub_1059, gl_dispatch_stub_1059, NULL, 1059),
    NAME_FUNC_OFFSET(44592, gl_dispatch_stub_1060, gl_dispatch_stub_1060, NULL, 1060),
    NAME_FUNC_OFFSET(44610, gl_dispatch_stub_1061, gl_dispatch_stub_1061, NULL, 1061),
    NAME_FUNC_OFFSET(44626, gl_dispatch_stub_1062, gl_dispatch_stub_1062, NULL, 1062),
    NAME_FUNC_OFFSET(44643, gl_dispatch_stub_1063, gl_dispatch_stub_1063, NULL, 1063),
    NAME_FUNC_OFFSET(44660, gl_dispatch_stub_1064, gl_dispatch_stub_1064, NULL, 1064),
    NAME_FUNC_OFFSET(44678, gl_dispatch_stub_1065, gl_dispatch_stub_1065, NULL, 1065),
    NAME_FUNC_OFFSET(44694, gl_dispatch_stub_1066, gl_dispatch_stub_1066, NULL, 1066),
    NAME_FUNC_OFFSET(44711, gl_dispatch_stub_1067, gl_dispatch_stub_1067, NULL, 1067),
    NAME_FUNC_OFFSET(44728, gl_dispatch_stub_1068, gl_dispatch_stub_1068, NULL, 1068),
    NAME_FUNC_OFFSET(44746, gl_dispatch_stub_1069, gl_dispatch_stub_1069, NULL, 1069),
    NAME_FUNC_OFFSET(44769, gl_dispatch_stub_1070, gl_dispatch_stub_1070, NULL, 1070),
    NAME_FUNC_OFFSET(44802, gl_dispatch_stub_1071, gl_dispatch_stub_1071, NULL, 1071),
    NAME_FUNC_OFFSET(44840, gl_dispatch_stub_1072, gl_dispatch_stub_1072, NULL, 1072),
    NAME_FUNC_OFFSET(44859, gl_dispatch_stub_1089, gl_dispatch_stub_1089, NULL, 1089),
    NAME_FUNC_OFFSET(44875, gl_dispatch_stub_1090, gl_dispatch_stub_1090, NULL, 1090),
    NAME_FUNC_OFFSET(44894, glActiveShaderProgram, glActiveShaderProgram, NULL, 1098),
    NAME_FUNC_OFFSET(44919, glBindProgramPipeline, glBindProgramPipeline, NULL, 1099),
    NAME_FUNC_OFFSET(44944, glCreateShaderProgramv, glCreateShaderProgramv, NULL, 1100),
    NAME_FUNC_OFFSET(44970, glDeleteProgramPipelines, glDeleteProgramPipelines, NULL, 1101),
    NAME_FUNC_OFFSET(44998, glGenProgramPipelines, glGenProgramPipelines, NULL, 1102),
    NAME_FUNC_OFFSET(45023, glGetProgramPipelineInfoLog, glGetProgramPipelineInfoLog, NULL, 1103),
    NAME_FUNC_OFFSET(45054, glGetProgramPipelineiv, glGetProgramPipelineiv, NULL, 1104),
    NAME_FUNC_OFFSET(45080, glIsProgramPipeline, glIsProgramPipeline, NULL, 1105),
    NAME_FUNC_OFFSET(45103, gl_dispatch_stub_1107, gl_dispatch_stub_1107, NULL, 1107),
    NAME_FUNC_OFFSET(45125, gl_dispatch_stub_1108, gl_dispatch_stub_1108, NULL, 1108),
    NAME_FUNC_OFFSET(45148, glProgramUniform1f, glProgramUniform1f, NULL, 1109),
    NAME_FUNC_OFFSET(45170, glProgramUniform1fv, glProgramUniform1fv, NULL, 1110),
    NAME_FUNC_OFFSET(45193, glProgramUniform1i, glProgramUniform1i, NULL, 1111),
    NAME_FUNC_OFFSET(45215, glProgramUniform1iv, glProgramUniform1iv, NULL, 1112),
    NAME_FUNC_OFFSET(45238, glProgramUniform1ui, glProgramUniform1ui, NULL, 1113),
    NAME_FUNC_OFFSET(45261, glProgramUniform1uiv, glProgramUniform1uiv, NULL, 1114),
    NAME_FUNC_OFFSET(45285, gl_dispatch_stub_1115, gl_dispatch_stub_1115, NULL, 1115),
    NAME_FUNC_OFFSET(45307, gl_dispatch_stub_1116, gl_dispatch_stub_1116, NULL, 1116),
    NAME_FUNC_OFFSET(45330, glProgramUniform2f, glProgramUniform2f, NULL, 1117),
    NAME_FUNC_OFFSET(45352, glProgramUniform2fv, glProgramUniform2fv, NULL, 1118),
    NAME_FUNC_OFFSET(45375, glProgramUniform2i, glProgramUniform2i, NULL, 1119),
    NAME_FUNC_OFFSET(45397, glProgramUniform2iv, glProgramUniform2iv, NULL, 1120),
    NAME_FUNC_OFFSET(45420, glProgramUniform2ui, glProgramUniform2ui, NULL, 1121),
    NAME_FUNC_OFFSET(45443, glProgramUniform2uiv, glProgramUniform2uiv, NULL, 1122),
    NAME_FUNC_OFFSET(45467, gl_dispatch_stub_1123, gl_dispatch_stub_1123, NULL, 1123),
    NAME_FUNC_OFFSET(45489, gl_dispatch_stub_1124, gl_dispatch_stub_1124, NULL, 1124),
    NAME_FUNC_OFFSET(45512, glProgramUniform3f, glProgramUniform3f, NULL, 1125),
    NAME_FUNC_OFFSET(45534, glProgramUniform3fv, glProgramUniform3fv, NULL, 1126),
    NAME_FUNC_OFFSET(45557, glProgramUniform3i, glProgramUniform3i, NULL, 1127),
    NAME_FUNC_OFFSET(45579, glProgramUniform3iv, glProgramUniform3iv, NULL, 1128),
    NAME_FUNC_OFFSET(45602, glProgramUniform3ui, glProgramUniform3ui, NULL, 1129),
    NAME_FUNC_OFFSET(45625, glProgramUniform3uiv, glProgramUniform3uiv, NULL, 1130),
    NAME_FUNC_OFFSET(45649, gl_dispatch_stub_1131, gl_dispatch_stub_1131, NULL, 1131),
    NAME_FUNC_OFFSET(45671, gl_dispatch_stub_1132, gl_dispatch_stub_1132, NULL, 1132),
    NAME_FUNC_OFFSET(45694, glProgramUniform4f, glProgramUniform4f, NULL, 1133),
    NAME_FUNC_OFFSET(45716, glProgramUniform4fv, glProgramUniform4fv, NULL, 1134),
    NAME_FUNC_OFFSET(45739, glProgramUniform4i, glProgramUniform4i, NULL, 1135),
    NAME_FUNC_OFFSET(45761, glProgramUniform4iv, glProgramUniform4iv, NULL, 1136),
    NAME_FUNC_OFFSET(45784, glProgramUniform4ui, glProgramUniform4ui, NULL, 1137),
    NAME_FUNC_OFFSET(45807, glProgramUniform4uiv, glProgramUniform4uiv, NULL, 1138),
    NAME_FUNC_OFFSET(45831, gl_dispatch_stub_1139, gl_dispatch_stub_1139, NULL, 1139),
    NAME_FUNC_OFFSET(45860, glProgramUniformMatrix2fv, glProgramUniformMatrix2fv, NULL, 1140),
    NAME_FUNC_OFFSET(45889, gl_dispatch_stub_1141, gl_dispatch_stub_1141, NULL, 1141),
    NAME_FUNC_OFFSET(45920, glProgramUniformMatrix2x3fv, glProgramUniformMatrix2x3fv, NULL, 1142),
    NAME_FUNC_OFFSET(45951, gl_dispatch_stub_1143, gl_dispatch_stub_1143, NULL, 1143),
    NAME_FUNC_OFFSET(45982, glProgramUniformMatrix2x4fv, glProgramUniformMatrix2x4fv, NULL, 1144),
    NAME_FUNC_OFFSET(46013, gl_dispatch_stub_1145, gl_dispatch_stub_1145, NULL, 1145),
    NAME_FUNC_OFFSET(46042, glProgramUniformMatrix3fv, glProgramUniformMatrix3fv, NULL, 1146),
    NAME_FUNC_OFFSET(46071, gl_dispatch_stub_1147, gl_dispatch_stub_1147, NULL, 1147),
    NAME_FUNC_OFFSET(46102, glProgramUniformMatrix3x2fv, glProgramUniformMatrix3x2fv, NULL, 1148),
    NAME_FUNC_OFFSET(46133, gl_dispatch_stub_1149, gl_dispatch_stub_1149, NULL, 1149),
    NAME_FUNC_OFFSET(46164, glProgramUniformMatrix3x4fv, glProgramUniformMatrix3x4fv, NULL, 1150),
    NAME_FUNC_OFFSET(46195, gl_dispatch_stub_1151, gl_dispatch_stub_1151, NULL, 1151),
    NAME_FUNC_OFFSET(46224, glProgramUniformMatrix4fv, glProgramUniformMatrix4fv, NULL, 1152),
    NAME_FUNC_OFFSET(46253, gl_dispatch_stub_1153, gl_dispatch_stub_1153, NULL, 1153),
    NAME_FUNC_OFFSET(46284, glProgramUniformMatrix4x2fv, glProgramUniformMatrix4x2fv, NULL, 1154),
    NAME_FUNC_OFFSET(46315, gl_dispatch_stub_1155, gl_dispatch_stub_1155, NULL, 1155),
    NAME_FUNC_OFFSET(46346, glProgramUniformMatrix4x3fv, glProgramUniformMatrix4x3fv, NULL, 1156),
    NAME_FUNC_OFFSET(46377, glUseProgramStages, glUseProgramStages, NULL, 1158),
    NAME_FUNC_OFFSET(46399, glValidateProgramPipeline, glValidateProgramPipeline, NULL, 1159),
    NAME_FUNC_OFFSET(46428, glDebugMessageCallback, glDebugMessageCallback, NULL, 1161),
    NAME_FUNC_OFFSET(46454, glDebugMessageCallback, glDebugMessageCallback, NULL, 1161),
    NAME_FUNC_OFFSET(46480, glDebugMessageControl, glDebugMessageControl, NULL, 1162),
    NAME_FUNC_OFFSET(46505, glDebugMessageControl, glDebugMessageControl, NULL, 1162),
    NAME_FUNC_OFFSET(46530, glDebugMessageInsert, glDebugMessageInsert, NULL, 1163),
    NAME_FUNC_OFFSET(46554, glDebugMessageInsert, glDebugMessageInsert, NULL, 1163),
    NAME_FUNC_OFFSET(46578, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 1164),
    NAME_FUNC_OFFSET(46602, glGetDebugMessageLog, glGetDebugMessageLog, NULL, 1164),
    NAME_FUNC_OFFSET(46626, glGetObjectLabel, glGetObjectLabel, NULL, 1165),
    NAME_FUNC_OFFSET(46646, glGetObjectPtrLabel, glGetObjectPtrLabel, NULL, 1166),
    NAME_FUNC_OFFSET(46669, glObjectLabel, glObjectLabel, NULL, 1167),
    NAME_FUNC_OFFSET(46686, glObjectPtrLabel, glObjectPtrLabel, NULL, 1168),
    NAME_FUNC_OFFSET(46706, glPopDebugGroup, glPopDebugGroup, NULL, 1169),
    NAME_FUNC_OFFSET(46725, glPushDebugGroup, glPushDebugGroup, NULL, 1170),
    NAME_FUNC_OFFSET(46745, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 1171),
    NAME_FUNC_OFFSET(46764, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 1172),
    NAME_FUNC_OFFSET(46784, glMultiDrawElements, glMultiDrawElements, NULL, 1173),
    NAME_FUNC_OFFSET(46807, glFogCoordfEXT, glFogCoordfEXT, NULL, 1174),
    NAME_FUNC_OFFSET(46819, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 1175),
    NAME_FUNC_OFFSET(46832, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 1279),
    NAME_FUNC_OFFSET(46850, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 1280),
    NAME_FUNC_OFFSET(46869, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 1281),
    NAME_FUNC_OFFSET(46887, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 1282),
    NAME_FUNC_OFFSET(46906, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 1283),
    NAME_FUNC_OFFSET(46925, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 1284),
    NAME_FUNC_OFFSET(46945, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 1285),
    NAME_FUNC_OFFSET(46963, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 1286),
    NAME_FUNC_OFFSET(46982, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 1287),
    NAME_FUNC_OFFSET(47001, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 1288),
    NAME_FUNC_OFFSET(47021, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 1289),
    NAME_FUNC_OFFSET(47039, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 1290),
    NAME_FUNC_OFFSET(47058, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 1291),
    NAME_FUNC_OFFSET(47077, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 1292),
    NAME_FUNC_OFFSET(47097, glTextureBarrierNV, glTextureBarrierNV, NULL, 1313),
    NAME_FUNC_OFFSET(47114, gl_dispatch_stub_1334, gl_dispatch_stub_1334, NULL, 1334),
    NAME_FUNC_OFFSET(47135, glAlphaFuncx, glAlphaFuncx, NULL, 1375),
    NAME_FUNC_OFFSET(47151, glClearColorx, glClearColorx, NULL, 1376),
    NAME_FUNC_OFFSET(47168, glClearDepthx, glClearDepthx, NULL, 1377),
    NAME_FUNC_OFFSET(47185, glColor4x, glColor4x, NULL, 1378),
    NAME_FUNC_OFFSET(47198, glDepthRangex, glDepthRangex, NULL, 1379),
    NAME_FUNC_OFFSET(47215, glFogx, glFogx, NULL, 1380),
    NAME_FUNC_OFFSET(47225, glFogxv, glFogxv, NULL, 1381),
    NAME_FUNC_OFFSET(47236, glFrustumf, glFrustumf, NULL, 1382),
    NAME_FUNC_OFFSET(47250, glFrustumx, glFrustumx, NULL, 1383),
    NAME_FUNC_OFFSET(47264, glLightModelx, glLightModelx, NULL, 1384),
    NAME_FUNC_OFFSET(47281, glLightModelxv, glLightModelxv, NULL, 1385),
    NAME_FUNC_OFFSET(47299, glLightx, glLightx, NULL, 1386),
    NAME_FUNC_OFFSET(47311, glLightxv, glLightxv, NULL, 1387),
    NAME_FUNC_OFFSET(47324, glLineWidthx, glLineWidthx, NULL, 1388),
    NAME_FUNC_OFFSET(47340, glLoadMatrixx, glLoadMatrixx, NULL, 1389),
    NAME_FUNC_OFFSET(47357, glMaterialx, glMaterialx, NULL, 1390),
    NAME_FUNC_OFFSET(47372, glMaterialxv, glMaterialxv, NULL, 1391),
    NAME_FUNC_OFFSET(47388, glMultMatrixx, glMultMatrixx, NULL, 1392),
    NAME_FUNC_OFFSET(47405, glMultiTexCoord4x, glMultiTexCoord4x, NULL, 1393),
    NAME_FUNC_OFFSET(47426, glNormal3x, glNormal3x, NULL, 1394),
    NAME_FUNC_OFFSET(47440, glOrthof, glOrthof, NULL, 1395),
    NAME_FUNC_OFFSET(47452, glOrthox, glOrthox, NULL, 1396),
    NAME_FUNC_OFFSET(47464, glPointSizex, glPointSizex, NULL, 1397),
    NAME_FUNC_OFFSET(47480, glPolygonOffsetx, glPolygonOffsetx, NULL, 1398),
    NAME_FUNC_OFFSET(47500, glRotatex, glRotatex, NULL, 1399),
    NAME_FUNC_OFFSET(47513, glSampleCoveragex, glSampleCoveragex, NULL, 1400),
    NAME_FUNC_OFFSET(47534, glScalex, glScalex, NULL, 1401),
    NAME_FUNC_OFFSET(47546, glTexEnvx, glTexEnvx, NULL, 1402),
    NAME_FUNC_OFFSET(47559, glTexEnvxv, glTexEnvxv, NULL, 1403),
    NAME_FUNC_OFFSET(47573, glTexParameterx, glTexParameterx, NULL, 1404),
    NAME_FUNC_OFFSET(47592, glTranslatex, glTranslatex, NULL, 1405),
    NAME_FUNC_OFFSET(47608, glClipPlanef, glClipPlanef, NULL, 1406),
    NAME_FUNC_OFFSET(47624, glClipPlanex, glClipPlanex, NULL, 1407),
    NAME_FUNC_OFFSET(47640, glGetClipPlanef, glGetClipPlanef, NULL, 1408),
    NAME_FUNC_OFFSET(47659, glGetClipPlanex, glGetClipPlanex, NULL, 1409),
    NAME_FUNC_OFFSET(47678, glGetFixedv, glGetFixedv, NULL, 1410),
    NAME_FUNC_OFFSET(47693, glGetLightxv, glGetLightxv, NULL, 1411),
    NAME_FUNC_OFFSET(47709, glGetMaterialxv, glGetMaterialxv, NULL, 1412),
    NAME_FUNC_OFFSET(47728, glGetTexEnvxv, glGetTexEnvxv, NULL, 1413),
    NAME_FUNC_OFFSET(47745, glGetTexParameterxv, glGetTexParameterxv, NULL, 1414),
    NAME_FUNC_OFFSET(47768, glPointParameterx, glPointParameterx, NULL, 1415),
    NAME_FUNC_OFFSET(47789, glPointParameterxv, glPointParameterxv, NULL, 1416),
    NAME_FUNC_OFFSET(47811, glTexParameterxv, glTexParameterxv, NULL, 1417),
    NAME_FUNC_OFFSET(47831, glBlendBarrier, glBlendBarrier, NULL, 1418),
    NAME_FUNC_OFFSET(47849, glPrimitiveBoundingBox, glPrimitiveBoundingBox, NULL, 1419),
    NAME_FUNC_OFFSET(47875, glPrimitiveBoundingBox, glPrimitiveBoundingBox, NULL, 1419),
    NAME_FUNC_OFFSET(47901, glPrimitiveBoundingBox, glPrimitiveBoundingBox, NULL, 1419),
    NAME_FUNC_OFFSET(47927, gl_dispatch_stub_1420, gl_dispatch_stub_1420, NULL, 1420),
    NAME_FUNC_OFFSET(47957, gl_dispatch_stub_1480, gl_dispatch_stub_1480, NULL, 1480),
    NAME_FUNC_OFFSET(47987, gl_dispatch_stub_1481, gl_dispatch_stub_1481, NULL, 1481),
    NAME_FUNC_OFFSET(48018, gl_dispatch_stub_1482, gl_dispatch_stub_1482, NULL, 1482),
    NAME_FUNC_OFFSET(-1, NULL, NULL, NULL, 0)
};

#undef NAME_FUNC_OFFSET
