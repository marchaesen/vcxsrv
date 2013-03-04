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
    "glDrawArraysInstancedARB\0"
    "glDrawElementsInstancedARB\0"
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
    "glFramebufferTextureFaceARB\0"
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
    "glGetInternalformativ\0"
    "glTexStorage1D\0"
    "glTexStorage2D\0"
    "glTexStorage3D\0"
    "glTextureStorage1DEXT\0"
    "glTextureStorage2DEXT\0"
    "glTextureStorage3DEXT\0"
    "glTexBufferRange\0"
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
    "glDiscardFramebufferEXT\0"
    "glLockArraysEXT\0"
    "glUnlockArraysEXT\0"
    "glSecondaryColor3fEXT\0"
    "glSecondaryColor3fvEXT\0"
    "glMultiDrawElementsEXT\0"
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
    "glActiveStencilFaceEXT\0"
    "glBindVertexArrayAPPLE\0"
    "glGenVertexArraysAPPLE\0"
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
    "glBindBufferARB\0"
    "glBufferDataARB\0"
    "glBufferSubDataARB\0"
    "glDeleteBuffersARB\0"
    "glDeleteQueriesARB\0"
    "glEndQueryARB\0"
    "glGenBuffersARB\0"
    "glGenQueriesARB\0"
    "glGetBufferParameterivARB\0"
    "glGetBufferPointervARB\0"
    "glGetBufferPointervOES\0"
    "glGetBufferSubDataARB\0"
    "glGetQueryObjectivARB\0"
    "glGetQueryObjectuivARB\0"
    "glGetQueryivARB\0"
    "glIsBufferARB\0"
    "glIsQueryARB\0"
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
    "glDisableIndexedEXT\0"
    "glEnableIndexedEXT\0"
    "glEndConditionalRenderNV\0"
    "glEndTransformFeedbackEXT\0"
    "glGetBooleanIndexedvEXT\0"
    "glGetFragDataLocationEXT\0"
    "glGetIntegerIndexedvEXT\0"
    "glGetTexParameterIivEXT\0"
    "glGetTexParameterIuivEXT\0"
    "glGetTransformFeedbackVaryingEXT\0"
    "glGetUniformuivEXT\0"
    "glGetVertexAttribIivEXT\0"
    "glGetVertexAttribIuivEXT\0"
    "glIsEnabledIndexedEXT\0"
    "glTexParameterIivEXT\0"
    "glTexParameterIuivEXT\0"
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
    "glFramebufferTextureARB\0"
    "glVertexAttribDivisorARB\0"
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
    "glDrawArraysInstanced\0"
    "glDrawElementsInstancedEXT\0"
    "glDrawElementsInstanced\0"
    "glBindFramebufferEXT\0"
    "glBindFramebufferOES\0"
    "glBindRenderbufferEXT\0"
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
    "glFramebufferTextureLayerARB\0"
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
    "glDeleteVertexArraysAPPLE\0"
    "glDeleteVertexArraysOES\0"
    "glGenVertexArraysOES\0"
    "glIsVertexArrayAPPLE\0"
    "glIsVertexArrayOES\0"
    "glProvokingVertexEXT\0"
    "glBlendEquationSeparateIndexedAMD\0"
    "glBlendEquationIndexedAMD\0"
    "glBlendFuncSeparateIndexedAMD\0"
    "glBlendFuncIndexedAMD\0"
    "glGetQueryObjecti64vEXT\0"
    "glGetQueryObjectui64vEXT\0"
    "glClearDepthfOES\0"
    "glDepthRangefOES\0"
    "glGetProgramBinaryOES\0"
    "glProgramBinaryOES\0"
    "glProgramParameteriARB\0"
    "glSampleMaskEXT\0"
    "glSamplePatternEXT\0"
    "glSecondaryColor3f\0"
    "glSecondaryColor3fv\0"
    "glMultiDrawElements\0"
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
#define gl_dispatch_stub_730 mgl_dispatch_stub_730
#define gl_dispatch_stub_731 mgl_dispatch_stub_731
#define gl_dispatch_stub_732 mgl_dispatch_stub_732
#define gl_dispatch_stub_819 mgl_dispatch_stub_819
#define gl_dispatch_stub_834 mgl_dispatch_stub_834
#define gl_dispatch_stub_835 mgl_dispatch_stub_835
#define gl_dispatch_stub_836 mgl_dispatch_stub_836
#define gl_dispatch_stub_837 mgl_dispatch_stub_837
#define gl_dispatch_stub_838 mgl_dispatch_stub_838
#define gl_dispatch_stub_839 mgl_dispatch_stub_839
#define gl_dispatch_stub_840 mgl_dispatch_stub_840
#define gl_dispatch_stub_841 mgl_dispatch_stub_841
#define gl_dispatch_stub_842 mgl_dispatch_stub_842
#define gl_dispatch_stub_843 mgl_dispatch_stub_843
#define gl_dispatch_stub_844 mgl_dispatch_stub_844
#define gl_dispatch_stub_845 mgl_dispatch_stub_845
#define gl_dispatch_stub_852 mgl_dispatch_stub_852
#define gl_dispatch_stub_869 mgl_dispatch_stub_869
#define gl_dispatch_stub_870 mgl_dispatch_stub_870
#define gl_dispatch_stub_944 mgl_dispatch_stub_944
#define gl_dispatch_stub_945 mgl_dispatch_stub_945
#define gl_dispatch_stub_946 mgl_dispatch_stub_946
#define gl_dispatch_stub_954 mgl_dispatch_stub_954
#define gl_dispatch_stub_955 mgl_dispatch_stub_955
#define gl_dispatch_stub_956 mgl_dispatch_stub_956
#define gl_dispatch_stub_957 mgl_dispatch_stub_957
#define gl_dispatch_stub_958 mgl_dispatch_stub_958
#define gl_dispatch_stub_959 mgl_dispatch_stub_959
#define gl_dispatch_stub_984 mgl_dispatch_stub_984
#define gl_dispatch_stub_985 mgl_dispatch_stub_985
#define gl_dispatch_stub_986 mgl_dispatch_stub_986
#define gl_dispatch_stub_989 mgl_dispatch_stub_989
#define gl_dispatch_stub_990 mgl_dispatch_stub_990
#define gl_dispatch_stub_991 mgl_dispatch_stub_991
#define gl_dispatch_stub_992 mgl_dispatch_stub_992
#define gl_dispatch_stub_993 mgl_dispatch_stub_993
#define gl_dispatch_stub_994 mgl_dispatch_stub_994
#define gl_dispatch_stub_995 mgl_dispatch_stub_995
#define gl_dispatch_stub_996 mgl_dispatch_stub_996
#define gl_dispatch_stub_997 mgl_dispatch_stub_997
#define gl_dispatch_stub_998 mgl_dispatch_stub_998
#define gl_dispatch_stub_999 mgl_dispatch_stub_999
#define gl_dispatch_stub_1000 mgl_dispatch_stub_1000
#define gl_dispatch_stub_1001 mgl_dispatch_stub_1001
#define gl_dispatch_stub_1002 mgl_dispatch_stub_1002
#define gl_dispatch_stub_1003 mgl_dispatch_stub_1003
#define gl_dispatch_stub_1004 mgl_dispatch_stub_1004
#define gl_dispatch_stub_1005 mgl_dispatch_stub_1005
#define gl_dispatch_stub_1006 mgl_dispatch_stub_1006
#define gl_dispatch_stub_1007 mgl_dispatch_stub_1007
#define gl_dispatch_stub_1008 mgl_dispatch_stub_1008
#define gl_dispatch_stub_1009 mgl_dispatch_stub_1009
#define gl_dispatch_stub_1010 mgl_dispatch_stub_1010
#define gl_dispatch_stub_1011 mgl_dispatch_stub_1011
#define gl_dispatch_stub_1012 mgl_dispatch_stub_1012
#define gl_dispatch_stub_1013 mgl_dispatch_stub_1013
#define gl_dispatch_stub_1014 mgl_dispatch_stub_1014
#define gl_dispatch_stub_1015 mgl_dispatch_stub_1015
#define gl_dispatch_stub_1016 mgl_dispatch_stub_1016
#define gl_dispatch_stub_1017 mgl_dispatch_stub_1017
#define gl_dispatch_stub_1018 mgl_dispatch_stub_1018
#define gl_dispatch_stub_1019 mgl_dispatch_stub_1019
#define gl_dispatch_stub_1020 mgl_dispatch_stub_1020
#define gl_dispatch_stub_1021 mgl_dispatch_stub_1021
#define gl_dispatch_stub_1022 mgl_dispatch_stub_1022
#define gl_dispatch_stub_1023 mgl_dispatch_stub_1023
#define gl_dispatch_stub_1024 mgl_dispatch_stub_1024
#define gl_dispatch_stub_1025 mgl_dispatch_stub_1025
#define gl_dispatch_stub_1026 mgl_dispatch_stub_1026
#define gl_dispatch_stub_1027 mgl_dispatch_stub_1027
#define gl_dispatch_stub_1028 mgl_dispatch_stub_1028
#define gl_dispatch_stub_1029 mgl_dispatch_stub_1029
#define gl_dispatch_stub_1030 mgl_dispatch_stub_1030
#define gl_dispatch_stub_1031 mgl_dispatch_stub_1031
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
void GLAPIENTRY gl_dispatch_stub_730(GLuint id, GLenum pname, GLint64 * params);
void GLAPIENTRY gl_dispatch_stub_731(GLuint id, GLenum pname, GLuint64 * params);
void GLAPIENTRY gl_dispatch_stub_732(GLuint id, GLenum target);
void GLAPIENTRY gl_dispatch_stub_819(GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint * params);
void GLAPIENTRY gl_dispatch_stub_834(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void GLAPIENTRY gl_dispatch_stub_835(const GLfloat * coords);
void GLAPIENTRY gl_dispatch_stub_836(GLint x, GLint y, GLint z, GLint width, GLint height);
void GLAPIENTRY gl_dispatch_stub_837(const GLint * coords);
void GLAPIENTRY gl_dispatch_stub_838(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void GLAPIENTRY gl_dispatch_stub_839(const GLshort * coords);
void GLAPIENTRY gl_dispatch_stub_840(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void GLAPIENTRY gl_dispatch_stub_841(const GLfixed * coords);
void GLAPIENTRY gl_dispatch_stub_842(GLenum type, GLsizei stride, const GLvoid * pointer);
GLbitfield GLAPIENTRY gl_dispatch_stub_843(GLfixed * mantissa, GLint * exponent);
void GLAPIENTRY gl_dispatch_stub_844(GLclampf value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_845(GLenum pattern);
void GLAPIENTRY gl_dispatch_stub_852(GLenum target, GLsizei numAttachments, const GLenum * attachments);
void GLAPIENTRY gl_dispatch_stub_869(const GLenum * mode, const GLint * first, const GLsizei * count, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_870(const GLenum * mode, const GLsizei * count, GLenum type, const GLvoid * const * indices, GLsizei primcount, GLint modestride);
void GLAPIENTRY gl_dispatch_stub_944(GLenum face);
void GLAPIENTRY gl_dispatch_stub_945(GLuint array);
void GLAPIENTRY gl_dispatch_stub_946(GLsizei n, GLuint * arrays);
void GLAPIENTRY gl_dispatch_stub_954(GLenum coord, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_955(GLenum coord, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_956(GLenum coord, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_957(GLclampd zmin, GLclampd zmax);
void GLAPIENTRY gl_dispatch_stub_958(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY gl_dispatch_stub_959(GLenum target, GLintptr offset, GLsizeiptr size);
void GLAPIENTRY gl_dispatch_stub_984(GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
void GLAPIENTRY gl_dispatch_stub_985(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_986(GLenum target, GLuint index, GLsizei count, const GLfloat * params);
void GLAPIENTRY gl_dispatch_stub_989(GLenum func, GLclampx ref);
void GLAPIENTRY gl_dispatch_stub_990(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void GLAPIENTRY gl_dispatch_stub_991(GLclampx depth);
void GLAPIENTRY gl_dispatch_stub_992(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void GLAPIENTRY gl_dispatch_stub_993(GLclampx zNear, GLclampx zFar);
void GLAPIENTRY gl_dispatch_stub_994(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_995(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_996(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_997(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_998(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_999(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1000(GLenum light, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1001(GLenum light, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1002(GLfixed width);
void GLAPIENTRY gl_dispatch_stub_1003(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_1004(GLenum face, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1005(GLenum face, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1006(const GLfixed * m);
void GLAPIENTRY gl_dispatch_stub_1007(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
void GLAPIENTRY gl_dispatch_stub_1008(GLfixed nx, GLfixed ny, GLfixed nz);
void GLAPIENTRY gl_dispatch_stub_1009(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void GLAPIENTRY gl_dispatch_stub_1010(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GLAPIENTRY gl_dispatch_stub_1011(GLfixed size);
void GLAPIENTRY gl_dispatch_stub_1012(GLfixed factor, GLfixed units);
void GLAPIENTRY gl_dispatch_stub_1013(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1014(GLclampx value, GLboolean invert);
void GLAPIENTRY gl_dispatch_stub_1015(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1016(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1017(GLenum target, GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1018(GLenum target, GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1019(GLfixed x, GLfixed y, GLfixed z);
void GLAPIENTRY gl_dispatch_stub_1020(GLenum plane, const GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_1021(GLenum plane, const GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_1022(GLenum plane, GLfloat * equation);
void GLAPIENTRY gl_dispatch_stub_1023(GLenum plane, GLfixed * equation);
void GLAPIENTRY gl_dispatch_stub_1024(GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1025(GLenum light, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1026(GLenum face, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1027(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1028(GLenum target, GLenum pname, GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1029(GLenum pname, GLfixed param);
void GLAPIENTRY gl_dispatch_stub_1030(GLenum pname, const GLfixed * params);
void GLAPIENTRY gl_dispatch_stub_1031(GLenum target, GLenum pname, const GLfixed * params);
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
    NAME_FUNC_OFFSET( 9441, glBindProgramARB, glBindProgramARB, NULL, 620),
    NAME_FUNC_OFFSET( 9458, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 621),
    NAME_FUNC_OFFSET( 9478, glGenProgramsARB, glGenProgramsARB, NULL, 622),
    NAME_FUNC_OFFSET( 9495, glGetProgramEnvParameterdvARB, glGetProgramEnvParameterdvARB, NULL, 623),
    NAME_FUNC_OFFSET( 9525, glGetProgramEnvParameterfvARB, glGetProgramEnvParameterfvARB, NULL, 624),
    NAME_FUNC_OFFSET( 9555, glGetProgramLocalParameterdvARB, glGetProgramLocalParameterdvARB, NULL, 625),
    NAME_FUNC_OFFSET( 9587, glGetProgramLocalParameterfvARB, glGetProgramLocalParameterfvARB, NULL, 626),
    NAME_FUNC_OFFSET( 9619, glGetProgramStringARB, glGetProgramStringARB, NULL, 627),
    NAME_FUNC_OFFSET( 9641, glGetProgramivARB, glGetProgramivARB, NULL, 628),
    NAME_FUNC_OFFSET( 9659, glIsProgramARB, glIsProgramARB, NULL, 629),
    NAME_FUNC_OFFSET( 9674, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 630),
    NAME_FUNC_OFFSET( 9701, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 631),
    NAME_FUNC_OFFSET( 9729, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 632),
    NAME_FUNC_OFFSET( 9756, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 633),
    NAME_FUNC_OFFSET( 9784, glProgramLocalParameter4dARB, glProgramLocalParameter4dARB, NULL, 634),
    NAME_FUNC_OFFSET( 9813, glProgramLocalParameter4dvARB, glProgramLocalParameter4dvARB, NULL, 635),
    NAME_FUNC_OFFSET( 9843, glProgramLocalParameter4fARB, glProgramLocalParameter4fARB, NULL, 636),
    NAME_FUNC_OFFSET( 9872, glProgramLocalParameter4fvARB, glProgramLocalParameter4fvARB, NULL, 637),
    NAME_FUNC_OFFSET( 9902, glProgramStringARB, glProgramStringARB, NULL, 638),
    NAME_FUNC_OFFSET( 9921, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 639),
    NAME_FUNC_OFFSET( 9941, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 640),
    NAME_FUNC_OFFSET( 9962, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 641),
    NAME_FUNC_OFFSET( 9982, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 642),
    NAME_FUNC_OFFSET(10003, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 643),
    NAME_FUNC_OFFSET(10023, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 644),
    NAME_FUNC_OFFSET(10044, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 645),
    NAME_FUNC_OFFSET(10064, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 646),
    NAME_FUNC_OFFSET(10085, glAttachObjectARB, glAttachObjectARB, NULL, 647),
    NAME_FUNC_OFFSET(10103, glCreateProgramObjectARB, glCreateProgramObjectARB, NULL, 648),
    NAME_FUNC_OFFSET(10128, glCreateShaderObjectARB, glCreateShaderObjectARB, NULL, 649),
    NAME_FUNC_OFFSET(10152, glDeleteObjectARB, glDeleteObjectARB, NULL, 650),
    NAME_FUNC_OFFSET(10170, glDetachObjectARB, glDetachObjectARB, NULL, 651),
    NAME_FUNC_OFFSET(10188, glGetAttachedObjectsARB, glGetAttachedObjectsARB, NULL, 652),
    NAME_FUNC_OFFSET(10212, glGetHandleARB, glGetHandleARB, NULL, 653),
    NAME_FUNC_OFFSET(10227, glGetInfoLogARB, glGetInfoLogARB, NULL, 654),
    NAME_FUNC_OFFSET(10243, glGetObjectParameterfvARB, glGetObjectParameterfvARB, NULL, 655),
    NAME_FUNC_OFFSET(10269, glGetObjectParameterivARB, glGetObjectParameterivARB, NULL, 656),
    NAME_FUNC_OFFSET(10295, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 657),
    NAME_FUNC_OFFSET(10320, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(10347, glBindFramebuffer, glBindFramebuffer, NULL, 659),
    NAME_FUNC_OFFSET(10365, glBindRenderbuffer, glBindRenderbuffer, NULL, 660),
    NAME_FUNC_OFFSET(10384, glBlitFramebuffer, glBlitFramebuffer, NULL, 661),
    NAME_FUNC_OFFSET(10402, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 662),
    NAME_FUNC_OFFSET(10427, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 663),
    NAME_FUNC_OFFSET(10448, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 664),
    NAME_FUNC_OFFSET(10470, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 665),
    NAME_FUNC_OFFSET(10496, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 666),
    NAME_FUNC_OFFSET(10519, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 667),
    NAME_FUNC_OFFSET(10542, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 668),
    NAME_FUNC_OFFSET(10565, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 669),
    NAME_FUNC_OFFSET(10591, glGenFramebuffers, glGenFramebuffers, NULL, 670),
    NAME_FUNC_OFFSET(10609, glGenRenderbuffers, glGenRenderbuffers, NULL, 671),
    NAME_FUNC_OFFSET(10628, glGenerateMipmap, glGenerateMipmap, NULL, 672),
    NAME_FUNC_OFFSET(10645, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 673),
    NAME_FUNC_OFFSET(10683, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(10712, glIsFramebuffer, glIsFramebuffer, NULL, 675),
    NAME_FUNC_OFFSET(10728, glIsRenderbuffer, glIsRenderbuffer, NULL, 676),
    NAME_FUNC_OFFSET(10745, glRenderbufferStorage, glRenderbufferStorage, NULL, 677),
    NAME_FUNC_OFFSET(10767, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 678),
    NAME_FUNC_OFFSET(10800, glFramebufferTextureFaceARB, glFramebufferTextureFaceARB, NULL, 679),
    NAME_FUNC_OFFSET(10828, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 680),
    NAME_FUNC_OFFSET(10853, glMapBufferRange, glMapBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(10870, glBindVertexArray, glBindVertexArray, NULL, 682),
    NAME_FUNC_OFFSET(10888, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 683),
    NAME_FUNC_OFFSET(10909, glGenVertexArrays, glGenVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(10927, glIsVertexArray, glIsVertexArray, NULL, 685),
    NAME_FUNC_OFFSET(10943, glGetActiveUniformBlockName, glGetActiveUniformBlockName, NULL, 686),
    NAME_FUNC_OFFSET(10971, glGetActiveUniformBlockiv, glGetActiveUniformBlockiv, NULL, 687),
    NAME_FUNC_OFFSET(10997, glGetActiveUniformName, glGetActiveUniformName, NULL, 688),
    NAME_FUNC_OFFSET(11020, glGetActiveUniformsiv, glGetActiveUniformsiv, NULL, 689),
    NAME_FUNC_OFFSET(11042, glGetUniformBlockIndex, glGetUniformBlockIndex, NULL, 690),
    NAME_FUNC_OFFSET(11065, glGetUniformIndices, glGetUniformIndices, NULL, 691),
    NAME_FUNC_OFFSET(11085, glUniformBlockBinding, glUniformBlockBinding, NULL, 692),
    NAME_FUNC_OFFSET(11107, glCopyBufferSubData, glCopyBufferSubData, NULL, 693),
    NAME_FUNC_OFFSET(11127, glClientWaitSync, glClientWaitSync, NULL, 694),
    NAME_FUNC_OFFSET(11144, glDeleteSync, glDeleteSync, NULL, 695),
    NAME_FUNC_OFFSET(11157, glFenceSync, glFenceSync, NULL, 696),
    NAME_FUNC_OFFSET(11169, glGetInteger64v, glGetInteger64v, NULL, 697),
    NAME_FUNC_OFFSET(11185, glGetSynciv, glGetSynciv, NULL, 698),
    NAME_FUNC_OFFSET(11197, glIsSync, glIsSync, NULL, 699),
    NAME_FUNC_OFFSET(11206, glWaitSync, glWaitSync, NULL, 700),
    NAME_FUNC_OFFSET(11217, glDrawElementsBaseVertex, glDrawElementsBaseVertex, NULL, 701),
    NAME_FUNC_OFFSET(11242, glDrawElementsInstancedBaseVertex, glDrawElementsInstancedBaseVertex, NULL, 702),
    NAME_FUNC_OFFSET(11276, glDrawRangeElementsBaseVertex, glDrawRangeElementsBaseVertex, NULL, 703),
    NAME_FUNC_OFFSET(11306, glMultiDrawElementsBaseVertex, glMultiDrawElementsBaseVertex, NULL, 704),
    NAME_FUNC_OFFSET(11336, glProvokingVertex, glProvokingVertex, NULL, 705),
    NAME_FUNC_OFFSET(11354, glGetMultisamplefv, glGetMultisamplefv, NULL, 706),
    NAME_FUNC_OFFSET(11373, glSampleMaski, glSampleMaski, NULL, 707),
    NAME_FUNC_OFFSET(11387, glTexImage2DMultisample, glTexImage2DMultisample, NULL, 708),
    NAME_FUNC_OFFSET(11411, glTexImage3DMultisample, glTexImage3DMultisample, NULL, 709),
    NAME_FUNC_OFFSET(11435, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 710),
    NAME_FUNC_OFFSET(11463, glBlendEquationiARB, glBlendEquationiARB, NULL, 711),
    NAME_FUNC_OFFSET(11483, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 712),
    NAME_FUNC_OFFSET(11507, glBlendFunciARB, glBlendFunciARB, NULL, 713),
    NAME_FUNC_OFFSET(11523, glBindFragDataLocationIndexed, glBindFragDataLocationIndexed, NULL, 714),
    NAME_FUNC_OFFSET(11553, glGetFragDataIndex, glGetFragDataIndex, NULL, 715),
    NAME_FUNC_OFFSET(11572, glBindSampler, glBindSampler, NULL, 716),
    NAME_FUNC_OFFSET(11586, glDeleteSamplers, glDeleteSamplers, NULL, 717),
    NAME_FUNC_OFFSET(11603, glGenSamplers, glGenSamplers, NULL, 718),
    NAME_FUNC_OFFSET(11617, glGetSamplerParameterIiv, glGetSamplerParameterIiv, NULL, 719),
    NAME_FUNC_OFFSET(11642, glGetSamplerParameterIuiv, glGetSamplerParameterIuiv, NULL, 720),
    NAME_FUNC_OFFSET(11668, glGetSamplerParameterfv, glGetSamplerParameterfv, NULL, 721),
    NAME_FUNC_OFFSET(11692, glGetSamplerParameteriv, glGetSamplerParameteriv, NULL, 722),
    NAME_FUNC_OFFSET(11716, glIsSampler, glIsSampler, NULL, 723),
    NAME_FUNC_OFFSET(11728, glSamplerParameterIiv, glSamplerParameterIiv, NULL, 724),
    NAME_FUNC_OFFSET(11750, glSamplerParameterIuiv, glSamplerParameterIuiv, NULL, 725),
    NAME_FUNC_OFFSET(11773, glSamplerParameterf, glSamplerParameterf, NULL, 726),
    NAME_FUNC_OFFSET(11793, glSamplerParameterfv, glSamplerParameterfv, NULL, 727),
    NAME_FUNC_OFFSET(11814, glSamplerParameteri, glSamplerParameteri, NULL, 728),
    NAME_FUNC_OFFSET(11834, glSamplerParameteriv, glSamplerParameteriv, NULL, 729),
    NAME_FUNC_OFFSET(11855, gl_dispatch_stub_730, gl_dispatch_stub_730, NULL, 730),
    NAME_FUNC_OFFSET(11876, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(11898, gl_dispatch_stub_732, gl_dispatch_stub_732, NULL, 732),
    NAME_FUNC_OFFSET(11913, glColorP3ui, glColorP3ui, NULL, 733),
    NAME_FUNC_OFFSET(11925, glColorP3uiv, glColorP3uiv, NULL, 734),
    NAME_FUNC_OFFSET(11938, glColorP4ui, glColorP4ui, NULL, 735),
    NAME_FUNC_OFFSET(11950, glColorP4uiv, glColorP4uiv, NULL, 736),
    NAME_FUNC_OFFSET(11963, glMultiTexCoordP1ui, glMultiTexCoordP1ui, NULL, 737),
    NAME_FUNC_OFFSET(11983, glMultiTexCoordP1uiv, glMultiTexCoordP1uiv, NULL, 738),
    NAME_FUNC_OFFSET(12004, glMultiTexCoordP2ui, glMultiTexCoordP2ui, NULL, 739),
    NAME_FUNC_OFFSET(12024, glMultiTexCoordP2uiv, glMultiTexCoordP2uiv, NULL, 740),
    NAME_FUNC_OFFSET(12045, glMultiTexCoordP3ui, glMultiTexCoordP3ui, NULL, 741),
    NAME_FUNC_OFFSET(12065, glMultiTexCoordP3uiv, glMultiTexCoordP3uiv, NULL, 742),
    NAME_FUNC_OFFSET(12086, glMultiTexCoordP4ui, glMultiTexCoordP4ui, NULL, 743),
    NAME_FUNC_OFFSET(12106, glMultiTexCoordP4uiv, glMultiTexCoordP4uiv, NULL, 744),
    NAME_FUNC_OFFSET(12127, glNormalP3ui, glNormalP3ui, NULL, 745),
    NAME_FUNC_OFFSET(12140, glNormalP3uiv, glNormalP3uiv, NULL, 746),
    NAME_FUNC_OFFSET(12154, glSecondaryColorP3ui, glSecondaryColorP3ui, NULL, 747),
    NAME_FUNC_OFFSET(12175, glSecondaryColorP3uiv, glSecondaryColorP3uiv, NULL, 748),
    NAME_FUNC_OFFSET(12197, glTexCoordP1ui, glTexCoordP1ui, NULL, 749),
    NAME_FUNC_OFFSET(12212, glTexCoordP1uiv, glTexCoordP1uiv, NULL, 750),
    NAME_FUNC_OFFSET(12228, glTexCoordP2ui, glTexCoordP2ui, NULL, 751),
    NAME_FUNC_OFFSET(12243, glTexCoordP2uiv, glTexCoordP2uiv, NULL, 752),
    NAME_FUNC_OFFSET(12259, glTexCoordP3ui, glTexCoordP3ui, NULL, 753),
    NAME_FUNC_OFFSET(12274, glTexCoordP3uiv, glTexCoordP3uiv, NULL, 754),
    NAME_FUNC_OFFSET(12290, glTexCoordP4ui, glTexCoordP4ui, NULL, 755),
    NAME_FUNC_OFFSET(12305, glTexCoordP4uiv, glTexCoordP4uiv, NULL, 756),
    NAME_FUNC_OFFSET(12321, glVertexAttribP1ui, glVertexAttribP1ui, NULL, 757),
    NAME_FUNC_OFFSET(12340, glVertexAttribP1uiv, glVertexAttribP1uiv, NULL, 758),
    NAME_FUNC_OFFSET(12360, glVertexAttribP2ui, glVertexAttribP2ui, NULL, 759),
    NAME_FUNC_OFFSET(12379, glVertexAttribP2uiv, glVertexAttribP2uiv, NULL, 760),
    NAME_FUNC_OFFSET(12399, glVertexAttribP3ui, glVertexAttribP3ui, NULL, 761),
    NAME_FUNC_OFFSET(12418, glVertexAttribP3uiv, glVertexAttribP3uiv, NULL, 762),
    NAME_FUNC_OFFSET(12438, glVertexAttribP4ui, glVertexAttribP4ui, NULL, 763),
    NAME_FUNC_OFFSET(12457, glVertexAttribP4uiv, glVertexAttribP4uiv, NULL, 764),
    NAME_FUNC_OFFSET(12477, glVertexP2ui, glVertexP2ui, NULL, 765),
    NAME_FUNC_OFFSET(12490, glVertexP2uiv, glVertexP2uiv, NULL, 766),
    NAME_FUNC_OFFSET(12504, glVertexP3ui, glVertexP3ui, NULL, 767),
    NAME_FUNC_OFFSET(12517, glVertexP3uiv, glVertexP3uiv, NULL, 768),
    NAME_FUNC_OFFSET(12531, glVertexP4ui, glVertexP4ui, NULL, 769),
    NAME_FUNC_OFFSET(12544, glVertexP4uiv, glVertexP4uiv, NULL, 770),
    NAME_FUNC_OFFSET(12558, glBindTransformFeedback, glBindTransformFeedback, NULL, 771),
    NAME_FUNC_OFFSET(12582, glDeleteTransformFeedbacks, glDeleteTransformFeedbacks, NULL, 772),
    NAME_FUNC_OFFSET(12609, glDrawTransformFeedback, glDrawTransformFeedback, NULL, 773),
    NAME_FUNC_OFFSET(12633, glGenTransformFeedbacks, glGenTransformFeedbacks, NULL, 774),
    NAME_FUNC_OFFSET(12657, glIsTransformFeedback, glIsTransformFeedback, NULL, 775),
    NAME_FUNC_OFFSET(12679, glPauseTransformFeedback, glPauseTransformFeedback, NULL, 776),
    NAME_FUNC_OFFSET(12704, glResumeTransformFeedback, glResumeTransformFeedback, NULL, 777),
    NAME_FUNC_OFFSET(12730, glBeginQueryIndexed, glBeginQueryIndexed, NULL, 778),
    NAME_FUNC_OFFSET(12750, glDrawTransformFeedbackStream, glDrawTransformFeedbackStream, NULL, 779),
    NAME_FUNC_OFFSET(12780, glEndQueryIndexed, glEndQueryIndexed, NULL, 780),
    NAME_FUNC_OFFSET(12798, glGetQueryIndexediv, glGetQueryIndexediv, NULL, 781),
    NAME_FUNC_OFFSET(12818, glClearDepthf, glClearDepthf, NULL, 782),
    NAME_FUNC_OFFSET(12832, glDepthRangef, glDepthRangef, NULL, 783),
    NAME_FUNC_OFFSET(12846, glGetShaderPrecisionFormat, glGetShaderPrecisionFormat, NULL, 784),
    NAME_FUNC_OFFSET(12873, glReleaseShaderCompiler, glReleaseShaderCompiler, NULL, 785),
    NAME_FUNC_OFFSET(12897, glShaderBinary, glShaderBinary, NULL, 786),
    NAME_FUNC_OFFSET(12912, glGetProgramBinary, glGetProgramBinary, NULL, 787),
    NAME_FUNC_OFFSET(12931, glProgramBinary, glProgramBinary, NULL, 788),
    NAME_FUNC_OFFSET(12947, glProgramParameteri, glProgramParameteri, NULL, 789),
    NAME_FUNC_OFFSET(12967, glDebugMessageCallbackARB, glDebugMessageCallbackARB, NULL, 790),
    NAME_FUNC_OFFSET(12993, glDebugMessageControlARB, glDebugMessageControlARB, NULL, 791),
    NAME_FUNC_OFFSET(13018, glDebugMessageInsertARB, glDebugMessageInsertARB, NULL, 792),
    NAME_FUNC_OFFSET(13042, glGetDebugMessageLogARB, glGetDebugMessageLogARB, NULL, 793),
    NAME_FUNC_OFFSET(13066, glGetGraphicsResetStatusARB, glGetGraphicsResetStatusARB, NULL, 794),
    NAME_FUNC_OFFSET(13094, glGetnColorTableARB, glGetnColorTableARB, NULL, 795),
    NAME_FUNC_OFFSET(13114, glGetnCompressedTexImageARB, glGetnCompressedTexImageARB, NULL, 796),
    NAME_FUNC_OFFSET(13142, glGetnConvolutionFilterARB, glGetnConvolutionFilterARB, NULL, 797),
    NAME_FUNC_OFFSET(13169, glGetnHistogramARB, glGetnHistogramARB, NULL, 798),
    NAME_FUNC_OFFSET(13188, glGetnMapdvARB, glGetnMapdvARB, NULL, 799),
    NAME_FUNC_OFFSET(13203, glGetnMapfvARB, glGetnMapfvARB, NULL, 800),
    NAME_FUNC_OFFSET(13218, glGetnMapivARB, glGetnMapivARB, NULL, 801),
    NAME_FUNC_OFFSET(13233, glGetnMinmaxARB, glGetnMinmaxARB, NULL, 802),
    NAME_FUNC_OFFSET(13249, glGetnPixelMapfvARB, glGetnPixelMapfvARB, NULL, 803),
    NAME_FUNC_OFFSET(13269, glGetnPixelMapuivARB, glGetnPixelMapuivARB, NULL, 804),
    NAME_FUNC_OFFSET(13290, glGetnPixelMapusvARB, glGetnPixelMapusvARB, NULL, 805),
    NAME_FUNC_OFFSET(13311, glGetnPolygonStippleARB, glGetnPolygonStippleARB, NULL, 806),
    NAME_FUNC_OFFSET(13335, glGetnSeparableFilterARB, glGetnSeparableFilterARB, NULL, 807),
    NAME_FUNC_OFFSET(13360, glGetnTexImageARB, glGetnTexImageARB, NULL, 808),
    NAME_FUNC_OFFSET(13378, glGetnUniformdvARB, glGetnUniformdvARB, NULL, 809),
    NAME_FUNC_OFFSET(13397, glGetnUniformfvARB, glGetnUniformfvARB, NULL, 810),
    NAME_FUNC_OFFSET(13416, glGetnUniformivARB, glGetnUniformivARB, NULL, 811),
    NAME_FUNC_OFFSET(13435, glGetnUniformuivARB, glGetnUniformuivARB, NULL, 812),
    NAME_FUNC_OFFSET(13455, glReadnPixelsARB, glReadnPixelsARB, NULL, 813),
    NAME_FUNC_OFFSET(13472, glDrawArraysInstancedBaseInstance, glDrawArraysInstancedBaseInstance, NULL, 814),
    NAME_FUNC_OFFSET(13506, glDrawElementsInstancedBaseInstance, glDrawElementsInstancedBaseInstance, NULL, 815),
    NAME_FUNC_OFFSET(13542, glDrawElementsInstancedBaseVertexBaseInstance, glDrawElementsInstancedBaseVertexBaseInstance, NULL, 816),
    NAME_FUNC_OFFSET(13588, glDrawTransformFeedbackInstanced, glDrawTransformFeedbackInstanced, NULL, 817),
    NAME_FUNC_OFFSET(13621, glDrawTransformFeedbackStreamInstanced, glDrawTransformFeedbackStreamInstanced, NULL, 818),
    NAME_FUNC_OFFSET(13660, gl_dispatch_stub_819, gl_dispatch_stub_819, NULL, 819),
    NAME_FUNC_OFFSET(13682, glTexStorage1D, glTexStorage1D, NULL, 820),
    NAME_FUNC_OFFSET(13697, glTexStorage2D, glTexStorage2D, NULL, 821),
    NAME_FUNC_OFFSET(13712, glTexStorage3D, glTexStorage3D, NULL, 822),
    NAME_FUNC_OFFSET(13727, glTextureStorage1DEXT, glTextureStorage1DEXT, NULL, 823),
    NAME_FUNC_OFFSET(13749, glTextureStorage2DEXT, glTextureStorage2DEXT, NULL, 824),
    NAME_FUNC_OFFSET(13771, glTextureStorage3DEXT, glTextureStorage3DEXT, NULL, 825),
    NAME_FUNC_OFFSET(13793, glTexBufferRange, glTexBufferRange, NULL, 826),
    NAME_FUNC_OFFSET(13810, glInvalidateBufferData, glInvalidateBufferData, NULL, 827),
    NAME_FUNC_OFFSET(13833, glInvalidateBufferSubData, glInvalidateBufferSubData, NULL, 828),
    NAME_FUNC_OFFSET(13859, glInvalidateFramebuffer, glInvalidateFramebuffer, NULL, 829),
    NAME_FUNC_OFFSET(13883, glInvalidateSubFramebuffer, glInvalidateSubFramebuffer, NULL, 830),
    NAME_FUNC_OFFSET(13910, glInvalidateTexImage, glInvalidateTexImage, NULL, 831),
    NAME_FUNC_OFFSET(13931, glInvalidateTexSubImage, glInvalidateTexSubImage, NULL, 832),
    NAME_FUNC_OFFSET(13955, glPolygonOffsetEXT, glPolygonOffsetEXT, NULL, 833),
    NAME_FUNC_OFFSET(13974, gl_dispatch_stub_834, gl_dispatch_stub_834, NULL, 834),
    NAME_FUNC_OFFSET(13988, gl_dispatch_stub_835, gl_dispatch_stub_835, NULL, 835),
    NAME_FUNC_OFFSET(14003, gl_dispatch_stub_836, gl_dispatch_stub_836, NULL, 836),
    NAME_FUNC_OFFSET(14017, gl_dispatch_stub_837, gl_dispatch_stub_837, NULL, 837),
    NAME_FUNC_OFFSET(14032, gl_dispatch_stub_838, gl_dispatch_stub_838, NULL, 838),
    NAME_FUNC_OFFSET(14046, gl_dispatch_stub_839, gl_dispatch_stub_839, NULL, 839),
    NAME_FUNC_OFFSET(14061, gl_dispatch_stub_840, gl_dispatch_stub_840, NULL, 840),
    NAME_FUNC_OFFSET(14075, gl_dispatch_stub_841, gl_dispatch_stub_841, NULL, 841),
    NAME_FUNC_OFFSET(14090, gl_dispatch_stub_842, gl_dispatch_stub_842, NULL, 842),
    NAME_FUNC_OFFSET(14112, gl_dispatch_stub_843, gl_dispatch_stub_843, NULL, 843),
    NAME_FUNC_OFFSET(14130, gl_dispatch_stub_844, gl_dispatch_stub_844, NULL, 844),
    NAME_FUNC_OFFSET(14147, gl_dispatch_stub_845, gl_dispatch_stub_845, NULL, 845),
    NAME_FUNC_OFFSET(14167, glColorPointerEXT, glColorPointerEXT, NULL, 846),
    NAME_FUNC_OFFSET(14185, glEdgeFlagPointerEXT, glEdgeFlagPointerEXT, NULL, 847),
    NAME_FUNC_OFFSET(14206, glIndexPointerEXT, glIndexPointerEXT, NULL, 848),
    NAME_FUNC_OFFSET(14224, glNormalPointerEXT, glNormalPointerEXT, NULL, 849),
    NAME_FUNC_OFFSET(14243, glTexCoordPointerEXT, glTexCoordPointerEXT, NULL, 850),
    NAME_FUNC_OFFSET(14264, glVertexPointerEXT, glVertexPointerEXT, NULL, 851),
    NAME_FUNC_OFFSET(14283, gl_dispatch_stub_852, gl_dispatch_stub_852, NULL, 852),
    NAME_FUNC_OFFSET(14307, glLockArraysEXT, glLockArraysEXT, NULL, 853),
    NAME_FUNC_OFFSET(14323, glUnlockArraysEXT, glUnlockArraysEXT, NULL, 854),
    NAME_FUNC_OFFSET(14341, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 855),
    NAME_FUNC_OFFSET(14363, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 856),
    NAME_FUNC_OFFSET(14386, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 857),
    NAME_FUNC_OFFSET(14409, glFogCoordfEXT, glFogCoordfEXT, NULL, 858),
    NAME_FUNC_OFFSET(14424, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 859),
    NAME_FUNC_OFFSET(14440, glResizeBuffersMESA, glResizeBuffersMESA, NULL, 860),
    NAME_FUNC_OFFSET(14460, glWindowPos4dMESA, glWindowPos4dMESA, NULL, 861),
    NAME_FUNC_OFFSET(14478, glWindowPos4dvMESA, glWindowPos4dvMESA, NULL, 862),
    NAME_FUNC_OFFSET(14497, glWindowPos4fMESA, glWindowPos4fMESA, NULL, 863),
    NAME_FUNC_OFFSET(14515, glWindowPos4fvMESA, glWindowPos4fvMESA, NULL, 864),
    NAME_FUNC_OFFSET(14534, glWindowPos4iMESA, glWindowPos4iMESA, NULL, 865),
    NAME_FUNC_OFFSET(14552, glWindowPos4ivMESA, glWindowPos4ivMESA, NULL, 866),
    NAME_FUNC_OFFSET(14571, glWindowPos4sMESA, glWindowPos4sMESA, NULL, 867),
    NAME_FUNC_OFFSET(14589, glWindowPos4svMESA, glWindowPos4svMESA, NULL, 868),
    NAME_FUNC_OFFSET(14608, gl_dispatch_stub_869, gl_dispatch_stub_869, NULL, 869),
    NAME_FUNC_OFFSET(14633, gl_dispatch_stub_870, gl_dispatch_stub_870, NULL, 870),
    NAME_FUNC_OFFSET(14660, glAreProgramsResidentNV, glAreProgramsResidentNV, NULL, 871),
    NAME_FUNC_OFFSET(14684, glExecuteProgramNV, glExecuteProgramNV, NULL, 872),
    NAME_FUNC_OFFSET(14703, glGetProgramParameterdvNV, glGetProgramParameterdvNV, NULL, 873),
    NAME_FUNC_OFFSET(14729, glGetProgramParameterfvNV, glGetProgramParameterfvNV, NULL, 874),
    NAME_FUNC_OFFSET(14755, glGetProgramStringNV, glGetProgramStringNV, NULL, 875),
    NAME_FUNC_OFFSET(14776, glGetProgramivNV, glGetProgramivNV, NULL, 876),
    NAME_FUNC_OFFSET(14793, glGetTrackMatrixivNV, glGetTrackMatrixivNV, NULL, 877),
    NAME_FUNC_OFFSET(14814, glGetVertexAttribdvNV, glGetVertexAttribdvNV, NULL, 878),
    NAME_FUNC_OFFSET(14836, glGetVertexAttribfvNV, glGetVertexAttribfvNV, NULL, 879),
    NAME_FUNC_OFFSET(14858, glGetVertexAttribivNV, glGetVertexAttribivNV, NULL, 880),
    NAME_FUNC_OFFSET(14880, glLoadProgramNV, glLoadProgramNV, NULL, 881),
    NAME_FUNC_OFFSET(14896, glProgramParameters4dvNV, glProgramParameters4dvNV, NULL, 882),
    NAME_FUNC_OFFSET(14921, glProgramParameters4fvNV, glProgramParameters4fvNV, NULL, 883),
    NAME_FUNC_OFFSET(14946, glRequestResidentProgramsNV, glRequestResidentProgramsNV, NULL, 884),
    NAME_FUNC_OFFSET(14974, glTrackMatrixNV, glTrackMatrixNV, NULL, 885),
    NAME_FUNC_OFFSET(14990, glVertexAttrib1dNV, glVertexAttrib1dNV, NULL, 886),
    NAME_FUNC_OFFSET(15009, glVertexAttrib1dvNV, glVertexAttrib1dvNV, NULL, 887),
    NAME_FUNC_OFFSET(15029, glVertexAttrib1fNV, glVertexAttrib1fNV, NULL, 888),
    NAME_FUNC_OFFSET(15048, glVertexAttrib1fvNV, glVertexAttrib1fvNV, NULL, 889),
    NAME_FUNC_OFFSET(15068, glVertexAttrib1sNV, glVertexAttrib1sNV, NULL, 890),
    NAME_FUNC_OFFSET(15087, glVertexAttrib1svNV, glVertexAttrib1svNV, NULL, 891),
    NAME_FUNC_OFFSET(15107, glVertexAttrib2dNV, glVertexAttrib2dNV, NULL, 892),
    NAME_FUNC_OFFSET(15126, glVertexAttrib2dvNV, glVertexAttrib2dvNV, NULL, 893),
    NAME_FUNC_OFFSET(15146, glVertexAttrib2fNV, glVertexAttrib2fNV, NULL, 894),
    NAME_FUNC_OFFSET(15165, glVertexAttrib2fvNV, glVertexAttrib2fvNV, NULL, 895),
    NAME_FUNC_OFFSET(15185, glVertexAttrib2sNV, glVertexAttrib2sNV, NULL, 896),
    NAME_FUNC_OFFSET(15204, glVertexAttrib2svNV, glVertexAttrib2svNV, NULL, 897),
    NAME_FUNC_OFFSET(15224, glVertexAttrib3dNV, glVertexAttrib3dNV, NULL, 898),
    NAME_FUNC_OFFSET(15243, glVertexAttrib3dvNV, glVertexAttrib3dvNV, NULL, 899),
    NAME_FUNC_OFFSET(15263, glVertexAttrib3fNV, glVertexAttrib3fNV, NULL, 900),
    NAME_FUNC_OFFSET(15282, glVertexAttrib3fvNV, glVertexAttrib3fvNV, NULL, 901),
    NAME_FUNC_OFFSET(15302, glVertexAttrib3sNV, glVertexAttrib3sNV, NULL, 902),
    NAME_FUNC_OFFSET(15321, glVertexAttrib3svNV, glVertexAttrib3svNV, NULL, 903),
    NAME_FUNC_OFFSET(15341, glVertexAttrib4dNV, glVertexAttrib4dNV, NULL, 904),
    NAME_FUNC_OFFSET(15360, glVertexAttrib4dvNV, glVertexAttrib4dvNV, NULL, 905),
    NAME_FUNC_OFFSET(15380, glVertexAttrib4fNV, glVertexAttrib4fNV, NULL, 906),
    NAME_FUNC_OFFSET(15399, glVertexAttrib4fvNV, glVertexAttrib4fvNV, NULL, 907),
    NAME_FUNC_OFFSET(15419, glVertexAttrib4sNV, glVertexAttrib4sNV, NULL, 908),
    NAME_FUNC_OFFSET(15438, glVertexAttrib4svNV, glVertexAttrib4svNV, NULL, 909),
    NAME_FUNC_OFFSET(15458, glVertexAttrib4ubNV, glVertexAttrib4ubNV, NULL, 910),
    NAME_FUNC_OFFSET(15478, glVertexAttrib4ubvNV, glVertexAttrib4ubvNV, NULL, 911),
    NAME_FUNC_OFFSET(15499, glVertexAttribPointerNV, glVertexAttribPointerNV, NULL, 912),
    NAME_FUNC_OFFSET(15523, glVertexAttribs1dvNV, glVertexAttribs1dvNV, NULL, 913),
    NAME_FUNC_OFFSET(15544, glVertexAttribs1fvNV, glVertexAttribs1fvNV, NULL, 914),
    NAME_FUNC_OFFSET(15565, glVertexAttribs1svNV, glVertexAttribs1svNV, NULL, 915),
    NAME_FUNC_OFFSET(15586, glVertexAttribs2dvNV, glVertexAttribs2dvNV, NULL, 916),
    NAME_FUNC_OFFSET(15607, glVertexAttribs2fvNV, glVertexAttribs2fvNV, NULL, 917),
    NAME_FUNC_OFFSET(15628, glVertexAttribs2svNV, glVertexAttribs2svNV, NULL, 918),
    NAME_FUNC_OFFSET(15649, glVertexAttribs3dvNV, glVertexAttribs3dvNV, NULL, 919),
    NAME_FUNC_OFFSET(15670, glVertexAttribs3fvNV, glVertexAttribs3fvNV, NULL, 920),
    NAME_FUNC_OFFSET(15691, glVertexAttribs3svNV, glVertexAttribs3svNV, NULL, 921),
    NAME_FUNC_OFFSET(15712, glVertexAttribs4dvNV, glVertexAttribs4dvNV, NULL, 922),
    NAME_FUNC_OFFSET(15733, glVertexAttribs4fvNV, glVertexAttribs4fvNV, NULL, 923),
    NAME_FUNC_OFFSET(15754, glVertexAttribs4svNV, glVertexAttribs4svNV, NULL, 924),
    NAME_FUNC_OFFSET(15775, glVertexAttribs4ubvNV, glVertexAttribs4ubvNV, NULL, 925),
    NAME_FUNC_OFFSET(15797, glGetTexBumpParameterfvATI, glGetTexBumpParameterfvATI, NULL, 926),
    NAME_FUNC_OFFSET(15824, glGetTexBumpParameterivATI, glGetTexBumpParameterivATI, NULL, 927),
    NAME_FUNC_OFFSET(15851, glTexBumpParameterfvATI, glTexBumpParameterfvATI, NULL, 928),
    NAME_FUNC_OFFSET(15875, glTexBumpParameterivATI, glTexBumpParameterivATI, NULL, 929),
    NAME_FUNC_OFFSET(15899, glAlphaFragmentOp1ATI, glAlphaFragmentOp1ATI, NULL, 930),
    NAME_FUNC_OFFSET(15921, glAlphaFragmentOp2ATI, glAlphaFragmentOp2ATI, NULL, 931),
    NAME_FUNC_OFFSET(15943, glAlphaFragmentOp3ATI, glAlphaFragmentOp3ATI, NULL, 932),
    NAME_FUNC_OFFSET(15965, glBeginFragmentShaderATI, glBeginFragmentShaderATI, NULL, 933),
    NAME_FUNC_OFFSET(15990, glBindFragmentShaderATI, glBindFragmentShaderATI, NULL, 934),
    NAME_FUNC_OFFSET(16014, glColorFragmentOp1ATI, glColorFragmentOp1ATI, NULL, 935),
    NAME_FUNC_OFFSET(16036, glColorFragmentOp2ATI, glColorFragmentOp2ATI, NULL, 936),
    NAME_FUNC_OFFSET(16058, glColorFragmentOp3ATI, glColorFragmentOp3ATI, NULL, 937),
    NAME_FUNC_OFFSET(16080, glDeleteFragmentShaderATI, glDeleteFragmentShaderATI, NULL, 938),
    NAME_FUNC_OFFSET(16106, glEndFragmentShaderATI, glEndFragmentShaderATI, NULL, 939),
    NAME_FUNC_OFFSET(16129, glGenFragmentShadersATI, glGenFragmentShadersATI, NULL, 940),
    NAME_FUNC_OFFSET(16153, glPassTexCoordATI, glPassTexCoordATI, NULL, 941),
    NAME_FUNC_OFFSET(16171, glSampleMapATI, glSampleMapATI, NULL, 942),
    NAME_FUNC_OFFSET(16186, glSetFragmentShaderConstantATI, glSetFragmentShaderConstantATI, NULL, 943),
    NAME_FUNC_OFFSET(16217, gl_dispatch_stub_944, gl_dispatch_stub_944, NULL, 944),
    NAME_FUNC_OFFSET(16240, gl_dispatch_stub_945, gl_dispatch_stub_945, NULL, 945),
    NAME_FUNC_OFFSET(16263, gl_dispatch_stub_946, gl_dispatch_stub_946, NULL, 946),
    NAME_FUNC_OFFSET(16286, glGetProgramNamedParameterdvNV, glGetProgramNamedParameterdvNV, NULL, 947),
    NAME_FUNC_OFFSET(16317, glGetProgramNamedParameterfvNV, glGetProgramNamedParameterfvNV, NULL, 948),
    NAME_FUNC_OFFSET(16348, glProgramNamedParameter4dNV, glProgramNamedParameter4dNV, NULL, 949),
    NAME_FUNC_OFFSET(16376, glProgramNamedParameter4dvNV, glProgramNamedParameter4dvNV, NULL, 950),
    NAME_FUNC_OFFSET(16405, glProgramNamedParameter4fNV, glProgramNamedParameter4fNV, NULL, 951),
    NAME_FUNC_OFFSET(16433, glProgramNamedParameter4fvNV, glProgramNamedParameter4fvNV, NULL, 952),
    NAME_FUNC_OFFSET(16462, glPrimitiveRestartNV, glPrimitiveRestartNV, NULL, 953),
    NAME_FUNC_OFFSET(16483, gl_dispatch_stub_954, gl_dispatch_stub_954, NULL, 954),
    NAME_FUNC_OFFSET(16500, gl_dispatch_stub_955, gl_dispatch_stub_955, NULL, 955),
    NAME_FUNC_OFFSET(16513, gl_dispatch_stub_956, gl_dispatch_stub_956, NULL, 956),
    NAME_FUNC_OFFSET(16527, gl_dispatch_stub_957, gl_dispatch_stub_957, NULL, 957),
    NAME_FUNC_OFFSET(16544, gl_dispatch_stub_958, gl_dispatch_stub_958, NULL, 958),
    NAME_FUNC_OFFSET(16568, gl_dispatch_stub_959, gl_dispatch_stub_959, NULL, 959),
    NAME_FUNC_OFFSET(16598, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 960),
    NAME_FUNC_OFFSET(16619, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 961),
    NAME_FUNC_OFFSET(16641, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 962),
    NAME_FUNC_OFFSET(16662, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 963),
    NAME_FUNC_OFFSET(16684, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 964),
    NAME_FUNC_OFFSET(16706, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 965),
    NAME_FUNC_OFFSET(16729, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 966),
    NAME_FUNC_OFFSET(16750, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 967),
    NAME_FUNC_OFFSET(16772, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 968),
    NAME_FUNC_OFFSET(16794, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 969),
    NAME_FUNC_OFFSET(16817, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 970),
    NAME_FUNC_OFFSET(16838, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 971),
    NAME_FUNC_OFFSET(16860, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 972),
    NAME_FUNC_OFFSET(16882, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 973),
    NAME_FUNC_OFFSET(16905, glClearColorIiEXT, glClearColorIiEXT, NULL, 974),
    NAME_FUNC_OFFSET(16923, glClearColorIuiEXT, glClearColorIuiEXT, NULL, 975),
    NAME_FUNC_OFFSET(16942, glBindBufferOffsetEXT, glBindBufferOffsetEXT, NULL, 976),
    NAME_FUNC_OFFSET(16964, glGetObjectParameterivAPPLE, glGetObjectParameterivAPPLE, NULL, 977),
    NAME_FUNC_OFFSET(16992, glObjectPurgeableAPPLE, glObjectPurgeableAPPLE, NULL, 978),
    NAME_FUNC_OFFSET(17015, glObjectUnpurgeableAPPLE, glObjectUnpurgeableAPPLE, NULL, 979),
    NAME_FUNC_OFFSET(17040, glActiveProgramEXT, glActiveProgramEXT, NULL, 980),
    NAME_FUNC_OFFSET(17059, glCreateShaderProgramEXT, glCreateShaderProgramEXT, NULL, 981),
    NAME_FUNC_OFFSET(17084, glUseShaderProgramEXT, glUseShaderProgramEXT, NULL, 982),
    NAME_FUNC_OFFSET(17106, glTextureBarrierNV, glTextureBarrierNV, NULL, 983),
    NAME_FUNC_OFFSET(17125, gl_dispatch_stub_984, gl_dispatch_stub_984, NULL, 984),
    NAME_FUNC_OFFSET(17150, gl_dispatch_stub_985, gl_dispatch_stub_985, NULL, 985),
    NAME_FUNC_OFFSET(17179, gl_dispatch_stub_986, gl_dispatch_stub_986, NULL, 986),
    NAME_FUNC_OFFSET(17210, glEGLImageTargetRenderbufferStorageOES, glEGLImageTargetRenderbufferStorageOES, NULL, 987),
    NAME_FUNC_OFFSET(17249, glEGLImageTargetTexture2DOES, glEGLImageTargetTexture2DOES, NULL, 988),
    NAME_FUNC_OFFSET(17278, gl_dispatch_stub_989, gl_dispatch_stub_989, NULL, 989),
    NAME_FUNC_OFFSET(17291, gl_dispatch_stub_990, gl_dispatch_stub_990, NULL, 990),
    NAME_FUNC_OFFSET(17305, gl_dispatch_stub_991, gl_dispatch_stub_991, NULL, 991),
    NAME_FUNC_OFFSET(17319, gl_dispatch_stub_992, gl_dispatch_stub_992, NULL, 992),
    NAME_FUNC_OFFSET(17329, gl_dispatch_stub_993, gl_dispatch_stub_993, NULL, 993),
    NAME_FUNC_OFFSET(17343, gl_dispatch_stub_994, gl_dispatch_stub_994, NULL, 994),
    NAME_FUNC_OFFSET(17350, gl_dispatch_stub_995, gl_dispatch_stub_995, NULL, 995),
    NAME_FUNC_OFFSET(17358, gl_dispatch_stub_996, gl_dispatch_stub_996, NULL, 996),
    NAME_FUNC_OFFSET(17369, gl_dispatch_stub_997, gl_dispatch_stub_997, NULL, 997),
    NAME_FUNC_OFFSET(17380, gl_dispatch_stub_998, gl_dispatch_stub_998, NULL, 998),
    NAME_FUNC_OFFSET(17394, gl_dispatch_stub_999, gl_dispatch_stub_999, NULL, 999),
    NAME_FUNC_OFFSET(17409, gl_dispatch_stub_1000, gl_dispatch_stub_1000, NULL, 1000),
    NAME_FUNC_OFFSET(17418, gl_dispatch_stub_1001, gl_dispatch_stub_1001, NULL, 1001),
    NAME_FUNC_OFFSET(17428, gl_dispatch_stub_1002, gl_dispatch_stub_1002, NULL, 1002),
    NAME_FUNC_OFFSET(17441, gl_dispatch_stub_1003, gl_dispatch_stub_1003, NULL, 1003),
    NAME_FUNC_OFFSET(17455, gl_dispatch_stub_1004, gl_dispatch_stub_1004, NULL, 1004),
    NAME_FUNC_OFFSET(17467, gl_dispatch_stub_1005, gl_dispatch_stub_1005, NULL, 1005),
    NAME_FUNC_OFFSET(17480, gl_dispatch_stub_1006, gl_dispatch_stub_1006, NULL, 1006),
    NAME_FUNC_OFFSET(17494, gl_dispatch_stub_1007, gl_dispatch_stub_1007, NULL, 1007),
    NAME_FUNC_OFFSET(17512, gl_dispatch_stub_1008, gl_dispatch_stub_1008, NULL, 1008),
    NAME_FUNC_OFFSET(17523, gl_dispatch_stub_1009, gl_dispatch_stub_1009, NULL, 1009),
    NAME_FUNC_OFFSET(17532, gl_dispatch_stub_1010, gl_dispatch_stub_1010, NULL, 1010),
    NAME_FUNC_OFFSET(17541, gl_dispatch_stub_1011, gl_dispatch_stub_1011, NULL, 1011),
    NAME_FUNC_OFFSET(17554, gl_dispatch_stub_1012, gl_dispatch_stub_1012, NULL, 1012),
    NAME_FUNC_OFFSET(17571, gl_dispatch_stub_1013, gl_dispatch_stub_1013, NULL, 1013),
    NAME_FUNC_OFFSET(17581, gl_dispatch_stub_1014, gl_dispatch_stub_1014, NULL, 1014),
    NAME_FUNC_OFFSET(17599, gl_dispatch_stub_1015, gl_dispatch_stub_1015, NULL, 1015),
    NAME_FUNC_OFFSET(17608, gl_dispatch_stub_1016, gl_dispatch_stub_1016, NULL, 1016),
    NAME_FUNC_OFFSET(17618, gl_dispatch_stub_1017, gl_dispatch_stub_1017, NULL, 1017),
    NAME_FUNC_OFFSET(17629, gl_dispatch_stub_1018, gl_dispatch_stub_1018, NULL, 1018),
    NAME_FUNC_OFFSET(17645, gl_dispatch_stub_1019, gl_dispatch_stub_1019, NULL, 1019),
    NAME_FUNC_OFFSET(17658, gl_dispatch_stub_1020, gl_dispatch_stub_1020, NULL, 1020),
    NAME_FUNC_OFFSET(17671, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(17684, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(17700, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(17716, gl_dispatch_stub_1024, gl_dispatch_stub_1024, NULL, 1024),
    NAME_FUNC_OFFSET(17728, gl_dispatch_stub_1025, gl_dispatch_stub_1025, NULL, 1025),
    NAME_FUNC_OFFSET(17741, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(17757, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(17771, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(17791, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(17809, gl_dispatch_stub_1030, gl_dispatch_stub_1030, NULL, 1030),
    NAME_FUNC_OFFSET(17828, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(17845, glTexGenf, glTexGenf, NULL, 190),
    NAME_FUNC_OFFSET(17858, glTexGenfv, glTexGenfv, NULL, 191),
    NAME_FUNC_OFFSET(17872, glTexGeni, glTexGeni, NULL, 192),
    NAME_FUNC_OFFSET(17885, glTexGeniv, glTexGeniv, NULL, 193),
    NAME_FUNC_OFFSET(17899, glReadBuffer, glReadBuffer, NULL, 254),
    NAME_FUNC_OFFSET(17914, glGetTexGenfv, glGetTexGenfv, NULL, 279),
    NAME_FUNC_OFFSET(17931, glGetTexGeniv, glGetTexGeniv, NULL, 280),
    NAME_FUNC_OFFSET(17948, glArrayElement, glArrayElement, NULL, 306),
    NAME_FUNC_OFFSET(17966, glBindTexture, glBindTexture, NULL, 307),
    NAME_FUNC_OFFSET(17983, glDrawArrays, glDrawArrays, NULL, 310),
    NAME_FUNC_OFFSET(17999, glAreTexturesResident, glAreTexturesResidentEXT, glAreTexturesResidentEXT, 322),
    NAME_FUNC_OFFSET(18024, glCopyTexImage1D, glCopyTexImage1D, NULL, 323),
    NAME_FUNC_OFFSET(18044, glCopyTexImage2D, glCopyTexImage2D, NULL, 324),
    NAME_FUNC_OFFSET(18064, glCopyTexSubImage1D, glCopyTexSubImage1D, NULL, 325),
    NAME_FUNC_OFFSET(18087, glCopyTexSubImage2D, glCopyTexSubImage2D, NULL, 326),
    NAME_FUNC_OFFSET(18110, glDeleteTextures, glDeleteTexturesEXT, glDeleteTexturesEXT, 327),
    NAME_FUNC_OFFSET(18130, glGenTextures, glGenTexturesEXT, glGenTexturesEXT, 328),
    NAME_FUNC_OFFSET(18147, glGetPointerv, glGetPointerv, NULL, 329),
    NAME_FUNC_OFFSET(18164, glIsTexture, glIsTextureEXT, glIsTextureEXT, 330),
    NAME_FUNC_OFFSET(18179, glPrioritizeTextures, glPrioritizeTextures, NULL, 331),
    NAME_FUNC_OFFSET(18203, glTexSubImage1D, glTexSubImage1D, NULL, 332),
    NAME_FUNC_OFFSET(18222, glTexSubImage2D, glTexSubImage2D, NULL, 333),
    NAME_FUNC_OFFSET(18241, glBlendColor, glBlendColor, NULL, 336),
    NAME_FUNC_OFFSET(18257, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(18276, glBlendEquation, glBlendEquation, NULL, 337),
    NAME_FUNC_OFFSET(18295, glDrawRangeElements, glDrawRangeElements, NULL, 338),
    NAME_FUNC_OFFSET(18318, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(18334, glColorTable, glColorTable, NULL, 339),
    NAME_FUNC_OFFSET(18350, glColorTableParameterfv, glColorTableParameterfv, NULL, 340),
    NAME_FUNC_OFFSET(18377, glColorTableParameteriv, glColorTableParameteriv, NULL, 341),
    NAME_FUNC_OFFSET(18404, glCopyColorTable, glCopyColorTable, NULL, 342),
    NAME_FUNC_OFFSET(18424, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(18443, glGetColorTable, glGetColorTableEXT, glGetColorTableEXT, 343),
    NAME_FUNC_OFFSET(18462, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(18492, glGetColorTableParameterfv, glGetColorTableParameterfvEXT, glGetColorTableParameterfvEXT, 344),
    NAME_FUNC_OFFSET(18522, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(18552, glGetColorTableParameteriv, glGetColorTableParameterivEXT, glGetColorTableParameterivEXT, 345),
    NAME_FUNC_OFFSET(18582, glColorSubTable, glColorSubTable, NULL, 346),
    NAME_FUNC_OFFSET(18601, glCopyColorSubTable, glCopyColorSubTable, NULL, 347),
    NAME_FUNC_OFFSET(18624, glConvolutionFilter1D, glConvolutionFilter1D, NULL, 348),
    NAME_FUNC_OFFSET(18649, glConvolutionFilter2D, glConvolutionFilter2D, NULL, 349),
    NAME_FUNC_OFFSET(18674, glConvolutionParameterf, glConvolutionParameterf, NULL, 350),
    NAME_FUNC_OFFSET(18701, glConvolutionParameterfv, glConvolutionParameterfv, NULL, 351),
    NAME_FUNC_OFFSET(18729, glConvolutionParameteri, glConvolutionParameteri, NULL, 352),
    NAME_FUNC_OFFSET(18756, glConvolutionParameteriv, glConvolutionParameteriv, NULL, 353),
    NAME_FUNC_OFFSET(18784, glCopyConvolutionFilter1D, glCopyConvolutionFilter1D, NULL, 354),
    NAME_FUNC_OFFSET(18813, glCopyConvolutionFilter2D, glCopyConvolutionFilter2D, NULL, 355),
    NAME_FUNC_OFFSET(18842, glGetConvolutionFilter, gl_dispatch_stub_356, gl_dispatch_stub_356, 356),
    NAME_FUNC_OFFSET(18868, glGetConvolutionParameterfv, gl_dispatch_stub_357, gl_dispatch_stub_357, 357),
    NAME_FUNC_OFFSET(18899, glGetConvolutionParameteriv, gl_dispatch_stub_358, gl_dispatch_stub_358, 358),
    NAME_FUNC_OFFSET(18930, glGetSeparableFilter, gl_dispatch_stub_359, gl_dispatch_stub_359, 359),
    NAME_FUNC_OFFSET(18954, glSeparableFilter2D, glSeparableFilter2D, NULL, 360),
    NAME_FUNC_OFFSET(18977, glGetHistogram, gl_dispatch_stub_361, gl_dispatch_stub_361, 361),
    NAME_FUNC_OFFSET(18995, glGetHistogramParameterfv, gl_dispatch_stub_362, gl_dispatch_stub_362, 362),
    NAME_FUNC_OFFSET(19024, glGetHistogramParameteriv, gl_dispatch_stub_363, gl_dispatch_stub_363, 363),
    NAME_FUNC_OFFSET(19053, glGetMinmax, gl_dispatch_stub_364, gl_dispatch_stub_364, 364),
    NAME_FUNC_OFFSET(19068, glGetMinmaxParameterfv, gl_dispatch_stub_365, gl_dispatch_stub_365, 365),
    NAME_FUNC_OFFSET(19094, glGetMinmaxParameteriv, gl_dispatch_stub_366, gl_dispatch_stub_366, 366),
    NAME_FUNC_OFFSET(19120, glHistogram, glHistogram, NULL, 367),
    NAME_FUNC_OFFSET(19135, glMinmax, glMinmax, NULL, 368),
    NAME_FUNC_OFFSET(19147, glResetHistogram, glResetHistogram, NULL, 369),
    NAME_FUNC_OFFSET(19167, glResetMinmax, glResetMinmax, NULL, 370),
    NAME_FUNC_OFFSET(19184, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(19200, glTexImage3D, glTexImage3D, NULL, 371),
    NAME_FUNC_OFFSET(19216, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(19235, glTexSubImage3D, glTexSubImage3D, NULL, 372),
    NAME_FUNC_OFFSET(19254, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(19277, glCopyTexSubImage3D, glCopyTexSubImage3D, NULL, 373),
    NAME_FUNC_OFFSET(19300, glActiveTexture, glActiveTexture, NULL, 374),
    NAME_FUNC_OFFSET(19319, glClientActiveTexture, glClientActiveTexture, NULL, 375),
    NAME_FUNC_OFFSET(19344, glMultiTexCoord1d, glMultiTexCoord1d, NULL, 376),
    NAME_FUNC_OFFSET(19365, glMultiTexCoord1dv, glMultiTexCoord1dv, NULL, 377),
    NAME_FUNC_OFFSET(19387, glMultiTexCoord1fARB, glMultiTexCoord1fARB, NULL, 378),
    NAME_FUNC_OFFSET(19405, glMultiTexCoord1fvARB, glMultiTexCoord1fvARB, NULL, 379),
    NAME_FUNC_OFFSET(19424, glMultiTexCoord1i, glMultiTexCoord1i, NULL, 380),
    NAME_FUNC_OFFSET(19445, glMultiTexCoord1iv, glMultiTexCoord1iv, NULL, 381),
    NAME_FUNC_OFFSET(19467, glMultiTexCoord1s, glMultiTexCoord1s, NULL, 382),
    NAME_FUNC_OFFSET(19488, glMultiTexCoord1sv, glMultiTexCoord1sv, NULL, 383),
    NAME_FUNC_OFFSET(19510, glMultiTexCoord2d, glMultiTexCoord2d, NULL, 384),
    NAME_FUNC_OFFSET(19531, glMultiTexCoord2dv, glMultiTexCoord2dv, NULL, 385),
    NAME_FUNC_OFFSET(19553, glMultiTexCoord2fARB, glMultiTexCoord2fARB, NULL, 386),
    NAME_FUNC_OFFSET(19571, glMultiTexCoord2fvARB, glMultiTexCoord2fvARB, NULL, 387),
    NAME_FUNC_OFFSET(19590, glMultiTexCoord2i, glMultiTexCoord2i, NULL, 388),
    NAME_FUNC_OFFSET(19611, glMultiTexCoord2iv, glMultiTexCoord2iv, NULL, 389),
    NAME_FUNC_OFFSET(19633, glMultiTexCoord2s, glMultiTexCoord2s, NULL, 390),
    NAME_FUNC_OFFSET(19654, glMultiTexCoord2sv, glMultiTexCoord2sv, NULL, 391),
    NAME_FUNC_OFFSET(19676, glMultiTexCoord3d, glMultiTexCoord3d, NULL, 392),
    NAME_FUNC_OFFSET(19697, glMultiTexCoord3dv, glMultiTexCoord3dv, NULL, 393),
    NAME_FUNC_OFFSET(19719, glMultiTexCoord3fARB, glMultiTexCoord3fARB, NULL, 394),
    NAME_FUNC_OFFSET(19737, glMultiTexCoord3fvARB, glMultiTexCoord3fvARB, NULL, 395),
    NAME_FUNC_OFFSET(19756, glMultiTexCoord3i, glMultiTexCoord3i, NULL, 396),
    NAME_FUNC_OFFSET(19777, glMultiTexCoord3iv, glMultiTexCoord3iv, NULL, 397),
    NAME_FUNC_OFFSET(19799, glMultiTexCoord3s, glMultiTexCoord3s, NULL, 398),
    NAME_FUNC_OFFSET(19820, glMultiTexCoord3sv, glMultiTexCoord3sv, NULL, 399),
    NAME_FUNC_OFFSET(19842, glMultiTexCoord4d, glMultiTexCoord4d, NULL, 400),
    NAME_FUNC_OFFSET(19863, glMultiTexCoord4dv, glMultiTexCoord4dv, NULL, 401),
    NAME_FUNC_OFFSET(19885, glMultiTexCoord4fARB, glMultiTexCoord4fARB, NULL, 402),
    NAME_FUNC_OFFSET(19903, glMultiTexCoord4fvARB, glMultiTexCoord4fvARB, NULL, 403),
    NAME_FUNC_OFFSET(19922, glMultiTexCoord4i, glMultiTexCoord4i, NULL, 404),
    NAME_FUNC_OFFSET(19943, glMultiTexCoord4iv, glMultiTexCoord4iv, NULL, 405),
    NAME_FUNC_OFFSET(19965, glMultiTexCoord4s, glMultiTexCoord4s, NULL, 406),
    NAME_FUNC_OFFSET(19986, glMultiTexCoord4sv, glMultiTexCoord4sv, NULL, 407),
    NAME_FUNC_OFFSET(20008, glCompressedTexImage1D, glCompressedTexImage1D, NULL, 408),
    NAME_FUNC_OFFSET(20034, glCompressedTexImage2D, glCompressedTexImage2D, NULL, 409),
    NAME_FUNC_OFFSET(20060, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(20086, glCompressedTexImage3D, glCompressedTexImage3D, NULL, 410),
    NAME_FUNC_OFFSET(20112, glCompressedTexSubImage1D, glCompressedTexSubImage1D, NULL, 411),
    NAME_FUNC_OFFSET(20141, glCompressedTexSubImage2D, glCompressedTexSubImage2D, NULL, 412),
    NAME_FUNC_OFFSET(20170, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(20199, glCompressedTexSubImage3D, glCompressedTexSubImage3D, NULL, 413),
    NAME_FUNC_OFFSET(20228, glGetCompressedTexImage, glGetCompressedTexImage, NULL, 414),
    NAME_FUNC_OFFSET(20255, glLoadTransposeMatrixd, glLoadTransposeMatrixd, NULL, 415),
    NAME_FUNC_OFFSET(20281, glLoadTransposeMatrixf, glLoadTransposeMatrixf, NULL, 416),
    NAME_FUNC_OFFSET(20307, glMultTransposeMatrixd, glMultTransposeMatrixd, NULL, 417),
    NAME_FUNC_OFFSET(20333, glMultTransposeMatrixf, glMultTransposeMatrixf, NULL, 418),
    NAME_FUNC_OFFSET(20359, glSampleCoverage, glSampleCoverage, NULL, 419),
    NAME_FUNC_OFFSET(20379, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(20402, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(20426, glBlendFuncSeparate, glBlendFuncSeparate, NULL, 420),
    NAME_FUNC_OFFSET(20449, glFogCoordPointer, glFogCoordPointer, NULL, 421),
    NAME_FUNC_OFFSET(20470, glFogCoordd, glFogCoordd, NULL, 422),
    NAME_FUNC_OFFSET(20485, glFogCoorddv, glFogCoorddv, NULL, 423),
    NAME_FUNC_OFFSET(20501, glMultiDrawArrays, glMultiDrawArrays, NULL, 424),
    NAME_FUNC_OFFSET(20522, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(20543, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(20564, glPointParameterf, glPointParameterf, NULL, 425),
    NAME_FUNC_OFFSET(20586, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(20608, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(20630, glPointParameterfv, glPointParameterfv, NULL, 426),
    NAME_FUNC_OFFSET(20653, glPointParameteri, glPointParameteri, NULL, 427),
    NAME_FUNC_OFFSET(20673, glPointParameteriv, glPointParameteriv, NULL, 428),
    NAME_FUNC_OFFSET(20694, glSecondaryColor3b, glSecondaryColor3b, NULL, 429),
    NAME_FUNC_OFFSET(20716, glSecondaryColor3bv, glSecondaryColor3bv, NULL, 430),
    NAME_FUNC_OFFSET(20739, glSecondaryColor3d, glSecondaryColor3d, NULL, 431),
    NAME_FUNC_OFFSET(20761, glSecondaryColor3dv, glSecondaryColor3dv, NULL, 432),
    NAME_FUNC_OFFSET(20784, glSecondaryColor3i, glSecondaryColor3i, NULL, 433),
    NAME_FUNC_OFFSET(20806, glSecondaryColor3iv, glSecondaryColor3iv, NULL, 434),
    NAME_FUNC_OFFSET(20829, glSecondaryColor3s, glSecondaryColor3s, NULL, 435),
    NAME_FUNC_OFFSET(20851, glSecondaryColor3sv, glSecondaryColor3sv, NULL, 436),
    NAME_FUNC_OFFSET(20874, glSecondaryColor3ub, glSecondaryColor3ub, NULL, 437),
    NAME_FUNC_OFFSET(20897, glSecondaryColor3ubv, glSecondaryColor3ubv, NULL, 438),
    NAME_FUNC_OFFSET(20921, glSecondaryColor3ui, glSecondaryColor3ui, NULL, 439),
    NAME_FUNC_OFFSET(20944, glSecondaryColor3uiv, glSecondaryColor3uiv, NULL, 440),
    NAME_FUNC_OFFSET(20968, glSecondaryColor3us, glSecondaryColor3us, NULL, 441),
    NAME_FUNC_OFFSET(20991, glSecondaryColor3usv, glSecondaryColor3usv, NULL, 442),
    NAME_FUNC_OFFSET(21015, glSecondaryColorPointer, glSecondaryColorPointer, NULL, 443),
    NAME_FUNC_OFFSET(21042, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(21059, glWindowPos2d, glWindowPos2d, NULL, 444),
    NAME_FUNC_OFFSET(21077, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(21095, glWindowPos2dv, glWindowPos2dv, NULL, 445),
    NAME_FUNC_OFFSET(21114, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(21131, glWindowPos2f, glWindowPos2f, NULL, 446),
    NAME_FUNC_OFFSET(21149, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(21167, glWindowPos2fv, glWindowPos2fv, NULL, 447),
    NAME_FUNC_OFFSET(21186, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(21203, glWindowPos2i, glWindowPos2i, NULL, 448),
    NAME_FUNC_OFFSET(21221, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(21239, glWindowPos2iv, glWindowPos2iv, NULL, 449),
    NAME_FUNC_OFFSET(21258, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(21275, glWindowPos2s, glWindowPos2s, NULL, 450),
    NAME_FUNC_OFFSET(21293, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(21311, glWindowPos2sv, glWindowPos2sv, NULL, 451),
    NAME_FUNC_OFFSET(21330, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(21347, glWindowPos3d, glWindowPos3d, NULL, 452),
    NAME_FUNC_OFFSET(21365, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(21383, glWindowPos3dv, glWindowPos3dv, NULL, 453),
    NAME_FUNC_OFFSET(21402, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(21419, glWindowPos3f, glWindowPos3f, NULL, 454),
    NAME_FUNC_OFFSET(21437, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(21455, glWindowPos3fv, glWindowPos3fv, NULL, 455),
    NAME_FUNC_OFFSET(21474, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(21491, glWindowPos3i, glWindowPos3i, NULL, 456),
    NAME_FUNC_OFFSET(21509, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(21527, glWindowPos3iv, glWindowPos3iv, NULL, 457),
    NAME_FUNC_OFFSET(21546, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(21563, glWindowPos3s, glWindowPos3s, NULL, 458),
    NAME_FUNC_OFFSET(21581, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(21599, glWindowPos3sv, glWindowPos3sv, NULL, 459),
    NAME_FUNC_OFFSET(21618, glBeginQuery, glBeginQuery, NULL, 460),
    NAME_FUNC_OFFSET(21634, glBindBuffer, glBindBuffer, NULL, 461),
    NAME_FUNC_OFFSET(21650, glBufferData, glBufferData, NULL, 462),
    NAME_FUNC_OFFSET(21666, glBufferSubData, glBufferSubData, NULL, 463),
    NAME_FUNC_OFFSET(21685, glDeleteBuffers, glDeleteBuffers, NULL, 464),
    NAME_FUNC_OFFSET(21704, glDeleteQueries, glDeleteQueries, NULL, 465),
    NAME_FUNC_OFFSET(21723, glEndQuery, glEndQuery, NULL, 466),
    NAME_FUNC_OFFSET(21737, glGenBuffers, glGenBuffers, NULL, 467),
    NAME_FUNC_OFFSET(21753, glGenQueries, glGenQueries, NULL, 468),
    NAME_FUNC_OFFSET(21769, glGetBufferParameteriv, glGetBufferParameteriv, NULL, 469),
    NAME_FUNC_OFFSET(21795, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(21818, glGetBufferPointerv, glGetBufferPointerv, NULL, 470),
    NAME_FUNC_OFFSET(21841, glGetBufferSubData, glGetBufferSubData, NULL, 471),
    NAME_FUNC_OFFSET(21863, glGetQueryObjectiv, glGetQueryObjectiv, NULL, 472),
    NAME_FUNC_OFFSET(21885, glGetQueryObjectuiv, glGetQueryObjectuiv, NULL, 473),
    NAME_FUNC_OFFSET(21908, glGetQueryiv, glGetQueryiv, NULL, 474),
    NAME_FUNC_OFFSET(21924, glIsBuffer, glIsBuffer, NULL, 475),
    NAME_FUNC_OFFSET(21938, glIsQuery, glIsQuery, NULL, 476),
    NAME_FUNC_OFFSET(21951, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(21966, glMapBuffer, glMapBuffer, NULL, 477),
    NAME_FUNC_OFFSET(21981, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(21998, glUnmapBuffer, glUnmapBuffer, NULL, 478),
    NAME_FUNC_OFFSET(22015, glBindAttribLocation, glBindAttribLocation, NULL, 480),
    NAME_FUNC_OFFSET(22039, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22066, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22093, glBlendEquationSeparate, glBlendEquationSeparate, NULL, 481),
    NAME_FUNC_OFFSET(22120, glCompileShader, glCompileShader, NULL, 482),
    NAME_FUNC_OFFSET(22139, glDisableVertexAttribArray, glDisableVertexAttribArray, NULL, 488),
    NAME_FUNC_OFFSET(22169, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(22186, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(22203, glDrawBuffers, glDrawBuffers, NULL, 489),
    NAME_FUNC_OFFSET(22219, glEnableVertexAttribArray, glEnableVertexAttribArray, NULL, 490),
    NAME_FUNC_OFFSET(22248, glGetActiveAttrib, glGetActiveAttrib, NULL, 491),
    NAME_FUNC_OFFSET(22269, glGetActiveUniform, glGetActiveUniform, NULL, 492),
    NAME_FUNC_OFFSET(22291, glGetAttribLocation, glGetAttribLocation, NULL, 494),
    NAME_FUNC_OFFSET(22314, glGetShaderSource, glGetShaderSource, NULL, 498),
    NAME_FUNC_OFFSET(22335, glGetUniformLocation, glGetUniformLocation, NULL, 500),
    NAME_FUNC_OFFSET(22359, glGetUniformfv, glGetUniformfv, NULL, 501),
    NAME_FUNC_OFFSET(22377, glGetUniformiv, glGetUniformiv, NULL, 502),
    NAME_FUNC_OFFSET(22395, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(22424, glGetVertexAttribPointerv, glGetVertexAttribPointerv, NULL, 503),
    NAME_FUNC_OFFSET(22452, glGetVertexAttribdv, glGetVertexAttribdv, NULL, 504),
    NAME_FUNC_OFFSET(22475, glGetVertexAttribfv, glGetVertexAttribfv, NULL, 505),
    NAME_FUNC_OFFSET(22498, glGetVertexAttribiv, glGetVertexAttribiv, NULL, 506),
    NAME_FUNC_OFFSET(22521, glLinkProgram, glLinkProgram, NULL, 509),
    NAME_FUNC_OFFSET(22538, glShaderSource, glShaderSource, NULL, 510),
    NAME_FUNC_OFFSET(22556, glStencilOpSeparate, glStencilOpSeparate, NULL, 513),
    NAME_FUNC_OFFSET(22579, glUniform1f, glUniform1f, NULL, 514),
    NAME_FUNC_OFFSET(22594, glUniform1fv, glUniform1fv, NULL, 515),
    NAME_FUNC_OFFSET(22610, glUniform1i, glUniform1i, NULL, 516),
    NAME_FUNC_OFFSET(22625, glUniform1iv, glUniform1iv, NULL, 517),
    NAME_FUNC_OFFSET(22641, glUniform2f, glUniform2f, NULL, 518),
    NAME_FUNC_OFFSET(22656, glUniform2fv, glUniform2fv, NULL, 519),
    NAME_FUNC_OFFSET(22672, glUniform2i, glUniform2i, NULL, 520),
    NAME_FUNC_OFFSET(22687, glUniform2iv, glUniform2iv, NULL, 521),
    NAME_FUNC_OFFSET(22703, glUniform3f, glUniform3f, NULL, 522),
    NAME_FUNC_OFFSET(22718, glUniform3fv, glUniform3fv, NULL, 523),
    NAME_FUNC_OFFSET(22734, glUniform3i, glUniform3i, NULL, 524),
    NAME_FUNC_OFFSET(22749, glUniform3iv, glUniform3iv, NULL, 525),
    NAME_FUNC_OFFSET(22765, glUniform4f, glUniform4f, NULL, 526),
    NAME_FUNC_OFFSET(22780, glUniform4fv, glUniform4fv, NULL, 527),
    NAME_FUNC_OFFSET(22796, glUniform4i, glUniform4i, NULL, 528),
    NAME_FUNC_OFFSET(22811, glUniform4iv, glUniform4iv, NULL, 529),
    NAME_FUNC_OFFSET(22827, glUniformMatrix2fv, glUniformMatrix2fv, NULL, 530),
    NAME_FUNC_OFFSET(22849, glUniformMatrix3fv, glUniformMatrix3fv, NULL, 531),
    NAME_FUNC_OFFSET(22871, glUniformMatrix4fv, glUniformMatrix4fv, NULL, 532),
    NAME_FUNC_OFFSET(22893, glUseProgram, glUseProgram, NULL, 533),
    NAME_FUNC_OFFSET(22915, glValidateProgram, glValidateProgram, NULL, 534),
    NAME_FUNC_OFFSET(22936, glVertexAttrib1d, glVertexAttrib1d, NULL, 535),
    NAME_FUNC_OFFSET(22956, glVertexAttrib1dv, glVertexAttrib1dv, NULL, 536),
    NAME_FUNC_OFFSET(22977, glVertexAttrib1s, glVertexAttrib1s, NULL, 537),
    NAME_FUNC_OFFSET(22997, glVertexAttrib1sv, glVertexAttrib1sv, NULL, 538),
    NAME_FUNC_OFFSET(23018, glVertexAttrib2d, glVertexAttrib2d, NULL, 539),
    NAME_FUNC_OFFSET(23038, glVertexAttrib2dv, glVertexAttrib2dv, NULL, 540),
    NAME_FUNC_OFFSET(23059, glVertexAttrib2s, glVertexAttrib2s, NULL, 541),
    NAME_FUNC_OFFSET(23079, glVertexAttrib2sv, glVertexAttrib2sv, NULL, 542),
    NAME_FUNC_OFFSET(23100, glVertexAttrib3d, glVertexAttrib3d, NULL, 543),
    NAME_FUNC_OFFSET(23120, glVertexAttrib3dv, glVertexAttrib3dv, NULL, 544),
    NAME_FUNC_OFFSET(23141, glVertexAttrib3s, glVertexAttrib3s, NULL, 545),
    NAME_FUNC_OFFSET(23161, glVertexAttrib3sv, glVertexAttrib3sv, NULL, 546),
    NAME_FUNC_OFFSET(23182, glVertexAttrib4Nbv, glVertexAttrib4Nbv, NULL, 547),
    NAME_FUNC_OFFSET(23204, glVertexAttrib4Niv, glVertexAttrib4Niv, NULL, 548),
    NAME_FUNC_OFFSET(23226, glVertexAttrib4Nsv, glVertexAttrib4Nsv, NULL, 549),
    NAME_FUNC_OFFSET(23248, glVertexAttrib4Nub, glVertexAttrib4Nub, NULL, 550),
    NAME_FUNC_OFFSET(23270, glVertexAttrib4Nubv, glVertexAttrib4Nubv, NULL, 551),
    NAME_FUNC_OFFSET(23293, glVertexAttrib4Nuiv, glVertexAttrib4Nuiv, NULL, 552),
    NAME_FUNC_OFFSET(23316, glVertexAttrib4Nusv, glVertexAttrib4Nusv, NULL, 553),
    NAME_FUNC_OFFSET(23339, glVertexAttrib4bv, glVertexAttrib4bv, NULL, 554),
    NAME_FUNC_OFFSET(23360, glVertexAttrib4d, glVertexAttrib4d, NULL, 555),
    NAME_FUNC_OFFSET(23380, glVertexAttrib4dv, glVertexAttrib4dv, NULL, 556),
    NAME_FUNC_OFFSET(23401, glVertexAttrib4iv, glVertexAttrib4iv, NULL, 557),
    NAME_FUNC_OFFSET(23422, glVertexAttrib4s, glVertexAttrib4s, NULL, 558),
    NAME_FUNC_OFFSET(23442, glVertexAttrib4sv, glVertexAttrib4sv, NULL, 559),
    NAME_FUNC_OFFSET(23463, glVertexAttrib4ubv, glVertexAttrib4ubv, NULL, 560),
    NAME_FUNC_OFFSET(23485, glVertexAttrib4uiv, glVertexAttrib4uiv, NULL, 561),
    NAME_FUNC_OFFSET(23507, glVertexAttrib4usv, glVertexAttrib4usv, NULL, 562),
    NAME_FUNC_OFFSET(23529, glVertexAttribPointer, glVertexAttribPointer, NULL, 563),
    NAME_FUNC_OFFSET(23554, glBeginConditionalRender, glBeginConditionalRender, NULL, 570),
    NAME_FUNC_OFFSET(23581, glBeginTransformFeedback, glBeginTransformFeedback, NULL, 571),
    NAME_FUNC_OFFSET(23609, glBindBufferBase, glBindBufferBase, NULL, 572),
    NAME_FUNC_OFFSET(23629, glBindBufferRange, glBindBufferRange, NULL, 573),
    NAME_FUNC_OFFSET(23650, glBindFragDataLocation, glBindFragDataLocation, NULL, 574),
    NAME_FUNC_OFFSET(23676, glClampColor, glClampColor, NULL, 575),
    NAME_FUNC_OFFSET(23692, glColorMaski, glColorMaski, NULL, 580),
    NAME_FUNC_OFFSET(23714, glDisablei, glDisablei, NULL, 581),
    NAME_FUNC_OFFSET(23734, glEnablei, glEnablei, NULL, 582),
    NAME_FUNC_OFFSET(23753, glEndConditionalRender, glEndConditionalRender, NULL, 583),
    NAME_FUNC_OFFSET(23778, glEndTransformFeedback, glEndTransformFeedback, NULL, 584),
    NAME_FUNC_OFFSET(23804, glGetBooleani_v, glGetBooleani_v, NULL, 585),
    NAME_FUNC_OFFSET(23828, glGetFragDataLocation, glGetFragDataLocation, NULL, 586),
    NAME_FUNC_OFFSET(23853, glGetIntegeri_v, glGetIntegeri_v, NULL, 587),
    NAME_FUNC_OFFSET(23877, glGetTexParameterIiv, glGetTexParameterIiv, NULL, 589),
    NAME_FUNC_OFFSET(23901, glGetTexParameterIuiv, glGetTexParameterIuiv, NULL, 590),
    NAME_FUNC_OFFSET(23926, glGetTransformFeedbackVarying, glGetTransformFeedbackVarying, NULL, 591),
    NAME_FUNC_OFFSET(23959, glGetUniformuiv, glGetUniformuiv, NULL, 592),
    NAME_FUNC_OFFSET(23978, glGetVertexAttribIiv, glGetVertexAttribIiv, NULL, 593),
    NAME_FUNC_OFFSET(24002, glGetVertexAttribIuiv, glGetVertexAttribIuiv, NULL, 594),
    NAME_FUNC_OFFSET(24027, glIsEnabledi, glIsEnabledi, NULL, 595),
    NAME_FUNC_OFFSET(24049, glTexParameterIiv, glTexParameterIiv, NULL, 596),
    NAME_FUNC_OFFSET(24070, glTexParameterIuiv, glTexParameterIuiv, NULL, 597),
    NAME_FUNC_OFFSET(24092, glTransformFeedbackVaryings, glTransformFeedbackVaryings, NULL, 598),
    NAME_FUNC_OFFSET(24123, glUniform1ui, glUniform1ui, NULL, 599),
    NAME_FUNC_OFFSET(24139, glUniform1uiv, glUniform1uiv, NULL, 600),
    NAME_FUNC_OFFSET(24156, glUniform2ui, glUniform2ui, NULL, 601),
    NAME_FUNC_OFFSET(24172, glUniform2uiv, glUniform2uiv, NULL, 602),
    NAME_FUNC_OFFSET(24189, glUniform3ui, glUniform3ui, NULL, 603),
    NAME_FUNC_OFFSET(24205, glUniform3uiv, glUniform3uiv, NULL, 604),
    NAME_FUNC_OFFSET(24222, glUniform4ui, glUniform4ui, NULL, 605),
    NAME_FUNC_OFFSET(24238, glUniform4uiv, glUniform4uiv, NULL, 606),
    NAME_FUNC_OFFSET(24255, glVertexAttribI1iv, glVertexAttribI1iv, NULL, 607),
    NAME_FUNC_OFFSET(24277, glVertexAttribI1uiv, glVertexAttribI1uiv, NULL, 608),
    NAME_FUNC_OFFSET(24300, glVertexAttribI4bv, glVertexAttribI4bv, NULL, 609),
    NAME_FUNC_OFFSET(24322, glVertexAttribI4sv, glVertexAttribI4sv, NULL, 610),
    NAME_FUNC_OFFSET(24344, glVertexAttribI4ubv, glVertexAttribI4ubv, NULL, 611),
    NAME_FUNC_OFFSET(24367, glVertexAttribI4usv, glVertexAttribI4usv, NULL, 612),
    NAME_FUNC_OFFSET(24390, glVertexAttribIPointer, glVertexAttribIPointer, NULL, 613),
    NAME_FUNC_OFFSET(24416, glPrimitiveRestartIndex, glPrimitiveRestartIndex, NULL, 614),
    NAME_FUNC_OFFSET(24442, glTexBuffer, glTexBuffer, NULL, 615),
    NAME_FUNC_OFFSET(24457, glFramebufferTexture, glFramebufferTexture, NULL, 616),
    NAME_FUNC_OFFSET(24481, glVertexAttribDivisor, glVertexAttribDivisor, NULL, 619),
    NAME_FUNC_OFFSET(24506, glBindProgramARB, glBindProgramARB, NULL, 620),
    NAME_FUNC_OFFSET(24522, glDeleteProgramsARB, glDeleteProgramsARB, NULL, 621),
    NAME_FUNC_OFFSET(24541, glGenProgramsARB, glGenProgramsARB, NULL, 622),
    NAME_FUNC_OFFSET(24557, glIsProgramARB, glIsProgramARB, NULL, 629),
    NAME_FUNC_OFFSET(24571, glProgramEnvParameter4dARB, glProgramEnvParameter4dARB, NULL, 630),
    NAME_FUNC_OFFSET(24594, glProgramEnvParameter4dvARB, glProgramEnvParameter4dvARB, NULL, 631),
    NAME_FUNC_OFFSET(24618, glProgramEnvParameter4fARB, glProgramEnvParameter4fARB, NULL, 632),
    NAME_FUNC_OFFSET(24641, glProgramEnvParameter4fvARB, glProgramEnvParameter4fvARB, NULL, 633),
    NAME_FUNC_OFFSET(24665, glVertexAttrib1fARB, glVertexAttrib1fARB, NULL, 639),
    NAME_FUNC_OFFSET(24682, glVertexAttrib1fvARB, glVertexAttrib1fvARB, NULL, 640),
    NAME_FUNC_OFFSET(24700, glVertexAttrib2fARB, glVertexAttrib2fARB, NULL, 641),
    NAME_FUNC_OFFSET(24717, glVertexAttrib2fvARB, glVertexAttrib2fvARB, NULL, 642),
    NAME_FUNC_OFFSET(24735, glVertexAttrib3fARB, glVertexAttrib3fARB, NULL, 643),
    NAME_FUNC_OFFSET(24752, glVertexAttrib3fvARB, glVertexAttrib3fvARB, NULL, 644),
    NAME_FUNC_OFFSET(24770, glVertexAttrib4fARB, glVertexAttrib4fARB, NULL, 645),
    NAME_FUNC_OFFSET(24787, glVertexAttrib4fvARB, glVertexAttrib4fvARB, NULL, 646),
    NAME_FUNC_OFFSET(24805, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 657),
    NAME_FUNC_OFFSET(24830, glDrawArraysInstancedARB, glDrawArraysInstancedARB, NULL, 657),
    NAME_FUNC_OFFSET(24852, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(24879, glDrawElementsInstancedARB, glDrawElementsInstancedARB, NULL, 658),
    NAME_FUNC_OFFSET(24903, glBindFramebuffer, glBindFramebuffer, NULL, 659),
    NAME_FUNC_OFFSET(24924, glBindFramebuffer, glBindFramebuffer, NULL, 659),
    NAME_FUNC_OFFSET(24945, glBindRenderbuffer, glBindRenderbuffer, NULL, 660),
    NAME_FUNC_OFFSET(24967, glBindRenderbuffer, glBindRenderbuffer, NULL, 660),
    NAME_FUNC_OFFSET(24989, glBlitFramebuffer, glBlitFramebuffer, NULL, 661),
    NAME_FUNC_OFFSET(25010, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 662),
    NAME_FUNC_OFFSET(25038, glCheckFramebufferStatus, glCheckFramebufferStatus, NULL, 662),
    NAME_FUNC_OFFSET(25066, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 663),
    NAME_FUNC_OFFSET(25090, glDeleteFramebuffers, glDeleteFramebuffers, NULL, 663),
    NAME_FUNC_OFFSET(25114, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 664),
    NAME_FUNC_OFFSET(25139, glDeleteRenderbuffers, glDeleteRenderbuffers, NULL, 664),
    NAME_FUNC_OFFSET(25164, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 665),
    NAME_FUNC_OFFSET(25193, glFramebufferRenderbuffer, glFramebufferRenderbuffer, NULL, 665),
    NAME_FUNC_OFFSET(25222, glFramebufferTexture1D, glFramebufferTexture1D, NULL, 666),
    NAME_FUNC_OFFSET(25248, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 667),
    NAME_FUNC_OFFSET(25274, glFramebufferTexture2D, glFramebufferTexture2D, NULL, 667),
    NAME_FUNC_OFFSET(25300, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 668),
    NAME_FUNC_OFFSET(25326, glFramebufferTexture3D, glFramebufferTexture3D, NULL, 668),
    NAME_FUNC_OFFSET(25352, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 669),
    NAME_FUNC_OFFSET(25381, glFramebufferTextureLayer, glFramebufferTextureLayer, NULL, 669),
    NAME_FUNC_OFFSET(25410, glGenFramebuffers, glGenFramebuffers, NULL, 670),
    NAME_FUNC_OFFSET(25431, glGenFramebuffers, glGenFramebuffers, NULL, 670),
    NAME_FUNC_OFFSET(25452, glGenRenderbuffers, glGenRenderbuffers, NULL, 671),
    NAME_FUNC_OFFSET(25474, glGenRenderbuffers, glGenRenderbuffers, NULL, 671),
    NAME_FUNC_OFFSET(25496, glGenerateMipmap, glGenerateMipmap, NULL, 672),
    NAME_FUNC_OFFSET(25516, glGenerateMipmap, glGenerateMipmap, NULL, 672),
    NAME_FUNC_OFFSET(25536, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 673),
    NAME_FUNC_OFFSET(25577, glGetFramebufferAttachmentParameteriv, glGetFramebufferAttachmentParameteriv, NULL, 673),
    NAME_FUNC_OFFSET(25618, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(25650, glGetRenderbufferParameteriv, glGetRenderbufferParameteriv, NULL, 674),
    NAME_FUNC_OFFSET(25682, glIsFramebuffer, glIsFramebuffer, NULL, 675),
    NAME_FUNC_OFFSET(25701, glIsFramebuffer, glIsFramebuffer, NULL, 675),
    NAME_FUNC_OFFSET(25720, glIsRenderbuffer, glIsRenderbuffer, NULL, 676),
    NAME_FUNC_OFFSET(25740, glIsRenderbuffer, glIsRenderbuffer, NULL, 676),
    NAME_FUNC_OFFSET(25760, glRenderbufferStorage, glRenderbufferStorage, NULL, 677),
    NAME_FUNC_OFFSET(25785, glRenderbufferStorage, glRenderbufferStorage, NULL, 677),
    NAME_FUNC_OFFSET(25810, glRenderbufferStorageMultisample, glRenderbufferStorageMultisample, NULL, 678),
    NAME_FUNC_OFFSET(25846, glFlushMappedBufferRange, glFlushMappedBufferRange, NULL, 680),
    NAME_FUNC_OFFSET(25874, glMapBufferRange, glMapBufferRange, NULL, 681),
    NAME_FUNC_OFFSET(25894, glBindVertexArray, glBindVertexArray, NULL, 682),
    NAME_FUNC_OFFSET(25915, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 683),
    NAME_FUNC_OFFSET(25941, glDeleteVertexArrays, glDeleteVertexArrays, NULL, 683),
    NAME_FUNC_OFFSET(25965, glGenVertexArrays, glGenVertexArrays, NULL, 684),
    NAME_FUNC_OFFSET(25986, glIsVertexArray, glIsVertexArray, NULL, 685),
    NAME_FUNC_OFFSET(26007, glIsVertexArray, glIsVertexArray, NULL, 685),
    NAME_FUNC_OFFSET(26026, glProvokingVertex, glProvokingVertex, NULL, 705),
    NAME_FUNC_OFFSET(26047, glBlendEquationSeparateiARB, glBlendEquationSeparateiARB, NULL, 710),
    NAME_FUNC_OFFSET(26081, glBlendEquationiARB, glBlendEquationiARB, NULL, 711),
    NAME_FUNC_OFFSET(26107, glBlendFuncSeparateiARB, glBlendFuncSeparateiARB, NULL, 712),
    NAME_FUNC_OFFSET(26137, glBlendFunciARB, glBlendFunciARB, NULL, 713),
    NAME_FUNC_OFFSET(26159, gl_dispatch_stub_730, gl_dispatch_stub_730, NULL, 730),
    NAME_FUNC_OFFSET(26183, gl_dispatch_stub_731, gl_dispatch_stub_731, NULL, 731),
    NAME_FUNC_OFFSET(26208, glClearDepthf, glClearDepthf, NULL, 782),
    NAME_FUNC_OFFSET(26225, glDepthRangef, glDepthRangef, NULL, 783),
    NAME_FUNC_OFFSET(26242, glGetProgramBinary, glGetProgramBinary, NULL, 787),
    NAME_FUNC_OFFSET(26264, glProgramBinary, glProgramBinary, NULL, 788),
    NAME_FUNC_OFFSET(26283, glProgramParameteri, glProgramParameteri, NULL, 789),
    NAME_FUNC_OFFSET(26306, gl_dispatch_stub_844, gl_dispatch_stub_844, NULL, 844),
    NAME_FUNC_OFFSET(26322, gl_dispatch_stub_845, gl_dispatch_stub_845, NULL, 845),
    NAME_FUNC_OFFSET(26341, glSecondaryColor3fEXT, glSecondaryColor3fEXT, NULL, 855),
    NAME_FUNC_OFFSET(26360, glSecondaryColor3fvEXT, glSecondaryColor3fvEXT, NULL, 856),
    NAME_FUNC_OFFSET(26380, glMultiDrawElementsEXT, glMultiDrawElementsEXT, NULL, 857),
    NAME_FUNC_OFFSET(26400, glFogCoordfEXT, glFogCoordfEXT, NULL, 858),
    NAME_FUNC_OFFSET(26412, glFogCoordfvEXT, glFogCoordfvEXT, NULL, 859),
    NAME_FUNC_OFFSET(26425, glVertexAttribI1iEXT, glVertexAttribI1iEXT, NULL, 960),
    NAME_FUNC_OFFSET(26443, glVertexAttribI1uiEXT, glVertexAttribI1uiEXT, NULL, 961),
    NAME_FUNC_OFFSET(26462, glVertexAttribI2iEXT, glVertexAttribI2iEXT, NULL, 962),
    NAME_FUNC_OFFSET(26480, glVertexAttribI2ivEXT, glVertexAttribI2ivEXT, NULL, 963),
    NAME_FUNC_OFFSET(26499, glVertexAttribI2uiEXT, glVertexAttribI2uiEXT, NULL, 964),
    NAME_FUNC_OFFSET(26518, glVertexAttribI2uivEXT, glVertexAttribI2uivEXT, NULL, 965),
    NAME_FUNC_OFFSET(26538, glVertexAttribI3iEXT, glVertexAttribI3iEXT, NULL, 966),
    NAME_FUNC_OFFSET(26556, glVertexAttribI3ivEXT, glVertexAttribI3ivEXT, NULL, 967),
    NAME_FUNC_OFFSET(26575, glVertexAttribI3uiEXT, glVertexAttribI3uiEXT, NULL, 968),
    NAME_FUNC_OFFSET(26594, glVertexAttribI3uivEXT, glVertexAttribI3uivEXT, NULL, 969),
    NAME_FUNC_OFFSET(26614, glVertexAttribI4iEXT, glVertexAttribI4iEXT, NULL, 970),
    NAME_FUNC_OFFSET(26632, glVertexAttribI4ivEXT, glVertexAttribI4ivEXT, NULL, 971),
    NAME_FUNC_OFFSET(26651, glVertexAttribI4uiEXT, glVertexAttribI4uiEXT, NULL, 972),
    NAME_FUNC_OFFSET(26670, glVertexAttribI4uivEXT, glVertexAttribI4uivEXT, NULL, 973),
    NAME_FUNC_OFFSET(26690, gl_dispatch_stub_989, gl_dispatch_stub_989, NULL, 989),
    NAME_FUNC_OFFSET(26706, gl_dispatch_stub_990, gl_dispatch_stub_990, NULL, 990),
    NAME_FUNC_OFFSET(26723, gl_dispatch_stub_991, gl_dispatch_stub_991, NULL, 991),
    NAME_FUNC_OFFSET(26740, gl_dispatch_stub_992, gl_dispatch_stub_992, NULL, 992),
    NAME_FUNC_OFFSET(26753, gl_dispatch_stub_993, gl_dispatch_stub_993, NULL, 993),
    NAME_FUNC_OFFSET(26770, gl_dispatch_stub_994, gl_dispatch_stub_994, NULL, 994),
    NAME_FUNC_OFFSET(26780, gl_dispatch_stub_995, gl_dispatch_stub_995, NULL, 995),
    NAME_FUNC_OFFSET(26791, gl_dispatch_stub_996, gl_dispatch_stub_996, NULL, 996),
    NAME_FUNC_OFFSET(26805, gl_dispatch_stub_997, gl_dispatch_stub_997, NULL, 997),
    NAME_FUNC_OFFSET(26819, gl_dispatch_stub_998, gl_dispatch_stub_998, NULL, 998),
    NAME_FUNC_OFFSET(26836, gl_dispatch_stub_999, gl_dispatch_stub_999, NULL, 999),
    NAME_FUNC_OFFSET(26854, gl_dispatch_stub_1000, gl_dispatch_stub_1000, NULL, 1000),
    NAME_FUNC_OFFSET(26866, gl_dispatch_stub_1001, gl_dispatch_stub_1001, NULL, 1001),
    NAME_FUNC_OFFSET(26879, gl_dispatch_stub_1002, gl_dispatch_stub_1002, NULL, 1002),
    NAME_FUNC_OFFSET(26895, gl_dispatch_stub_1003, gl_dispatch_stub_1003, NULL, 1003),
    NAME_FUNC_OFFSET(26912, gl_dispatch_stub_1004, gl_dispatch_stub_1004, NULL, 1004),
    NAME_FUNC_OFFSET(26927, gl_dispatch_stub_1005, gl_dispatch_stub_1005, NULL, 1005),
    NAME_FUNC_OFFSET(26943, gl_dispatch_stub_1006, gl_dispatch_stub_1006, NULL, 1006),
    NAME_FUNC_OFFSET(26960, gl_dispatch_stub_1007, gl_dispatch_stub_1007, NULL, 1007),
    NAME_FUNC_OFFSET(26981, gl_dispatch_stub_1008, gl_dispatch_stub_1008, NULL, 1008),
    NAME_FUNC_OFFSET(26995, gl_dispatch_stub_1009, gl_dispatch_stub_1009, NULL, 1009),
    NAME_FUNC_OFFSET(27007, gl_dispatch_stub_1010, gl_dispatch_stub_1010, NULL, 1010),
    NAME_FUNC_OFFSET(27019, gl_dispatch_stub_1011, gl_dispatch_stub_1011, NULL, 1011),
    NAME_FUNC_OFFSET(27035, gl_dispatch_stub_1012, gl_dispatch_stub_1012, NULL, 1012),
    NAME_FUNC_OFFSET(27055, gl_dispatch_stub_1013, gl_dispatch_stub_1013, NULL, 1013),
    NAME_FUNC_OFFSET(27068, gl_dispatch_stub_1014, gl_dispatch_stub_1014, NULL, 1014),
    NAME_FUNC_OFFSET(27089, gl_dispatch_stub_1015, gl_dispatch_stub_1015, NULL, 1015),
    NAME_FUNC_OFFSET(27101, gl_dispatch_stub_1016, gl_dispatch_stub_1016, NULL, 1016),
    NAME_FUNC_OFFSET(27114, gl_dispatch_stub_1017, gl_dispatch_stub_1017, NULL, 1017),
    NAME_FUNC_OFFSET(27128, gl_dispatch_stub_1018, gl_dispatch_stub_1018, NULL, 1018),
    NAME_FUNC_OFFSET(27147, gl_dispatch_stub_1019, gl_dispatch_stub_1019, NULL, 1019),
    NAME_FUNC_OFFSET(27163, gl_dispatch_stub_1020, gl_dispatch_stub_1020, NULL, 1020),
    NAME_FUNC_OFFSET(27179, gl_dispatch_stub_1021, gl_dispatch_stub_1021, NULL, 1021),
    NAME_FUNC_OFFSET(27195, gl_dispatch_stub_1022, gl_dispatch_stub_1022, NULL, 1022),
    NAME_FUNC_OFFSET(27214, gl_dispatch_stub_1023, gl_dispatch_stub_1023, NULL, 1023),
    NAME_FUNC_OFFSET(27233, gl_dispatch_stub_1024, gl_dispatch_stub_1024, NULL, 1024),
    NAME_FUNC_OFFSET(27248, gl_dispatch_stub_1025, gl_dispatch_stub_1025, NULL, 1025),
    NAME_FUNC_OFFSET(27264, gl_dispatch_stub_1026, gl_dispatch_stub_1026, NULL, 1026),
    NAME_FUNC_OFFSET(27283, gl_dispatch_stub_1027, gl_dispatch_stub_1027, NULL, 1027),
    NAME_FUNC_OFFSET(27300, gl_dispatch_stub_1028, gl_dispatch_stub_1028, NULL, 1028),
    NAME_FUNC_OFFSET(27323, gl_dispatch_stub_1029, gl_dispatch_stub_1029, NULL, 1029),
    NAME_FUNC_OFFSET(27344, gl_dispatch_stub_1030, gl_dispatch_stub_1030, NULL, 1030),
    NAME_FUNC_OFFSET(27366, gl_dispatch_stub_1031, gl_dispatch_stub_1031, NULL, 1031),
    NAME_FUNC_OFFSET(-1, NULL, NULL, NULL, 0)
};

#undef NAME_FUNC_OFFSET
